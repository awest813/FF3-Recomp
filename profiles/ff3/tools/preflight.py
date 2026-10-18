"""Validate local FF3 runtime inputs before launching the native player."""
import json
import os
from pathlib import Path
from disc_image import DiscImage, check_disc, sfo_values
from setup import digest

PROFILE = Path(__file__).resolve().parents[1]
FIXED_FONT_SHA256 = '81048dac1956f98a51ec20798e1bdc28271caa703d23c68f90c8ac3c3373f2c7'


def host_is_executable(host):
    if os.name == 'nt':
        # Windows reports every existing file as executable. A player launch
        # needs a PE image with an .exe suffix; the setup tests rely on that.
        with host.open('rb') as stream:
            return host.suffix.lower() == '.exe' and stream.read(2) == b'MZ'
    return os.access(host, os.X_OK)


def check_runtime(elf, iso, host):
    errors = []
    config = json.loads((PROFILE / 'config/npuh10125.json').read_text())
    for path, expected, label in ((elf, config['expected_sha256'], 'Prepared EBOOT'),
                                  (elf.parent / 'libfont.fixed.elf', FIXED_FONT_SHA256, 'Prepared font')):
        if not path.is_file():
            errors.append(label + ' missing: ' + str(path))
        elif digest(path) != expected:
            errors.append(label + ' hash mismatch; use the pinned NPUH10125 v1.00 preparation')
    disc = elf.parent / 'disc'
    try:
        identity = sfo_values((disc / 'PSP_GAME/PARAM.SFO').read_bytes())
        if identity.get('DISC_ID') != 'NPUH10125' or identity.get('DISC_VERSION') != '1.00':
            errors.append('Extracted disc does not identify as NPUH10125 v1.00')
        for folder in ('MODULE', 'SOUND', 'MOVIE'):
            if not (disc / 'PSP_GAME/USRDIR/FILES' / folder).is_dir():
                errors.append('Extracted disc is missing FILES/' + folder)
    except (OSError, ValueError) as error:
        errors.append('Extracted disc unavailable: ' + str(error))
    try:
        check_disc(DiscImage(iso))
    except (OSError, ValueError) as error:
        errors.append('Raw ISO unavailable: ' + str(error))
    if not host.is_file():
        errors.append('Native player missing: ' + str(host))
    elif not host_is_executable(host):
        errors.append('Native player is not executable; rebuild it: ' + str(host))
    return errors
