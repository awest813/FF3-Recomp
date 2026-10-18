# Windows and Linux build plan

This is the working plan for making Windows and Linux first-class build hosts. It records what already builds, where configure stops, and the order of changes that closes the gaps. Playable commercial builds remain the hosts named in the current map.

macOS FF3 play is outside this plan. That player already has its own [status](../profiles/ff3/STATUS.md) and [roadmap](../profiles/ff3/NATIVE_ROADMAP.md).

## Progress

The FF3 software host (`ff3_host`) and `ff3_host_tests` are part of the asset-free profile build. Continuous integration runs that build, and the framework build, on Linux, macOS, and Windows. Windows uses the Visual Studio 2022 toolset with Ninja. `ff3_probe` is the headless diagnostic for Windows and Linux once a local corpus and a matching FFmpeg 7 install exist. The commands are in [profiles/ff3/WINDOWS_LINUX.md](../profiles/ff3/WINDOWS_LINUX.md). `FF3_BUILD_WINDOW=ON` still selects the macOS AppKit player and stops configuration on the other hosts.

The Vice City Stories tools configure without `profiles/vcs/generated/`. Continuous integration builds `vcs_recomp` and the CPU-only tests on Linux, macOS, and Windows. `VCSNative` is added only when that corpus is present.

Still open: a headless Linux `VCSNative` (phase 3), a Linux VCS window (phase 4), and an interactive FF3 window on Windows and Linux (the remainder of phase 5).

## Current map

Continuous integration (`.github/workflows/ci.yml`) runs on `ubuntu-latest`, `macos-latest`, and `windows-latest`. Each host builds three configurations: the framework (`PSPRECOMP_PROFILE=""`), FF3 with `FF3_BUILD_GENERATED=OFF` and `FF3_BUILD_WINDOW=OFF`, and VCS with no generated corpus. Linux and macOS use the platform's default single-configuration generator. Windows uses Ninja after the Visual Studio x64 environment is loaded, so `CMAKE_BUILD_TYPE` means the same thing on every host. `ctest` runs without `-C`.

Checked on Linux with GCC 13.3, from a clean tree:

- The framework command in the README configures, builds, and passes `psprecomp_tests` and `stripped_prx_tests`.
- The FF3 asset-free command configures, builds, and passes those two tests plus `ff3_bootstrap_tests`, `ff3_tooling_tests`, `ff3_setup_tests`, `ff3_host_tests`, `ff3_instruction_tests`, and `ff3_instruction_dense_tests`.
- `cmake -S . -B out/vcs-tools -DPSPRECOMP_PROFILE=vcs` configures, builds `vcs_recomp`, and passes `vcs_config_tests`, `audio_resampler_tests`, and `vcs_bootstrap_paths_tests` along with the two framework tests. `VCSNative` is not generated. A partial `generated/` that is missing `generated_unit_0043.cpp` still stops configure, because `PSPRECOMP_PROFILE_GUIDED_AOT` defaults to `ON`.

### What each host can do

| Surface | Linux | Windows |
| --- | --- | --- |
| `psprecomp_core`, `psp_analyze`, `psp_recomp`, `dump_function`, framework tests | Built in CI | Built in CI with MSVC |
| FF3 bootstrap, synthetic instruction, tooling, setup, and software host tests | Built in CI | Built in CI with MSVC |
| FF3 `ff3_probe` and `ff3_player` | `ff3_probe` configures when the local corpus and FFmpeg 7 are present. `FF3_BUILD_WINDOW=ON` stops off macOS | Same. FFmpeg must be built with the compiler that links the probe |
| `vcs_recomp` and the CPU-only VCS tests | Built in CI without the generated corpus | Built in CI without the generated corpus |
| `VCSNative` | DirectX 12 sources compile as disabled stubs. `display_window.cpp` and `audio_output.cpp` compile as no-op stubs. The player still cannot link without generated code, and the non-Windows link line requires FFmpeg 7 | Maintained path: Visual Studio 2022, DirectX 12, waveOut, Win32 window, vendored FFmpeg import libraries, local corpus. Scripts live in `profiles/vcs/scripts`. The Ninja recipe is [VCS_NINJA_BUILD.md](VCS_NINJA_BUILD.md) |

`VCSNative` calls `psprecomp::register_generated_functions`, declared in `include/psprecomp/runtime.hpp` and defined by the generated registry. An empty `generated/` directory cannot link the player. The Windows release scripts assume the corpus is already present.

Off Windows, `initialize_ge_gpu_backend` in `profiles/vcs/host/ge_gpu_backend_dx12.cpp` returns success and reports the active backend as software, with the message that DirectX 12 is available only on Windows. The software GE rasterizer itself is `profiles/vcs/host/ge_renderer.cpp`, which the FF3 host already compiles. A later Linux player can use that rasterizer. It does not need a new GPU API to produce a framebuffer.

## Goals

