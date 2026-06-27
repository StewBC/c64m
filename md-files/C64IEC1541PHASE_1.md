# C64IEC1541PHASE_1.md — 1541 Phase 1: MOS 6522 VIA Implementation

## Required reading before proceeding

Read the following in order, all found under `md-files/` in the repository root:

1. `AGENTS.md` — agent workflow, build/test rules, architecture rules, thread ownership.
2. `MASTER.md` — component responsibility boundaries, dependency directions.
3. `STATUS.md` — current handoff routing and baseline summary.
4. `docs/status/CIA.md` — the CIA implementation is the closest existing analogue
   to the VIA. Read it to understand the project's timer/interrupt conventions
   before writing new ones.
5. `docs/status/CPU_MACHINE.md` — confirms the CPU core reuse model.
6. `C64IECEVAL.md` — the CPU reuse evaluation. All five questions returned clean
   verdicts. Understand these findings before starting.
7. `C64IEC1541.md` — the full 1541 architecture plan. This phase implements one
   part of it.
8. This document.

After reading, **stop**. State any questions or concerns before touching any
code. Then, and only then, proceed to implementation.

---

## Scope of this phase

This phase implements **only** the MOS 6522 VIA module:

```
src/machine/via6522.h    — new file
src/machine/via6522.c    — new file
```

Nothing else changes. No `c1541`, no `c64` wiring, no IEC bus. Just the VIA,
self-contained, with passing unit tests.

The VIA is the foundation everything else rests on. Getting it right in
isolation — with tests — is much safer than building it entangled with the 1541
and IEC wiring all at once.

---

## MOS 6522 VIA background

The MOS 6522 Versatile Interface Adapter is a peripheral chip used in the 1541
disk drive (and many other 6502-based systems). It provides:

- Two 8-bit bidirectional I/O ports (Port A and Port B) with direction registers.
- Two 16-bit programmable interval timers (T1 and T2).
- An 8-bit shift register.
- Interrupt logic (IFR/IER) that can flag and enable seven interrupt sources.
- Peripheral control lines (CA1, CA2, CB1, CB2) for handshaking.

The 6522 is similar in spirit to the 6526 CIA already implemented in this
project, but simpler: no TOD clock, no BCD alarm, no CNT cascade mode. Study
the existing CIA implementation (`src/machine/cia.c`, `src/machine/cia.h`) for
project conventions on timer countdown semantics and interrupt flag/enable
separation before writing the VIA.

**The 6522 is not the same chip as the 6526.** Do not copy CIA code and assume
it is correct for the VIA. Use the CIA only as a style and convention reference.

---

## Register map

Base address `B` (will be `$1800` for VIA #1 and `$1C00` for VIA #2 in the
1541 memory map, but the VIA module itself is address-agnostic — it receives
a register index 0–15).

| Index | Name | Read behaviour | Write behaviour |
|---|---|---|---|
| 0 | ORB / IRB | Read Port B (input pins, masked by DDRB) | Write ORB (output latch) |
| 1 | ORA / IRA | Read Port A (input pins, masked by DDRA); clears CA1/CA2 flags | Write ORA (output latch) |
| 2 | DDRB | Read DDRB | Write DDRB |
| 3 | DDRA | Read DDRA | Write DDRA |
| 4 | T1C-L | Read T1 counter low byte; **clears T1 interrupt flag (IFR bit 6)** | Write T1 latch low |
| 5 | T1C-H | Read T1 counter high byte | Write T1 latch high; **loads counter from latch; starts T1; clears IFR bit 6** |
| 6 | T1L-L | Read T1 latch low | Write T1 latch low (does not affect counter) |
| 7 | T1L-H | Read T1 latch high | Write T1 latch high (does not affect counter or start T1) |
| 8 | T2C-L | Read T2 counter low; **clears T2 interrupt flag (IFR bit 5)** | Write T2 latch low |
| 9 | T2C-H | Read T2 counter high | Write T2 latch high; **loads counter from latch; starts T2; clears IFR bit 5** |
| 10 | SR | Read shift register | Write shift register |
| 11 | ACR | Read ACR | Write ACR |
| 12 | PCR | Read PCR | Write PCR |
| 13 | IFR | Read IFR (bit 7 = any active interrupt) | Write clears named bits (write 1 to clear) |
| 14 | IER | Read IER (bit 7 always 1) | Write: if bit 7=1, set named bits; if bit 7=0, clear named bits |
| 15 | ORA (no handshake) | Read Port A without clearing CA1/CA2 flags | Write ORA without handshake |

