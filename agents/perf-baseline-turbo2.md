# Performance baseline: turbo=2 free-run

Recorded **2026-07-24** on **Apple M2** (8-core arm64, macOS), with plateau
updates through **2026-07-25**. Use this file to compare free-run throughput
after performance work. Prefer re-running the same recipes on the same class of
host; absolute MHz will move with silicon and OS load, but **relative deltas**
between rows should stay meaningful.

Related: `runtime-control.md` (turbo semantics), `architecture.md` (thread
ownership), `disk-iec1541.md` (soft power), `testing.md` (ctest).

---

## Contract (do not weaken when optimizing)

| Item | Rule |
|------|------|
| **Correctness bar** | Turbo **1 and 2**: free-run with **live ARGB**, collisions, full VIC/CIA/SID model as today. **No behavioral shortcuts.** Internal rewrites OK only if observables hold. |
| **Warp (turbo 3)** | May drop paint / batch aggressively for speed. Returning to turbo 2 must leave a **valid turbo-2 world** (as if turbo never went above 2). UI/control still serviced. |
| **Phase 1 measure** | Pure machine core, paint **on**, drives soft-powered **off** (`profile_c64_hotloop` / `bench_core_mhz`) |
| **Phase 2 measure** | Product free-run **turbo=2** headless, drives off (control-port Φ2 window) |
| **1541** | Soft power when cold (default). Pure-core “host only” rows assume drives off. Drive 8 on is a secondary budget. |
| **Platform** | macOS / Linux / Windows; ARM + x64. Prefer portable hot paths (e.g. NEON + scalar). |

**Kill suite before claiming a win:** `ctest --test-dir build --output-on-failure`,
plus demos that exercise the changed path when VIC/CPU is touched (e.g.
**lft-nine**, **EoD**, **Deus Ex Machina** / VICE oracle work already in tree).
Do not land VIC/CPU changes on bench-only evidence.

---

## Host and build

| Item | Value |
|------|--------|
| Host (original baseline) | Apple M2, arm64 |
| Date | 2026-07-24 (plateau notes through 2026-07-25) |
| Binary | `./build/c64m`, `./build/profile_c64_hotloop` (Release/Ninja under `build/`) |
| Video standard | PAL (Φ2 real-time ≈ **0.985248 MHz**) |
| Workload | Idle BASIC/Kernal after reset/boot (no demo, no disk I/O during window) |
| Contended runs | **No** — one process at a time for product measurements |

`× realtime` below uses `phi2_hz / 985248`.

---

## How to measure (reproduce)

Always **serial**. Contention collapses Φ2 rates. Prefer **2–3 passes**; thermal/OS
noise of **±0.3–0.5 MHz** is normal on this class of host.

### Pure machine core (authoritative Phase 1)

```text
cmake --build build -j$(sysctl -n hw.ncpu)   # or nproc / hw.ncpu equivalent

# Preferred wrapper (same host table every time):
./tools/bench_core_mhz.sh 20000000

# Or direct:
./build/profile_c64_hotloop 20000000
./build/profile_c64_hotloop 20000000 no-video
./build/profile_c64_hotloop 20000000 1541-one
./build/profile_c64_hotloop 20000000 1541-one no-video
./build/profile_c64_hotloop 20000000 1541
./build/profile_c64_hotloop 20000000 1541 no-video
./build/profile_c64_hotloop 20000000 1541 media
./build/profile_c64_hotloop 20000000 1541 media no-video
```

Flags (any order after cycle count): `no-video`, `1541` (drive8+drive9 ROM),
`1541-one` (drive8 only), `media` (emulate_1541+media_1541), `null-error`.

Report the `mhz=` field. Original baseline used **two passes** of 20M cycles.

**Accuracy:**

```text
ctest --test-dir build --output-on-failure
```

### Product free-run (Φ2 via control port)

Serial: start one `c64m`, wait for control port, pause barrier → run N seconds →
pause → `get-state` cycle delta / wall time. Client: `tools/c64_control_client.py`.

