#!/usr/bin/env python3
"""Local FF3 input validation and PSPRecomp analysis/code-generation driver."""

import argparse
import filecmp
import hashlib
import json
import os
import time
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PROFILE = Path(__file__).resolve().parents[1]


def number(value):
    if isinstance(value, str):
        return int(value, 0)
    if type(value) is int:
        return value
    raise ValueError("Expected an integer or a string containing an integer")


def read_config(path):
    config = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("FF3 profile configuration must be a JSON object")
    if config.get("schema_version") != 1:
        raise ValueError("Unsupported FF3 profile schema_version")
    for key in ("load_base", "unit_span_bytes"):
        value = number(config[key])
        if not 0 <= value <= 0xFFFFFFFF or value % 4:
            raise ValueError(f"{key} must be an aligned unsigned 32-bit value")
        config[key] = value
    if config["unit_span_bytes"] == 0:
        raise ValueError("unit_span_bytes must be nonzero")
    expected = config.get("expected_sha256")
    if expected is not None and not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        raise ValueError("expected_sha256 must be null or a 64-digit SHA-256")
    if "expected_size_bytes" in config:
        size = number(config["expected_size_bytes"])
        if not 52 <= size <= 0xFFFFFFFF:
            raise ValueError("expected_size_bytes must be a valid ELF file size")
        config["expected_size_bytes"] = size
    return config


def inspect_elf(path, expected_sha256=None):
    """Check the container and ELF structure before invoking native tools."""
    with path.open("rb") as stream:
        header = stream.read(52)
        if header[:4] == b"~PSP":
            raise ValueError("Encrypted/compressed ~PSP input; prepare a decrypted ELF/PRX outside PSPRecomp")
        if header[:4] == b"\0PBP":
            raise ValueError("PBP container; supply its decrypted executable as an ELF/PRX")
        if header[:4] != b"\x7fELF":
            raise ValueError("Expected a decrypted ELF/PRX, not an ISO, asset, or other container")
        if len(header) != 52 or header[4:7] != b"\x01\x01\x01":
            raise ValueError("Expected a complete ELF32 little-endian version 1 header")
        fields = struct.unpack("<16sHHIIIIIHHHHHH", header)
        _, kind, machine, version, entry, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, shstr = fields
        if machine != 8 or version != 1 or kind not in (2, 0xFFA0):
            raise ValueError("Expected a MIPS executable (ET_EXEC) or PSP PRX (0xFFA0)")
        size = path.stat().st_size
        if ehsize != 52 or not phnum or phsize < 32 or phoff < 52 or phoff + phnum * phsize > size:
            raise ValueError("Invalid or truncated ELF program-header table")
        if shnum and (shsize < 40 or shoff < 52 or shoff + shnum * shsize > size or shstr >= shnum):
            raise ValueError("Invalid or truncated ELF section-header table")
        executable_segments = 0
        for index in range(phnum):
            stream.seek(phoff + index * phsize)
            ptype, offset, _, _, filesz, memsz, flags, _ = struct.unpack("<8I", stream.read(32))
            if filesz and offset + filesz > size:
                raise ValueError("ELF segment extends past end of file")
            if ptype == 1:
                if filesz > memsz:
                    raise ValueError("ELF load segment has file size larger than memory size")
                executable_segments += bool(flags & 1 and filesz)
        if not executable_segments:
            raise ValueError("ELF has no file-backed executable load segment")
        for index in range(shnum):
            stream.seek(shoff + index * shsize)
            section = struct.unpack("<10I", stream.read(40))
            if section[1] != 8 and section[5] and section[4] + section[5] > size:
                raise ValueError("ELF section extends past end of file")
        stream.seek(0)
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    sha256 = digest.hexdigest()
    if expected_sha256 and sha256.lower() != expected_sha256.lower():
        raise ValueError(f"Executable SHA-256 mismatch: expected {expected_sha256}, got {sha256}")
    return {"input": str(path.resolve()), "sha256": sha256, "size_bytes": size,
            "elf_type": kind, "entry": f"0x{entry:08X}",
            "identity_pinned": expected_sha256 is not None}


