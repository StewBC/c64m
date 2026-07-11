# C64MFULL_CIA.md
# Full CIA Pin and Serial Timing Accuracy Plan for c64m

## Purpose

This document is a multi-phase implementation plan for completing the remaining
MOS 6526 CIA behavior that c64m does not yet model:

```text
- FLAG external interrupt input line;
- serial data register (SDR) with CNT/SP shift behavior;
- PC pulse and port handshake lines;
- pin/race-level interrupt line timing (the CIA interrupt delay and
  ICR read/set races).
```

It is the refined, coding-agent-ready expansion of the work deferred in
[`C64MCIA_NEW.md`](C64MCIA_NEW.md) Work Areas B and C, and of Phases H, I, and J
in the broader roadmap [`C64MCIA.md`](C64MCIA.md). It reuses their scope and
acceptance intent but restates them against the current source so a coding agent
can execute each phase without rereading the entire roadmap.

## Implementation Status (read first)

This work is now **in scope** (the earlier `AGENTS.md` out-of-scope note for CIA
FLAG/serial/handshake was stale and has been corrected). Future work such as
`.TAP` tape support builds on these CIA pins.

```text
Phase 1  FLAG external interrupt input .................. DONE
Phase 2  Serial SDR/CNT/SP shift ....................... DONE
Phase 3  PC pulse / handshake .......................... DONE
Phase 4  Pin/race-level interrupt timing ............... DONE (Option 2 wired)
Phase 5  Validation and documentation close-out ........ DONE
```

Phase 4 first landed as a **conservative refactor** (`cia_interrupt_line`
separate from latched `cia_irq_pending`, unit-tested delay without changing
CPU-visible timing). **Option 2 is now wired:** `c64_cpu_irq_pending` and
`c64_cpu_nmi_pending` sample `cia_interrupt_line`. The VICE/hardware corpus
under `md-files/corpus/cia-timing/` remains the external oracle for further
race-level work (PRG runner, cycle-stamped logs, chip-variant policy).

New public CIA API from this work: `cia_set_flag_line`, `cia_set_sp_line`,
`cia_pc_line`, `cia_interrupt_line`. The FLAG/SP/PC machine-side seams are not
yet wired to concrete peripherals (cassette FLAG, RS-232, user-port handshake);
tape and RS-232 work will consume them.

Each phase followed the standard `AGENTS.md` phase workflow and Definition Of
Done. The sections below are retained as the design record.

## Mandatory Ground Rules

- The source code is the source of truth. Treat this document as an
  implementation guide, not as proof that a file, symbol, or behavior currently
  exists or behaves as described.
- Before editing, inspect the repository and verify the actual file names,
  structs, functions, tests, and current behavior. Prefer names discovered in
  source over names from this document.
- Implement only the phase in hand. Do not pull later-phase behavior forward
  unless it is the smallest necessary support for the current phase's acceptance
  criteria.
- Preserve the architecture rules from `AGENTS.md`: frontend must not depend on
  machine, runtime must not depend on frontend or platform, machine must not
  depend on runtime/frontend/platform, and live machine state stays on the
  runtime thread.
- Preserve the snapshot rule: frontend/debugger views receive copied snapshots
  or debugger-safe peeks only; no live machine pointers cross threads.
- CPU-visible reads/writes may have side effects. Debugger-safe peeks
  (`cia_debug_read_register`) must never advance shift state, pulse handshake
  lines, clear ICR flags, or disturb TOD latch state.
- Build vertically. Prefer the smallest demonstrable machine slice. Do not add
  speculative abstractions or unrequested variant policy.
- Rebuild the binary and run the test suite before declaring any phase done.

## Required Reading

```text
1. AGENTS.md
2. md-files/MASTER.md
3. md-files/STATUS.md
4. md-files/docs/status/README.md
5. md-files/docs/status/CIA.md
6. md-files/C64MCIA.md and md-files/C64MCIA_NEW.md
7. This document
8. The relevant source and tests discovered in the repository
```

## Reference Sources

```text
- MOS/CSG 6526 Complex Interface Adapter data sheet.
- Oxyron CIA register reference.
- Marko Makela / Michael J. Klein, "A Software Model of the CIA6526".
- 6526 vs 6526A interrupt-delay and timer-write race notes.
```

## Current State (verified against source)

Implementation lives in [`cia.c`](../src/machine/cia.c) /
[`cia.h`](../src/machine/cia.h); the C64 wiring lives in
[`c64.c`](../src/machine/c64.c) and [`c64_bus.c`](../src/machine/c64_bus.c).
Existing coverage is in [`test_c64_cia.c`](../tests/machine/test_c64_cia.c).

Present and working (through Phase G):

