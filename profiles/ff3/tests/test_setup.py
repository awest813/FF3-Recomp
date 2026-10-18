#!/usr/bin/env python3
"""Synthetic ISO tests; no commercial game bytes are used."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from disc_image import DiscImage, check_disc
from setup import copy_identical
from preflight import check_runtime


def record(name, sector, size, directory=False):
    n = 33 + len(name)
    n += n % 2
    b = bytearray(n)
    b[0] = n
    struct.pack_into('<I', b, 2, sector)
    struct.pack_into('<I', b, 10, size)
    b[25] = 2 if directory else 0
    b[32] = len(name)
    b[33:33 + len(name)] = name
    return b


def metadata(disc='NPUH10125'):
    keys = b'DISC_ID\0DISC_VERSION\0'
    values = disc.encode() + b'\0' + b'1.00\0'
    return (struct.pack('<5I', 0x46535000, 0x101, 52, 52 + len(keys), 2) +
            struct.pack('<HHIII', 0, 0x204, len(disc) + 1, len(disc) + 1, 0) +
            struct.pack('<HHIII', 8, 0x204, 5, 5, len(disc) + 1) + keys + values)


def iso_bytes(disc='NPUH10125', name=b'libfont.prx;1'):
    b = bytearray(32 * 2048)
    pvd = 16 * 2048
    b[pvd:pvd + 7] = b'\x01CD001\x01'
    struct.pack_into('<H', b, pvd + 128, 2048)
    b[pvd + 156:pvd + 190] = record(b'\0', 20, 2048, True)
    sfo = metadata(disc)
    for sector, entries in {
        20: record(b'PSP_GAME', 21, 2048, True),
        21: record(b'PARAM.SFO;1', 26, len(sfo)) + record(b'USRDIR', 22, 2048, True),
        22: record(b'FILES', 23, 2048, True),
        23: record(b'MODULE', 24, 2048, True),
        24: record(name, 27, 4),
    }.items():
        b[sector * 2048:sector * 2048 + len(entries)] = entries
    b[26 * 2048:26 * 2048 + len(sfo)] = sfo
    b[27 * 2048:27 * 2048 + 4] = b'test'
    return b


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='ff3 setup spaces ')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.iso = self.root / 'disc.iso'
        self.iso.write_bytes(iso_bytes())

    def test_extract_and_revision(self):
        image = DiscImage(self.iso)
        self.assertEqual(check_disc(image)['DISC_ID'], 'NPUH10125')
        destination = self.root / 'extracted'
        image.extract(destination)
        self.assertEqual((destination / 'PSP_GAME/USRDIR/FILES/MODULE/libfont.prx').read_bytes(), b'test')
        with self.assertRaises(FileExistsError):
            image.extract(destination)
        self.iso.write_bytes(iso_bytes(disc='OTHER1234'))
        with self.assertRaisesRegex(ValueError, 'NPUH10125'):
            check_disc(DiscImage(self.iso))

    def test_bad_images_and_paths(self):
        for data in (b'CISO' + bytes(40000), iso_bytes()[:42000],
                     iso_bytes(name=b'../../bad'), iso_bytes(name=b'bad\\name')):
            self.iso.write_bytes(data)
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                DiscImage(self.iso)
        data = iso_bytes()
        struct.pack_into('<I', data, 24 * 2048 + 2, 99999)
        self.iso.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'outside'):
            DiscImage(self.iso)

    def test_preserve_inputs_and_preflight_diagnostics(self):
        source = self.root / 'source'
        destination = self.root / 'kept'
        source.write_bytes(b'original')
        copy_identical(source, destination)
        copy_identical(source, destination)
        source.write_bytes(b'different')
        with self.assertRaisesRegex(ValueError, 'Refusing to overwrite'):
            copy_identical(source, destination)
        self.assertEqual(destination.read_bytes(), b'original')
        errors = check_runtime(self.root / 'EBOOT.ELF', self.iso, self.root / 'player')
        self.assertTrue(any('Prepared EBOOT missing' in e for e in errors))
        self.assertTrue(any('Prepared font missing' in e for e in errors))
        self.assertTrue(any('Native player missing' in e for e in errors))
        player = self.root / 'player'
        player.write_bytes(b'not an executable')
        player.chmod(0o644)
        errors = check_runtime(self.root / 'EBOOT.ELF', self.iso, player)
        self.assertTrue(any('not executable' in e for e in errors))


if __name__ == '__main__':
    unittest.main()