1. `hello` / ensure `run`
2. `pause` + `wait-paused`
3. `get-state` → `cycle=C0`, `frame=F0`; optional `get-drive-cpu 8` for `rom=` / `media=`
4. `run`; wall sleep **4–5 s**
5. `pause` + `wait-paused`
6. `get-state` → `cycle=C1`; `mhz = (C1-C0) / dt / 1e6`

Configs used for the product rows:

| Label | Launch sketch |
|-------|----------------|
| headless no1541 | `--headless --control-port P -i no1541.ini -a --turbo=2 -! -P` |
| headless 1541 ROM only | same with 1541 path, `emulate_1541=false` `media_1541=false` |
| headless 1541+media | 1541 path + `emulate_1541=true` `media_1541=true` |
| headless 1541+media t3 | same + `--turbo=3` |
| windowed … | drop `--headless` (SDL window + host audio) |
| default discover `-n` | `--headless … -n -a --turbo=2 -! -P` (picks up `roms/1541.rom`) |

Minimal ini shapes (paths relative to repo root):

```ini
# no1541.ini — no 1541 key; disk flags off
[Video]
standard=PAL
[config]
turbo_speeds=2
[roms]
single_system=true
system=roms/system.rom
character=roms/character.rom
[disk]
emulate_1541=false
media_1541=false
```

```ini
# with1541.ini — matches typical real config shape
[Video]
standard=PAL
[config]
turbo_speeds=2
[roms]
single_system=true
system=roms/system.rom
character=roms/character.rom
1541=roms/1541.rom
[disk]
emulate_1541=true
media_1541=true
```

**Do not** run multiple free-run instances in parallel when collecting baselines.

### Thread samples (optional)

```text
sample <pid> 3 -mayDie -file /tmp/sample_c64m.txt
```

Inspect the `c64m-runtime` thread vs main. macOS `sample` fires once per ms per
thread; use **idle vs busy call stacks**, not raw inter-thread sample share, as
CPU%.

---

## Plateau note (post 2026-07 work)

On Apple M2, pure-core serial `bench_core_mhz` after the 2026-07 stacks is roughly:

| Mode | ≈ MHz (idle BASIC, drives off) |
|------|--------------------------------|
| Paint-on | **~16.5–17.1** |
| Paint-off | **~22–23** |

That is about **~2×** paint-on from the original **~7.8–8.0** MHz pure host row
below. Absolute numbers are host-specific; re-measure after any free-run or
hot-path change. Do not equate pure-core MHz with product feel without a
product window.

---

## Baseline numbers (original 2026-07-24 table)

### A. Pure `c64_step_cycle` (`profile_c64_hotloop`, 20M cycles × 2)

| Config | Pass1 MHz | Pass2 MHz | Avg MHz | ≈×RT |
|--------|-----------|-----------|---------|------|
| Host only, video **on** | 7.696 | 7.814 | **7.76** | 7.9× |
| Host only, video **off** | 13.386 | 13.352 | **13.37** | 13.6× |
| Host + drive8 ROM, video on | 5.743 | 5.710 | **5.73** | 5.8× |
| Host + drive8 ROM, video off | 8.441 | 8.198 | **8.32** | 8.4× |
| Host + drive8+9 ROM, video on | 4.642 | 4.733 | **4.69** | 4.8× |
| Host + drive8+9 ROM, video off | 6.380 | 6.435 | **6.41** | 6.5× |
| Host + d8+d9 + media flags, video on | 4.339 | 4.411 | **4.38** | 4.4× |
| Host + d8+d9 + media flags, video off | 5.834 | 5.895 | **5.86** | 5.9× |

Notes:

- `1541` loads **both** drive ROMs; product does the same when `roms/…/1541` is set.
- `media` sets `emulate_1541` + `media_1541` with **no** mounted image (idle media path).
- Early short-loop check (5M cycles) was consistent within ~0.2 MHz of the 20M averages.

### B. Product free-run (serial, 4 s windows × 2, control-port Φ2)

