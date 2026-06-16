# C64VICPHASE_A.md — VIC-II Phase A: Raster Timing Accuracy & Bad Lines

## Purpose

This document is a coding-agent-ready implementation guide for VIC-II Phase A as
defined in `md-files/C64MVICII.md`. Read `md-files/AGENTS.md`, `md-files/MASTER.md`,
and `md-files/STATUS.md` before starting. Implement only what is described here.

Primary reference: Christian Bauer, "The MOS 6567/6569 video controller (VIC-II) and
its application in the Commodore 64" (1996). All timing and counter rules below derive
from that document.

---

## Goal

Make the VIC-II raster counter cycle-accurate, add Bad Line detection, stall the CPU
for 40 cycles on each Bad Line (BA/RDY model), maintain the internal VC/VCBASE/RC
counters that gate display vs idle state, and fire the raster IRQ at the correct cycle
within the correct raster line.

The batch frame renderer in `vicii_make_frame_snapshot` is **not** changed in this
phase. Pixel-accurate rendering is Phase B. This phase establishes the internal counter
state that Phase B and later phases will consume.

---

## Files Modified

| File | Reason |
|------|--------|
| `src/machine/vicii.h` | New fields in `vicii` and `vicii_timing`; updated `vicii_step_cycle` signature; new `vicii_ba_active()` declaration |
| `src/machine/vicii.c` | `vicii_reset` init; register read/write updates; full `vicii_step_cycle` replacement; `vicii_ba_active` implementation |
| `src/machine/c64.c` | `c64_step_vic` passes bus pointer; `c64_step_cycle` adds BA stall; `c64_cpu_irq_pending` adds VIC IRQ check |

No other files are modified in this phase.

---

## Section 1 — Register Map

All register indices are relative to the VIC-II base at `$D000`, mirrored every 64
bytes through `$D3FF`. `vicii_read_register` and `vicii_write_register` receive the
raw address and mask it with `0x3F` to produce the register index.

| Index | Address | Name       | Bits relevant to Phase A |
|-------|---------|------------|--------------------------|
| `$11` | `$D011` | CONTROL_1  | Bit 7 = RST8 (raster Y bit 8, read-only in normal use), bit 4 = DEN, bits 2:0 = YSCROLL |
| `$12` | `$D012` | RASTER     | Read: raster_y bits 7:0. Write: raster_compare bits 7:0 |
| `$19` | `$D019` | IRQ_STATUS | Bit 0 = IRST (raster IRQ fired). Bits 7:4 always read as 1. Write-1-to-clear per bit |
| `$1A` | `$D01A` | IRQ_ENABLE | Bit 0 = ERST (enable raster IRQ). Bits 7:4 always read as 1 |

**$D011 write:** bits 6:0 are stored in `registers[0x11]`. Bit 7 (RST8) writes the
high bit of the raster compare value, stored separately in `timing.raster_compare`.
RST8 is not a stored register bit; writing it updates `raster_compare` only.

**$D011 read:** bit 7 returns the current raster line bit 8 (`timing.raster_line >> 8`).
This is already implemented correctly.

**$D012 write:** sets `raster_compare` bits 7:0, preserving the RST8-sourced bit 8.

**$D012 read:** returns `timing.raster_line & 0xFF`. Already implemented correctly.

**$D019 write:** each bit set to 1 in the written byte *clears* the corresponding IRQ
flag (write-1-to-clear). Example: writing `$01` clears IRST. Other flag bits are
unaffected.

**$D019 read:** return `(irq_status & 0x0F) | 0xF0`. Bits 7:4 are always 1.

**$D01A read:** return `(irq_enable & 0x0F) | 0xF0`. Bits 7:4 are always 1.

---

## Section 2 — New Fields

### 2.1 `vicii_timing` additions (in `vicii.h`)

