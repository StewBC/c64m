# Edge of Disgrace investigation handoff

Operational handoff for Edge of Disgrace (EoD) visual work. Two mid/late scenes
are coupled through VIC-II VC/RC/`$D011` timing and must not be fixed in isolation.

**This handoff file is untracked.** Update it in place; do not commit unless asked.

Legend used below:

| Tag | Meaning |
| --- | --- |
| **CONCRETE** | Measured in c64m, VICE, pixel compare, or unit tests this session |
| **HYPOTHESIS** | Plausible model not fully proven for *both* scenes |
| **FALSE LEAD** | Tried; disproven or harmful as a sole fix |

---

## Where we are (2026-07-18 EOD)

### Final regression sweep / capture correction

The original `eod_regression_capture` results were not live VIC-II output.
`runtime_client_request_frame()` publishes the geometric debug reconstruction,
and turbo mode 3 (warp) intentionally disables the cycle renderer. Those
frames reproduced the sparse/corrupt appearance in the reported screenshots
even though the live framebuffer was coherent.

The capture tool now drains the frame slot, runs at turbo 2 (max), consumes each
`FRAME_READY` payload immediately, and can sample multiple separated frames at
the checker, plasma, or an arbitrary frame offset. A live sweep through roughly
frame 35,000 after boot verified the checker/fist/star/plasma, face, eod-3,
sister, tile scenes, woman/android scroll, and eye scene. Representative files
from this session are `c64m-checker-live16.png`,
`c64m-plasma-live60.png`, `c64m-face-live.png`,
`c64m-eod3-live.png`, `c64m-sister-live.png`,
`eod-late-sheet.png`, and `eod-far-sheet.png`.

The eye scene's complete `FLIP` and `DISK` labels were present in the raw
384x312 frame; the frontend's tight 352x248 crop clipped them. PAL now uses the
normal-border 384x272 viewport (rows 20..291), which displays both labels fully
and keeps the same near-4:3 PAR-corrected shape. NTSC retains its prior crop.

The VICE NTSC sprite fetch slots were also corrected to zero-based cycles
`58,60,62,64,1,3,5,7`. PAL EoD output is unaffected. Sprite BA tests now assert
the live VICE per-cycle masks/RDY pin rather than the obsolete persistent
six-cycle-window abstraction. Final `ctest`: **51/51**.

### Resolution in the current working tree

**CONCRETE:** the 13↔14 ping-pong was a symptom of two missing pieces, not a
real VIC-II ambiguity:

1. c64m checked cached BA/AEC from the preceding cycle before beginning the
   current VIC cycle. This placed the face IRQ's `$D011` store seven raster
   cycles later than VICE and made cycle-14 UpdateVc look necessary.
2. c64m bulk-filled forty c-data entries and addressed g-data as `VCBASE+col`.
   It had no VMLI and could not reproduce VICE's interleaved FetchG/FetchC
   counter state. The `matrix_d018_scr` reload was compensating for that gap.

The hardware-shaped implementation now does:

- `vicii_begin_cycle` (VIC Phi1/internal work and current BA/AEC), then CPU
  Phi2, then `vicii_finish_cycle` (raster advance and delayed `$D011` latch).
- UpdateVc at zero-based cycle **13**: `VC=VCBASE`, `VMLI=0`, badline RC clear.
- c-accesses at **14..53**; g-accesses at **15..54**. Each display g-access uses
  current VC/VMLI and then increments them; the following c-access fills the
  next VMLI slot.
- UpdateRc at zero-based cycle **57**; no end-of-line `VC += 40` shortcut.
- badline BA lead starts at **11**, final c-access AEC is cycle **53**, CPU
  resumes at **54**; the old badline RELEASE=3 workaround is gone.
- no bulk line fill and no `$D018` matrix-reload special case.

**CONCRETE VICE/c64m phase trace:** face stores are early (VICE `$D011` C7), so
cycle-13 UpdateVc observes them. On eod-3 R103, UpdateVc observes old `$5B`, and
the CPU's same-cycle store `$5B→$5F` occurs afterward; it cannot clear RC for
that UpdateVc. One live badline/YSCROLL source now works for both scenes.

**CONCRETE visual verification from one clean disk-1A run:** face/3D animation
is coherent; eod-3's two rainbow bands stay inside the VICE-shaped black frame;
the following sister imagery renders and transitions onward; early star,
checker/plasma/right-edge/bottom scenes also render without the old spill.
Captures are under `eod-new-face.png`,
`eod-new-eod3-sheet.png`, `eod-new-sister-sheet.png`,
and `eod-new-early-sheet.png` for this session.

`ctest --test-dir build --output-on-failure` is **51/51**. New tests pin the
same-cycle UpdateVc/CPU-write distinction, VC/VMLI FetchG→FetchC order, current
BA/AEC schedule, CPU stall/resume cycles, and snapshot state.

### Branch / commits (HEAD)

```text
0fe7ba2  Model VIC-II Phi1 and video counters per cycle       [JOINT FIX]
2c1a455  Run VIC before CPU bus events each cycle (Phi1-before-Phi2)
db06b58  Restore EoD face/3D band by keeping UpdateVc at cycle 14
6f11e98  Defer VIC line latch to cycle 14 after UpdateVc RC-clear   [FALSE LEAD]
5ad36bf  Fix eod-3 FLI VCBASE via UpdateVc timing and delayed YSCROLL
b400a9e  … earlier main (face OK; eod-3 FLI VCBASE broken)
```