| Config | Pass1 MHz | Pass2 MHz | Avg MHz | ≈×RT | drive8 rom/media |
|--------|-----------|-----------|---------|------|------------------|
| Headless, **no 1541**, turbo=2 | 6.216 | 6.495 | **6.36** | 6.5× | 0 / 0 |
| Headless, 1541 ROM only, turbo=2 | 4.425 | 4.332 | **4.38** | 4.4× | 1 / 0 |
| Headless, 1541+media, turbo=2 | 4.004 | 4.072 | **4.04** | 4.1× | 1 / 1 |
| Headless, 1541+media, turbo=**3** | 3.649 | 3.663 | **3.66** | 3.7× | 1 / 1 |
| Windowed + audio, 1541+media, turbo=2 | 3.797 | 3.751 | **3.77** | 3.8× | 1 / 1 |
| Windowed + audio, no 1541, turbo=2 | 5.911 | 5.602 | **5.76** | 5.8× | 0 / 0 |
| Headless, default `-n` discover 1541, turbo=2 | 4.361 | 4.363 | **4.36** | 4.4× | 1 / 0 |

### C. UI vs machine thread (turbo=2, qualitative sample)

Taken with `sample` during free-run; hierarchical presence on **c64m-runtime**:

| Thread | Headless turbo=2 | Windowed turbo=2 |
|--------|------------------|------------------|
| main / UI | Mostly idle (`SDL_WaitEventTimeout` / nanosleep) | Mostly blocked in `SDL_RenderPresent` / Metal (vsync); **&lt;2%** in c64m UI code |
| `c64m-runtime` | **Saturated** — `runtime_step_cycle` → `c64_step_cycle` | Same |
| `c64m-control` | Blocked in accept | Same |

Frame slot is latest-wins with drops — runtime does **not** block on present.

---

## Derived cost model (original host, 2026-07-24)

Anchored on pure host paint-on **7.76 MHz** and product headless 1541+media
**4.04 MHz**. Shares shift after later stacks; the **loci** still matter.

| Factor | Approx effect | Evidence |
|--------|---------------|----------|
| VIC live paint (host only) | ~7.8 → ~13.4 when off (~+70% core) | pure no-video |
| Drive8 ROM step | **~−2.0 MHz** on host+paint | pure 1541-one |
| Drive9 ROM step (no disk) | **~−1.0 MHz** more | pure dual vs one |
| media flags, idle (no image) | **~−0.3 MHz** | pure media vs ROM-only |
| Runtime loop vs matching pure | **~7–18%** | product vs pure same flags; larger % without 1541 |
| Windowed + audio vs headless | **~6–9%** | windowed rows vs headless |
| Frame publish / dirty flush | **≪1%** of runtime samples when idle | `sample` |

**Conclusion:** for turbo=2 free-run, the UI thread is not the limiter;
`c64m-runtime` is. With a 1541 ROM installed, **dual-drive ROM stepping**
dominates the host-only→product gap when drives are powered. Soft power keeps
cold drives off by default. Pure host-only must not be compared to
product-with-1541 without calling out drive cost.

### One Phi2 (hot-path map)

```text
c64_step_cycle / c64_step_cycles_ex
  c64_begin_vic → vicii_begin_cycle
    Phi1 prepare/fetch
    if pixel_output: vicii_render_live_cycle   // paint-on cost
    badline / border / sprite / BA / Phi2 fetch
  6510 micro (or BA stall)                     // micro_hot / between_hot
  cia1 + cia2 + sid
  vicii_finish_cycle
  drive_sync                                   // cheap when both off
```

| Bucket | Code loci |
|--------|-----------|
| Live paint | `vicii_render_live_cycle`, hborder flush, finish colour repair |
| Non-paint VIC | rest of `vicii_begin_cycle` |
| 6510 | `c6510_micro_step`, peeks |
| SID/CIA | `sid_advance_cycles`, `cia_step_cycle` |
| Glue | `c64_step_cycles_ex`, free-run batching in `runtime_thread.c` |

Key files: `src/machine/c64.c`, `vicii.c` / `vicii.h`, `c6510.c`, `sid.c`,
`cia.c`, `src/runtime/runtime_thread.c`, `tools/bench_core_mhz.sh`,
`tools/profile_c64_hotloop.c`, `tools/c64_control_client.py`.

### Warp (mode 3) caveat

Headless turbo=3 with an active control port can be **slower** than turbo=2 on
the original baseline (~3.66 vs ~4.04) because the main loop drains the frame
slot and warp rebuilds geometric snapshots whenever the slot is free. Windowed
warp can look faster (paint off; slot freed only at ~display rate). Prefer
matched headless turbo=2 rows for advancement tracking unless intentionally
testing warp.