def run_tool(command):
    print("Running: " + subprocess.list2cmdline([str(arg) for arg in command]), flush=True)
    subprocess.run([str(arg) for arg in command], check=True)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def prepare_input(source, destination, config):
    """Normalize an already decrypted dump against a pinned size AND hash."""
    size = config.get("expected_size_bytes")
    expected = config.get("expected_sha256")
    if size is None or expected is None:
        raise ValueError("prepare requires a config with expected_size_bytes and expected_sha256")
    if source.stat().st_size < size:
        raise ValueError("Decrypted dump is shorter than expected_size_bytes")
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ff3-prepare-", dir=destination.parent) as directory:
        staged = Path(directory) / "EBOOT.ELF"
        with source.open("rb") as stream, staged.open("wb") as output:
            remaining = size
            while remaining:
                chunk = stream.read(min(remaining, 1024 * 1024))
                if not chunk:
                    raise ValueError("Decrypted dump changed while being read")
                output.write(chunk)
                remaining -= len(chunk)
        # Structural bounds must still be valid after trimming. A known hash
        # prevents accidentally accepting truncation of a different revision.
        identity = inspect_elf(staged, expected)
        if destination.exists():
            if inspect_elf(destination)["sha256"] != identity["sha256"]:
                raise ValueError(f"Refusing to overwrite a different ELF: {destination}")
        else:
            staged.replace(destination)
    print(f"Prepared {destination}\nSHA-256: {identity['sha256']}")


def analyze(args, config, identity):
    args.analysis_dir.mkdir(parents=True, exist_ok=True)
    report_path = args.analysis_dir / "ff3_report.json"
    # Stage all reports so a failed native analysis does not replace a good run.
    with tempfile.TemporaryDirectory(prefix=".ff3-analyze-", dir=args.analysis_dir) as directory:
        stage = Path(directory)
        command = [args.analyzer, args.elf, stage / report_path.name, hex(config["load_base"])]
        run_tool(command)
        report = json.loads((stage / report_path.name).read_text(encoding="utf-8"))
        if report["sha256"] != identity["sha256"]:
            raise ValueError("Executable changed during analysis; rerun with a stable input")
        for key, value in report["artifacts"].items():
            report["artifacts"][key] = str(args.analysis_dir / Path(value).name)
        write_json(stage / report_path.name, report)
        write_json(stage / "ff3_input.json", {**identity, "profile": config,
                   "analyzer": str(Path(args.analyzer).resolve())})
        for artifact in stage.iterdir():
            artifact.replace(args.analysis_dir / artifact.name)
    summary = report["automatic_analysis"]
    print(f"FF3 input SHA-256: {identity['sha256']}")
    print(f"Analysis: {summary['function_count']} candidate functions; "
          f"{report['import_count']} imports; "
          f"{summary['unsupported_instruction_occurrences']} unsupported instruction occurrences; "
          f"{summary['truncated_functions']} truncated functions")
    if not identity["identity_pinned"]:
        print("Executable identity is unpinned; this ELF has not been verified as a supported FF3 release.")
    return report


def generate(args, config, identity, report):
    relocations = report["relocations"]
    if relocations["invalid"] or relocations["unsupported"]:
        raise ValueError("Generation stopped: resolve invalid/unsupported relocations in ff3_report.json first")
    summary = report["automatic_analysis"]
    if not summary["function_count"]:
        raise ValueError("Generation stopped: no functions discovered")
    if summary["truncated_functions"] or summary["unsupported_instruction_occurrences"]:
        raise ValueError("Generation stopped: resolve truncated functions/unsupported instructions in ff3_report.json first")
    destination = args.analysis_dir / "generated"
    if destination.exists() and not (destination / "ff3_codegen.json").is_file():
        raise ValueError(f"Refusing to replace an unmanaged generated directory: {destination}")
    # Generate in a private directory. Preserve the last good corpus on failure.
    with tempfile.TemporaryDirectory(prefix=".ff3-codegen-", dir=args.analysis_dir) as directory:
        stage = Path(directory) / "generated"
        command = [args.recompiler, args.elf, "--auto", stage,
                   hex(config["load_base"]), hex(config["unit_span_bytes"])]
        run_tool(command)
        if inspect_elf(args.elf)["sha256"] != identity["sha256"]:
            raise ValueError("Executable changed during generation; previous output preserved")
        codegen_report = json.loads((stage / "auto_codegen_report.json").read_text(encoding="utf-8"))
        if not codegen_report["translation_units"] or not (stage / "generated_registry.cpp").is_file():
            raise ValueError("Generator produced no usable corpus; previous output preserved")
        # Record a reproducible command with the final destination, not the staging path.
        command[3] = destination
        write_json(stage / "ff3_codegen.json", {
            "schema_version": 1, **identity, "profile": config,
            "command": [str(arg) for arg in command],
            "recompiler_sha256": hashlib.sha256(Path(args.recompiler).read_bytes()).hexdigest(),
            "status": "generated_only_not_boot_validated"
        })
        # Retain mtimes for byte-identical files so a generator fix only
        # recompiles affected units, while still swapping the corpus atomically.
        if destination.exists():
            for candidate in stage.iterdir():
                old = destination / candidate.name
                if candidate.is_file() and old.is_file() and filecmp.cmp(candidate, old, shallow=False):
                    shutil.copystat(old, candidate)
        previous = Path(directory) / "previous"
        if destination.exists():
            destination.rename(previous)
        try:
            stage.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise
    print(f"Generated corpus: {destination}\nGeneration completed; use the diagnostic probe to investigate execution.")


