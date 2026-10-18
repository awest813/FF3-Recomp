# Bootstrap HLE scope and provenance

The profile owns these independently written diagnostic services. They are not
a complete PSP kernel. No emulator implementation was copied into this profile.

## Current host additions

The original table below covers the initial bootstrap subset. The host now also
implements callback notification/delivery, delay/sleep waits, semaphore/event/mutex
synchronization, thread joins/deletion, vblank subinterrupts, controller samples
and scripted input, volatile memory, clock/power/RTC queries and utility-module
tracking. `bootstrap_sync.cpp` and `bootstrap_callbacks.cpp` own scheduling and
callback state. Unsupported timeout/flag combinations stop explicitly.

Disc I/O, graphics, audio, system dialogs, savedata and native font loading are
separate adapters described in [native-menu.md](native-menu.md).

## Initial services

| Library | NID | Supported behavior |
| --- | --- | --- |
| SysMemUserForUser | `358CA1BB` | Record a 6.60-family compiled SDK version; return zero |
| SysMemUserForUser | `F77D77CB` | Record the compiler version; return zero |
| ThreadManForUser | `446D8DE6` | Create a dormant thread with an owned stack |
| ThreadManForUser | `F475845D` | Copy arguments to the thread stack and make it ready |
| ThreadManForUser | `AA73C935` | Exit the current thread and select the next ready thread |
| ThreadManForUser | `809CE29B` | Exit/delete the current thread and reclaim its stack |
| ThreadManForUser | `293B45B8` | Return the current host-assigned thread UID |
| SysMemUserForUser | `237DBD4F` | Allocate a partition-2 block, low/high or low/high aligned |
| SysMemUserForUser | `9D9A5BA1` | Return the address of an owned partition block |
| SysMemUserForUser | `B6D61D02` | Free an owned partition block and coalesce free intervals |
| SysMemUserForUser | `A291F107` / `F919F628` | Report largest/total free bytes in the diagnostic arena |
| ModuleMgrForUser | `D8B73127` | Find the registered module containing a guest address |
| ModuleMgrForUser | `748CBED9` | Write the 96-byte module-info structure |
| Kernel_Library | `092968F4` | Suspend interrupts and return the prior state |
| Kernel_Library | `5F10D406` / `3B84732D` | Restore interrupt state, then schedule if enabled |
| Kernel_Library | `B55249D2` | Report whether interrupts are enabled |
| SysMemUserForUser | `13A5ABEF` | Format bounded integer/string debug output |
| ThreadManForUser | `C07BB470` / `ED1410E0` | Create/delete a user fixed pool |
| ThreadManForUser | `D979E9BF` / `623AE665` | Allocate/try-allocate an available fixed-pool block |
| ThreadManForUser | `F6414A71` | Return a block to its owning fixed pool |

The SDK setter accepts versions whose high halfword is `0x0606`. A different
family stops explicitly; the host does not attempt the firmware reset/hang path.

The scheduler selects lower numerical priorities first and uses ready order to
break ties. Starting a higher-priority thread saves the caller's full context at
the import return address, then switches to the new context. Equal priorities
wait for a blocking operation or exit. Updating the runtime thread identity invalidates generated
import caller tokens, preventing stale wrappers from replacing the new PC.
A registered host trampoline handles a thread's ordinary function return.
Interrupt suspension saves an opaque 0/1 state token. Nested restore calls retain
that state; scheduling is deferred until interrupts are re-enabled. Exiting with
interrupts disabled is an explicit unsupported-case stop. Virtual timers and vblank interrupts now drive scheduling; interrupt returns
restore the interrupted context before dispatching an awakened thread.

Threads accept priorities 8–119, user/VFPU attributes, a null options pointer,
registered entry points, and stacks of at least 512 bytes. Names are limited to
31 bytes and the diagnostic thread table to 256 records. GP is inherited from
the creator. Arguments are copied before scheduling; argument/frame areas are
16-byte aligned. Stacks are filled with `0xFF` for diagnostic consistency.
Thread entry reserves 256 bytes above its argument area and a 64-byte call frame.
`$k0` points to the zeroed 256-byte record. The local CRT writes/reads its
per-thread pointer at `$k0+4`; leaving `$k0` zero caused a guest memory fault after
module registration. The loader has a separate record at its initial SP. Records
remain part of each thread's saved context and cannot overlap copied arguments.
This stack/TLS layout is a host convention, not a verified firmware kernel layout;
other firmware fields within the record are not implemented.

One allocator owns memory from the aligned end of the loaded image up to the
loader stack. Stacks allocate from the high end; partition blocks support types
0, 1, 3 and 4. Allocations round up to 256 bytes, preserve requested larger
power-of-two alignment, split free intervals and coalesce on release. Thread
block, module and pool UIDs share one increasing sequence; APIs validate object type.
The allocator excludes loaded segments and the loader stack. Its addresses and
free-memory totals do not yet reproduce firmware allocation policy/overhead.

