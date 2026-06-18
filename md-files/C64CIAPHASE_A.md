# C64 CIA Phase A - Register Map, Mirroring, Safe Reads, and Current-State Reconciliation

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

Lock down the current CIA register contract and reconcile the mismatch between the CIA roadmap, CPU-visible behavior, debugger-visible behavior, and manual observation that `$DC06` appears to remain `$FF`. This is primarily an audit-and-correction phase.

## Implementation Order Position

This phase must be first. Later timer, interrupt, port, TOD, and serial phases depend on knowing which reads are CPU-visible side-effecting reads and which reads are debugger-safe peeks.

## Scope

- Verify CIA #1 maps to `$DC00-$DCFF` and CIA #2 maps to `$DD00-$DDFF`.
- Verify both 256-byte pages mirror through `addr & 0x0F`.
- Verify all 16 CIA register offsets exist for both chips.
- Verify CPU-visible reads use normal CIA read semantics.
- Verify debugger/memory snapshot reads avoid side effects such as ICR clear-on-read and future TOD latch transitions.
- Verify Timer A live counter reads at `$04/$05`.
- Verify Timer B live counter reads at `$06/$07`.
- Verify Timer A/B latch writes through `$04/$05` and `$06/$07`.
- Reconcile why `$DC06` may read as `$FF` in manual testing.
- Document deterministic reset behavior for registers, latches, counters, masks, flags, and output lines.

## Out Of Scope

- Full timer mode matrix.
- CNT input.
- Timer B cascade.
- PB6/PB7 timer outputs.
- TOD clock behavior beyond safe-read implications.
- Serial shift behavior.
- FLAG and handshaking.
- Open-bus hardware accuracy.

## CIA Register Map To Verify

Both CIA chips expose these offsets within the mirrored page:

```text
$00 PRA     Port A data
$01 PRB     Port B data
$02 DDRA    Port A direction
$03 DDRB    Port B direction
$04 TAL     Timer A low
$05 TAH     Timer A high
$06 TBL     Timer B low
$07 TBH     Timer B high
$08 TOD10   TOD tenths
$09 TODSEC  TOD seconds
$0A TODMIN  TOD minutes
$0B TODHR   TOD hours
$0C SDR     Serial data
$0D ICR     Interrupt control
$0E CRA     Control A
$0F CRB     Control B
```

## Repo Inspection Tasks

- Locate the generic CIA implementation and CIA state struct.
- Locate C64 bus read/write routing for `$DCxx` and `$DDxx`.
- Locate machine reset initialization for both CIA instances.
- Locate CPU-visible memory read/write paths.
- Locate debugger-safe memory snapshot/peek paths.
- Locate tests that already cover CIA mirroring, timer registers, ICR, debugger memory views, keyboard reads, VIC-bank selection, and boot smoke behavior.

## Implementation Guidance

1. Add or harden a non-side-effecting CIA peek path if debugger/snapshot reads currently bypass too much state or accidentally trigger read side effects.
2. Make the distinction explicit in code comments or API names: normal CIA read vs debugger-safe peek.
3. Ensure CPU-visible Timer B low/high reads return the live counter, not stale raw register storage.
4. Ensure Timer B low/high writes update the latch and, when stopped, match the currently documented immediate counter-load behavior unless Phase B changes it with evidence.
5. Trace the `$DC06 == $FF` observation through a minimal CPU-visible diagnostic. Identify whether the issue is the diagnostic, debugger path, CRB start state, latch value, timer stepping, bus routing, or a bug.
6. Keep reset deterministic unless a later phase explicitly models hardware power-on randomness.

## Required Tests And Diagnostics

- Register mirroring test for representative addresses across `$DC00-$DCFF` and `$DD00-$DDFF`.
- CPU-visible read/write test for all 16 offsets on both CIAs where raw storage behavior is expected.
- Timer B stopped-write test: write `$DC06/$DC07`, verify latch update and current phase's stopped-load behavior.
- Timer B running diagnostic: start Timer B and observe a changing live counter through CPU-visible reads.
- Debugger-safe read test: pending ICR flags survive debugger memory snapshot/peek reads.
- Regression run for boot, keyboard, VIC-bank, and debugger memory tests.

## Acceptance Criteria

- `$DC00-$DC0F` and `$DD00-$DD0F` mirror correctly through their whole 256-byte CIA pages.
- CPU-visible `$DC06/$DC07` reads are proven to come from Timer B's live counter.
- CPU-visible `$DC06/$DC07` writes are proven to update Timer B's latch and current stopped-load behavior.
- A minimal CPU-level diagnostic starts Timer B and observes a changing live counter, or the exact defect preventing it is identified and fixed.
- Debugger memory views can inspect CIA registers without clearing ICR flags or changing future TOD latch state.
- Existing boot, keyboard, VIC-bank, and debugger memory tests continue to pass.

## STATUS.md Update Requirements

Update `md-files/STATUS.md` with: Phase A complete, the verified CIA register/mirroring contract, the CPU-visible vs debugger-safe read policy, Timer B reconciliation result, tests added or updated, and any remaining raw-vs-live debugger limitation.
