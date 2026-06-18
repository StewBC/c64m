# C64M D64 Loading Plan

## Purpose

This document describes the project-level plan for adding useful `.D64` disk image
loading to `c64m` through a KERNAL trap, assuming CIA phases A through G are already
complete.

The goal is not to emulate a 1541 disk drive, IEC serial bus timing, drive CPU, drive
ROM, fast loaders, or electrical bus behavior. The goal is to make ordinary C64 disk
usage work well enough for interactive BASIC and simple program loading:

```text
LOAD "$",8
LIST
LOAD "PROGRAM",8
LOAD "PROGRAM",8,1
RUN
```

This document is a roadmap for later phase documents. It intentionally avoids
hardcoding repo file names until the implementation agent has inspected the current
source tree. The source code is the source of truth.

At the end of each implementation phase derived from this document,
`md-files/STATUS.md` must be updated succinctly with the actual implemented state,
remaining gaps, test coverage, and any intentional limitations.

---

## Assumptions

The following emulator functionality is assumed complete before disk loading work
starts:

- CPU, memory map, RAM/ROM banking, and KERNAL/BASIC boot path work.
- Runtime/frontend ownership rules are already in place.
- The live machine exists only on the runtime thread.
- Frontend receives copied snapshots only.
- CIA phases A through G are complete enough for:
  - CIA register map and mirroring.
  - Timer A/B core behavior.
  - ICR and IRQ/NMI line behavior.
  - Timer control behavior required by ordinary software.
  - CIA #1 keyboard, joystick, and RESTORE behavior.
  - CIA #2 VIC bank behavior.
  - TOD behavior where implemented by CIA Phase G.
- Full CIA serial behavior is not required.
- Full IEC electrical bus behavior is not required.
- 1541 CPU/ROM emulation is not planned for this path.

This plan also assumes that the project still follows the existing architecture rule:
frontend talks to runtime/client/platform/tools/util, runtime owns the live machine, and
machine state is not directly owned or read by the frontend.

---

## Non-Goals

Do not implement these features as part of the KERNAL-trap D64 path:

- 1541 CPU emulation.
- 1541 ROM execution.
- IEC serial bus timing.
- CIA SDR/CNT/SP shift-register behavior.
- CIA handshaking/FLAG/PC pulse behavior beyond what is already implemented.
- Fast-loader compatibility.
- Cycle-perfect disk protocol timing.
- GCR bitstream decoding for normal directory and PRG loading.
- Write support, save support, scratch, rename, validate, initialize, or disk commands.
- Multiple drives unless a later phase explicitly authorizes it.
- Disk image mutation.

The initial implementation should be read-only.

---

## User-Visible Goal

The user should be able to mount a `.D64` file as device 8 and use common C64 commands:

```text
LOAD "$",8
LIST
LOAD "GAME",8
RUN
LOAD "GAME",8,1
```

Expected behavior:

- `LOAD "$",8` loads a synthesized BASIC directory listing into BASIC memory.
- `LIST` shows a plausible C64 directory listing.
- `LOAD "NAME",8` loads the selected PRG using normal BASIC-load semantics.
- `LOAD "NAME",8,1` loads the selected PRG at the PRG file's two-byte load address.
- Failed loads return a KERNAL-style failure result that causes BASIC to report a normal
  load error rather than crashing the emulator.

This path should feel like using a C64 disk from BASIC, even though no 1541 drive is
being emulated.

---

## Implementation Strategy

Use a KERNAL-load trap rather than IEC or 1541 emulation.

The implementation agent must inspect the current source code before choosing exact
files or function names. Do not assume file names from this document. The likely areas to
inspect are:

- Machine CPU stepping and instruction execution hooks.
- KERNAL ROM call boundaries and reset/boot behavior.
- Runtime command/event model.
- Existing PRG loader behavior.
- Frontend program/machine UI for selecting files.
- Any existing tools-level file parsing utilities.
- Existing tests for PRG loading, keyboard-buffer injection, memory writes, and runtime
  commands.

The preferred architecture is:

```text
frontend file selection
    -> copied runtime command containing mounted disk path or disk bytes
    -> runtime thread owns mounted disk state or immutable parsed disk data
    -> machine/KERNAL trap reads mounted disk state during CPU execution
    -> machine writes loaded data into C64 RAM through machine-owned memory paths
```

No live machine pointers may cross threads.

---

## What To Trap

The preferred trap point is the KERNAL LOAD routine, not BASIC command parsing.

On a C64, BASIC's `LOAD` command eventually calls KERNAL routines using the logical file
setup already established by the ROM. Trapping at or near the KERNAL LOAD routine keeps
BASIC parsing, filename handling, device number handling, and normal screen messages as
close to ROM behavior as practical.

