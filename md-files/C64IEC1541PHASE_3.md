# C64IEC1541PHASE_3.md — 1541 Phase 3: IEC Bus Wiring and D64 Sector Access

## Required reading before proceeding

Read the following in order, all found under `md-files/` in the repository root:

1. `AGENTS.md` — agent workflow, build/test rules, architecture rules, thread ownership.
2. `MASTER.md` — component responsibility boundaries, dependency directions.
3. `STATUS.md` — current handoff routing and baseline summary.
4. `docs/status/CIA.md` — CIA #2 IEC pin model, `iec_external_pull`, `c64_set_iec_external_pull()`.
5. `docs/status/DISK_IO.md` — D64 backend, KERNAL LOAD trap, trap coexistence rule.
6. `docs/status/IEC1541.md` — current 1541 status (updated through Phase 2).
7. `C64IEC1541.md` — full 1541 architecture plan.
8. This document.

Additionally, before writing any code for the IEC bus or D64 sector intercept,
read the following external references:

- **1541 ROM disassembly:** `github.com/mist64/1541rom` — the annotated
  disassembly of the standard 1541 DOS ROM. You will need this to identify
  the exact addresses for the sector read intercept points and to verify the
  VIA #2 port initialisation sequence.
- **cbmbus_doc:** `github.com/mist64/cbmbus_doc` — layer-by-layer IEC protocol
  documentation including exact timing windows and line state sequences.

After reading all of the above, **stop**. State any questions or concerns before
touching any code. Then, and only then, proceed to implementation.

---

## Prerequisites

Phase 2 must be complete:
- `src/machine/c1541.c`, `c1541.h`, `via6522.c`, `via6522.h` all exist.
- 1541 ROM reaches its idle loop after reset without crashing.
- All Phase 1 and Phase 2 tests pass.
- `docs/status/IEC1541.md` reflects Phase 2 status.

Confirm before starting.

---

## Scope of this phase

This phase completes the 1541 emulator by wiring it to the real world:

1. **IEC bus wiring** — VIA #2 Port A ↔ `c64_set_iec_external_pull()` ↔ CIA #2.
2. **CA1 / ATN NMI** — ATN line asserted by C64 triggers NMI in the 1541 CPU.
3. **D64 sector access** — 1541 ROM's sector read requests are satisfied from
   the mounted D64 image via ROM address intercept.
4. **KERNAL trap coexistence** — disable the existing KERNAL LOAD trap for
   devices whose 1541 ROM is loaded; retain it as fallback when ROM is absent.

After this phase, standard C64 disk loads work via the genuine 1541 ROM, fast
loaders work automatically (the ROM executes uploaded drive-side code natively),
and the existing KERNAL trap remains as the no-ROM fallback.

---

## Part A — IEC bus wiring

### VIA #2 port assignment

The 1541 VIA #2 Port A bits connect to the IEC bus as follows. Verify this
against the 1541 ROM disassembly's port initialisation sequence (`$EAA0` area)
before writing code — ROM initialisation will set DDRA to reflect this exact
assignment.

| VIA #2 Port A bit | Direction | IEC signal |
|---|---|---|
| 0 | Output | DATA out (1541 drives DATA low when bit 0 = 0 and DDRA bit 0 = 1) |
| 1 | Output | CLK out (1541 drives CLK low when bit 1 = 0 and DDRA bit 1 = 1) |
| 2 | Input | DATA in (reads 0 when bus DATA is low) |
| 3 | Input | CLK in (reads 0 when bus CLK is low) |
| 4 | Input | ATN in (reads 0 when bus ATN is low, i.e. C64 asserting ATN) |
| 5 | Input | Device address bit (hardware jumper; tie to 1 for device 8, 0 for device 9 — check ROM) |
| 6 | (unused) | — |
| 7 | Output | ATN acknowledge (1541 pulls ATN low when bit 7 = 0 and DDRA bit 7 = 1) |

**Polarity note:** On the IEC bus, all signals are active-low (open-collector).
"Asserted" means the line is pulled low. A VIA output bit of 0 (with DDRA=1)
means the 1541 is pulling that line low. A VIA input bit of 0 means the bus
line is currently low (being pulled by C64 or another device).

Verify these assignments against the ROM disassembly before trusting this table.
The ROM's VIA #2 DDRA init write tells you exactly which bits are outputs.