Working tree: clean for tracked files except this untracked handoff (and any
local samples). **`ctest` → 51/51** on `0fe7ba2`.

### Scene map

| Scene | Wall-clock after disk1A @ turbo=1 | Approx frame after swap | Status on HEAD |
| --- | --- | --- | --- |
| **Face / 3D band** (“reg”) | **~2m10s** | ~11200 | **Face structure OK** (coherent band shape vs pre-`5ad36bf`) |
| eod-1 checker | earlier | ~7258 (`$A3BD`) | Must not regress (on `main` baseline) |
| eod-2 plasma | earlier | ~9240 (`$1000`) | Must not regress |
| **eod-3 booze/box** | **~3m35s** | ~15450 | **Contained and animated like VICE** |
| Sister after eod-3 | after eod-3 | ~19500 | **Rendered and advanced normally** |

### Joint-fix status (blunt)

**The current working tree jointly satisfies face + eod-3 + sister + eod-1/2.**

Ping-pong observed this session (all **CONCRETE** via pixel/metrics):

| Config | Face | eod-3 FLI (VCBASE / structure) |
| --- | --- | --- |
| `b400a9e` (pre eod-3 VIC work) | good | bad (stuck VCBASE class) |
| `5ad36bf` UpdateVc@**13** + delayed YSCROLL RC-clear + `g_line` + matrix D018 | **garbage** | good (linelog VCBASE steps) |
| `6f11e98` latch-defer only (still @13 + delayed RC) | still garbage (agent misread noise as shape) | kept eod-3 class |
| `db06b58` UpdateVc@**14** + live RC + keep `g_line`/matrix | **good** (vs pre pixel-exact on stable samples) | weaker again |
| `2c1a455` + `db06b58` VIC: VIC-before-CPU, UpdateVc still **14** | good (with order change) | not jointly closed |

**Do not** flip UpdateVc 13↔14 again without comparing **both** oracle PNG sets.

---

## Oracle assets (use these, not agent “avgrowdiff” alone)

### Face / 3D band (~2m10s)

| File | Role |
| --- | --- |
| `eod-3b-reg-vice.png` | **VICE oracle** — face + mid-screen band; geometric content animates / rotates |
| `eod-3a-reg-c64m.png` | **Broken c64m** (user) — eyes OK; **lower face destroyed**; band not a coherent shape |
| `eod-3b-reg-c64m.png` | **Broken c64m** (user) — band = noise soup, not a rotating object |

**CONCRETE visual bar for “face fixed”:**

- Eyes + **mouth** visible outside the band.
- Band contains a **frame-to-frame coherent** geometric object (cube/gear class), not full-width horizontal stripe garbage and not random multicolour noise covering the lower face.
- Host screenshots are scaled; use for **structure**, not RGB hash.

**FALSE LEAD:** treating high-frequency stripe/noise bands as “a shape” from thumbnail metrics alone.

### eod-3 booze/box (~3m35s)

| File | Role |
| --- | --- |
| `eod-3-vice.png` | VICE oracle — black frame, “booze”, animation contained |
| `eod-3.png` | Old buggy c64m host shot (spill / corruption) |

**CONCRETE bar for eod-3:** black rectangular frame, animation **inside**, no gross outside spill; VCBASE should advance on the FLI strip (see linelog). Sister scene after eod-3 still open.

---

## First principles (hardware / VICE)

### CONCRETE (from VICE `src/viciisc/`)

- Cycle index: `VICII_PAL_CYCLE(c) = (c) - 1` → PAL cycle 14 is **0-based cycle 13**.
- UpdateVc is scheduled on **Phi2(14)** in `vicii-chip-model.c` (with BaFetch).
- `check_badline()` uses `vicii.ysmooth` (`(raster_line & 7) == ysmooth`).
- UpdateVc: `vc = vcbase`; if `bad_line` then `rc = 0` (`vicii-cycle.c`).
- `$D011` store sets `ysmooth = value & 7` and `regs[0x11]` (`d011_store`).
- **Instrumented compare (prior session, still trusted):** on eod-3 FLI band R103,
  UpdateVc sees `$D011=$5B` (bad=0, RC=4, VCBASE `$0F0`); same-cycle store
  `$5B→$5F` is **after** that sample; VCBASE advances `$0F0→$118→…→$230`.
- Pre-`5ad36bf` c64m: `$5F` visible at UpdateVc on `(raster&7)==7` → RC forced 0
  every 4 lines → **VCBASE stuck `$0F0`**.

### RESOLVED (phase order and sequencer)

- Real machine: Phi1 VIC samples, Phi2 CPU may store. Same-cycle `$D011` does not
  feed that cycle’s UpdateVc/check_badline sample.
- c64m **used to** apply deferred/micro **CPU bus events before** VIC advance →
  same-cycle STA `$D011` already in live regs when UpdateVc ran (explains eod-3
  stuck VCBASE under UpdateVc@14 without any delay).
- VIC-before-CPU alone was insufficient because the CPU stall decision still
  used prior-cycle VIC pins, shifting the face loop, and the VIC still used a
  bulk matrix model. Current-cycle BA/AEC plus VMLI-accurate FetchG/FetchC makes
  UpdateVc at VICE index **13** work with one live YSCROLL sample for BA + RC.

