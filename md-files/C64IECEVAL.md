# C64IECEVAL.md — CPU Core Reuse Evaluation for 1541 Emulation

## Purpose

Before implementing a 1541 emulator that shares the existing 6502/6510 CPU core
with the C64, this evaluation determines whether the core is safe to instantiate
twice in the same process, and what (if any) work is required before it can be
reused.

This is an **evaluation and reporting task only**. No code is written or changed.
The findings determine the architecture of the 1541 implementation and must be
recorded in the findings section at the bottom of this document before any IEC
phase work begins.

---

## Required reading before proceeding

Read the following in order, all found under `md-files/` in the repository root:

1. `AGENTS.md` — agent workflow, architecture rules, thread ownership.
2. `MASTER.md` — component responsibility boundaries, dependency directions.
3. `STATUS.md` — current handoff routing and baseline summary.
4. `docs/status/CPU_MACHINE.md` — 6510 implementation status, bus integration,
   undocumented opcode coverage, BA stall model.
5. This document.

After reading, **stop**. State any questions or concerns. Then proceed with the
source reading and evaluation below.

---

## Background

The plan is to implement a 1541 disk drive emulator that:

- Runs the real 1541 ROM on a 6502 CPU instance.
- Steps in lockstep with the C64 on the same runtime thread (no separate thread;
  no inter-thread synchronisation needed on the IEC bus).
- Communicates with the C64 via the existing `iec_external_pull` / CIA #2 port
  model, which already models the open-collector IEC bus lines.

The C64's CPU is implemented in `src/machine/c6510.c` and `src/machine/c6510_inln.h`,
adapted from the a2m Apple II emulator's cycle-accurate NMOS 6502 core. The
a2m core was already used in two systems (Apple II and C64), which is reason for
optimism — but the C64 adaptation may have introduced coupling that makes a second
instantiation unsafe.

The 1541 contains a **standard NMOS 6502** (not a 6510). The differences from
the 6510 are minor: the 6510 adds a built-in I/O port at addresses `$00`/`$01`
for the C64's memory banking; the 1541's plain 6502 has no such port. Everything
else — instruction set, cycle timing, undocumented opcodes — is identical.

---

## Evaluation questions

Read the source files listed under each question. Answer each question precisely.
Do not infer from documentation alone — read the actual code.

---

### Q1 — Global and static state

**Files to read:** `src/machine/c6510.c`, `src/machine/c6510_inln.h`

**Question:** Does the CPU implementation use any file-scope static variables or
global variables that accumulate state across calls?

Look for:

- `static` variables at file scope (outside any function).
- `static` variables inside functions on the hot execution path (e.g. inside the
  opcode dispatch loop, inside fetch/decode, inside the cycle-step entry point).
- Any global arrays or tables that are **written** at runtime (read-only lookup
  tables are fine).

**What to report:**

- List every file-scope static and every function-local static found in the
  execution path, with the variable name, type, and what it holds.
- If none are found: state that explicitly.
- Verdict: **SAFE** (no mutable statics) or **UNSAFE** (mutable statics exist,
  must be moved into the struct before reuse).

---

### Q2 — Bus callback model

**Files to read:** `src/machine/c6510.c`, `src/machine/c6510_inln.h`,
`src/machine/c64_bus.c`, `src/machine/c64.h`

**Question:** How does the CPU core call back into the machine for bus reads and
writes? Is it via a function pointer / callback stored in the CPU struct, or via
direct calls to `c64_*` functions?

Look for:

- The CPU's read and write paths — how does `c6510` read a byte from memory, and
  how does it write one?
- Whether those calls go through a struct field (e.g. `cpu->read(cpu->ctx, addr)`)
  or call a named C64 function directly (e.g. `c64_cpu_read(c64, addr)`).
- Whether the CPU struct holds a pointer to its owning machine (a `void *ctx` or
  `c64_t *c64` field, or similar).

**What to report:**

- Describe the exact mechanism. Quote the relevant function signature(s) and the
  call site(s) in the opcode execution path.
- Verdict:
  - **CLEAN** — CPU calls through a function pointer or callback; the 1541 can
    supply its own bus callbacks with no changes to the CPU core.
  - **COUPLED** — CPU calls `c64_*` functions directly; a bus abstraction layer
    must be introduced before the core can be reused. Describe the scope of that
    work.

---

### Q3 — Per-instance isolation

**Files to read:** `src/machine/c6510.c`, `src/machine/c64.h`

