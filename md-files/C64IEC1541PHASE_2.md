# C64IEC1541PHASE_2.md — 1541 Phase 2: 1541 Machine Shell

## Required reading before proceeding

Read the following in order, all found under `md-files/` in the repository root:

1. `AGENTS.md` — agent workflow, build/test rules, architecture rules, thread ownership.
2. `MASTER.md` — component responsibility boundaries, dependency directions.
3. `STATUS.md` — current handoff routing and baseline summary.
4. `docs/status/CPU_MACHINE.md` — 6502 CPU core, bus callback model, `c6510_step()`
   signature and calling convention. Read the actual source (`src/machine/c6510.c`,
   `src/machine/c6510_inln.h`) to confirm the step function signature before use.
5. `docs/status/IEC1541.md` — current 1541 implementation status (created in Phase 1).
6. `docs/status/DISK_IO.md` — existing KERNAL LOAD trap; understand the trap
   coexistence rule before modifying `c64.c`.
7. `C64IEC1541.md` — full 1541 architecture plan.
8. This document.

After reading, **stop**. State any questions or concerns before touching any
code. Then, and only then, proceed to implementation.

---

## Prerequisites

Phase 1 must be complete:
- `src/machine/via6522.c` and `src/machine/via6522.h` exist.
- All Phase 1 VIA unit tests pass.
- `docs/status/IEC1541.md` exists.

Confirm before starting.

---

## Scope of this phase

This phase builds the **1541 machine shell**:

```
src/machine/c1541.h    — new file
src/machine/c1541.c    — new file
```

And makes the minimum changes to existing files to wire the 1541 into `c64_t`:

```
src/machine/c64.h      — add c1541 fields
src/machine/c64.c      — init/reset/destroy/step the 1541
```

**The IEC bus is not wired in this phase.** VIA #2 Port A outputs are computed
but not yet connected to `c64_set_iec_external_pull()`. That is Phase 3.

**The KERNAL LOAD trap is not disabled yet.** Trap coexistence logic is also
Phase 3, once the 1541 is actually serving files.

The deliverable for this phase is: **the 1541 ROM reaches its idle loop after
reset without crashing.** The 1541 runs in lockstep with the C64, executes its
init sequence, and sits quietly waiting for IEC activity it will not yet receive.

---

## Architecture constraints

- `c1541` belongs in `machine/`. No SDL, no Nuklear, no runtime, no platform,
  no frontend, no `tools/d64/` includes.
- The 1541 is owned by `c64_t`. It is not a top-level runtime object.
- The 1541 runs on the runtime/emulation thread, stepped from
  `c64_advance_one_cycle()`. No separate thread. No locking.
- The CPU core (`C6510`) is reused directly. `C64IECEVAL.md` confirmed this is
  safe: no global state, fully callback-driven bus, self-contained struct.

---

## 1541 memory map

Implement this exactly. Do not add complexity beyond what is listed.

| Address range | Maps to |
|---|---|
| `$0000–$07FF` | 2 KB RAM |
| `$0800–$0FFF` | 2 KB RAM mirror (`addr & 0x07FF`) |
| `$1000–$17FF` | Unmapped — return `0xFF`, ignore writes |
| `$1800–$180F` | VIA #1 registers (`addr & 0x0F` → `via6522_read/write(&via1, ...)`) |
| `$1810–$1BFF` | VIA #1 mirror — map to `$1800–$180F` (`addr & 0x0F`) |
| `$1C00–$1C0F` | VIA #2 registers (`addr & 0x0F` → `via6522_read/write(&via2, ...)`) |
| `$1C10–$1FFF` | VIA #2 mirror — map to `$1C00–$1C0F` (`addr & 0x0F`) |
| `$2000–$BFFF` | Unmapped — return `0xFF`, ignore writes |
| `$C000–$FFFF` | 16 KB ROM (`addr - 0xC000` into `rom[16384]`) |

The VIA mirroring is important: the 1541 ROM accesses VIAs through mirror
addresses during init. Without correct mirroring the ROM will malfunction.

---

## `c1541` struct