### `c1541_update_iec_bus()`

Add this function to `c1541.c` (static, not exposed in header). Call it from
`c1541_advance_one_cycle()` after `via6522_step()` calls and before
`c6510_step()`:

```c
static void c1541_update_iec_bus(c1541 *drive) {
    // Step 1: Compute what the 1541 is asserting on the IEC bus.
    // An output pin asserts its line when: DDRA bit = 1 (output) AND ORA bit = 0.
    uint8_t ddra2 = drive->via2.ddra;
    uint8_t ora2  = drive->via2.ora;

    uint8_t drive_pull = 0;
    if ((ddra2 & 0x01) && !(ora2 & 0x01)) drive_pull |= C64_IEC_DATA;  // bit 0: DATA out
    if ((ddra2 & 0x02) && !(ora2 & 0x02)) drive_pull |= C64_IEC_CLK;   // bit 1: CLK out
    if ((ddra2 & 0x80) && !(ora2 & 0x80)) drive_pull |= C64_IEC_ATN;   // bit 7: ATN ack

    // Step 2: Update the C64's view of what the drive is pulling.
    c64_set_iec_external_pull(drive->c64, drive_pull);

    // Step 3: Read the combined bus state.
    // The C64's CIA #2 open-collector model computes the combined bus state
    // in c64_cia2_port_inputs(). We need to read the current C64-side output
    // for each IEC line and combine with the drive's own output (open-collector:
    // any puller wins).
    //
    // Read the C64's CIA #2 Port A and DDRA to determine what the C64 is
    // asserting. Use whatever accessor is available on c64_t — do NOT call
    // CIA register read functions that have side effects. Use a safe peek or
    // read cached state. Confirm the correct accessor from c64.h / c64.c.
    //
    // For each line: bus_line_low = (c64_asserts_line) || (drive_asserts_line)
    uint8_t c64_pull = c64_get_iec_c64_pull(drive->c64); // implement this accessor
    uint8_t bus_low  = drive_pull | c64_pull; // open-collector: either puller wins

    uint8_t data_low = (bus_low & C64_IEC_DATA) != 0;
    uint8_t clk_low  = (bus_low & C64_IEC_CLK)  != 0;
    uint8_t atn_low  = (bus_low & C64_IEC_ATN)  != 0;

    // Step 4: Feed bus state into VIA #2 input pins.
    // Input bits: 0 = bus line asserted (low); 1 = bus line released (high).
    uint8_t port_a_in = drive->via2.port_a_in; // preserve non-IEC bits
    if (data_low) port_a_in &= ~0x04; else port_a_in |= 0x04; // bit 2: DATA in
    if (clk_low)  port_a_in &= ~0x08; else port_a_in |= 0x08; // bit 3: CLK in
    if (atn_low)  port_a_in &= ~0x10; else port_a_in |= 0x10; // bit 4: ATN in
    via6522_set_port_a_inputs(&drive->via2, port_a_in);

    // Step 5: Feed ATN to CA1 for NMI detection.
    // CA1 is connected to ATN. When ATN goes low (atn_low=1), CA1 goes low.
    // The ROM configures CA1 for negative-edge interrupt (PCR bit 0 = 0).
    via6522_set_ca1(&drive->via2, atn_low ? 0 : 1);
}
```

### `c64_get_iec_c64_pull()` — new accessor

Add to `c64.h` (declaration) and `c64.c` (implementation):

```c
// Returns a bitmask (using C64_IEC_* flags) of IEC lines the C64 is currently
// asserting (pulling low) via CIA #2 Port A. Used by c1541 to compute combined
// bus state. Does not have CIA register read side effects.
uint8_t c64_get_iec_c64_pull(c64_t *machine);
```

Implementation: read CIA #2 Port A and DDRA via safe peek (not through the
CPU-visible bus read path). An output pin is asserting its line when DDRA bit = 1
and PA bit = 0. Apply the C64's IEC bit assignments (bit 3 = ATN, bit 4 = CLK,
bit 5 = DATA — verify from the existing `c64_cia2_port_inputs()` code before
implementing). Return the corresponding `C64_IEC_*` flags.

---

## Part B — CA1 / ATN NMI

The 1541 ROM uses CA1 on VIA #2 as an NMI source to interrupt whatever it is
doing when the C64 asserts ATN. This is already handled by:

1. `c1541_update_iec_bus()` calling `via6522_set_ca1()` when ATN changes.
2. `via6522_set_ca1()` setting IFR bit 1 on the active edge.
3. `c1541_advance_one_cycle()` computing `nmi` from VIA #2 IFR bit 1 (with IER
   bit 1 enabled — the ROM enables this during init).

Verify that the ROM's VIA #2 init sequence enables IER bit 1 (CA1 interrupt
enable) by checking the ROM disassembly. If it does not (e.g. if the ROM uses
a different NMI mechanism), adjust accordingly and document the finding.

The `nmi` computation in `c1541_advance_one_cycle()` (currently stubbed as 0
in Phase 2) becomes:

```c
int nmi = (drive->via2.ifr & drive->via2.ier & 0x02) != 0;
```

This replaces the Phase 2 stub. No other changes to the cycle-step function.

---

## Part C — D64 sector access

### The problem

The 1541 ROM accesses disk sectors through VIA #1 (the parallel port connected
to the real disk drive's read head and stepper motor). In the emulator, VIA #1
is a stub. The ROM's sector read loop must be intercepted and satisfied from the
mounted D64 image instead.

### The intercept approach

Rather than fully emulating disk mechanics, intercept at specific 1541 ROM
addresses where the ROM expects a sector to be available in its buffer. This is
analogous to the existing C64 KERNAL LOAD trap.

**Target ROM version:** the standard 1541 DOS 2.6 ROM, combined from two chips
in address order — lower half first, then upper half:

- `1541-c000.325302-01.bin` — `$C000–$DFFF` (8 KB, lower half)
- `1541-e000.901229-06AA.bin` — `$E000–$FFFF` (8 KB, upper half)

Combined: `cat 1541-c000.325302-01.bin 1541-e000.901229-06AA.bin > 1541.rom`

This produces a 16 KB image where bytes 0–8191 map to `$C000` and bytes
8192–16383 map to `$E000`. Stefan has already done this and placed the result
at `roms/1541.rom`. The mist64 `dos1541` repository targets this exact
combination at commit `bd9aae0` (the 901229-06 branch). All intercept addresses
below are for this version.

**Before writing any code, read the 1541 ROM disassembly** at
`github.com/mist64/1541rom` (branch/commit `bd9aae0`, the 901229-06 version)
to identify the following:

1. The address where the ROM has finished reading a sector into its RAM buffer
   (typically after `$D586` or the GCR decode loop — the exact address depends
   on ROM version).
2. The RAM address of the 1541's sector buffer (typically `$0300` in standard
   1541 ROM — confirm from disassembly).
3. The RAM addresses where the ROM stores the requested track and sector numbers
   before initiating a read (typically around `$0006/$0007` — confirm).

Document these addresses as named constants in `c1541.c`:

```c
// Standard 1541 ROM (DOS 2.6: 325302-01 + 901229-06AA combined).
// Intercept addresses verified against: github.com/mist64/1541rom bd9aae0
// (the 901229-06 version). If a different ROM is used, re-verify these.
//
// To find C1541_ROM_SECTOR_READ_ENTRY: in the disassembly, search for the
// GCR decode / sector-read-complete routine. The entry point is the address
// the ROM jumps to when it initiates a physical sector read. The return
// address is where execution resumes after the sector data is in the buffer.
//
// To find track/sector RAM locations: search the disassembly for writes to
// the track and sector variables before the sector read is initiated.
// These are typically in zero-page or low RAM.
#define C1541_ROM_SECTOR_READ_ENTRY   0x0000  // FILL IN from disassembly
#define C1541_ROM_SECTOR_READ_RETURN  0x0000  // FILL IN from disassembly
#define C1541_RAM_TRACK_REG           0x0000  // FILL IN from disassembly
#define C1541_RAM_SECTOR_REG          0x0000  // FILL IN from disassembly
#define C1541_RAM_SECTOR_BUF          0x0300  // standard; confirm from disassembly
#define C1541_RAM_STATUS              0x0000  // FILL IN from disassembly
#define C1541_STATUS_OK               0x00    // FILL IN: ROM's success value
#define C1541_STATUS_NO_DISK          0x00    // FILL IN: ROM's no-disk value
#define C1541_STATUS_READ_ERROR       0x00    // FILL IN: ROM's read-error value
```