```text
- Register map, $DCxx/$DDxx mirroring via addr & 0x0F, raw registers[16] backing.
- Timer A/B latch + live counter, start/stop, force-load strobe, one-shot vs
  continuous, PB6/PB7 pulse and toggle output (cia_step_timer,
  cia_timer_update_pb_output).
- Timer B input modes: Phi2, CNT, Timer A underflow, and Timer A+CNT
  (cia_timer_b_should_count); Timer A input modes Phi2/CNT
  (cia_timer_a_should_count).
- ICR flags/mask separation; read-clears-flags; bit 7 = enabled-pending;
  ICR write set/clear mask semantics (cia_read_register / cia_write_register).
- CIA #1 -> CPU IRQ; CIA #2 -> CPU NMI via an edge latch in c64.c
  (c64_cpu_irq_pending, c64_cpu_nmi_pending); RESTORE is a separate one-shot NMI.
- Keyboard matrix, joystick, CIA #2 VIC bank + IEC open-collector lines, TOD +
  alarm with coherent read latch and 50/60 Hz policy.
- cia_step_cycle is called once per system cycle for each CIA (c64.c:148,152).
```

Missing or stubbed (the target of this plan):

```text
- FLAG line: no external input, no ICR bit 4 event generation. The ICR source
  mask (CIA_INTERRUPT_SOURCE_MASK = 0x1f) already reserves bits 3 and 4, and
  cia_set_interrupt_source already accepts them, but nothing raises them.
- SDR ($0C): raw passthrough only; no shift register, no CNT/SP shift, no
  serial-complete (ICR bit 3) event. CRA bit 6 (serial direction) is stored
  but unused.
- CNT: modeled as a per-cycle pulse flag (cia_pulse_cnt / cnt_pulse) consumed by
  the timers, but no external C64 wiring drives it and it is not tied to serial
  shifting. cia_pulse_cnt currently has no caller in the machine.
- PC pulse / port handshake lines: not modeled.
- Interrupt line timing: flags assert IRQ/NMI in the same cycle they are set.
  The real 6526 one-cycle interrupt delay and the ICR read/set race are not
  modeled. IRQ/NMI is derived by polling cia_irq_pending, not from a sampled
  interrupt line edge inside the CIA.
```

## C64 External Pin Wiring (needed for correct FLAG/PC/SP/CNT)

Generic 6526 behavior must be kept in `cia.c`; C64-specific pin wiring belongs in
the machine/bus layer (`c64.c` / `c64_bus.c`), following the existing pattern
where keyboard/joystick/IEC/VIC-bank wiring lives outside the generic CIA.

```text
CIA #1 ($DC00):
  FLAG  <- Cassette read line, and user-port /FLAG2 pin.
  PC    -> user-port PC2 handshake pin (pulses on PRB access).
  SP/CNT-> user-port serial pins (rarely used by ordinary software).

CIA #2 ($DD00):
  FLAG  <- user-port /FLAG pin (RS-232 receive edge in KERNAL RS-232).
  PC    -> user-port handshake pin.
  SP/CNT-> user-port serial pins.
```

Ordinary software rarely exercises SP/CNT or PC. FLAG is used by tape and RS-232.
Because no in-scope milestone target needs these, phases here must provide clean
machine-side seams (injectable edges/levels) and deterministic behavior rather
than full peripheral emulation.

## Dependency Order Overview

```text
Phase 1  FLAG external interrupt input (ICR bit 4)
Phase 2  Serial data register: SDR, CNT/SP shift, serial-complete (ICR bit 3)
Phase 3  PC pulse and port handshake lines
Phase 4  Pin/race-level interrupt line timing (CIA interrupt delay + ICR races)
Phase 5  Validation suite, debugger visibility, and documentation close-out
```

Rationale for the order:

```text
- Phases 1-3 add the three missing observable features against the current
  same-cycle interrupt model. Each is a small, self-contained vertical slice
  with its own tests and low regression risk.
- Phase 4 performs the single deepest change: re-timing the interrupt line for
  all sources (timers, TOD, serial, FLAG) at once, so the risky global re-timing
  is done once rather than three times.
- Phase 5 locks in regressions, debugger visibility, and the documentation and
  DEFERRED.md close-out.
```

---

## Phase 1 - FLAG External Interrupt Input

### Goal

Add the FLAG line as a negative-edge-triggered interrupt source that sets ICR
bit 4 and routes through the existing mask/flag/read-clear/IRQ-NMI machinery.

### C64 wiring

CIA #1 FLAG is driven by the cassette read line and user-port /FLAG2; CIA #2 FLAG
is driven by the user-port /FLAG pin. Provide a generic CIA entry point and drive
it from the machine layer; do not hardwire cassette or RS-232 semantics into
`cia.c`.

