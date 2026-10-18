# Final Fantasy III profile

Experimental native macOS player, code-generation tooling, and diagnostic host
for Final Fantasy III on PSP. The NPUH10125 v1.00 input is pinned by SHA-256; see
[`STATUS.md`](STATUS.md) for measured progress. The native host now starts New Game,
renders the opening battle, accepts Attack commands, and returns to the cave after
victory. It includes a software GE renderer, a recompiled font module, media
decoding and the HLE services used by this opening sequence. See
[native menu setup](native-menu.md) for disc/font preparation and dependencies,
and [the gameplay checkpoint](native-gameplay.md) for a reproducible scripted run.
On macOS, [interactive native play](native-play.md) adds a live window and keyboard
controls. Progress through the opening cave into the world map and native
[save/load across a restart](native-savedata.md) are verified. Full-game
compatibility is unverified.
See the [native architecture audit and roadmap](NATIVE_ROADMAP.md) for current
host boundaries, remaining dependencies, and staged implementation gates.
The load base `0x08804000` is the framework default, pending comparison with
PPSSPP runtime memory. Automatic analysis produces candidate functions, not a
complete census.

## Run the player

Start with the [macOS quickstart](QUICKSTART.md) for the guided ISO/input setup,
build, launch checks, controls and troubleshooting. The steps below document the
lower-level tooling workflow. An ISO alone does not supply the required decrypted
EBOOT and font inputs.

## Build the tools

Requirements: CMake 3.20+, a C++20 compiler, and Python 3.9+. Commands below run
from the repository root. They also work without any game input present.

```sh
cmake -S . -B out/ff3 -DPSPRECOMP_PROFILE=ff3 -DCMAKE_BUILD_TYPE=Release
cmake --build out/ff3 --config Release --parallel 2
ctest --test-dir out/ff3 -C Release --output-on-failure
```

If using the isolated tooling environment prepared in this workspace, replace
`cmake` and `ctest` with `out/tooling-venv/bin/cmake` and
`out/tooling-venv/bin/ctest`. It is local build tooling, not a profile dependency.

The tests generate a tiny synthetic MIPS ELF in a temporary directory. They do
not use FF3 data. To run only Python input-validation tests before building:

```sh
python3 profiles/ff3/tests/test_tooling.py
```

## Prepare and identify an executable

Supply an already decrypted ELF/PRX from your local game copy. Extraction and
decryption are outside this profile. An ISO, PBP, or encrypted/compressed `~PSP`
EBOOT cannot be passed directly to the analyzer. The filename extension is not
used to decide whether an input is valid.

Put your executable at `profiles/ff3/game/EBOOT.ELF`, or pass an external path as
`-DFF3_ELF=/absolute/path/to/EBOOT.ELF` at CMake configuration time. Existing root
ignore rules cover the profile's `game/`, `analysis/`, and `captures/` directories.
Keep executables, assets, local reports, symbols, and captures there.

```sh
python3 profiles/ff3/tools/ff3.py inspect profiles/ff3/game/EBOOT.ELF
```

This checks the ELF container, architecture and structural bounds, and prints
SHA-256. It does not establish that the input is FF3. After independently checking
the release identity, record `disc_id` and `expected_sha256` in
`config/profile.json`. A pinned hash mismatch stops analysis and generation.
Leave unknown values null; do not infer release identity from a filename.
For local variants, copy the config under `analysis/` and select it with
`-DFF3_CONFIG=/absolute/path/to/local-profile.json` or the CLI's `--config`.

### Prepared NPUH10125 input

`config/npuh10125.json` pins the observed NPUH10125 v1.00 executable, not general
compatibility for every FF3 release. PPSSPP v1.20.4's developer EBOOT dump provided
the local input. The observed dump is 2,589,024 bytes; the encrypted PSP header
declares an ELF size of 2,588,681 bytes, which is also the highest file offset
referenced by its ELF tables. The remaining 343 bytes are not part of that ELF.

For an already decrypted dump of this revision, `prepare` copies exactly the
pinned ELF size and verifies both structural bounds and the pinned SHA-256. It
preserves the source and refuses to overwrite a different destination:

```sh
python3 profiles/ff3/tools/ff3.py prepare /absolute/path/to/NPUH10125_EBOOT.BIN \
  --config profiles/ff3/config/npuh10125.json
cmake -S . -B out/ff3 -DPSPRECOMP_PROFILE=ff3 \
  -DFF3_CONFIG="$PWD/profiles/ff3/config/npuh10125.json"
```

`--output-elf` changes the preparation destination. This command does not decrypt
anything and requires a config with both `expected_size_bytes` and
`expected_sha256`. Unknown revisions must be inspected independently.
The destination is local and excluded from source control.

## Analyze and generate

```sh
cmake --build out/ff3 --config Release --target ff3_analyze
cmake --build out/ff3 --config Release --target ff3_generate
```

`ff3_analyze` writes these local artifacts under `profiles/ff3/analysis/`:

| Artifact | Purpose |
| --- | --- |
| `ff3_input.json` | Input hash, path, size, profile settings, analyzer path |
| `ff3_report.json` | Module metadata, relocations, imports, discovery statistics |
| `ff3_report_imports.csv` | Imported libraries, NIDs, known names, stub addresses |
| `ff3_report_function_seeds.csv` | Candidate entry points and discovery source |
| `ff3_report_functions_auto.csv` | Per-function analysis, unsupported/truncated flags |
| `ff3_report_unsupported.csv` | Unique unsupported addresses/opcodes, including delay slots |