Do not leave any `0x0000` placeholders in committed code. Every constant must
be filled in from the disassembly before the phase is considered complete.

### The intercept implementation

**Before writing the intercept check**, confirm the exact field name for the
program counter in the `C6510` struct. Read `src/machine/c6510.h` and find
the PC field. It is likely `drive->cpu.pc` or `drive->cpu.regs.pc` — do not
assume `drive->cpu.cpu.pc` (that would imply a doubly-nested struct). Use
whatever the actual field is, and add a comment citing the struct definition:

```c
// PC field confirmed from src/machine/c6510.h: cpu.<field>
// Replace <field> with the actual PC field name before compiling.
if (drive->rom_loaded && drive->cpu.<field> == C1541_ROM_SECTOR_READ_ENTRY) {
    c1541_satisfy_sector_read(drive);
}
```

The same field name applies everywhere PC is read or written in `c1541.c`,
including the `C1541_ROM_SECTOR_READ_RETURN` assignment in
`c1541_satisfy_sector_read()`.

```c
static void c1541_satisfy_sector_read(c1541 *drive) {
    // Read requested track and sector from 1541 RAM
    uint8_t track  = drive->ram[C1541_RAM_TRACK_REG];
    uint8_t sector = drive->ram[C1541_RAM_SECTOR_REG];

    // Get the mounted D64 image for this device.
    // Access via drive->c64 back-pointer and the existing c64_drive_slot
    // structure. Use whichever field gives access to the raw D64 image bytes.
    // Do NOT call any function that triggers a KERNAL trap or has runtime side
    // effects. Read the image bytes directly.
    const c64_drive_slot *slot = c64_get_drive_slot(drive->c64, drive->device_number);
    if (!slot || !slot->image_bytes || !slot->image_size) {
        // No disk mounted: set the 1541's error status and return.
        // The ROM checks a status byte after the read; set it to indicate
        // "no disk" or "drive not ready". Confirm the status byte address
        // from the ROM disassembly.
        drive->ram[C1541_RAM_STATUS] = C1541_STATUS_NO_DISK;
        return;
    }

    // Compute the byte offset of (track, sector) in the D64 image.
    // Use the standard D64 sector offset calculation.
    // Track numbering: 1-based. Sector numbering: 0-based.
    // The sector size in D64 is always 256 bytes.
    int offset = d64_sector_offset(track, sector); // implement inline (see below)
    if (offset < 0 || offset + 256 > (int)slot->image_size) {
        drive->ram[C1541_RAM_STATUS] = C1541_STATUS_READ_ERROR;
        return;
    }

    // Copy 256 bytes into the 1541's sector buffer.
    memcpy(&drive->ram[C1541_RAM_SECTOR_BUF], slot->image_bytes + offset, 256);

    // Set success status.
    drive->ram[C1541_RAM_STATUS] = C1541_STATUS_OK;

    // Advance the 1541 CPU's PC past the read routine to the point where
    // the ROM would resume after a successful sector read.
    // Confirm this resume address from the disassembly.
    drive->cpu.cpu.pc = C1541_ROM_SECTOR_READ_RETURN;
}
```

### D64 sector offset calculation

The D64 format uses variable sectors-per-track. Implement this as a static
inline function in `c1541.c` — do not include `tools/d64/d64.h`:

```c
// Returns the byte offset of (track, sector) in a standard 35-track D64 image.
// Returns -1 if track/sector is out of range.
// Track is 1-based; sector is 0-based.
static int d64_sector_offset(uint8_t track, uint8_t sector) {
    // Standard D64 sector counts per track zone:
    // Tracks  1-17: 21 sectors each
    // Tracks 18-24: 19 sectors each
    // Tracks 25-30: 18 sectors each
    // Tracks 31-35: 17 sectors each
    static const uint8_t sectors_per_track[36] = {
        0,                          // track 0 (unused)
        21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, // 1-17
        19,19,19,19,19,19,19,       // 18-24
        18,18,18,18,18,18,          // 25-30
        17,17,17,17,17              // 31-35
    };
    if (track < 1 || track > 35) return -1;
    if (sector >= sectors_per_track[track]) return -1;
    int offset = 0;
    for (int t = 1; t < track; t++) offset += sectors_per_track[t] * 256;
    offset += sector * 256;
    return offset;
}
```

