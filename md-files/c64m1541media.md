# c64m1541media.md — Real 1541 media-level emulation

## Status of this document

**M0–M8 IMPLEMENTED (opt-in).** Further title-by-title loader work is ongoing
matrix expansion, not a new subsystem.

| Field | Value |
|---|---|
| Quest | Real 1541 media-level emulation |
| Related status | `docs/status/IEC1541.md`, `docs/status/DISK_IO.md`, `docs/status/DEFERRED.md` |
| Prior architecture | `C64IEC1541.md` + Phases 1–5 |
| Enable | `[disk] media_1541=1` with `emulate_1541=1` (Configure: "1541 media (GCR)") |
| Code | `src/machine/c1541_gcr.*`, `c1541_media.*`, wiring in `c1541.c` |

### Phase completion

| Phase | Status | Notes |
|---|---|---|
| M0 Config + harness | **Done** | `media_1541` in config/INI/UI; media mode gate on intercept |
| M1 Disk VIA mechanics | **Done** | Motor, spin-up, stepper, head-stop, WPS, density |
| M2 GCR + D64 tracks | **Done** | Encode/decode, standard sector layout, track synthesis |
| M3 Rotation / SYNC / read | **Done** | Port A GCR stream, BYTE READY→SO; stock LOAD without READ intercept |
| M4 Port A write | **Done (hybrid)** | Port A + PCR write-gate flux path live; DOS WRITE jobs still intercept to D64 then `poke_sector` GCR |
| M5 Format | **Done (hybrid)** | FORMT EXECUTE still erases D64 track then `rebuild_track` GCR |
| M6 G64 | **Done (read-only v1)** | `tools/g64`, mount via runtime, media attach; WRITE → protect |
| M7 Fast-loader matrix | **Done (v1)** | Matrix documented; G64 first-file LOAD automated; RUN/secondary still open |
| M8 Harden | **Done (v1)** | Motor-off flux idle; density prefers VIA when programmed; G64 FORMT WPROT |

### M4/M5 hybrid note

Pure job-level Port A capture for stock DOS WRITE proved unreliable (invalid
GCR under the head; D64 mirror never updated). Shipped approach:

1. **READ/SEARCH/VERIFY** — no intercept; real ROM GCR path (M3).
2. **WRITE** — intercept buffer→D64 (Phase 4 reliability), then rewrite that
   sector’s data GCR block in the track image (`c1541_media_poke_sector`).
3. **FORMT EXECUTE** — erase D64 track + rebuild track GCR (`rebuild_track`).
4. **Port A flux write** still runs (PCR CB2 write gate + bit clock) for
   non-job / future pure-physical work and keeps BYTE READY correct.

### M7 compatibility matrix (v1)

Rules for this matrix:

- **LOAD\*** means stock KERNAL/DOS `LOAD"*",8` of the first directory PRG via
  media path (`emulate_1541=1`, `media_1541=1`, 1541 ROM present).
- **RUN/secondary** means post-bootstrap behaviour (custom drive code, fast
  loaders, multi-file, protection). Not claimed unless listed PASS.
- Prefer fixing **shared** mechanisms over per-title hacks.

| Title / path | Image | LOAD* | RUN / secondary | Notes |
|---|---|---|---|---|
| Intercept baseline | D64 GALENCIA | PASS | n/a | Job intercept, no media |
| Media D64 stock | D64 GALENCIA | PASS | not claimed | Physical GCR READ; automated |
| Media D64 SAVE | D64 blank | n/a | SAVE PASS | Hybrid WRITE; automated |
| Media G64 first file | Robocop (Data East 1987) G64 | PASS | **FAIL (expected)** | Custom loader after bootstrap; human + automated LOAD* |
| Media G64 write | any G64 | n/a | WRITE/FORMT → protect | Read-only v1 |

**How to smoke Robocop (human):**

```text
./build/c64m -a -d '8=./assets/disks/robocop[data_east_1987](ntsc)(alt)(!).g64'
```

Use the project `c64m.ini` (or equivalent) with `emulate_1541=1`, `media_1541=1`,
and ROM paths set. Optional: `--control-port PORT` to observe with
`get-cpu` / `get-memory` / `get-disk-status` after load.

**Optional control-port observe (after UI or headless launch with your ini):**

