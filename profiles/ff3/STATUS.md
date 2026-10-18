# FF3 bring-up — NPUH10125

Observed on 2026-09-27: the **native recompiled game reaches opening gameplay**.
New Game loads the cave, keeps Luneth's default name, renders the first battle,
accepts Attack commands, awards victory rewards and returns to the cave.
Directional input moves Luneth and scrolls the cave camera; walking triggered a
random encounter with the battle command menu visible.
Software-rendered guest framebuffers were visually inspected. The earlier start
menu checkpoint remains available in `analysis/menu/start-menu.png`.
The native host runs the original startup and gameplay code, loads its custom
font through a recompiled font module, and decodes the opening movie. Later
gameplay and pixel-for-pixel agreement with a reference PSP remain unverified.

## Input identity

- Disc metadata: `FINAL FANTASY III`, `NPUH10125`, disc/app version `1.00`.
- Module: `ff3`, module version `1.1`.
- Canonical decrypted ELF size: 2,588,681 bytes.
- SHA-256: `baf63a2c51de4f174dc7ab3d9c72329c58bff4d51970154bd533c62ee1965b46`.
- Configuration: `config/npuh10125.json`.
- Preparation: installed PPSSPP v1.20.4 developer EBOOT dump, then the
  hash-checked `prepare` command described in the README.
- Local provenance: `analysis/input_provenance.json`, with original dump size,
  hash, preserved source path, disc metadata hashes and normalization details.

The load base is still the framework default, `0x08804000`. All addresses below
are derived using that base, not yet confirmed by a reference memory capture.

## Analyzer results

| Measurement | Result |
| --- | ---: |
| Runtime entry | `0x089635DC` |
| Module info | `0x089C19E8` |
| Imports | 229 |
| Imports without a name in the bundled NID registry | 179 |
| Relocations applied | 73,398 |
| Invalid / unsupported relocations reported | 0 / 0 |
| Candidate functions | 11,874 |
| Covered instruction labels (delay slots normally excluded) | 369,206 |
| Indirect call sites, including overlapping functions | 3,950 |
| Truncated candidate functions | 0 |
| Unsupported instruction occurrences, including overlaps and delay slots | 0 |
| Unique unsupported addresses | 0 |
| Unique unsupported addresses used as delay slots | 0 |

Import discovery establishes what the ELF requests, not which imports are
implemented by a host. Likewise, zero reported relocation errors is not yet a
differential validation of relocated memory.

## Framework corrections made during bring-up

1. Module-info discovery now handles stripped PRXs whose section names are empty,
   using the first program header's module-info file offset with bounds checks.
2. Function discovery prefers allocated executable sections over the full RWX
   load segment. FF3 stores data in that segment too; decoding it as instructions
   produced false functions and thousands of spurious unsupported instructions.
   Sectionless executables retain the program-header fallback.
3. Unsupported-instruction accounting now checks branch delay slots, and the
   analyzer writes a unique-address CSV to make each gap inspectable.

Asset-free tests cover stripped module discovery/imports, the kernel-marker bit,
nonzero segment addresses, malformed offsets, executable-section boundaries,
sectionless fallback, and an unsupported delay slot blocking generation.

## Instruction support added

Counts below are the original unique-address gaps, before implementation. The
current unsupported-address report is empty. Instruction names were checked
against the PSPDEV Allegrex binutils opcode definitions.

| Instruction | Addresses | Implemented behavior |
| --- | ---: | --- |
| `break` | 58 | Diagnostic trap with EPC and delay-slot exception context |
| `madd` | 38 | Signed multiply-accumulate into HI/LO with defined wraparound |
| `msub` | 3 | Signed multiply-subtract into HI/LO with defined wraparound |
| `vcrs.t` | 12 | VFPU cross-product component operation and prefix behavior |
| `vi2uc.q` | 6 | Integer-to-byte packing, swizzle and destination mask behavior |
| `vrndf1.s` | 1 | Deterministic RCX state, checked against published hardware vectors |

These operations are lowered without relaxing the generation gate. BREAK stops
the host with exception diagnostics; guest exception-handler dispatch remains
unimplemented. Random support currently covers the scalar opcode used by FF3.

## Generation and regression coverage

