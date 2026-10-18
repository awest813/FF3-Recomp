#!/usr/bin/env python3
"""Open the native macOS FF3 window, optionally auto-starting New Game."""
import argparse
import datetime
import os
from pathlib import Path
import subprocess
import sys
import uuid
from run_menu import ROOT
from run_gameplay import gameplay_inputs
from preflight import check_runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path,
                        default=ROOT/'out/ff3/profiles/ff3/ff3_player.app/Contents/MacOS/ff3_player')
    parser.add_argument('--gameplay', action='store_true',
                        help='Auto-play the opening battle, then hand over keyboard control in the cave')
    parser.add_argument('--elf', type=Path, default=ROOT/'profiles/ff3/game/EBOOT.ELF',
                        help='Prepared ELF; disc/, libfont.fixed.elf and saves live beside it')
    parser.add_argument('--iso', type=Path, help='Override disc.iso / FF3_ISO_PATH for raw sector reads')
    parser.add_argument('--check', action='store_true', help='Validate runtime inputs without opening the game')
    args = parser.parse_args()
    args.elf = args.elf.resolve()
    iso = (args.iso or Path(os.environ.get('FF3_ISO_PATH', str(args.elf.parent/'disc.iso')))).resolve()
    errors = check_runtime(args.elf, iso, args.host)
    if errors:
        print('FF3 is not ready:\n- ' + '\n- '.join(errors), file=sys.stderr)
        print('Setup: profiles/ff3/QUICKSTART.md', file=sys.stderr)
        return 1
    if args.check:
        print('FF3 input checks passed; native player is present. Launch to verify the build.')
        return 0
    run = ROOT/'profiles/ff3/analysis/play-runs'/(
        datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    env = dict(os.environ, PSPRECOMP_NO_CHAIN='1', FF3_ISO_PATH=str(iso))
    # A live session must not inherit diagnostic stop limits or held buttons.
    for key in ('FF3_INPUT_SCRIPT', 'FF3_MAX_FRAMES', 'FF3_AUTOPLAY_UNTIL_MS',
                'FF3_DIALOG_RESPONSE', 'FF3_FRAME_DIR'):
        env.pop(key, None)
    if args.gameplay:
        env['FF3_INPUT_SCRIPT'] = ','.join(event for event in gameplay_inputs().split(',')
                                          if int(event.split(':')[0]) < 173000)
        env['FF3_AUTOPLAY_UNTIL_MS'] = '173000'
        env['FF3_DIALOG_RESPONSE'] = '1'
    print('Arrows: D-pad | WASD: analog | Z: confirm | X: back | C/V: square/triangle')
    print('Q/E: L/R | Return/Esc: Start | Backspace: Select | Tab: toggle fast forward | M: mute')
    print('-/+: volume | F: smooth/pixel scaling')
    print('Name entry opens a native editor; Down then Z on OK keeps the default name.')
    print('Native log: '+str(run/'native.log'), flush=True)
    with (run/'native.log').open('w') as log:
        result = subprocess.run([str(args.host.resolve()),
                                 str(args.elf), '--play', 'bootstrap'],
                                cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print('Native host stopped; inspect '+str(run/'native.log'), file=sys.stderr)
    return result.returncode


if __name__ == '__main__':
    try:
        sys.exit(main())
    except OSError as error:
        print('FF3 launch: ' + str(error), file=sys.stderr)
        sys.exit(1)