### Registers and bits

```text
ICR bit 4 (0x10)  FLAG interrupt source flag/mask (already reserved).
FLAG              External input line, active on high->low transition.
```

### Implementation direction

```text
- Add a stored FLAG line level to struct cia and a public setter, e.g.
  cia_set_flag_line(cia *c, bool level), that detects a high->low edge and calls
  cia_set_interrupt_source(c, CIA_INTERRUPT_FLAG) with CIA_INTERRUPT_FLAG = 0x10.
- Edge detection is level-change based: only a 1->0 transition raises the flag;
  holding FLAG low does not re-raise.
- Add the machine-side seam (c64.c) to drive CIA #1 and CIA #2 FLAG. For this
  phase a test-injectable edge is sufficient; real cassette/RS-232 sources may be
  connected by their own later work.
- Debugger-safe peeks must not change FLAG line state or raise the flag.
```

### Acceptance criteria

```text
- A high->low FLAG edge sets ICR bit 4.
- Enabled FLAG source asserts CPU IRQ on CIA #1 and CPU NMI on CIA #2.
- Masked FLAG source sets the ICR flag but does not assert IRQ/NMI.
- Reading ICR clears the FLAG flag; a held-low FLAG does not re-raise without a
  new edge.
- Existing timer, TOD, keyboard, joystick, VIC-bank, and IEC tests still pass.
```

---

## Phase 2 - Serial Data Register, CNT/SP, and Shift Timing

### Goal

Implement the SDR shift register and CNT/SP behavior so that CIA serial I/O is
functional and deterministic, with serial-complete raising ICR bit 3.

### Registers and bits

```text
$0C SDR      Serial data register (shift buffer, not raw storage).
CRA bit 6    Serial port direction: 0 = input, 1 = output.
ICR bit 3 (0x08)  Serial-complete interrupt source (already reserved).
CNT          Serial shift clock line (output in output mode, input in input mode).
SP           Serial data line (output in output mode, input in input mode).
```

### Implementation direction

```text
- Add explicit shift state to struct cia: active flag, bit counter, shift
  register byte, direction, and current SP/CNT output levels.
- Output mode (CRA bit 6 = 1): a write to $0C loads the shift buffer and starts a
  transfer. Bits shift out on SP clocked by CNT; CNT is derived from Timer A
  underflows (two Timer A underflows per bit, matching 6526 output timing). After
  8 bits, set ICR bit 3 (serial complete) via cia_set_interrupt_source; if
  another byte was written meanwhile, chain it.
- Input mode (CRA bit 6 = 0): external CNT edges clock bits from SP into the
  shift register; after 8 bits, copy to the SDR read value and set ICR bit 3.
- Reuse the existing Timer A underflow event in cia_step_timer and the existing
  cnt_pulse mechanism rather than adding a separate scheduler.
- Provide machine-side seams for external SP/CNT (test-injectable) so serial can
  be exercised without full peripheral emulation.
- Debugger-safe peeks of $0C must not advance shift state or clear ICR bit 3.
```

### Out of scope

```text
- Full disk-drive or IEC protocol state machines.
- RS-232 byte framing beyond the CIA-side shift behavior.
- Pin-perfect SP/CNT analog timing beyond deterministic project cadence.
```

### Acceptance criteria

```text
- Writing SDR in output mode shifts exactly eight bits on SP with CNT timing,
  MSB-first per the selected reference, and sets ICR bit 3 on completion.
- Injecting eight CNT edges in input mode shifts eight SP bits into SDR and sets
  ICR bit 3.
- Enabled serial source asserts IRQ on CIA #1 and NMI on CIA #2; masked serial
  source only sets the flag.
- Timer-A-driven output timing is deterministic and testable.
- Debugger-safe reads do not alter serial state. Existing tests still pass.
```

---

## Phase 3 - PC Pulse and Port Handshake Lines

### Goal

Model the PC output handshake pulse so peripherals and diagnostics that rely on
CIA port handshaking behave correctly.

### Behavior

```text
- PC goes low for one cycle following a CPU-visible read or write of PRB ($01).
- PC returns high on the next cycle.
- Only CPU-visible accesses (cia_read_register / cia_write_register) pulse PC;
  debugger-safe peeks must not.
```

### Implementation direction

```text
- Add a PC-pulse state to struct cia (e.g. a one-cycle countdown or a pending
  flag consumed in cia_step_cycle) and a query/output level.
- Trigger the pulse from PRB read and PRB write in the CPU-visible paths only.
- Expose the PC line level to the machine layer via a getter so c64.c can route
  it to the user-port handshake pin if/when a peripheral consumes it. For this
  phase a queryable, test-observable PC level is sufficient.
- Keep handshake behavior independent of ordinary PRA/PRB/DDR behavior.
```