def probe(args, config, identity):
    host = shutil.which(args.host) if args.host else None
    if not host:
        raise ValueError("Supply --host with the built ff3_probe executable")
    if not 1 <= args.max_dispatches <= 100000000 or not 1 <= args.timeout <= 300:
        raise ValueError("Probe limits: 1..100000000 dispatches, 1..300 seconds")
    args.analysis_dir.mkdir(parents=True, exist_ok=True)
    command = [str(Path(host).resolve()), str(args.elf), str(args.max_dispatches), args.hle]
    started = time.monotonic()
    # Return to the dispatcher at cross-unit boundaries for diagnostic visibility.
    environment = dict(os.environ, PSPRECOMP_NO_CHAIN="1")
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=args.timeout, env=environment, check=False)
        output = result.stdout.decode("utf-8", errors="replace")
        exit_code = result.returncode
        status = "diagnostic_stop" if exit_code == 2 else "host_error"
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or b"").decode("utf-8", errors="replace")
        exit_code = None
        status = "wall_clock_limit"
    log_path = args.analysis_dir / "ff3_probe.log"
    log_path.write_text(output, encoding="utf-8")
    write_json(args.analysis_dir / "ff3_probe.json", {
        "schema_version": 1, "status": status, "exit_code": exit_code,
        "elapsed_seconds": time.monotonic() - started, "input": identity,
        "host_sha256": hashlib.sha256(Path(host).read_bytes()).hexdigest(),
        "command": command, "max_dispatches": args.max_dispatches,
        "timeout_seconds": args.timeout, "chaining": False, "hle_mode": args.hle, "log": str(log_path),
        "environment": {key: environment[key] for key in (
            "FF3_FRAME_DIR", "FF3_FRAME_EVERY", "FF3_FRAME_START", "FF3_MAX_FRAMES", "FF3_INPUT_SCRIPT", "FF3_MOVIE_TRACE",
            "FF3_DIALOG_RESPONSE", "FF3_AUDIO_DIR", "FF3_ISO_PATH") if key in environment},
        "boot_validated": False,
    })
    print(output, end="")
    print(f"Probe: {status}; report: {args.analysis_dir / 'ff3_probe.json'}")
    return 0 if status == "diagnostic_stop" else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=("prepare", "inspect", "analyze", "generate", "probe"))
    parser.add_argument("elf", type=Path, help="decrypted ELF/PRX from your local copy")
    parser.add_argument("--config", type=Path, default=PROFILE / "config/profile.json")
    parser.add_argument("--analysis-dir", type=Path, default=PROFILE / "analysis")
    parser.add_argument("--output-elf", type=Path, default=PROFILE / "game/EBOOT.ELF",
                        help="destination for prepare (never overwrites a different executable)")
    parser.add_argument("--analyzer", help="path to the built psp_analyze executable")
    parser.add_argument("--recompiler", help="path to the built psp_recomp executable")
    parser.add_argument("--hle", choices=("bootstrap", "none"), default="bootstrap")
    parser.add_argument("--host", help="compiled ff3_probe executable")
    parser.add_argument("--max-dispatches", type=int, default=64)
    parser.add_argument("--timeout", type=int, default=10, help="probe wall-clock limit in seconds")
    args = parser.parse_args(argv)
    try:
        args.elf = args.elf.resolve()
        args.analysis_dir = args.analysis_dir.resolve()
        config = read_config(args.config)
        if args.stage == "prepare":
            prepare_input(args.elf, args.output_elf, config)
            return 0
        identity = inspect_elf(args.elf, config.get("expected_sha256"))
        if args.stage == "probe":
            return probe(args, config, identity)
        if args.stage == "inspect":
            print(json.dumps(identity, indent=2))
            return 0
        for name in ("analyzer", "recompiler") if args.stage == "generate" else ("analyzer",):
            value = getattr(args, name)
            found = shutil.which(value) if value else None
            if not found:
                raise ValueError(f"Supply --{name} with a built PSPRecomp tool (or use the CMake ff3_{args.stage} target)")
            setattr(args, name, str(Path(found).resolve()))
        report = analyze(args, config, identity)
        if args.stage == "generate":
            generate(args, config, identity, report)
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f"ff3: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