---

## Primary comparison rows (use these first)

When landing a free-run optimization, re-measure at least:

1. **Pure host, video on** — `profile_c64_hotloop 20000000`  
   Original baseline avg: **7.76 MHz** (later plateau ~16.5–17 MHz; see changelog)
2. **Pure host+d8+d9+media, video on** — `… 1541 media`  
   Baseline avg: **4.38 MHz**
3. **Product headless, 1541+media, turbo=2**  
   Baseline avg: **4.04 MHz** (~4.1× realtime)
4. **Product windowed, 1541+media, turbo=2** (optional product bar with UI/audio)  
   Baseline avg: **3.77 MHz**
5. **Product headless, no 1541, turbo=2** (isolates runtime tax)  
   Baseline avg: **6.36 MHz**

Report new numbers next to these five. A win that only moves (5) but not (3)
is runtime-only; a win on (2) and (3) is machine-path. For pure-core-only work,
also report paint-off (`no-video`) so the on/off gap is visible.

---

## Assertions validated (2026-07-24)

1. **UI thread is not the free-run bottleneck** — headless vs windowed Δ ~6–9% at
   matched machine config; main thread idle or vsync-waiting.
2. **Machine/runtime thread is the bottleneck** — `c64m-runtime` saturated in
   `c64_step_cycle`; product free-run tracks pure core under the same 1541/video
   flags within ~10%.

---

## Measurement pitfalls (durable)

1. **Thermal / contention = fake regressions.** Serial benches only; 2–3 passes.  
2. **`line_class` can go stale** if tests teleport raster / `allow_bad_lines`. Demote
   to full path when allow_bad_lines, near vborder, sprites, or border opens.  
3. **Do not stop before BRK in plain `c64_step_cycle`.** Only free-run multi-step
   uses `C64_STEP_STOP_BEFORE_BRK`. Stopping in `step_cycle(1)` breaks BRK and
   frame tests.  
4. **Debug dumps in the Phi2 path** must be behind `#ifdef C64M_VIC_TRACE` (or
   equivalent compile gate).  
5. **BA / AEC vs schedule** are not always identical when sprites + badline
   overlap; only collapse walks when `sprite_active_mask == 0`.  
6. **Snapshot `c64_hz`** must refresh on load.  
7. **Tests that poke `sprite_active[]`** must rebuild sprite masks.  
8. **`c6510_micro_cycles_remaining` is not a free multi-Phi2 oracle** in Release
   (`assert` off). Do not pre-paint or skip work past an unverified remaining count.

---

## Changelog