```text
hello
get-disk-status 8
wait-frame 100 60000
get-cpu
get-memory $0801 64 map
```

Do not use a scratch ini missing ROM/`emulate_1541` keys — that yields
`?DEVICE NOT PRESENT` and is not a media-path failure.

### What M7 does *not* finish

- Making Robocop (or any protected/commercial multi-stage title) fully playable
- Pure Port A job WRITE capture (still hybrid)
- G64 write-back
- Exhaustive loader catalogue

Expand the matrix as titles are tried; fix only shared media/VIA/timing gaps.

---

## Required reading

Read in order under `md-files/`:

1. `AGENTS.md` — milestone scope, architecture, thread/snapshot rules
2. `MASTER.md` — component boundaries
3. `STATUS.md` — current handoff routing
4. `docs/status/IEC1541.md` — 1541/VIA/IEC status and known limits
5. `docs/status/DISK_IO.md` — D64 mount/flush/load-save model
6. `docs/status/DEFERRED.md` — Disk/IEC deferred items
7. `C64IEC1541.md` and `C64IEC1541PHASE_1.md` … `PHASE_5.md` — how the job intercept was built
8. Source: `src/machine/c1541.c`, `c1541.h`, `via6522.c`, `via6522.h`, `c64.h` (drive slot)
9. This document

External references (implementation agents should use these, not invent pinouts):

- 1541 ROM / DOS labels: `github.com/mist64/1541rom` (dos1541, dos.lbl)
- Hardware: 1541 service manual (UC1 serial VIA $1800, UC2 disk VIA $1C00)
- G64 format spec (track dumps used by VICE and most media-level emulators)
- Conceptual background: physical sector layout, SYNC, stepper/head stop, 300 rpm

---

## 1. Evaluation: where we are

### 1.1 What works today (job-level 1541)

The drive path is a real 1541 **CPU + dual VIA + DOS 2.6 ROM** on the runtime
thread, lockstep with the C64. IEC ATN/CLK/DATA open-collector wiring works for
devices 8 and 9. Sector I/O is **not** media-level: the emulator intercepts DOS
jobs and copies 256-byte sectors to/from a mounted D64.

| Capability | Status | Mechanism |
|---|---|---|
| 6502 + 2 KB RAM + 16 KB ROM | Done | `c1541` shell |
| VIA timers / IFR / IER / CA1 | Done enough for IEC + ROM init | `via6522` |
| Serial VIA ($1800) IEC | Done | `c1541_update_iec_bus()` each cycle |
| Disk-controller VIA ($1C00) ports | **Stub** | Registers accept R/W; no motor/SYNC/GCR semantics |
| BYTE READY → SO | Partial | T1 PB7 toggle → `c6510_set_overflow` only |
| Sector READ/SEARCH/VERIFY | Done | Job intercept → D64 sector copy |
| Sector WRITE | Done | Job intercept → D64 mutate + dirty |
| Format `N0:` | Synthetic | FORMT EXECUTE intercept erases track; ROM writes BAM/dir |
| Scratch/rename/validate/status | Done | Real ROM + ordinary READ/WRITE jobs |
| GCR encode/decode | **Unmodelled** | Explicit comments in `c1541.c` |
| Rotation / SYNC / head / motor | **Unmodelled** | Bypassed by intercept |
| G64 | **Absent** | `C64_DRIVE_IMAGE_D64` only |
| Fast loaders needing mechanics | **Not validated** | Fail if they touch disk VIA / raw GCR path |

Primary intercept window (`src/machine/c1541.c`):

```text
PC in $F2B0–$F2F6  → satisfy queued jobs (READ/WRITE/VERIFY/SEARCH/EXECUTE)
PC == $F3B1        → physical job fallback
PC == $F4CA (reed) → sector READ fallback → jump $F505 (read40) or errr
```

Source intent (paraphrased from file header and job handlers):

> The emulator mounts D64 images rather than modelling a rotating disk. Supported
> jobs are satisfied before the ROM waits for SYNC from the disk controller. GCR
> formatting is unmodelled; a D64 stores decoded sectors, not tracks.

### 1.2 Naming correction (important)

Older planning text sometimes called “VIA #1” the head/GCR port. **In real 1541
hardware and in c64m code:**

