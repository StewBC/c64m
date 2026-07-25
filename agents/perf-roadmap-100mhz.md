# Performance roadmap: 100 MHz Phi2 (aspirational)

**Status:** planning only. No implementation obligation from this document.  
**Date frozen with findings:** 2026-07-24 (Apple M2 measurements).  
**Related:** `perf-baseline-turbo2.md` (numbers to beat), `disk-iec1541.md` (soft power),
`runtime-control.md` (turbo 1/2/3), `architecture.md` (threads).

This is an engineering plan with tiers and kill criteria. The **100 MHz** target is
ludicrously aspirational on purpose: it forces structural thinking past "shave 5%."
Near-term work still has to live under the turbo=2 accuracy contract.

---

## Brief (locked)

| # | Decision |
|---|----------|
| **Bar** | Phase 1: pure machine core, paint on, drives off (like `profile_c64_hotloop`). Phase 2: product free-run turbo=2 headless. Windowed+audio is later, not the dream bar. |
| **Accuracy (turbo <= 2)** | **Current c64m turbo=2 contract** (live ARGB, collisions, full VIC/CIA/SID model as now). Hardware-or-better is a later aspiration; we must not regress what we have now. Internal rewrites OK only if observables hold. |
| **Platform** | Cross-platform: **macOS, Linux, Windows**; **ARM and x64**. Optimize Apple Silicon first; keep portable hooks (SIMD backends, no Apple-only forever). |
| **1541** | **100 MHz with drives soft-powered off.** Drive 8 on is a secondary budget. Dual-on is not required for "done." |
| **Shortcuts** | **Turbo 1 and 2: no behavioral shortcuts.** Same accuracy path. **Turbo 3 (warp): anything goes** for speed, provided switching back to turbo 2 continues accurately **as if turbo was never > 2** (state must remain a valid turbo=2 world). UI/control socket must still be serviced. |
| **Tone** | Tiered plan + kill tests, not a promise of calendar dates. |

---

## Alignment with the 2026-07 discussion defaults

User answers and the proposed defaults were **almost 1:1**. The only material delta:

| Topic | Default proposal | Locked choice |
|-------|------------------|---------------|
| Turbo <= 2 shortcuts | Optional "fast accurate" with kill tests | **No behavioral shortcuts at all** |
| Turbo 3 | Barely specified | **Anything goes**, but handoff to turbo=2 must be seamless |

Platform: defaults said "Apple first + SIMD/multi-thread allowed"; locked choice adds explicit **Win/Linux + x64** (same spirit, stronger portability bar).

Everything else (phased 100 MHz bar, turbo=2 accuracy = current contract, drives-off for 100 MHz, engineering tiers) matches.

---

## Findings freeze: what makes the ~8 MHz ceiling

Measured pure core (M2, PAL, idle BASIC, no 1541 stepping):

| Mode | Phi2 MHz | ns / emulated cycle |
|------|----------|---------------------|
| Video **on** (live ARGB) | **~7.7-8.0** | **~125-130 ns** |
| Video **off** (paint gated) | **~13.2** | **~76 ns** |

Paint-on vs paint-off wall delta: about **~40-42%** of each paint-on cycle is live output work.

### One Phi2 as the code runs it

```text
c64_step_cycle
  c64_step_cycle_internal
    c64_begin_vic -> vicii_begin_cycle
      Phi1 prepare + fetch (always)
      if pixel_output: vicii_render_live_cycle  // ~8 x live_pixel
      badline / border / sprite / sequencer
    6510 micro path (or BA stall)
    cia1 + cia2 + sid (always, one Phi2 each)
    vicii_finish_cycle (color pipe / post-CPU fixups / flush)
    drive_sync (cheap when soft-powered off)
```

### Approximate bill of materials (paint-on cycle = 100)

From structure + on/off wall split + `sample` on `profile_c64_hotloop`:

| Share | Bucket | Code locus |
|------:|--------|------------|
| ~40+ | Live ARGB paint | `vicii_render_live_cycle`, `vicii_live_pixel`, `vicii_background_pixel_ex`, paint parts of `vicii_finish_cycle` |
| ~30 | VIC non-paint / Phi1 | rest of `vicii_begin_cycle` (fetch, badline, borders, BA-related) |
| ~10 | 6510 | `c6510_micro_step`, prepare, bus R/W |
| ~6 | SID | `sid_advance_cycles` every Phi2 |
| ~5 | CIA1+CIA2 | `cia_step_cycle` / timers |
| ~7 | Glue / residual | step shell, inlined work, sample noise |

