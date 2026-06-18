# C64 CIA Phase G - Time-of-Day Clock and Alarm

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

Implement the CIA time-of-day clock, TOD register latching, alarm registers, BCD representation, AM/PM hour behavior, and TOD interrupt source.

## Implementation Order Position

Run after A, B, and D. This phase depends on safe peeks, ICR source handling, and reliable machine timing.

## Scope

- `$08` TOD tenths.
- `$09` TOD seconds in BCD.
- `$0A` TOD minutes in BCD.
- `$0B` TOD hours in BCD with AM/PM behavior.
- TOD advances from a 50 Hz or 60 Hz source according to CRA bit 7 and machine configuration policy.
- Multi-byte TOD reads use 6526 latch sequencing to produce coherent values.
- Debugger-safe peeks do not disturb TOD latch state.
- TOD writes set clock or alarm depending on CRB bit 7.
- Alarm match sets ICR bit 2.
- Enabled TOD interrupt asserts CIA #1 IRQ or CIA #2 NMI through Phase D output logic.

## Out Of Scope

- Pin-perfect TOD clock input.
- Hardware variant differences beyond the selected project policy.
- Serial/FLAG/handshake behavior.

## Registers And Bits

```text
$08 TOD10   Tenths
$09 TODSEC  Seconds, BCD
$0A TODMIN  Minutes, BCD
$0B TODHR   Hours, BCD plus AM/PM policy
CRA bit 7   TOD clock source selection policy
CRB bit 7   TOD alarm-write select
ICR bit 2   TOD alarm interrupt source
```

## Repo Inspection Tasks

- Locate machine PAL/NTSC configuration and timing cadence.
- Locate CIA control register storage and read/write code.
- Locate debugger-safe CIA peek path.
- Locate ICR helper for setting reserved source flags.
- Locate tests for PAL/NTSC timing and machine reset.

## Implementation Guidance

1. Store TOD clock and alarm in explicit CIA state rather than raw register passthrough.
2. Validate BCD ranges and define behavior for invalid BCD writes according to the selected reference or deterministic emulator policy.
3. Implement TOD read latching exactly enough for coherent multi-byte reads. Document the chosen latch sequence in comments/tests.
4. Ensure debugger-safe peeks can inspect TOD state without starting/stopping/clearing latches.
5. Decide PAL/NTSC default TOD source policy and document it in status.
6. Use Phase D ICR helper for TOD alarm flag setting; do not implement a separate interrupt path.

## Required Tests And Diagnostics

- TOD advances at expected 50/60 Hz cadence for selected machine configuration.
- Seconds/minutes/hours roll over in BCD.
- AM/PM hour behavior follows selected reference.
- Multi-byte read across a boundary returns coherent latched values.
- TOD writes set the clock when CRB bit 7 is clear.
- TOD writes set the alarm when CRB bit 7 is set.
- Alarm match sets ICR bit 2.
- Enabled TOD source asserts IRQ for CIA #1 and NMI for CIA #2.
- Debugger-safe TOD peeks do not alter latch state.

## Acceptance Criteria

- TOD advances at the expected PAL/NTSC cadence.
- Multi-byte TOD reads produce coherent values across second/minute/hour boundaries.
- TOD writes set the clock when alarm-select is clear.
- TOD writes set alarm registers when alarm-select is set.
- Alarm match sets the ICR TOD flag and asserts IRQ/NMI when enabled.
- TOD debugger peeks do not alter CPU-visible TOD latch state.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase G complete, TOD source/cadence policy, BCD/hour policy, latch policy, alarm/interrupt behavior, tests added, and any variant or edge-case limitations deferred to Phase J.