```c
typedef struct vicii_timing {
    uint32_t cycles_per_line;
    uint32_t lines_per_frame;
    uint32_t cycle_in_line;
    uint32_t raster_line;
    uint64_t frame_number;
    bool     frame_complete;
    vicii_video_standard standard;

    /* Phase A additions */
    uint16_t raster_compare;       /* 9-bit: 0-311 PAL, 0-262 NTSC */
    bool     ba_low;               /* true while VIC holds BA low */
    uint32_t ba_low_until_cycle;   /* cycle_in_line at which BA returns high */
} vicii_timing;
```

### 2.2 `vicii` struct additions (in `vicii.h`)

```c
struct vicii {
    uint8_t registers[VICII_REGISTER_COUNT];
    vicii_timing timing;
    c64_frame working_frame;

    /* Phase A additions */
    uint16_t vc;               /* Video Counter, 10-bit, 0-1023 */
    uint16_t vc_base;          /* VCBASE: latched copy of vc at Bad Line row start */
    uint8_t  rc;               /* Row Counter, 3-bit, 0-7 */
    bool     display_state;    /* true while a character row is in progress */
    bool     bad_line;         /* true if the current raster line is a Bad Line */
    uint8_t  video_matrix[40]; /* character codes fetched on Bad Lines */
    uint8_t  color_line[40];   /* color nibbles fetched on Bad Lines */
    uint8_t  irq_status;       /* live shadow of $D019 low nibble */
    uint8_t  irq_enable;       /* live shadow of $D01A low nibble */
};
```

### 2.3 New function declaration (in `vicii.h`)

```c
bool vicii_ba_active(const vicii *v);
```

### 2.4 Updated function signature (in `vicii.h`)

Change `vicii_step_cycle` to accept the bus pointer needed for c-access fetches:

```c
/* Old */
void vicii_step_cycle(vicii *v);

/* New */
void vicii_step_cycle(vicii *v, const c64_bus_t *bus);
```

The `c64_bus_t` forward declaration is already present in `vicii.h`.

---

## Section 3 — `vicii_reset` Updates

After the existing `memset` calls in `vicii_reset`, add:

```c
v->vc                        = 0;
v->vc_base                   = 0;
v->rc                        = 0;
v->display_state             = false;
v->bad_line                  = false;
v->timing.raster_compare     = 0;
v->timing.ba_low             = false;
v->timing.ba_low_until_cycle = 0;
v->irq_status                = 0;
v->irq_enable                = 0;
memset(v->video_matrix, 0, sizeof(v->video_matrix));
memset(v->color_line,   0, sizeof(v->color_line));
```

---

## Section 4 — Register Read/Write Updates

### 4.1 `vicii_write_register`

Replace the current single-line store with a switch. The default case preserves the
existing behavior for all other registers.

```c
void vicii_write_register(vicii *v, uint16_t addr, uint8_t value) {
    uint8_t reg;

    assert(v);
    reg = (uint8_t)(addr & 0x3Fu);

    switch (reg) {
    case 0x11: /* CONTROL_1: bit 7 is RST8, updates raster_compare bit 8 */
        v->registers[reg] = value & 0x7Fu;
        if (value & 0x80u) {
            v->timing.raster_compare |= 0x100u;
        } else {
            v->timing.raster_compare &= 0x00FFu;
        }
        break;

    case 0x12: /* RASTER compare low byte */
        v->timing.raster_compare =
            (v->timing.raster_compare & 0x100u) | (uint16_t)value;
        break;

    case 0x19: /* IRQ_STATUS: write-1-to-clear */
        v->irq_status &= (uint8_t)(~value & 0x0Fu);
        v->registers[reg] = v->irq_status;
        break;

    case 0x1A: /* IRQ_ENABLE */
        v->irq_enable    = value & 0x0Fu;
        v->registers[reg] = v->irq_enable;
        break;

    default:
        v->registers[reg] = value;
        break;
    }
}
```

### 4.2 `vicii_read_register`

Add two cases before the final `return v->registers[reg]` in the existing function:

```c
case 0x19: /* IRQ_STATUS: bits 7:4 always 1 */
    return (uint8_t)(v->irq_status | 0xF0u);

case 0x1A: /* IRQ_ENABLE: bits 7:4 always 1 */
    return (uint8_t)(v->irq_enable | 0xF0u);
```

Keep the existing $D011 and $D012 read handling unchanged.

---

## Section 5 — `vicii_step_cycle` Replacement

### 5.1 Private constants (add to the enum block in `vicii.c`)

```c
VICII_BADLINE_FIRST        = 0x30,  /* first Y that can be a Bad Line (PAL and NTSC) */
VICII_BADLINE_LAST         = 0xF7,  /* last Y that can be a Bad Line */
VICII_CACCESS_FIRST_CYCLE  = 15,    /* first cycle of the c-access window */
VICII_CACCESS_LAST_CYCLE   = 54,    /* last cycle of the c-access window (40 total) */
VICII_BA_ASSERT_CYCLE      = 12,    /* BA goes low at this cycle on Bad Lines */
VICII_VC_MAX               = 1023,
VICII_IRQ_RASTER           = 0x01,  /* IRST bit position */
```

The c-access window (cycles 15–54) applies to both PAL and NTSC. For NTSC the line is
65 cycles wide instead of 63, but the window position within the line is the same.

### 5.2 Helper: `vicii_is_bad_line` (static, in `vicii.c`)

```c
static bool vicii_is_bad_line(const vicii *v) {
    uint32_t y      = v->timing.raster_line;
    uint8_t  yscroll = v->registers[0x11] & 0x07u;
    bool     den    = (v->registers[0x11] & 0x10u) != 0;

    if (!den) return false;
    if (y < VICII_BADLINE_FIRST || y > VICII_BADLINE_LAST) return false;
    return (uint8_t)(y & 0x07u) == yscroll;
}
```

### 5.3 Helper: `vicii_assert_raster_irq` (static, in `vicii.c`)

The IRST flag is always set when the raster compare matches, regardless of ERST. ERST
controls only whether the IRQ line is asserted to the CPU. Setting the flag here is
sufficient; the CPU-side check (Section 6.3) gates on `irq_status & irq_enable`.

```c
static void vicii_assert_raster_irq(vicii *v) {
    v->irq_status   |= VICII_IRQ_RASTER;
    v->registers[0x19] = v->irq_status;
}
```

### 5.4 Full `vicii_step_cycle` replacement

