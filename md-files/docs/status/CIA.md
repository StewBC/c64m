# CIA status

## Current implementation

- CIA is complete through Phase G.
- CIA #1 and CIA #2 routing are implemented.
- Timers, ICR, IRQ/NMI behavior, keyboard, joystick, RESTORE, CIA #2 VIC bank control, IEC port pins, TOD, and alarm are implemented.

## Important invariants

- CPU-visible CIA reads have side effects.
- Debugger-safe reads avoid side effects.
- Timer A and Timer B use project-level cycle countdown semantics.
- Timer latch/live counters, force-load strobe, one-shot/continuous modes, CNT and cascade sources, and PB6/PB7 output behavior are modeled.
- ICR masks and flags are separate.
- Normal ICR reads clear reported flags.
- Debugger peeks do not clear ICR or TOD state.
- CIA #1 drives CPU IRQ.
- CIA #2 drives the CPU NMI edge latch.
- RESTORE remains a separate one-shot NMI source.
- CIA #1 handles bidirectional keyboard matrix and joystick ports.
- CIA #2 handles VIC bank selection and IEC ATN/CLK/DATA open-collector line modeling.
- TOD uses BCD tenths/seconds/minutes/hours, 12-hour AM/PM, 50/60 Hz source policy, coherent read latch, and alarm ICR source.

## Recent changes

- C64MENH Phase 1 reconciled CIA #2 NMI status.
- Current code and tests confirm that CIA #1 interrupt output routes to CPU IRQ.
- Current code and tests confirm that CIA #2 enabled-pending interrupt output routes to CPU NMI callback through an edge latch.
- CPU NMI sampling occurs at instruction entry before IRQ.
- Older `C64MCIA.md` text was updated to remove the stale claim that CIA #2 NMI was not wired.

## Known limitations / deferred

- Full CIA accuracy and pin/race-level timing are deferred.
- Exact sub-cycle pin behavior is not modeled.

## Tests / smoke checks

- Confirm normal CIA ICR reads clear reported flags.
- Confirm debugger-safe CIA peeks do not clear ICR or TOD state.
- Confirm CIA #1 IRQ pending can remain pending without CPU IRQ entry while the CPU interrupt-disable flag is set.
- Confirm RESTORE still behaves independently from CIA #2 NMI.

## Files likely involved

- `src/machine/cia*`
- `src/machine/c64*`
- `src/runtime/*`
- CIA and interrupt tests under `tests/`