```c
// src/machine/c1541.h

#ifndef C1541_H
#define C1541_H

#include "c6510.h"
#include "via6522.h"

// Forward declaration — c1541 holds a back-pointer to c64_t for Phase 3
// IEC bus wiring. Declare c64_t as incomplete type here; do not include c64.h
// from c1541.h (would create a circular include). c64.h includes c1541.h.
typedef struct c64 c64_t;

#define C1541_ROM_SIZE  16384
#define C1541_RAM_SIZE  2048

typedef struct c1541 {
    C6510    cpu;
    via6522  via1;
    via6522  via2;
    uint8_t  ram[C1541_RAM_SIZE];
    uint8_t  rom[C1541_ROM_SIZE];
    int      rom_loaded;        // 1 if ROM was loaded successfully
    c64_t   *c64;               // back-pointer (used in Phase 3 only)
    int      device_number;     // 8 or 9
} c1541;

void    c1541_init(c1541 *drive, c64_t *c64, int device_number);
void    c1541_destroy(c1541 *drive);
void    c1541_reset(c1541 *drive);
int     c1541_load_rom(c1541 *drive, const char *path);
void    c1541_advance_one_cycle(c1541 *drive);

#endif // C1541_H
```

Note: ROM is stored inline in the struct (fixed 16 KB array), not heap-allocated.
This keeps ownership simple and avoids a malloc/free pair. If struct size is a
concern in context, discuss with the human before switching to heap allocation.

---

## Bus callbacks

These are `static` functions in `c1541.c`, not exposed in the header:

```c
static uint8_t c1541_bus_read(void *user, uint16_t addr) {
    c1541 *drive = (c1541 *)user;
    if (addr < 0x0800) return drive->ram[addr];
    if (addr < 0x1000) return drive->ram[addr & 0x07FF];
    if (addr >= 0x1800 && addr < 0x2000) return via6522_read(&drive->via1, addr & 0x0F);
    if (addr >= 0x1C00 && addr < 0x2000) return via6522_read(&drive->via2, addr & 0x0F);
    if (addr >= 0xC000) return drive->rom[addr - 0xC000];
    return 0xFF;
}
```

**Important:** the VIA #1 and VIA #2 ranges overlap in the above sketch —
`$1C00–$1FFF` is a subset of `$1800–$1FFF`. Check the range tests in order:
check VIA #2 (`$1C00+`) before VIA #1 (`$1800+`) so VIA #2 is not shadowed.
Or use non-overlapping ranges as in the memory map table above. Be precise.

```c
static void c1541_bus_write(void *user, uint16_t addr, uint8_t value) {
    c1541 *drive = (c1541 *)user;
    if (addr < 0x0800) { drive->ram[addr] = value; return; }
    if (addr < 0x1000) { drive->ram[addr & 0x07FF] = value; return; }
    if (addr >= 0x1C00 && addr < 0x2000) { via6522_write(&drive->via2, addr & 0x0F, value); return; }
    if (addr >= 0x1800 && addr < 0x2000) { via6522_write(&drive->via1, addr & 0x0F, value); return; }
    // ROM and unmapped: ignore
}
```

---

## `c1541_init()`

```c
void c1541_init(c1541 *drive, c64_t *c64, int device_number) {
    memset(drive, 0, sizeof(c1541));
    drive->c64 = c64;
    drive->device_number = device_number;
    drive->rom_loaded = 0;
    via6522_init(&drive->via1);
    via6522_init(&drive->via2);
    // CPU init: use the existing c6510_init() pattern from c64.c.
    // Pass c1541_bus_read, c1541_bus_write, and drive as the user pointer.
    // Set CPU class to CPU_6502 (plain 6502, no 6510 I/O port).
    // Confirm exact c6510_init() signature from src/machine/c6510.c before use.
    c6510_init(&drive->cpu, CPU_6502, c1541_bus_read, c1541_bus_write, drive);
}
```

Confirm the exact `c6510_init()` signature and `CPU_6502` constant name from the
source before writing this call. Do not guess.

---

## `c1541_load_rom()`

The 1541 ROM exists in two physical forms:

**Combined (preferred):** a single 16 KB file covering `$C000–$FFFF`, as
distributed by VICE (`dos1541`) and most emulator ROM packs. This is what
`c1541_load_rom()` expects by default.