---

## Experiment matrix (this session)

All “face” judgments require looking at PNG structure (eyes/mouth/band), not only
`avgrowdiff` / unique-colour counts (those can score stripe garbage as “ok”).

| Experiment | Concrete result |
| --- | --- |
| A: UpdateVc@**14** + **live** RC + keep `g_line` + matrix D018 reload | Face **pixel-matched** pre-`5ad36bf` on early locked samples (`vs_pre=0.00`). |
| H: pre-`5ad36bf` + **only** cycle 14→13 (live RC, latch@13, no g_line) | Face **broken** (stripes). → cycle **13 alone** is enough to hurt face. |
| F: UpdateVc@**14** + **delayed** RC only | Face **broken**. → delayed RC at 14 hurts face. |
| E / C: @13 + live RC (variants) | Face **broken**. |
| G: @13 + **unified** `reg11_delay` for all badline (BA+RC) | Face broken; later free-run hit **`stop=brk`** (demo desync). → **do not** delay BA with CPU-before-VIC. |
| I / latch-defer: @13 + delayed RC + bulk latch@14 | Metrics looked “better”; visuals still not pre-level; agent overclaimed. **FALSE LEAD** as complete fix. |
| Surgical same-cycle arm filter @14 | Broke face (face needs same-cycle RC arm under old CPU-before-VIC). |
| Deferred `$D011` apply until end of VIC step | Broke snapshot tests (write without step never flushes); unit failures until reverted. |
| Unified Phi1 YSCROLL for BA | **BRK** at ~frame 10510 after disk1A (concrete crash path). |
| **VIC devices before CPU events** (`2c1a455`) + UpdateVc@**14** | Face **good** structure (coherent band shape). Tests 51/51. |
| Same order + UpdateVc@**13** + latch@14 | Face **still garbage** (avgrowdiff ~50–136, stripes). → not only `$D011` visibility. |

### What is still HYPOTHESIS about face@13

Why face dies when UpdateVc is on cycle 13 (even with VIC-before-CPU and latch@14):

- **HYPOTHESIS:** bulk video-matrix latch vs VICE per-cycle FetchC / missing VMLI
  interacts with RC/VC phase at 13.
- **HYPOTHESIS:** BA release / badline window edge relative to cycle 13 vs 14
  shifts IRQ/raster code for the 3D writer.
- **HYPOTHESIS:** remaining half-cycle mismatch (paint/g-fetch still not split
  Phi1/Phi2 inside `vicii_step_cycle`).

Not proven; next work should instrument VC/RC/BA/`$D011` on face band rasters
under UpdateVc@13 + VIC-before-CPU vs VICE, not flip knobs blindly.

---

## Current implementation (concrete code)

### Split VIC cycle (`src/machine/c64.c`, `src/machine/vicii.c`)

- `c64_begin_vic_for_current_cycle` establishes this cycle's VIC state and pins.
- A between-instruction CPU interrupt poll happens before CIA/SID advance; this
  preserves the CIA IRQ pipeline while still using current VIC BA/AEC.
- CPU micro/deferred bus work runs in the same raster cycle.
- `vicii_finish_cycle` captures `reg11_delay` after CPU Phi2 and advances raster.
- Direct VIC tests retain `vicii_step_cycle` as begin+finish convenience.

### VC/VMLI and bus sequence

- UpdateVc 13, c-access 14..53, g-access 15..54, UpdateRc 57.
- FetchG uses current VC/current VMLI, increments both, then same-cycle FetchC
  uses the incremented pair. Cycle 14 fills slot zero before the first FetchG.
- VCBASE is copied from the forty per-cycle VC advances when RC reaches 7.
- `g_line`, `video_matrix`, `color_line`, `vmli`, and `reg11_delay` are serialized;
  snapshot format is version 7.

## Previous committed state / investigation history

### `2c1a455` — machine order (`src/machine/c64.c`)

**CONCRETE:**

- Split `c64_advance_one_cycle` into:
  - `c64_step_devices_for_current_cycle` (VIC/CIA/SID, no `clock.cycle++`)
  - `c64_finish_cycle` (`clock.cycle++`, drive sync)
- Deferred CPU path: **devices → apply pending bus events → finish cycle**
- Micro path: **devices → `c6510_micro_step` → finish cycle**
- BA stall path still advances devices as before.

Intent: VIC UpdateVc/badline for cycle *N* see `$D011` **before** that cycle’s
CPU store (Phi1-before-Phi2 approximation).

**Limit (CONCRETE):** injection harness in `test_c64_vicii.c` still does
`write_register` then `vicii_step_cycle` (not the c64 deferred order). Unit
tests do not fully cover demo CPU ordering.

### `db06b58` VIC behaviour (still in tree under `2c1a455`)

**CONCRETE:**

- `VICII_VC_RC_CYCLE = 14` (not VICE’s 13)
- RC-clear uses **live** badline / live YSCROLL
- `g_line[]` g-fetch latch with `reg11_delay` for BMM/ECM address bits
- Non-badline matrix reload when `$D018` screen base changes **and** BMM
  (`matrix_d018_scr`) — eod-3 FLI→bitmap colour; eod-2-sensitive if broadened

### `5ad36bf` (historical — do not re-apply whole as sole “eod-3 fix”)

**CONCRETE effects:**