---

## IFR bit assignments

| Bit | Source |
|---|---|
| 7 | Any interrupt (set if any enabled flag is set; read-only; computed) |
| 6 | T1 timeout |
| 5 | T2 timeout |
| 4 | CB1 active edge |
| 3 | CB2 active edge |
| 2 | Shift register complete |
| 1 | CA1 active edge |
| 0 | CA2 active edge |

IFR bit 7 is never written. On read it returns `(IFR & IER & 0x7F) != 0 ? 0x80 : 0`.

---

## ACR bit assignments

| Bit(s) | Function |
|---|---|
| 7 | T1 PB7 output enable (1 = T1 toggles PB7 on timeout) |
| 6 | T1 mode: 0 = one-shot, 1 = free-run |
| 5 | T2 mode: 0 = interval timer, 1 = pulse count on PB6 (implement interval only) |
| 4–2 | Shift register mode (implement as stub — register is R/W but behaviour not required) |
| 1 | Port B latch enable |
| 0 | Port A latch enable |

For this phase, implement T1 one-shot and free-run (ACR bit 6), T1 PB7 output
(ACR bit 7), and T2 interval timer (ACR bit 5 = 0). All other ACR bits are
accepted on write and returned on read but need not affect behaviour.

---

## PCR bit assignments

| Bit(s) | Function |
|---|---|
| 7–5 | CB2 control |
| 4 | CB1 edge select: 0 = negative (high→low), 1 = positive (low→high) |
| 3–1 | CA2 control |
| 0 | CA1 edge select: 0 = negative (high→low), 1 = positive (low→high) |

For this phase, implement CA1 edge detection using PCR bit 0. CA2 and CB1/CB2
edge detection should be stubbed (accept writes, return reads, do not fire
interrupt flags). CA1 is the critical one: it is the ATN interrupt source on
VIA #2.

---

## Struct definition

```c
// src/machine/via6522.h

typedef struct via6522 {
    /* Port registers */
    uint8_t  ora;           // Port A output register
    uint8_t  orb;           // Port B output register
    uint8_t  ddra;          // Port A direction (1 = output)
    uint8_t  ddrb;          // Port B direction (1 = output)

    /* External input pins (set by owner, not by register writes) */
    uint8_t  port_a_in;     // current external input state on Port A pins
    uint8_t  port_b_in;     // current external input state on Port B pins

    /* Timers */
    uint16_t t1_counter;
    uint16_t t1_latch;
    int      t1_running;
    int      t1_pb7_state;  // current PB7 toggle state (0 or 1)
    uint16_t t2_counter;
    uint8_t  t2_latch_low;
    int      t2_running;

    /* Shift register (stub) */
    uint8_t  sr;

    /* Control registers */
    uint8_t  acr;
    uint8_t  pcr;

    /* Interrupt registers */
    uint8_t  ifr;
    uint8_t  ier;

    /* CA1 edge detection */
    uint8_t  ca1_last;      // last known CA1 pin level (0 or 1)
} via6522;
```

---

## Function signatures