**Implication:** zeroing paint alone only reaches ~13 MHz. Hitting **100 MHz** needs roughly **~12-13x** on the paint-on path (or ~8x on the paint-off path **and** bringing paint back for free). That is multiple large multiplies, not a polish pass.

### Product gap (secondary)

Headless free-run, drives off, turbo=2: **~6.8 MHz** vs pure **~8 MHz** (~15% runtime/thread tax). Soft power already removed the dual-1541 free-run tax when cold. Close product-to-core after core moves; do not confuse the two.

---

## Math of the dream

Target: **100 MHz** pure paint-on, drives off, turbo=2 accuracy.

| Starting point | Factor needed to 100 MHz |
|----------------|--------------------------|
| Pure paint-on ~8 MHz | **~12.5x** |
| Pure paint-off ~13 MHz | **~7.7x** then paint must not re-tax |
| Product free-run ~6.8 MHz | **~15x** (phase 2) |

Rule of thumb: need **several independent ~1.5-3x levers** that compose. One 20% opt is noise.

---

## Measurement contract (do not argue without numbers)

Always report:

1. **Pure paint-on, drives off** - `./build/profile_c64_hotloop 20000000`  
2. **Pure paint-off, drives off** - `... no-video`  
3. **Product headless turbo=2, drives off** - control-port Phi2 window (see baseline doc)  
4. **Product headless turbo=2, drive 8 on** - secondary  
5. **Accuracy kill suite** - `ctest`; plus known VICE/oracle demos for any VIC/CPU change  

Primary comparison rows remain those in `perf-baseline-turbo2.md`. Update that file when a phase lands a durable new plateau.

**100 MHz "phase 1 done"** means (1) holds ~100 MHz on class hardware (document host), with turbo=2 accuracy tests green.  
**Phase 2 done** means (3) holds a high fraction of (1) (e.g. >= 80% of pure core), not necessarily full 100 MHz on day one of product integration.

---

## Roadmap tiers

### Tier 0 - Hygiene and instrumentation (foundation)

**Goal:** know what moved and do not re-break soft power / turbo contracts.

| Work | Why | Kill test |
|------|-----|-----------|
| Keep pure + product recipes scriptable | Reproducible gains | Same script on M2 (and later x64) |
| Optional per-bucket cycle counters behind a define | Attribute future opts | Counters sum to wall within ~10% |
| Turbo 3 state invariant tests | Warp may skip work | After turbo 3 free-run, set turbo 2: machine matches a twin that only ever ran turbo 2 (or pass oracle suite) |
| Soft power remains default cold | Free-run bar | Both-off product ~ host-only class, not dual-on class |

**Expected gain:** 0-10% if anything; mostly process.

---

### Tier 1 - Paint path re-architecture (largest measured single bucket)

**Goal:** attack the ~40% live ARGB cost without changing turbo=2 pixels/collisions.

Directions (any combination; nothing forbidden in the dream):

| Lever | Idea | Accuracy risk | Plausible band |
|-------|------|---------------|----------------|
| **Specialize common modes** | Fast paths for hires text / MCM / idle border-only spans | High if mode detection wrong | 1.2-2x paint |
| **SIMD compose** | NEON/SSE/AVX for 8-dot spans, palette lookup, border fill | Medium (endian/lane bugs) | 1.3-2x paint |
| **Defer/batch flush** | Fewer per-cycle pipe flushes where model allows | High (border/color latency) | 1.1-1.5x paint |
| **Indexed framebuffer option** | Internal 4-bit index + palette; ARGB only for present/get-frame | Low if conversion correct | 1.2-1.8x paint + cache |
| **Line/block paint** | When no mid-line register writes, paint N cycles at once | High (must detect hazards) | 1.5-3x paint |
| **GPU present path** | Keep CPU model; upload indexes; not a model skip | Low for model | Product FPS, not Phi2 |

**Kill tests:** pixel dumps / VICE DISPLAY_GET on lft-nine, EoD checker, border/MCM cases; collision latches; on/off MHz delta shrinks while paint-on MHz rises.

