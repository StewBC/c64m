# AGENTS.md

## Purpose

c64m is a C99 Commodore 64 emulator.

## Goal

Current milestone:

```text
Emulate both a PAL Commodore 64 and an NTSC Commodore 64 to an acceptable
level of fidelity for ordinary software.
```

For this milestone, acceptable fidelity means:

```text
- a broad set of BASIC programs, single-file PRGs, many KERNAL-loading D64
  titles, games, simple demos, and selected diagnostics run correctly enough
  to be useful;
- video output is correct enough for normal PAL and NTSC software;
- SID audio is present and recognizable;
- keyboard and joystick input are usable through the real C64 input paths;
- host PRG files and D64 images can load through the supported loader paths,
  including the compatibility KERNAL trap and the optional real 1541 ROM/IEC
  path where enabled;
- generic 8K/16K `.crt` cartridges can load through host convenience paths.
```

This is not a promise of full demo-scene compatibility, bit-perfect analog
SID behavior, complete media-level drive emulation, broad fast-loader
compatibility, or cycle-perfect hardware recreation.

## Scope Limits

In scope for the current milestone:

```text
- SID audio emulation sufficient for recognizable C64 sound.
- Host audio output infrastructure using SDL without violating dependency rules.
- NTSC sprite BA timing parity with the existing PAL path.
- Cycle-level VIC-II AEC/RDY pin behavior derived from the scheduled Phi2 bus
  accesses, including the 6510 read/write arbitration consequences.
- Audit and, if needed, implementation of practical 6510 undocumented opcode
  coverage used by ordinary C64 software.
- CIA #2 NMI verification if documentation or tests disagree about current
  behavior.
- D64 load/save support for devices 8 and 9 through the compatibility KERNAL
  traps.
- Optional real 1541 ROM/IEC execution for devices 8 and 9 when
  `[disk] emulate_1541=1` and a supported 16 KB 1541 DOS 2.6 ROM is loaded,
  including ROM-level LOAD, job-intercepted sector READ/WRITE, and the currently
  implemented DOS command/error-channel behavior.
- Selected diagnostics only where they validate in-scope behavior.
```

Explicitly out of scope for the current milestone:

```text
- Full IEC/drive fidelity beyond the currently documented 1541 ROM/IEC path.
- Media-level 1541 mechanics such as GCR tracks, rotation, SYNC, motor/head
  behavior, G64 support, and exact physical format fidelity.
- Broad fast-loader support; loaders depending on unmodeled drive mechanics or
  nonstandard drive ROM behavior are not validated.
- Devices beyond 8 and 9.
- 1541-family variants such as 1571.
- Cross-drive copy and Commodore DOS block/memory commands not covered by the
  current ROM/job-intercept implementation.
- Full CIA cycle-level accuracy and sub-Phi2 timing.
- CIA FLAG, PC pulse, and handshake lines unless needed by a specific accepted
  in-scope test.
- VIC-II light pen.
- Last-byte-on-bus open-bus behavior.
- Bit-perfect SID filter or SID chip variant modeling.
- SID 8580 support or runtime SID variant switching.
- NTSC color generation differences.
- Full cartridge mapper support beyond generic 8K/16K CRT cartridges.
```

Generic 8K/16K cartridge loading is implemented. Broader cartridge mapper
support remains a possible future follow-on, not part of this milestone.

## Required Reading Order

The root for .md files, unless specifically otherwise noted, is the folder `./md-files/`.

For all work:

```text
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. docs/status/README.md
5. The relevant docs/status/<COMPONENT>.md handoff file
6. The relevant C64M<COMPONENT>.md planning document, if one exists
7. The current phase or implementation guide, if one exists
```

`STATUS.md` is now a short routing and current-handoff document. It is not the
full project encyclopedia. Detailed current facts live in the focused component
handoff files under `docs/status/`.


Use this component map when choosing what to read:

```text
docs/status/VICII.md              VIC-II video, raster, display modes, sprites, BA
docs/status/CIA.md                CIA timers, ICR, IRQ/NMI, keyboard, joystick, TOD
docs/status/SID.md                SID register behavior, voices, ADSR, filter, tests
docs/status/AUDIO.md              Runtime/platform audio output, buffering, recording
docs/status/CPU_MACHINE.md        6510, memory, banking, reset/boot, IRQ/NMI, loaders
docs/status/FRONTEND_DEBUGGER.md  UI, debugger, dialogs, memory views, help, assembler
docs/status/DISK_IO.md            D64 parser, mount/unmount, KERNAL LOAD/SAVE traps
docs/status/IEC1541.md            1541 CPU/ROM/IEC, VIA 6522, job intercepts, limits
docs/status/TESTING.md            Tests and human smoke checks
docs/status/DEFERRED.md           Known limitations and future work
docs/status/OPTIMIZATIONS.md      Accepted and rejected performance changes
```

For current milestone planning, the expected high-level documents are:

```text
C64MAUDIO.md
C64MSID.md
C64MVICII_NEW.md
C64MCPU_NEW.md
C64MCIA_NEW.md
```