```c
// src/machine/via6522.h

void    via6522_init(via6522 *v);
void    via6522_reset(via6522 *v);

// Register access (reg = 0–15)
uint8_t via6522_read(via6522 *v, uint8_t reg);
void    via6522_write(via6522 *v, uint8_t reg, uint8_t value);

// Advance one phi2 cycle
void    via6522_step(via6522 *v);

// Returns 1 if (IFR & IER & 0x7F) != 0
int     via6522_irq_pending(via6522 *v);

// Feed external pin state into Port A input bits.
// Called by the owning machine after computing bus state.
// 'inputs' is the full 8-bit value; bits set as inputs (DDRA=0) will
// be read back as these values; bits set as outputs are unaffected.
void    via6522_set_port_a_inputs(via6522 *v, uint8_t inputs);

// Feed external pin state into Port B input bits. Same semantics as above.
void    via6522_set_port_b_inputs(via6522 *v, uint8_t inputs);

// Feed CA1 pin level (0 or 1). Detects configured active edge and sets
// IFR bit 1 if edge matches PCR bit 0 polarity.
void    via6522_set_ca1(via6522 *v, uint8_t level);
```

---

## Implementation notes

### Port reads

When the CPU reads Port A (register 1):
```
result = (ora & ddra) | (port_a_in & ~ddra)
```
Output bits come from `ora`; input bits come from `port_a_in`. This is the
standard open-drain / bidirectional port model. Port B (register 0) is the same
with `orb` and `ddrb`.

Reading register 1 (ORA with handshake) clears IFR bits 0 and 1 (CA2 and CA1
flags). Reading register 15 (ORA without handshake) does not clear them.

### Timer 1

On write to T1C-H (register 5):
- Store value in `t1_latch` high byte.
- Load `t1_counter` from `t1_latch` (both bytes).
- Clear IFR bit 6.
- Set `t1_running = 1`.

On `via6522_step()`, if `t1_running`:
- Decrement `t1_counter`.
- If `t1_counter` wraps through zero (i.e. was 0 before decrement, or reaches
  0xFFFF — confirm against 6522 datasheet: the counter is 16-bit and underflows
  from 0x0000 to 0xFFFF):
  - Set IFR bit 6.
  - If ACR bit 7: toggle `t1_pb7_state`.
  - If ACR bit 6 = 1 (free-run): reload `t1_counter` from `t1_latch`.
  - If ACR bit 6 = 0 (one-shot): set `t1_running = 0`.

Reading T1C-L (register 4) clears IFR bit 6.

### Timer 2

T2 in interval timer mode (ACR bit 5 = 0, which is the only mode implemented):

On write to T2C-H (register 9):
- Store value in `t2_counter` high byte (low byte was already stored in
  `t2_latch_low` on the previous write to register 8).
- Set `t2_counter` low byte from `t2_latch_low`.
- Clear IFR bit 5.
- Set `t2_running = 1`.

On `via6522_step()`, if `t2_running`:
- Decrement `t2_counter`.
- On underflow: set IFR bit 5. T2 does **not** reload (it is always one-shot).
  Set `t2_running = 0`.

Reading T2C-L (register 8) clears IFR bit 5.

### CA1

```c
void via6522_set_ca1(via6522 *v, uint8_t level) {
    uint8_t active_edge = (v->pcr & 0x01) ? 1 : 0; // PCR bit 0: 0=neg, 1=pos
    uint8_t edge_fired = active_edge
        ? (!v->ca1_last && level)    // positive: low→high
        : (v->ca1_last && !level);   // negative: high→low
    v->ca1_last = level;
    if (edge_fired) {
        v->ifr |= 0x02; // set IFR bit 1
    }
}
```

### IFR / IER

IER write: if bit 7 = 1, OR the lower 7 bits into IER; if bit 7 = 0, AND the
complement of the lower 7 bits into IER (clear those bits).

IFR write: clear the bits named by the written value (write 1 to clear, similar
to CIA ICR). Bit 7 is read-only and cannot be written.

IFR bit 7 on read: return `((v->ifr & v->ier & 0x7F) != 0) ? 0x80 : 0x00`
ORed into the lower 7 bits of IFR.

`via6522_irq_pending()` returns `(v->ifr & v->ier & 0x7F) != 0`.

---

## What to add to CMake

Add `via6522.c` to the `machine/` library target. No new external dependencies.

---

## Tests to write

