# C64 CIA Phase D - Interrupt Control Register and IRQ/NMI Line Behavior

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

Implement full CIA interrupt flag, mask, read-clear, write-mask, and output-line behavior. CIA #1 must assert CPU IRQ. CIA #2 must assert CPU NMI.

## Implementation Order Position

Run after Phase B and before Phase C unless Phase C is needed to expose missing timer source events. D is early because timer underflows become much more useful once interrupt behavior is reliable.

## Scope

- ICR read returns source flags and bit 7 when any enabled source is pending.
- ICR read clears the currently reported source flags.
- Debugger-safe peeks do not clear flags.
- ICR write changes interrupt masks, not interrupt flags.
- ICR write bit 7 selects set-mask vs clear-mask.
- ICR write bits 0-4 select affected source masks.
- Timer A and Timer B sources are fully wired.
- TOD, serial, and FLAG sources have reserved flag/mask handling even if their events are implemented in later phases.
- CIA #1 enabled-pending source asserts CPU IRQ.
- CIA #2 enabled-pending source asserts CPU NMI.
- Output line state updates when flags are set, masks change, or flags are cleared.

## Out Of Scope

- Implementing TOD, serial, or FLAG event generation.
- Full CPU pin-level NMI edge race accuracy.
- RESTORE behavior changes except preserving existing RESTORE NMI path.

## ICR Bits

```text
ICR bit 0: Timer A underflow
ICR bit 1: Timer B underflow
ICR bit 2: TOD alarm match
ICR bit 3: Serial port complete
ICR bit 4: FLAG line
ICR bit 7 on read: enabled-pending summary
ICR bit 7 on write: 1=set selected masks, 0=clear selected masks
```

## Repo Inspection Tasks

- Locate ICR flag/mask state and read/write code.
- Locate CPU IRQ pending callback/poll path.
- Locate CPU NMI input/path and existing RESTORE NMI implementation.
- Locate runtime/machine interrupt entry diagnostics.
- Locate debugger-safe CIA peek/memory snapshot code from Phase A.

## Implementation Guidance

1. Keep ICR flags and masks separate.
2. Do not let ICR writes directly clear source flags.
3. Ensure writing masks can immediately assert or deassert output lines depending on already-pending flags.
4. Ensure clearing flags by CPU-visible ICR read immediately updates output lines.
5. Route CIA #1 through the existing CPU IRQ aggregate path.
6. Route CIA #2 through CPU NMI in a way that preserves RESTORE. If CPU NMI is edge-sensitive internally, generate edges only when CIA #2 output transitions from inactive to active.
7. Add reserved source helpers for TOD/serial/FLAG so later phases can set flags without changing ICR semantics.

## Required Tests And Diagnostics

- Masked Timer A underflow sets flag but does not assert IRQ/NMI.
- Enabled Timer A underflow sets flag and asserts IRQ/NMI.
- Same for Timer B.
- ICR read returns bit 7 only when enabled-pending exists.
- ICR read clears flags and deasserts line if no enabled flags remain.
- ICR write with bit 7 set enables selected masks and preserves unselected masks.
- ICR write with bit 7 clear disables selected masks and preserves unselected masks.
- CIA #1 timer interrupt reaches CPU IRQ entry when CPU interrupt flag permits it.
- CIA #2 timer interrupt reaches CPU NMI entry.
- RESTORE still reaches CPU NMI.
- Debugger-safe ICR peek does not clear flags.

## Acceptance Criteria

- Masked timer underflows set ICR source flags but do not assert IRQ/NMI.
- Enabled timer underflows set source flags and assert IRQ/NMI.
- Reading ICR clears the reported flags and deasserts output if no enabled flags remain.
- Writing ICR with bit 7 set enables selected sources without changing unselected sources.
- Writing ICR with bit 7 clear disables selected sources without changing unselected sources.
- CIA #1 Timer A can drive a KERNAL-style periodic IRQ path.
- CIA #2 can generate an NMI through its interrupt output path.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase D complete, ICR semantics, CIA #1 IRQ status, CIA #2 NMI status, CPU interrupt diagnostics added, RESTORE regression result, and deferred TOD/serial/FLAG event generation.
