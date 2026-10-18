#!/usr/bin/env python3
"""Prepare the user's decrypted FF3 font PRX using external PRXTool relocation data.

PRXTool remains a separate development tool; none of its source is linked into
PSPRecomp. All input bytes, relocation CSV, and generated code stay local.
"""
import argparse
import csv
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

BASE = 0x09F00000
SOURCE_SHA256 = '9ce90741b32c65274a6a8245a20f63bb68a6e01706afe10b603ede195fdec764'

def prepare(source, relocator, recompiler, output, generated):
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise ValueError('Unrecognized FF3 libfont PRX')
    result = subprocess.run([str(relocator.resolve()), str(source.resolve())], check=True, capture_output=True, text=True)
    rows = list(csv.DictReader(result.stdout.splitlines()))
    ph = [struct.unpack_from('<8I', original, 52+32*i) for i in range(3)]
    data = bytearray(original)
    for row in rows:
        kind, symbol, offset, addend = (int(row[key]) for key in ('type','symbol','offset','addend'))
        patch, target = symbol & 255, symbol >> 8
        if patch > 1 or target > 1 or offset+4 > ph[patch][4]:
            raise ValueError('Relocation outside load segment')
        at = ph[patch][1]+offset
        word = struct.unpack_from('<I', original, at)[0]
        address = BASE + ph[target][2]
        if kind == 2:
            word = (word + address) & 0xFFFFFFFF
        elif kind in (4,14,15):
            word = (word & 0xFC000000) | (((word & 0x03FFFFFF)+(address >> 2)) & 0x03FFFFFF)
        elif kind == 6:
            word = (word & 0xFFFF0000) | ((word+address) & 65535)
        elif kind == 13:
            signed = addend if addend < 0x80000000 else addend-0x100000000
            value = ((word & 65535) << 16) + signed + address
            word = (word & 0xFFFF0000) | (((value+0x8000) >> 16) & 65535)
        else:
            raise ValueError('Unhandled relocation type: '+str(kind))
        struct.pack_into('<I', data, at, word)
    struct.pack_into('<H', data, 16, 2)  # Fixed-address ELF, already relocated.
    struct.pack_into('<H', data, 44, 2)
    for i in range(2):
        struct.pack_into('<I', data, 52+32*i+8, BASE+ph[i][2])
    names = b'\0.text\0.rodata\0.data\0.bss\0.rodata.sceModuleInfo\0.shstrtab\0'
    strings = len(data); data.extend(names)
    while len(data)%4: data.append(0)
    sections = len(data)
    # The module-info offset marks the end of the font library's code/stubs.
    module = ph[0][3]-ph[0][1]
    specs = [(0,0,0,0,0,0,0,0,0,0),
             (names.index(b'.text'),1,6,BASE,ph[0][1],module,0,0,4,0),
             (names.index(b'.rodata\0'),1,2,BASE+module,ph[0][1]+module,ph[0][4]-module,0,0,4,0),
             (names.index(b'.data'),1,3,BASE+ph[1][2],ph[1][1],ph[1][4],0,0,4,0),
             (names.index(b'.bss'),8,3,BASE+ph[1][2]+ph[1][4],0,ph[1][5]-ph[1][4],0,0,4,0),
             (names.index(b'.rodata.sceModuleInfo'),1,2,BASE+module,ph[0][1]+module,52,0,0,4,0),
             (names.index(b'.shstrtab'),3,0,0,strings,len(names),0,0,1,0)]
    for spec in specs: data.extend(struct.pack('<10I',*spec))
    struct.pack_into('<I',data,32,sections)
    struct.pack_into('<HHH',data,46,40,len(specs),6)
    output.parent.mkdir(parents=True,exist_ok=True); output.write_bytes(data)
    generated.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(recompiler.resolve()),str(output.resolve()),'--auto',str(generated.resolve()),hex(BASE),'0x4000'],check=True)
    for path in list(generated.glob('*.cpp'))+list(generated.glob('generated_*.hpp')):
        code=path.read_text().replace('namespace psprecomp {','namespace ff3_font {\nusing namespace psprecomp;')
        code=code.replace('class Runtime;\nstruct AllegrexContext;','').replace('#include \"psprecomp/guest_memory.hpp\"','#include \"psprecomp/runtime.hpp\"')
        code=re.sub(r'invoke_import_cached\(\d+u, ', 'invoke_import(', code)
        code=re.sub(r'(register_generated_unit\()(\d+)u,', lambda m:m[1]+str(int(m[2])+1471)+'u,',code)
        code=re.sub(r'(invoke_chained_direct<[^,]+, )(\d+)u,',lambda m:m[1]+str(int(m[2])+1471)+'u,',code)
        path.write_text(code)
    manifest={'source_sha256':SOURCE_SHA256,'fixed_sha256':hashlib.sha256(data).hexdigest(),'base':hex(BASE),'relocations':len(rows)}
    (generated/'font_identity.hpp').write_text('#pragma once\nnamespace ff3_font { inline constexpr const char *sha256="'+manifest['fixed_sha256']+'"; }\n')
    (generated/'font_provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (generated/'font_relocations.csv').write_text(result.stdout)
    print(json.dumps(manifest,indent=2))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for field in ('source','relocator','recompiler','output','generated'): parser.add_argument(field,type=Path)
    args=parser.parse_args();prepare(**vars(args))