### Acceptance criteria

```text
- A CPU-visible PRB read or write drives PC low for exactly one cycle, then high.
- Debugger-safe PRB peeks do not pulse PC.
- Ordinary PRA/PRB/DDRA/DDRB behavior is unchanged.
- Existing keyboard, joystick, VIC-bank, IEC, timer, and interrupt tests pass.
```

---

## Phase 4 - Pin/Race-Level Interrupt Line Timing

### Goal

Move the CIA interrupt output from same-cycle assertion to hardware-accurate
line timing: the 6526 one-cycle interrupt delay and the ICR read/set race, for
all sources (timers, TOD, serial, FLAG) uniformly.

### Behavior to model

```text
- Interrupt delay: when a source sets its ICR flag, the interrupt output line
  goes active one cycle later, not in the same cycle.
- ICR read/set race: reading ICR clears flags; if a new flag is set on the same
  cycle as an ICR read, model the documented 6526 outcome (the classic
  "read ICR loses/keeps interrupt" behavior for the selected variant).
- Timer write/reload/force-load and start-bit races near underflow must be
  deterministic and match the selected reference cadence.
- IRQ/NMI should be presented as a sampled interrupt line with defined timing,
  rather than a bare cia_irq_pending poll, while preserving the existing CIA #2
  NMI edge latch and separate RESTORE NMI path in c64.c.
```

### Implementation direction

```text
- Introduce an internal interrupt-line state advanced inside cia_step_cycle: a
  pending-to-asserted delay stage so newly set flags reach the output one cycle
  later.
- Re-express c64_cpu_irq_pending / c64_cpu_nmi_pending in terms of the delayed
  CIA interrupt line without regressing VIC IRQ ORing or the CIA #2 NMI edge
  latch.
- Audit every existing timer/TOD/serial/FLAG test for the one-cycle shift and
  update expectations deliberately, documenting each intended change.
- Choose and document the default variant (6526 vs 6526A) in STATUS.md; expose
  variant differences only where a test proves a concrete need. Do not add
  speculative variant switches.
```

### Acceptance criteria

```text
- Timer-edge tests around 0x0001 -> 0x0000 -> underflow -> reload pass with the
  one-cycle interrupt-delay expectations.
- ICR read/set race tests are deterministic and match the selected variant.
- Force-load / start / stop writes during countdown produce expected results.
- CIA #2 NMI edge behavior and RESTORE NMI remain correct and independent.
- The selected default CIA variant is documented in STATUS.md.
```

---

## Phase 5 - Validation Suite, Debugger Visibility, and Close-Out

### Goal

Make the new CIA behavior maintainable and regression-proof, and reconcile the
documentation.

### Work

```text
- Unit + integration tests covering FLAG, serial shift, PC pulse, and the
  interrupt-delay/ICR-race behavior, in addition to the existing CIA tests.
- Optional debugger visibility: expose copied CIA serial/FLAG/PC/interrupt-line
  state through snapshots or safe peeks only. No live machine pointers cross
  threads.
- Add small local assembly diagnostics where useful; add public CIA test
  programs only where licensing permits, and track pass/fail in STATUS.md.
- Update docs/status/CIA.md (implementation + invariants + tests), STATUS.md
  (top-level handoff), docs/status/DEFERRED.md (remove the items now
  implemented, keep any that remain deferred), and docs/status/TESTING.md
  (new tests/smoke checks).
```

### Acceptance criteria

```text
- FLAG, serial, PC, and interrupt-timing tests exist and pass.
- Existing boot, display, keyboard, joystick, debugger, VIC-II, PRG, D64, PAL,
  and NTSC tests still pass.
- Any debugger CIA state added follows the snapshot rule.
- STATUS.md, docs/status/CIA.md, and docs/status/DEFERRED.md accurately reflect
  what is implemented and what remains deferred.
```

---

## Notes for Phase Document Authors

When executing any phase above, ensure the change set records:

```text
- Exact register addresses, bit masks, and CPU-visible vs debugger-safe behavior
  for every register or line touched.
- Which existing c64m structs/files change (cia.c/h, c64.c, c64_bus.c, tests)
  and which new fields are added.
- Whether the behavior applies to CIA #1, CIA #2, or both, and whether it depends
  on external C64 pin wiring rather than generic 6526 behavior.
- Precise acceptance criteria expressed as observable emulator behavior or test
  conditions, plus the regression tests added.
- Any interaction with the runtime thread, copied snapshots, or frontend
  debugger views.
- Any intentionally deferred hardware edge cases.
```

Follow the project rule throughout: build vertically, prefer the smallest
demonstrable machine slice, do not implement future phases early, and do not add
speculative abstractions.
