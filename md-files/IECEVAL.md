# IEC + 1541 Emulation Evaluation

Evaluation of the work required to add minimal IEC serial bus + 1541 emulation
sufficient to allow a game like Galencia to load its PAL/NTSC variant PRG after
the initial bootstrap has already run.

Ground truth: source code. Status docs are context only.

---

## 1. CIA2 IEC Line State

CIA2 Port A ($DD00) has a complete, correct open-collector bus model. The relevant
code is `c64_cia2_port_inputs()` in `src/machine/c64.c:900-933`.

**Pin assignments** (KERNAL-standard DDRA = $3F, so bits 3-5 are outputs, bits
6-7 are inputs):

| Bit | Direction | Meaning          |
|-----|-----------|------------------|
| 3   | Output    | ATN out          |
| 4   | Output    | CLK out          |
| 5   | Output    | DATA out         |
| 6   | Input     | CLK in (from bus)|
| 7   | Input     | DATA in (from bus)|

The function:
1. Reads the computed output pins for Port A (before external inputs).
2. If bit 3 is low (ATN being driven by C64), asserts `C64_IEC_ATN` pull.
3. If bit 4 is low (CLK being driven by C64), asserts `C64_IEC_CLK` pull.
4. If bit 5 is low (DATA being driven by C64), asserts `C64_IEC_DATA` pull.
5. ORs in `machine->iec_external_pull` (device-side contributions).
6. Propagates each asserted line to the appropriate `port_a_pull_down` bits:
   - CLK pull → pulls down bit 4 (CLK out) AND bit 6 (CLK in).
   - DATA pull → pulls down bit 5 (DATA out) AND bit 7 (DATA in).

**Bits 6-7 are not floating or zero.** They correctly reflect the combined
open-collector bus state. If the C64 is releasing CLK (bit 4 high) and no device
is pulling (`iec_external_pull = 0`), bit 6 reads high (bus released). If the
device asserts `C64_IEC_CLK` via `iec_external_pull`, bit 6 reads low.

**Existing device-side API:**
- `c64_iec_line` enum defined in `src/machine/c64.h:73-77`: `C64_IEC_ATN = 0x01`,
  `C64_IEC_CLK = 0x02`, `C64_IEC_DATA = 0x04`.
- `uint8_t iec_external_pull` field in `c64_t` (`src/machine/c64.h:314`).
- `c64_set_iec_external_pull(c64_t*, uint8_t)` function (`src/machine/c64.c:1188`).
  Sets the device-side pull and calls `c64_bus_refresh_vic_bank_base()` (which
  is harmless; the VIC bank calculation only looks at bits 0-1 of Port A anyway).

**What does not exist:** any IEC protocol state machine or device emulator.
The pin model is ready; nothing interprets it.

---

## 2. Existing Hooks

### KERNAL LOAD trap

The trap is implemented in `c64_try_kernal_load_trap()` (`src/machine/c64.c:540-603`).

**Trigger:** `machine->cpu.cpu.pc == C64_KERNAL_LOAD_ENTRY` (= `0xFFD5`, defined
at `src/machine/c64.c:11`). The check runs at the start of every call to
`c64_step_cycle_internal()` and `c64_step_instruction()`.

**Behavior on trigger:**
1. Reads device from `$BA`, SA from `$B9`, A register from CPU, filename length
   from `$B7`, filename pointer from `$BB/$BC`.
2. Rejects: device not 8 or 9, A ≠ 0, SA ≠ 0 and SA ≠ 1.
3. Rejects: no D64 mounted.
4. For `$` filename: synthesizes a BASIC directory listing directly in RAM.
5. Otherwise: finds file by name/pattern in the pre-parsed directory, then
   walks the D64 sector chain byte-by-byte into RAM (`c64_drive_load_prg_to_memory()`).
6. Synthesizes the KERNAL LOAD return: sets CPU A/X/Y registers, carry flag,
   status byte at `$90`, `$AE/$AF`, pops two bytes from stack, and jumps to the
   return address + 1. The caller sees a normal LOAD return.

**The trap entirely bypasses CIA2 and IEC.** There is no CIA2 register
manipulation, no bus handshake, no real drive timing.

**Reuse potential:** The trap works correctly for any code path that calls the
KERNAL LOAD entry at $FFD5, including BASIC `LOAD`, and machine-language code
that does `JSR $FFD5`. It covers SA=0 (load at PRG address) and SA=1 (load at
current BASIC start). SA=2 (no load) is rejected, which is correct — SA=2 is
for verify.