`ff3_generate` runs fresh analysis first and refuses invalid/unsupported
relocations, truncated functions, unsupported instruction occurrences, or an empty
function set. This is an early gate, not proof of complete or correct translation.
Resolve those findings in the decoder/analysis or a justified profile-specific
generator before proceeding. Indirect targets and runtime-loaded modules still
need investigation even when this gate passes.

The NPUH10125 baseline now passes this gate. The unique-address count includes
delay slots; occurrence counts can be larger because candidate functions overlap.

Generated sources go to the ignored `analysis/generated/` directory. Its
`ff3_codegen.json` records the input identity, settings, generator binary hash,
and command arguments. A generator failure preserves the previous successful
corpus. Successful regeneration replaces that entire managed directory; keep
handwritten code outside it. Do not run simultaneous generation commands against
the same analysis directory.

To use separately built framework tools, the equivalent CLI is:

```sh
python3 profiles/ff3/tools/ff3.py generate profiles/ff3/game/EBOOT.ELF \
  --analyzer out/ff3/psp_analyze --recompiler out/ff3/psp_recomp
```

On multi-configuration generators those executable paths may include `Release/`
and `.exe`; the CMake targets supply the correct paths automatically.
`-DFF3_ANALYSIS_DIR=/absolute/path` changes the output location; keep it local.

## Compile generated code

After generation succeeds, prepare the disc, font and media dependency as described
in [native-menu.md](native-menu.md), then reconfigure the same build. For bring-up on a machine
with limited memory, use low optimization and one compiler job:

```sh
cmake -S . -B out/ff3 -DPSPRECOMP_PROFILE=ff3 -DFF3_BUILD_GENERATED=ON \
  -DPSPRECOMP_GENERATED_OPT_LEVEL=0 -DFF3_BUILD_FONT=ON
cmake --build out/ff3 --config Release --target ff3_probe --parallel 1
```

This builds `ff3_generated` and the `ff3_probe` diagnostic host. The pinned profile
uses 16 KiB address partitions to keep individual translation units manageable.
Reconfigure with `FF3_BUILD_GENERATED=OFF` to work without a generated corpus.

## Run a bounded diagnostic probe

```sh
python3 profiles/ff3/tools/ff3.py probe profiles/ff3/game/EBOOT.ELF \
  --config profiles/ff3/config/npuh10125.json \
  --host out/ff3/profiles/ff3/ff3_probe --max-dispatches 100000 --timeout 10 \
  --analysis-dir profiles/ff3/analysis/modules
```

On multi-configuration generators, the host may be under a `Release/` subdirectory.
The host checks the ELF hash against the identity compiled from the generated
corpus, loads and relocates it, registers the generated entries, and runs the
module entry. The default `--hle bootstrap` now supports the observed startup path
through the rendered start menu. Unimplemented services still stop explicitly.
For the full bounded menu checkpoint, use:

```sh
python3 profiles/ff3/tools/run_menu.py
```

Use `--hle none --analysis-dir profiles/ff3/analysis/no-hle` to reproduce the
first-import boundary with no services installed. Bootstrap mode uses a zero
argument pair, module GP, a loader stack near the top of user RAM, priority 32,
a reserved `$k0` record, and a host return trampoline. These are diagnostic
assumptions pending reference startup capture. Thread stacks and partition blocks share a bounded arena above
the loaded image and below the loader stack. See
[bootstrap HLE scope and provenance](bootstrap-hle.md) for supported calls and limits.

The driver disables cross-unit chaining for trace visibility, enforces a wall
clock timeout in addition to the dispatch limit, and writes `ff3_probe.log` and
`ff3_probe.json`. A dispatch limit alone cannot bound an intra-unit guest loop.
The JSON records host/input hashes, command, HLE mode, limits, elapsed time and outcome.
A diagnostic stop is not a successful native game boot. Direct host invocation
returns 2 for a diagnostic stop and 1 for an error; the driver returns 0 when it
successfully records an ordinary diagnostic stop, and 1 for errors/timeouts.

## Reverse engineering and next milestone

Load the same executable and base in your Ghidra Allegrex setup, then use the
existing `tools/ghidra/ExportPSPFunctions.java` exporter to save a function map
under `analysis/`. Compare it with the automatic candidates. The exporter writes
`name,address,size` (hex address/size); the analyzer's statistics CSV is a different
format. The current automatic generator does not consume Ghidra maps or PPSSPP
symbols. Any integration of additional seeds must be implemented and validated;
do not treat a successful export as improved automatic coverage.

Opening gameplay and the macOS interactive window are visually verified.
[Device audio, intro playback and frame pacing](native-media.md) are now implemented. The next
work includes reference comparison and later progression. Native
[save/load](native-savedata.md) is verified across a full player restart. The FF3 host
shares the MIT VCS software rasterizer and media decoder; it does not link the VCS
game host or DX12 backend. Capture comparison against a controlled reference run
remains future work. Record executable identity, settings, inputs and matching
guest events alongside the framebuffer; a frame hash alone is not sufficient.

Follow `docs/SOURCE_PROVENANCE.md` for implementation and third-party boundaries.
