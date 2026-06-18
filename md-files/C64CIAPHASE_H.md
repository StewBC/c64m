# C64 CIA Phase H - Serial Data Register, CNT/SP, and Shift Timing

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

Implement the CIA serial shift register and CNT/SP line behavior sufficiently for C64 software that uses CIA serial I/O, fast loaders, diagnostics, or peripheral protocols.

## Implementation Order Position

Run after timer modes and ICR behavior are reliable: A, B, D, C, then H. It may also benefit from Phase F's IEC line model, but SDR/CNT/SP should remain CIA-side behavior.

## Scope

- `$0C` SDR reads/writes interact with serial shift state, not only raw storage.
- Serial output shifts eight bits on SP using CNT timing.
- Serial input shifts eight bits from SP using CNT timing.
- Serial complete sets ICR bit 3.
- Enabled serial interrupt asserts IRQ/NMI through Phase D.
- Timer A serial-output timing is honored where applicable by CRA controls.
- CNT edges are synchronized to the project's CIA timing model.

## Out Of Scope

- Full disk-drive emulation.
- Full IEC protocol state machine.
- Pin-perfect line timing beyond deterministic project cadence.
- RS-232 complete emulation unless source inspection shows existing scoped support.

## Registers And Bits

```text
$0C SDR     Serial data register
CRA bit 6   Serial direction/control policy
ICR bit 3   Serial complete interrupt source
CNT         Serial clock/timing input/output line
SP          Serial data input/output line
```

## Repo Inspection Tasks

- Locate SDR raw register handling.
- Locate CRA bit 6 storage/use, if any.
- Locate CNT/SP line representation from Phase C/F, if present.
- Locate timer A underflow/output event code.
- Locate ICR source helper for serial complete.
- Locate any IEC or peripheral diagnostics.

## Implementation Guidance

1. Add explicit serial shift state: active/inactive, bit count, shift register, direction, and line levels as needed.
2. Keep CIA-side serial behavior independent from disk-drive protocol emulation.
3. Use existing timer/CNT event hooks instead of adding a separate scheduler if possible.
4. On eight shifted bits, update SDR/readable state and set ICR serial source.
5. Make input and output deterministic and testable with direct line/event hooks.
6. Ensure debugger-safe peeks do not advance shift state or clear serial interrupt flags.

## Required Tests And Diagnostics

- Writing SDR and enabling serial output shifts exactly eight bits.
- SP output bit order matches selected reference.
- CNT pulses occur at deterministic times or in response to test-injected edges.
- Serial input shifts eight SP bits into SDR.
- Serial complete sets ICR bit 3.
- Enabled serial source asserts IRQ on CIA #1 and NMI on CIA #2.
- Debugger-safe reads do not alter serial state.
- Regression for timers, ICR, CIA #2 IEC line behavior, and boot.

## Acceptance Criteria

- Writing SDR and enabling serial output shifts eight bits on SP with CNT timing.
- Serial input can shift eight bits into SDR and set the serial interrupt flag.
- ICR serial source follows the same mask, flag, read-clear, and IRQ/NMI behavior as other sources.
- Timer-driven serial transfer timing is deterministic and testable.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase H complete, SDR/CNT/SP behavior, serial timing policy, interrupt behavior, tests added, and explicit deferred peripheral/protocol limitations.
