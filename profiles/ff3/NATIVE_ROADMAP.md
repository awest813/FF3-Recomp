# FF3 native player: audit and implementation plan

Audited 2026-09-27. This is a source audit and proposed roadmap, not a claim that
the changes below are implemented. Scope: the NPUH10125 v1.00 macOS player.

## What “native” means here

The game logic already executes as ahead-of-time generated C++ compiled to host
machine code. There is no CPU interpreter or JIT fallback. Guest registers, RAM,
addresses and PSP API contracts remain because the compiled game depends on them.
Keeping those contracts is compatible with a native application.

The proposed destination is native GPU rendering, reliable native OS services,
native input/audio/window integration, and a relocatable application with a
guided local asset import. Keep the working recompiled game logic and its ABI.
Removing the guest memory model and reconstructing all gameplay as conventional
C++ would be a separate engine rewrite, not a prerequisite for this destination.

## Audit findings

| Area | Current implementation and evidence | Work remaining |
| --- | --- | --- |
| Game execution | [Runtime](../../src/runtime.cpp) dispatches registered compiled functions; [main](src/main.cpp) loads the pinned ELF and registers generated code. Missing functions stop explicitly. | Measure dispatch cost and coverage. Production optimization is not established: [setup](tools/setup.py) selects generated optimization level 0 and interactive launch forces chaining off. |
| Graphics | [Display host](src/display_host.cpp) processes GE commands through the shared [software renderer](../vcs/host/ge_renderer.cpp). [GPU adapter](src/software_backend.cpp) reports software and returns false for GPU support. | Implement a real GPU backend. Patch tessellation and some signal modes are unsupported. Preserve guest-visible framebuffer behavior. |
| Presentation and UI | [AppKit window](src/native_window.mm) copies the decoded 480×272 RGB framebuffer into a bitmap. Keyboard, scaling and native save dialogs exist. Game menus are rendered by the game. | Metal presentation, controller support, settings and packaging. Keep game UI appearance and interaction; replacing every game menu with OS widgets is unnecessary. |
| Device audio | [Native audio](src/native_audio.cpp) already uses AudioQueue. FFmpeg decoding and the SAS mixer are native code. | Improve streaming contracts, timing diagnostics and tested pause/resume behavior. Native libraries do not need replacement simply to qualify as native. |
| ATRAC | [ATRAC host](src/atrac_host.cpp) locates source files by scanning disc files and decodes through a whole-file source. Second-buffer streaming explicitly stops. | Complete required guest streaming semantics and replace repeated source discovery with an asset index. Validate seeking, loops and buffer ownership. |
| Intro movies | [Movie host](src/movie_host.cpp) decodes with FFmpeg, writes frames into guest buffers and advances on a fixed 33,367 μs cadence. Nonzero start PTS and unsupported play modes stop. | Use decoded timestamps and a shared playback clock; validate EOF, skip, pause and audio synchronization. Consider direct GPU upload only after preserving required guest-memory behavior. |
| Threads and services | [Bootstrap HLE](src/bootstrap_hle.cpp), [synchronization](src/bootstrap_sync.cpp), [callbacks](src/bootstrap_callbacks.cpp) and [pools](src/bootstrap_pools.cpp) implement cooperative guest execution and subsets of PSP contracts. | Inventory actually exercised contracts, complete waits/timeouts and error behavior as needed. Do not map guest threads blindly to concurrent OS threads. |
| Files | [File host](src/file_host.cpp) maps files and raw ISO sectors to host storage. Async requests perform synchronous host I/O and retain a result. | Centralize asset ownership and cancellation/error behavior; offload measured blocking I/O while delivering completions at defined guest scheduling boundaries. |
| Saves | [Savedata host](src/savedata_host.cpp) uses native files and a native picker. Save/load across restart is verified. Writes use per-file staged replacement. | Move user data outside the checkout, preserve/migrate existing saves, own state per runtime, and test interrupted multi-file updates. FILES mode 12 and encrypted PSP interchange are not implemented. |
| Fonts/modules | [Font module](src/font_module.cpp) loads a separately recompiled, pinned font ELF and forwards guest calls. Movie modules are recognized HLE replacements; arbitrary PRX loading is unsupported. | Investigate a native font implementation with matching game font data, metrics and glyph output before removing the translated font module. |
| Installation | [Quickstart](QUICKSTART.md) and [setup](tools/setup.py) prepare and build from matching ISO, decrypted EBOOT and decrypted font inputs. [CMake](CMakeLists.txt) embeds a local ELF path; media libraries use the local build layout. | Relocatable assets and library paths, an import flow, persistent settings and a standalone local application. ISO-only setup remains a separate unresolved input/decryption requirement. |

