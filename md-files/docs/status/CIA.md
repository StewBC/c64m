# CIA status

## Current implementation

- CIA is complete through Phase G, plus the full-accuracy pin/serial work from
  `C64MFULL_CIA.md` Phases 1-4 (FLAG, serial SDR/CNT/SP, PC handshake, and a
  conservative delayed interrupt-line model).
- CIA #1 and CIA #2 routing are implemented.
- Timers, ICR, IRQ/NMI behavior, keyboard, joystick, RESTORE, CIA #2 VIC bank control, IEC port pins, TOD, and alarm are implemented.
- FLAG external interrupt input (ICR bit 4), the serial shift register (SDR /
  CNT / SP, ICR bit 3), and the PC handshake pulse are implemented.

## Important invariants

- CPU-visible CIA reads have side effects.
- Debugger-safe reads avoid side effects.
- Timer A and Timer B use project-level cycle countdown semantics.
- Timer latch/live counters, force-load strobe, one-shot/continuous modes, CNT and cascade sources, and PB6/PB7 output behavior are modeled.
- ICR masks and flags are separate.
- Normal ICR reads clear reported flags.
- Debugger peeks do not clear ICR or TOD state, and do not advance serial shift
  state or pulse the PC handshake line.
- CIA #1 drives CPU IRQ.
- CIA #2 drives the CPU NMI edge latch.
- RESTORE remains a separate one-shot NMI source.
- CIA #1 handles bidirectional keyboard matrix and joystick ports.
- Joystick port state has two host input sources: SDL game controllers and the host keyboard (see `docs/status/FRONTEND_DEBUGGER.md`). Both are combined in the frontend and delivered through `c64_set_joystick`; the CIA side is unchanged.
- CIA #2 handles VIC bank selection and IEC ATN/CLK/DATA open-collector line modeling.
- TOD uses BCD tenths/seconds/minutes/hours, 12-hour AM/PM, 50/60 Hz source policy, coherent read latch, and alarm ICR source.
- FLAG is negative-edge triggered: a high->low transition on `cia_set_flag_line`
  raises ICR bit 4. Holding FLAG low does not re-raise without a new edge.
- Serial output is clocked by Timer A underflows (one bit per two underflows,
  MSB first); serial input is clocked by external CNT edges (`cia_pulse_cnt`
  sampling the SP line via `cia_set_sp_line`). Either direction sets ICR bit 3
  after eight bits.
- The PC line pulses low for the one cycle following a CPU-visible PRB read or
  write, then returns high (`cia_pc_line`).
- `cia_irq_pending` reports the immediate latched ICR (flags & mask) state.
  `cia_interrupt_line` is the delayed output pin (asserts/deasserts one cycle
  behind the latched state). **Option-2 Phase 4:** the CPU IRQ (CIA #1) and NMI
  edge (CIA #2) paths sample `cia_interrupt_line`, so CPU-visible interrupt
  timing includes the 6526 one-cycle delay. RESTORE remains a separate NMI
  source. VIC IRQ is still OR'd with CIA #1 on the IRQ line.

## Recent changes

- **Option-2 Phase 4:** `c64_cpu_irq_pending` / `c64_cpu_nmi_pending` now use
  `cia_interrupt_line` (delayed pin) instead of immediate `cia_irq_pending`.
  Unit tests cover the one-cycle CPU delay; existing IRQ/NMI vector tests step
  the delay pipeline before expecting CPU entry. VICE priority corpus remains
  the external oracle (`md-files/corpus/cia-timing/`).
- C64MFULL Phases 1-4 added FLAG, serial SDR/CNT/SP, PC handshake, and the
  delayed interrupt-line abstraction. Public CIA API includes
  `cia_set_flag_line`, `cia_set_sp_line`, `cia_pc_line`, `cia_interrupt_line`.
- C64MENH Phase 1 reconciled CIA #2 NMI status.
- Current code and tests confirm that CIA #1 interrupt output routes to CPU IRQ.
- Current code and tests confirm that CIA #2 enabled-pending interrupt output routes to CPU NMI callback through an edge latch.
- CPU NMI sampling occurs at instruction entry before IRQ.

## Known limitations / deferred

- CPU-visible one-cycle interrupt delay is wired (Option-2 Phase 4). Remaining
  fidelity work: greening the c64m PRG corpus matrix, cycle-stamped dual-emulator
  event logs for hard races, and explicit 6526 vs 6526A vs 8521 variant policy.
  See `md-files/corpus/cia-timing/`.
- Corpus tools: `tools/cia-timing-corpus/` (`run_x64sc.sh`, `run_c64m.sh`).
  VICE baselines green on priority/lorenz-cia/cia-core. c64m priority matrix:
  **11 PASS / 18 FAIL / 2 OTHER** (`results/c64m-priority-latest.tsv`) after
  Lorenz start-delay + underflow-on-1 pipeline. Timers: two Phi2 clocks after
  START before counting; underflow reloads from latch and discards the next
  count clock (phi2 sequence 2-1-2-2-…).
- 6526 vs 6526A vs 8521 chip-variant policy is not modeled yet (corpus runs both
  `-ciamodel 0` and `1` as separate cases).
- Serial timing models one bit per two Timer A underflows; sub-cycle SP/CNT
  analog edge timing is not modeled.
- The new FLAG/SP/PC lines expose machine-side seams (`cia_set_flag_line`,
  `cia_set_sp_line`, `cia_pc_line`) but are not yet wired to concrete C64
  peripherals (cassette FLAG, RS-232, user-port handshake). Tape (`.TAP`) and
  RS-232 work will drive them.

## Tests / smoke checks

- Confirm normal CIA ICR reads clear reported flags.
- Confirm debugger-safe CIA peeks do not clear ICR or TOD state, do not advance
  serial shift state, and do not pulse PC.
- Confirm CIA #1 IRQ pending can remain pending without CPU IRQ entry while the CPU interrupt-disable flag is set.
- Confirm RESTORE still behaves independently from CIA #2 NMI.
- Confirm a FLAG high->low edge sets ICR bit 4, respects the mask, and can raise
  CIA #2 NMI; a held-low FLAG does not re-raise.
- Confirm serial output shifts eight bits MSB-first on SP with Timer A timing and
  sets ICR bit 3; serial input shifts eight CNT-clocked bits into SDR and can
  raise CIA #2 NMI.
- Confirm the PC line pulses low for one cycle after a CPU-visible PRB access.
- Confirm `cia_interrupt_line` asserts one cycle behind `cia_irq_pending` and
  deasserts one cycle after an ICR read clears the flag.
- Confirm CPU IRQ/NMI entry waits for the delayed pin (not same-cycle as
  underflow / FLAG edge latch).

## Files likely involved

- `src/machine/cia*`
- `src/machine/c64*`
- `src/runtime/*`
- CIA and interrupt tests under `tests/` (`tests/machine/test_c64_cia.c`)