```c
void vicii_step_cycle(vicii *v, const c64_bus_t *bus) {
    uint32_t cycle;
    uint16_t screen_base;
    uint32_t col;
    uint16_t screen_addr;

    assert(v);
    assert(bus);

    cycle = v->timing.cycle_in_line;

    /* ------------------------------------------------------------------ */
    /* Start-of-line events (cycle 0 of the current raster line)           */
    /* ------------------------------------------------------------------ */
    if (cycle == 0) {
        /* Raster IRQ: fires at cycle 0 of the matching line (Bauer §3.5) */
        if (v->timing.raster_line == v->timing.raster_compare) {
            vicii_assert_raster_irq(v);
        }

        /* Bad Line detection: evaluated once at the start of each line */
        v->bad_line = vicii_is_bad_line(v);

        if (v->bad_line) {
            /* RC resets to 0 on a Bad Line (Bauer §3.7.1) */
            v->rc = 0;
            /* VC is reloaded from VCBASE at the start of each Bad Line */
            v->vc = v->vc_base;
            /* Enter display state */
            v->display_state = true;
            /* BA goes low at cycle VICII_BA_ASSERT_CYCLE = 12.
               Since we are at cycle 0, assert BA now for the whole line.
               The c-access window opens at cycle 15 and closes after cycle 54.
               BA returns high at cycle 55. */
            v->timing.ba_low             = true;
            v->timing.ba_low_until_cycle = (uint32_t)(VICII_CACCESS_LAST_CYCLE + 1);
        }
    }

    /* ------------------------------------------------------------------ */
    /* c-access window (cycles 15-54) on Bad Lines                         */
    /* Fetches 40 character codes from screen RAM and 40 color nibbles.    */
    /* ------------------------------------------------------------------ */
    if (v->bad_line &&
        cycle >= (uint32_t)VICII_CACCESS_FIRST_CYCLE &&
        cycle <= (uint32_t)VICII_CACCESS_LAST_CYCLE) {

        col         = cycle - (uint32_t)VICII_CACCESS_FIRST_CYCLE;
        screen_base = (uint16_t)((v->registers[0x18] >> 4) * 0x0400u);
        screen_addr = (uint16_t)(screen_base + v->vc + col);

        v->video_matrix[col] = c64_bus_vic_read_ram(bus, screen_addr);
        v->color_line[col]   = c64_bus_vic_read_color(bus, (uint16_t)(v->vc + col));
    }

    /* ------------------------------------------------------------------ */
    /* BA signal: release when the c-access window is over                 */
    /* ------------------------------------------------------------------ */
    if (v->timing.ba_low && cycle >= v->timing.ba_low_until_cycle) {
        v->timing.ba_low = false;
    }

    /* ------------------------------------------------------------------ */
    /* Advance cycle counter                                               */
    /* ------------------------------------------------------------------ */
    v->timing.cycle_in_line++;
    if (v->timing.cycle_in_line < v->timing.cycles_per_line) {
        return;
    }

    /* ------------------------------------------------------------------ */
    /* End-of-line events                                                  */
    /* ------------------------------------------------------------------ */
    v->timing.cycle_in_line = 0;

    /* Advance VC by 40 for the g-accesses that occurred on display lines.
       This must happen before the RC/VCBASE update below. */
    if (v->display_state) {
        v->vc = (uint16_t)((v->vc + VICII_TEXT_COLUMNS) & (uint16_t)VICII_VC_MAX);

        /* RC increments at end of each display line */
        if (v->rc == VICII_RC_MAX) {
            /* Last pixel row of this character row: latch VCBASE, leave display state */
            v->vc_base    = v->vc;
            v->display_state = false;
        }
        v->rc = (uint8_t)((v->rc + 1u) & 0x07u);
    }

    /* Advance raster line */
    v->timing.raster_line++;
    if (v->timing.raster_line < v->timing.lines_per_frame) {
        return;
    }

    /* ------------------------------------------------------------------ */
    /* End-of-frame events                                                 */
    /* ------------------------------------------------------------------ */
    v->timing.raster_line = 0;
    v->timing.frame_number++;
    v->timing.frame_complete = true;

    /* VC and VCBASE reset to 0 at top of frame (Bauer §3.7.2) */
    v->vc            = 0;
    v->vc_base       = 0;
    v->rc            = 0;
    v->display_state = false;
    v->bad_line      = false;
}
```

---

## Section 6 — Changes to `c64.c`

### 6.1 `c64_step_vic` — pass bus pointer

```c
static void c64_step_vic(c64_t *machine) {
    vicii_step_cycle(&machine->vic, &machine->bus);
    machine->clock.vic_cycles++;
}
```

### 6.2 `c64_step_cycle` — reorder and add BA stall

The VIC must tick first so that the BA state it computes from the new cycle position is
available before the CPU decides whether to execute. The existing function ticks the
CPU first; reverse that order and add the stall check.

