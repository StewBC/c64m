# Performance baseline: turbo=2 free-run

Recorded **2026-07-24** on **Apple M2** (8-core arm64, macOS). Use this file to
compare free-run throughput after performance work. Prefer re-running the same
recipes on the same class of host; absolute MHz will move with silicon and OS
load, but **relative deltas** between rows should stay meaningful.

**Bar for full correctness:** turbo mode 2 (max) — free-run with live ARGB paint.
Warp (mode 3) is a diagnostic upper bound when paint is off, not the product bar.

Related: `runtime-control.md` (turbo semantics), `architecture.md` (thread ownership),
`perf-roadmap-100mhz.md` (aspirational path past the paint-on core ceiling).

---

## Host and build

| Item | Value |
|------|--------|
| Host | Apple M2, arm64 |
| Date | 2026-07-24 |
| Binary | `./build/c64m`, `./build/profile_c64_hotloop` (Release/Ninja tree under `build/`) |
| Video standard | PAL (Φ2 real-time ≈ **0.985248 MHz**) |
| Workload | Idle BASIC/Kernal after reset/boot (no demo, no disk I/O during window) |
| Contended runs | **No** — one process at a time for product measurements |

`× realtime` below uses `phi2_hz / 985248`.

---

## How to measure (reproduce)

### Pure machine core

```text
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

Report the `mhz=` field. Baseline used **two passes** of 20M cycles; table shows
both pass values and their average.

### Product free-run (Φ2 via control port)

Serial: start one `c64m`, wait for control port, pause barrier → run N seconds →
pause → `get-state` cycle delta / wall time. Example client pattern (see session
scripts; `tools/c64_control_client.py`):

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

**Do not** run multiple free-run instances in parallel when collecting baselines;
CPU contention collapses Φ2 rates.

### Thread samples (optional)

```text
sample <pid> 3 -mayDie -file /tmp/sample_c64m.txt
```

Inspect the `c64m-runtime` thread vs main. macOS `sample` fires once per ms per
thread; use **idle vs busy call stacks**, not raw inter-thread sample share, as
CPU%.

---

## Baseline numbers

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

Earlier concurrent 3-instance run (invalid for absolute MHz) still showed
headless≈windowed; exclusive serial table is authoritative.

Frame slot is latest-wins with drops — runtime does **not** block on present.

---

## Derived cost model (this host)

Anchored on pure host paint-on **7.76 MHz** and product headless 1541+media
**4.04 MHz**.

| Factor | Approx effect | Evidence |
|--------|---------------|----------|
| VIC live paint (host only) | ~7.8 → ~13.4 when off (~+70% core) | pure no-video |
| Drive8 ROM step | **~−2.0 MHz** on host+paint | pure 1541-one |
| Drive9 ROM step (no disk) | **~−1.0 MHz** more | pure dual vs one |
| media flags, idle (no image) | **~−0.3 MHz** | pure media vs ROM-only |
| Runtime loop vs matching pure | **~7–18%** | product vs pure same flags; larger % without 1541 |
| Windowed + audio vs headless | **~6–9%** | windowed rows vs headless |
| Frame publish / dirty flush | **≪1%** of runtime samples when idle | `sample` |

**Conclusion recorded with the baseline:** for turbo=2 free-run, the UI thread is
not the limiter; `c64m-runtime` is. With a 1541 ROM installed (default discovery
or ini), **dual-drive ROM stepping dominates** the host-only→product gap. Pure
host-only must not be compared to product-with-1541 without calling out the
drive cost.

### Warp (mode 3) caveat

Headless turbo=3 with an active control port can be **slower** than turbo=2 on
this baseline (~3.66 vs ~4.04) because the main loop drains the frame slot and
warp rebuilds geometric snapshots whenever the slot is free. Windowed warp can
look faster (paint off; slot freed only at ~display rate). Prefer matched
headless turbo=2 rows for advancement tracking unless intentionally testing warp.

---

## Primary comparison rows (use these first)

When landing a free-run optimization, re-measure at least:

1. **Pure host, video on** — `profile_c64_hotloop 20000000`  
   Baseline avg: **7.76 MHz**
2. **Pure host+d8+d9+media, video on** — `… 1541 media`  
   Baseline avg: **4.38 MHz**
3. **Product headless, 1541+media, turbo=2**  
   Baseline avg: **4.04 MHz** (~4.1× realtime)
4. **Product windowed, 1541+media, turbo=2** (optional product bar with UI/audio)  
   Baseline avg: **3.77 MHz**
5. **Product headless, no 1541, turbo=2** (isolates runtime tax)  
   Baseline avg: **6.36 MHz**

Report new numbers next to these five. A win that only moves (5) but not (3)
is runtime-only; a win on (2) and (3) is machine-path.

---

## Assertions validated (2026-07-24)

1. **UI thread is not the free-run bottleneck** — headless vs windowed Δ ~6–9% at
   matched machine config; main thread idle or vsync-waiting.
2. **Machine/runtime thread is the bottleneck** — `c64m-runtime` saturated in
   `c64_step_cycle`; product free-run tracks pure core under the same 1541/video
   flags within ~10%.

---

## Changelog

| Date | Note |
|------|------|
| 2026-07-24 | Initial baseline (M2). Extended `profile_c64_hotloop` flags: `1541`, `1541-one`, `media`, `no-video`. |
| 2026-07-24 | Soft power: product free-run no longer steps unpowered 1541s (default cold). Re-measure primary rows after this change; host-only product path should rise toward pure host. |
| 2026-07-24 | After soft power + paint fast paths (M2): pure host paint-on ~**9.85 MHz**; product headless drives-off ~**8.2 MHz**. See `perf-roadmap-100mhz.md`. |
| 2026-07-24 | Sprite-slot LUT + modes 1-3 span paint (M2): pure paint-on ~**10.5 MHz**, product drives-off ~**8.6 MHz**. |
| 2026-07-24 | XSCROLL spans + free-run audio mute + slim free-run loop (M2): pure paint-on ~**10.5 MHz**, product headless drives-off turbo=2 ~**9.8 MHz** (was ~8.6). |
| 2026-07-24 | Idle/over-border paint spans + lazy prep (M2): pure paint-on ~**12.2 MHz**, product headless drives-off turbo=2 ~**11.0 MHz**. |
| 2026-07-24 | Phi1 sprite-cycle gate + ECM-4 spans + color-pipe/flush tighten (M2): pure paint-on ~**12.4 MHz**, product drives-off turbo=2 ~**11.3 MHz**. |
| 2026-07-24 | SID silent/env-idle + mode-0 bulk + idle Phi1 skip (M2): pure paint-on ~**13.0 MHz**, product drives-off turbo=2 ~**11.6 MHz**. |
| 2026-07-24 | CIA idle-timer/serial gates + mode-2 bulk + paint-off finish skip (M2): pure paint-on ~**13.5 MHz**, product drives-off turbo=2 ~**12.1 MHz**. |
| 2026-07-24 | Fast BRK peek + MCM bulk + hborder index flip (M2): pure paint-on ~**13.4 MHz**, product drives-off turbo=2 ~**12.3 MHz**. |