| Chip | Address in 1541 | Role in c64m | Media relevance |
|---|---|---|---|
| Serial VIA | `$1800` | `drive->via1` | IEC only (already wired) |
| Disk controller VIA | `$1C00` | `drive->via2` | **All media mechanics** |

All media work targets **`via2` at `$1C00`**, not the serial VIA. Status text that
says “VIA #1 head / GCR” should be read as “disk-controller VIA head/GCR” and
updated when this quest lands.

### 1.3 Disk-controller VIA pin model (target semantics)

Canonical 1541 UC2 ($1C00) mapping used by DOS and almost every media emulator:

**Port B (`ORB`/`IRB`)**

| Bit | Direction | Signal | Meaning for media layer |
|---|---|---|---|
| 0–1 | out | STP0/STP1 | Stepper phase; sequence steps head ±½ track per valid transition |
| 2 | out | MTR | Spindle motor enable (1 = on; spin-up delay needed) |
| 3 | out | LED | Activity LED (cosmetic; may report in UI later) |
| 4 | in | WPS | Write protect sense (from mount `writable` inverted to hardware polarity) |
| 5–6 | out | DS0/DS1 | Bit-rate / density zone select |
| 7 | in | SYNC | Active when head sees ≥10 consecutive 1-bits (sync mark) |

**Port A (`ORA`/`IRA`)**

| Mode | Meaning |
|---|---|
| Read (DDRA=0) | Next GCR byte from head after BYTE READY |
| Write (DDRA=$FF) | GCR byte written under head while motor on and not write-protected |

**BYTE READY**

Hardware asserts BYTE READY each time a GCR byte has been shifted in/out. On the
1541 this is wired so the 6502 can sample it via the SO (overflow) path and/or
related VIA timing. c64m already toggles SO from **via2 T1 PB7** state changes —
that is a partial approximation of BYTE READY and must be reconciled with a real
bitcell/byte clock, not left as an independent free-running timer artifact.

### 1.4 Physical media facts the model must capture

```text
Spindle:           ~300 rpm → 200 ms/revolution when motor is up to speed
Tracks:            1..35 used by stock DOS; hardware can reach ~40 (half-tracks)
Zones / SPT:       21 / 19 / 18 / 17 sectors (tracks 1–17 / 18–24 / 25–30 / 31–35)
Encoding:          4→5 GCR; 256 data bytes → 320 GCR bytes per data block
Sector layout:     SYNC + header + gap + SYNC + data + gap (inter-sector gaps)
Header contains:   ID, track, sector, checksum (and related GCR fields)
Index hole:        not used by 1541; position comes from SYNC + header contents
Head position:     software-maintained track + physical stepper; track-1 bang
Write protect:     sensed on disk-controller VIA PB4
```

D64 stores **decoded 256-byte sectors** only. G64 stores **per-track GCR/bit
streams** and is the natural host format for nonstandard layouts.

### 1.5 Why the intercept exists (and what it costs)

Job intercept was the correct vertical slice for the current milestone:

- Unlocks real ROM serial/IEC and ordinary LOAD/SAVE without building a disk physics lab
- Avoids `machine/` depending on `tools/d64` (inline offset tables instead)
- Keeps lockstep cost low (no per-cycle bitcell work)

Costs that define this quest:

| Cost | Effect |
|---|---|
| ROM never runs GCR read/write loops against hardware | Any code that bypasses job queue and talks to `$1C00` fails |
| No rotation time | Inter-sector timing, motor spin-up, and half-track seeks are free or wrong |
| No SYNC | Fast loaders that wait on PB7 SYNC hang or mis-sync |
| No density / bit rate | Zone-dependent timing is invisible |
| No track image | Custom formats, bad GCR, extra tracks, G64 dumps unsupported |
| Format is synthetic | Real FORMT GCR layout never laid down |
| Fast loaders unvalidated | Explicit deferred item |

### 1.6 Architecture constraints that still apply

From `AGENTS.md` / `MASTER.md`:

```text
machine/  → tools + util only (no runtime/frontend/platform/SDL)
1541 lives on runtime thread only; owned by c64_t
No live machine pointers to UI thread
Frontend sees copied snapshots only
Do not #include tools/d64 from c1541 for core hot path unless a later design
  explicitly relocates shared helpers under an approved shared layer
```

Media-level state is **machine state**. Host formats (D64/G64 parse/load) belong
in `tools/`; runtime mounts and flushes; machine consumes mounted image bytes
and media views.

