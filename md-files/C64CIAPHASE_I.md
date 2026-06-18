# C64 CIA Phase I - Handshake Lines, FLAG, PC Pulse, and Edge-Sensitive Behavior

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

Complete remaining CIA external-line behavior: FLAG interrupt input, PC pulse output, port handshaking, and edge-sensitive interactions used by peripherals and diagnostics.

## Implementation Order Position

Run after A, B, D, C, E/F, G, and H. This phase touches many external-line interactions and should not precede core timer, port, interrupt, TOD, or serial behavior.

## Scope

- FLAG external input line.
- FLAG selected edge/event sets ICR bit 4.
- Enabled FLAG source asserts IRQ/NMI through Phase D.
- PC pulse and port handshaking behavior for the selected CIA model.
- Read/write-triggered handshake effects occur only on CPU-visible accesses.
- Debugger-safe peeks do not generate handshake pulses or clear pending handshakes.
- Handshake state coexists with PRA/PRB/DDRA/DDRB behavior.

## Out Of Scope

- Pin-perfect edge race behavior; defer polish to Phase J.
- Full external peripheral emulation.
- New frontend UI for external pins unless already justified by tests.

## Registers And Sources

```text
ICR bit 4: FLAG interrupt source
PRA/PRB:  Port read/write interactions with handshaking
DDRA/DDRB: Direction behavior must coexist with handshaking
PC:        Pulse output line according to selected CIA model
FLAG:      External interrupt input line
```

## Repo Inspection Tasks

- Locate port read/write functions and debugger-safe peek alternatives.
- Locate external input command/event patterns used for keyboard, joystick, RESTORE, and IEC.
- Locate ICR helper for FLAG source setting.
- Locate any existing stubs or comments for PC/FLAG.

## Implementation Guidance

1. Add only the external-line state needed for FLAG and PC/handshake tests.
2. Keep CPU-visible read/write side effects separate from debugger-safe peeks.
3. Route FLAG through the same ICR mask/read-clear/output-line code as every other source.
4. If edge-sensitive behavior requires previous line level, store it in CIA state and update deterministically.
5. Do not add broad peripheral abstractions unless current source structure already has them and tests require them.

## Required Tests And Diagnostics

- FLAG edge sets ICR bit 4.
- Masked FLAG does not assert IRQ/NMI.
- Enabled FLAG asserts IRQ on CIA #1 and NMI on CIA #2.
- CPU-visible port read/write generates documented PC/handshake pulse.
- Debugger-safe peek does not generate PC/handshake pulse.
- Handshake behavior does not break keyboard, joystick, VIC bank, IEC, timer, TOD, serial, or interrupt tests.

## Acceptance Criteria

- FLAG edge tests set the ICR FLAG bit and assert IRQ/NMI when enabled.
- Reading or writing the relevant port registers produces the documented handshake pulses.
- Debugger-safe peeks do not generate handshake pulses or clear pending handshakes.
- Existing keyboard, joystick, VIC bank, IEC, timer, interrupt, TOD, and serial tests continue to pass.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase I complete, FLAG edge policy, PC/handshake behavior, tests added, and cycle-level edge cases deferred to Phase J.
