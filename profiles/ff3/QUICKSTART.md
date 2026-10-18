# Run Final Fantasy III on macOS

This is an experimental native recompilation of **Final Fantasy III PSP,
NPUH10125 v1.00**. Opening gameplay through the world map, audio/video and native
save/load have been tested on Apple Silicon macOS. Full-game compatibility and
Intel Mac playback are not verified. Windows/Linux can build the asset-free
tooling, but do not currently have an interactive FF3 window.

An ISO alone is **not enough**: you also need an already decrypted EBOOT dump and
an already decrypted libfont PRX dump from the supported release. This project
extracts ISO files and translates code; it does not decrypt PSP executables.
No commercial inputs or ready-to-play binaries are included.

## 1. Gather the inputs

Keep these source files somewhere permanent outside the checkout:

| Input | Required identity |
| --- | --- |
| Uncompressed `.iso` | `PSP_GAME/PARAM.SFO`: DISC_ID `NPUH10125`, DISC_VERSION `1.00` |
| Decrypted EBOOT `.BIN` or `.ELF` | First 2,588,681 bytes SHA-256 `baf63a2c51de4f174dc7ab3d9c72329c58bff4d51970154bd533c62ee1965b46` |
| Decrypted libfont `.prx` or `.elf` | SHA-256 `9ce90741b32c65274a6a8245a20f63bb68a6e01706afe10b603ede195fdec764` |

The observed PPSSPP EBOOT dump includes 343 trailing bytes. Setup normalizes it
and verifies the pinned hash. The encrypted `EBOOT.BIN` or `libfont.prx` extracted
from an ISO will not work as the decrypted inputs. Other regions/revisions are
rejected rather than silently translated with incompatible settings.

## 2. Install build tools

You need Python 3.9+, CMake 3.20+, Apple Clang with the macOS SDK, Make and Git.
Install Apple's Command Line Tools if they are absent:

```sh
xcode-select --install
```

From the repository root, use an isolated environment for CMake if it is not
already installed:

```sh
python3 -m venv out/tooling-venv
out/tooling-venv/bin/python -m pip install 'cmake>=3.20'
```

Allow several GiB of free disk space for extracted assets, generated source,
objects, and dependencies. The setup downloads pinned external PRXTool source
and a checksum-verified FFmpeg 7.1.1 archive. An internet connection is needed
on the first build. No game inputs are uploaded.

## 3. Prepare and build

Replace the three example paths with your files. Paths containing spaces work
when quoted. Run this from the repository root:

```sh
python3 profiles/ff3/tools/setup.py \
  --elf "/path/to/NPUH10125_EBOOT.BIN" \
  --font "/path/to/NPUH10125_libfont.prx" \
  --iso "/path/to/Final Fantasy III.iso" \
  --cmake "$PWD/out/tooling-venv/bin/cmake"
```

If CMake is on PATH, omit `--cmake`. The command:

1. Validates the supplied release and decrypted-file hashes.
2. Copies/normalizes inputs into ignored `profiles/ff3/game/`.
3. Extracts the ISO to `game/disc/` and links `game/disc.iso` to your original ISO.
4. Builds the analyzer/recompiler and generates the game code locally.
5. Builds a pinned external relocation helper and prepares the font module.
6. Builds the minimal shared FFmpeg libraries and the macOS player with one
   compiler job, then runs CTest and the launch preflight.

**Keep the original ISO at its path.** The game uses raw sector reads as well as
extracted filenames. Moving/deleting it breaks the link; `play.py --iso` can
supply its new location.

Setup never replaces different EBOOT/font inputs or your saves. It refuses an
existing extracted disc without its matching setup manifest. Existing manually
prepared checkouts can keep using the [manual setup](native-menu.md); run the
preflight below instead of reimporting them. For input preparation without builds
or network access, add `--prepare-only`. To use an existing relocation helper,
pass `--relocator /path/to/dump_relocations`.

## 4. Check and launch

```sh
python3 profiles/ff3/tools/play.py --check
python3 profiles/ff3/tools/play.py
```

The preflight checks input identity, required disc directories, raw ISO access,
prepared font and player presence. It does not replace a runtime test of dynamic
libraries or all extracted asset contents.

Click the window to focus it. Arrow keys move, **Z** confirms, **X** goes back,
**V** opens the party menu, **Return** skips the intro, and **Tab** toggles fast
forward. [Full controls](native-play.md) include volume and scaling. A normal
launch lets you choose **Load Game**; `--gameplay` deliberately starts a new game.

Save where the game permits it (for example, on the world map after the opening
cave). The host opens a native slot picker. Saves persist in
`profiles/ff3/game/ms0/PSP/SAVEDATA/`; back up this directory before replacing a
checkout. [Savedata details](native-savedata.md) cover format and limitations.
Saves are local plaintext; encrypted PSP-save import is not supported.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Hash mismatch or encrypted-input error | Check release and provide decrypted dumps, not the ISO's encrypted modules. Renaming files does not decrypt them. |
| `cmake` / `clang++` / `make` missing | Complete step 2 and pass the CMake path if using the isolated environment. |
| Missing/broken `disc.iso` | Restore the original ISO location or launch with `--iso "/new/path/game.iso"`. |
| Existing disc has no matching manifest | Keep the existing data and use the manual setup, or prepare a fresh checkout. Do not delete saves to retry. |
| Missing native player or prepared font | Finish setup; asset-free tooling alone does not build the player. |
| Compiler killed / memory pressure | Keep the default single compiler job and generated optimization level 0; close memory-heavy applications. |
| `dyld` cannot load FFmpeg | Keep `out/media/install` in place or rebuild media and reconfigure/rebuild the player. The `.app` is not portable by itself. |
| Host stops after launch | Read the printed `analysis/play-runs/<run>/native.log`; report the last error, release ID, OS and commit, without uploading game files. |
| No keyboard response | Focus the game window and close any native slot/message/name dialog. |

All input, extracted, generated, dependency and save files stay local and ignored
by Git. The maintained source-only tests are described in
[CONTRIBUTING.md](../../CONTRIBUTING.md).

## Setup validation

On September 27, 2026, a source-only export was prepared in an isolated checkout
on Apple Silicon macOS. The ISO reader extracted 3,886 files, all checked
byte-for-byte against their ISO extents. A fresh pinned PRXTool helper matched the
existing 908 relocation records. Main and font code were regenerated, the player
was built with one compiler job, and all eight native-build CTest suites passed.
The FFmpeg source/install cache was reused; this was not a fresh dependency
download test. A bounded run of the new host reached the rendered title menu,
which was visually inspected. The source-only build separately passed all seven
asset-free suites. This validates the documented setup on the tested Mac; it is
not a full-game or cross-platform gameplay claim.