**Honest ceiling if paint became free:** ~13 MHz with today's non-paint core. Still far from 100. Tier 1 is necessary, not sufficient.

---

### Tier 2 - Non-paint VIC (Phi1 / sequencer)

**Goal:** shrink the ~30% that remains even with `pixel_output_enabled == false`.

| Lever | Idea | Accuracy risk | Plausible band |
|-------|------|---------------|----------------|
| **Table-driven cycle kinds** | Preclassify "this cycle_in_line does X" | Medium | 1.2-1.5x VIC |
| **Badline / idle line specialization** | Different code for display vs border vs vblank | Medium-high | 1.2-2x on common frames |
| **Fetch elision when BA/CPU stalled** | Skip work that cannot change observables | High | 1.1-1.4x |
| **Split "timing VIC" vs "paint VIC"** | Timing always; paint only when needed | Medium | Enables turbo 3 + cleaner turbo 2 |

**Kill tests:** BA/RDY/AEC vs VICE; raster IRQ; badline DMA; demos with FLI/FLD/sideborder.

---

### Tier 3 - 6510 path

**Goal:** ~10% of paint-on today; grows as VIC shrinks.

| Lever | Idea | Accuracy risk | Plausible band |
|-------|------|---------------|----------------|
| **Tighten microcode interpreter** | Less branching, better tables | Low | 1.2-1.5x CPU |
| **Block / basic-block cache** | Same PC region fast path | Medium (self-mod, IRQ) | 1.5-3x CPU |
| **JIT (host code)** | Emit NEON/x64 for hot blocks | High | 2-5x CPU |
| **IRQ/BA boundary specialization** | Fast common "no BA" path | Medium | 1.1-1.3x |

**Kill tests:** CPU test suite; interrupt timing; BA mid-instruction; KERNAL/BASIC boot.

---

### Tier 4 - CIA + SID always-on cost

**Goal:** ~10% combined today; must not change turbo=2 behavior.

| Lever | Idea | Notes |
|-------|------|-------|
| **SID**: cheaper advance when all gates down / no filter change | Still update state needed for exact resume | Turbo 2: same samples if audio enabled at 1x; free-run audio policy separate |
| **CIA**: specialize "timers not under load" | TOD/alarms still exact | Kill: CIA corpus, TOD, serial |
| **Coarse TOD** between events | Only if bit-identical at reads | Hard under "no shortcuts" - may be illegal for turbo 2 |

Under **no behavioral shortcuts**, Tier 4 is mostly **implementation efficiency**, not "skip the chip."

---

### Tier 5 - Structural machine loop (required for order-of-magnitude)

The single-threaded "one C function per Phi2" loop will not reach 100 MHz by micro-opts alone. Structural options:

| Lever | Idea | Accuracy / design notes |
|-------|------|-------------------------|
| **Multi-Phi2 batching** | Step N cycles when no hazard (no CPU write to VIC/CIA, no IRQ edge) | Hazard detection is the product |
| **Parallel paint thread** | Timing core produces tokens; paint consumes | Ordering vs CPU Phi2 stores is the hard part |
| **Parallel 1541** when powered | Drive thread with catch-up points | Secondary to 100 MHz bar |
| **SoA / arena state** | Cache-friendly machine blob | Portable win |
| **Language/hot-path rewrite** | C++/Rust module for VIC+CPU only | Keep C API boundary for the rest |

**Kill tests:** any batching must pass same cycle-accurate suite as unbatched; random "force N=1" mode for debug.

---

### Tier 6 - Turbo 3 (warp) as a separate animal

Per locked policy: **turbo 3 may drop paint, elide collision work, batch aggressively, etc.**, as long as:

1. **Machine state** remains a state that turbo=2 would accept (or is updated so that entering turbo 2 is continuous).  
2. **No "turbo 3 only" corruption** of RAM/CPU/VIC registers that turbo 2 would not have written.  
3. **UI and control port** still run (accept pause, set-turbo, etc.).

**Ideal warp implementation:** share the timing core with turbo 2; disable paint and any pure-display work; allow larger batches; optional coarser device advance **only if** a verified catch-up restores bit-exact turbo=2 state at the boundary (if catch-up cannot be exact, do not take the shortcut).

**Kill test (mandatory):** twin-run or record/replay - long turbo 3 free-run, then `set-turbo 2`, compare to twin that stayed at turbo 2 (or to oracle checkpoints).

