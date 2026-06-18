# C64MCIA.md
# CIA Implementation Plan for c64m

## Purpose

This document is an intermediate-level implementation plan for completing MOS 6526 CIA
emulation in c64m. Each phase is sequenced by dependency order and is specific enough
to understand scope and intent, but each phase will require a refinement pass (producing
a coding-agent-ready phase document) before being handed to a coding agent.

This document follows the same planning level as `C64MVICII.md`: it defines scope,
observable behavior, implementation direction, and acceptance criteria, while avoiding
premature file-by-file implementation details.

## Reference

Primary sources:

- MOS/CSG 6526 Complex Interface Adapter data sheet.
- Oxyron CIA register reference.
- Marko Makela / Michael J. Klein, "A Software Model of the CIA6526".

Project sources:

- `AGENTS.md` for architecture, ownership, phase workflow, and development philosophy.
- `STATUS.md` for the current c64m implementation state.
- `C64MVICII.md` for phase-document structure and planning level.

---

## Current State

This section reflects the current-code handoff in `C64MCIA_Current.md`, not the desired
end state. It is intended to keep this roadmap aligned with what the code already claims
to model before later phase documents are written.

### Architecture already present

- CIA #1 and CIA #2 share one generic `cia` struct and one `cia.c` implementation.
- Per-chip C64 roles are handled outside the generic CIA layer by machine and bus wiring.
- Both CIAs are stepped once per system clock cycle inside `c64_advance_one_cycle`, in
  lockstep with the VIC.
- There is no sub-cycle, Phi2 phase, or bus-event edge model yet.

### Register and bus behavior already present

- `$DC00-$DCFF` maps to CIA #1.
- `$DD00-$DDFF` maps to CIA #2.
- Both CIA ranges mirror with `addr & 0x0F`.
- All 16 registers exist in a raw `uint8_t registers[16]` backing array.
- Registers with special behavior are handled by `cia_read_register` and
  `cia_write_register`; other registers fall through to raw array read/write.
- Debug reads currently bypass normal CIA read side effects by reading the raw register
  array directly. This avoids ICR clear-on-read in memory views, but it also means
  debugger views may not show live timer counters unless a richer safe-peek path is added.

### Timers already present

- Timer A and Timer B each have a latch, live counter, and underflow flag.
- `$04/$05` read Timer A's live counter low/high bytes.
- `$06/$07` read Timer B's live counter low/high bytes.
- `$04/$05` and `$06/$07` writes update the corresponding timer latch.
- If a timer is stopped, timer-register writes also load the live counter immediately.
- CRA/CRB bit 0 starts or stops the corresponding timer.
- CRA/CRB bit 3 implements one-shot vs continuous behavior.
- CRA/CRB bit 4 is consumed as a force-load strobe and reloads the counter from the latch.
- The per-cycle timer step decrements running timers from system cycles, underflows at
  zero, sets the timer interrupt source, reloads from the latch, and stops in one-shot
  mode.
- Current code treats latch value `$0000` as reload `$FFFF`. This must be checked against
  the selected CIA reference behavior before full accuracy is claimed.

### Interrupt behavior already present

- Interrupt flags and interrupt masks exist.
- ICR read returns flags plus bit 7 when any enabled source is pending.
- ICR read clears all interrupt flags and both timer underflow states.
- ICR write uses bit 7 as set-mask vs clear-mask and bits 0-4 as the affected source set.
- Timer A and Timer B interrupt sources are modeled.
- TOD alarm, serial complete, and FLAG interrupt sources are not modeled.
- CIA #1 IRQ is ORed with VIC IRQ in the CPU IRQ poll path.
- CIA #2 interrupt pending state is exposed diagnostically, but it is not wired to the CPU
  NMI callback. CPU NMI currently comes from the RESTORE path only.

### Port behavior already present