Fixed pools support partition 2, zero attributes, null options, at most 4,096
blocks per pool and 256 pools. The host uses a 16-byte block stride and allocates
the backing region from the same arena. Allocation validates the guest result
pointer before reserving a block. Free validates exact block ownership; delete
reclaims the backing region. Exhaustion stops explicitly; blocking waits,
callback allocation, timeouts, PSP failure codes, and status APIs remain absent.

Module lookup checks each loaded PT_LOAD interval independently, including its
BSS, and rejects gaps. Module query validates a 96-byte caller buffer and writes
it only after validating the UID/size/range. Entry, GP, attributes, version and
name come from the ELF/module metadata. Text/data sizes use executable versus
non-executable segment file sizes; BSS uses memory-size minus file-size. These
segment-derived totals still need a reference comparison. No dynamic module
loading, address-alias lookup, legacy query structures or module enumeration is
implemented. Module query is covered by synthetic tests; the native trace so far
uses only address lookup.

Printf accepts the seven 32-bit argument registers after the format pointer.
Integer, character and string conversions, width/precision, and 32-bit `long`
are supported. The formatter limits format/field lengths to 1,024 bytes and
output to 4,096 bytes. Floating point, 64-bit arguments, wide strings, `%n`, and
stack arguments stop before any output is emitted. Host libc never receives the
guest format string. The return count excludes the host's `[guest]` log prefix.

Invalid UIDs, unsupported attributes/options, fixed-address allocations,
invalid alignment, exhaustion and other unsupported requests stop the probe.
Those stops are host diagnostics, not PSP error-code emulation. No preemptive
timer, blocking wait, callback dispatch, guest kernel thread structure, exception
delivery or dynamic module loading is implemented. Hardware/reference comparisons remain
necessary before claiming kernel compatibility.

## Behavioral references

- [PSPSDK thread API](https://pspdev.github.io/pspsdk/group__ThreadMan.html):
  lifecycle calls, priorities, attributes and states.
- [PSPSDK memory API](https://github.com/pspdev/pspsdk/blob/master/src/user/pspsysmem.h):
  partition allocation, address lookup, freeing, and free-size contracts.
- [PSPSDK module API/layout](https://github.com/pspdev/pspsdk/blob/master/src/user/pspmodulemgr.h)
  and [imports](https://github.com/pspdev/pspsdk/blob/master/src/user/ModuleMgrForUser.S):
  address lookup, query contract, NIDs and guest structure field order.
- [PSPSDK interrupt API](https://github.com/pspdev/pspsdk/blob/master/src/user/pspintrman.h)
  and [imports](https://github.com/pspdev/pspsdk/blob/master/src/user/Kernel_Library.S):
  saved-state suspend/resume and interrupt-enabled query.
- [PSPSDK pool APIs](https://github.com/pspdev/pspsdk/blob/master/src/user/pspthreadman.h)
  and [imports](https://github.com/pspdev/pspsdk/blob/master/src/user/ThreadManForUser.S):
  fixed-pool ownership/lifecycle contracts. Allocation layout is a diagnostic host policy.
- [uofw SysMem exports](https://github.com/uofw/uofw/blob/master/src/kd/sysmem/exports.exp)
  and [version behavior](https://github.com/uofw/uofw/blob/master/src/kd/sysmem/misc.c):
  the `sceKernelSetCompiledSdkVersion660` name, SDK family check and compiler
  version declaration. These were behavioral references only.
- [PPSSPP v1.20.4 memory declarations](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/HLE/sceKernelMemory.h):
  allocation type identities 3 (low aligned) and 4 (high aligned). No source was
  imported or adapted from its allocator.
- The repository's MIT VCS host was inspected for PSP EABI argument registers
  and runtime scheduler integration, including its reserved `$k0` area. Local
  FF3 instructions independently establish the `$k0+4` access. The FF3 host is a separate implementation;
  no VCS-specific game address or kernel-memory shortcut is used. The separate
  graphics/media host now reuses its MIT software rasterizer and decoder adapters.

## Validation

`ff3_bootstrap_tests` uses synthetic host functions and guest memory, without game
inputs. It checks priority switching, equal-priority ready ordering, return
trampolines, argument ownership, caller restoration, GP/SP state, runtime token
invalidation, deletion/reclamation, allocator alignment/overflow/exhaustion,
shared stack/block accounting, coalescing, invalid UIDs and rejected requests.
Additional cases cover module gaps/BSS boundaries and query-buffer canaries,
interrupt nesting/deferred scheduling, TLS isolation, bounded printf formatting,
and pool stride, reuse, reclamation, ownership and exhausted/invalid requests.
The Python tooling test checks both HLE modes in the command and saved report.
The local FF3 trace separately confirms execution from module entry and main-thread
startup through module lookup, CRT initialization, guest printf and fixed-pool
allocation, callback delivery, font/media initialization and the rendered start
menu. See [native-menu.md](native-menu.md) for the observed scope and limits.
