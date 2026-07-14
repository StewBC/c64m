# CIA handoff

## Source of truth

Implementation: `src/machine/cia.{c,h}`, keyboard integration in `keyboard.{c,h}`,
and C64 interrupt/IEC wiring in `c64.c`. Tests: `tests/machine/test_c64_cia.c`,
`tests/machine/test_c64_keyboard.c`, and the corpus under
`md-files/corpus/cia-timing/`.

Public device hooks are `cia_read_register()`/`cia_write_register()` for CPU access,
`cia_debug_read_register()` for side-effect-safe inspection, `cia_step_cycle()` for
timing, `cia_pulse_cnt()`/`cia_set_sp_line()` for serial edges,
`cia_set_flag_line()` for FLAG, `cia_pc_line()` for the handshake output, and
`cia_interrupt_line()` for the delayed interrupt pin.

## Current behavior

- CIA #1 and #2 implement timers A/B, ICR flags/masks, one-shot/continuous mode,
  CNT/cascade timing, PB6/PB7 timer output, keyboard matrix, joysticks, TOD and
  alarm.
- CIA #1 drives IRQ. CIA #2 drives the CPU NMI edge latch. RESTORE is a separate
  one-shot NMI source.
- CPU-visible reads have hardware side effects; `cia_debug_read_register()` and
  debugger peeks do not clear ICR/TOD, shift serial state, or pulse PC.
- FLAG is negative-edge triggered. Serial output shifts MSB-first on SP from Timer
  A underflows; serial input shifts on external CNT pulses. Eight bits set ICR bit 3.
- PRB CPU-visible access pulses PC low for one cycle. `cia_interrupt_line()` is a
  delayed output pin; CPU IRQ/NMI sampling uses it, not immediate `cia_irq_pending()`.
- CIA #1 reads keyboard/joystick inputs. CIA #2 selects the VIC bank and models
  open-collector IEC ATN/CLK/DATA lines.
- TOD is BCD, 12-hour AM/PM, with coherent reads, alarm, and configured 50/60 Hz
  source timing.

## Timer timing facts

The current model has project-level Phi2 timing: LOW timer writes update the latch;
a stopped timer loads on HIGH write; force-load is deferred; underflow reloads and
skips the next count clock; START clear on a running timer is delayed one Phi2.
Do not generalize that delay to every CR bit without a failing test and a hardware/
VICE comparison.

## Remaining limits

The CIA corpus is not fully green for c64m: the documented current result is 13/31
priority cases passing, with remaining timer/IR race work, cycle-stamped dual logs,
and no explicit 6526/6526A/8521 policy. SP/CNT sub-cycle analog timing is absent.
FLAG/SP/PC seams are not yet connected to concrete tape, RS-232, or user-port
peripherals.

Use the VICE/hardware corpus as the oracle for race-level changes. Keep immediate
ICR state, delayed interrupt pin, and CPU sampling as separate concepts.

When diagnosing an interrupt, log ICR flags, ICR mask, `cia_irq_pending()`,
`cia_interrupt_line()`, CPU IRQ/NMI sampling, and the edge latch separately. A
same-cycle result can be correct for the latched state and wrong for the CPU pin.