1. Windows runs the same asset-free framework and FF3 checks that Linux and macOS already run.
2. A fresh clone can configure and test the VCS tools that do not need the commercial corpus, on Windows and Linux.
3. With a local corpus and FFmpeg, Linux can build a headless software `VCSNative`, then a windowed software player.
4. Windows and Linux can build the headless FF3 probe from a local corpus. An interactive FF3 window on those hosts comes after the presentation boundary is explicit.

## Out of scope

- Publishing game executables, disc images, saves, or translated commercial corpora. Those stay in the ignored paths listed in [REPOSITORY.md](REPOSITORY.md).
- Adding an EBOOT or PRX decryption implementation.
- Putting title addresses or title HLE into `psprecomp_core`.
- Replacing the Windows DirectX 12 player, or treating a Linux software build as parity with it.
- Bringing the removed Vulkan backend back as the Linux GPU path. Stage 44 of the VCS host removed it from the Windows runtime. A Linux GPU backend, if one is written later, is a new profile backend behind `GeGpuBackendKind`.
- Claiming full-game compatibility on any host.

## Phase 1 — Windows joins the asset-free CI

`windows-latest` is in the matrix in `.github/workflows/ci.yml`. This phase added `framework` and `ff3`, with `FF3_BUILD_GENERATED=OFF` and `FF3_BUILD_WINDOW=OFF`. Phase 2 adds the `vcs` cell. The notes below are the acceptance record for the Windows framework and FF3 job.

Keep the job on the Ninja generator so `CMAKE_BUILD_TYPE=Release` and `ctest` without `-C` mean the same thing they mean on Linux. The GitHub Windows image provides Ninja and the Visual Studio 2022 C++ toolset. The default Visual Studio generator is multi-config: it ignores `CMAKE_BUILD_TYPE` and needs `--config Release` plus `ctest -C Release`. Switching the whole matrix to that generator would make the Linux and macOS steps lie. A Windows-only `-G Ninja` plus the `vcvars` environment is the smaller change.

Use the compiler that the VCS scripts already assume (MSVC from Visual Studio 2022), so the first Windows job exercises `/W4`, `/permissive-`, `/EHsc`, and `/MP` in the root `CMakeLists.txt`.

**Acceptance.** Linux, macOS, and Windows pass the same asset-free tests: two framework tests, and eight FF3 tests once `ff3_host_tests` is included. The job still runs `tools/check_repository.py`. No game files, no FFmpeg, no generated corpus. The README host table and [REPOSITORY.md](REPOSITORY.md) describe this job.

## Phase 2 — VCS tools configure without the corpus

`profiles/vcs/CMakeLists.txt` skips the player when `generated/*.cpp` is empty. The notes below are the acceptance record.

- When `profiles/vcs/generated/*.cpp` is empty, skip the profile-guided hot-unit override. Keep the fatal for a partial corpus that is missing `generated_unit_0043.cpp`. Skip it when `generated/` contains no translation units.
- Emit `vcs_recomp` unconditionally. It is the profile generator and links only `psprecomp_core`.
- Keep building the tests that do not compile the host media stack: `vcs_config_tests`, `audio_resampler_tests`, and `vcs_bootstrap_paths_tests`.
- Skip `VCSNative` and `vcs_profile_tests` when the corpus is absent. `vcs_profile_tests` compiles the host sources and, off Windows, runs `find_library(... REQUIRED)` for FFmpeg 7 (`libavcodec.so.61`, `libavformat.so.61`, `libavutil.so.59`, `libswresample.so.5`, `libswscale.so.8`). That search must not run during a tools-only configure. The DirectX 12 probes stay Windows-only and do not need the corpus, so a Windows tools build still compiles them.
- When the corpus is present, preserve today's Windows behavior: hot-unit overrides, the AVX fallback list, `/GL-` on generated units, the 64 MiB stack, and the post-build copy of `VCSNative.ini` and the vendored FFmpeg DLLs.

**Acceptance.** On Linux and Windows, from a tree with no `profiles/vcs/generated/`:

```bash
cmake -S . -B out/vcs-tools -DPSPRECOMP_PROFILE=vcs -DCMAKE_BUILD_TYPE=Release
cmake --build out/vcs-tools --parallel 2
ctest --test-dir out/vcs-tools --output-on-failure
```

`vcs_recomp` exists. `VCSNative` does not. The three CPU-only VCS tests pass along with the framework tests. A Windows tree that still has the local corpus builds `VCSNative` with the existing `profiles/vcs/scripts/build_release.bat` recipe.

Add `vcs` to the CI matrix only after this configure succeeds, and only for this tools-only result. Do not upload or cache the generated corpus for CI.

## Phase 3 — Headless Linux VCS host

This phase needs a local corpus and a local game root. It stays off CI.

Build `VCSNative` on Linux when the corpus exists and FFmpeg 7 is found. Prefer `pkg-config` for the FFmpeg 7 libraries, and keep the current soname list as the fallback the CMake file already documents. A missing FFmpeg install should fail the player target with a clear message and leave `vcs_recomp` and the CPU tests buildable.

The non-Windows stubs already make a headless process link:

- `initialize_ge_gpu_backend` selects software and returns success.
- `display_window_*` and `audio_output_*` are empty.
- `ge_renderer.cpp` still rasterizes into guest memory.

