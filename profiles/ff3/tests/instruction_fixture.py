"""Build an asset-free ELF covering the instruction families first found in FF3."""
import struct
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[2]); root.mkdir(parents=True, exist_ok=True)
base = 0x08804000
words = [0] * (0x400 // 4)
def put(offset, values):
    words[offset // 4:offset // 4 + len(values)] = values
# Direct calls let automatic analysis discover each test entry.
put(0, sum(([0x0C000000 | ((base + off) >> 2), 0] for off in range(0x40, 0x201, 0x40)), []) + [0xD])
put(0x40, [0x0085001C, 0x0085002E, 0x000001CD])  # madd/msub a0,a1; break 7
put(0x80, [0x10850001, 0x000001CD, 0xD])          # beq; break in slot
put(0xC0, [0x50850001, 0x000001CD, 0xD])          # beql; annul slot when unequal
put(0x100, [0x66818002, 0xD])                     # vcrs.t C020,C000,C010
put(0x140, [0xD03C8080, 0xD])                     # vi2uc.q S000,C000 (overlap)
put(0x180, [0xD0220000, 0xD])                     # vrndf1.s S000
put(0x1C0, [0x486000FF, 0x03E00008, 0x0085001C])              # jr ra; madd in slot
put(0x200, [0x0085001C, 0xD])                     # madd then trap
put(0x270, [0x0C000000 | ((base + 0x240) >> 2), 0])
put(0x280-8, [0x0C000000 | ((base + 0x9000) >> 2), 0])
put(0x240, [0x0C000000 | ((base + 0x280) >> 2), 0, 0xD])
put(0x280, [0x03E00008, 0])  # synthetic import stub
ident = b'\x7fELF\x01\x01\x01' + bytes(9)
header = struct.pack('<16sHHIIIIIHHHHHH', ident, 0xFFA0, 8, 1, 0, 52, 0x480, 0, 52, 32, 2, 40, 3, 0)
segment = struct.pack('<8I', 1, 0x80, 0, 0x380, len(words)*4, len(words)*4, 5, 16)
elf = root / 'instructions.elf'
data = bytearray(header + segment + bytes(0x80-84) + struct.pack('<%dI' % len(words), *words))
data[0x384:0x38C] = b'fixture\0'
struct.pack_into('<II', data, 0x380+44, base+0x340, base+0x354)
struct.pack_into('<I', data, 0x3C0, base+0x370)
data[0x3C8] = 5
struct.pack_into('<HII', data, 0x3CA, 1, base+0x380, base+0x280)
data[0x3F0:0x3F8] = b'fixture\0'
struct.pack_into('<I', data, 0x400, 0x12345678)
data += bytes(40) + struct.pack('<10I', 0, 1, 6, 0, 0x80, 0x300, 0, 0, 4, 0)
# A second code segment forces sparse local dispatch in this same unit.
struct.pack_into('<8I', data, 84, 1, 0x500, 0x9000, 0, 12, 12, 5, 4)
data += struct.pack('<10I', 0, 1, 6, 0x9000, 0x500, 12, 0, 0, 4, 0)
data += bytes(0x500-len(data)) + struct.pack('<3I', 0x0085001C, 0x03E00008, 0)
elf.write_bytes(data)
subprocess.run([sys.argv[1], str(elf), '--auto', str(root), hex(base), sys.argv[3] if len(sys.argv) > 3 else '0x20000'], check=True)
