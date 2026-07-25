# Performance hand-off (2026-07 session)

**Status:** active 2026-07-25 (hazard strip on `perf/hazard-batch`; paint re-arch already on `main`).  
**Host frozen with numbers:** Apple M2, arm64, macOS.  
**Tip plateau:** pure paint-on ~**16.8 MHz**, paint-off ~**22.8 MHz** after fuse + free-pin micro_hot.  
**Related:** `perf-baseline-turbo2.md` (how to measure + changelog numbers),  
`perf-roadmap-100mhz.md` (locked brief + tier plan to 100 MHz).

This note is the **session brain dump**: what plateau we hold, what we already
harvested, what *not* to re-do as micro-opts, and where the next multi-MHz
lives. Prefer this + the two perf docs over re-deriving history from chat.

---

## 1. Locked contract (do not weaken)

Same as the roadmap brief; restated so hand-off readers cannot miss it:

| Item | Rule |
|------|------|
| **Phase 1 bar** | Pure machine core, **paint on**, drives soft-powered **off** (`profile_c64_hotloop`) |
| **Phase 2 bar** | Product free-run **turbo=2** headless, drives off |
| **Turbo 1 & 2** | **No behavioral shortcuts.** Same accuracy as today (live ARGB, collisions, full VIC/CIA/SID model). Internal rewrites OK only if observables hold |
| **Turbo 3 (warp)** | Anything goes for speed; handoff back to turbo 2 must leave a valid turbo-2 world |
| **Platform** | macOS / Linux / Windows; ARM + x64. Apple Silicon first; no permanent Apple-only hot path without a portable fallback |
| **1541** | Soft power when cold. 100 MHz dream is **drives off**. Drive 8 on is secondary budget |

**Kill suite before claiming a win:** `ctest` (56 tests at freeze), plus demos that
already signed off this session when relevant: **lft-nine**, **EoD**, **Deus Ex
Machina** (and any VICE/oracle pixel work already in the tree). Do not land VIC/CPU
changes on “bench only.”

---

## 2. Plateau at freeze (M2, serial, idle BASIC)

Primary pure-core recipe (always serial — contention collapses Φ2 rates):

```text
./tools/bench_core_mhz.sh 20000000
# or:
./build/profile_c64_hotloop 20000000
./build/profile_c64_hotloop 20000000 no-video
```

| Mode | Session start (doc freeze ~8 MHz era) | End of session (user triple / tip) | ≈ factor |
|------|--------------------------------------|-------------------------------------|----------|
| Pure **paint-on** | ~**7.8–8.0** MHz | ~**16.3–16.5** MHz | **~2.1×** |
| Pure **paint-off** | ~**13.3** MHz | ~**22.3–22.5** MHz | **~1.7×** |
| Drive8 video-on | ~5.7 (early pure table) | ~**9.3–9.5** MHz | — |
| Drive8+9 video-on | ~4.7 | ~**7.0** MHz | — |

Product headless turbo=2 drives-off was tracked earlier in the day around
**~12.5–13.1 MHz** after soft power + paint stacks; re-measure after any free-run
change (see baseline doc control-port recipe). Do not assume pure-core MHz = app
feel without a product window.

**Noise:** ±0.3–0.5 MHz thermal/OS is normal. Prefer 2–3 serial passes. An earlier
“regression” to ~13.1 was noise; triple-run confirmed the higher plateau.

**Math of the dream:** 100 MHz paint-on still needs ~**6×** from ~16.5 MHz
(~12.5× from original 8). Micro-opts alone will not get there.

---

## 3. What this session already harvested

Grouped by theme (not every commit). Changelog detail lives in the baseline and
roadmap files.

### Paint / VIC live path
- Mode-specialized span paint (hires / MCM / bitmap / ECM) at XSCROLL=0 and
  XSCROLL-aware path B
- Vertical-border bulk B0C (path A); idle / over-border paths C/D
- Hborder 2-deep index flip (no pipe memcpy); CSEL=1 consecutive flush
- Portable **NEON + scalar** 8-dot store/fill helpers
- MCM bulk expand tighten
- `finish_cycle`: skip mode re-decode / XSCROLL under vertical border
- Lazy paint prep; one-shot color-pipe drain per span