The implementation agent must verify the exact ROM addresses used by the loaded KERNAL
image instead of assuming one hardcoded ROM variant. At minimum, the trap design should
account for the standard KERNAL LOAD entry point and document any supported ROM
assumptions.

The trap should read the current KERNAL file-load parameters from the emulated machine
state, including:

- Device number.
- Secondary address.
- Filename pointer and filename length.
- Requested load target behavior.
- KERNAL/BASIC state needed to return success or failure.

Only device 8 is in scope initially.

If the device number is not 8, the trap should decline and let the ROM continue normally,
unless a later phase defines behavior for other devices.

---

## D64 Support Scope

Initial D64 support should parse enough of the disk image to support directory listing
and PRG file extraction.

Required:

- Recognize common 35-track `.D64` images.
- Parse BAM/directory sectors sufficiently to enumerate directory entries.
- Support PRG files.
- Follow the track/sector chain for PRG file contents.
- Extract the two-byte PRG load address from file data.
- Handle deleted/empty/non-PRG entries safely.
- Detect malformed track/sector chains without infinite loops.
- Report load failure for missing or unsupported files.

Deferred unless explicitly required later:

- 40-track, 42-track, error-info, and extended D64 variants.
- REL, SEQ, USR, DEL file behavior beyond directory display.
- Disk write support.
- Error channel behavior.
- Wildcards beyond the minimum useful behavior.
- File overwrite semantics.
- Drive command parser.
- GCR-level disk representation.

---

## Directory Listing Behavior

`LOAD "$",8` should synthesize a BASIC program containing a directory listing.

The synthesized listing should be compatible enough that BASIC `LIST` displays the disk
title and entries in familiar form.

The exact formatting does not need to be cycle- or byte-perfect in the first pass, but it
should be stable, deterministic, and close enough for ordinary use.

Recommended first-pass directory contents:

```text
0 "DISK NAME" ID 2A
10 "FILE1" PRG
20 "FILE2" PRG
...
BLOCKS FREE.
```

A more authentic directory listing can be implemented later. The first implementation
should prioritize correctness of BASIC memory format and usability over cosmetic
perfection.

The directory load should behave like loading a BASIC program:

- The directory listing is written into the normal BASIC program area.
- BASIC end pointers are updated consistently.
- `LIST` works immediately after `LOAD "$",8`.
- The C64 screen output remains controlled by BASIC/KERNAL ROM behavior where practical.

---

## PRG Loading Behavior

For `LOAD "NAME",8` and `LOAD "NAME",8,1`, support PRG files from the mounted D64.

A PRG file begins with a two-byte little-endian load address.

Recommended behavior:

### `LOAD "NAME",8`

Use normal BASIC-load behavior.

- If the load is a BASIC program, load into the current BASIC start area according to
  KERNAL/BASIC semantics.
- Update BASIC program end pointers so `RUN` works.
- Preserve the expected behavior users get from BASIC `LOAD`.

The implementation agent must verify whether this is best achieved by using KERNAL's
existing target address variables or by writing directly to BASIC memory and updating
KERNAL/BASIC pointers.

### `LOAD "NAME",8,1`

Use the PRG file's embedded two-byte load address.

- Load bytes after the two-byte address to the embedded load address.
- Return success to the KERNAL/BASIC caller.
- Do not automatically run the program.

### End Address

The trap should set the KERNAL return state to report the final loaded address in the
same way the ROM LOAD routine would, as far as needed by BASIC and common callers.

The implementation phase document should require targeted tests for the loaded byte
range and final address behavior.

---

## Filename Matching

Initial matching should support ordinary C64 filenames well enough for BASIC use.

Required:

- Exact filename match after converting between C64/PETSCII disk names and the host-side
  representation used internally by the parser.
- Case-insensitive host convenience only if it does not break C64 semantics.
- Strip or ignore surrounding quotes as represented by the KERNAL filename buffer.
- Treat `"$"` as directory load.

Recommended but optional in the first implementation:

- `*` wildcard matching for common `LOAD "*",8,1` use.
- Prefix wildcard such as `LOAD "GAME*",8`.
- `?` single-character wildcard.

Deferred:

- Full DOS pattern semantics.
- File type suffix parsing such as `,P,R` unless required by a target program.

---

## Mounted Disk State

The mounted disk should be runtime-owned or machine-owned in a way that respects thread
ownership.

Acceptable designs:

1. Runtime owns the mounted disk bytes and passes an immutable parsed/copyable view to
   the machine on the runtime thread.
2. Machine owns the mounted disk state, and runtime commands mutate it only on the
   runtime thread.
