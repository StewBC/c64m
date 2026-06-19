# C64AUDFID_5.md
# CIA #2 NMI Verification And Reconciliation Guide

## Component

C64MCIA_NEW

## Status

Coding-agent-ready verification guide. Implementation is conditional.

## Purpose

Reconcile CIA #2 NMI behavior between code, tests, `STATUS.md`, and older CIA
planning documents. This is primarily an audit and verification task. Implement
only the minimal fix and tests needed if the audit proves a real behavior gap.

## Required Reading Before Work

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MCIA_NEW.md` if present
5. `C64MCIA.md`
6. Older CIA current-state or phase documents if referenced by `STATUS.md`
7. This guide

## Goal

Determine whether CIA #2 enabled interrupt sources can drive CPU NMI in the
current implementation and whether code, tests, and docs agree.

Resolve the result in the smallest correct way:

- If code and tests are correct but docs are stale, update docs.
- If docs are correct and code is wrong, fix code and add tests.
- If code is correct but tests are missing, add tests.
- If behavior is intentionally deferred, ensure `STATUS.md` says so clearly.

## In Scope

- Inspect CIA #2 interrupt flag and mask behavior.
- Inspect CIA #2 output-line wiring to CPU NMI.
- Inspect RESTORE NMI path and ensure it is not confused with CIA #2 NMI.
- Inspect CPU NMI edge/pending handling enough to verify CIA #2 delivery.
- Inspect tests and diagnostics.
- Add minimal tests for CIA #2 timer-to-NMI if missing.
- Add minimal implementation fix if CIA #2 NMI is absent or miswired.
- Update `STATUS.md` and stale planning notes.

## Explicit Non-Goals

Do not implement these in this phase unless directly required to verify CIA #2
NMI from an already-implemented source:

- CIA Phase I handshake lines.
- CIA FLAG behavior.
- CIA PC pulse behavior.
- CIA Phase J cycle-level/sub-Phi2 timing.
- Full CIA rewrite.
- TOD implementation unless already present and needed only as an existing source.
- Serial data register shifting.
- IEC serial bus protocol.
- 1541 emulation.
- Fast loaders.
- D64 writes.
- Cartridge support.
- VIC-II light pen.
- Open-bus behavior.

## Files To Inspect

Find actual names in the repo. Likely areas:

```text
src/machine/cia.*
src/machine/c64.*
src/machine/cpu_6510.*
src/machine/bus or interrupt routing files
src/runtime/debug or snapshot files
tests/*cia*
tests/*nmi*
tests/*restore*
STATUS.md
C64MCIA.md
C64MCIA_NEW.md
```

Search for:

```text
NMI
IRQ
ICR
RESTORE
cia2
CIA2
DD00
DD0D
timer underflow
```

## Verification Questions

Answer these in a short reconciliation note or in `STATUS.md`:

1. Does CIA #2 have an interrupt pending/line output distinct from CIA #1 IRQ?
2. Does CIA #2 ICR mask behavior match CIA #1 behavior for implemented sources?
3. Does CIA #2 Timer A underflow set ICR bit 0?
4. Does CIA #2 Timer B underflow set ICR bit 1?
5. Does an enabled CIA #2 interrupt source reach CPU NMI?
6. Is CPU NMI edge-sensitive or level-sensitive in the current code?
7. Does reading CIA #2 ICR clear the reported flags and deassert/rearm the line?
8. Is RESTORE NMI separate from CIA #2 NMI?
9. Do debugger-safe reads avoid clearing ICR flags?
10. Do docs and `STATUS.md` match the verified behavior?

## Minimal Expected Behavior

For this milestone, the required behavior is:

- CIA #2 uses the same implemented ICR flag/mask logic as CIA #1.
- Enabled CIA #2 timer interrupts can assert CPU NMI.
- CPU enters the NMI path when CIA #2 asserts NMI.
- Reading CIA #2 ICR clears flags according to implemented CIA ICR policy.
- RESTORE can still generate NMI through its existing path.
- RESTORE and CIA #2 do not incorrectly block each other.

This guide does not require adding unimplemented sources such as FLAG, serial,
or TOD solely to test NMI.

## Audit Procedure

1. Run existing tests to establish baseline.
2. Inspect CIA ICR implementation.
3. Inspect CIA #1 IRQ routing.
4. Inspect CIA #2 NMI routing.
5. Inspect CPU NMI request/latch/edge handling.
6. Inspect RESTORE NMI path.
7. Inspect debugger-safe read paths for CIA ICR.
8. Search `STATUS.md` and planning docs for claims about CIA #2 NMI.
9. Add or run a minimal CIA #2 timer-to-NMI diagnostic.
10. Decide: docs-only update, tests-only update, minimal code fix, or deferred.

## Minimal Diagnostic Program

Create a CPU-level test or emulator integration test equivalent to:

```text
1. Install an NMI vector pointing to a small handler that increments a memory byte
   and RTIs.
2. Configure CIA #2 Timer A latch to a small value.
3. Clear pending CIA #2 ICR flags by reading $DD0D.
4. Enable CIA #2 Timer A interrupt by writing $81 to $DD0D.
5. Start CIA #2 Timer A.
6. Run enough cycles for underflow.
7. Assert the NMI handler ran exactly or at least once according to test harness
   stability.
8. Read $DD0D and confirm timer interrupt flag behavior.
9. Confirm CIA #1 IRQ state was not required for the NMI.
```

If the project has direct machine tests instead of CPU-program tests, assert the
same behavior through machine state:

- CIA #2 timer underflow sets the CIA #2 ICR flag.
- Enabled CIA #2 pending output invokes CPU NMI request.
- CPU NMI observable state changes after instruction boundary according to the
  existing CPU model.

## Minimal Code Fix Policy

If CIA #2 NMI is missing, add the smallest routing fix.

Likely shape:

```text
cia2 enabled interrupt pending -> c64 machine interrupt routing -> cpu NMI input/request
```

Do not rewrite CIA. Do not implement unneeded CIA phases. Do not add a new event
queue for NMI.

Ensure:

- CIA #1 still drives IRQ, not NMI.
- CIA #2 drives NMI, not IRQ.
- RESTORE still drives NMI.
- Multiple NMI sources are combined safely.
- Clearing CIA #2 ICR can deassert/rearm CIA #2 NMI according to current CPU NMI
  model.

If the CPU NMI model cannot distinguish edge rearming, document the limitation
and fix only if it blocks the minimal diagnostic.

## Documentation Reconciliation

When done, update `STATUS.md` and stale docs with one of these results:

```text
CIA #2 NMI verified implemented and tested.
CIA #2 NMI implemented but newly tested in this guide.
CIA #2 NMI was missing and has been minimally implemented and tested.
CIA #2 NMI remains deferred, with reason.
```

If older planning docs said CIA #2 NMI was absent but code now has it, mark that
older statement stale or add a note pointing to current status. Do not leave two
contradictory claims without explanation.

## Tests And Regression Requirements

Required after any code change:

- CIA #1 timer IRQ test still passes or is added if absent.
- CIA #2 timer NMI test passes.
- RESTORE NMI test still passes or is added if absent.
- ICR read-clear behavior still passes for CIA #1 and CIA #2.
- Existing boot, keyboard, joystick, VIC bank, debugger, PRG, D64, PAL, and NTSC
  tests still pass.

If tests cannot be automated, add a manual diagnostic with clear expected output,
but prefer automated tests.

## Acceptance Checklist

This guide is complete only when all items below are true:

- CIA #2 ICR behavior has been inspected.
- CIA #2 interrupt output routing has been inspected.
- CPU NMI request/entry path has been inspected.
- RESTORE NMI path has been checked as separate from CIA #2.
- Docs and tests have been checked for disagreement.
- A minimal CIA #2 timer-to-NMI test or diagnostic exists, or the exact reason it
  cannot exist is documented.
- Any missing CIA #2 NMI routing is fixed with the smallest viable code change.
- CIA #1 IRQ behavior is not regressed.
- RESTORE NMI behavior is not regressed.
- No CIA Phase I, Phase J, IEC, or full CIA rewrite work is added.
- `STATUS.md` accurately reflects the verified behavior and any remaining CIA
  deferrals.