## Documentation Character Rule

`manual/manual.md` is a C64-facing document. Its source text must use only
characters available in ASCII or PETSCII. In practice, keep the file ASCII-only:
do not add Unicode punctuation, arrows, typographic quotes, or Unicode glyphs.
Use ASCII spellings, names, or documented escapes for PETSCII characters instead.

## Architecture

Allowed dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> tools + util
tools    -> util
platform -> util + SDL2
```

Never:

```text
frontend -> machine
platform -> machine
runtime  -> frontend
runtime  -> platform
machine  -> runtime
machine  -> frontend
machine  -> platform
```

## Thread Ownership

```text
UI thread:
    SDL
    renderer
    frontend

Runtime thread:
    runtime
    live machine

SDL audio callback thread:
    platform audio callback only
```

The live machine exists only on the runtime thread.

No live machine pointers may cross threads.

The SDL audio callback must not call runtime or machine code. It may only read from the
approved audio sample buffer path.

## Snapshot Rule

Frontend receives copied snapshots only.

Frontend never reads live machine memory directly.

Runtime publishes copies.

Machine owns live state.

Debugger and UI views must use runtime-provided copies, responses, snapshots, or
side-effect-safe peeks.

## Audio Rule

SID belongs to `machine/`.

SDL audio device ownership belongs to `platform/`.

The generic audio sample buffer belongs to `util/` or another dependency-safe shared
module approved by `MASTER.md`.

Runtime may write SID samples into the shared audio buffer.

The SDL audio callback may read samples from the shared audio buffer.

Runtime must not include platform headers, call SDL, or know that SDL exists.

Audio sample flow is not part of the normal runtime event queue, but it must follow the
same thread-safety discipline.

## Development Philosophy

Build vertically.

Prefer the smallest demonstrable machine slice.

Do not implement future phases early.

Do not add speculative abstractions.

Do not expand the current milestone into full peripheral emulation.

Diagnostics are validation aids, not the product definition. Passing a diagnostic is
required only when the diagnostic covers accepted in-scope behavior.

## Phase Workflow

For each phase:

```text
1. Read STATUS.md.
2. Read docs/status/README.md.
3. Read the relevant docs/status/<COMPONENT>.md handoff file.
4. Read the relevant high-level C64M<COMPONENT>.md document, if one exists.
5. Read the current phase or implementation guide, if one exists.
6. Implement only the documented phase.
7. Run tests.
8. Update STATUS.md and the relevant docs/status/<COMPONENT>.md file.
9. If deferred behavior changed, update docs/status/DEFERRED.md.
10. If tests or smoke checks changed, update docs/status/TESTING.md.
```

**Running the binary directly opens an SDL window and blocks until the user quits
(Cmd+Q on macOS, Alt+F4 on Windows). An automated step that launches `./build/c64m`
without a time limit will hang indefinitely.** For build verification prefer
`./build/c64m --help`, or the automated test suite (`ctest` / individual test
binaries). If the running emulator must be observed, time-limit the launch, for
example `timeout 5 ./build/c64m`, and accept that it will be killed rather than
exiting cleanly.

**When running c64m for testing without an ini file, launch it from the repo
root as `./build/c64m`, not from inside `build/` as `./c64m`.** With no ini
file supplying explicit ROM paths, ROM lookup falls back to searching `.`,
`rom`, and `roms` relative to the current working directory. The checked-in
ROMs live in `roms/` at the repo root, so running from `build/` will not find
them.

If documents disagree, do not guess. Treat it as a reconciliation task:

```text
- inspect the code and tests;
- identify which document is stale;
- fix behavior if needed;
- update STATUS.md only for top-level current handoff facts;
- update the relevant docs/status/<COMPONENT>.md file for detailed component facts;
- update docs/status/DEFERRED.md if the deferred list changed.
```

## Definition Of Done

A phase is complete when:

```text
- Acceptance criteria pass.
- Existing tests continue to pass.
- Architecture rules remain intact.
- Thread ownership rules remain intact.
- Runtime/frontend snapshot rules remain intact.
- STATUS.md reflects the top-level current handoff.
- The relevant docs/status/<COMPONENT>.md file reflects detailed component reality.
- docs/status/DEFERRED.md reflects any changed deferred behavior.
- Deferred behavior remains explicitly deferred.
```

## Current Milestone Completion Criteria

The current PAL/NTSC fidelity milestone may be claimed only when:

```text
- SID produces recognizable audio through the host audio output path.
- SID voice 3 oscillator and envelope reads behave well enough for ordinary
  software that uses them for randomness or detection.
- PAL and NTSC video paths both have correct-enough sprite BA behavior for the
  selected milestone tests.
- Practical 6510 undocumented opcode coverage has been audited and either
  confirmed or implemented.
- CIA #2 NMI behavior has been reconciled between code, tests, STATUS.md,
  docs/status/CIA.md, and planning documents.
- Existing boot, keyboard, joystick, debugger, PRG, D64, PAL, and NTSC smoke
  tests still pass.
```