The trap does **not** cover:
- Code that bypasses $FFD5 entirely (custom loaders).
- OPEN/CLOSE/CHKIN/CHKOUT channel I/O (`$FFC0/$FFC3/$FFC6/$FFC9`).
- GET/PUT character I/O over IEC.

There is no existing OPEN/CLOSE/CHKIN/CHKOUT/CHRIN trap infrastructure. If the
game uses those KERNAL routines to stream a file rather than calling LOAD, it
would fall through to real IEC — which currently produces no device response.

### CIA2 write path

In `src/machine/c64_bus.c:98-131`, `c64_io_write()` handles CIA2 writes. When
Port A (`$DD00`, reg 0) or DDRA (`$DD02`, reg 2) is written, it already calls
`c64_bus_refresh_vic_bank_base()` (c64_bus.c:126). This is the natural
injection point for notifying an IEC state machine of pin changes. No structural
change to CIA is needed; a 2-3 line addition here is sufficient.

---

## 3. D64 Support

The D64 backend is complete and fully sufficient for a 1541 emulation layer.

**Tools-level library** (`src/tools/d64/d64.c`, `src/tools/d64/d64.h`):
- `d64_image_create()`: Parses a raw D64 byte array. Validates size (174848 or
  175531 bytes). Parses BAM (disk title, ID, DOS type, free block count). Walks
  the directory chain from track 18/sector 1. Handles up to 683 sectors.
- `d64_image_directory_count()` / `d64_image_directory_entry()`: Enumerates all
  non-DEL directory entries.
- `d64_image_find_entry_ascii()`: Case-insensitive ASCII name lookup.
- `d64_image_extract_prg()`: Walks the file's sector chain, appending data
  bytes to a `d64_file_data` heap buffer. Handles final-sector byte count.
  Returns the complete PRG (including the 2-byte load address header) as a flat
  byte array.
- Error handling: detects chain loops, out-of-range sectors, malformed chains.

**Machine-level inline copy** (`src/machine/c64.c`): `c64_drive_load_prg_to_memory()`
(c64.c:285-372) re-implements sector walking directly into RAM, working from
`c64_drive_slot.image_bytes` rather than a `d64_image`. It is functionally
equivalent to `d64_image_extract_prg()` for the subset needed for LOAD.

An IEC device emulator could use either backend. The most natural choice for an
IEC device embedded in `src/machine/` is to work directly from `c64_drive_slot`,
mirroring the existing inline approach, to avoid pulling in the tools/d64 library
as a machine dependency. Alternatively, the machine could hold a parsed `d64_image *`
alongside the raw bytes; but that's a separate refactor and not required.

**Summary:** D64 open, directory enumerate, name lookup, and sector streaming are
all complete. The backend is ready.

---

## 4. What Needs to Be Built

### A. CIA2 write notification hook (trivial)

`src/machine/c64_bus.c:c64_io_write()` lines 123-130. Add one line after the
existing `c64_bus_refresh_vic_bank_base()` call to notify the IEC device of
Port A or DDRA changes. This requires adding an `iec_device *` pointer to
`c64_bus_t`, or — cleaner — intercepting at the `c64.c` level in
`c64_cpu_write()` when the address is in $DD00-$DD0F and IO is visible.

The `c64.c`-level intercept avoids polluting `c64_bus_t` with an IEC pointer
and keeps IEC as a machine-layer concern consistent with how joystick and
keyboard callbacks are wired.

### B. IEC bus state machine (new module: `src/machine/iec.c` + `iec.h`)

The state machine monitors Port A pin changes and implements the standard
Commodore serial IEC protocol. Required states:

1. **Idle** — ATN high, CLK/DATA both high. No pull from device.
2. **ATN command receive** — ATN asserted low by C64. Device pulls DATA low
   (ready). C64 clocks bits on CLK. Device reads command byte.
   - Command bytes: LISTEN (0x20 | device), TALK (0x40 | device),
     UNTALK (0x5F), UNLISTEN (0x3F), OPEN channel (0x6F | channel),
     CLOSE channel (0xE0 | channel), DATA channel (0x60 | channel).
3. **Filename receive** (after LISTEN + OPEN channel) — ATN goes high. Device
   receives filename bytes over the CLK/DATA handshake.
4. **File lookup** — After the CLOSE-channel-with-filename sequence, look up the
   filename in the mounted D64 and buffer the file data.
