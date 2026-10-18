# Native savedata

The native player stores saves beside the game executable in
`profiles/ff3/game/ms0/PSP/SAVEDATA/`. FF3 uses `NPUH10125` for system data and
`NPUH10125DATA...` directories for game slots. These local files are ignored by
source control. Keep this directory when updating the build.

The savedata adapter now stages each file beside its destination and renames it
only after the complete write closes successfully. Existing save data is not
truncated in place. Zero-byte writes replace old contents correctly. A failed
auxiliary-file write returns an error before replacing the main data file.
Filesystem failures return a savedata error and finish the utility lifecycle
instead of unwinding out of the game.

Slot enumeration honors the game's `DATA*` wildcard (and `?`) and excludes
unrelated/system directories and directory symlinks. Previously every directory
with the game ID prefix was returned, regardless of the requested slot pattern.

Asset-free host tests exercise save, overwrite, reload into a fresh Runtime,
failed overwrites, invalid auxiliary descriptors, zero-byte files, insufficient
load buffers, missing files, wildcard slot selection, invalid paths, filesystem
failure, list-save selection/cancellation, load filtering, headless cancellation,
and INIT/VISIBLE/QUIT/FINISHED/NONE lifecycle transitions. Test data lives
in a unique temporary directory, never in the player's savedata directory.

LISTSAVE and LISTLOAD now use a native macOS slot picker. The host reads the
game's save-name list, returns the selected suffix to guest memory, filters empty
slots when loading, and asks before overwriting. Cancellation leaves existing
files untouched. Headless list requests cancel instead of silently picking a slot.
Previously LISTSAVE with an empty suffix wrote PLAYDATA.BIN into the system-data
directory, making the resulting save invisible to FF3's DATA* enumeration.

These are native plaintext saves. Encrypted PSP save interchange, PSP-styled
dialog rendering, and FILES mode 12 are not implemented. File replacement is
atomic on the macOS host, but a complete save involving multiple files is not a
single transaction, and power-loss durability has not been tested. This is
ordinary game saving, not an emulator save state; the game's save restrictions
remain in effect.

The guest structure layout follows [PSPSDK savedata definitions](https://github.com/pspdev/pspsdk/blob/master/src/utility/psputility_savedata.h).

## Live validation — September 27, 2026

The first world-map save exposed the missing LISTSAVE selection: its data landed
in NPUH10125 beside SYSTEM.BIN. That directory was backed up under
`analysis/save-backups/before-slot-fix-20260927/`, and the game data and images
were copied into DATA00 for recovery without deleting the original files.

The rebuilt player loaded that recovered save, then wrote both DATA00 and DATA01
through LISTSAVE (15,224-byte PLAYDATA.BIN files). A second full player restart
completed LISTLOAD with result 0 and visibly returned to world-map gameplay.
Logs: `analysis/play-runs/20260927-134627-f0e60f46/native.log` (save) and
`analysis/play-runs/20260927-134937-91e2f05d/native.log` (fresh load). All seven
CTest suites pass. This validates the native save/load path after the opening
cave; it does not validate encrypted PSP saves or every later-game scenario.

## Preserved local regression save

The DATA01 checkpoint from this session is copied to
`game/test-saves/post-opening-cave/`, outside the active save directory. It includes
PLAYDATA.BIN, its images, a separate SYSTEM.BIN snapshot, and `manifest.json` with
file sizes and SHA-256 checksums. The copy was verified against the originals.
This fixture is ignored by Git and is not supplied in a fresh clone.

Use it for manual load regression checks after host changes. With the player
closed, restore the slot to an unused name; this example refuses to overwrite
DATA02 if it already exists:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json, shutil
root = Path('profiles/ff3/game')
fixture = root / 'test-saves/post-opening-cave'
manifest = json.loads((fixture / 'manifest.json').read_text())
for name, expected in manifest['files'].items():
    data = (fixture / name).read_bytes()
    if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
        raise SystemExit('Fixture checksum mismatch: ' + name)
shutil.copytree(fixture / 'NPUH10125DATA01', root / 'ms0/PSP/SAVEDATA/NPUH10125DATA02')
PY
python3 profiles/ff3/tools/play.py
```

Choose Load Game and DATA02, verify world-map gameplay resumes, and check that
the native log reports LISTLOAD (mode 4) result 0. The preserved SYSTEM.BIN is
for reference; this restore leaves current system settings alone. Keep the
fixture unchanged and save subsequent gameplay into an active slot.