- 112 generated units using 16 KiB address partitions; about 49 MiB of C++.
- 369,003 emitted instruction addresses, excluding discovered import stub bodies.
- 97,994 registered block entries and 229 import wrappers.
- Seven CTest suites pass, including asset-free generated-code execution with
  both dense and sparse dispatch layouts and eight Python tooling tests.

The full FF3 corpus exposed additional code-generator defects:

1. The direct-import case continued the emitter loop at the same PC indefinitely.
   It now terminates that block; an emission bound catches repeated emission.
2. Sparse dispatch referenced undeclared local state and selected the original
   context PC after local transfers. It now dispatches using the current local PC.
3. Local calls/returns could enter import stub bodies instead of the registered
   host wrapper. Import stubs are now excluded from local generated entries and
   handled as explicit runtime boundaries.
4. `mfvc $zero,255` instantiated an invalid constant-register accessor. Reads
   to `$zero` now discard the result without an accessor; the synchronous VFPU
   implementation already satisfies the interlock ordering.

The execution fixtures cover signed multiplication, accumulator wraparound,
normal/likely branch traps, delay slots, VFPU prefix/mask/overlap handling, scalar
random outputs and RCX state, and direct/indirect import boundaries. The probe
wrapper tests error/timeout reporting and preservation of the last trace output.
See [instruction provenance](../../docs/allegrex-instruction-provenance.md) for
reference revisions, hardware observations, and remaining semantic limits.

## Native execution

The `ff3_probe` host checks the input SHA-256 against the compiled corpus, loads
and relocates the ELF, and invokes its module entry. Its bootstrap HLE now
supports thread/TLS setup, interrupt masking, module lookup/query, partition and
fixed-pool memory, and bounded guest debug output. Startup registers, loader
priority, stack/TLS layout and allocation addresses remain diagnostic assumptions
pending a reference capture. The Python driver adds a wall-clock timeout,
disables cross-unit chaining, and records the HLE mode and host/input identities.

On macOS ARM64 with Clang 21, all 112 units and the host compile at `-O0`.
The bounded menu checkpoint reaches **1,618,686 outer dispatches**, with a
10,000,000-dispatch ceiling and a 2,400-vblank limit. It completed in about eight
host seconds on this machine and stopped with `FF3 diagnostic frame limit`,
without an unresolved import or guest-memory fault. A longer diagnostic run also
remained at the rendered menu until its wall-clock limit.

Run `python3 profiles/ff3/tools/run_menu.py` after building. Each run records its
own trace, host/input hashes, scripted input, and framebuffer captures under
`analysis/menu-runs/`. The runner acknowledges the autosave notice and presses
Start at virtual 25 and 32 seconds. It checks for a bounded diagnostic completion;
**visual inspection is still required** to confirm the menu. The generic probe's
`boot_validated` field deliberately remains false: a limit alone proves no boot.

The host now includes cooperative threads, joins, semaphores, event flags,
lightweight mutexes, callbacks, vblank interrupt delivery, virtual delays,
partition/FPL/volatile memory, extracted-disc and raw ISO-sector reads, and local
savedata operations. GE list execution uses the repository's software rasterizer.
SAS/VAG, ATRAC and PMF decoding reuse MIT VCS components; FFmpeg is a separate
shared LGPL dependency. The host can capture PCM but does not play it to a device.

The font PRX is prepared at `0x09F00000` from a hash-checked local decrypted dump.
External PRXTool decodes its compressed relocation records; our preparation tool
applies them and generates native C++. The normalizer and generated output remain
separate from the external tool. Firmware fonts are not mounted; FF3's own custom
font is used. Read-only jump tables and exported entry points are included in
function discovery.

Seven CTest suites cover the framework, instructions, tooling, bootstrap services,
and host regressions. The host regression checks that SIGNAL/END does not discard
subsequent GE draws and that raw-sector reads cannot cross their declared extent.
Thread tests cover joins, synchronization, callback waits and interrupt restoration.

The first longer gameplay run completed 55,296,112 dispatches and 12,000 vblanks
in about 76 host seconds, stopping at its requested frame limit without an
unresolved import or guest-memory fault. Reviewed captures show Luneth in battle
at 26/32 HP, the message `Received 3 EXP!`, and the cave after battle. Diagnostic
dispatch limits now allow up to 100 million; the menu runner retains its smaller
limit. The gameplay runner selects OK beneath the default name, avoiding the
headless keyboard boundary through normal controller input. See
[the gameplay checkpoint](native-gameplay.md).

