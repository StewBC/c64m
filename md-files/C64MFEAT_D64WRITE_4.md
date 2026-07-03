# C64MFEAT_D64WRITE_4 — D64 writing / SAVE to disk

## Status of this document

Implementation guide. Agent-ready for the KERNAL-trap approach; the 1541-DOS-write
approach is scoped but larger. Feature #4 of the "next features" list.

**Milestone scope:** Explicitly OUT of the current milestone. `AGENTS.md` Scope
Limits list "D64 writes, SAVE to disk, directory modification, DOS command
channel, or disk error channel" as out of scope. This is a deliberate
next-milestone feature. Do not implement it while a milestone-completion task is
in flight; land it as its own milestone step and update scope docs accordingly.

## Required reading before starting

1. `AGENTS.md` — note the explicit out-of-scope list; this feature *changes* that
   list, so it needs a matching scope decision recorded in `STATUS.md`/`AGENTS.md`.
2. `STATUS.md`.
3. `docs/status/DISK_IO.md` — D64 parser, mount/unmount, KERNAL LOAD traps,
   drive-queue INI persistence.
4. `docs/status/IEC1541.md` — the real-1541 ROM/IEC path.
5. `md-files/C64MDISK.md` and the `C64MDISKPHASE_*` series — original disk design.
6. This document.

## Goal

Persist data from the C64 back into a mounted D64 image: at minimum implement
`SAVE"NAME",8` of a PRG (write file, allocate sectors, update BAM + directory),
so users can save BASIC programs and game progress. Read-only mounting stays the
default; writing is opt-in per image.

## Non-goals (for a first version)

- No DOS command channel (`OPEN 15,8,15,...` scratch/rename/format) unless trivial
  to add after SAVE works.
- No error channel emulation.
- No relative (REL) files, no `@:` replace semantics beyond what SAVE needs.
- No .G64 / other formats. D64 (35-track, 683 blocks) only in v1.

## Current state (verified against source)

- The disk subsystem is **read-only today**. `docs/status/DEFERRED.md`:
  "D64 writes are not implemented / SAVE to disk is not implemented / Error
  channel is not implemented."
- Load is handled two ways:
  - **KERNAL LOAD trap** in the machine: `c64_try_kernal_load_trap()`
    (`src/machine/c64.c:554`), triggered when `PC == C64_KERNAL_LOAD_ENTRY`
    (`0xFFD5`, `src/machine/c64.c:11`). It reads ZP device/filename/secondary,
    finds the directory entry (`c64_drive_find_entry`), and copies bytes into RAM
    (`c64_drive_load_prg_to_memory`), returning via
    `c64_kernal_load_return(machine, ok, status, end_address)`.
  - **Real 1541 ROM/IEC path** when `emulate_1541=1` and a drive ROM is loaded
    (the trap deliberately bails for that device: `src/machine/c64.c:575-582`).
- The D64 parser/model lives in `src/tools/d64/d64.{c,h}` (read side). Drive slots
  and image bytes are on the machine: `slot->image_bytes`, `slot->image_kind ==
  C64_DRIVE_IMAGE_D64` (see `c64_try_kernal_load_trap`).
- Disk images persist to the `[disk]` INI section on quit
  (`docs/status/DISK_IO.md`), but the on-host `.d64` file is currently never
  rewritten.

## Two implementation strategies

### Strategy A — KERNAL SAVE trap (recommended v1; symmetric with existing LOAD)
Mirror the existing LOAD trap for SAVE. The KERNAL SAVE routine has a known entry
(`$FFD8` = `SAVE` KERNAL vector; the internal save is around `$F5DD`/`$F685` in
the KERNAL — confirm the exact trap PC the same way `C64_KERNAL_LOAD_ENTRY` was
chosen). On trap:
1. Read ZP: device, filename ptr+len, and the save start/end pointers (SAVE
   passes start address in ZP `$C1/$C2` and end in the A/X/Y per the KERNAL SAVE
   convention — verify against the ZP symbol constants already defined near
   `C64_ZP_DEVICE_NUMBER` / `C64_ZP_FILENAME_POINTER` in `src/machine/c64.c`).
2. Gather the byte range from RAM (respecting the 2-byte PRG load-address header).
3. Call a new `d64` write API to allocate blocks, chain sectors, write the file,
   update the BAM and the directory, and mark the slot dirty.
4. Return success/failure through a `c64_kernal_save_return()` analogous to
   `c64_kernal_load_return()`.

Pros: reuses the proven trap mechanism, no drive-controller timing needed, works
even with `emulate_1541=0` (the common case). Cons: bypasses real DOS, so it will
not satisfy custom savers that talk to the drive directly (those need Strategy B).

### Strategy B — Real 1541 DOS write path (larger; defer)
When `emulate_1541=1`, let the actual 1541 ROM perform the write over the modeled
IEC bus, and persist the resulting sectors. This is substantially more work
(drive-side write, VIA/motor/head behavior — much of which `docs/status/DEFERRED.md`
lists as unmodeled) and should be a separate later phase. Document it as deferred.

## Core new component: `d64` write API

Add to `src/tools/d64/d64.{c,h}` (writes belong in the tool, invoked by the
machine, keeping `machine -> util/tools` direction legal):