### 1.7 Eval verdict

| Question | Answer |
|---|---|
| Is job intercept wrong? | No — it is the right current-milestone design. |
| Can media-level be a small patch on intercept? | No — it is a new subsystem that eventually **replaces** physical job satisfaction. |
| Can D64 alone be enough forever? | For stock DOS sector I/O yes; for G64/custom/fast loaders no. |
| Should this start now? | **No**, not under the current milestone. Open as a post-milestone quest with explicit phases. |
| Hardest parts? | Correct BYTE READY/SYNC timing; dual representation D64↔track GCR; performance of rotation under lockstep; dual-mode coexistence during migration. |
| Highest value first slice? | Disk-controller VIA PB motor/stepper/SYNC/WPS + synthetic track GCR from D64 + disable intercept for standard READ so stock ROM GCR path works. |

---

## 2. Goals and non-goals

### 2.1 Goals (quest complete when true)

1. **GCR tracks** — each mounted side has a track image (or on-demand equivalent) holding GCR bytes/bits the head can stream.
2. **Rotation** — when motor is on (after spin-up), a head bit/byte position advances at the selected density rate; one revolution wraps the track buffer.
3. **SYNC** — PB7 reflects presence of a long run of 1-bits under the head.
4. **Motor / head** — MTR enables rotation; STP0/STP1 step the head; track-1 mechanical stop is modelled enough for bang/initialize.
5. **Disk-controller VIA behavior** — Port A GCR I/O and Port B mechanics are live; ROM physical read/write/format paths can run without job intercept for those operations.
6. **G64 support** — mount/read (and optionally write-back) G64 track dumps; D64 remains supported via GCR synthesis.
7. **Nonstandard / fast-loader compatibility** — loaders that drive `$1C00`, wait on SYNC/BYTE READY, use half-tracks, or custom GCR layouts work for a documented acceptance set (not “all demos ever”).

### 2.2 Non-goals (still deferred after this quest unless reopened)

```text
- 1571 / dual-sided / MFM
- Devices beyond 8 and 9
- Parallel cable / SpeedDOS hardware dongles
- Bit-perfect analog bitcell noise, wow/flutter, weak bits (optional later)
- Full cross-drive copy / every B-*/M-* edge case beyond what ROM + media enable
- Cycle-perfect VIA shift-register modes unused by 1541
- Replacing the KERNAL LOAD trap path when emulate_1541=0
- Claiming full demo-scene or copy-protection compatibility on day one
```

### 2.3 Compatibility policy during migration

```text
Mode A — intercept (current): default until media path is proven for stock DOS.
Mode B — media: ROM GCR path + disk VIA; job intercept for physical READ/WRITE
          disabled when media ready flag is set.
Mode C — hybrid (transition only): intercept WRITE to D64 mirror while media
          reads from track GCR; retire as soon as write path is solid.
```

Prefer a short hybrid window over permanent dual semantics. Document which mode
is active via a `c64_config` / INI flag (e.g. `[disk] media_1541=0|1`) so
regressions can bisect.

---

## 3. Target architecture

### 3.1 New machine-side media module

Suggested files (names can be adjusted; keep in `machine/`):

```text
src/machine/c1541_media.h
src/machine/c1541_media.c     — rotation, head, SYNC, byte clock, track buffers
src/machine/c1541_gcr.h
src/machine/c1541_gcr.c       — 4/5 GCR tables, encode/decode sector, track layout
```

Keep `c1541.c` as ownership/wiring: cycle order, IEC, intercept policy, VIA step
hooks. Do not grow `c1541.c` into a 3k-line physics file.

### 3.2 State owned by `c1541` (sketch)

```c
typedef struct c1541_media {
    int      motor_on;           /* latched from via2 PB2 */
    int      motor_ready;        /* after spin-up cycles */
    uint32_t motor_spin_cycles;  /* countdown/up counter */
    int      half_track;         /* 2..80+; track = half_track/2 */
    uint8_t  stepper_phase;      /* last STP0/STP1 */
    int      density;            /* from DS0/DS1 */
    uint32_t bit_clock;          /* fractional or integer divider */
    uint32_t head_bit_pos;       /* bit index into current track */
    int      sync_level;         /* last PB7 level */
    int      byte_ready;         /* BYTE READY assertion */
    int      reading;            /* inferred from DDRA / mode */
    /* Track store: either pointers into mounted G64-derived buffers
       or synthesised GCR tracks owned by media. */
    c1541_track tracks[/* max half-tracks or 42 */];
    int      tracks_valid;
    int      dirty_track_mask[]; /* if G64/D64 write-back needed */
} c1541_media;
```

