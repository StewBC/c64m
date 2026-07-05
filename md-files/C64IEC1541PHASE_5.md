# C64IEC1541PHASE_5.md — 1541 Phase 5: DOS Command & Error Channel

## Status of this document

**IMPLEMENTED.** Verification sweep (5.1) and format (5.2) are done; the outcome
was even smaller than this plan anticipated.

Verification sweep results (driven live via the control port, real 1541 ROM +
Phase 4 write, screen scraped from `$0400`):
- **Scratch** (`S0:name`): works — directory entry removed, blocks reclaimed.
- **Rename** (`R0:new=old`): works — entry renamed in place.
- **Validate** (`V0`): works — executes without error.
- **Error/status channel** (`OPEN 15,8,15 : INPUT#15,…`): works — real messages
  round-trip, e.g. `00, OK,00,00` and `01,FILES SCRATCHED,01,00`.

All of the above needed **no new code** — they are ordinary directory/BAM sector
READ/WRITE jobs the real ROM issues, satisfied by Phase 3 read + Phase 4 write.

Only **format** needed code, and the fix was simpler than the "synthesize a
formatted image" design below: the DOS `NEW` command formats each track via an
EXECUTE job (`$E0`, `& $78 == $60`) that runs the ROM's GCR FORMT routine against
the unmodelled VIA #1. We intercept that EXECUTE job in `c1541_format_track()`
(`src/machine/c1541.c`): erase the target track's sectors in the writable image
and report success. The ROM's own DOS then writes the fresh BAM + directory
(disk name/ID) through normal WRITE jobs — so **no `d64_image_format()` tool
helper was required**. Read-only mounts return write-protect. Verified
end-to-end: `N0:NAME,ID` wipes existing files, sets the new name/ID, and the disk
is immediately usable for SAVE. Tests: `test_queued_format_job_success` /
`test_queued_format_job_write_protect` in `tests/machine/test_c1541.c`.

The design sections below are kept as the historical record; where they propose
a `d64_image_format()` tool helper, note that the shipped implementation did not
need it.

Implementation guide, agent-ready. Phase 5 of the 1541 emulation series. Depends
on Phase 4 (`C64IEC1541PHASE_4.md`, job-level write) being complete. This phase
makes the DOS command/error channel (secondary address 15) usable while the real
1541 ROM is active (`emulate_1541=1`): status readback (`72,DISK FULL`, `26,WRITE
PROTECT ON`, `00, OK`, …), the sector-level commands that already fall out of the
real ROM plus Phase 4 write, and — the one genuinely new mechanism — **format**
(`N0:name,id`), which is a track-level operation the sector intercept cannot
express.

## Required reading before proceeding

Read the following in order, all under `md-files/` in the repository root:

1. `AGENTS.md`, `MASTER.md`, `STATUS.md`.
2. `docs/status/DISK_IO.md` — D64 backend, BAM/directory layout, writable/dirty.
3. `docs/status/IEC1541.md` — 1541 status through Phase 4 (job-level read+write,
   intercept addresses, job-queue mechanism).
4. `C64IEC1541.md` — full 1541 architecture plan.
5. `C64IEC1541PHASE_4.md` — job-level write; this phase assumes it.
6. This document.

External references:
- **1541 ROM disassembly:** `github.com/mist64/1541rom` — command parser entry,
  the error-message table, and the `FORMT` (`$C8` execute) job path.
- A D64 layout reference for a freshly formatted 35-track image (empty BAM, empty
  directory on track 18) — to synthesize the format result.

After reading, **stop**. State any questions or concerns before touching any code.
Then, and only then, proceed to implementation.

---

## Prerequisites

- Phase 4 complete: WRITE jobs persist to `image_bytes`; write-protect handled;
  runtime flush persists to host `.d64`.
- All Phase 1–4 tests pass.

---

## Key insight: most of this comes free — scope accordingly

With the genuine 1541 ROM running (`emulate_1541=1`) **and** Phase 4 sector writes
in place, a large part of "DOS + error channel" is **already handled by the ROM
itself** and needs little or no new code. Verify each of the following works
before writing anything for it; only build what actually fails:

- **Error/status channel readback.** The ROM maintains its own error-message
  buffer and serves channel-15 TALK natively over the modelled IEC bus. Reading
  the status (`OPEN 15,8,15 : INPUT#15,EN,EM$,ET,ES : CLOSE 15`, or the BASIC
  error-channel print) should round-trip through the real ROM once the drive can
  report job results — which Phase 4 makes possible for writes. **Expect this to
  work; the task is to verify, not to reimplement.**
- **Scratch (`S0:name`), rename (`R0:new=old`), validate/collect (`V`),
  initialize (`I`).** These parse on channel 15 in the ROM and bottom out in
  ordinary sector READ/WRITE jobs against the directory and BAM — all of which the
  Phase 3 read + Phase 4 write intercepts already satisfy. **Expect these to work
  via the real ROM; verify each.**

So the **genuinely new work in Phase 5 is narrow**: (1) **format**, which uses a
track-level execute job the sector intercept doesn't cover, and (2) confirming /
patching any status-channel gaps surfaced by verification.

---

## Scope

### In scope
- **Format intercept** (`N0:name,id` and `N0:name` "quick"/clear): satisfy the
  ROM's `FORMT` execute job by synthesizing a formatted image in `image_bytes`
  (fresh empty BAM, empty directory on track 18, disk name + ID) instead of
  running the unmodelled track-level GCR write. Honour `slot->writable`; mark
  dirty; persist via the existing flush.
- **Verification pass** over the "comes free" list above; document what works and
  fix only real gaps (e.g. a status code the ROM path can't surface because a job
  result was reported wrong in Phase 4).

### Out of scope
- The `emulate_1541=0` KERNAL-trap world — no command/error channel there; it
  keeps Part A `SAVE`-only behaviour.
- Relative-file DOS semantics beyond what the ROM already does.
- Media-level format fidelity (real GCR track layout, custom formats, bad-track
  emulation) and G64 — permanently deferred.
- Multi-drive DOS commands (`C0:...=1:...` copy across drives), duplicate `D`.

---

## Design

### Format — the one new intercept
The ROM's `NEW` command parses `N`, then queues a track-level execute job
(`FORMT`, job code `$C8` — confirm against disassembly) per track. The sector
intercept cannot express "erase and lay down a whole track," so intercept the
execute job the same way READ/WRITE are intercepted at the job-dispatch window:

1. Detect the `FORMT`/execute job in the job handlers (add an
   `C1541_JOB_CMD_EXECUTE` / format-specific case alongside the Phase 4 WRITE
   case). Confirm the exact command bits under `C1541_JOB_CMD_MASK`.