**Split (Zimmers.net distribution):** two separate 8 KB files:
- `1541-c000.325302-01.bin` — lower half (`$C000–$DFFF`)
- `1541-e000.901229-06AA.bin` — upper half (`$E000–$FFFF`)

If the user supplies one of the split files, `c1541_load_rom()` will reject it
(wrong size). Provide a second function `c1541_load_rom_split()` so the user
can supply both halves separately.

```c
// Load a single combined 16 KB ROM image ($C000–$FFFF).
int c1541_load_rom(c1541 *drive, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(drive->rom, 1, C1541_ROM_SIZE, f);
    fclose(f);
    if (n != C1541_ROM_SIZE) {
        memset(drive->rom, 0, C1541_ROM_SIZE);
        return 0;
    }
    drive->rom_loaded = 1;
    return 1;
}

// Load two split 8 KB ROM halves and combine them.
// path_lo: the $C000–$DFFF half (1541-c000.325302-01.bin)
// path_hi: the $E000–$FFFF half (1541-e000.901229-06AA.bin)
// Combined order: lo first, then hi — same as:
//   cat 1541-c000.325302-01.bin 1541-e000.901229-06AA.bin > 1541.rom
int c1541_load_rom_split(c1541 *drive, const char *path_lo, const char *path_hi) {
    FILE *f;
    size_t n;

    f = fopen(path_lo, "rb");
    if (!f) return 0;
    n = fread(drive->rom, 1, C1541_ROM_SIZE / 2, f);
    fclose(f);
    if (n != C1541_ROM_SIZE / 2) { memset(drive->rom, 0, C1541_ROM_SIZE); return 0; }

    f = fopen(path_hi, "rb");
    if (!f) { memset(drive->rom, 0, C1541_ROM_SIZE); return 0; }
    n = fread(drive->rom + C1541_ROM_SIZE / 2, 1, C1541_ROM_SIZE / 2, f);
    fclose(f);
    if (n != C1541_ROM_SIZE / 2) { memset(drive->rom, 0, C1541_ROM_SIZE); return 0; }

    drive->rom_loaded = 1;
    return 1;
}
```

Declare both functions in `c1541.h`.

### ROM keys in the INI file

The 1541 ROM is specified in the `[roms]` section of the INI file, following
the same pattern as the existing `character` and `system` keys:

```ini
[roms]
character=roms/character.rom
system=roms/system.rom
1541=roms/1541.rom
```

The `1541` key accepts a combined 16 KB image (`$C000–$FFFF`). This is the
preferred form — Stefan has already combined the split files and placed the
result at `roms/1541.rom`.

Optionally, also support two split 8 KB keys for users who have the Zimmers.net
files directly:

```ini
1541.c000=roms/1541-c000.325302-01.bin
1541.e000=roms/1541-e000.901229-06AA.bin
```

If `1541` is present, load it with `c1541_load_rom()` and ignore the split keys.
If `1541` is absent but both split keys are present, load with
`c1541_load_rom_split()`. If none of the keys are present (or the files are
missing), log a message at the same verbosity level used for missing C64 ROMs
and continue with `rom_loaded = 0` (fallback to KERNAL trap). A missing 1541
ROM is not a fatal error.

Follow the exact INI parsing pattern used for `character` and `system` — do not
invent a new parsing mechanism.

---

## `c1541_reset()`

```c
void c1541_reset(c1541 *drive) {
    memset(drive->ram, 0, C1541_RAM_SIZE);
    via6522_reset(&drive->via1);
    via6522_reset(&drive->via2);
    // Reset CPU using the same reset mechanism as the C64 CPU.
    // Confirm the exact reset function/macro from c6510.c before use.
    c6510_reset(&drive->cpu);
    // The 1541 CPU reset vector is at $FFFC/$FFFD in the ROM.
    // c6510_reset() should read the reset vector through the bus callbacks,
    // which will return ROM bytes at those addresses. Confirm this is how
    // the C64 CPU reset works before assuming it applies here.
}
```

---

## `c1541_advance_one_cycle()`

