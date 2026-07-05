# C64IEC1541PHASE_4.md — 1541 Phase 4: Job-Level Write (D64 persistence via the real drive)

## Status of this document

**IMPLEMENTED.** The job-level WRITE intercept described here has landed:
`C1541_JOB_CMD_WRITE` / `C1541_JOB_WRITE_PROT` and
`c1541_copy_job_buffer_to_sector()` in `src/machine/c1541.c`, the mutable
`c64_get_drive_slot_mut()` accessor in `src/machine/c64.{c,h}`, and WRITE-job
tests in `tests/machine/test_c1541.c`. The optional debounced write-time flush
(Phase 4.3) was **not** added; persistence relies on the existing eject/quit/mount
dirty flush. Remaining sections below are kept as the design record.

Implementation guide, agent-ready. Phase 4 of the 1541 emulation series. This is
the symmetric completion of Phase 3: Phase 3 added job-level **read**; this phase
adds job-level **write**, so files saved while the real 1541 ROM is active
(`emulate_1541=1`) actually persist to the mounted D64 image.

**Scope gate.** This changes disk write behaviour. `AGENTS.md` historically listed
"D64 writes / SAVE to disk" as out of scope; that gate was already opened for the
KERNAL SAVE trap (see `C64MFEAT_D64WRITE_4.md`, "Part A"). This phase extends the
same, already-approved capability into the real-drive path. Confirm with the
maintainer that the scope decision recorded for Part A also covers the 1541 path,
and update `STATUS.md`/`AGENTS.md` accordingly.

## Required reading before proceeding

Read the following in order, all found under `md-files/` in the repository root:

1. `AGENTS.md` — agent workflow, build/test rules, architecture rules, thread
   ownership, snapshot rules.
2. `MASTER.md` — component responsibility boundaries, dependency directions.
3. `STATUS.md` — current handoff routing and baseline summary.
4. `docs/status/DISK_IO.md` — D64 backend, mount/unmount, `writable`/`dirty`
   flags, host flush policy, KERNAL LOAD/SAVE trap coexistence.
5. `docs/status/IEC1541.md` — current 1541 status through Phase 3 (job-level read
   intercept, job-queue mechanism, intercept addresses).
6. `C64IEC1541.md` — full 1541 architecture plan.
7. `C64IEC1541PHASE_3.md` — the read intercept this phase mirrors.
8. `C64MFEAT_D64WRITE_4.md` — the whole-`SAVE` KERNAL trap (Part A) whose
   persistence plumbing (`writable`/`dirty` + runtime flush) this phase reuses.
9. This document.

External reference (as in Phase 3):
- **1541 ROM disassembly:** `github.com/mist64/1541rom` — to confirm the WRITE
  job command code and the write-path entry addresses named below.

After reading, **stop**. State any questions or concerns before touching any code.
Then, and only then, proceed to implementation.

---

## Prerequisites

- Phases 1–3 complete: `c1541.c/.h`, `via6522.c/.h` exist; the drive runs the
  genuine ROM; job-level **read** works (`LOAD"*",8` via the real drive).
- Part A (KERNAL SAVE trap) landed: `slot->writable` / `slot->dirty` exist on the
  drive slot (`src/machine/c64.h:125–126`), `slot->dirty = true` is honoured, and
  `runtime_flush_disk_slot()` / `runtime_flush_dirty_disks()`
  (`src/runtime/runtime_thread.c:690, 722`) persist dirty slots to the host `.d64`
  on mount/eject/quit.
- Confirm all existing tests pass before starting.

---

## Scope

### In scope
- Handle the 1541 **WRITE** job at the same job-queue intercept altitude Phase 3
  uses for READ: copy the drive's job buffer back into the mounted D64
  `image_bytes`, mark the slot dirty, and let the existing runtime flush persist
  it to the host file.
- Honour `slot->writable`: a WRITE job to a read-only-mounted image completes with
  the drive's **write-protect** status so the ROM reports `26,WRITE PROTECT ON`
  up the error channel. Never mutate a read-only image.
- Cover everything that DOS funnels through sector WRITE jobs while the real ROM
  is active: `SAVE`, sequential file writes (`OPEN"f,S,W"` / `PRINT#` / `CLOSE`),
  relative-file record writes, and the BAM/directory updates those entail.

### Out of scope (explicitly)
- **Format** (`N0:name,id`) and any other track-level `EXECUTE` job — deferred to
  Phase 5 (`C64IEC1541PHASE_5.md`), which handles the DOS command/error channel.
  Sector-level WRITE is not sufficient to express track formatting.
- The `emulate_1541=0` (KERNAL-trap) world. That path keeps its existing
  Part A `SAVE`-only behaviour; this phase does not add command/error-channel or
  non-`SAVE` writes there.