- UpdateVc → 13 + RC-clear from `reg11_delay` fixed FLI linelog VCBASE vs VICE
- **Also** destroyed face scene (user `eod-3*-reg-c64m.png`)

### `6f11e98` (FALSE LEAD)

Claimed face fix by deferring bulk latch only. Did **not** restore face to oracle
structure. Superseded by `db06b58` analysis.

---

## How to park / measure (operational)

### Media

```text
Disk 0:  EdgeOfDisgrace_0.d64
Disk 1A: EdgeOfDisgrace_1a.d64
c64m:    build/c64m
VICE:    /Applications/vice-arm64-gtk3-3.10/bin/x64sc
VICE src:/Users/swessels/Develop/svm/vice-emu-code/vice   # use src/viciisc/
```

Artifacts only under `/private/tmp/`. Client: `tools/c64_control_client.py`.

### Turbo

| Turbo | Framebuffer |
| --- | --- |
| 1 (normal) | Live — real-time |
| 2 (max) | Live free-run — required for visual truth |
| 3 (warp) | Debug geometry only; race only; after warp, step ≥1 frame at max/normal before trusting `get-frame` |

### Markers

| Addr | Scene | Notes |
| --- | --- | --- |
| `$020C` | all | Disk swap wait; ~frame 4701; mount 1A here |
| `$A3BD` | eod-1 only | Clear before later scenes |
| `$1000` | eod-2 | Plasma IRQ ~9240 |
| `$0E2C` | face-era | IRQ vector seen at face scene (not a durable park yet) |
| eod-3 | none solid | Free-run ~3m35s @ t=1 or ~frame 15450 after swap |

### Race recipe

```text
turbo 3 (warp) → break $020C → mount 1A → turbo 2 (max) or 1 → free-run
face:  ~6500 frames after swap (~2m10s @ t=1)
eod-3: ~10750 frames after swap (~3m35s @ t=1)
```

Judge with PNG structure + optional pixel-diff to pre golden
(`/private/tmp/eod-cmp-pre/` if still present from this session).

### Linelog (eod-3 FLI)

```text
C64M_LINELOG=/private/tmp/eod3-linelog.txt \
C64M_LINELOG_F0=15440 C64M_LINELOG_F1=15450 \
C64M_LINELOG_R0=99 C64M_LINELOG_R1=170
```

**CONCRETE** (on face-safe UpdateVc@14 + g_line/matrix, prior run): R99–R170 showed
VCBASE `$0F0…$230` (9 steps). Treat as “not stuck”, **not** full scene sign-off
(containment/sister still open; user visual bar is higher).

### VICE oracle notes

- Use **viciisc**; instrumented UpdateVc/`d011_store` logs were temporary and
  **reverted** after prior session.
- `screenshot "path" 2` = PNG.
- Compare structure/colour **classes**, not full-image hashes.

---

## Preservation checklist (before claiming done)

- [x] Face @ ~2m10s matches VICE structure (not stripe/noise band) — check
      `eod-3b-reg-vice.png` / user eye
- [x] eod-3 booze/box: containment + animation inside black frame vs VICE
- [x] Sister effect after eod-3
- [x] eod-1 checker, eod-2 right edge + solid bottom, lft-nine tests
- [x] `ctest --test-dir build --output-on-failure` → 51/51
- [x] VICE UpdateVc index 13 with one live YSCROLL/badline model

---

## Follow-up

1. Keep the split begin/CPU/finish cycle and VMLI sequencing together; reverting
   either half recreates the 13↔14 scene ping-pong.
2. User visual review remains the final bar for any subtle EoD phase not sampled
   by the captured sequences.
3. If another demo exposes a bus issue, compare current-cycle BA/AEC and the
   Phi1 FetchG/Phi2 FetchC order against VICE before adding a release or reload
   workaround.

---

## Older fixed scenes (do not undo)

| Commit | What |
| --- | --- |
| `3d2154f` | VIC-II side-border timing |
| `349f293` | eod-1 checker: DEN not live blank; live VC/RC while DEN low |
| `3d0d554` | eod-2: no late mid-line c-latch col39 overwrite |
| `fc5a58b` | eod-2: invalid-mode idle solid black |
| `b400a9e` | `set-turbo` control command |

Checker rules still load-bearing:

1. DEN is not a live graphics blank.
2. Live path keeps live display_state / VCBASE / RC while DEN is low.

### eod-1 / eod-2 park (regression)

- eod-1: `$A3BD` after disk1A; phase `$A487/$A488`; clear `$A3BD` after scene.
- eod-2: `$1000`; right black x≈336–337; outer green; bottom solid black.

### Operational traps

- Kill with `lsof -t -iTCP:PORT`, not `pkill -f c64m` (self-match risk on
  command lines that embed the port/path string).
- One control client; `get-memory` length decimal, max 1024.
- `wait-frame` is a **delta**.
- turbo 3 (warp) frames are not visual truth.
- Do not trust unit tests over VICE when they disagree; update tests only when
  VICE-justified.

---

## Agents

| Doc | Why |
| --- | --- |
| `agents/README.md` | Project map |
| `agents/vicii.md` | VIC rules (must track source) |
| `agents/control-port.md` | Protocol / `set-turbo` |
| `agents/vice-oracle.md` | VICE traps |

---

## End-of-session summary for the next agent

