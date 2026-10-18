"""Read ordinary PSP ISO9660 files without mounting the image or running game code."""
from pathlib import Path, PurePosixPath
import struct

SECTOR = 2048


class DiscImage:
    def __init__(self, path):
        self.path = Path(path)
        self.size = self.path.stat().st_size
        self.files = {}
        with self.path.open('rb') as stream:
            self.stream = stream
            pvd = self.read(16 * SECTOR, SECTOR)
            if pvd[:7] != b'\x01CD001\x01':
                raise ValueError('Expected an uncompressed PSP ISO9660 image (not CSO/PBP)')
            if struct.unpack_from('<H', pvd, 128)[0] != SECTOR:
                raise ValueError('Unsupported ISO sector size')
            self._seen = set()
            self.walk(pvd[156:190], PurePosixPath(), 0)
        self.stream = None

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > self.size:
            raise ValueError('ISO extent lies outside the image')
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise ValueError('Truncated ISO image')
        return data

    def extent(self, record):
        if len(record) < 34 or record[1] or record[26] or record[27] or record[25] & 0x80:
            raise ValueError('Unsupported ISO directory record or multi-extent file')
        sector, size = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
        if sector * SECTOR + size > self.size:
            raise ValueError('ISO extent lies outside the image')
        return sector * SECTOR, size

    def walk(self, record, parent, depth):
        offset, size = self.extent(record)
        if depth > 32 or (offset, size) in self._seen or size > 16 * 1024 * 1024:
            raise ValueError('Invalid ISO directory tree')
        self._seen.add((offset, size))
        data = self.read(offset, size)
        at = 0
        while at < len(data):
            length = data[at]
            if not length:
                at = (at // SECTOR + 1) * SECTOR
                continue
            item = data[at:at + length]
            if len(item) != length or length < 34 or 33 + item[32] > length:
                raise ValueError('Malformed ISO directory entry')
            at += length
            raw = item[33:33 + item[32]]
            if raw in (b'\0', b'\1'):
                continue
            name = raw.decode('ascii').split(';', 1)[0]
            if not name or name in ('.', '..') or any(c in name for c in '/\\:'):
                raise ValueError('Unsafe ISO filename')
            path = parent / name
            location = self.extent(item)
            if item[25] & 2:
                self.walk(item, path, depth + 1)
            else:
                key = path.as_posix()
                if key in self.files or len(self.files) >= 100000:
                    raise ValueError('Duplicate or excessive ISO entries')
                self.files[key] = location

    def file_bytes(self, name, limit=1024 * 1024):
        offset, size = self.files[name]
        if size > limit:
            raise ValueError('ISO metadata exceeds size limit')
        with self.path.open('rb') as stream:
            stream.seek(offset)
            data = stream.read(size)
        if len(data) != size:
            raise ValueError('Truncated ISO file')
        return data

    def extract(self, destination):
        """Only write to a new directory; callers stage before installing it."""
        destination = Path(destination)
        destination.mkdir(exist_ok=False)
        with self.path.open('rb') as stream:
            for name, (offset, size) in self.files.items():
                target = destination.joinpath(*PurePosixPath(name).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                stream.seek(offset)
                with target.open('xb') as output:
                    remaining = size
                    while remaining:
                        chunk = stream.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            raise ValueError('ISO changed or truncated during extraction')
                        output.write(chunk)
                        remaining -= len(chunk)


def sfo_values(data):
    if len(data) < 20 or data[:4] != b'\0PSF':
        raise ValueError('Invalid PARAM.SFO')
    keys, values, count = struct.unpack_from('<III', data, 8)
    if count > 256 or 20 + count * 16 > len(data):
        raise ValueError('Invalid PARAM.SFO table')
    result = {}
    for index in range(count):
        key, kind, size, capacity, value = struct.unpack_from('<HHIII', data, 20 + index * 16)
        start = keys + key
        end = data.find(b'\0', start)
        if start >= len(data) or end < start or size > capacity or values + value + size > len(data):
            raise ValueError('Invalid PARAM.SFO entry')
        if kind == 0x0204:
            result[data[start:end].decode('utf-8')] = data[values + value:values + value + size].rstrip(b'\0').decode('utf-8')
    return result


def check_disc(image):
    try:
        identity = sfo_values(image.file_bytes('PSP_GAME/PARAM.SFO'))
    except KeyError as error:
        raise ValueError('ISO is missing PSP_GAME/PARAM.SFO') from error
    if identity.get('DISC_ID') != 'NPUH10125' or identity.get('DISC_VERSION') != '1.00':
        raise ValueError('This player requires the NPUH10125 v1.00 ISO')
    for name in ('PSP_GAME/USRDIR/FILES/MODULE/libfont.prx',):
        if name not in image.files:
            raise ValueError('ISO is missing ' + name)
    return identity