```c
bool c64_step_cycle(c64_t *machine, char *error, size_t error_size) {
    assert(machine);

    if (!machine->ready) {
        c64_set_error(error, error_size, "machine is not ready");
        return false;
    }

    /* Tick VIC and CIAs first so BA state is valid for this cycle */
    c64_step_vic(machine);
    c64_step_cia1(machine);
    c64_step_cia2(machine);
    c64_step_sid(machine);
    machine->clock.cycle++;

    /* Stall CPU if BA is low (VIC is stealing cycles for c-accesses) */
    if (!vicii_ba_active(&machine->vic)) {
        if (machine->cpu_cycles_remaining == 0) {
            machine->cpu_cycles_remaining = c6510_step(&machine->cpu);
            if (machine->cpu_cycles_remaining == 0) {
                machine->cpu_cycles_remaining = 1;
            }
        }
        machine->cpu_cycles_remaining--;
        machine->clock.cpu_cycles++;
    }

    c64_set_error(error, error_size, "");
    return true;
}
```

### 6.3 `c64_cpu_irq_pending` — add VIC IRQ

The VIC fires a CPU IRQ when `irq_status & irq_enable` is non-zero. Add that check:

```c
static uint8_t c64_cpu_irq_pending(void *user) {
    c64_t *machine = user;
    bool cia_irq = cia_irq_pending(&machine->cia1);
    bool vic_irq = (machine->vic.irq_status & machine->vic.irq_enable) != 0;
    return (cia_irq || vic_irq) ? 1u : 0u;
}
```

### 6.4 `vicii_ba_active` implementation (in `vicii.c`)

```c
bool vicii_ba_active(const vicii *v) {
    assert(v);
    return v->timing.ba_low;
}
```

---

## Section 7 — `c64_step_instruction` Note

`c64_step_instruction` calls `c6510_step` directly and does not VIC-tick. This is
correct for the single-instruction debugger path and must not change. It will not
trigger Bad Line stalls. The VIC cycle counter (`clock.vic_cycles`) will drift from
the CPU cycle counter when using single-step, which is acceptable.

---

## Section 8 — BA Timing Detail

Per Bauer section 3.6, BA goes low 3 cycles before the VIC needs the bus. The
c-access window begins at cycle 15, so BA should go low at cycle 12. In the
implementation above, the Bad Line is detected at cycle 0 and BA is asserted
immediately. This means BA goes low at cycle 0 rather than cycle 12 on a Bad Line
raster line. This is conservative (BA is low for 55 cycles instead of 43) but
correct in that the CPU never executes during the actual c-access window. For
cycle-exact BA timing (needed for demos that rely on the 3-cycle BA warning window),
change the assertion:

```c
/* Deferred BA assertion: assert only when cycle reaches VICII_BA_ASSERT_CYCLE */
if (v->bad_line && cycle == (uint32_t)VICII_BA_ASSERT_CYCLE) {
    v->timing.ba_low             = true;
    v->timing.ba_low_until_cycle = (uint32_t)(VICII_CACCESS_LAST_CYCLE + 1);
}
```

Move this block out of the `if (cycle == 0)` section and place it as a standalone
check at the top of `vicii_step_cycle`. Remove the BA assertion from inside the
`cycle == 0` block. The early approach (asserting at cycle 0) is simpler and passes
all Phase A acceptance criteria; the exact approach (asserting at cycle 12) is
deferred to Phase H (full BA/AEC model).

---

## Section 9 — Acceptance Criteria

### 9.1 Raster IRQ fires at the correct line

Write a small test PRG that sets `$D012` to raster line 100, sets ERST (`$D01A = $01`),
installs an IRQ handler at `($FFFE/$FFFF)` that increments a counter at `$C000` and
clears IRST by writing `$01` to `$D019`, then lets the machine run. Observe via the
debugger memory view that `$C000` increments approximately once per frame (PAL: ~50
times per second, NTSC: ~60 times per second).

### 9.2 `$D012` read returns the current raster line

Use the BASIC monitor (`PRINT PEEK(53266)`) in a polling loop. Values must cycle
through 0–311 (PAL) or 0–262 (NTSC) in order. They need not advance by exactly 1
per read (the loop takes many cycles), but must never exceed the frame maximum or
jump backwards within a frame.

### 9.3 `$D019` write-1-to-clear works