1. The joint fix is the combination of **current-cycle VIC arbitration**, the
   **begin/CPU/finish phase split**, and **per-cycle VC/VMLI FetchG/FetchC**.
2. UpdateVc is now VICE-aligned at **13** without delayed-YSCROLL or `$D018`
   special cases. UpdateRc is **57**; c/g windows are **14..53 / 15..54**.
3. Face, eod-3 containment, sister, and early EoD scenes were captured from one
   clean run. The full suite is **51/51**.
4. The old face regression under cycle 13 was real; its cause was the remaining
   stale-pin CPU timing plus bulk matrix model, not the cycle index itself.

---

## Left-edge checker discontinuity (2026-07-19 session)

### Repro
`eod_debug.ini` + autorun, turbo 3 (warp) to disk1A auto-swap and `$A3BD`, then
live turbo 2 (max) or 1. Fine checker samples: `eod-4a-c64m.png` /
`eod-4b-c64m.png`. VICE oracle for structure: `eod-1-vice.png` (clean).
Headless: `eod_regression_capture … /tmp/out.ppm 8 checker 4 25`.

### CONCRETE structure of the bug (live 384×312)
- **Left open border is sprites** (spr6 @ X=0; colours vary by animation phase,
  e.g. 14/10 or 2/6).
- **Display matrix** is character mode with a `~code` charset and matching
  bit stream (`94 b5 a5` / `5a 52 d6` family).
- Baseline (pre-fix) fine checker: **mono column at x=24** (solid colour on
  every row — the visible vertical line), plus moving double-pixel lattice
  (intentional soft-scroll pattern).
- Top black bar: x=0 still previous colour (1px `$D021` color_latency);
  bottom: x=0 still black one row late. Hint only; not the main seam.

### Root cause (**CONCRETE**)
The visible line is a **mono column at x=24**: every checker row is the same
colour there (palette index 2), while other columns are ~50/50.

EoD opens the side border with `$D016=$62` (CSEL=0, **XSCROLL=2**). That
XSCROLL=2 must not pad the next line’s first matrix pixels. When it does,
x=24 becomes a forced `$D021` B0C pad — a solid vertical stripe when `$D021`
is a checker colour.

VICE samples `xscroll_pipe` only at the end of g-access draw cycles (pre-CPU
register). The cycle-56 dodge write is not sampled into the pipe for the next
line’s first cell. c64m was applying XSCROLL=2 into that first cell (pipe
sampled too late/broadly).

**Bar for fixed:** no mono column at x=24; `ones@24 ≈ 50%`; seam 23/24 = 0.
Do **not** use `same_left@24` alone (misses a solid mono column).

### Experiments
| Change | Mono x=24 | Seam | Notes |
| --- | --- | --- | --- |
| g phase +2 | metric only | — | Broke glyph@24 |
| Line-latched XSCROLL@14 | global mess | — | **FALSE LEAD** |
| Pipe sample every cycle post-CPU | still mono | — | Captured `$62` |
| Force XSCROLL=0 | **gone** | 0/193 | Proved pad was the cause |
| **Pipe sample g-cycles 15..54 only, pre-CPU** | **gone** | 0/193 | **THE fix** |

### CONCRETE fix landed
- `xscroll_pipe` updated in `vicii_finish_cycle` **after** the CPU store, and
  **only** on g-access cycles 15..54.
  - Not before CPU (misses same-cycle restore).
  - Not on cycles 0..14 (would pick up still-live previous-line `$62`).
  - Not on cycles 55+ (border-dodge `$62` must not pad next line).
- Live matrix/idle use the pipe; snapshot uses live `$D016`.
- Unit test `test_open_border_sprite_matrix_checker_joins`.
- Rebuild `./build/c64m` for interactive checks.

### Metrics after fix (`eod-v3` samples)
- mono at x=24: **none** on all fine frames (was 193/193 solid)
- seam 23/24: **0/193**
- ones@24: **~96/193 (~50%)**
- Moving doubles (pattern lattice) remain — intentional soft-scroll structure.
- Matches force-XSCROLL=0 experiment without breaking ghost XSCROLL tests.

### Top/bottom black bar x=0 stub (2026-07-19, fixed)
**CONCRETE:** native checker frames showed `y=50: 4 then all 0` and
`y=247: 0 then all 4` — first painted column kept the previous `$D020` for one
pixel. VICE has a solid black line. Root cause: `color_pipe_d020/d021` advanced
only on painted pixels; HBLANK dots never drained the 1px 6569 color_latency.
**Fix:** advance color pipes once per VIC dot every cycle (including HBLANK).
Unit test: `test_color_latency_drains_during_hblank`. Mid-line latency tests
unchanged.

### Plasma black blocks in the opened side border (2026-07-20, **FIXED**)

**Symptom (user, annotated):**
- `eod-plasma-c64m-black.png` — pure black **blocks** at the sides of the
  booze plasma; hard vertical stop.
- `eod-plasma-vice-no-black.png` — same region smooth, **0** pure black.

Reach the scene with the new capture mode (`@N` races N frames from the depack
marker, skipping the checker wait); the scene is frame ~10475:

```text
./build/eod_regression_capture roms/system.rom roms/character.rom roms/1541.rom \
  EdgeOfDisgrace_0.d64 EdgeOfDisgrace_1a.d64 \
  /private/tmp/eodp.ppm 4 @5600 3 60
```