Exact sizes: prefer **42 half-tracks** (tracks 1–21 full steps with half-steps)
or a compact “current track buffer + cache of N tracks” if memory is a concern.
G64 can require per-track variable lengths up to ~8000+ bytes; allocate from
mount, not a giant fixed array if avoidable.

### 3.3 Data flow

```text
Host file (.d64 / .g64)
    → tools parser (new g64 + existing d64)
    → runtime mount copies bytes into c64_drive_slot
    → machine builds media view:
         D64  → synthesise standard GCR tracks on mount or on first access
         G64  → copy/adopt track dumps as-is
    → each c1541_advance_one_cycle:
         sample via2 outputs (motor/step/density/write data)
         advance rotation if motor_ready
         update SYNC + Port A input + BYTE READY/SO
         if write: mutate track buffer under head
    → on unmount/flush:
         G64 dirty tracks → tools encode → host file
         D64 path: either decode dirty tracks back to sectors, or keep a
         sector mirror updated on successful sector-framed writes
```

### 3.4 Cycle order (proposed)

Inside `c1541_advance_one_cycle()`:

```text
1. via6522_step(via1); via6522_step(via2); update SO from media BYTE READY
2. c1541_media_step(drive)     /* NEW: motor/head/rotation/SYNC/Port A */
3. c1541_update_iec_bus(drive) /* existing serial VIA */
4. optional job intercept only if media mode disabled
5. c6510_step when cpu_cycles_remaining == 0
```

`c1541_media_step` must run **after** VIA registers reflect this cycle’s CPU
writes from the previous instruction window, consistent with current VIA-before-CPU
ordering. Document and test edge cases carefully; do not invent a second
ordering without measuring against ROM job timing.

### 3.5 D64 synthesis (standard layout)

On mount of a 35-track D64 (or first media access):

For each track 1..35:

1. Choose gap sizes matching common “1541 standard format” (document constants;
   align with VICE/common G64-from-D64 practice).
2. For each sector 0..SPT-1:
   - Build header block (ID from BAM T18S0, track, sector, checksum)
   - SYNC + GCR(header) + header gap
   - SYNC + GCR(data block id + 256 bytes + checksum) + inter-sector gap
3. Store as circular bit or byte stream for that track.

IDs and BAM come from the D64 image itself. Empty/unformatted regions in a
partial image still need defined behaviour (zeros / no SYNC).

### 3.6 G64 mount

```text
tools/g64: parse header, track offsets, track lengths, optional speed zones
runtime: image_kind = C64_DRIVE_IMAGE_G64; store raw file or expanded tracks
machine: media attaches track pointers; no sector intercept required for reads
```

Write-back policy (choose in phase design, default conservative):

- **v1:** G64 read-only mount (writable=0) — enough for most copy-protected titles
- **v2:** dirty-track rewrite to host G64
- D64 write-back: decode modified tracks to 256-byte sectors when layout remains
  standard; if GCR is nonstandard, refuse D64 save and require G64

### 3.7 Job intercept retirement

When `[disk] media_1541=1` and tracks are valid:

| Job | Behaviour |
|---|---|
| READ / SEARCH / VERIFY | **Do not intercept** — ROM GCR path runs |
| WRITE | **Do not intercept** once Port A write path proven; else temporary hybrid |
| EXECUTE / FORMT | **Do not intercept** once format writes GCR tracks; then refresh D64 sector mirror if still mounted as D64 |

Keep intercept code compiled in behind the flag until a long soak period passes;
do not delete until acceptance tests are green for both modes.

### 3.8 Performance budget

Lockstep today: one `c1541_advance_one_cycle` per C64 cycle per drive (×2 drives).

Media step must be **O(1)** per cycle (advance bit clock, maybe one bit/byte).
Do not GCR-decode whole sectors on the hot path every cycle. Encode on write
completion / track synthesis only.

If profiling shows pain:

1. Step media only when motor_on (idle drives free)
2. Multi-bit batching when CPU is waiting (careful with VIA sampling)
3. Disable second drive when unmounted / no ROM