Add `tests/machine/test_via6522.c` (following the naming and structure of
existing test files, e.g. `test_sid.c`).

Required test cases — each must pass before this phase is considered complete:

**Initialisation:**
- After `via6522_init()`: all registers zero, `ca1_last = 0`, timers not running.
- After `via6522_reset()`: same as init.

**Port reads:**
- DDRA = `0xFF` (all output): Port A read returns `ora`.
- DDRA = `0x00` (all input): Port A read returns `port_a_in`.
- DDRA = `0xF0` (mixed): Port A read returns `(ora & 0xF0) | (port_a_in & 0x0F)`.
- Same three cases for Port B / DDRB.

**Timer 1 one-shot (ACR bit 6 = 0):**
- Write latch low then high (triggers load and start).
- Step N cycles (N = latch value). Verify IFR bit 6 is clear before underflow.
- Step one more cycle. Verify IFR bit 6 is set.
- Verify `t1_running = 0` after underflow.
- Verify counter does not reload (stays at 0xFFFF or near).
- Read T1C-L: verify IFR bit 6 is cleared.

**Timer 1 free-run (ACR bit 6 = 1):**
- Set ACR bit 6. Load and start T1 with a small value.
- Run past underflow twice. Verify IFR bit 6 is set on each underflow.
- Verify counter reloads from latch each time.

**Timer 1 PB7 (ACR bit 7 = 1):**
- Enable PB7 output and free-run. Run past two underflows.
- Verify `t1_pb7_state` toggles on each underflow.

**Timer 2:**
- Write T2 latch low then high. Step N cycles. Verify IFR bit 5 set on underflow.
- Verify T2 does not reload.
- Read T2C-L: verify IFR bit 5 is cleared.

**CA1 negative edge (PCR bit 0 = 0, default):**
- `via6522_set_ca1(v, 1)` then `via6522_set_ca1(v, 0)`: verify IFR bit 1 set.
- `via6522_set_ca1(v, 0)` then `via6522_set_ca1(v, 1)`: verify IFR bit 1 NOT set.

**CA1 positive edge (PCR bit 0 = 1):**
- `via6522_set_ca1(v, 0)` then `via6522_set_ca1(v, 1)`: verify IFR bit 1 set.
- `via6522_set_ca1(v, 1)` then `via6522_set_ca1(v, 0)`: verify IFR bit 1 NOT set.

**IFR / IER:**
- Set IER bit 6 (T1 enable). Fire T1 underflow. Verify `via6522_irq_pending()` = 1.
- Clear IER bit 6. Verify `via6522_irq_pending()` = 0 (flag still set, but masked).
- Write IFR with bit 6 set. Verify IFR bit 6 is cleared.
- Verify IFR bit 7 on read reflects `(IFR & IER & 0x7F) != 0`.

---

## Acceptance criteria

- All tests listed above pass.
- All existing project tests continue to pass (no regressions).
- `via6522.c` and `via6522.h` contain no SDL, Nuklear, runtime, platform,
  frontend, or `tools/d64/` includes.
- No global or static mutable state in `via6522.c`.

---

## Status document updates after this phase

- `STATUS.md`: add a one-line note under "Recent high-value handoff notes" that
  the VIA 6522 module is implemented and tested.
- Create `docs/status/IEC1541.md` as a new stub file with the following content:

```markdown
# IEC / 1541 status

## Current implementation

- VIA 6522 module implemented and unit-tested (Phase 1).
- 1541 machine shell: not yet started.
- IEC bus wiring: not yet started.

## Known limitations / deferred

- Everything beyond VIA 6522 is deferred to subsequent phases.
```

Do not update `docs/status/DEFERRED.md` yet — IEC and 1541 remain fully
deferred until Phase 3 is complete.

---

## What comes next

Phase 2 uses `via6522` to build the 1541 machine shell: the 6502 CPU instance,
memory map, ROM loading, and cycle-step orchestration. The IEC bus is not wired
in Phase 2 either — that comes in Phase 3. Each phase builds on a tested
foundation.