**The old "freecolor $8" story was wrong.** `C64M_LINELOG_FULL=1` shows the
colour latch is **`$B` on all 40 columns** (free index 3 = cyan), and `$D021/2/3`
are constant `$0C/$08/$0D` for the whole field. The four plasma colours are
B0C/B1C/B2C + cyan; the animation is pure `$D018` charset switching with `rc=7`
frozen and no bad lines. No cell can produce a black freecolor.

**Root cause (CONCRETE).** The black was never in the field. A black histogram
puts every black pixel at **x=0..23 and x=344..383** — the *opened side border*
(`$D015=$F0`, sprites 4..7 multicolor, spr6 at X=0). The border flip-flop is
innocent (`mbff=0`, identical CSEL trace on black and clean rows). What differed
is the **background under the sprites' transparent pixels**: c64m painted the
`$3FFF` ghost byte there, whose set bits decode to colour 0 = black.

VICE has no graphics data in that region at all. No g-access loads the sequencer
outside cycles 15..54, so `vicii-draw-cycle.c` does `gbuf_pipe0_reg = 0` when the
cycle is not visible; `vicii_fetch_idle()` reads `$3FFF` for the bus but, unlike
`vicii_fetch_idle_gfx()`, never assigns `gbuf`. Every pair is 00 → `COL_D021`, so
the opened border is flat B0C (grey 12 here) — which is one of the plasma colours,
hence "smooth" in the VICE shot.

**Fix landed:** `vicii_border_gfx_pixel()` in `src/machine/vicii.c` replaces the
idle-ghost call for x outside `[24,344)`; pair-0 colour per mode (B0C for
hires/MCM text and MCM bitmap, vbuf low nibble for standard bitmap,
`$D021 + (vbuf>>6)` for ECM text, black for invalid). vbuf is taken from the
retained last display column, matching VICE. See `agents/vicii.md`.

**Verification:** plasma black in field+border **3.96% → 0.00%** across three
sampled frames; checker frame 7623 and lft-nine frames 2000/4000/6000/9000/
12000/16000 are **pixel-identical** in the over-border columns before vs after
(the change is unreachable for x in `[24,344)`). `ctest` **51/51**.

**Tests updated (VICE-justified):** `test_live_side_border_shows_zero_graphics`
and `test_live_open_border_ignores_xscroll` replace the two ghost-byte
shine-through tests, which asserted c64m's own model rather than VICE's.
`test_live_mcm_idle_ghost_is_hires` still pins the idle hires/MCM decode but now
probes **inside** the window (YSCROLL=7 leaves rasters 51..54 idle with the
vertical border already open) instead of in the border.

**FALSE LEADS:** freecolor `$8`/`color&7` masking; 1-cycle hborder; full 4-bit
freecolor (`$A`→pink); free-0→B0C paint hack; permanent edge colour force `$0A`.
All of these chased a field-colour bug that did not exist.

### Still open
- Do not flip UpdateVc 13↔14 for display bugs.
- Nothing outstanding on the plasma/side-border path.

### Helmet portrait left-margin stripes (2026-07-20, **FIXED**)

**Symptom (user):** after disk1A, turbo=1 free-run to wall ~4m55s. c64m shows
alternating blue/gray horizontal bars left of the black picture frame
(`eod-stripes-c64m.png`). VICE is uniform blue border
(`eod-stripes-none-vice.png`).

**CONCRETE geometry (live 384×312 @ ~frame 19570 / `@14700` from depack):**
- x=0..23 solid `$D020=$06` (main border OK).
- x=24..45: **4-line** runs of blue `$06` vs light gray `$0F` (not every raster).
- x≥46 black frame + picture; right side solid blue.
- Gray is palette `$0F`, **not** `$D021` (D021=0 black).

**Root cause (CONCRETE):** classic late-badline / FLI BA-lead path. On RC=4
lines the demo writes `$D011=$3F` (YSCROLL=7) and `$D018=$38` after UpdateVc,
creating a badline without the three-cycle BA lead. Cycles 14..16 do c-accesses
with `prefetch_cycles` still 3..1 → VICE writes `vbuf=$ff`, `cbuf=ram[PC]&0xf`.
c64m hard-coded `cbuf=$0f` (light gray). Probe: PC `$12ED`/`$1431`/… open-bus
nibble is `$6` (blue) — matches colour RAM and VICE. Contaminated columns 0..2
persist for RC 5..7 + the next RC0 paint → 4 gray lines, then a normal badline
refills `$6` for 4 blue lines.

**Fix landed:** `bus.cpu_open_bus_pc` snapshot from the 6510 PC before each VIC
cycle; BA-lead cbuf uses `ram[pc]&0x0f` (VICE `vicii_fetch_matrix`). Unit test
`test_late_badline_observes_three_cycle_ba_takeover` now asserts open-bus cbuf.
Left margin after fix: **100% blue, 0 gray**. `ctest` **51/51**.

**Capture:**
```text
./build/eod_regression_capture roms/system.rom roms/character.rom roms/1541.rom \
  EdgeOfDisgrace_0.d64 EdgeOfDisgrace_1a.d64 \
  /private/tmp/eod-stripes.ppm 4 @14700 1 1
# Bar: x=24..45 solid D020 blue; no $0F gray bars
```