This table is identical to what `src/tools/d64/d64.c` uses internally. It is
small enough to duplicate cleanly rather than creating a cross-layer dependency.

### `c64_get_drive_slot()` — new accessor

Add to `c64.h` (declaration) and `c64.c` (implementation):

```c
// Returns a pointer to the drive slot for the given device number (8 or 9).
// Returns NULL if device_number is out of range.
// The returned pointer is valid as long as the c64_t is alive.
// Do not call this from any thread other than the runtime thread.
const c64_drive_slot *c64_get_drive_slot(c64_t *machine, int device_number);
```

---

## Part D — KERNAL LOAD trap coexistence

The existing KERNAL LOAD trap at `$FFD5` must be disabled for devices whose
1541 ROM is loaded and running **and** the 1541 ROM path is enabled by the
user. When the ROM is present but the toggle is off, the KERNAL trap fires as
normal — the 1541 runs silently in lockstep but does not serve files.

### Config flag

Add a boolean to the machine config struct (follow the existing pattern for
other `[machine]` or `[disk]` INI booleans):

```c
int emulate_1541;   // 1 = route disk I/O through genuine 1541 ROM; 0 = KERNAL trap
```

Expose it in the INI file under `[disk]`:

```ini
[disk]
emulate_1541=1
```

Default: `1` if the 1541 ROM file was loaded successfully, `0` if it was not.
Follow the exact INI read pattern used for other boolean config keys — do not
invent a new mechanism.

The flag must also be exposed in the frontend so the user can toggle it at
runtime without restarting. Add it alongside the existing disk UI controls.
When the flag changes at runtime, call `c64_apply_config()` (or whatever the
existing config-apply path is) so the change takes effect immediately.

### Where the trap is checked

The trap fires in `c64_try_kernal_load_trap()` (in `c64.c`). It checks the
device number (`$BA`) and rejects unsupported devices. Find this function and
read it fully before modifying it.

### The change

In `c64_try_kernal_load_trap()`, before processing a load for device 8 or 9,
add:

```c
// If the 1541 ROM is loaded and enabled for this device, do not intercept —
// let the real 1541 ROM handle it via IEC.
if (device == 8 && machine->drive8.rom_loaded && machine->config.emulate_1541)
    return 0; // 0 = trap not taken
if (device == 9 && machine->drive9.rom_loaded && machine->config.emulate_1541)
    return 0;
```

Place this check after the device number is read from `$BA` and validated, but
before any D64 or file access. Do not change any other logic in the trap.

When `rom_loaded = 0` or `emulate_1541 = 0` for a device, the trap fires
exactly as before. This preserves full backward compatibility for users without
a 1541 ROM file, and gives users with a ROM file the option to fall back to the
KERNAL trap path without removing the ROM.

---

## Putting it together: `c1541_advance_one_cycle()` final form

```c
void c1541_advance_one_cycle(c1541 *drive) {
    if (!drive->rom_loaded) return;

    // 1. Step VIAs (may set interrupt flags before CPU samples them)
    via6522_step(&drive->via1);
    via6522_step(&drive->via2);

    // 2. Synchronise IEC bus (VIA #2 outputs → c64 pull; C64 state → VIA #2 inputs + CA1)
    c1541_update_iec_bus(drive);

    // 3. Intercept sector read requests from the ROM
    // (Replace .pc with the actual PC field name confirmed from c6510.h)
    if (drive->cpu.<pc_field> == C1541_ROM_SECTOR_READ_ENTRY) {
        c1541_satisfy_sector_read(drive);
    }

    // 4. Compute interrupt lines
    int irq = via6522_irq_pending(&drive->via1) ||
              via6522_irq_pending(&drive->via2);
    int nmi = (drive->via2.ifr & drive->via2.ier & 0x02) != 0;

    // 5. Step the 6502
    c6510_step(&drive->cpu, irq, nmi);
}
```

---

## Tests to add

Add to `tests/machine/test_c1541.c`:

**IEC bus wiring:**
- With C64 asserting ATN (`C64_IEC_ATN` in `c64_t.iec_external_pull` or CIA #2
  state): verify `c1541_update_iec_bus()` feeds ATN low into VIA #2 Port A bit 4
  and calls `via6522_set_ca1()` with level 0.