Force `Rendering.Backend=DirectX12` in an INI to resolve to software on this host, and say so in the runtime log that already records the requested and active backend. The Windows default stays DirectX 12.

**Acceptance.** With a local ULUS-10160 corpus, FFmpeg 7, and a local game root, a bounded `PSPRECOMP_MAX_DISPATCHES` run starts, prints the software backend line, and exits without a window. Compare a small GE capture or the existing software-renderer counters against a Windows software run of the same scene before treating the framebuffer as correct. A successful link only establishes that the host linked.

## Phase 4 — Linux window and audio for VCS

Add the Linux implementations in the existing `#else` branches of `profiles/vcs/host/display_window.cpp` and `profiles/vcs/host/audio_output.cpp`. Keep the Win32 implementations on `_WIN32`.

Use one small dependency for the window, keyboard, and PCM playback, linked only by the VCS host. SDL2 fits that boundary: the profile already isolates presentation behind those two files, and the framework should not grow a GUI library. Match the Win32 key table (the San Andreas-derived PSP pad mapping in `display_window.cpp`) so the same keys work on both hosts.

Present the software framebuffer. Leave DirectX 12 as the Windows GPU path. A Linux GPU backend is a later profile backend, chosen and reviewed on its own, after the software window matches the Windows software frames for the scenes used in phase 3.

**Acceptance.** The same local corpus shows a window, accepts the keyboard map, and plays the mix the Windows waveOut host already produces. DirectX 12 remains the default Windows backend. The tools-only CI job from phase 2 still has no SDL dependency.

## Phase 5 — FF3 probe, then a window, on Windows and Linux

Two different builds, in this order.

**Headless probe.** `ff3_host` compiles in the asset-free CI build. `ff3_probe` is behind `FF3_BUILD_GENERATED` and links that host plus FFmpeg media code. The Windows and Linux configure line, including the font module and `FF3_FFMPEG_ROOT`, is [profiles/ff3/WINDOWS_LINUX.md](../profiles/ff3/WINDOWS_LINUX.md). `FF3_BUILD_WINDOW` stays off. Public CI continues to pass `-DFF3_BUILD_GENERATED=OFF`. A MinGW FFmpeg tree is refused when the probe compiler is MSVC.

**Interactive window.** `profiles/ff3/CMakeLists.txt` enables `FF3_BUILD_WINDOW` by default on Apple and fatals elsewhere. `src/native_window.mm` and `src/native_audio.cpp` are AppKit and AudioToolbox. A Windows or Linux window is a new presentation backend beside that pair, selected by the host, still compiling the shared software renderer from `profiles/vcs/host/ge_renderer.cpp`.

Do this after phase 4 if the VCS window shows which pieces of present, input, and audio are actually shareable. Share a framebuffer-present interface only if both profiles call the same shape. Leave AppKit and SDL out of `psprecomp_core`. Until that backend exists, `FF3_BUILD_WINDOW=ON` should keep failing configure off macOS with a message that names the missing backend.

The macOS roadmap still applies to Metal, save packaging, and full-game coverage. This phase does not pull that work forward.

**Acceptance.** On Windows and on Linux, `ff3_probe` runs the opening sequence far enough to match an existing macOS checkpoint log, using local files. The interactive build, once it exists, opens a window, takes keyboard input, and plays audio for that same sequence. Full-game compatibility stays unverified on every host, including macOS.

## Suggested first patch

Phase 1 and the empty-corpus half of phase 2 are independent. Phase 1 is the smaller risk: it adds a CI cell and does not change player defaults. Phase 2 is the change that makes `PSPRECOMP_PROFILE=vcs` usable on a fresh clone, which is the blocker confirmed above.

Start with phase 2 in `profiles/vcs/CMakeLists.txt`, because every later VCS build depends on configure succeeding. Then add the Windows CI job. Leave phases 3–5 until those two have green checks.

## Risks

- Generated VCS units are large. The Windows player keeps per-unit `/Ox` with `/GL-`, caps `cl.exe` `/MP`, and lists AVX fallback units for MSVC 19.44 (`0018`, `0022`, `0035`, `0089`, `0091`, `0117`, `0164`). CI must not compile that corpus.
- FF3 generated units are the same class of file. The player docs already ask for one compiler job. The asset-free CI job stays at `--parallel 2` because it only compiles the synthetic fixtures.
- FFmpeg on Linux is pinned to the FFmpeg 7 ABI by the sonames in the VCS CMake file. A distro FFmpeg 6 install will not satisfy that search. The failure belongs on the player target, not on `vcs_recomp`.
- `vcs_config_tests` calls `GetSystemMetrics` only inside `_WIN32`. The non-Windows branches return the custom width and height. Run that test on Linux in phase 2 before treating it as portable; the source split is necessary and not sufficient.
- Desktop resolution on Linux currently falls back to the custom size in `resolve_display_surface_dimensions` and `resolve_internal_resolution`. Phase 4 should read the window size the new backend already has, rather than inventing a monitor query inside the config parser.