```c
/* All operate on the in-memory D64 image buffer (683*256 or 768*256 bytes).
   The machine owns the buffer; the tool mutates it and reports dirtiness. */
typedef struct d64_image d64_image;   /* if not already present, wrap bytes+len */

bool d64_bam_alloc_sector(d64_image *img, uint8_t *out_track, uint8_t *out_sector);
bool d64_bam_free_sector(d64_image *img, uint8_t track, uint8_t sector);
bool d64_write_prg(d64_image *img, const uint8_t *name, size_t name_len,
                   const uint8_t *data, size_t data_len,
                   uint8_t file_type /* PRG */, bool replace);
/* Serialize the (possibly modified) image back to a host file. */
bool d64_flush_to_file(const d64_image *img, const char *path, char *err, size_t errsz);
```

Implement the standard D64 geometry: 35 tracks, variable sectors/track
(21/19/18/17), track 18 = directory + BAM, sector interleave (10 for files, 3 for
directory), file sector chaining (first two bytes = next track/sector, `00/last`
terminates), and directory entry format (type byte, first-sector T/S, 16-byte
padded PETSCII name, block count). Reference the tables already implied by the
read side in `d64.c`.

## Implementation phases

### Phase 1 — Read/verify D64 geometry + BAM in the tool
- Confirm the read side already encodes track→sector counts and offsets; expose
  them for the writer. Add BAM read/verify helpers.

### Phase 2 — Sector allocation + file write (in-memory)
- Implement `d64_bam_alloc_sector` / `d64_write_prg` operating purely on the
  in-memory buffer. Unit-test round trips (write PRG → re-read via existing read
  path → bytes match; BAM free-block count decremented correctly).

### Phase 3 — KERNAL SAVE trap in the machine
- Add `C64_KERNAL_SAVE_ENTRY` constant and `c64_try_kernal_save_trap()` next to
  the LOAD trap (`src/machine/c64.c`), wired into the same dispatch point that
  calls `c64_try_kernal_load_trap` (`src/machine/c64.c:793,1118`). Bail (like
  LOAD) when the real 1541 handles the device.
- Mark the slot dirty on successful in-memory write.

### Phase 4 — Persistence policy
- Decide when the modified image is flushed to the host `.d64` file:
  immediately after each SAVE, or on eject/quit. Recommended: flush immediately
  after a successful SAVE **and** on eject/quit, guarded by a per-slot
  `writable`/`dirty` flag. Wire flush through the runtime thread (it owns the live
  machine and already does disk file I/O), not the frontend.
- Add a per-image "mount read-write" option: a `[disk]` INI flag and/or a UI
  toggle in the disk panel (`docs/status/DISK_IO.md` describes the disk UI
  `[N][Add][Eject]`). Default remains read-only to protect user images.

### Phase 5 — Runtime/UI surface
- Runtime command + client wrapper to toggle writable and to force-flush.
- Surface save success/failure via the existing event/notification path.

## Tests / smoke checks

- **Tool unit tests** `tests/tools/test_d64_write.c`: allocate/free sectors,
  BAM block-count invariants, write a PRG then read it back byte-exact, overwrite/
  replace, disk-full behavior (allocation fails cleanly), directory-full behavior.
- **Machine trap test:** simulate a SAVE trap (set ZP + registers as the KERNAL
  would) and assert the image gains the file and the directory/BAM update.
- **Round-trip smoke (manual):** `timeout 12 ./build/c64m --disk 8=writable.d64`,
  in BASIC `10 PRINT"HI"`, `SAVE"TEST",8`, then `LOAD"TEST",8` in a fresh run and
  confirm it loads. Verify with an external tool (e.g. `c1541`/VICE) that the
  image is well-formed.

## Docs to update on completion

- `AGENTS.md` — move "D64 writes / SAVE to disk" out of the out-of-scope list (or
  note the new milestone that owns it). This is a scope change and must be
  explicit.
- `STATUS.md` — new capability line.
- `docs/status/DISK_IO.md` — writable mount, SAVE trap, flush policy, INI flag.
- `docs/status/DEFERRED.md` — remove "D64 writes / SAVE to disk not implemented";
  keep "error channel" and "1541 DOS write path (Strategy B)" as still deferred.
- `docs/status/TESTING.md` — new tests + smoke.

## Open questions / decisions for the author

1. **Scope gate.** This changes an explicit `AGENTS.md` non-goal. Confirm the
   maintainer wants it landed now and record the scope change; otherwise keep it
   as a documented deferred design only.
2. **Exact SAVE trap PC.** Determine the correct KERNAL trap address/entry for
   SAVE the same way `C64_KERNAL_LOAD_ENTRY = 0xFFD5` was chosen (trap at the
   KERNAL API vector vs. an internal routine). Verify the ZP contract for the save
   start/end pointers against the ROM in use.
3. **Flush timing + safety.** Recommended: writable is opt-in per image, flush on
   SAVE and on eject/quit, never touch an image mounted read-only. Confirm this
   matches the desired safety posture for user disks.
4. **DOS command channel.** Decide whether scratch/rename/initialize are in this
   feature or a follow-on. Recommended: follow-on.