Turbo 3 success metric is **MHz or frames of sim/sec**, not ARGB. It can be a lab for batching ideas later promoted to turbo 2 only when accuracy-safe.

---

### Tier 7 - Product free-run (phase 2)

After pure core moves:

| Work | Why |
|------|-----|
| Slim free-run loop (fewer per-cycle checks when no BP/paste/history) | Close 6.8 vs 8.0 gap |
| Turbo 2 audio: do not `sid_sample` every Phi2 into host buffer at free-run rates unless needed | Host path; model still advances |
| Frame publish stays latest-wins | Already non-blocking |
| Soft power stays default | Disk-off bar |

---

## Stacked path to ~100 MHz (dream arithmetic)

Illustrative only - multiplies are independent and optimistic:

| Stage | Example multiply | Running pure paint-on MHz |
|-------|------------------|---------------------------|
| Today | 1.0 | ~8 |
| Paint 2.0x + VIC non-paint 1.5x | 3.0 | ~24 |
| CPU 2.0x + devices 1.3x | 2.6 | ~62 |
| Batching / structural 1.6x | 1.6 | **~100** |

If any major multiply fails, the dream slips; that is expected. The table is a **dependency graph**, not a schedule.

**Minimum credible path if 100 is unreachable soon:**

1. **20-30 MHz** pure paint-on (paint + VIC specialization + some SIMD) - transformative product free-run.  
2. **50+ MHz** paint-off / turbo 3 with clean turbo=2 handoff.  
3. **100 MHz** only with structural batching + multi-year hot-path investment.

Celebrate plateaus; update the baseline file when a plateau is real.

---

## What not to do first

- Micro-opt the free-run command loop before paint/VIC (wrong ceiling).  
- "Speed hacks" that change turbo=2 pixels/timing for demos.  
- Dual-1541 optimisations as the path to 100 MHz (out of bar).  
- Apple-only intrinsics without an x64 path plan.  
- Turbo 3 shortcuts that poison state for turbo 2 return.

---

## Near-term suggested sequence (still planning)

1. **Instrument** optional bucket timers on pure hotloop (paint / begin / finish / cpu / cia / sid).  
2. **Paint specialization + SIMD spike** on Apple Silicon, with portable scalar fallback.  
3. **Hazard-aware multi-cycle batching spike** behind a debug flag (N=1 default).  
4. **Turbo 3 handoff test** so warp experiments stay honest.  
5. Only then chase product loop tax.

Each step: measure pure on/off + product both-off; run accuracy suite; write results next to this roadmap or in the baseline changelog.

---

## Success criteria summary

| Milestone | Criterion |
|-----------|-----------|
| Soft power (done) | Drives off free-run ~ host-only class |
| Core paint win | Pure paint-on MHz up; on/off gap shrinks or both rise; pixels hold |
| Core 20+ MHz | Pure paint-on >= 20 MHz on M2-class; ctest + key demos |
| Warp honesty | Turbo 3 fast; return to 2 accuracy-clean |
| Dream | Pure paint-on ~100 MHz drives off; then product free-run approaches it |

---

## Changelog