- PRA and PRB use a basic `(data & dir) | ~dir` read formula.
- DDRA and DDRB are raw stores.
- CIA #1 has keyboard matrix scanning on PRB reads when a keyboard is attached.
- CIA #2 port A bits 1-0 select the VIC bank through the existing inverted C64 wiring.
- Joystick ports are not modeled.
- IEC serial bus lines are not modeled.
- TOD registers `$08-$0B` are raw passthrough only.
- SDR `$0C` is raw passthrough only.

### Observed discrepancy to resolve

The current-code handoff says Timer B already has live-counter reads and per-cycle
countdown. The user's manual observation is that `$DC06` always returns `$FF` and does
not appear to count down. Treat this as a reconciliation task, not as proof that the
planned Timer B architecture is absent. Likely causes to check in the first refinement
pass include:

- the test program may be reading through a debug/snapshot path rather than the CPU-visible
  `cia_read_register` path;
- Timer B may not actually be started by CRB bit 0 in the tested sequence;
- the counter may be loaded with `$FFFF` and read too coarsely to observe low-byte motion;
- the timer step may not be reached for the tested machine path;
- writes to `$DC06/$DC07` may not be routed to the live Timer B instance being stepped;
- there may be a regression between the handoff description and the code under test.

### Not yet implemented, or not yet complete enough to claim full CIA accuracy

- Sub-cycle or phase-accurate CIA timing.
- Full timer mode matrix, including CNT input and Timer B cascade from Timer A underflows.
- PB6/PB7 timer output pulse/toggle behavior.
- CIA #2 interrupt output wired to CPU NMI.
- TOD clock, alarm, latching, BCD, AM/PM, and TOD interrupt behavior.
- Serial data register shift behavior on CNT/SP.
- FLAG interrupt input, PC pulse, and port handshaking.
- Joystick port integration with the CIA #1 keyboard matrix lines.
- IEC serial bus integration through CIA #2 port lines.
- Complete port electrical behavior beyond the current simple DDR/latch formula.
- Full 6526/6526A/8521 variant policy.
- Open-bus and unused-bit read behavior for CIA registers.

---

## Dependency Order Overview

```
Phase A  Register map, mirroring, safe reads, and current-state reconciliation
Phase B  Timer A/B core countdown and reload hardening
Phase C  Timer control modes, PB output, and cascade sources
Phase D  Interrupt control register and IRQ/NMI line behavior
Phase E  CIA #1 ports: keyboard, joystick, and RESTORE integration
Phase F  CIA #2 ports: VIC bank and IEC serial bus integration
Phase G  Time-of-day clock and alarm
Phase H  Serial data register, CNT/SP, and shift timing
Phase I  Handshake lines, FLAG, PC pulse, and edge-sensitive behavior
Phase J  Cycle-level accuracy, read/write edge cases, and hardware variants
Phase K  Validation suite, debugger visibility, and compatibility corpus
```

Phases A through D are the correctness foundation. They should be completed before
expanding into TOD, serial shift, or obscure handshaking behavior.

Phases E and F connect CIA accuracy to actual C64 behavior. They should follow the core
CIA register/timer/interrupt phases because both ports depend on correct DDR, PRA/PRB,
and interrupt behavior.

Phases G through I add remaining 6526 feature completeness. They are lower priority for
basic boot and keyboard use, but important for full hardware compatibility.

Phases J and K are accuracy-polish and validation phases. They should not be used to hide
missing basic behavior from earlier phases.

---

## CIA Register Map

Each CIA has 16 registers mirrored through its 256-byte I/O page.

CIA #1 base: `$DC00`
CIA #2 base: `$DD00`