### Non-paint VIC / Phi1
- Sprite slot LUT; skip p/s schedule and sequencer when DMA/enable empty
- `sprite_active_mask` / `sprite_visible_mask` for BA and paint
- Table-driven `cycle_in_line` flags (PAL/NTSC)
- `line_class` deep **VBORDER_IDLE** specialization (+ demote when teleported /
  allow_bad_lines / sprites)
- Slim `begin_cycle` when VBORDER_IDLE and no sequencer events
- Skip idle Phi1 g/fetch when `!display_state`
- AEC from `bus_access` when no sprite DMA (avoid second slot walk)
- Gated vborder compares near top/bottom only

### Machine loop / free-run
- `c64_step_cycles` / `c64_step_cycles_ex` with mid-instruction **micro hot** path
  and between-instruction **between hot** path
- Free-run: BRK-aware multi-Phi2 strips (`C64_STEP_STOP_BEFORE_BRK`, ≤128), turbo
  command-poll batch 8k, mute host audio / skip N audio no-ops under free-run
- Soft 1541 power; drive-sync closed form when both off; cached `c64_hz` (refresh
  on snapshot load)
- KERNAL LOAD/SAVE trap gated on PC; hot peeks for opcode / BRK

### SID / CIA / CPU
- SID silent sample path; env-idle skip; phase-only when silent + no sync + env parked
- CIA idle timer/serial gates; fully-idle “TOD + IRQ pin only” tick
- 6510 micro opcode **class table** for `can_begin`, step dispatch, `access_kind`

### Hygiene
- Compile-gate `C64M_LINELOG` (and keep other VIC traces) behind `C64M_VIC_TRACE`
- Snapshot / test fixes along the way (e.g. `c64_hz` after load; sprite mask sync
  in tests that poke `sprite_active`)

### Workflow that worked
1. Local stacked branches (`perf/…`), **no push** until user OK  
2. ~5 commits of related work, ctest green on tip  
3. User runs demos + `bench_core_mhz`  
4. FF-merge tip → `main`, delete local branches, push  

Bisect-friendly. Prefer that over one giant mega-commit.

---

## 4. Hot path map (where time still goes)

Rough bill from early freeze (~8 MHz paint-on); shares shift as paint drops, but
loci remain:

```text
c64_step_cycle / c64_step_cycles_ex
  c64_begin_vic → vicii_begin_cycle
    Phi1 prepare/fetch
    if pixel_output: vicii_render_live_cycle   // paint-on cost
    badline / border / sprite / BA / Phi2 fetch
  6510 micro (or BA stall)                     // between_hot / micro_hot
  cia1 + cia2 + sid                            // idle gates already applied
  vicii_finish_cycle
  drive_sync                                   // cheap when both off
```

| Bucket | Code | Notes at freeze |
|--------|------|-----------------|
| Live paint | `vicii_render_live_cycle`, hborder flush, finish color repair | On/off delta still ~**6 MHz** (~16.5 vs ~22.5) |
| Non-paint VIC | rest of `vicii_begin_cycle` | Much of idle/vborder already specialized |
| 6510 | `c6510_micro_step`, peeks | Class table in; no block-cache/JIT yet |
| SID/CIA | `sid_advance_cycles`, `cia_step_cycle` | Idle/silent specialized; still every Phi2 |
| Glue | step_cycles free-run batching | Micro + BRK-aware strips landed |

**Implication:** another round of “skip one more idle if” is **diminishing returns**.
The on/off gap and single-threaded per-Phi2 structure are the structural ceilings.

---

## 5. Pitfalls learned this session

1. **Thermal / contention = fake regressions.** Always serial benches; 2–3 passes.  
2. **`line_class` can go stale** if tests teleport raster/`allow_bad_lines` mid-line.
   Demote to FULL when allow_bad_lines, near vborder, sprites, or border opens.  
3. **Do not stop before BRK in plain `c64_step_cycle`.** Only free-run multi-step
   uses `C64_STEP_STOP_BEFORE_BRK`. Stopping in `step_cycle(1)` breaks BRK and
   frame tests.  