Do not break determinism for debugger step.

---

## 4. Phased implementation plan

Each phase should get its own `C64IEC1541MEDIA_PHASE_N.md` guide before coding,
following the project’s vertical-slice style. Phases below are the roadmap.

### Phase M0 — Scope open + measurement harness

**Purpose:** Make the quest legitimate and measurable without media physics yet.

- Maintainer marks quest in-scope for a future milestone; update `AGENTS.md`
  scope lists when that milestone starts (not before).
- Add optional drive instrumentation (debug-only): log via2 PB writes, PC in
  physical GCR routines, job queue without satisfying — or a “force physical”
  test switch that disables intercept to **observe hang points**.
- Capture baseline: stock `LOAD"*",8` time/cycles with intercept; list target
  fast loaders for later acceptance (start with 2–3 well-known ones, not twenty).

**Exit:** Written acceptance loader list + baseline numbers + flag sketch.

### Phase M1 — Disk-controller VIA mechanics (no GCR stream yet)

**Purpose:** Motor, stepper, WPS, density bits behave; SYNC can be stubbed high/low.

Implement in `c1541_media`:

- Sample via2 ORB/DDRB each media step
- Motor on/off + spin-up delay (constant, documented; e.g. ~ few hundred ms of
  drive cycles — tune later)
- Stepper: detect STP phase changes → ±1 half-track; clamp at head-stop
- Write protect input on PB4 from `slot->writable`
- Density from DS0/DS1 stored for later bit rate
- LED ignored or published to snapshot later

Unit tests:

- Step in/out changes half_track predictably
- Bang against track 1 stop does not underflow
- Motor off freezes head_bit_pos
- WPS follows writable mount

**Exit:** ROM init + motor spin no longer sees dead PB; still uses intercept for data.

### Phase M2 — GCR primitives + standard track synthesis from D64

**Purpose:** Correct encode/decode and a full standard disk layout in memory.

- GCR nibble tables (encode/decode)
- Header and data block builders
- `c1541_media_build_from_d64(slot)` → tracks[]
- Round-trip test: sector bytes → track GCR → decode → original 256 bytes
- Layout constants documented (gap sizes, sync length 10+ bits of 1s)

**Exit:** Property tests green; no VIA streaming required yet.

### Phase M3 — Rotation, SYNC, Port A read, BYTE READY

**Purpose:** Stock ROM physical READ works with intercept **off**.

- Bit clock per density zone (rates must match 1541 zone tables used by DOS)
- Advance `head_bit_pos` when motor_ready
- Assemble bits → bytes; present on via2 Port A when DDRA is input
- SYNC level on PB7 when current window is sync mark
- BYTE READY / SO: assert on complete byte; clear on Port A read (match hardware
  polarity and timing as closely as practical; document approximations)
- Disable READ/SEARCH/VERIFY intercept when media mode on

Acceptance:

- `LOAD"$",8` and `LOAD"*",8` via real ROM **without** job READ intercept
- Directory and PRG match intercept-mode results on a golden D64 (e.g. GALENCIA)
- Automated test: force media mode, load known PRG, check memory fingerprint

**Exit:** First true media-level read path.

### Phase M4 — Port A write + D64 dirty mirror

**Purpose:** Physical WRITE and format-adjacent writes mutate track GCR and persist.

- While DDRA output and motor on and not WPS: shift out ORA into track bits
- On sector-framed write completion (or continuous overwrite): mark track dirty
- D64 persistence strategy (pick one, implement fully):
  - **Preferred:** after each standard sector write decoded back to
    `image_bytes[offset]`, set `slot->dirty` (reuses runtime flush)
  - Alternative: full track decode on unmount only (harder to keep BAM coherent mid-session)
- WRITE job intercept off in media mode
- Write-protect still returns DOS 26 via real ROM sensing PB4

Acceptance:

- SAVE / sequential write / BAM update persist across remount
- Read-only mount cannot change `image_bytes`

**Exit:** Media mode feature-complete for stock DOS R/W.

### Phase M5 — Real format (FORMT) via GCR

**Purpose:** Remove synthetic `c1541_format_track` erase when media mode on.

- ROM FORMT EXECUTE runs against rotating blank/new track buffers
- After format, rebuild D64 sector image from tracks if still D64-backed
- Quick format vs full format fall out of real ROM behaviour