```
Offset  Name    Function
$00     PRA     Port A data register
$01     PRB     Port B data register
$02     DDRA    Port A data direction register
$03     DDRB    Port B data direction register
$04     TAL     Timer A low byte
$05     TAH     Timer A high byte
$06     TBL     Timer B low byte
$07     TBH     Timer B high byte
$08     TOD10   Time-of-day tenths
$09     TODSEC  Time-of-day seconds
$0A     TODMIN  Time-of-day minutes
$0B     TODHR   Time-of-day hours
$0C     SDR     Serial data register
$0D     ICR     Interrupt control register
$0E     CRA     Control register A
$0F     CRB     Control register B
```

The same offset behavior applies to both `$DCxx` and `$DDxx`, except that their external
pin wiring differs.

---

## Phase A - Register Map, Mirroring, Safe Reads, and Current-State Reconciliation

### Goal

Lock down the current CIA register contract and reconcile any mismatch between the code
handoff, CPU-visible behavior, and debugger-visible behavior. This phase is now primarily
an audit and correction phase, because basic register storage, mirroring, timer live
counter fields, and debug side-effect avoidance already exist.

### Properties

**Address mirroring:**

- CIA #1 responds to `$DC00-$DCFF`.
- CIA #2 responds to `$DD00-$DDFF`.
- Effective CIA register offset is `addr & 0x0F`.
- All mirrors behave identically for side effects unless a debugger-safe peek path is
  explicitly used.

**Side-effecting vs safe reads:**

- CPU-visible reads may trigger CIA read side effects.
- Debugger-safe peeks must not trigger side effects such as ICR clear-on-read or TOD
  latch transitions.
- Memory snapshots must use debugger-safe CIA peeks, not normal bus reads.

**Timer register baseline:**

- CPU-visible reads of `$04/$05` must return Timer A's current counter low/high bytes.
- CPU-visible reads of `$06/$07` must return Timer B's current counter low/high bytes.
- CPU-visible writes of `$04/$05` must update Timer A's latch low/high bytes.
- CPU-visible writes of `$06/$07` must update Timer B's latch low/high bytes.
- Debugger-safe reads may intentionally avoid side effects, but they must be documented
  as raw-register peeks or upgraded to expose copied live counter state.
- The observed `$DC06 == $FF` behavior must be traced to either test setup, debug path,
  bus routing, timer stepping, or an implementation bug.

**Initial values and reset:**

- Reset should initialize CIA registers, latches, counters, interrupt masks, and line
  outputs consistently.
- Any intentional deviation from real hardware power-on randomness must be documented
  as deterministic-emulator behavior.

### Acceptance Criteria

- `$DC00-$DC0F` and `$DD00-$DD0F` mirror correctly through the whole 256-byte CIA page.
- CPU-visible `$DC06/$DC07` reads are proven to come from the Timer B live counter.
- CPU-visible `$DC06/$DC07` writes are proven to update Timer B's latch and, when stopped,
  the currently implemented immediate counter load behavior.
- A minimal CPU-level diagnostic can start Timer B and observe a changing live counter, or
  the exact defect preventing that observation is identified and fixed.
- Debugger memory views can inspect CIA registers without clearing ICR flags or changing
  TOD latch state, with any raw-vs-live limitations documented.
- Existing boot, keyboard, VIC-bank, and debugger memory tests continue to pass.

---

## Phase B - Timer A/B Core Countdown and Reload Behavior

### Goal

Harden the already-present 16-bit down-counter behavior for Timer A and Timer B until it
is trustworthy, tested, and documented. This is no longer a from-scratch phase; current
code already has latches, counters, start/stop, force-load, one-shot, continuous reload,
and Timer A/B interrupt flagging.

### Properties

**Counter and latch separation:**

- Each timer owns a 16-bit latch and a separate 16-bit live counter.
- Writes to timer low/high registers update the latch.
- Reads from timer low/high registers return the live counter.
- Loading the counter from the latch is controlled by timer start, force-load, underflow,
  and reset behavior, not by every latch write.

**Countdown source for this phase:**

