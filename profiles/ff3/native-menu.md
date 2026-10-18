# Native start-menu checkpoint

The NPUH10125 v1.00 native build renders **New Game / Load Game / Gallery / Options**.
The verified run stops at 2,400 virtual vblanks after scripted Start input. This is
an asset-dependent visual milestone, not full-game compatibility or a hardware
accuracy claim. The runtime is headless and writes framebuffer images.

## Local inputs

Keep all game bytes under ignored `game/` and generated outputs under `analysis/`.
Prepare `game/EBOOT.ELF` using the pinned procedure in the README. Extract your
own ISO preserving its hierarchy into `game/disc/` (so `PSP_GAME/USRDIR` is inside
that directory). Also set `FF3_ISO_PATH` to the original ISO, or place a symlink at
`game/disc.iso`: FF3 uses both named files and `sce_lbn..._size...` sector extents.
The host bounds each extent against the ISO and clamps reads to that extent.

Copy the already decrypted local `NPUH10125_libfont.prx` dump to `game/libfont.elf`.
Its expected SHA-256 is
`9ce90741b32c65274a6a8245a20f63bb68a6e01706afe10b603ede195fdec764`.
No decryption code or commercial input is distributed here.

The font has compressed PSP relocations. Build the small CSV helper against an
external checkout of [PSPDEV PRXTool](https://github.com/pspdev/prxtool), revision
`08849d87fa8692f2ba29238ec1710f2706e3e021`. From that checkout on macOS:

```sh
clang++ -std=c++11 -Wno-deprecated -Wno-writable-strings -I. -iquote tinyxml \
  /absolute/path/to/PSPRecomp/profiles/ff3/tools/prxtool_dump_relocations.cpp \
  ProcessElf.C ProcessPrx.C NidMgr.C VirtualMem.C output.C disasm.C pspkerror.C \
  tinyxml/*.cpp -o dump_relocations
```

From the PSPRecomp root:

```sh
python3 profiles/ff3/tools/prepare_font.py profiles/ff3/game/libfont.elf \
  /absolute/path/to/prxtool/dump_relocations out/ff3/psp_recomp \
  profiles/ff3/game/libfont.fixed.elf profiles/ff3/analysis/font_generated
python3 profiles/ff3/tools/build_media.py out/media
cmake -S . -B out/ff3 -DPSPRECOMP_PROFILE=ff3 \
  -DFF3_CONFIG="$PWD/profiles/ff3/config/npuh10125.json" \
  -DFF3_BUILD_GENERATED=ON -DFF3_BUILD_FONT=ON \
  -DFF3_FFMPEG_ROOT="$PWD/out/media/install" -DPSPRECOMP_GENERATED_OPT_LEVEL=0
cmake --build out/ff3 --target ff3_probe --parallel 1
python3 profiles/ff3/tools/run_menu.py
```

Generate the main corpus first using the README. The font tool verifies the input
hash, decodes relocation records using the external helper, applies relocation
arithmetic independently, builds a fixed-address ELF and recompiles its exports
and jump tables. Its two generated units use a separate namespace and uncached
import lookups to avoid collisions with the main corpus. The fixed font SHA-256
is `81048dac1956f98a51ec20798e1bdc28271caa703d23c68f90c8ac3c3373f2c7`.

## Run evidence

`run_menu.py` creates a fresh timestamped directory under `analysis/menu-runs/`.
It sets a 90-second host timeout, ten-million-dispatch ceiling, and 2,400-vblank
limit; captures every tenth changed framebuffer; acknowledges the autosave notice;
and sends Start at virtual 25 and 32 seconds, releasing it after 300 ms each time.
It writes the last capture as PNG without modifying its pixels. Inspect it to
verify the menu: the runner checks only bounded completion, not image contents.

The reviewed checkpoint used 1,618,686 outer dispatches and about eight host
seconds. Its trace ends at the requested frame limit. The generic probe report
keeps `boot_validated: false`, since a diagnostic limit is not a visual assertion.
Host/input hashes, input script and relevant environment settings are recorded.

Controls: `FF3_INPUT_SCRIPT` is a chronological comma-separated list of
`virtual_milliseconds:button_mask` values (Start is `0x8`, Cross is `0x4000`).
`FF3_DIALOG_RESPONSE=1` acknowledges a message; 2 selects No and 3 selects Back.
These are explicit diagnostic inputs, not modifications to the game's code.
The runner fixes these settings for this checkpoint. Direct probe runs can use
other values. A separate [macOS player](native-play.md) provides a native window,
keyboard controls and message-dialog buttons.

`FF3_AUDIO_DIR` optionally captures decoded signed little-endian PCM per channel.
The headless checkpoint has no host-device playback; the macOS player supports
[device audio](native-media.md). Saves use `game/ms0/PSP/SAVEDATA`; startup with
no `SYSTEM.BIN` correctly follows the missing-save path. Native gameplay save/load
is now [verified across a restart](native-savedata.md). Existing encrypted PSP
saves remain unverified.

## Sources and boundaries

- The repository's MIT VCS `ge_renderer.cpp`, `framebuffer_capture.cpp` and
  `vcs_media_decoder.cpp` are linked directly. FF3-specific display-list execution
  and framebuffer capture live in `src/display_host.cpp`. The VCS game runtime and
  GPU backends are not linked. Two array-size expressions in the shared rasterizer
  were corrected for Clang's constant-expression rules.
- SAS/VAG, ATRAC and savedata adapters were adapted from the repository's MIT VCS
  host, retaining attribution. SAS effects are approximate. ATRAC uses matching
  original assets from the extracted disc for host decoding, while maintaining
  guest stream-buffer accounting. Second-buffer streaming still stops explicitly.
- [PSPSDK](https://github.com/pspdev/pspsdk) supplies public ABI declarations, NIDs,
  structures and errors. [rust-psp's PSMF declarations](https://github.com/overdrivenpotato/rust-psp/blob/master/psp/src/sys/psmf.rs)
  informed the movie ABI; FF3's observed GetPsmfInfo call uses only two arguments.
  PMF normal playback, stop and loop handling are limited; seek/trick modes are not
  implemented. Host movie-library adapters are identified as HLE in the trace.
- [FFmpeg 7.1.1](https://ffmpeg.org/releases/) is built as a separate minimal shared
  LGPL dependency with no GPL options. `build_media.py` pins its archive SHA-256,
  enables ATRAC3/ATRAC3+/H.264/AAC/PCM, and records the small upstream film-grain
  object dependency workaround. Source and license files remain in `out/media`.
  Redistribution must include the applicable FFmpeg notices/source obligations.
- PRXTool is an external AFL-2.0 development tool. Its source is neither copied
  into the MIT runtime nor linked into the shipped host. Only its CSV output is
  consumed locally. Game-derived generated C++ and captures remain ignored.

## Verification and limits

Seven suites build without game files or media dependencies, including the
synthetic ISO/setup checks. The prepared native build adds an eighth host suite,
whose test data is synthetic. New cases cover read-only jump-table
seeding, thread joins, event/semaphore/mutex waits, callback and interrupt context
restoration, GE SIGNAL/END continuation, and bounded raw ISO-sector reads.
The menu and opening-movie frame were separately inspected using the real local
assets. The GE regression specifically guards the early-END bug that previously
left all frames black.

No pixel comparison with real hardware/PPSSPP has been performed. Startup register
and address-layout assumptions remain provisional. Kernel, GE signal modes,
savedata queries and utility behavior are incomplete. The title menu milestone
does not validate Load Game, Gallery or Options. New Game and opening in-game
rendering are covered separately by [the gameplay checkpoint](native-gameplay.md).