| Date | Note |
|------|------|
| 2026-07-24 | Initial baseline (M2). Extended `profile_c64_hotloop` flags: `1541`, `1541-one`, `media`, `no-video`. |
| 2026-07-24 | Soft power: product free-run no longer steps unpowered 1541s (default cold). Re-measure primary rows after this change; host-only product path should rise toward pure host. |
| 2026-07-24 | After soft power + paint fast paths (M2): pure host paint-on ~**9.85 MHz**; product headless drives-off ~**8.2 MHz**. |
| 2026-07-24 | Sprite-slot LUT + modes 1-3 span paint (M2): pure paint-on ~**10.5 MHz**, product drives-off ~**8.6 MHz**. |
| 2026-07-24 | XSCROLL spans + free-run audio mute + slim free-run loop (M2): pure paint-on ~**10.5 MHz**, product headless drives-off turbo=2 ~**9.8 MHz** (was ~8.6). |
| 2026-07-24 | Idle/over-border paint spans + lazy prep (M2): pure paint-on ~**12.2 MHz**, product headless drives-off turbo=2 ~**11.0 MHz**. |
| 2026-07-24 | Phi1 sprite-cycle gate + ECM-4 spans + color-pipe/flush tighten (M2): pure paint-on ~**12.4 MHz**, product drives-off turbo=2 ~**11.3 MHz**. |
| 2026-07-24 | SID silent/env-idle + mode-0 bulk + idle Phi1 skip (M2): pure paint-on ~**13.0 MHz**, product drives-off turbo=2 ~**11.6 MHz**. |
| 2026-07-24 | CIA idle-timer/serial gates + mode-2 bulk + paint-off finish skip (M2): pure paint-on ~**13.5 MHz**, product drives-off turbo=2 ~**12.1 MHz**. |
| 2026-07-24 | Fast BRK peek + MCM bulk + hborder index flip (M2): pure paint-on ~**13.4 MHz**, product drives-off turbo=2 ~**12.3 MHz**. |
| 2026-07-24 | Drive-sync cold path + ECM bulk + turbo 4k batch (M2): pure paint-on ~**13.7 MHz**, product drives-off turbo=2 ~**12.4 MHz**. |
| 2026-07-24 | Opcode peek for micro-begin + sprite_active_mask BA (M2): product drives-off turbo=2 ~**12.5 MHz**. |
| 2026-07-24 | Confirmed pure plateau (3× serial `bench_core_mhz`): paint-on ~**13.75 MHz**, paint-off ~**17.0 MHz** (earlier ~13.1 was thermal noise). |
| 2026-07-24 | sprite_visible_mask + c64_hz cache + KERNAL PC gate (M2): pure paint-on ~**14.0 MHz**, product drives-off turbo=2 ~**12.6 MHz**. |
| 2026-07-24 | Unrolled hires bulk + no sprite-slot work without DMA (M2): pure paint-on holds ~**13.7 MHz** (triple sample). |
| 2026-07-24 | Path-A unrolled B0C + BA idle early-return + idle sprite sequencer gates (M2): pure paint-on ~**14.5 MHz**, paint-off ~**17.6 MHz**, product drives-off turbo=2 ~**13.1 MHz**. |
| 2026-07-24 | finish_cycle skips mode re-decode / XSCROLL under vertical border (M2): pure paint-on holds ~**14.2 MHz**. |
| 2026-07-24 | Go1–5 local stack tip (M2): pure paint-on ~**15.1–15.7 MHz**, paint-off ~**18.7–19.5 MHz** (`c64_step_cycles` micro strip + paint/VIC/CPU tables). |
| 2026-07-24 | GoA–E local stack tip (M2): pure paint-on ~**15.8 MHz**, paint-off ~**21.5 MHz** (between-hot chain, slim vborder begin, silent SID, idle CIA, free-run audio skip). |
| 2026-07-24 | Go N1–5 local stack tip (M2): pure paint-on ~**16.3–16.4 MHz**, paint-off ~**21.8–22.3 MHz** (BRK-aware cross-instr free-run strips, MCM bulk, AEC schedule skip). |
| 2026-07-24 | User tip benches ~**16.5 / 22.4** MHz paint-on/off. |
| 2026-07-25 | Frame double-buffer swap (no ~650KB EOF memcpy), skip full-frame border clear, EOF hborder pipe drain, solid-span flush flags. M2 pure paint-on ~**16.5–16.7 MHz**, paint-off ~**22.2–22.3 MHz**. ctest 56/56. |
| 2026-07-25 | Fuse between_hot + micro drain; free-pin micro_hot skips access-kind walk; demote stale VBORDER_IDLE on allow_bad_lines. M2 pure paint-on ~**16.8 MHz**, paint-off ~**22.7–22.9 MHz**. ctest 56/56. |
| 2026-07-25 | Perf program paused. Folded durable measure/contract/pitfalls into this file; removed session hand-off and 100 MHz roadmap docs. |
| 2026-07-25 | CPU flight recorder accepted baseline: matched runtime `config-off` **14.716/14.705 MHz** versus full recording **13.593/13.634 MHz**, about **7.4%** loss. This is above the 5% target but below the 10% ceiling and was explicitly accepted without further optimization. A final post-CTest single pass measured **14.180 / 13.484 MHz**. The 256 MiB idle-BASIC arena retained **9.51 million** records at about **28 bytes/record**. Full-store query results were exact-address miss **219.734-221.473 ms**, newest hit **0.020-0.024 ms**, PC **1.3-1.4 ms**, and three-opcode pattern **0.058-0.064 ms**; the linear miss is below the 500 ms indexing threshold. |