- Timer A currently counts one system cycle per CIA step when started.
- Timer B currently counts one system cycle per CIA step when started.
- This phase should define whether that is acceptable as the project's current Phi2 model
  or whether an explicit Phi2-cycle abstraction is needed before cycle-level work.
- CNT and Timer A underflow cascade modes are deferred to Phase C.

**Underflow behavior:**

- A timer underflows when it counts down past `$0000`.
- Underflow sets the corresponding ICR flag.
- In continuous mode, the timer reloads from its latch and keeps running.
- In one-shot mode, the timer reloads from its latch and stops.
- Latch value `$0000` is a valid value and must follow documented 6526 behavior, not a
  special emulator no-op.

**Start/stop behavior:**

- CRA bit 0 controls Timer A start/stop.
- CRB bit 0 controls Timer B start/stop.
- Stopped timers do not decrement.
- Running timers decrement at the correct machine-cycle cadence for their selected input.

**Force-load behavior:**

- CRA bit 4 force-loads Timer A from its latch.
- CRB bit 4 force-loads Timer B from its latch.
- The force-load bit is a strobe-style control bit and must not remain set as a normal
  stored state if hardware clears it.

### Acceptance Criteria

- A program that writes `$DC06/$DC07`, starts Timer B in Phi2 mode, waits, and reads
  `$DC06/$DC07` observes a decreasing counter value.
- Timer A and Timer B both underflow and reload correctly in continuous mode.
- Timer A and Timer B both underflow, reload, and stop correctly in one-shot mode.
- Force-load copies latch to counter for both timers.
- Timer reads remain stable enough for ordinary low/high read sequences used by C64 code.
- Existing zero-latch timer reload and one-shot stop diagnostics continue to pass.

---

## Phase C - Timer Control Modes, PB Output, and Cascade Sources

### Goal

Complete the timer mode matrix controlled by CRA and CRB, including CNT input, Timer B
cascade modes, PB6/PB7 output behavior, pulse/toggle output, and timer-output interaction
with port B.

### Properties

**CRA bits:**

- Bit 0: Timer A start.
- Bit 1: Timer A PB6 output enable.
- Bit 2: Timer A PB6 output mode: pulse or toggle.
- Bit 3: Timer A run mode: continuous or one-shot.
- Bit 4: Timer A force-load latch into counter.
- Bit 5: Timer A input mode: Phi2 or CNT.
- Bit 6: Serial port direction/control as applicable.
- Bit 7: TOD clock source selection policy for 50/60 Hz.

**CRB bits:**

- Bit 0: Timer B start.
- Bit 1: Timer B PB7 output enable.
- Bit 2: Timer B PB7 output mode: pulse or toggle.
- Bit 3: Timer B run mode: continuous or one-shot.
- Bit 4: Timer B force-load latch into counter.
- Bits 5-6: Timer B input source selection.
- Bit 7: TOD alarm write select.

**Timer B input sources:**

- Phi2 cycle countdown.
- CNT input edge countdown.
- Timer A underflow countdown.
- Timer A underflow while CNT is active, if supported by the selected 6526 behavior.

**PB6/PB7 output:**

- Timer A can drive PB6 when enabled.
- Timer B can drive PB7 when enabled.
- Timer output may override normal DDRB behavior as documented by the 6526.
- Pulse mode produces a one-cycle pulse on underflow.
- Toggle mode toggles output on each underflow and initializes/reset states consistently.

### Acceptance Criteria

- Timer A decrements from Phi2 and CNT sources according to CRA.
- Timer B decrements from Phi2, CNT, Timer A underflow, and combined modes according to CRB.
- PB6/PB7 reflect timer output when enabled and ordinary port behavior when disabled.
- Pulse mode produces a one-cycle observable output.
- Toggle mode toggles on repeated underflows.
- Timer cascade tests can use Timer A underflow to clock Timer B.

---

## Phase D - Interrupt Control Register and IRQ/NMI Line Behavior

### Goal