5. **TALK data send** — C64 sends TALK. Device releases CLK (ready-to-send),
   waits for C64 to release DATA (ready-to-receive), then clocks bytes out.
   EOI is signaled on the last byte by holding CLK released for >200µs before
   the first bit.
6. **Error / no device** — If no D64 is mounted or the file is not found,
   device does not respond to ATN; the C64 sees a timeout (device-not-present).

**Bit-timing model:** The KERNAL IEC routines are not cycle-accurate; they use
software delays (typically ~60µs/bit). The emulated device does not need to match
exact timing. The simplest correct approach is an edge-driven model:

- On each CIA2 Port A change, re-evaluate the state machine.
- The device sets `iec_external_pull` whenever it needs to assert CLK or DATA.
- Because the C64's own KERNAL IEC routines spin-poll CIA2 bit 6/7 (CLK/DATA in),
  the CIA2 read path already returns the correct bus state. No additional
  read-side work is needed.

If a per-cycle IEC step is wanted (for timeout detection), add `iec_device_step()`
called from `c64_advance_one_cycle()` alongside the existing CIA/VIC/SID steps.
For minimal IEC over KERNAL, this is not strictly necessary — the KERNAL routines
have multi-millisecond timeout windows that cycle granularity easily satisfies.

### C. Filename receipt and file lookup

During the ATN + LISTEN + OPEN channel sequence, the device receives the filename
as a stream of bytes over the IEC bus. The state machine needs a small buffer
(16 bytes is the max C64 KERNAL filename length) to accumulate them. After the
full OPEN sequence completes, look up the filename in `c64_drive_slot` using the
existing `c64_drive_find_entry()` logic (c64.c:221-239) or equivalent.

### D. Data channel byte streaming

After TALK is issued, the device streams the PRG bytes back one at a time using
the IEC bit-bang handshake. The device pre-loads the file into a heap buffer
(using `d64_image_extract_prg()` or the inline sector walker) and tracks a byte
index. Each bit transfer:

1. Device asserts CLK low (not-ready-to-send).
2. C64 asserts DATA low (ready-to-receive) — device sees bit 7 of Port A go low.
3. Device releases CLK.
4. Device clocks 8 bits, LSB first: for each bit, assert/release DATA per bit
   value while toggling CLK. C64 samples DATA on CLK rising edge.
5. C64 acknowledges byte by asserting DATA briefly.
6. For last byte: EOI — hold CLK released for >200µs before first bit; C64
   acknowledges EOI by asserting DATA briefly.

In practice the state machine has ~15-20 distinct sub-states per byte, driven
purely by CIA2 Port A pin changes. No timers needed if the design is edge-driven.

### E. CIA2 read-back (already works)

No new work required. As described in section 1, the `c64_cia2_port_inputs()`
function already propagates `iec_external_pull` through to bits 6 and 7 of Port A.
When the device calls `c64_set_iec_external_pull(machine, C64_IEC_CLK)`, the
CIA2 Port A read at bit 6 immediately returns 0 (CLK in asserted). The KERNAL
IEC routines polling CIA2 will see the correct device state.

---

## 5. Fast Loader Risk

**Risk: HIGH.**

Galencia (2016, Jason Aldred) is a modern C64 release designed for original
hardware and common storage devices. It is a polished, professional release.
Modern C64 games nearly universally use custom fast loaders for any loads
beyond the initial BASIC `LOAD"*",8` bootstrap, because:
- The KERNAL IEC routines run at ~300 bytes/second — too slow for large files
  on real hardware.
- Fast loaders (e.g., DreamLoad, FC3, custom variants) run at 2-8× speed.
- SD2IEC, 1541 Ultimate, and similar devices support fast loader protocols.

**The existing KERNAL LOAD trap at $FFD5 handles the initial bootstrap load.**
If that is the only LOAD Galencia performs (e.g., the bootstrap is a loader stub
that contains both PAL and NTSC code inline), no IEC work is needed at all.

If Galencia performs a secondary load using KERNAL LOAD (`JSR $FFD5` with device
8 and a filename), the existing trap handles it already.

If Galencia installs a fast loader and then uses raw IEC bit-banging to load the
PAL/NTSC variant, neither the existing KERNAL trap nor a standard IEC
implementation will help. The fast loader protocol typically involves:
- Uploading a drive-side routine (sent byte-by-byte over standard IEC).
- Switching to a custom, non-standard timing and command set.
- Exchanging data at 2-8× speed over CIA2 bits.