The reviewed repeatable run at
`analysis/gameplay-runs/20260927-113333-bf8e0d07/` completed 47,686,539 dispatches
and 11,040 vblanks. Frame 7,440 shows the EXP reward; frames 9,900 and 10,200
show the cave before and after walking; frame 11,040 shows the next encounter's
Attack / Magic / Item / Guard menu. All seven CTest suites pass after increasing
and testing the diagnostic dispatch ceiling. These are manually reviewed visual
observations, separate from the probe's deliberately conservative validation flag.

Remaining limitations include unvalidated startup/load-base assumptions, partial
kernel and utility APIs, no gamepad support,
no hardware comparison, unsupported ATRAC second-buffer streaming, limited PMF
playback modes, and approximate SAS effects. Savedata is local plaintext and is
not a verified PSP-compatible encrypted save implementation. Broader OSK modes,
gameplay beyond the opening cave and battle,
loading an existing retail save, and changing menu settings have not been tested.
See [host setup and provenance](native-menu.md).

## Interactive macOS player

`ff3_player.app` now presents the guest framebuffer in a native resizable window
and feeds live keyboard input into `sceCtrl`. It adds focus-loss key release,
short-tap retention, analog/digital sampling, frame pacing, fast forward and native
message-dialog buttons and single-field UTF-16 name entry. The `play.py` launcher can auto-play the opening sequence
before handing over control; normal launch accepts live input from startup.
Window close exits cleanly rather than reporting a diagnostic failure. A reviewed
live run rendered battle rewards and the cave and closed at 105,948,477 dispatches.
Seven suites pass with added controller, dialog and name-entry regressions. See
[native play](native-play.md) for launch commands, controls and current limits.

## Native media playback

The macOS player now outputs mixed PCM through AudioQueue, plays the intro without
alternating black buffers, and paces presentation against guest time. The live
intro measured about 30 changed fps at 100% speed; the user confirmed clear,
synchronized audio. The full movie EOF check and all seven CTest suites pass.
See [media validation](native-media.md) for details and remaining limits.

## Sprite and UI coverage

The software renderer now uses half-open shared triangle edges to prevent double
blending on translucent panels, and preserves one-pixel sprite scissor bounds.
New GE-list tests cover both windings, half-pixel coordinates, clipping, flipped
UVs and sprite transparency. Rectangle bounds also honor half-pixel sample
positions. Exact cardinal VROT endpoints fix the one-pixel gaps FF3 introduced
when truncating slightly rotated panel vertices. A rebuilt party-menu capture
confirms the four vertical black seams and bottom-bar mark are gone; all seven
CTest suites pass. See [UI rendering checks](native-ui-rendering.md).

## Native save/load

Savedata writes now stage complete files before replacement, including empty
overwrites. Failed auxiliary writes preserve the previous main data file, and
filesystem errors return through the utility lifecycle. Slot listing honors
FF3's `DATA*` filter instead of including the system-data directory. LISTSAVE
and LISTLOAD now present a native slot picker with cancellation and overwrite
confirmation, and return the chosen slot name to the game.

All seven CTest suites pass, including new host tests for save/overwrite/load
across fresh Runtime instances, failed writes, buffer bounds and slot filtering.
Live validation wrote DATA00 and DATA01 with the updated player, then a fresh
launch successfully loaded a saved game and returned to world-map gameplay.
See [native savedata](native-savedata.md) for storage location and limitations.

## References and provenance

- [PSPDEV PRX generator](https://github.com/pspdev/pspsdk/blob/master/tools/psp-prxgen.c)
  and [PSP format documentation](https://uofw.github.io/upspd/docs/hardware/psp_doc.pdf):
  module-info location in a PRX header.
- [PPSSPP v1.20.4 configuration](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/Config.cpp)
  and [dump implementation](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/System.cpp):
  the installed emulator's `DumpFileTypes` option and EBOOT output naming.
- [PSPDEV opcode definitions](https://github.com/pspdev/binutils-gdb/blob/allegrex-v2.37.0/opcodes/mips-opc.c)
  and [VFPU documentation](https://pspdev.github.io/vfpu-docs/): opcode
  identification and behavioral references.

The framework fixes are independently written from format properties and local
observations. No PPSSPP decryption or runtime implementation was copied into this
repository. Game inputs, reports and emulator dumps remain local ignored data.