3. A tools-level D64 parser produces plain owned data structures, but live mounted state
   remains runtime/machine-owned.

Do not let the frontend read or mutate live machine disk state directly.

Frontend may display copied disk metadata snapshots in a later UI phase, but that is not
required for the first KERNAL trap implementation.

---

## UI and Runtime Command Scope

The first useful UI/runtime scope is:

- Select a `.D64` image from the frontend.
- Mount it as device 8.
- Show the mounted disk path/name in the Machine or Programs UI area.
- Allow unmounting or replacing the mounted disk.
- Preserve the existing PRG loader behavior.

The implementation agent should inspect the current frontend/runtime command model and
follow existing patterns. Do not introduce a new cross-thread ownership model.

A minimal command set might include:

```text
mount disk image path or bytes
unmount disk image
query copied mounted disk status
```

Exact command names and structs must be chosen from the current code style after repo
inspection.

---

## Error Handling

The first implementation should fail safely and predictably.

Required failure cases:

- No disk mounted for device 8.
- Unsupported or malformed D64 image.
- File not found.
- Unsupported file type.
- Track/sector chain loop.
- Track/sector out of range.
- File too large for target memory range.

On failure:

- Do not crash.
- Do not corrupt unrelated emulator state.
- Return a KERNAL-style failure result sufficient for BASIC to report failure or continue
  normally.
- Record enough debug/log information for maintainers to diagnose the failure.

Do not implement a full 1541 error channel unless a later phase explicitly requires it.

---

## Testing Strategy

Tests should be added for implemented behavior only.

Recommended test layers:

### D64 parser tests

- Parse disk name and ID.
- Enumerate directory entries.
- Extract a PRG file by exact name.
- Reject unsupported/malformed images.
- Detect bad track/sector chains.

### Directory synthesis tests

- Generate a BASIC-loadable directory program.
- Verify BASIC line links are valid.
- Verify `LIST`-style text content is present in memory or decoded output.

### KERNAL trap tests

- With a mounted disk, `LOAD "$",8` loads a directory program.
- With a mounted disk, `LOAD "NAME",8` loads a BASIC PRG and updates BASIC state enough
  for `RUN`.
- With a mounted disk, `LOAD "NAME",8,1` loads to the embedded PRG address.
- Missing file returns failure without memory corruption.
- Device numbers other than 8 are not hijacked unless deliberately supported.

### Runtime/frontend tests

- Mount command updates runtime-owned state.
- Replacing a disk frees/replaces previous state safely.
- Unmounting clears device 8 state.
- Frontend displays only copied disk status.

### Regression tests

- Existing boot, keyboard, debugger, PRG loader, VIC bank, CIA, and breakpoint tests
  continue to pass.

---

## Suggested Phase Breakdown

This document is not itself a phase document. Suggested later phase documents:

```text
C64MDISKPHASE_A.md - D64 parser and PRG extraction
C64MDISKPHASE_B.md - Runtime/frontend mount state for device 8
C64MDISKPHASE_C.md - KERNAL LOAD trap for PRG files
C64MDISKPHASE_D.md - LOAD "$",8 directory synthesis
C64MDISKPHASE_E.md - Filename matching, wildcard basics, and load errors
C64MDISKPHASE_F.md - UI polish, mounted disk status, and validation tests
```

The suggested order is implementation-oriented:

1. First prove that D64 data can be parsed and PRG bytes can be extracted without any
   emulator coupling.
2. Then add safe runtime-owned mount state.
3. Then trap KERNAL LOAD for actual PRG loading.
4. Then add directory listing support.
5. Then improve filename matching and failure behavior.
6. Then polish UI/status and add broader regression coverage.

---

## Compatibility Expectations

This path should support:

- BASIC directory loading.
- BASIC PRG loading from D64.
- Many simple single-file programs.
- Some PRG-based games that do not require a real drive, custom loader, or disk-side
  command behavior.

This path should not be expected to support:

- Fast loaders.
- Multi-load games that talk directly to the 1541 or IEC bus.
- Copy-protected disks.
- Programs that require 1541 RAM/ROM behavior.
- Programs that rely on precise IEC line timing.
- Software that uses disk commands beyond simple load.

If those become goals later, create a separate 1541/IEC roadmap rather than extending
this KERNAL-trap path until it becomes accidental drive emulation.

---

## Status Discipline

Every phase derived from this plan must update `md-files/STATUS.md` at the end.

The status update should be succinct and factual:

- Which disk phase is complete.
- What user-visible commands work.
- What image/file formats are supported.
- What tests were added or updated.
- What remains explicitly not implemented.

Keep skipped features listed as not implemented. Do not claim 1541, IEC, fast-loader, or
full disk compatibility from this KERNAL-trap path.