```c
void c1541_advance_one_cycle(c1541 *drive) {
    if (!drive->rom_loaded) return;

    // Step VIAs before CPU (they may set IRQ/NMI lines the CPU samples)
    via6522_step(&drive->via1);
    via6522_step(&drive->via2);

    // Determine IRQ: asserted if either VIA has pending + enabled interrupts
    int irq = via6522_irq_pending(&drive->via1) ||
              via6522_irq_pending(&drive->via2);

    // NMI: CA1 on VIA #2 (ATN line). IFR bit 1 set = NMI pending.
    // In Phase 2 ATN is not yet wired, so NMI will always be 0 here.
    int nmi = (drive->via2.ifr & drive->via2.ier & 0x02) != 0;

    // Step the 6502 one cycle.
    // Confirm exact c6510_step() signature from src/machine/c6510.c.
    // The C64 uses this same function — find the call site in c64_advance_one_cycle()
    // and mirror the calling convention exactly.
    c6510_step(&drive->cpu, irq, nmi);

    // Phase 3 will add: c1541_update_iec_bus(drive) here.
}
```

---

## `c1541_destroy()`

```c
void c1541_destroy(c1541 *drive) {
    // Nothing to free in this phase (ROM is inline array, not heap).
    // Zero the struct for safety.
    memset(drive, 0, sizeof(c1541));
}
```

---

## Changes to `c64.h`

Add to `c64_t` struct (follow the existing pattern for how `cia`, `vicii`, `sid`
are embedded — inline struct or pointer, whichever is consistent):

```c
c1541  drive8;
c1541  drive9;
```

Include `c1541.h` from `c64.h`. Since `c1541.h` forward-declares `c64_t` rather
than including `c64.h`, there is no circular include.

---

## Changes to `c64.c`

### ROM path

Follow the existing C64 ROM loading convention. See the `c1541_load_rom()` /
`c1541_load_rom_split()` section above for the full search order (combined
files first, then Zimmers.net split pair). Implement `c64_find_1541_rom()`
following the same directory search pattern used for C64 ROMs.

### In `c64_init()`

Read the `1541` key from the `[roms]` INI section using the same accessor used
for `character` and `system`. If present, call `c1541_load_rom()`. If absent,
check for the `1541.c000` and `1541.e000` split keys and call
`c1541_load_rom_split()` if both are present. Both drives (8 and 9) use the
same ROM image:

```c
c1541_init(&machine->drive8, machine, 8);
c1541_init(&machine->drive9, machine, 9);

const char *rom1541 = ini_get(config, "roms", "1541");
if (rom1541) {
    c1541_load_rom(&machine->drive8, rom1541);
    c1541_load_rom(&machine->drive9, rom1541);
} else {
    const char *lo = ini_get(config, "roms", "1541.c000");
    const char *hi = ini_get(config, "roms", "1541.e000");
    if (lo && hi) {
        c1541_load_rom_split(&machine->drive8, lo, hi);
        c1541_load_rom_split(&machine->drive9, lo, hi);
    }
}
// If neither key is present: rom_loaded remains 0; KERNAL trap stays active.
```

Confirm the exact INI accessor name and signature from the existing `c64_init()`
code before writing this. Use whatever function loads the `character` and
`system` paths — do not invent a new one.

### In `c64_reset()`

```c
c1541_reset(&machine->drive8);
c1541_reset(&machine->drive9);
```

### In `c64_destroy()`

```c
c1541_destroy(&machine->drive8);
c1541_destroy(&machine->drive9);
```

### In `c64_advance_one_cycle()`

Add after the existing CIA/VIC/SID/CPU steps. Follow the exact ordering used
for other peripheral steps. Confirm the existing step order from source before
inserting:

```c
c1541_advance_one_cycle(&machine->drive8);
c1541_advance_one_cycle(&machine->drive9);
```

**Do not change any other behaviour in `c64_advance_one_cycle()`.** Do not
move, reorder, or remove existing steps.

---

## Verifying the deliverable

The phase is complete when the 1541 ROM reaches its idle loop. To verify:

Add a **temporary** debug log in `c1541_advance_one_cycle()` (gated on a debug
flag, not unconditional) that prints the 1541 CPU's PC every N cycles. After
reset, the 1541 ROM should:

1. Execute the reset vector handler (typically starting around `$EAA0` in the
   standard 1541 ROM).
2. Initialize VIA #1 and VIA #2 registers.
3. Enter the main ATN polling loop (typically around `$EB3B` or similar —
   confirm against the 1541 ROM disassembly).

If the ROM crashes (PC goes to an unexpected address, or the CPU executes a
`BRK` / `KIL` opcode, or the PC stops changing), the most likely causes are:
- Incorrect memory map (ROM not visible at `$C000`, or wrong VIA address range).
- Incorrect reset vector read (the CPU reset must read `$FFFC/$FFFD` through the
  bus callbacks, which return ROM bytes).
- VIA register side-effects during init that are not implemented correctly.

Use the annotated 1541 ROM disassembly at `github.com/mist64/1541rom` to trace
what the ROM is doing at the address where the crash occurs. This is the
authoritative reference for 1541 ROM behaviour.

---

## Tests to add

Add to `tests/machine/test_c1541.c` (new file):

- `c1541_init()` produces correct initial state: `rom_loaded = 0`, VIAs zeroed,
  device number stored.
- `c1541_load_rom()` with a correctly-sized buffer sets `rom_loaded = 1` and
  populates `drive->rom`.
- `c1541_load_rom()` with wrong-sized buffer leaves `rom_loaded = 0`.
- `c1541_bus_read()` returns RAM at `$0000`, RAM mirror at `$0800`, `0xFF` at
  `$2000`, ROM at `$C000`, ROM at `$FFFC` (reset vector address).
- `c1541_bus_write()` to RAM is readable back. Write to ROM address is ignored
  (ROM reads back original value).
- `c1541_advance_one_cycle()` with `rom_loaded = 0` is a no-op (CPU PC unchanged).

Human smoke check:
- Launch c64m with a 1541 ROM present. Confirm no crash on startup.
- Add the temporary PC trace log. Confirm the 1541 ROM reaches its idle loop
  within a few thousand cycles after reset.
- Confirm all existing C64 smoke tests still pass (KERNAL trap still active —
  trap coexistence is Phase 3).

---

## Acceptance criteria

- `c1541_advance_one_cycle()` steps CPU and VIAs without crashing.
- 1541 ROM reaches its idle loop after reset (verified via temporary PC trace).
- All existing project tests pass — no regressions to C64 behaviour.
- `c1541.c` and `c1541.h` contain no SDL, Nuklear, runtime, platform, frontend,
  or `tools/d64/` includes.
- No global or static mutable state in `c1541.c`.

---

## Status document updates after this phase

- `docs/status/IEC1541.md`: update to reflect Phase 2 complete. Add:

```markdown
## Current implementation

- VIA 6522 module implemented and unit-tested (Phase 1).
- 1541 machine shell implemented (Phase 2): CPU, VIAs, memory map, ROM loading,
  cycle-step integration. 1541 ROM reaches idle loop after reset.
- IEC bus wiring: not yet started (Phase 3).
- KERNAL LOAD trap coexistence: not yet implemented (Phase 3).

## ROM
- 1541 ROM loaded from INI key `[roms] 1541` (combined 16 KB, e.g. `roms/1541.rom`).
- Split-file fallback via `[roms] 1541.c000` + `[roms] 1541.e000` (8 KB each).
- Falls back to KERNAL trap if no key is present or file is missing.
```

- `STATUS.md`: add a one-line note that 1541 Phase 2 is complete (shell running,
  IEC not yet wired).

---

## What comes next

Phase 3 wires the IEC bus: connects VIA #2 Port A outputs to
`c64_set_iec_external_pull()`, feeds C64 IEC line state back into VIA #2 input
pins, implements the CA1/ATN NMI path, implements D64 sector access from the
1541 ROM's perspective, and disables the KERNAL LOAD trap when the 1541 ROM is
present. After Phase 3, standard D64 loads work via the genuine 1541 ROM, and
fast loaders work automatically because the ROM executes the uploaded drive-side
code natively.
