#!/usr/bin/env python3
"""Prepare local FF3 inputs and build the native host.

macOS builds the interactive player. Linux and Windows build the headless probe.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

import ff3
from disc_image import DiscImage, check_disc
from prepare_font import SOURCE_SHA256, prepare as prepare_font

ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / 'profiles/ff3'
PRXTOOL_REVISION = '08849d87fa8692f2ba29238ec1710f2706e3e021'


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def run(command, cwd=ROOT):
    print('+ ' + ' '.join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], cwd=cwd, check=True)


def tool(name):
    value = shutil.which(name)
    if not value:
        raise ValueError('Missing tool: ' + name + '. See profiles/ff3/QUICKSTART.md.')
    return value


def copy_identical(source, destination):
    if destination.exists():
        if digest(source) != digest(destination):
            raise ValueError('Refusing to overwrite different local input: ' + str(destination))
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def prepare_inputs(elf, font, iso, game):
    config = ff3.read_config(PROFILE / 'config/npuh10125.json')
    # Validate all supplied inputs before touching the managed game directory.
    with tempfile.TemporaryDirectory(prefix='ff3-input-check-') as temporary:
        ff3.prepare_input(elf, Path(temporary) / 'EBOOT.ELF', config)
    if digest(font) != SOURCE_SHA256:
        raise ValueError('Font hash mismatch: supply the decrypted NPUH10125 libfont PRX dump, not the encrypted file from the ISO')
    image = DiscImage(iso)
    identity = check_disc(image)
    game.mkdir(parents=True, exist_ok=True)
    ff3.prepare_input(elf, game / 'EBOOT.ELF', config)
    copy_identical(font, game / 'libfont.elf')
    disc = game / 'disc'
    manifest = game / 'disc-source.json'
    iso_hash = digest(iso)
    if disc.exists():
        if not manifest.is_file() or json.loads(manifest.read_text()).get('sha256') != iso_hash:
            raise ValueError('Existing disc directory has no matching setup manifest. Keep it intact; use a fresh checkout for setup or follow the manual instructions.')
        # Catch missing/partial extraction without hashing hundreds of MB on every rerun.
        for name, (_, size) in image.files.items():
            file = disc / name
            if not file.is_file() or file.stat().st_size != size:
                raise ValueError('Extracted disc file missing or wrong size: ' + name)
    else:
        with tempfile.TemporaryDirectory(prefix='.disc-prepare-', dir=game) as temporary:
            staged = Path(temporary) / 'disc'
            image.extract(staged)
            staged.rename(disc)
        manifest.write_text(json.dumps({'sha256': iso_hash, 'identity': identity,
                                       'files': len(image.files)}, indent=2) + '\n')
    link = game / 'disc.iso'
    if link.exists() or link.is_symlink():
        if not link.exists() or digest(link) != iso_hash:
            raise ValueError('Refusing to replace a different or broken disc.iso; preserve it and resolve the input path first')
    else:
        link.symlink_to(iso.resolve())
    print('Prepared matching game, font, extracted disc and raw ISO access.', flush=True)


def host_compiler():
    if platform.system() == 'Darwin':
        return tool('clang++')
    for name in ('g++', 'clang++', 'c++'):
        found = shutil.which(name)
        if found:
            return found
    raise ValueError('Missing a C++ compiler. See profiles/ff3/WINDOWS_LINUX.md.')


def relocator(output, compiler):
    source = output / 'prxtool'
    git = tool('git')
    if not source.exists():
        run([git, 'clone', '--no-checkout', 'https://github.com/pspdev/prxtool.git', source])
        run([git, 'checkout', '--detach', PRXTOOL_REVISION], cwd=source)
    revision = subprocess.check_output([git, 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    if revision != PRXTOOL_REVISION:
        raise ValueError('Unexpected PRXTool revision at ' + str(source))
    binary = output / 'dump_relocations'
    files = ['ProcessElf.C', 'ProcessPrx.C', 'NidMgr.C', 'VirtualMem.C', 'output.C', 'disasm.C', 'pspkerror.C']
    warnings = ['-Wno-deprecated']
    if 'clang' in Path(compiler).name.lower():
        warnings.append('-Wno-writable-strings')
    else:
        warnings.append('-Wno-write-strings')
    run([compiler, '-std=c++11', *warnings, '-I.', '-iquote', 'tinyxml',
         PROFILE / 'tools/prxtool_dump_relocations.cpp', *files,
         *sorted(Path('tinyxml').joinpath(p.name) for p in (source / 'tinyxml').glob('*.cpp')),
         '-o', binary], cwd=source)
    return binary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path, help='Decrypted NPUH10125 EBOOT dump (.BIN or .ELF)')
    parser.add_argument('--font', required=True, type=Path, help='Decrypted NPUH10125 libfont PRX dump')
    parser.add_argument('--iso', required=True, type=Path, help='Matching uncompressed NPUH10125 v1.00 ISO; keep it at this path')
    parser.add_argument('--prepare-only', action='store_true', help='Prepare local inputs without downloading/building dependencies')
    parser.add_argument('--cmake', default='cmake', help='CMake executable (3.20+)')
    parser.add_argument('--relocator', type=Path, help='Existing dump_relocations helper; otherwise build pinned external PRXTool')
    args = parser.parse_args(argv)
    try:
        window = platform.system() == 'Darwin'
        cmake = None
        compiler = None
        if not args.prepare_only:
            cmake = tool(args.cmake)
            tool('git')
            tool('make')
            compiler = host_compiler()
            if platform.system() == 'Windows':
                for name in ('bash', 'ninja'):
                    tool(name)
        prepare_inputs(args.elf.resolve(), args.font.resolve(), args.iso.resolve(), PROFILE / 'game')
        if args.prepare_only:
            return 0
        build = ROOT / 'out/ff3'
        config = PROFILE / 'config/npuh10125.json'
        configure = [cmake, '-S', ROOT, '-B', build, '-DPSPRECOMP_PROFILE=ff3', '-DCMAKE_BUILD_TYPE=Release']
        if platform.system() == 'Windows':
            configure.extend(['-G', 'Ninja', '-DCMAKE_CXX_COMPILER=' + compiler, '-DCMAKE_C_COMPILER=' + compiler.replace('g++', 'gcc').replace('clang++', 'clang')])
        run([*configure,
             '-DFF3_CONFIG=' + str(config), '-DFF3_BUILD_GENERATED=OFF', '-DFF3_BUILD_WINDOW=OFF',
             '-DFF3_ELF=' + str(PROFILE / 'game/EBOOT.ELF'),
             '-DFF3_ANALYSIS_DIR=' + str(PROFILE / 'analysis'),
             '-DPSPRECOMP_BUILD_TESTS=ON', '-DPSPRECOMP_BUILD_PROFILE_TESTS=ON'])
        run([cmake, '--build', build, '--target', 'psp_analyze', 'psp_recomp', '--parallel', '1'])
        run([cmake, '--build', build, '--target', 'ff3_generate', '--parallel', '1'])
        dependency = ROOT / 'out/ff3-setup'
        dependency.mkdir(parents=True, exist_ok=True)
        helper = args.relocator.resolve() if args.relocator else relocator(dependency, compiler)
        prepare_font(PROFILE / 'game/libfont.elf', helper, build / 'psp_recomp',
                     PROFILE / 'game/libfont.fixed.elf', PROFILE / 'analysis/font_generated')
        run([sys.executable, PROFILE / 'tools/build_media.py', ROOT / 'out/media'])
        run([*configure, '-DFF3_CONFIG=' + str(config), '-DFF3_BUILD_GENERATED=ON', '-DFF3_BUILD_FONT=ON',
             '-DFF3_BUILD_WINDOW=' + ('ON' if window else 'OFF'), '-DPSPRECOMP_GENERATED_OPT_LEVEL=0',
             '-DFF3_FFMPEG_ROOT=' + str(ROOT / 'out/media/install')])
        run([cmake, '--build', build, '--parallel', '1'])
        ctest = Path(cmake).resolve().with_name('ctest')
        run([ctest, '--test-dir', build, '--output-on-failure'])
        if window:
            run([sys.executable, PROFILE / 'tools/play.py', '--check'])
            print('\nReady. Launch with: python3 profiles/ff3/tools/play.py')
        else:
            from preflight import check_runtime
            probe = build / 'profiles/ff3' / ('ff3_probe.exe' if platform.system() == 'Windows' else 'ff3_probe')
            errors = check_runtime(PROFILE / 'game/EBOOT.ELF', PROFILE / 'game/disc.iso', probe)
            if errors:
                raise ValueError('FF3 is not ready:\n- ' + '\n- '.join(errors))
            print('\nHeadless probe ready: ' + str(probe))
            print('Launch a bounded diagnostic with:')
            print('  ' + str(probe) + ' ' + str(PROFILE / 'game/EBOOT.ELF') + ' 64 bootstrap')
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print('FF3 setup: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
