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
- host PRG files and read-only D64 images can load through the supported
  loader paths.
```

This is not a promise of full demo-scene compatibility, bit-perfect analog
SID behavior, full drive emulation, or cycle-perfect hardware recreation.

## Scope Limits

In scope for the current milestone:

```text
- SID audio emulation sufficient for recognizable C64 sound.
- Host audio output infrastructure using SDL without violating dependency rules.
- NTSC sprite BA timing parity with the existing PAL path.
- Audit and, if needed, implementation of practical 6510 undocumented opcode
  coverage used by ordinary C64 software.
- CIA #2 NMI verification if documentation or tests disagree about current
  behavior.
- Selected diagnostics only where they validate in-scope behavior.
```

Explicitly out of scope for the current milestone:

```text
- IEC serial bus protocol implementation beyond the current CIA #2 pin model.
- 1541 CPU, ROM, firmware, or drive-side emulation.
- Fast loaders.
- D64 writes, SAVE to disk, directory modification, DOS command channel, or
  disk error channel.
- Full CIA cycle-level accuracy and sub-Phi2 timing.
- CIA FLAG, PC pulse, and handshake lines unless needed by a specific accepted
  in-scope test.
- VIC-II light pen.
- Exact RDY/AEC sub-cycle CPU pin timing.
- Last-byte-on-bus open-bus behavior.
- Bit-perfect SID filter or SID chip variant modeling.
- SID 8580 support or runtime SID variant switching.
- NTSC color generation differences.
- Cartridge support for this milestone.
```

Cartridge support is a possible future follow-on, not part of this milestone.

## Required Reading Order

For all work:

```text
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. The relevant C64M<COMPONENT>.md planning document
5. The current phase or implementation guide, if one exists
```

For current milestone planning, the expected high-level documents are:

```text
C64MAUDIO.md
C64MSID.md
C64MVICII_NEW.md
C64MCPU_NEW.md
C64MCIA_NEW.md
```

## Architecture

Allowed dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
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
2. Read the relevant high-level C64M<COMPONENT>.md document.
3. Read the current phase or implementation guide.
4. Implement only the documented phase.
5. Run tests.
6. Update STATUS.md.
```

If a document disagrees with STATUS.md, do not guess. Treat it as a reconciliation task:

```text
- inspect the code and tests;
- identify which document is stale;
- fix behavior if needed;
- update STATUS.md only when the implementation and tests support the claim.
```

## Definition Of Done

A phase is complete when:

```text
- Acceptance criteria pass.
- Existing tests continue to pass.
- Architecture rules remain intact.
- Thread ownership rules remain intact.
- Runtime/frontend snapshot rules remain intact.
- STATUS.md reflects reality.
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
- CIA #2 NMI behavior has been reconciled between code, tests, STATUS.md, and
  planning documents.
- Existing boot, keyboard, joystick, debugger, PRG, D64, PAL, and NTSC smoke
  tests still pass.
```
