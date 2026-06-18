# C64 CIA Phase K - Validation Suite, Debugger Visibility, and Compatibility Corpus

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

Make CIA accuracy maintainable by adding regression tests, diagnostics, debugger surfaces, and external compatibility programs that prevent regressions and make future changes safer.

## Implementation Order Position

Run last as the consolidation phase, after A through J. It may add tests for all earlier phases but should not introduce new emulator behavior except debugger visibility that follows snapshot rules.

## Scope

- Unit tests for each CIA register group.
- Integration tests for CPU IRQ/NMI, keyboard/joystick reads, VIC bank switching, IEC behavior, TOD, serial, FLAG, and handshake behavior.
- Timer tests through direct register reads, ICR behavior, and CPU interrupt entry.
- Debugger visibility for copied CIA state if useful: latches, counters, control registers, ICR flags/masks, TOD, port effective levels, serial/handshake state.
- Debugger views use copied snapshots and safe peeks only.
- Compatibility corpus using small local assembly diagnostics and public CIA tests where licensing permits.
- `md-files/STATUS.md` accurately tracks pass/fail status and remaining gaps.

## Out Of Scope

- Implementing missing CIA functional behavior from Phases A-J.
- Importing public test programs without checking licensing.
- Exposing live machine pointers to frontend/debugger.

## Repo Inspection Tasks

- Locate all existing unit/integration test frameworks.
- Locate debugger snapshot structs and frontend hardware/debugger tabs.
- Locate assembly diagnostic build/run tooling, if any.
- Locate license policy or precedent for third-party test assets.
- Locate `md-files/STATUS.md` and any related high-level planning docs.

## Implementation Guidance

1. Start by inventorying current CIA test coverage and mapping tests to phases A-J.
2. Add missing local diagnostics before importing external programs.
3. If adding debugger CIA state, publish copied state from runtime snapshots only. Do not add frontend access to live machine objects.
4. Keep debugger safe-peek behavior side-effect free.
5. Track compatibility corpus results succinctly in status: test name, source/license, current pass/fail, and notable limitations.
6. Prefer small deterministic tests over broad opaque ROM tests when isolating regressions.

## Required Tests And Diagnostics

- Register map/mirroring/safe-read coverage.
- Timer core coverage.
- Timer mode/PB6/PB7/cascade coverage.
- ICR and IRQ/NMI coverage.
- CIA #1 keyboard/joystick/RESTORE coverage.
- CIA #2 VIC bank/IEC coverage.
- TOD coverage.
- SDR/CNT/SP coverage.
- FLAG/PC/handshake coverage.
- Cycle-edge/variant coverage from Phase J.
- Debugger CIA snapshot/peek tests if debugger visibility is added.

## Acceptance Criteria

- CIA register, timer, interrupt, port, TOD, serial, and handshake tests exist.
- Existing boot, display, keyboard, debugger, breakpoint, and VIC-II tests continue to pass.
- Debugger CIA state, if added, follows the snapshot rule.
- `md-files/STATUS.md` accurately names implemented CIA phases and remaining gaps.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase K complete, the CIA test inventory/corpus summary, debugger visibility status, external test license/pass-fail notes, remaining not-implemented items, and any future maintenance instructions.