Implement full CIA interrupt flag, mask, read-clear, write-mask, and output-line behavior.
CIA #1 must assert CPU IRQ. CIA #2 must assert CPU NMI.

### Properties

**ICR read behavior:**

- Reading `$0D` returns interrupt flags.
- Reading `$0D` clears the currently reported interrupt flags.
- Bit 7 of the read value reports whether at least one enabled interrupt source is
  pending.
- Debugger-safe peeks must not clear flags.

**ICR write behavior:**

- Writing `$0D` modifies the interrupt mask, not the interrupt flags.
- Bits 0-4 select which mask bits are affected.
- Bit 7 selects set-mask vs clear-mask behavior.
- Bits not selected by bits 0-4 are unchanged.

**Interrupt sources:**

- Bit 0: Timer A underflow.
- Bit 1: Timer B underflow.
- Bit 2: alarm/TOD match.
- Bit 3: serial port complete.
- Bit 4: FLAG line.

**Output lines:**

- CIA #1 enabled-pending interrupt asserts the CPU IRQ input.
- CIA #2 enabled-pending interrupt asserts the CPU NMI input.
- IRQ/NMI output must update when flags are set, masks are changed, or flags are cleared.
- CIA #2 NMI is edge-sensitive at the CPU input level; repeated NMI behavior must be
  modeled carefully enough for RESTORE and IEC-adjacent code.

### Acceptance Criteria

- Masked timer underflows set ICR source flags but do not assert IRQ/NMI.
- Enabled timer underflows set source flags and assert IRQ/NMI.
- Reading ICR clears the reported flags and deasserts output if no enabled flags remain.
- Writing ICR with bit 7 set enables selected sources without changing unselected sources.
- Writing ICR with bit 7 clear disables selected sources without changing unselected sources.
- CIA #1 Timer A can drive a KERNAL-style periodic IRQ path.
- CIA #2 can generate an NMI through its interrupt output path.

---

## Phase E - CIA #1 Ports: Keyboard, Joystick, and RESTORE Integration

### Goal

Complete CIA #1 port behavior as it is wired in the C64: keyboard matrix scanning,
joystick ports, light-pen/fire interactions where applicable, and RESTORE/NMI-related
input routing.

### Properties

**Port data and DDR behavior:**

- PRA/PRB reads return a mix of output latch bits, DDR-selected output bits, and external
  input line states.
- DDRA/DDRB select input vs output direction per bit.
- Output latch state is preserved even when a bit is configured as input.
- Input bits should read high when un-driven unless an external device pulls them low.

**Keyboard matrix:**

- CIA #1 port A and port B form the C64 keyboard matrix scan path.
- The emulated keyboard matrix must affect port reads through the same line-pull model
  used by real scanning, not through special-case KERNAL shortcuts.
- Multiple simultaneous keys, shifted keys, Commodore key, Control, cursor keys, RUN/STOP,
  and restore-adjacent behavior must be representable.

**Joystick ports:**

- Joystick port 1 and port 2 directional/fire inputs share CIA #1 port lines with the
  keyboard matrix.
- Joystick line pulls must combine correctly with keyboard line pulls.
- Active-low joystick behavior must be visible in the correct PRA/PRB bits.

**RESTORE:**

- RESTORE is not an ordinary keyboard matrix key.
- RESTORE should feed the correct NMI path while preserving CIA #1 keyboard behavior.
- Any existing host Delete-to-RESTORE mapping should remain a frontend/runtime command
  path that results in correct machine-level NMI behavior.

### Acceptance Criteria

- Existing BASIC typing, cursor, Shift, Commodore, Control, RUN/STOP, and paste tests
  continue to pass.
- A direct keyboard matrix diagnostic sees active-low key closures through CIA #1 ports.
- Joystick port 1 and port 2 diagnostics read correct direction/fire bits.
- Keyboard and joystick inputs combine electrically: either source can pull a shared line
  low.
