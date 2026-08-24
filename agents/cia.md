# CIA

## Source of truth

`src/machine/cia.{c,h}`, keyboard in `keyboard.{c,h}`, C64 interrupt/IEC
wiring in `c64.c`. Tests: `tests/machine/test_c64_cia.c`,
`test_c64_keyboard.c`. Race-level evidence: `tools/cia-timing-corpus/`
(optional fetch, not a ctest gate).

Hooks: `cia_read_register()` / `cia_write_register()` for CPU access,
`cia_debug_read_register()` for side-effect-safe inspection,
`cia_step_cycle()`, `cia_pulse_cnt()` / `cia_set_sp_line()`,
`cia_set_flag_line()`, `cia_pc_line()`, `cia_interrupt_line()`.

## Behavior

CIA #1 and #2 implement timers A/B, ICR flags/masks, one-shot/continuous,
CNT/cascade, PB6/PB7 timer output, keyboard matrix, joysticks, TOD and alarm.

CIA #1 drives IRQ. CIA #2 drives the CPU NMI edge latch. RESTORE is a
separate one-shot NMI. CPU-visible reads have hardware side effects;
`cia_debug_read_register()` and debugger peeks do not clear ICR/TOD, shift
serial state, or pulse PC.

FLAG is negative-edge. Serial output shifts MSB-first on SP from Timer A
underflows; serial input shifts on external CNT pulses. Eight bits set ICR
bit 3. PRB CPU-visible access pulses PC low for one cycle.

`cia_interrupt_line()` is a **delayed** output pin. CPU IRQ/NMI sampling
uses it, not immediate `cia_irq_pending()`. A same-cycle result can be
correct for the latched ICR and wrong for the CPU pin. When diagnosing an
interrupt, log ICR flags, ICR mask, `cia_irq_pending()`,
`cia_interrupt_line()`, CPU sampling, and the NMI edge latch separately.

CIA #1 reads keyboard/joystick. CIA #2 selects the VIC bank and models
open-collector IEC ATN/CLK/DATA.

TOD is BCD, 12-hour AM/PM, coherent reads, alarm, configured 50/60 Hz.
Writing clock hours stops TOD; writing tenths restarts it. Alarm-register
writes do not halt the clock. Read latching (hours read freezes a snapshot
until tenths) is separate from write stop/start.

## Timer facts

LOW timer writes update the latch. A stopped timer loads on HIGH write.
Force-load is deferred. Underflow reloads and skips the next count clock.
START clear on a running timer is delayed one Phi2. Do not generalize that
delay to every CR bit without a failing test and a VICE/hardware compare.

CIA snapshot chunks store the delayed pin pipeline (`interrupt_ff`,
`interrupt_pending_latched`, `interrupt_line`). Missing that caused
spurious CIA2 NMI / `$FFFF` BRK after load-state. Timer delay bits
(`start_delay`, `skip_tick`, `load_delay`, ...) are **not** in the
snapshot (`known-gaps.md`).

## Limits

SP/CNT sub-cycle analog timing is absent. FLAG/SP/PC are not connected to
tape, RS-232, or user-port peripherals. No explicit 6526/6526A/8521 policy.

Use VICE/hardware as the oracle for race-level changes. Re-run the corpus
scripts; do not treat a remembered pass count as current.
