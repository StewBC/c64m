# C64MENH Phase 1: CIA #2 NMI Reconciliation

## Purpose

Determine whether CIA #2 NMI behavior is already correct, partially correct, or inconsistent across code, tests, `STATUS.md`, and planning documents.

This is an analysis-first milestone task. Do not assume there is a bug. The primary deliverable is reconciliation: code, tests, and documentation should agree.

## Background

The current milestone requires CIA #2 NMI behavior to be reconciled before the PAL/NTSC ordinary-software fidelity milestone can be claimed.

Known project facts from the current status documents:

- CIA #1 drives IRQ.
- CIA #2 drives NMI edge latch.
- CIA #2 handles VIC bank selection and IEC port pins.
- CIA timer, ICR, IRQ/NMI behavior is implemented through the current CIA phase.
- The milestone still calls out CIA #2 NMI reconciliation as a completion criterion.

This means the work may be either:

- confirming the implementation and tests are already correct, then documenting that fact;
- finding stale planning/status text and correcting it;
- finding a real behavior mismatch and fixing it with tests.

## Required Reading

Read in this order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MCIA_NEW.md`, if present
5. Any current CIA phase or implementation guide, if present
6. Current CIA code and tests

Do not guess when documents disagree. Treat disagreement as the task.

## Suggested Branch

Use a dedicated branch:

```sh
git checkout -b enhancement/cia2-nmi-reconcile
```

## Phase Goal

Answer these questions with evidence:

1. What does the current implementation do for CIA #2 NMI generation and edge latching?
2. What do the tests currently assert?
3. What do planning documents and `STATUS.md` claim?
4. Are code, tests, and documents consistent?
5. If not, which source is stale or wrong?
6. Is any code change needed, or is this only documentation/test reconciliation?

## Files And Areas To Inspect

At minimum, inspect:

```text
src/machine/cia.c
src/machine/cia.h
src/machine/c64.c
src/machine/c64_bus.c
src/machine/c6510.c
src/machine/c6510_inln.h
tests/machine/test_c64_cia.c
tests/machine/test_c64_cpu_validation.c
md-files/STATUS.md
md-files/C64MCIA_NEW.md, if present
```

Search terms:

```text
NMI
IRQ
ICR
cia2
cia_2
cia2_nmi_line
nmi_pending
restore
edge
underflow
interrupt
```

## Important Behavior To Verify

Verify and document:

- CIA #1 interrupt output routes to CPU IRQ.
- CIA #2 interrupt output routes to CPU NMI.
- CPU NMI handling is edge-triggered as expected by the emulator model.
- CIA #2 NMI edge latch is not repeatedly triggered while the line remains asserted.
- CIA #2 ICR read side effects behave correctly.
- RESTORE/NMI behavior remains isolated from CIA #2 behavior where appropriate.
- NMI sampling is tied to CPU instruction-entry behavior in the current CPU model.
- Debugger-safe CIA peeks do not clear ICR flags or otherwise cause side effects.

## Analysis Commands

Suggested inspection commands:

```sh
git status --short --branch
rg -n "NMI|nmi|IRQ|irq|ICR|icr|cia2|cia_2|restore|RESTORE|edge|underflow" src tests md-files
nl -ba src/machine/cia.c | sed -n '1,260p'
nl -ba src/machine/cia.c | sed -n '260,620p'
nl -ba src/machine/c64.c | sed -n '1,180p'
nl -ba src/machine/c64.c | sed -n '700,880p'
nl -ba tests/machine/test_c64_cia.c | sed -n '1,260p'
nl -ba tests/machine/test_c64_cia.c | sed -n '650,820p'
```

Adjust line ranges as needed.

## Stop / Continue Gate

After analysis, stop and write a short decision note before implementing anything.

Continue only if at least one of these is true:

- code and tests disagree;
- tests do not cover the documented behavior;
- documents are stale or ambiguous;
- there is a small, clear implementation fix with focused tests.

Stop without code changes if:

- code, tests, and documents already agree;
- no milestone-relevant ambiguity remains;
- proposed changes would expand into full CIA cycle-level accuracy or pin-race behavior outside the current milestone.

## Implementation Guidance If Continuing

If behavior is wrong or under-tested:

1. Add the smallest focused test that demonstrates the mismatch.
2. Fix the smallest relevant code path.
3. Run targeted CIA tests.
4. Run the full test suite.
5. Update `STATUS.md` only if the implementation and tests support the claim.

Do not broaden this phase into:

- full CIA pin/race-level timing;
- FLAG/PC/handshake lines unless required by accepted in-scope tests;
- IEC serial bus protocol;
- sub-Phi2 timing.

## Acceptance Criteria

This phase is complete when:

- CIA #2 NMI behavior is explicitly reconciled across code, tests, and documents.
- Any implementation changes are covered by focused tests.
- Existing CIA, CPU, VIC, runtime, PRG, D64, and smoke tests still pass.
- `STATUS.md` reflects reality if a durable status claim changed.
- Deferred CIA behavior remains explicitly deferred.

## Required Commands Before Final Hand-Off

At minimum:

```sh
cmake --build build
ctest --test-dir build
```

If the build directory or generator differs, use the repo's normal build/test commands and report the exact commands used.

Do not run `./build/c64m` without a timeout.

## Hand-Off Report

End with a concise hand-off report containing:

- branch name;
- commit hash or note that changes are uncommitted;
- exact commands run;
- files inspected;
- files changed;
- tests run and results;
- code/tests/docs reconciliation result;
- whether CIA #2 NMI is now complete for the current milestone;
- any remaining uncertainty;
- recommended next step.