- RESTORE enters the CPU NMI path without corrupting ordinary keyboard matrix state.

---

## Phase F - CIA #2 Ports: VIC Bank and IEC Serial Bus Integration

### Goal

Complete CIA #2 port behavior as it is wired in the C64: VIC bank selection, IEC serial
bus control/sense lines, RS-232-adjacent lines where applicable, and NMI-capable CIA #2
interrupt behavior.

### Properties

**VIC bank selection:**

- CIA #2 port A bits 1-0 select the VIC-II memory bank using the C64's inverted wiring.
- The VIC bank should be derived from the actual CIA #2 port output level, not from raw
  writes alone.
- DDRA and external line state must be considered so that bank selection follows the
  effective output state.

**IEC serial bus:**

- CIA #2 port A lines drive and read IEC serial bus signals as wired in the C64.
- ATN, CLK, DATA, and sense lines must use open-collector/open-drain style behavior,
  where multiple devices may pull a line low and released lines read high.
- Disk-drive emulation is outside this CIA plan unless explicitly authorized, but the CIA
  side of the bus must be accurate enough for later drive integration.

**NMI behavior:**

- CIA #2 interrupt output feeds the CPU NMI path.
- Timer, TOD, serial, and FLAG sources on CIA #2 should all route through the same ICR
  logic defined in Phase D.

### Acceptance Criteria

- Existing VIC bank tests continue to pass.
- VIC bank selection changes according to effective CIA #2 port A output bits 1-0.
- IEC lines can be read and driven through CIA #2 port registers with active-low behavior.
- Releasing an IEC line lets it return high unless another attached source pulls it low.
- CIA #2 enabled interrupt sources can produce CPU NMI entries.

---

## Phase G - Time-of-Day Clock and Alarm

### Goal

Implement the CIA time-of-day clock, TOD register latching behavior, alarm registers,
BCD representation, AM/PM hour behavior, and TOD interrupt source.

### Properties

**Registers:**

- `$08`: tenths of seconds.
- `$09`: seconds in BCD.
- `$0A`: minutes in BCD.
- `$0B`: hours in BCD with AM/PM representation according to 6526 behavior.

**Clock source:**

- TOD advances from a 50 Hz or 60 Hz source depending on the CIA control setting and
  machine configuration policy.
- The default behavior must be documented for PAL and NTSC.

**Read latching:**

- TOD reads use the 6526 latch sequence so multi-byte reads see a coherent time value.
- Debugger-safe peeks must not disturb TOD latch state.

**Write behavior:**

- TOD writes set the clock or alarm depending on the CRB alarm-select bit.
- Writing TOD should stop/resume or latch intermediate state according to documented
  6526 behavior.

**Alarm:**

- Alarm registers compare against TOD.
- A match sets the ICR TOD/alarm flag.
- If the TOD interrupt source is enabled, the CIA output line asserts IRQ/NMI.

### Acceptance Criteria

- TOD advances at the expected PAL/NTSC cadence.
- Multi-byte TOD reads produce coherent values across second/minute/hour boundaries.
- TOD writes set the clock when alarm-select is clear.
- TOD writes set alarm registers when alarm-select is set.
- Alarm match sets the ICR TOD flag and asserts IRQ/NMI when enabled.
- TOD debugger peeks do not alter CPU-visible TOD latch state.

---

## Phase H - Serial Data Register, CNT/SP, and Shift Timing

### Goal

Implement the CIA serial shift register and CNT/SP line behavior sufficiently for C64
software that uses CIA serial I/O, fast loaders, diagnostics, or peripheral protocols.

### Properties

**SDR behavior:**

- `$0C` is the serial data register.
- Reads and writes interact with the shift register state, not merely raw storage.
- Serial-complete events set the ICR serial flag.

**CNT and SP lines:**

- CNT provides shift timing in the relevant serial modes.
- SP carries serial data in/out.
- CNT edges must be synchronized to CIA timing where required.

