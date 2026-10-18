#!/usr/bin/env python3
"""Run the pinned native FF3 menu checkpoint; inspect the saved framebuffer."""
import argparse
import datetime
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import uuid
import zlib

ROOT=Path(__file__).resolve().parents[3]
def png_from_ppm(source,destination):
    magic,dimensions,maximum,rgb=source.read_bytes().split(b'\n',3)
    width,height=map(int,dimensions.split())
    if magic!=b'P6' or maximum!=b'255' or len(rgb)!=width*height*3:raise ValueError('Unexpected framebuffer format')
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
    destination.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'out/ff3/profiles/ff3/ff3_probe')
    parser.add_argument('--output',type=Path,default=ROOT/'profiles/ff3/analysis/menu-runs')
    args=parser.parse_args()
    run=args.output.resolve()/(datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8]);run.mkdir(parents=True)
    env=dict(os.environ,FF3_FRAME_DIR=str(run/'frames'),FF3_FRAME_EVERY='10',FF3_MAX_FRAMES='2400',
             FF3_INPUT_SCRIPT='25000:0x8,25300:0,32000:0x8,32300:0',FF3_DIALOG_RESPONSE='1')
    command=[sys.executable,str(ROOT/'profiles/ff3/tools/ff3.py'),'probe',str(ROOT/'profiles/ff3/game/EBOOT.ELF'),
             '--config',str(ROOT/'profiles/ff3/config/npuh10125.json'),'--host',str(args.host.resolve()),
             '--max-dispatches','10000000','--timeout','90','--analysis-dir',str(run)]
    with (run/'driver.log').open('w') as log: result=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    report=json.loads((run/'ff3_probe.json').read_text())
    trace=(run/'ff3_probe.log').read_text()
    if result.returncode or 'stop=FF3 diagnostic frame limit ' not in trace:
        print('Checkpoint stopped early; inspect '+str(run/'ff3_probe.log'),file=sys.stderr);return 1
    frames=sorted((run/'frames').glob('frame_*.ppm'))
    if not frames:raise ValueError('No framebuffer captured')
    png_from_ppm(frames[-1],run/'last-frame.png')
    print('Native checkpoint reached its frame limit. Visually inspect '+str(run/'last-frame.png'))
    print('Trace and input settings: '+str(run/'ff3_probe.json'))
    return 0
if __name__=='__main__':sys.exit(main())
