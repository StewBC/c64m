# C64MCIA_NEW.md

# CIA Verification and Remaining Work Plan for c64m

## Purpose

This document identifies CIA work relevant to the current PAL/NTSC fidelity
milestone.

It does not replace the existing `C64MCIA.md`, which remains the broader CIA
accuracy roadmap. This document narrows the CIA work to what matters now.

## Current Position

STATUS.md says CIA behavior is complete through Phase G, including timers,
ICR/IRQ/NMI behavior, keyboard, joystick, RESTORE, CIA #2 VIC bank and IEC port
pins, and TOD/alarm.

The older CIA planning document contains current-state sections that may describe
an earlier implementation state. In particular, it says CIA #2 interrupt pending
state was exposed diagnostically but not wired to the CPU NMI path. That conflicts
with STATUS.md and must be reconciled before the current milestone is claimed.

## Goal

Do not expand CIA scope unless verification finds a real gap in accepted
milestone behavior.

Required for this milestone:

```text
- verify CIA #2 NMI behavior against current code and tests;
- update stale documentation or fix the implementation if needed.
```

Important but not blocking:

```text
- serial data register, CNT/SP shift behavior;
- extra CIA validation suite;
- better debugger CIA visibility.
```

Explicitly out of scope for this milestone:

```text
- full CIA pin/race-level timing;
- sub-Phi2 timing;
- FLAG and PC handshake behavior unless an accepted test requires it;
- full 6526/6526A/8521 variant policy;
- open-bus and unused-bit perfection;
- IEC protocol or drive emulation.
```

## Work Area A - CIA #2 NMI Verification

### Why It Matters

CIA #2 can assert NMI. If this is missing, some diagnostics and software using
CIA #2 timer-triggered NMI will fail.

### Verification Direction

The detailed verification guide should:

```text
- inspect the CIA #2 interrupt output path;
- inspect CPU NMI edge-latch behavior;
- distinguish RESTORE NMI from CIA #2 NMI;
- create or run a minimal program that enables a CIA #2 timer interrupt and
  observes CPU NMI entry;
- verify ICR read-clear behavior deasserts or updates the line correctly;
- verify masked CIA #2 interrupts do not generate NMI;
- update STATUS.md or C64MCIA.md depending on what is actually true.
```

### Acceptance Direction

```text
- CIA #2 enabled timer interrupt can produce CPU NMI entry.
- RESTORE NMI still works.
- CIA #1 IRQ still works.
- ICR mask/read-clear behavior remains correct.
- Documentation no longer contradicts implementation.
```

## Work Area B - CIA Serial Shift Register

### Milestone Status

Not required for the current milestone unless a selected ordinary-software target
needs CIA serial I/O directly.

### Planning Direction

If accepted later, use the existing `C64MCIA.md` Phase H as the starting point.
Do not fold this into CIA #2 NMI verification.

## Work Area C - CIA Handshake, FLAG, and Pin/Race Accuracy

### Milestone Status

Deferred. This is not required for the current PAL/NTSC ordinary-software
milestone.

### Planning Direction

If accepted later, use existing `C64MCIA.md` Phases I through K as the roadmap.
Those phases should be refined into coding-agent-ready documents only when the
project deliberately moves from ordinary-software fidelity toward deeper hardware
compatibility.

## Work Area D - Validation and Debugger Visibility

### Milestone Status

Useful, but not a blocker unless needed to prove accepted behavior.

### Planning Direction

A later guide may add:

```text
- CIA timer diagnostics;
- CIA #1 IRQ diagnostics;
- CIA #2 NMI diagnostics;
- TOD diagnostics;
- joystick and keyboard matrix diagnostics;
- copied CIA debugger snapshot state.
```

Debugger views must use snapshots or safe peeks only.

## Suggested Detailed Specs To Write Later

```text
1. CIA #2 NMI verification guide.
2. CIA documentation reconciliation guide, if STATUS.md and C64MCIA.md disagree.
3. CIA serial shift implementation guide, if accepted later.
4. CIA validation corpus guide.
```