**Timer interaction:**

- Timer A may provide serial output timing depending on control settings.
- CRA serial mode bits must be honored.

**Scope limit:**

- This phase implements CIA-side serial behavior. Full disk-drive or peripheral emulation
  remains outside scope unless a separate phase document authorizes it.

### Acceptance Criteria

- Writing SDR and enabling serial output shifts eight bits on SP with CNT timing.
- Serial input can shift eight bits into SDR and set the serial interrupt flag.
- ICR serial source follows the same mask, flag, read-clear, and IRQ/NMI behavior as other
  sources.
- Timer-driven serial transfer timing is deterministic and testable.

---

## Phase I - Handshake Lines, FLAG, PC Pulse, and Edge-Sensitive Behavior

### Goal

Complete the remaining CIA external-line behavior: FLAG interrupt input, PC pulse output,
port handshaking, and edge-sensitive interactions used by peripherals and diagnostics.

### Properties

**FLAG:**

- FLAG is an external input line.
- The selected edge/event sets the ICR FLAG flag.
- FLAG must route through the same ICR mask and output-line behavior as timers, TOD, and
  serial.

**PC pulse and handshaking:**

- Port handshaking behavior must update PC and related lines according to 6526 rules.
- Handshake effects caused by reads/writes must occur only on CPU-visible accesses, not
  debugger-safe peeks.

**Port interaction:**

- Handshake line behavior must coexist with ordinary PRA/PRB and DDRA/DDRB behavior.
- Output line transitions should be timestamped consistently with machine bus events.

### Acceptance Criteria

- FLAG edge tests set the ICR FLAG bit and assert IRQ/NMI when enabled.
- Reading or writing the relevant port registers produces the documented handshake pulses.
- Debugger-safe peeks do not generate handshake pulses or clear pending handshakes.
- Existing keyboard, joystick, VIC bank, IEC, timer, and interrupt tests continue to pass.

---

## Phase J - Cycle-Level Accuracy, Read/Write Edge Cases, and Hardware Variants

### Goal

Tighten CIA behavior from functional correctness to hardware-level compatibility. This
phase should be attempted only after the visible register, timer, interrupt, port, TOD,
serial, and handshake behaviors are implemented.

### Properties

**Cycle-level scheduling:**

- CIA timers and line transitions must advance from the machine's timed bus-event model.
- CPU-visible reads and writes must occur at the correct event cycle within an instruction.
- Timer underflow, reload, ICR flag setting, PB pulse/toggle, and IRQ/NMI output changes
  must happen at deterministic cycle positions.

**Read/write races:**

- Reads around underflow boundaries must match documented or selected-reference behavior.
- Writes to timer latches, force-load bits, start bits, and ICR masks near underflow must
  be deterministic and tested.
- ICR read-clear races with newly arriving interrupt sources must be handled deliberately.

**Unused bits and open bus:**

- CIA unused bits must read as the documented value for the selected chip variant.
- Any open-bus or last-data-bus behavior should be modeled only if authorized by a phase
  document and backed by tests.

**Chip variant policy:**

- Choose and document the default CIA model: 6526, 6526A, 8521, or a project-specific
  compatibility model.
- Differences must be explicit and testable.
- Variant selection should not be added speculatively before a concrete need exists.

### Acceptance Criteria

- Timer-edge tests around `$0001->$0000->underflow->reload` pass with cycle-level expected
  results.
- ICR read-clear race tests are deterministic.
- Force-load/start/stop writes during countdown produce expected results.
- PB6/PB7 pulse/toggle timing is correct at the selected cycle granularity.
- The selected default CIA variant is documented in `STATUS.md`.

---

## Phase K - Validation Suite, Debugger Visibility, and Compatibility Corpus

### Goal

Make CIA accuracy maintainable. Add tests, diagnostics, debugger surfaces, and external
compatibility programs that prevent regressions and support future phase documents.