Acceptance:

- `N0:name,id` produces loadable empty disk with correct name/ID
- Media-mode format no longer needs EXECUTE intercept

**Exit:** Format fidelity at standard GCR layout level.

### Phase M6 — G64 tools + mount path

**Purpose:** Host G64 images.

- `src/tools/g64/` parser (header, tracks, lengths); unit tests with fixtures
- `C64_DRIVE_IMAGE_G64` on drive slot
- Runtime mount/unmount/UI extension (file browser filter, CLI `--disk`)
- Machine attaches tracks without D64 synthesis
- Read-only G64 sufficient for v1

Acceptance:

- Mount known G64; stock LOAD of a standard-formatted G64 works
- At least one nonstandard/protection G64 from the acceptance list progresses
  further than D64-only ever could

**Exit:** G64 is a first-class read image type.

### Phase M7 — Fast-loader / nonstandard compatibility pass

**Purpose:** Turn “unvalidated” into a documented matrix.

For each title/loader on the M0 list:

| Result | Action |
|---|---|
| Works | Add automated or scripted smoke where possible |
| Fails on timing | Measure SYNC/BYTE READY; fix media clock or SO polarity |
| Fails on half-track / custom GCR | Needs G64 of original; D64 synthesis will never suffice |
| Needs 1571 / parallel / special ROM | Explicitly out of scope; document |

Improve only mechanisms that help multiple titles (clock, SYNC width, spin-up,
half-track). Avoid one-off hacks per game.

**Exit:** Compatibility matrix checked into `docs/status/IEC1541.md` or TESTING.

### Phase M8 — Hardening, performance, docs, intercept removal

- Profile dual-drive media step; apply O(1) idle elision
- Debugger: show half-track, motor, sync, head pos in hardware view (optional)
- Save-state: include media struct + track dirtiness (or document as deferred if
  track buffers too large — prefer include)
- Default: media mode on when `emulate_1541=1` **or** keep opt-in until soak
- Remove dead intercept paths only after dual-mode soak
- Update README / manual: G64, media flag, limits

**Exit:** Quest complete per §2.1; deferred list rewritten.

---

## 5. Testing strategy

### 5.1 Automated (required)

```text
tests/machine/test_c1541_gcr.c
  - nibble encode/decode all 16 values
  - sector round-trip
  - header checksum vector

tests/machine/test_c1541_media.c
  - stepper / head-stop
  - motor freezes rotation
  - SYNC detects synthetic sync run
  - BYTE READY asserts once per byte
  - WPS polarity

tests/tools/test_g64.c
  - parse fixture; reject truncated; track length bounds

tests/machine/test_c1541.c (extend)
  - media mode LOAD fingerprint vs intercept mode
  - media mode WRITE + remount
```

### 5.2 Integration / human smoke

```text
- emulate_1541=1, media=1, D64: LOAD"$",8 / LOAD"*",8 / RUN golden title
- SAVE + quit + reload
- N0:name,id then SAVE and LOAD
- G64 mount of standard dump
- One known fast loader from matrix
- emulate_1541=0 still uses KERNAL trap (no regression)
- media=0 still uses job intercept (bisect safety)
```

### 5.3 Non-regression

All existing `test_c1541`, IEC, D64, autorun-with-1541 tests must pass with
**default** config remaining intercept or with media proving identical outcomes
for stock paths.

---

## 6. Risks and open design choices

| Risk / choice | Notes | Recommendation |
|---|---|---|
| BIT vs BYTE track storage | Bits are accurate for partial bytes; bytes+bit index are faster | Byte buffer + bit index into current byte is enough for v1 |
| SO / BYTE READY exact wiring | Hardware details easy to get wrong; ROM is picky | Implement against ROM physical read loop; golden-test cycle counts loosely |
| Spin-up time | Too short → unrealistic; too long → slow tests | Configurable constant; short default for tests |
| Half-tracks | Needed for some protections | Model half-track index from the start (cheap) |
| D64 write-back after GCR mutate | Nonstandard GCR cannot map to D64 | Refuse or force G64 save |
| Dual drive cost | 2× media step every C64 cycle | Idle short-circuit when motor off / no ROM |
| Intercept deletion timing | Early deletion breaks bisect | Flag for ≥1 release |
| NTSC drive clock | Status notes PAL 1:1 only; real 1541 ~1 MHz region | Keep 1:1 with host C64 clock until a clock-ratio phase; document error |
| VIA SR unused | 1541 GCR path is Port A + external shift logic abstraction | Do not require full 6522 SR for v1 unless tests demand it |
| Scope creep into “VICE parity” | Bottomless | Acceptance matrix is the finish line, not infinite titles |