**Question:** Is the `cpu_6510` struct fully self-contained? If two `cpu_6510`
instances existed simultaneously, would they share any state other than read-only
tables?

Look for:

- The `cpu_6510` struct definition. List all fields.
- Any field that points into `c64_t` or holds C64-specific state (e.g. a pointer
  to the C64's RAM, a pointer to CIA structs, etc.).
- Any field whose type or name suggests it is C64-specific rather than generic
  6502 state.

**What to report:**

- List all struct fields.
- Identify any that are C64-specific or that would need to differ between a C64
  instance and a 1541 instance.
- Verdict:
  - **SELF-CONTAINED** — the struct holds only generic 6502 architectural state;
    machine-specific coupling is handled entirely through the bus callback (Q2).
  - **COUPLED** — the struct holds C64-specific fields that would need to be
    generalised or removed for 1541 reuse. List them.

---

### Q4 — 6510 I/O port handling

**Files to read:** `src/machine/c6510.c`, `src/machine/c64_bus.c`

**Question:** Is the 6510-specific I/O port (addresses `$00` and `$01`, used for
C64 memory banking) handled inside the CPU core, or is it handled in the bus
layer (`c64_bus.c`) as a special case in the memory map?

The 1541 has a plain 6502, not a 6510. If the `$00`/`$01` I/O port is handled
inside the CPU core, that logic must either be removed for the 1541 instance or
made optional. If it is handled in the bus layer, the 1541 simply uses a
different bus layer and the CPU core needs no changes.

**What to report:**

- Where exactly is the `$00`/`$01` port handled? Quote the relevant code or
  describe its location precisely.
- Verdict:
  - **BUS-LAYER** — handled in `c64_bus.c`; the CPU core is a plain 6502 and
    the 1541 can reuse it unchanged.
  - **CPU-CORE** — handled inside `c6510.c`; the 1541 will need either a flag
    to disable it, a separate stripped-down CPU file, or the logic moved to the
    bus layer.

---

### Q5 — Cycle-step entry point

**Files to read:** `src/machine/c6510.c`, `src/machine/c64.c`

**Question:** What is the entry point for advancing the CPU by one cycle, and
does it cleanly separate "advance the CPU state machine" from "service C64-specific
hardware"?

For lockstep 1541 emulation, the runtime will call something like:

```c
c6510_step(c64_cpu);   // advance C64 CPU one cycle
iec1541_step(drive);   // advance 1541 CPU one cycle (and its VIAs)
```

This requires that the CPU step function only advances the CPU — it must not also
advance the VIC-II, CIA, SID, or other C64-specific hardware. Those must be
driven from the machine's own step wrapper.

Look for:

- The function that advances the CPU by one phi2 cycle. What is its signature?
- Does that function call into VIC-II, CIA, SID, or other C64 hardware directly,
  or does it only advance the 6502 state machine and perform bus reads/writes?
- Is there a higher-level `c64_advance_one_cycle()` that orchestrates CPU + all
  peripherals? If so, what does it call in what order?

**What to report:**

- Describe the call chain from the runtime step down to the CPU step and bus access.
- Verdict:
  - **CLEAN** — CPU step is self-contained; peripheral stepping is done separately
    in the machine's own orchestration layer. The 1541 can have its own equivalent
    orchestration.
  - **ENTANGLED** — CPU step drives peripheral updates directly; separation work
    is needed before the 1541 can be added without coupling the two machines'
    peripheral update paths.

---

## Summary findings

### Q1 — Global and static state
```
Verdict: SAFE

c6510.c has no file-scope statics and no global variables. The five functions
(c6510_init, c6510_set_irq_pending_callback, c6510_set_nmi_pending_callback,
c6510_reset, c6510_step) operate exclusively on the C6510 *m argument. No
opcode dispatch table exists at file scope; the dispatch is a direct
switch(opcode) inside c6510_step() with all state stored in *m.

c6510_inln.h contains only `static inline` functions. None declare static local
variables. Every function operates purely through the C6510 *m parameter.
The CYCLE(m) macro expands to do { (m)->cpu.cycles++; } while(0) — no hidden
state.

No mutable statics anywhere in the execution path.
```

### Q2 — Bus callback model
```
Verdict: CLEAN

The C6510 struct (c6510.h) carries four function pointers and one opaque
context pointer:

    typedef uint8_t (*c6510_read_fn)(void *user, uint16_t address);
    typedef void    (*c6510_write_fn)(void *user, uint16_t address, uint8_t value);
    typedef uint8_t (*c6510_irq_pending_fn)(void *user);
    typedef uint8_t (*c6510_nmi_pending_fn)(void *user);

    typedef struct C6510 {
        CPU cpu;
        void *user;
        c6510_read_fn read;
        c6510_write_fn write;
        c6510_irq_pending_fn irq_pending;
        c6510_nmi_pending_fn nmi_pending;
    } C6510;

All bus access in c6510_inln.h routes through two helpers:

    static inline uint8_t read_from_memory(C6510 *m, uint16_t address) {
        return m->read(m->user, address);
    }
    static inline void write_to_memory(C6510 *m, uint16_t address, uint8_t value) {
        m->write(m->user, address, value);
    }

Every opcode in the dispatch (c6510.c) calls only these two helpers and the
CYCLE(m) macro. No c64_* function is ever called from within the CPU core.

In c64.c the C64 wires its own callbacks:
    c6510_init(&machine->cpu, machine, c64_cpu_read, c64_cpu_write);
    c6510_set_irq_pending_callback(&machine->cpu, c64_cpu_irq_pending);
    c6510_set_nmi_pending_callback(&machine->cpu, c64_cpu_nmi_pending);

The 1541 replaces these with its own callbacks. No changes to c6510.c or
c6510_inln.h are required.
```

### Q3 — Per-instance isolation
```
Verdict: SELF-CONTAINED

Fields of the inner CPU struct (c6510.h):
    uint16_t pc           — program counter
    uint16_t opcode_pc    — PC at start of current opcode (for trace)
    uint16_t sp           — stack pointer (range 0x100-0x1FF)
    uint8_t  A, X, Y      — accumulator and index registers
    uint8_t  flags        — processor status (bit fields C,Z,I,D,B,E,V,N)
    uint16_t address_16   — address bus scratch (union of address_lo/hi)
    uint16_t scratch_16   — ALU scratch (union of scratch_lo/hi)
    uint8_t  page_fault   — undocumented-opcode page-cross flag
    uint8_t  irq_defer    — CLI/SEI one-instruction IRQ defer flag
    uint8_t  irq_defer_i  — saved I flag for deferred IRQ check
    uint8_t  opcode_active — signals write-history recording in bus callback
    uint32_t class        — CPU variant (CPU_6502 or CPU_65c02)
    uint64_t cycles       — monotonic cycle counter
    uint64_t irq_entries  — IRQ entry count (diagnostic)
    uint64_t nmi_entries  — NMI entry count (diagnostic)

Fields of the C6510 wrapper struct (c6510.h):
    CPU cpu               — the inner CPU state above
    void *user            — opaque machine context pointer
    c6510_read_fn read    — bus read callback
    c6510_write_fn write  — bus write callback
    c6510_irq_pending_fn irq_pending — IRQ query callback
    c6510_nmi_pending_fn nmi_pending — NMI query callback

C64-specific fields: NONE. The void *user field holds a c64_t * in the C64
case but is deliberately opaque — the CPU core never casts or dereferences it.
For the 1541 it would hold a c1541_t *.

The `class` field is set to CPU_6502 by c6510_init() — already the correct
value for the 1541's plain 6502.

Two C6510 instances share nothing. Each has its own register file, cycle
counter, and callback pointers. Two simultaneous instances are safe.
```

### Q4 — 6510 I/O port handling
```
Verdict: BUS-LAYER

The $00/$01 I/O port is handled entirely inside c64_bus.c. There is zero
special-case logic for these addresses anywhere in c6510.c or c6510_inln.h.

In c64_bus.c, c64_bus_read() (line 195-200):
    if (address <= C64_CPU_PORT_DATA) {   // C64_CPU_PORT_DATA = 0x0001
        return c64_bus_cpu_port_read(bus, address);
    }

In c64_bus.c, c64_bus_write() (line 231-239):
    if (address == C64_CPU_PORT_DIRECTION) {  // 0x0000
        bus->cpu_port_direction = value;
        return;
    }
    if (address == C64_CPU_PORT_DATA) {       // 0x0001
        bus->cpu_port_data = value;
        return;
    }

The CPU core calls m->read(m->user, addr) / m->write(m->user, addr, val) and
has no knowledge of what those addresses mean. The 1541's bus callbacks simply
map $0000-$07FF to the 1541's 2K RAM with no special cases — the 1541's plain
6502 has no I/O port at those addresses.

The CPU core requires no modification for 1541 reuse.
```

### Q5 — Cycle-step entry point
```
Verdict: CLEAN

Call chain:

1. c64_step_cycle(c64_t *machine, ...)     [public API, c64.c:1115]
       calls c64_step_cycle_internal(machine)

2. c64_step_cycle_internal(machine)        [internal orchestrator, c64.c:769]
       — checks KERNAL load trap (early-out if triggered)
       — if no pending CPU trace and BA not active:
             calls c64_prepare_deferred_cpu_trace(machine) [c64.c:751]
                 sets cpu_bus_mode = DEFER_WRITES
                 calls c6510_step(&machine->cpu)  ← PURE CPU STEP
                     reads use snapshot RAM, writes are recorded as events
                 resets cpu_bus_mode = IMMEDIATE
       — per elapsed cycle while pending_cpu_elapsed < total_cycles:
             applies recorded bus events at the right offset
             calls c64_advance_one_cycle(machine)

3. c64_advance_one_cycle(machine)          [peripheral orchestrator, c64.c:106]
       c64_step_vic(machine)   → vicii_step_cycle()
       c64_step_cia1(machine)  → cia_step_cycle()
       c64_step_cia2(machine)  → cia_step_cycle()
       c64_step_sid(machine)   → sid_advance_cycles()
       machine->clock.cycle++

4. c6510_step(C6510 *m)                    [pure 6502 state machine, c6510.c:49]
       c6510_take_nmi_if_pending(m)   — queries m->nmi_pending(m->user)
       c6510_take_irq_if_pending(m)   — queries m->irq_pending(m->user)
       switch(opcode) { ... }         — dispatches instruction helpers
       all bus access via m->read(m->user, addr) / m->write(m->user, addr, val)
       no VIC, CIA, SID, or clock calls whatsoever
       returns cycle count consumed

c6510_step() is completely decoupled from C64-specific hardware. Peripheral
stepping is entirely in c64_advance_one_cycle(), which is called by the C64's
own orchestration. The 1541 implements its own equivalent:

    c6510_step(&drive->cpu)          // 1541 CPU instruction (uses 1541 callbacks)
    c1541_advance_one_cycle(drive)   // step VIA #1, VIA #2, 1541 clock

No entanglement between the two machines' peripheral update paths.
```

---

## Overall verdict and recommended next step

**A — Ready for reuse.**

All five verdicts are the clean/safe option:

| Question | Verdict |
|----------|---------|
| Q1 — Global/static state | SAFE |
| Q2 — Bus callback model | CLEAN |
| Q3 — Per-instance isolation | SELF-CONTAINED |
| Q4 — 6510 I/O port handling | BUS-LAYER |
| Q5 — Cycle-step entry point | CLEAN |

The existing CPU core (`c6510.c` + `c6510_inln.h`) can be instantiated directly
for the 1541 with **no changes** to those files. The 1541 implementation consists
entirely of new code:

1. A `c1541_t` machine struct (RAM, ROM, VIA #1, VIA #2, embedded C6510 instance).
2. Bus callbacks `c1541_cpu_read` / `c1541_cpu_write` mapping the 1541 memory map
   (2K RAM at $0000-$07FF, VIA #1 at $1800, VIA #2 at $1C00, ROM at $C000-$FFFF).
3. IRQ/NMI pending callbacks driven by the VIA interrupt flags.
4. `c1541_advance_one_cycle()` stepping the two VIAs and the 1541 clock.
5. Runtime wiring to call both `c64_step_cycle()` and a new `c1541_step_cycle()`
   per master clock tick.

No preparatory CPU-core refactoring is needed. Proceed directly to the 1541
architecture planning document (`C64IEC1541.md`).

---

## What comes next

Once the overall verdict is recorded and any preparatory work identified, the
next document will be `C64IEC1541.md` — the architecture and implementation plan
for the 1541 emulator itself, covering:

- The 1541 memory map (ROM, RAM, VIA #1, VIA #2, serial port).
- The MOS 6522 VIA implementation needed for the 1541's serial and parallel ports.
- How the 1541 integrates with the existing IEC bus model in `c64.c`.
- The lockstep stepping model.
- Runtime mount/unmount of D64 images by the 1541 (replacing the current KERNAL trap approach incrementally or wholesale).
- Status document updates.

That document is not written until this evaluation is complete.