### Properties

**Regression tests:**

- Unit tests should cover each CIA register group in isolation.
- Integration tests should cover CPU IRQ/NMI entry paths, keyboard/joystick reads, VIC
  bank switching, and IEC line behavior.
- Timer tests should cover both direct register reads and behavior observed through ICR
  and CPU interrupts.

**Debugger visibility:**

- Debugger views may expose copied CIA state: latches, counters, control registers, ICR
  flags/masks, TOD, and port effective input/output levels.
- Debugger views must use copied snapshots and safe peeks only.
- No live machine pointers may cross from runtime to frontend.

**Compatibility corpus:**

- Add small local assembly diagnostics for register behavior.
- Add known public CIA test programs where licensing permits.
- Track pass/fail status in `STATUS.md`.

**Status discipline:**

- Every completed CIA phase must update `STATUS.md`.
- Deferred behaviors must remain listed under Not Implemented until implemented and tested.

### Acceptance Criteria

- CIA register, timer, interrupt, port, TOD, serial, and handshake tests exist.
- Existing boot, display, keyboard, debugger, breakpoint, and VIC-II tests continue to pass.
- Debugger CIA state, if added, follows the snapshot rule.
- `STATUS.md` accurately names implemented CIA phases and remaining gaps.

---

## Suggested Phase Sequence for c64m

Given the project's philosophy of vertical slices and the current state, the suggested
order is:

```
1. Phase A  - Register map, mirroring, safe reads, and current-state reconciliation
2. Phase B  - Timer A/B core countdown and reload hardening
3. Phase D  - Interrupt control register and IRQ/NMI line behavior
4. Phase C  - Timer control modes, PB output, and cascade sources
5. Phase E  - CIA #1 ports: keyboard, joystick, and RESTORE integration
6. Phase F  - CIA #2 ports: VIC bank and IEC serial bus integration
7. Phase G  - Time-of-day clock and alarm
8. Phase H  - Serial data register, CNT/SP, and shift timing
9. Phase I  - Handshake lines, FLAG, PC pulse, and edge-sensitive behavior
10. Phase J - Cycle-level accuracy, read/write edge cases, and hardware variants
11. Phase K - Validation suite, debugger visibility, and compatibility corpus
```

Phase A should be prioritized immediately because the handoff says Timer B live reads and
countdown already exist, while manual observation says `$DC06` remains `$FF`. The first
work item is therefore to reconcile the observed behavior with the actual CPU-visible and
debug-visible code paths. Phase B then hardens the already-present timer core rather than
reimplementing it blindly. Phase D remains early because timer underflows are much more
useful once ICR and IRQ/NMI behavior are trustworthy, and because CIA #2 NMI is explicitly
not wired yet.

Phases E and F may be split into smaller documents if keyboard/joystick and IEC/VIC bank
work require different maintainers or test harnesses.

Phases G, H, and I are lower priority for basic C64 boot and keyboard use, but they are
required before claiming full CIA accuracy.

---

## Notes for Phase Document Authors

When refining any phase above into a coding-agent-ready document, include:

- Exact register addresses, bit masks, and read/write behavior for all registers touched.
- Which existing c64m structs and files are modified.
- Which new structs or fields are added.
- Which behavior is CPU-visible and which behavior is debugger-safe only.
- Precise acceptance criteria expressed as observable emulator behavior or test conditions.
- Required regression tests and any manual diagnostic programs.
- Any interaction with the runtime thread, copied snapshots, or frontend debugger views.
- Whether the behavior applies to CIA #1, CIA #2, or both.
- Whether the behavior depends on external C64 wiring rather than generic 6526 behavior.
- Any intentionally deferred hardware edge cases.

Implementation should follow the project rule: build vertically, prefer the smallest
demonstrable machine slice, do not implement future phases early, and do not add
speculative abstractions.
