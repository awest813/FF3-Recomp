#!/usr/bin/env python3
"""Exercise New Game, the opening battle, and cave movement in native FF3."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

from run_menu import ROOT, png_from_ppm


def gameplay_inputs():
    # All times are guest milliseconds. Keep the default name by selecting OK.
    events = [(25000, 0x8), (25300, 0), (32000, 0x8), (32300, 0),
              (41000, 0x4000), (41300, 0)]
    for time in range(50000, 71000, 3000):
        events.extend([(time, 0x4000), (time + 200, 0)])
    events.extend([(72000, 0x40), (72300, 0)])
    # Confirm dialogue and choose Attack and its target in the opening battle.
    for time in range(74000, 173000, 3000):
        events.extend([(time, 0x4000), (time + 200, 0)])
    events.extend([(175000, 0x20), (175800, 0),
                   (178000, 0x10), (179000, 0)])
    return ','.join(f'{time}:0x{buttons:x}' for time, buttons in events)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path,
                        default=ROOT / 'out/ff3/profiles/ff3/ff3_probe')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'profiles/ff3/analysis/gameplay-runs')
    parser.add_argument('--party-menu', action='store_true',
                        help='Open the party menu after the opening battle for UI inspection')
    parser.add_argument('--timeout', type=int, default=300,
                        help='Host wall-clock timeout in seconds (default: 300)')
    args = parser.parse_args()
    run = args.output.resolve() / (datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
                                   + '-' + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    inputs = gameplay_inputs()
    if args.party_menu:
        events = [event for event in inputs.split(',')
                  if int(event.split(':')[0]) < 173000]
        inputs = ','.join(events + ['175000:0x1000', '175300:0'])
    env = dict(os.environ, FF3_FRAME_DIR=str(run / 'frames'),
               FF3_FRAME_EVERY='60', FF3_MAX_FRAMES='11040',
               FF3_INPUT_SCRIPT=inputs, FF3_DIALOG_RESPONSE='1')
    if args.party_menu:
        env.update(PSPRECOMP_NO_CHAIN='1', PSPRECOMP_RASTER_THREADS='1',
                   FF3_TRACE_DISPATCH='0', FF3_FRAME_START='10500',
                   FF3_MAX_FRAMES='10800')
    command = [sys.executable, str(ROOT / 'profiles/ff3/tools/ff3.py'), 'probe',
               str(ROOT / 'profiles/ff3/game/EBOOT.ELF'), '--config',
               str(ROOT / 'profiles/ff3/config/npuh10125.json'), '--host',
               str(args.host.resolve()), '--max-dispatches', '100000000',
               '--timeout', str(args.timeout), '--analysis-dir', str(run)]
    print('Running native gameplay checkpoint: ' + str(run), flush=True)
    with (run / 'driver.log').open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log,
                                stderr=subprocess.STDOUT)
    report_path = run / 'ff3_probe.json'
    if not report_path.exists():
        print('Probe failed; inspect ' + str(run / 'driver.log'), file=sys.stderr)
        return 1
    report = json.loads(report_path.read_text())
    trace = Path(report['log']).read_text()
    if result.returncode or 'stop=FF3 diagnostic frame limit ' not in trace:
        print('Checkpoint stopped early; inspect ' + report['log'], file=sys.stderr)
        return 1
    # These are observation points, not automatic assertions about game state.
    checkpoints = {'battle': 7140, 'battle-result': 7440,
                   'before-movement': 9900, 'after-movement': 10200,
                   'last-frame': 11040}
    if args.party_menu:
        checkpoints = {'party-menu': 10800}
    for name, frame in checkpoints.items():
        source = run / 'frames' / f'frame_{frame:06d}.ppm'
        if not source.exists():
            print('Missing observation frame: ' + str(source), file=sys.stderr)
            return 1
        png_from_ppm(source, run / (name + '.png'))
    print('Frame limit reached. Visually inspect checkpoint PNGs in ' + str(run))
    print('This diagnostic completion does not automatically validate gameplay.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
