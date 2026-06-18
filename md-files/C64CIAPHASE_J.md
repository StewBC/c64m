# C64 CIA Phase J - Cycle-Level Accuracy, Read/Write Edge Cases, and Hardware Variants

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

Tighten CIA behavior from functional correctness to hardware-level compatibility. This phase should only run after visible register, timer, interrupt, port, TOD, serial, and handshake behavior are implemented and tested.

## Implementation Order Position

Run near the end, after Phases A through I. Do not use this phase to hide missing basic behavior from earlier phases.

## Scope

- CIA timers and line transitions advance from the machine's timed bus-event model.
- CPU-visible reads/writes occur at deterministic event cycles within instructions.
- Underflow, reload, ICR flag setting, PB pulse/toggle, and IRQ/NMI output changes occur at selected cycle positions.
- Reads around underflow boundaries match documented or selected-reference behavior.
- Writes to latches, force-load, start/stop, and ICR masks near underflow are deterministic.
- ICR read-clear races with newly arriving sources are handled deliberately.
- Unused bits and open-bus behavior follow the selected project model.
- Default CIA chip variant policy is selected and documented.

## Out Of Scope

- Adding missing Phase A-I functional behavior.
- Broad variant-selection UI unless required by tests or maintainers.
- Last-byte-on-bus modeling unless explicitly authorized and backed by tests.

## Repo Inspection Tasks

- Locate timed bus event infrastructure and CPU read/write classification.
- Locate machine master-cycle advancement.
- Locate all CIA step/read/write paths and their timestamp inputs.
- Locate VIC-II timing interactions that may share bus event assumptions.
- Locate tests for timed `$D020` writes, BA stalling, interrupts, and memory snapshots as examples of cycle-level testing style.

## Implementation Guidance

1. First write diagnostics that expose current edge behavior before changing code.
2. Choose the default CIA reference model: 6526, 6526A, 8521, or an explicit project compatibility model.
3. Document any deviations from hardware that are intentional emulator policy.
4. Keep cycle scheduling local and explicit. Do not create speculative generalized pin simulation.
5. Treat open-bus/unused-bit behavior conservatively and test every claimed value.
6. Preserve architecture and runtime ownership: timing accuracy must not leak live machine state to frontend/debugger code.

## Required Tests And Diagnostics

- Timer edge tests around `$0001 -> $0000 -> underflow -> reload`.
- Reads of timer low/high near underflow.
- Force-load/start/stop writes during countdown.
- ICR read-clear coinciding with new interrupt source.
- ICR mask writes coinciding with pending flags.
- PB6/PB7 pulse/toggle exact timing.
- IRQ/NMI assertion/deassertion timing.
- Unused-bit/open-bus behavior for CIA registers according to selected policy.
- Regression for all prior CIA phases and boot/display/debugger tests.

## Acceptance Criteria

- Timer-edge tests around `$0001->$0000->underflow->reload` pass with cycle-level expected results.
- ICR read-clear race tests are deterministic.
- Force-load/start/stop writes during countdown produce expected results.
- PB6/PB7 pulse/toggle timing is correct at the selected cycle granularity.
- The selected default CIA variant is documented in `md-files/STATUS.md`.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase J complete, selected CIA variant/model, cycle-level scheduling policy, edge-case behavior, tests added, and any explicitly deferred hardware-level limitations.