| Date | Note |
|------|------|
| 2026-07-24 | Initial roadmap from pure-core profiling + locked brief (phase 1 core / phase 2 product; turbo<=2 no shortcuts; turbo 3 free with clean handoff; drives-off for 100 MHz; Win/Linux/mac, ARM+x64, Apple first). |
| 2026-07-24 | Tier 0: `tools/bench_core_mhz.sh`. Tier 1 start: VIC live paint fast paths (vertical-border bulk B0C; mode-0 XSCROLL=0 text span expand; tighter hborder flush). M2 pure paint-on ~**9.85 MHz** (was ~8.0); product headless drives-off ~**8.2 MHz** (was ~6.9). ctest 56/56. |
| 2026-07-24 | Continue: O(1) sprite-slot LUT (Phi1); span expand modes 1-3 (MCM/bitmap) at XSCROLL=0. M2 pure paint-on ~**10.5 MHz**, paint-off ~**15.4 MHz**, product drives-off ~**8.6 MHz**. ctest 56/56. |
| 2026-07-24 | XSCROLL-aware span path B (modes 0-3); free-run turbo mutes SID sample/host audio; slim free-run batch (no BP/paste/history). M2 pure paint-on ~**10.5 MHz**, paint-off ~**14.8 MHz**, product headless drives-off turbo=2 ~**9.8 MHz**. ctest 56/56. |
| 2026-07-24 | Lazy paint prep + idle/over-border span paths C/D (no idle bus read on A/B). M2 pure paint-on ~**12.2 MHz**, paint-off ~**15.2 MHz**, product headless drives-off turbo=2 ~**11.0 MHz**. ctest 56/56. |
| 2026-07-24 | begin_cycle sprite work only on sequencer cycles; ECM mode-4 spans; one-shot color-pipe drain per span; consecutive CSEL=1 flush. M2 pure paint-on ~**12.4 MHz**, paint-off ~**15.5 MHz**, product drives-off ~**11.3 MHz**. ctest 56/56. |
| 2026-07-24 | SID silent voice path + env-idle skip; mode-0 bulk expand; skip idle Phi1 ghost read. M2 pure paint-on ~**13.0 MHz**, paint-off ~**16.1 MHz**, product drives-off ~**11.6 MHz**. ctest 56/56. |
| 2026-07-24 | CIA idle-timer/serial gates; paint-off finish_cycle skip; mode-2 bulk. M2 pure paint-on ~**13.5 MHz**, paint-off ~**16.5 MHz**, product drives-off ~**12.1 MHz**. ctest 56/56. |
| 2026-07-24 | Fast BRK peek; MCM bulk paint; hborder 2-slot index flip (no pipe memcpy). M2 pure paint-on ~**13.4 MHz**, paint-off ~**16.5 MHz**, product drives-off ~**12.3 MHz**. ctest 56/56. |
| 2026-07-24 | Drive-sync closed form when both 1541s off; ECM bulk; turbo free-run 4k batch; gated vborder compares. M2 pure paint-on ~**13.7 MHz**, paint-off ~**16.7 MHz**, product drives-off ~**12.4 MHz**. ctest 56/56. |
| 2026-07-24 | Hot-path opcode peek for micro begin; sprite_active_mask for BA. Product drives-off ~**12.5 MHz**. ctest 56/56. |
| 2026-07-24 | Confirmed pure plateau (user 3× serial): paint-on ~**13.75 MHz**, paint-off ~**17.0 MHz**. |
| 2026-07-24 | sprite_visible_mask, cached c64_hz (refresh on snapshot load), KERNAL trap PC gate. Pure paint-on ~**14.0 MHz**, product drives-off ~**12.6 MHz**. ctest 56/56. |
| 2026-07-24 | Unrolled hires bulk expand; skip sprite p/s schedule when DMA mask empty. Holds pure paint-on ~**13.7 MHz** plateau. ctest 56/56. |
| 2026-07-24 | Path-A unrolled B0C bulk; BA early-return when no sprite DMA/badline; allow_bad_lines gate; idle sprite prepare/latch/sequencer skips. M2 pure paint-on ~**14.5 MHz**, paint-off ~**17.6 MHz**, product drives-off ~**13.1 MHz**. ctest 56/56. |
| 2026-07-24 | finish_cycle early-out of $D016/$D011 re-decode + XSCROLL under vertical border. Holds pure paint-on ~**14.2 MHz**. ctest 56/56. |
| 2026-07-24 | Local stack Go1–5 (no push): NEON/scalar 8-dot paint helpers; cycle_in_line flag table; deep vborder line_class; `c64_step_cycles` micro hot-path batch; 6510 op-class table. Pure paint-on ~**15.1–15.7 MHz**, paint-off ~**18.7–19.5 MHz** on M2. ctest 56/56 on tip. |
| 2026-07-24 | Local stack GoA–E (no push): between-instr hot chain; idle Phi1 skip; slim VBORDER_IDLE begin_cycle; silent SID phase-only; fully-idle CIA TOD path; free-run skip N audio no-ops + 8k turbo batch. Pure paint-on ~**15.8 MHz**, paint-off ~**21.5 MHz**. ctest 56/56. |
| 2026-07-24 | Local stack Go N1–5 (no push): `c64_step_cycles_ex` + STOP_BEFORE_BRK free-run strips (≤128); MCM XSCROLL=0 bulk; no-sprite AEC from bus_access; access_kind op-class. Pure paint-on ~**16.3 MHz**, paint-off ~**22 MHz**. ctest 56/56. |