**Without access to Galencia's binary**, the specific behavior cannot be
determined from this codebase. If the goal is "load after bootstrap", the
first thing to try is mounting the D64 and running with the existing KERNAL trap
enabled — it may already work. Only if the secondary load hangs (infinite loop
polling CIA2) is a fast loader confirmed to be in use.

**If fast loader is required:** flag as out of scope for this task. The fast
loader protocol is game-specific, requires binary analysis, and would need a
separate "fast loader pass-through" or trap at a game-specific address.

---

## 6. Effort Estimate

| Component | File | Estimated Lines | Notes |
|-----------|------|-----------------|-------|
| IEC bus state machine | `src/machine/iec.c` (new) | 350–500 | Edge-driven; no per-cycle timing needed |
| IEC header | `src/machine/iec.h` (new) | 50–80 | State enum, struct, API |
| CIA2 write hook | `src/machine/c64.c` or `c64_bus.c` | 10–20 | Add IEC notify on Port A / DDRA write |
| Machine wiring | `src/machine/c64.c` | 30–50 | Init, reset, step hook, `iec_external_pull` calls |
| Machine struct | `src/machine/c64.h` | 10–20 | Add `iec_device` field to `c64_t` |

**Total: ~2 new files, ~450–650 lines of new code, ~60–90 lines of changes
to existing files.**

Complexity is **comparable to the CIA timer implementation** (cia.c is ~595
lines). The IEC protocol logic is simpler in terms of state variety (no BCD,
no alarm, no cascade), but the bit-bang handshake state machine has many
sub-states that need careful sequencing.

Significantly smaller than the VIC-II work (vicii.c is much larger, with complex
raster timing, bad-line logic, and sprite BA windows).

---

## 7. Recommended Approach

**Create `src/machine/iec.c` + `src/machine/iec.h`** as a new machine-layer
module. Keep it within `machine/` to maintain the allowed dependency direction
(`machine → util`, no platform or runtime dependencies).

**IEC device struct** (`iec_device` or embedded directly in `c64_t`):
- State enum (idle, atn-command, listen-data, open-filename, talk-data, etc.)
- Command byte (which device/channel is addressed)
- Channel state (open filename buffer, pointer to loaded file bytes, byte index)
- File data buffer (heap-allocated PRG bytes)
- Pointer back to `c64_t` (to read `drives[]` and call `c64_set_iec_external_pull`)

**Integration point — option A (recommended):**
In `c64.c:c64_cpu_write()` (c64.c:835), after `c64_bus_write()`, check if the
address is in $DD00-$DD0F with IO visible and the register is Port A or DDRA.
If so, call `iec_device_port_a_changed(&machine->iec, cia_read_port_a_pins(&machine->cia2))`.
This keeps IEC as a `c64.c`-layer concern alongside joystick and keyboard, does
not pollute `c64_bus_t`, and the existing `c64_io_write()` hook point
(c64_bus.c:123) already has the VIC bank refresh as a precedent.

**Integration point — option B:**
Add `iec_device *iec` to `c64_bus_t` and call `iec_device_port_a_changed()` from
`c64_io_write()` at c64_bus.c:126. More symmetric with the VIC bank refresh,
but requires `c64_bus_t` to know about the IEC device.

**No CIA2 changes are required.** The CIA2 Port A open-collector model is
complete. The IEC device only needs to read computed pin state via
`cia_read_port_a_pins()` and drive bus lines via `c64_set_iec_external_pull()`.

**Ordering dependencies:**
1. No CIA, VIC, or CPU changes are prerequisites.
2. `c64.h` gets a new `iec_device` field — trivial addition.
3. The IEC module depends on `c64_drive_slot` (defined in `c64.h`) for D64
   data access. This is a `machine → machine` dependency, which is fine.
4. If `d64_image_extract_prg()` is used instead of the inline sector walker,
   `machine/iec.c` would depend on `tools/d64/d64.h`. The allowed dependency
   direction is `machine → util`, and `tools/` is peer to `machine/`, not `util/`.
   Check `AGENTS.md` dependency rules before choosing this path. The inline
   sector walker approach (already present in c64.c) avoids this dependency
   question entirely.

**Recommended first step:** Before implementing IEC, mount Galencia's D64 and
run with the existing KERNAL trap. If the secondary load completes (trap fires
again with the PAL/NTSC filename), no IEC work is needed at all. Only proceed
with IEC if the machine hangs in the KERNAL serial routines.