### Capture recipe
```text
./build/eod_regression_capture roms/system.rom roms/character.rom roms/1541.rom \
  EdgeOfDisgrace_0.d64 EdgeOfDisgrace_1a.d64 \
  /private/tmp/eod-checker.ppm 8 checker 4 25
# Bar: no mono column at x=24; ones@24 ~50%; seam23/24 == 0 on fine frames
```

### Logo scroller flash + out-of-band leak (2026-07-20, **FIXED** 2026-07-21)

**Symptom (user):** ~6m18s wall-clock after disk1A @ turbo 1. Purple logo field +
animated-height black scroller band. c64m: scroller text **flashes** (on/off by
frame, class A) and **leaks** green glyphs onto purple outside the black band
(`eod-scroller-c64m.png`). Good structure: `eod-scroller-c64m-1.png`
(and VICE). Band height animation is intentional.

**Repro / park:**
```text
./build/eod_regression_capture roms/system.rom roms/character.rom roms/1541.rom \
  EdgeOfDisgrace_0.d64 EdgeOfDisgrace_1a.d64 \
  /private/tmp/eod-scroller/s.ppm 0 @18800 16 1
# ~frame 23672 after boot; wall-clock landmark ~6m18s @ t=1 maps here
```

**CONCRETE geometry (live 384×312, consecutive frames @ ~23672+):**
- Flash and leak **co-occur**: empty black band + green residue below on purple.
- "OK" frames: full-width blue (`$6`) + green (`$5`/`$D`) scroller text in band.
- "FLASH" frames: solid black across full width (incl. open side border) for the
  FLD stretch; occasional green below band.
- Clearing `g_line` on idle entry: **no change** to OK/FLASH pattern → OK text is
  not stale-g_line shine-through during idle.

**Still-valid FLD register sequence (C64M_LINELOG, R150–220):** one character row
of display (badline + rc 0..7), then **FLD**: YSCROLL cycles `$11..$18` (RSEL
toggles with `$18`), `$D018` animates `FF/DD/BB/...`, `$D020/$D021=$00`.

> **Superseded analysis removed (2026-07-22).** An earlier "CONCRETE sequencer"
> / "paint probe" / "Working model" here blamed a late FLD `$D011`/YSCROLL write
> deadline causing a VCBASE "runaway" (`disp=1 rc=7`, `vcb += $28/line`), plus a
> `$D012`/CSEL story, and had the good/bad frames **inverted** (it tagged good
> frames as idle `disp=0`). That read the symptom backwards and was reverted —
> `disp=1 rc=7 vcb += $28/line` is the FLD scroller working *correctly*, and good
> frames *sustain* display through the FLD stretch. The real root cause and fix
> are below.

### Fix landed (2026-07-22) — real root cause, VICE-confirmed

**Root cause (CONCRETE, measured c64m + live VICE oracle):**
- The scroller sustains display through the FLD stretch by writing YSCROLL each
  line so a 1-cycle `bad_line` flicker re-arms `display_state` (which is a latch:
  set by `(raster&7)==YSCROLL`, cleared only by UpdateRc at `rc==7`). Good frames
  show ~60 re-arm cycles across R161-207; bad frames show **0** → band stays idle
  (black) and leaks.
- Whether it re-arms is set by a stable-raster residual at `$0F00`
  (`SEC; LDA #$28; SBC $DC04; AND #7` → SMC BPL sled), driven off the VIC **raster
  IRQ** (CIA Timer A is only its ruler; timer phase `ta=16@C0` is identical in
  c64m and VICE).
- VICE renders the scroller stably; its handler-entry floor is **cycle 18**, and
  `pad` stays in {0..4}. c64m's good frames enter 18-21 (match VICE) but **bad
  frames entered at 17** — one cycle earlier than VICE ever goes. That read
  `$DC04` one cycle early → `ta`+1 → `pad = ($28-ta)&7` **wrapped 0 -> 7** →
  shortest sled → wrong FLD phase → dead band.
- The 17-vs-18 split is exactly *mid-instruction* BA-stall (read completes at the
  resume cycle, IRQ begins next → C18, correct) vs *between-instructions* BA-stall
  + pending IRQ (c64m began the IRQ sequence *on* the resume cycle → C17).

**Fix (`src/machine/c64.c`, `c64.h`):** when the 6510 resumes from a
between-instructions BA stall with a pending interrupt, consume the resume cycle
as the interrupt's opcode (dummy) fetch and begin the sequence the *following*
cycle (`cpu_prev_between_stall` / `cpu_deferred_interrupt`). Matches VICE
(`DO_INTERRUPT` dummy fetch absorbs the BA steal) and c64m's own
mid-instruction-stall path. See `agents/machine.md`.