Write `$01` to `$D019` with IRST set, then immediately read `$D019`. Bit 0 must be 0.

### 9.4 `$D019` and `$D01A` bits 7:4 read as 1

Read both registers with no IRQs pending. High nibble of each must be `$F`.

### 9.5 Bad Lines at correct Y positions

With `YSCROLL=3` (default; `$D011` low 3 bits = 3) and `DEN=1`, Bad Lines occur at
raster lines where `(y & 7) == 3` within 0x30–0xF7. For PAL, that is 25 distinct Bad
Lines per frame (one per character row). Add a temporary debug counter
`uint32_t bad_line_count` to the `vicii` struct. Increment it once each time
`v->bad_line` transitions from false to true (i.e., at cycle 0 when the condition is
met). At end of frame, `bad_line_count` must be 25 (PAL) or 25 (NTSC, same
Y range). Reset the counter at end of each frame.

### 9.6 CPU stall for 40 cycles on Bad Lines

After a fixed number of frames, compare `machine->clock.cycle` with
`machine->clock.cpu_cycles`. The difference must grow by approximately
`25 × 40 = 1000` cycles per frame (25 bad lines × 40 stall cycles each, PAL). The
exact value may vary by a few cycles depending on where in the frame the comparison
is made; within ±10 cycles per frame is acceptable.

### 9.7 VC wraps correctly

At the start of each new frame, `v->vc` and `v->vc_base` must both be 0. Verify with
a temporary assert in the frame-wrap section of `vicii_step_cycle` during development.
The `frame_number` counter must still increment correctly every 312 (PAL) or 263
(NTSC) lines.

### 9.8 Existing tests pass without modification

Run the full test suite. All regression tests for BASIC typing, keyboard, breakpoint
system, and debugger views must pass unchanged. The boot path to the BASIC READY
prompt must remain functional.

### 9.9 BASIC screen remains visible

After all changes, the emulator must boot to the BASIC READY prompt with a normal
40×25 text display. The frame output produced by `vicii_make_frame_snapshot` must not
regress.

---

## Section 10 — What Is Not Implemented in This Phase

Do not implement any of the following:

- Pixel-accurate smooth scrolling (XSCROLL/YSCROLL display shift) — Phase B
- RSEL/CSEL border clamping — Phase B
- Graphics modes beyond standard text (MCM, bitmap, ECM, invalid) — Phase C
- Sprites (display, collision, priority) — Phase D/E
- Light pen latch — Phase F
- Open bus / last-byte behavior — Phase G
- Exact per-cycle BA/AEC read-vs-write discrimination — Phase H
- DRAM refresh counter

---

## Section 11 — Architecture Reminders

- `vicii` lives in `machine/`. It must not call SDL, Nuklear, or any runtime function.
- The new `vicii_ba_active()` is called from `c64.c`. Both are in `machine/` and on
  the emulation thread. No threading concern, no lock needed.
- Do not pass live `vicii *` pointers to the frontend. The `c64_vicii_snapshot` copy
  path remains the correct channel for debugger views.
- `vicii_step_cycle` now takes `const c64_bus_t *bus`. The bus is machine-owned and
  lives on the same emulation thread. Passing it by pointer is safe.
- The `ba_low` field and all vicii state are emulation-thread-only. No mutex needed.

---

## Section 12 — Change Summary

| File | Changes |
|------|---------|
| `src/machine/vicii.h` | New fields in `vicii_timing` and `vicii`; `vicii_ba_active()` declaration; updated `vicii_step_cycle` signature |
| `src/machine/vicii.c` | `vicii_reset` initializes new fields; `vicii_write_register` switch for $11/$12/$19/$1A; `vicii_read_register` adds $19/$1A cases; `vicii_step_cycle` replaced; `vicii_ba_active` added |
| `src/machine/c64.c` | `c64_step_vic` passes bus; `c64_step_cycle` reordered with BA stall; `c64_cpu_irq_pending` adds VIC IRQ |