- With 1541 asserting DATA (VIA #2 ORA bit 0 = 0, DDRA bit 0 = 1): verify
  `c64_t.iec_external_pull` has `C64_IEC_DATA` set after `c1541_update_iec_bus()`.

**D64 sector access:**
- Construct a minimal synthetic D64 image in a test buffer. Set track/sector in
  1541 RAM. Call `c1541_satisfy_sector_read()`. Verify correct 256 bytes appear
  in the 1541's sector buffer.
- Out-of-range track: verify error status set, no buffer overrun.
- No disk mounted: verify error status set.

**KERNAL trap coexistence:**
- With `drive8.rom_loaded = 1`: confirm trap returns without action for device 8.
- With `drive8.rom_loaded = 0`: confirm trap fires normally for device 8.

**Human smoke checks:**
- With 1541 ROM absent: all existing KERNAL trap smoke checks pass unchanged.
- With 1541 ROM present: mount a standard D64. Type `LOAD "*",8,1` then `RUN`.
  Confirm program loads via 1541 ROM (not KERNAL trap — can verify by temporarily
  logging which path fires).
- With 1541 ROM present: `LOAD "$",8` — directory listing loads.
- With 1541 ROM present: mount Galencia D64 and run. Confirm Galencia reaches
  the game screen (secondary load via fast loader now works because the 1541 ROM
  executes the uploaded drive-side code natively).

---

## Acceptance criteria

- Standard D64 file loads work via the genuine 1541 ROM when ROM is present.
- Galencia (or equivalent modern title using a fast loader) loads and reaches
  the game screen.
- When 1541 ROM is absent, all existing KERNAL trap smoke checks pass unchanged.
- All Phase 1 and Phase 2 tests continue to pass.
- All existing C64 smoke tests (boot, keyboard, joystick, debugger, PRG, PAL,
  NTSC) continue to pass.
- Architecture, thread ownership, and snapshot rules from `AGENTS.md` remain
  intact.
- No `tools/d64/` headers included from `machine/`.
- No SDL, Nuklear, runtime, platform, or frontend headers in any new or modified
  `machine/` file.
- ROM intercept addresses documented in source with disassembly reference cited.

---

## Status document updates after this phase

- `docs/status/IEC1541.md`: replace stub with full status:

```markdown
# IEC / 1541 status

## Current implementation

- VIA 6522 module implemented and unit-tested.
- 1541 machine shell: CPU, VIAs, memory map, ROM loading, cycle-step.
- IEC bus wiring: VIA #2 ↔ CIA #2 open-collector model, CA1/ATN NMI.
- D64 sector access: ROM intercept at [address], satisfied from mounted image.
- KERNAL LOAD trap disabled per-device when 1541 ROM is loaded; retained as
  fallback when ROM is absent.

## ROM
- 1541 ROM loaded from INI key `[roms] 1541` (combined 16 KB, e.g. `roms/1541.rom`).
- Split-file fallback via `[roms] 1541.c000` + `[roms] 1541.e000` (8 KB each).
- Falls back to KERNAL LOAD trap if no key present or file missing.

## Known limitations / deferred
- D64 writes and SAVE to disk: not implemented.
- Error channel: not implemented.
- Additional drive ROM variants (1571, etc.): not implemented.
- Devices beyond 8 and 9: not implemented.
- VIA #1 disk mechanics: stubbed (not needed for ROM sector intercept approach).
```

- `docs/status/DISK_IO.md`: update **Known limitations / deferred**:
  - Remove: "IEC timing/protocol is not implemented", "1541 CPU/ROM emulation
    is not implemented", "Fast loaders are not implemented".
  - Add: "Fast loaders supported via genuine 1541 ROM execution when ROM is
    present."
  - Retain: D64 writes, SAVE, error channel, devices beyond 8/9.

- `docs/status/DEFERRED.md`: update **Disk / IEC** section to reflect that
  standard IEC and fast loaders are now handled by the genuine ROM. Update
  remaining deferred items.

- `STATUS.md`: update baseline summary to include 1541 emulation. Add to
  "Recent high-value handoff notes" a summary of what the 1541 emulation covers
  and the ROM fallback behaviour.

- `docs/status/TESTING.md`: add VIA unit tests, c1541 unit tests, and 1541
  smoke checks to the documented test coverage.