2. On the **first** format job of a `NEW`, synthesize a freshly-formatted 35-track
   D64 into `image_bytes`:
   - Zero all sector data.
   - Write a valid empty **BAM** on track 18 sector 0 (all sectors free except
     track 18's directory/BAM sectors), including the disk name (PETSCII, padded)
     and the 2-char disk ID from the command.
   - Write an empty **directory** on track 18 sector 1 (first entry link
     `00/FF`, no files).
   - Follow the exact byte layout the read side already understands (mirror the
     tables/offsets implied by `d64_sector_offset` and the read path). Since
     `machine/` may not include `tools/d64/`, either lay the bytes down inline in
     `c1541.c` **or** — cleaner — add a `d64` tool helper
     (`d64_image_format(...)`) invoked from the machine the same indirect way
     Part A calls `d64_image_write_prg()` from `c64.c` (not from `c1541.c`). Prefer
     the tool helper so the well-formed-image logic lives with the other D64
     write code and is unit-testable there.
   - `N0:name` without an ID ("quick format" / clear) only clears the BAM +
     directory, preserving the existing disk ID. Handle both forms.
3. Complete each subsequent per-track format job as OK without further image
   changes (the image was fully formatted on the first job), so the ROM's format
   loop finishes cleanly and reports `00, OK` on the error channel.
4. Set `slot->dirty = true`; honour `slot->writable` (write-protected → the ROM
   reports `26,WRITE PROTECT ON`).

Because `machine/` must not include `tools/d64/`, route any new `d64_image_format`
call through the same seam Part A uses (invoke the tool from `c64.c`, or pass a
formatted buffer down), not directly from `c1541.c`.

### Everything else — verify, don't build
For scratch/rename/validate/initialize and status readback, write a verification
checklist (below) and only add code where a real defect is found. If a status code
never appears because Phase 4 reported the wrong job result, fix it in the Phase 4
job handler rather than adding a parallel status path here.

---

## Implementation phases

### Phase 5.1 — verification sweep (no code first)
With Phase 4 in place and the ROM present, run the checklist under "Tests" for
scratch/rename/validate/initialize and status readback. Record what already works.
This determines how much of the rest is needed.

### Phase 5.2 — format
Implement the `FORMT`/execute intercept and the formatted-image synthesis
(preferably as a `d64_image_format()` tool helper invoked via `c64.c`). Handle
`N0:name,id` and `N0:name`. Writable gating + dirty + flush.

### Phase 5.3 — gap fixes
Fix only the concrete status/command gaps surfaced in 5.1.

### Phase 5.4 — tests + docs.

---

## Tests / smoke checks

### Automated (tool-level, under `tests/tools/`)
- If `d64_image_format()` is added: format an image, then via the existing read
  path assert (a) directory is empty, (b) BAM reports the correct free-block count
  for a blank 35-track disk, (c) disk name + ID match the command, (d) the image
  passes the same structural checks `d64_image_write_prg` round-trip tests use.
- Write a PRG into a freshly-formatted image and read it back byte-exact
  (format → write → read chain).

### Manual smoke (1541 ROM present, `emulate_1541=1`, writable mount)
- **Status readback:** `OPEN 15,8,15 : INPUT#15,A,B$,C,D : PRINT A;B$;C;D :
  CLOSE 15` after a fresh mount → expect `0 OK 0 0` (or the ROM's power-on
  message). After a failed op → expect the matching error (e.g. `62 FILE NOT
  FOUND`).
- **Format:** `OPEN 15,8,15,"N0:BLANK,42" : CLOSE 15`, then `LOAD"$",8` / `LIST`
  → empty directory titled `BLANK`, free blocks = blank-disk count. Save a file,
  confirm it appears. Validate the image in `c1541`/VICE.
- **Scratch/rename/validate:** create two files, `S0:one`, confirm it's gone and
  blocks freed; `R0:new=two`, confirm rename; `V`, confirm free-block count is
  consistent. (Expected to work via the ROM; the smoke confirms it.)
- **Write-protect:** on a read-only mount, `N0:...` and `S0:...` report
  `26,WRITE PROTECT ON`; the host `.d64` is unchanged.

---

## Acceptance criteria

- `OPEN 15,8,15` status readback returns correct DOS error codes/messages via the
  real ROM.
- `N0:name,id` and `N0:name` produce a well-formed formatted image (verified by
  the read path and by an external tool) and persist to the host `.d64`.
- Scratch / rename / validate / initialize work (via the ROM) and their effects
  persist.
- Read-only mounts are never mutated; command-channel writes report write-protect.
- No `tools/d64/` headers included from `machine/`; format synthesis routed
  through the same seam Part A uses. No file I/O in `machine/`.
- Thread ownership / snapshot rules intact; all prior tests pass.

---

## Status document updates required after this phase

- `STATUS.md` — DOS command + error channel usable via the real 1541; format
  supported.
- `docs/status/DISK_IO.md` — command channel (secondary 15), status readback,
  format synthesis, what remains deferred (media-level format, cross-drive copy).
- `docs/status/IEC1541.md` — format/execute intercept; verification results for
  scratch/rename/validate/initialize; status-channel behaviour.
- `docs/status/DEFERRED.md` — remove "error channel not implemented"; keep
  media-level fidelity / G64 / cross-drive copy as deferred.
- `docs/status/TESTING.md` — format tests + DOS-channel smoke.
- `AGENTS.md` — scope reflects DOS command/error channel support.

---

## Open questions / decisions for the author

1. **Format home.** Synthesize the formatted image inline in `c1541.c`, or add a
   `d64_image_format()` tool helper invoked via `c64.c`? Recommended: the tool
   helper — keeps well-formed-image logic with `d64_image_write_prg`, unit-testable,
   and respects the `machine/ ⇏ tools/` rule.
2. **How much is actually free.** Phase 5.1's verification sweep decides the real
   size of this phase. If scratch/rename/validate/initialize and status readback
   all work via the ROM, Phase 5 is essentially "format + verification."
3. **Exact `FORMT` job code.** Confirm the execute/format command bits and the
   error-message table addresses against the ROM disassembly.