- Media-level fidelity (VIA #1 head / GCR / rotation) and G64 — permanently
  deferred per the maintainer's decision; see `C64MFEAT_D64WRITE_4.md` discussion.

---

## Background: why the intercept altitude works for writes

Phase 3 satisfies READ jobs during the ROM's job-dispatch window
(`c1541_satisfy_queued_jobs()` is called when `PC` is in `$F2B0–$F2F6`,
`src/machine/c1541.c`), plus fallbacks at `C1541_ROM_PHYS_READ` (`$F3B1`) and
`C1541_ROM_REED` (`$F4CA`). At that window the emulator inspects the job queue
(ZP `$00–$05`), reads the target track/sector from `hdrs[]` (ZP `$06+`), and for a
READ **fills** the job buffer (`$0300 + n*$0100`) from the image, then marks
`jobs[n]` done — so the ROM never runs the unmodelled GCR read.

Writes are the mirror image and fit the *same* window for a clean reason: DOS
**fills the job buffer first, then queues the WRITE job**. So when the dispatch
loop is reached with a pending WRITE job, the buffer already contains the exact
256 bytes DOS wants on disk. The emulator copies buffer → `image_bytes` at the
sector offset, marks `jobs[n]` = OK, and the ROM's physical write path is never
executed against the stub VIA #1. No new intercept address is required — the WRITE
case slots into the existing job handlers.

---

## Design

### WRITE job command code
Phase 3 masks the raw job byte with `C1541_JOB_CMD_MASK` (`0x78`) and matches
`READ=0x00`, `VERIFY=0x20`, `SEARCH=0x30`. The standard 1541 WRITE job is `$90`,
i.e. `0x90 & 0x78 == 0x10`. Add:

```c
#define C1541_JOB_CMD_WRITE  0x10u   /* confirm against 1541 ROM disassembly */
```

Verify the value against the ROM disassembly before relying on it (same diligence
Phase 3 applied to the READ/VERIFY/SEARCH codes and the intercept addresses).

### New helper — buffer → image (mirror of `c1541_copy_sector_to_job_buffer`)
Add next to the read helper (`src/machine/c1541.c`):

```c
/* Writes the active job's 256-byte buffer back into the mounted D64 image.
   Returns 0 (and writes nothing) if the sector is out of range, the image is
   not mounted, or the slot is not writable.  On success marks the slot dirty. */
static int c1541_copy_job_buffer_to_sector(c1541 *drive, uint8_t n);
```

It reuses `c1541_job_sector_offset()` for track/sector→offset and bounds checking.
It must:
- Fetch the slot via `c64_get_drive_slot(drive->c64, drive->device_number)`.
- If `!slot->writable`, return a distinct "write-protected" result (see below) so
  the caller can set the write-protect job status rather than a generic error.
- `memcpy(slot->image_bytes + offset, &drive->ram[buf_addr], 256)` where
  `buf_addr = C1541_RAM_SECTOR_BUF + n*0x0100`.
- Set `slot->dirty = true` (the field the runtime flush watches). Do **not** flush
  to the host here — persistence is the runtime's job; keep `machine/` free of
  file I/O and thread concerns.

Note the slot is fetched `const` today for reads; the write helper needs a mutable
slot pointer. Use the existing mutable accessor if one exists
(`c64_get_drive_slot` returns what — confirm const-ness) or add a mutable variant
following the existing pattern. Do **not** `#include` any `tools/d64/` header from
`machine/` — write raw bytes to `image_bytes` exactly as the read path reads them.

### Wire into the job handlers
Two functions dispatch on the job command and currently `return 0` in `default:`
for WRITE — `c1541_satisfy_queued_job()` (the dispatch-window scan) and
`c1541_satisfy_physical_job()` (the `$F3B1` fallback). Add a `WRITE` case to
**both**, symmetric with their `READ` cases:

```c
case C1541_JOB_CMD_WRITE: {
    int wp = 0;
    int ok = c1541_copy_job_buffer_to_sector(drive, n, &wp);
    uint8_t status = ok ? C1541_JOB_OK
                        : (wp ? C1541_JOB_WRITE_PROTECT : C1541_JOB_ERROR);
    c1541_complete_queued_job(drive, n, status);   /* or c1541_complete_job() in the physical path */
    return 1;
}
```

Define the write-protect job status per the ROM (DOS translates it to error 26):

```c
#define C1541_JOB_WRITE_PROTECT  0x08u   /* confirm exact code against ROM */
```

`VERIFY` after a write (DOS commonly issues a verify pass) already returns OK in
Phase 3 without touching media, which remains correct here since our write is
authoritative.

### Persistence — reuse Part A, add write-time flush policy
Marking `slot->dirty = true` is enough for the existing eject/quit/mount flush to
persist the image. Decide whether to *also* flush immediately after a write
completes:
- **Recommended:** rely on the existing dirty-flush points (eject/quit/mount)
  as the baseline, and additionally trigger a flush a short, debounced interval
  after the last write settles, so a crash mid-session doesn't lose a save. A
  per-write synchronous flush is simplest but rewrites the whole `.d64` on every
  256-byte sector, which is wasteful during a multi-sector `SAVE`. Whatever is
  chosen, the flush itself stays on the runtime thread
  (`runtime_flush_disk_slot`), never in `machine/`.
- The `machine/` side only sets `dirty`. Any new "flush now" trigger belongs in
  the runtime, following the existing command/flush pattern in
  `runtime_thread.c`.

---

## Implementation phases

### Phase 4.1 — write helper + job handlers (in-drive)
Add `C1541_JOB_CMD_WRITE` / `C1541_JOB_WRITE_PROTECT`, implement
`c1541_copy_job_buffer_to_sector()`, wire the WRITE case into both job handlers,
mutable-slot access, dirty marking. No runtime/UI changes yet.

### Phase 4.2 — writable gating
Ensure a WRITE to a read-only slot returns write-protect and never mutates the
image. Confirm the default mount stays read-only (protecting user images) and that
"mount writable" is the same opt-in Part A introduced.

### Phase 4.3 — persistence policy
Confirm dirty writes reach the host `.d64` via the existing flush points; add the
optional debounced write-time flush if chosen. No new file I/O in `machine/`.

### Phase 4.4 — tests + smoke
Automated and manual checks below.

---

## Tests / smoke checks

### Automated (under `tests/machine/`, following existing conventions)
- **WRITE round-trip:** stage a job buffer with known bytes + a `hdrs[]`
  track/sector + a queued WRITE job on a writable in-memory image; run the job
  handler; assert `image_bytes` at the sector offset now equals the buffer and
  `slot->dirty == true`.
- **Read-back via Phase 3:** after the write, issue a READ job for the same
  track/sector into a *different* buffer and assert byte-exact equality (Phase 3
  read is the oracle for Phase 4 write).
- **Write-protect:** same setup on a `writable == false` slot; assert
  `image_bytes` is unchanged, `slot->dirty` is unchanged, and the job status is
  the write-protect code.
- **Out-of-range sector:** WRITE job with an invalid track/sector leaves the image
  untouched and returns the error status.
- **Non-write jobs unaffected:** the existing Phase 3 READ/VERIFY/SEARCH tests
  still pass.

### Manual smoke (with 1541 ROM present, `emulate_1541=1`)
- `timeout 20 ./build/c64m --disk 8=writable.d64` (writable mount), then in BASIC:
  `10 PRINT"HI"`, `SAVE"TEST",8`, wait for the drive to finish. Restart fresh and
  `LOAD"TEST",8` — confirm it loads.
- High-score / sequential path: `OPEN 2,8,2,"SCORE,S,W" : PRINT#2,"42" : CLOSE 2`,
  then in a fresh run `OPEN 2,8,2,"SCORE,S,R" : INPUT#2,A$ : CLOSE 2` — confirm
  `A$` is `42`.
- Write-protect: mount the same image read-only and confirm `SAVE` fails with
  `26,WRITE PROTECT ON` (verifiable once Phase 5 exposes the error channel; until
  then confirm no bytes change on the host file).
- External validation: open the resulting `.d64` in `c1541`/VICE and confirm the
  directory, BAM free-block count, and file contents are well-formed.

---

## Acceptance criteria

- With the 1541 ROM present, `SAVE` and sequential/relative file writes persist to
  the mounted D64 and survive a restart.
- Phase 3 read is byte-exact over anything Phase 4 writes.
- Read-only-mounted images are never mutated; a write attempt reports
  write-protect.
- `slot->dirty` is set on write and the existing runtime flush persists the image
  to the host `.d64`.
- No `tools/d64/` headers included from `machine/`; no SDL/Nuklear/runtime/
  platform/frontend headers in `c1541.*`. No file I/O added to `machine/`.
- Thread ownership and snapshot rules intact; all prior tests pass.

---

## Status document updates required after this phase

- `STATUS.md` — note D64 writes now work via the real 1541 (not just the KERNAL
  SAVE trap); add to recent handoff notes.
- `docs/status/DISK_IO.md` — job-level WRITE intercept, write-protect handling,
  write-time flush policy. Remove any "1541 path is read-only" limitation.
- `docs/status/IEC1541.md` — WRITE job handling, buffer→image helper, dirty/flush.
- `docs/status/DEFERRED.md` — narrow the deferred item to media-level write /
  format (track-level) / G64; remove the blanket "1541 write not implemented".
- `docs/status/TESTING.md` — new WRITE round-trip / write-protect tests + smoke.
- `AGENTS.md` — reflect the scope decision (writes via the real drive).

---

## Open questions / decisions for the author

1. **Write-time flush.** Baseline (eject/quit/mount) only, or add a debounced
   flush after writes settle? Recommended: add the debounced flush for crash
   safety; never per-sector synchronous.
2. **Exact WRITE job code + write-protect status.** Confirm `0x90`/`0x08`
   against the ROM disassembly before relying on the masked values above.
3. **Mutable slot access.** Reuse an existing mutable accessor or add one — keep
   it consistent with how Part A mutates `slot->dirty`.