No fresh performance measurements or full-game playthrough were performed for
this audit. Software rasterization is an obvious candidate for measurement, not
a proven explanation for every slow frame. Counts of imports or explicit stops
are not compatibility percentages: some stops correctly reject invalid input.

Existing evidence: the isolated setup build reached the title menu and passed
eight native-build test suites; the source-only build passed seven. Interactive
testing covers the opening cave, battle, world map and saving/loading across a
restart. Intro audio was reported clear and synchronized. Later game coverage
remains unverified. See [status](STATUS.md) and [save checkpoint](native-savedata.md).

## Target architecture

```text
Locally generated FF3 code + guest ABI/memory
                     |
       Runtime-owned host service boundary
        /            |              \
 GE state/draws   scheduling + I/O   media + fonts
      |               |                |
 Metal backend   asset/save stores   native libraries
      |               |                |
 AppKit window / input / settings / AudioQueue
```

Retain the software renderer as a test oracle and explicit diagnostic fallback.
Extract reusable GE interfaces from the VCS-specific adapter without creating a
second divergent copy of the renderer. Keep platform APIs behind narrow host
interfaces; finish macOS before promising other platforms.

## Implementation milestones

### 1. Establish repeatable correctness and timing baselines

Add opt-in diagnostics for guest execution, GE processing/rasterization,
framebuffer conversion/presentation, decoder work and audio queues. Report guest
speed, presentation rate and changed-frame rate separately; a stationary screen
must not appear to be a simulation failure because changed-frame FPS is zero.

Capture reproducible input sequences and checkpoints for title/intro, cave,
battle, menus and world-map save/reload. Record input hashes, build settings,
timing configuration and hardware alongside results. Add GE captures that can be
replayed without progressing gameplay. Keep captures containing game data and
the preserved real save local and ignored; use synthetic fixtures in public CI.

**Gate:** the current backend reproduces the checkpoints, diagnostics explain
where frame time goes, and baseline results can be compared across builds.
Do not promise a universal 60 FPS target: preserve the game's intended timing.

### 2. Define ownership and host service boundaries

Introduce runtime-owned graphics, media, asset and save services incrementally.
Remove global mutable service state where it prevents clean shutdown/restart or
isolated tests. Separate immutable disc content from writable user data. Define
framebuffer ownership, guest-memory invalidation and synchronization before GPU
implementation. Document which host calls may yield guest execution.

**Gate:** all baseline checkpoints and existing suites pass with unchanged
output. Repeated player startup/shutdown releases resources cleanly. Existing
saves remain readable and no migration overwrites an occupied destination.

### 3. Move rendering to Metal in two reviewable steps

First present the software framebuffer through Metal. This removes the AppKit
bitmap presentation path but is **not GPU rasterization**. Measure its benefit.

Then translate GE draws/state into Metal buffers, textures and pipelines at the
original 480×272 resolution. Cover sprite/triangle rules, texture addressing,
CLUT/swizzle, skinning/morphing, depth/stencil/blending, scissor and mip behavior.
Implement framebuffer feedback, transfer/readback and memory-coherence rules;
these are correctness requirements, not optional optimizations. Record an
explicit reason whenever a draw falls back to software.

**Gate:** replay comparisons against software pass, UI seams and font positioning
remain correct, and all baseline scenes run without unexplained fallbacks.
Review 3D pixel tolerances explicitly; do not hide errors behind broad image
thresholds. Enable higher rendering resolutions only after original-resolution
parity. Apple's [Metal documentation](https://developer.apple.com/metal/) is the
primary platform reference for this backend and its profiling tools.