**Verification:** scroller **16/16 frames good** (was 6/16), entry floor now 18
(no C17), `ctest` **51/51**; checker/plasma/stripes render correct; face and
eod-3 **pixel-identical** pre/post fix (the change is unreachable unless an IRQ
lands on a between-instructions BA-stall boundary). `session.md` (the prior
agent's $D012 writeup) is superseded and its conclusions are not trustworthy.

### Rotating geometric solid — horizontal sweep (2026-07-21, **FIXED**)

**Symptom (user):** ~2 min after disk1A swap at turbo MAX, a rotating
dodeca/icosahedron on a grey field (black top/bottom bars). In VICE it sweeps
left↔right↔left across the screen while rotating; in c64m it was pinned to the
**left edge**, only wiggling ~0-8px. `eod-geom-c64m.png`.

**Reach it:** shared vector engine `$9000` (also drives the "booze design" logo)
with a global per-frame counter at `$FE/$FF`; scene ends at counter `$5828`.
`eod_regression_capture … @22000 24 50` filmstrips through it (~frame 27000+).
To sync c64m↔VICE, conditional-break `$9006` (engine does `INC $FE` at `$9000`)
on `@ram:$fe/$ff`; VICE needs a **single** conditional bp (delete `$020C`/`$028A`
first — moncommands don't block sequentially, a stale bp ends `g` early).

**CONCRETE mechanism (measured, c64m + instrumented VICE viciisc):** the object
is drawn at a FIXED buffer position (static render from RAM = same cx in c64m and
in VICE at both sweep extremes; `$6C00` screen matrix identical). The entire
sweep is a **hardware VSP/AGSP display scroll**. In IRQ `$1606`: `LDA $0800,X;
AND #$07; EOR #$17; STA $D016` writes only fine XSCROLL (0-7px, which c64m already
did — hence the wiggle); the coarse offset comes from a cycle-timed `$D011=$7A`
write (ysmooth=2) on the idle line **R50** at a cycle set by the `$0800` high
bits via a self-modified sled. R50 starts idle with `rc=7`; the mid-line `$7A`
turns the bad line on at (e.g.) cycle 20, so ~34 g-accesses run → `VC` advances 34
→ UpdateRc (`rc==7`) sets `VCBASE=34` → every row below is shifted → coarse
horizontal position. The write cycle sweeps with `$1637`/`$0800` → the L-R-L
sweep (VICE cx ≈ 104↔278, period 256 frames).

**Root cause (CONCRETE):** VICE `vicii_cycle_start_of_frame` resets only `vc`/
`vcbase` and **carries `rc` (=7 from the bottom border), `vmli`, `idle_state`**.
c64m's end-of-frame additionally reset `rc=0` (`vmli=0`, `display_state=false`),
so the top idle region had `rc=0`; the R50 partial bad line still advanced VC but
UpdateRc never captured `VCBASE` (`rc!=7`) → no coarse shift → object stuck left.

**Fix landed:** `src/machine/vicii.c` end-of-frame now resets only `vc`/`vc_base`
(matches VICE). Test `test_frame_boundary_carries_rc_vmli_display`. See
`agents/vicii.md`.

**Verification:** object now sweeps cx ≈ 75↔270 (24-frame filmstrip); `ctest`
**51/51**; broad sweep (@2000 16×1500) **pixel-identical pre/post** for all 13
non-geom samples (logo/fist/star/face/eod-3/sister/tiles/woman-android/eye incl.
top borders); scroller 16/16 good; plasma 0 side-border black blocks. The carry
is unobservable on normal frames (first real bad line clears `rc` at UpdateVc).

### Android portrait right-border gray band (2026-07-21, **FIXED**)

**Repro:** launch the visible demo with `eod_debug.ini` at turbo MAX. This is
required: it enables both `emulate_1541=true` and `media_1541=true` and supplies
the 1541 ROM. Its one-shot advances disk 0 to 1A. Confirm 1A has taken by pausing
after several seconds and checking that the CPU has left the `$020C` loop, then
arm `$020C` again. At the second stop mount 1B, start a wall-clock timer with the
mount, run, and pause/capture after 29 seconds. The portrait has an `edge of` /
`disgrace` gray band at the upper right. Do not use `--noini`: the compatibility
disk path does not run EoD's post-1A physical-media fastloader correctly.

**Symptom:** c64m left a blue gap between the text box and the right-border gray
band. Every one of the 34 band rows had 9 blue pixels at x=344..352, and 10 rows
had 17 blue pixels through x=360. VICE carries the gray band through the border;
only a one-pixel sparkle remains on some rows.

**Root cause (VICE-confirmed):** CPU and raster phase already matched. The gray
band is made by timed `STA $D020` writes, not by opening the graphics border.
VICE `draw_colors8()` resolves its buffered colour tokens after the CPU-owned
Phi2 store; on a 6569 only the oldest token retains the previous colour. c64m
constructed its live span in `vicii_begin_cycle` and did not resolve a same-cycle
CPU `$D020` write until the next render cycle, adding an erroneous eight-dot
delay on top of the real one-pixel colour latency. The two-span horizontal-border
compensation made that delay visible as the blue gap and teeth.

**Fix:** `vicii_finish_cycle` now applies a changed post-CPU `$D020` value to the
pending border-colour tokens: it preserves only dot zero of the oldest span,
updates the rest of that span, updates the newer span in full, and carries the
new value forward. Regression
`test_color_latency_resolves_same_cycle_phi2_d020_write` exercises the production
VIC-begin → CPU-Phi2-write → VIC-finish order.

**Verification:** the correctly configured visible disk 0 → 1A → 1B run stopped
at the second `$020C` on frame 32583 and captured frame 37920 exactly 29 seconds
after mounting 1B. All 34 gray rows are solid from x=345 through x=383; 26 are
also gray at x=344 and 8 retain only the expected one-pixel blue sparkle there.
There are no 8-pixel teeth or wider blue gap. Running another wall-clock second
at MAX (191 frames) produced the identical seam structure.