### Open questions for maintainer before M1 coding

1. Default: media opt-in flag forever, or become the default when `emulate_1541=1`?
2. G64 write-back in v1 or read-only G64 first?
3. Save-state must include full track buffers in the first media ship, or may defer?
4. Which 3–5 fast loaders / titles define the M7 acceptance set?
5. Is a separate milestone name required (post PAL/NTSC fidelity), and should
   `AGENTS.md` gain an explicit “Media 1541” milestone block when opened?

---

## 7. File / touch map (expected)

| Area | Files |
|---|---|
| Media core | `src/machine/c1541_media.c/.h`, `c1541_gcr.c/.h` |
| Drive wiring | `src/machine/c1541.c/.h` |
| VIA (only if SR/CA2/CB gaps appear) | `src/machine/via6522.c/.h` |
| Slot / image kind | `src/machine/c64.h`, mount paths in `c64.c` |
| G64 tools | `src/tools/g64/*` (new) |
| Runtime mount/flush | `src/runtime/*disk*`, flush dirty G64/D64 |
| Config / INI | `app_options`, `[disk] media_1541`, browser filters |
| Frontend | disk UI kind display; optional hardware view fields |
| Tests | `tests/machine/test_c1541*.c`, `tests/tools/test_g64.c` |
| Docs | `docs/status/IEC1541.md`, `DISK_IO.md`, `DEFERRED.md`, `TESTING.md`, `manual/manual.md`, `README.md` |

Dependency direction reminder:

```text
tools/g64  → util
machine    → may use tools only if MASTER allows machine→tools (it does)
runtime    → machine + tools for mount helpers
frontend   → runtime_client only
```

---

## 8. Suggested acceptance criteria (quest-level)

```text
1. With media mode enabled and intercept physical READ/WRITE disabled:
   - Standard D64 LOAD"$" and LOAD"*",8 match prior intercept results.
   - Writable D64 SAVE persists and reloads.
2. Disk-controller VIA shows live SYNC while motor is on over formatted tracks.
3. Stepper + motor behaviour is unit-tested; head cannot step below track 1 stop.
4. At least one G64 image mounts and yields a correct stock load when content is
   standard-formatted; G64 path is documented.
5. Documented fast-loader acceptance set: each entry pass/fail with reason;
   no silent “unknown”.
6. emulate_1541=0 and media=0 paths do not regress existing tests.
7. Architecture, thread, and snapshot rules remain intact.
8. STATUS + IEC1541 + DEFERRED + TESTING + manual updated; intercept described
   as legacy compatibility path or removed after soak.
```

---

## 9. Relation to current milestone

```text
Current milestone (AGENTS.md):
  Optional real 1541 ROM/IEC with job-intercepted sector R/W — DONE through Phase 5.
  Media-level GCR/rotation/SYNC/motor/head/G64/fast loaders — OUT OF SCOPE.

This document:
  Evaluation of the gap + plan for a future quest/milestone.
  Does not authorize implementation work by itself.
```

When the quest is opened, create phase guides (`C64IEC1541MEDIA_PHASE_1.md`, …)
from §4, implement only one phase at a time, and update status handoff files at
each phase exit — same workflow as IEC Phases 1–5.

---

## 10. Summary

The current 1541 is a **real DOS/IEC computer** sitting on a **fake disk**. Job
intercept against D64 sectors was the correct way to finish the present
milestone. Real media-level emulation is a **new subsystem**: GCR track store,
rotation clock, disk-controller VIA (`via2` @ `$1C00`), SYNC/BYTE READY, then
G64 and a deliberate fast-loader matrix — with the intercept retired behind a
flag once stock ROM paths run on the physical model.

Recommended first implementation slice after scope-open: **M1 mechanics + M2 GCR
synthesis + M3 streaming read**, proving stock `LOAD` without READ intercept,
before investing in G64 UI or wide loader compatibility.
