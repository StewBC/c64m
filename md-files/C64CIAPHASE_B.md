# C64 CIA Phase B - Timer A/B Core Countdown and Reload Hardening

## Mandatory Ground Rules

- The source code is the source of truth. Treat this document as an implementation guide, not as proof that a file, symbol, or behavior currently exists.
- Before editing, inspect the repository and verify the actual file names, structs, functions, tests, and current behavior. Do not hardcode source-file assumptions from this document unless the repo confirms them.
- Implement only this phase. Do not pull later-phase behavior forward unless it is the smallest necessary support for this phase's acceptance criteria.
- Preserve the architecture rules from `AGENTS.md`: frontend must not depend on machine, runtime must not depend on frontend or platform, and live machine state must remain on the runtime thread.
- Preserve the snapshot rule: frontend/debugger views receive copied snapshots or debugger-safe peeks only; no live machine pointers may cross threads.
- At the end of the phase, update `md-files/STATUS.md` succinctly with the implemented behavior, tests/diagnostics added, known limitations, and any deferred items.

## Required Reading

1. `AGENTS.md`
2. `md-files/STATUS.md`
3. `C64MCIA.md`
4. This phase document
5. Relevant source and tests discovered in the repository

## Repository Inspection Rule

Use repository search to locate the actual CIA, C64 bus, runtime, debugger snapshot, keyboard/joystick, IEC, CPU interrupt, and test code as needed. Prefer names discovered in source over names from planning documents. If the repo disagrees with this document, trust the repo, then update this document or `md-files/STATUS.md` only if the discrepancy is material to the phase handoff.

## Recommended CIA Implementation Sequence

From an implementation-dependency standpoint, use this sequence unless source inspection proves a better local order:

```text
A, B, D, C, E, F, G, H, I, J, K
```

This filename is alphabetical by phase, but the practical execution sequence intentionally places D before C so interrupt behavior is stable before expanding timer mode complexity.

## Goal

Make Timer A and Timer B trustworthy 16-bit down-counters with tested latch/counter separation, start/stop, force-load, underflow, reload, one-shot, continuous, and interrupt-source flag behavior.

## Implementation Order Position

Run after Phase A. This phase should precede interrupt-line hardening because timer underflow is the simplest repeatable interrupt source.

## Scope

- Timer A and Timer B each have a 16-bit latch and separate 16-bit live counter.
- Writes to timer registers update the latch.
- Reads from timer registers return the live counter.
- CRA bit 0 starts/stops Timer A.
- CRB bit 0 starts/stops Timer B.
- CRA/CRB bit 3 selects continuous vs one-shot.
- CRA/CRB bit 4 force-loads latch into counter as a strobe.
- Underflow sets the corresponding ICR source flag.
- Continuous underflow reloads and keeps running.
- One-shot underflow reloads and stops.
- Define and document the project's current cycle source for Phi2-mode countdown.
- Resolve latch value `$0000` behavior against the chosen CIA reference.

## Out Of Scope

- CNT input countdown.
- Timer B cascade from Timer A.
- PB6/PB7 output.
- Full ICR mask/read-clear/output-line behavior beyond setting timer source flags already needed by timer tests.
- TOD, serial, FLAG, and handshake sources.
- Cycle-edge race accuracy.

## Control Bits For This Phase

```text
CRA $0E bit 0: Timer A start
CRA $0E bit 3: Timer A one-shot when set, continuous when clear
CRA $0E bit 4: Timer A force-load strobe
CRB $0F bit 0: Timer B start
CRB $0F bit 3: Timer B one-shot when set, continuous when clear
CRB $0F bit 4: Timer B force-load strobe
```

## Repo Inspection Tasks

- Locate timer state fields for latch, counter, running state, underflow state, and control registers.
- Locate the machine/CIA stepping path and its relationship to master cycle, Phi2, and timed bus events.
- Locate ICR source flag helpers.
- Locate existing zero-latch and one-shot timer diagnostics noted in status.
- Locate tests or diagnostics that execute CPU-visible timer programming sequences.

## Implementation Guidance

1. Keep latch writes separate from live counter loads except for documented stopped-timer or force-load behavior.
2. Make force-load a strobe: after a write with bit 4 set, the stored control register should not keep bit 4 set unless verified source code/reference policy requires it.
3. Decide whether current per-system-cycle countdown is the accepted current Phi2 model. If yes, document that it is the current project abstraction, not pin-level CIA timing.
4. Validate `$0000` latch behavior. Do not assume `$0000` means `$FFFF` unless the selected reference and tests justify it.
5. Ensure underflow timing is deterministic and testable with small latch values.
6. Do not add CNT or cascade modes yet; leave unsupported modes documented for Phase C.

## Required Tests And Diagnostics

- Timer A and Timer B basic countdown after start.
- Timer A and Timer B stopped timers do not decrement.
- Timer A and Timer B force-load copy latch to live counter.
- Timer A and Timer B continuous underflow reload and keep running.
- Timer A and Timer B one-shot underflow reload and stop.
- Timer A and Timer B source flags are set on underflow.
- Timer B CPU-level diagnostic writes `$DC06/$DC07`, starts CRB bit 0, waits, and reads a lower value.
- Regression for existing zero-latch and one-shot behavior.

## Acceptance Criteria

- A program that writes `$DC06/$DC07`, starts Timer B in Phi2 mode, waits, and reads `$DC06/$DC07` observes a decreasing counter.
- Timer A and Timer B both underflow and reload correctly in continuous mode.
- Timer A and Timer B both underflow, reload, and stop correctly in one-shot mode.
- Force-load copies latch to counter for both timers.
- Timer reads remain stable enough for ordinary low/high read sequences used by C64 code.
- Existing zero-latch timer reload and one-shot stop diagnostics continue to pass or are updated with a documented reference-backed behavior change.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase B complete, the chosen current Phi2 countdown abstraction, `$0000` latch policy, force-load policy, test names/diagnostics added, and timer modes still deferred to Phase C/J.