4. **Debug dumps in the Phi2 path must be `#ifdef C64M_VIC_TRACE`.** LINELOG was
   an unguarded getenv/fopen hole; fixed.  
5. **Solid-glyph special cases can *hurt*** if they add mispredicted branches on
   mixed gdata; measure before keeping.  
6. **BA / AEC vs schedule_phi2** are not always identical when sprites + badline
   overlap; only collapse when `sprite_active_mask == 0`.  
7. **Snapshot `c64_hz`** must refresh on load (midload regression if stale).  
8. **Tests that poke `sprite_active[]`** must rebuild masks.

---

## 6. How to measure next session

```text
# Pure core (authoritative for Phase 1)
cmake --build build -j$(sysctl -n hw.ncpu)
./tools/bench_core_mhz.sh 20000000

# Accuracy
ctest --test-dir build --output-on-failure   # expect 56/56 at freeze

# Product free-run turbo=2 drives-off (Phase 2) — see baseline doc:
# headless + control-port get-state cycle window, serial, 4 s
```

Update **both** `perf-baseline-turbo2.md` and `perf-roadmap-100mhz.md` changelogs
when a durable plateau moves. Absolute MHz are host-specific; relative story matters.

---

## 7. Next work: structural first (recommended order)

Cheap micro-opts: expect **0–1 MHz** and noise. Prefer **one structural lever**
per stack, with kill tests.

| Priority | Lever | Why | Accuracy risk |
|----------|--------|-----|----------------|
| **1** | **True multi-Phi2 hazard batching** beyond free-run strips | When next ops are pure RAM, no IRQ/BA edge—amortize glue. Free-run already strips mid-instr + BRK-aware ≤128 | High — hazard detection is the product |
| **2** | **Paint re-architecture** | Close ~6 MHz on/off gap: real NEON bit-expand, indexed 4-bit FB + palette at present, or line/block paint when no mid-line reg writes | Medium–high |
| **3** | **6510 basic-block / tighter interpreter** | CPU share grows as VIC shrinks; class table is done | Medium |
| **4** | **Product gap** | Headless turbo=2 vs pure; runtime/control tax | Medium (don’t break BRK/control) |
| **5** | Parallel paint tokens / SoA / language boundary | 100 MHz class only | High / design-heavy |

Still valid from roadmap tiers: table-driven cycle kinds (partially done),
display-vs-border line split (partially done), turbo-3-only shortcuts with clean
handoff.

**Do not** open with another “five idle-if Go’s” unless measuring first and
expecting modest gains.

---

## 8. Key files

| Area | Paths |
|------|--------|
| Step loop | `src/machine/c64.c` (`c64_step_cycles_ex`, `between_hot`, `micro_hot`) |
| VIC | `src/machine/vicii.c`, `vicii.h` (paint paths, `line_class`, cycle flags) |
| 6510 | `src/machine/c6510.c` (op class table, micro step) |
| SID/CIA | `src/machine/sid.c`, `cia.c` |
| Free-run | `src/runtime/runtime_thread.c` |
| Bench | `tools/bench_core_mhz.sh`, `tools/profile_c64_hotloop.c` |
| Control client | `tools/c64_control_client.py` |

---

## 9. Suggested first message for the next agent

> Read `agents/perf-handoff-2026-07.md`, `perf-baseline-turbo2.md`, and
> `perf-roadmap-100mhz.md`. Plateau is ~16.5 / ~22.5 MHz pure paint-on/off on M2
> (~2.1× from ~8). Turbo≤2 no behavioral shortcuts. Prefer one structural lever
> (hazard batching or paint re-arch) over more idle micro-opts. Measure with
> `./tools/bench_core_mhz.sh 20000000`, ctest 56/56, demos lft-nine/EoD/DEM.
> Local stacked branches, no push until asked.

---

## 10. Session tone / user preference

- User likes **autonomous “Go 5”** local stacks without babysitting.  
- User validates demos + benches, then **“merge and rem branches” + push**.  
- Accuracy and demos beat raw MHz.  
- Bedtime hand-off: stop cleanly; do not leave uncommitted WIP without a note.

*End of hand-off. Sleep well.*