### 4. Optimize compiled execution and harden services

Use milestone 1 measurements to select generated optimization levels and assess
runtime chaining. Compare optimized execution with the conservative baseline at
scheduler, callback and save boundaries before changing defaults. Only replace
hot compiled leaf functions with handwritten native paths when ABI and
guest-memory differential tests support the replacement.

Complete the service contracts actually required by broader play: streaming
ATRAC buffers, file completion behavior, callback ordering, waits/timeouts and
recoverable PSP error returns. Synchronize movies using decoded timestamps and
bounded audio/video queues. Preserve AudioQueue and FFmpeg unless profiling
justifies another implementation. Hardware decoding is optional.

**Gate:** no gameplay speed changes, deadlocks or save differences in regression
runs; record frame-time percentiles, underruns and A/V drift against baseline.
Set hardware-specific performance thresholds from measurements before making
this the default. Longer sessions must not accumulate queue growth or drift.

### 5. Resolve the font dependency with a research gate

Inventory font calls, input formats, metrics, layout and rasterization using the
existing implementation as reference. Prototype the smallest native service
that reproduces the game’s own font output. Do not assume CoreText or FreeType
understands the shipped font format or that an OS font has equivalent metrics.

**Gate:** menu, dialogue, names, numbers and available language glyphs match at
multiple scales. Remove the decrypted font PRX and helper-tool requirement only
after coverage passes. If the prototype cannot achieve parity, retain the
working compiled module and document the remaining dependency. This milestone
does not remove the decrypted EBOOT requirement.

### 6. Package a standalone local player

Build an asset manifest/import flow with clear validation failures. Resolve
assets relative to the installed configuration instead of an embedded checkout
path. Bundle native libraries with relocatable load paths and dependency notices.
Store saves/settings in the user's application-data directory and offer a
backup-preserving migration from the existing game directory. Add controller
input, remapping and accessible host settings without redesigning game menus.

Distinguish a distributable generic importer/runtime from the player generated
locally from the user's inputs. Preserve the repository's source-only boundary;
do not make public packaging depend on including generated commercial game code
or assets. A private local app bundle can contain locally generated material.

**Gate:** move the local app away from the checkout and run from Finder under a
clean environment, without a terminal or development paths. Launch again offline,
verify audio/video, and save/reload after restarting. Document precisely which
inputs are still required. An ISO-only promise requires its own validated import
solution; it does not follow from completing the GPU backend.

### 7. Expand compatibility before declaring completion

Extend the checkpoint matrix through towns, shops, equipment, multi-character
battles, jobs, transitions, additional movies, bosses and the ending. Exercise
cancel/error paths, full save-slot lists, interrupted writes, audio device
changes and long sessions. Resolve stale or contradictory bootstrap documentation
alongside each implemented contract.

**Gate:** a complete campaign and repeatable regression matrix pass on the stated
supported hardware/input revision. Report known gaps rather than a percentage
claim of “native” or “complete.”

## Dependencies and recommended first change

| Work | Prerequisites | Relative uncertainty |
| --- | --- | --- |
| Diagnostics and captures | Current working player | Low–medium |
| Service boundaries | Baseline checks | Medium |
| Metal presentation | Baseline and display ownership | Low–medium |
| Metal rasterization | GE replay and memory-coherence contract | High |
| Compiled execution optimizations | Timing and state comparison | Medium–high |
| Media/service hardening | Baseline; can proceed independently of GPU draws | Medium–high |
| Native font replacement | API/data inventory and comparison harness | High; research gate |
| Relocatable local app | Asset/save ownership; can precede font replacement | Medium |
| Full-game completion | Broad gameplay evidence for every backend change | High; unknown coverage |

Start with **one bounded diagnostics/replay change**: capture timings and a
repeatable title → cave → battle → world-map save/load baseline without changing
default execution behavior. Follow with Metal presentation, then one GE draw
family at a time. This makes each later native replacement measurable and
reviewable while preserving the currently playable path. Calendar estimates
should follow the first profiling and GE parity experiments.
