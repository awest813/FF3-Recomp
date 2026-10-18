# Final Fantasy III on Windows and Linux

The interactive window is the macOS player. Windows and Linux build the same
asset-free tools, the software host, and — once local game files exist — the
headless diagnostic probe `ff3_probe`. Full-game play on these hosts is
unverified. Opening gameplay through the world map has been exercised on Apple
Silicon; see [STATUS.md](STATUS.md).

Continuous integration builds the framework and this profile with
`FF3_BUILD_GENERATED=OFF` and `FF3_BUILD_WINDOW=OFF` on Linux, macOS, and
Windows. That run includes `ff3_host_tests`, which compiles the software GE
rasterizer and the file, audio, display, and save hosts. It does not need
FFmpeg or a game disc.

## Asset-free tools

From the repository root:

```sh
cmake -S . -B out/ff3-tools -DPSPRECOMP_PROFILE=ff3 -DCMAKE_BUILD_TYPE=Release \
  -DFF3_BUILD_GENERATED=OFF -DFF3_BUILD_WINDOW=OFF
cmake --build out/ff3-tools --parallel 2
ctest --test-dir out/ff3-tools --output-on-failure
```

On Windows, configure with Ninja from an x64 Visual Studio developer shell so
`CMAKE_BUILD_TYPE=Release` is honored:

```bat
cmake -S . -B out\ff3-tools -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DPSPRECOMP_PROFILE=ff3 -DFF3_BUILD_GENERATED=OFF -DFF3_BUILD_WINDOW=OFF
cmake --build out\ff3-tools --parallel 2
ctest --test-dir out\ff3-tools --output-on-failure
```

`FF3_BUILD_WINDOW=ON` stops configuration off macOS. The AppKit player stays in
[QUICKSTART.md](QUICKSTART.md).

## Headless probe

The probe needs three local inputs, the same ones the macOS player uses:

| Input | Where it goes |
| --- | --- |
| Decrypted NPUH10125 v1.00 EBOOT | `profiles/ff3/game/EBOOT.ELF` |
| Decrypted libfont PRX, then the prepared font module | `profiles/ff3/analysis/font_generated/` |
| Uncompressed matching ISO, extracted disc, and `disc.iso` | `profiles/ff3/game/` |

Hashes and the refusal rules are in the [quickstart](QUICKSTART.md). This
repository does not decrypt executables.

Linux can run the existing setup script. It prepares those inputs, generates
code, builds the minimal FFmpeg 7.1.1 libraries, and links `ff3_probe` with the
window left off:

```sh
python3 profiles/ff3/tools/setup.py \
  --elf "/path/to/NPUH10125_EBOOT.BIN" \
  --font "/path/to/NPUH10125_libfont.prx" \
  --iso "/path/to/Final Fantasy III.iso"
```

The probe is `out/ff3/profiles/ff3/ff3_probe`. Run it against the prepared ELF:

```sh
out/ff3/profiles/ff3/ff3_probe profiles/ff3/game/EBOOT.ELF 64 bootstrap
```

`--play` is the interactive macOS path. The headless probe rejects it until a
window backend exists for the host.

### Windows probe

Build FFmpeg with the same compiler that will link `ff3_probe`.

- MinGW GCC: install GCC, GNU make, and bash (Git for Windows or MSYS2), then
  `python profiles/ff3/tools/build_media.py out/media`. Configure CMake with
  that `g++` and `-DFF3_FFMPEG_ROOT=%CD%\out\media\install`.
- MSVC: point `-DFF3_FFMPEG_ROOT` at a shared FFmpeg 7 install whose import
  libraries are `avcodec.lib` and its siblings. A MinGW `libavcodec.dll.a`
  tree is refused, because MSVC cannot link it. `build_media.py` produces the
  MinGW or Unix shared libraries, not MSVC import libraries.

Then, after `ff3_generate` and the font preparation described in
[native-menu.md](native-menu.md):

```bat
cmake -S . -B out\ff3 -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DPSPRECOMP_PROFILE=ff3 -DFF3_BUILD_GENERATED=ON -DFF3_BUILD_WINDOW=OFF ^
  -DFF3_BUILD_FONT=ON -DFF3_FFMPEG_ROOT=%CD%\out\media\install ^
  -DFF3_CONFIG=%CD%\profiles\ff3\config\npuh10125.json
cmake --build out\ff3 --target ff3_probe --parallel 1
```

Use one compiler job for the generated units when memory is tight. The build
copies FFmpeg DLLs from `out/media/install/bin` next to `ff3_probe.exe` when
that directory exists. On Linux the probe's build RPATH points at
`out/media/install/lib`.

`python profiles/ff3/tools/setup.py --prepare-only` only copies and checks the
local inputs. The full Windows setup script follows the MinGW path when `g++`,
`bash`, `make`, and `ninja` are on `PATH`.
