# PAL horizontal border centering (9f1ea9e) — investigation log

**Date:** 2026-07-23  
**Snapshot for repro:** `assets/snapshots/c64m-20260723-111935.c64state` (EoD-style checker, lands on the scene immediately)  
**Related commit:** `9f1ea9e` — *Centre PAL frame on the 320 display for VICE 32/32 borders*  
**Current outcome:** PAL frame origin offset remains **0** (framebuffer x = VIC X). 32/32 centering is deferred.

This note is a working log for a future agent. Source and tests remain authoritative; this document records *why* a tempting geometry change failed on a cycle-timed open-border demo.

---

## Goal of 9f1ea9e

Match VICE’s published PAL “normal” geometry:

| | Left border | Display | Right border | Total |
|--|-------------|---------|--------------|-------|
| **VICE** (`VICII_SCREEN_PAL_NORMAL_*BORDERWIDTH`) | 32 | 320 | 32 | 384 |
| **c64m before 9f1ea9e** | 24 (VIC X 0–23) | 320 (24–343) | 40 (344–383) | 384 |
| **c64m after 9f1ea9e (intent)** | 32 | 320 at frame x 32–351 | 32 | 384 |

Mechanism in the commit:

- Keep all **hardware** logic in VIC-X space (border compares 24/344, sprites, XSCROLL).
- Shift **paint landing** only: `framebuffer_x = (VIC_X + 8) mod 504`.
- Inverse map for snapshot / MCM re-decode: `VIC_X = fb_x - 8` (with modular wrap for the pad).

So display column 0 (VIC X = 24) lands at frame x = 32. NTSC offset stayed 0.

---

## How to reproduce the defect (post-9f1ea9e)

```text
./build/c64m -P --sna assets/snapshots/c64m-20260723-111935.c64state --control-port 6510
# optional remote: set-turbo 1, get-frame (do not use --headless if you want the SDL view)
```

**Visual scene**

- Purple top/bottom borders (`$D020` ≈ colour 4).
- Open side borders.
- Fine (then coarser) red/blue checker (colours 2 and 6).
- Solid black frame lines (~2 px) above and below the checker.

**What went wrong after 9f1ea9e**

- Top black line: mostly solid; **below** it, left edge corruption (~8 px, looked “random”).
- Bottom black line (upper of the two rows): **checker bleed** — solid colour 2 for about **8 consecutive pixels** on the left, rest of the line solid black.

Pre-9f1ea9e parent (`452e0a1`): those black lines were solid full-width; checker left edge was continuous.

Measured on live `get-frame` (384×312 ARGB):

| Row | Role | After 9f1ea9e (left 16 px, palette indices) |
|-----|------|-----------------------------------------------|
| y=50–51 | Top black frame | Solid 0 (OK) |
| y=52 | First checker row | Left 8 corrupted (zeros mixed into pattern) |
| y=245 | Bottom black (top row of pair) | `[2,2,2,2,2,2,2,2, 0,0,0,0,0,0,0,0]` |
| y=246 | Bottom black | Solid 0 (OK) |

Exactly **eight** bad pixels = one character / one paint cycle.

---

## Geometry of the +8 map

PAL line length: `63 × 8 = 504` dots.

c64m anchored paint (unchanged by the commit’s *intent*):

```text
raw_xs = 24 + (cycle - 15) × 8
```

| Cycle (0-based) | VIC X (approx) | Frame x with +8 mod 504 |
|-----------------|----------------|-------------------------|
| 11 | 496–503 (raw −8…−1) | **0–7** (new left pad) |
| 12 | 0–7 | 8–15 |
| 15 | 24–31 | 32–39 (display start) |
| 58 | 368–375 | 376–383 |
| 59 | 376–383 | **outside** crop (lost) |

So the commit did **not** slide the old 384 window by 8. It:

1. **Inserted** content from VIC X **496–503** (cycle 11) as the new left 8 px.
2. **Dropped** VIC X **376–383** from the right of the crop.

Those 496–503 dots are chronologically just **before** X wraps to 0 (left side of the raster in X-space), not “the 8 pixels that used to fall off the right of the old crop.”

### Sprite geometry that matters

- Non-expanded sprite at **X=480**, width 24 → covers **480–503 only**. Never appears in the old crop (X 0–383). After +8 it occupies **exactly** frame x 0–7 (dx 16–23 of that sprite).
- X-expanded sprite at **X=480**, width 48 → covers 480–503 **and** wraps to 0–23. That pattern is intentional for full left-border coverage (lft-nine digit work; see `vicii_sprite_dx_wrapped` comments). The first half of that sprite was always off the old 24 px left crop.

Live register peek on the checker snapshot often showed something like: spr7 at X=480, `$D01D` X-expand = 0 (parked, non-expanded), enable may read 0 while `sprite_visible` is still sticky from DMA.

---

## VICE reference (local tree)

Path used: `/Users/swessels/Develop/svm/vice-emu-code/vice`.

Relevant bits (`viciisc`):

```c
// vicii-timing.h
VICII_SCREEN_PAL_NORMAL_LEFTBORDERWIDTH  = 0x20  // 32
VICII_SCREEN_PAL_NORMAL_RIGHTBORDERWIDTH = 0x20

// vicii-draw.c
DBUF_OFFSET = 17 * 8 - screen_leftborderwidth
// → 136 - 32 = 104 for normal borders
// published line = dbuf[DBUF_OFFSET .. +384)
```

dbuf is filled chronologically from early raster cycles (high X, then wrap through 0). With offset 104 the published frame **starts around cycle 14 / xpos ≈ 4**, i.e. it does **not** simply “show the same modular left pad as c64m’s (X+8)%504 starting at 496.” Exact xpos↔buffer alignment between c64m’s classic X and VICE’s cycle table is easy to mis-count (Phi1 vs Phi2, 1-cycle draw pipe); do not treat the +8 map as proven byte-identical to VICE without a side-by-side dump.

Takeaway for c64m: **VICE’s normal crop is not free license to paint X=496–503 as left-edge content without checking demos that park sprites there.**

---

## What broke (mechanism)

### Primary: wrong content in the new left 8

After +8, frame x 0–7 is composed at **VIC X 496–503** on **cycle 11**.

That region is over-border for graphics (`x ≥ 344 + XSCROLL` / pair-0 / B0C or mode-specific border_gfx), but **sprites still mux** when the main border flip-flop is open (Bauer / existing c64m model).

On EoD black frame rows:

- Most of the line is solid black (idle / invalid / forced-black path, main border open so purple `$D020` does not cover).
- Left 8 showed **opaque sprite (or early-cycle B0C)** — measured as solid palette **2**.
- Rest of the same line at X 0–7 (frame x 8–15) stayed black.

So the black “frame” was no longer a clean rule; the pad was a different paint path and a different cycle than the classic left edge.

### Secondary: cycle-11 vs cycle-12 state

Even when forcing paint X to 0–7 while still *landing* on the wrap cycle (cycle 11), left pad ≠ X=0–7 painted on cycle 12. Mid-line `$D020`/`$D021` pipes and multiplexed sprite registers differ by a cycle on this demo. **Same VIC X, different cycle → different colours.**

### Why “what left the right is not what arrived on the left”

Old right tail was X 376–383 (deep in right border / late cycles).  
New left pad was X 496–503 (pre-wrap, sprite-parking band).  
Unrelated X bands. Sliding intuition does not apply.

---

## Attempts and results

### A. Modular wrap +8 as in 9f1ea9e (landing and compose at 496–503)

- **Good:** Continuous scroll geometry for anything that truly wraps; display centered.
- **Bad:** EoD black-frame left bleed (~8 px colour 2); top-of-checker left glitches.
- **Status:** Original regression.

### B. Duplicate classic X=0–7 into frame x 0–7 (same cycle as X=0–7 paint)

- **Good:** Black lines solid (same state as true left edge); no parked-sprite path.
- **Bad:** **Systematic** `left8 == mid8`. When checker blocks grow, the first column looks **2× wide** (user screenshot `assets/snapshots/Screenshot 2026-07-23 at 12.23.15 PM.png`). Very noticeable in motion.
- **Status:** Rejected.

### C. No modular land; pad = B0C / sprites suppressed on X∈[496,504)

- **Good:** Removes many parked non-expanded sprites at 480.
- **Bad:** Colour 2 on black lines **remained** with *all* sprites suppressed on the pad → background / colour-pipe / border_gfx at the early cycle, not only sprites. Also solid B0C next to checker merges with the first cell when B0C matches colour 2/6 → same “wide first column” look without a bit-identical duplicate.
- **Status:** Rejected.

### D. Park filter only: on left-pad X, draw sprite only if `spr_x + width > line_dots` (wraps into low X)

- Intent: hide non-expanded park at 480 (`480+24 = 504` not `> 504`); keep expanded-at-480 for lft-nine.
- **Bad:** Insufficient alone (bg/pipe still wrong on some rows); easy to get wrong on multiplexed mid-line X.
- **Status:** Interesting for a future partial fix; not sufficient for EoD.

### E. Deferred pad: land pad when painting X=0–7, but *compose* at X=496–503 with **that** cycle’s pipes

- Intent: continuous geometry + late colour state.
- **Partial:** Black y=245 went solid black in one trial; no bit-identical left8==mid8.
- **Bad:** Pad often solid colour 2 (B0C / non-checker) while mid is checker; adjacent same colour → wide first run (e.g. 8–10 px of red) — still a left-edge discontinuity under large blocks.
- **Status:** Promising structure, not good enough for EoD.

### F. Revert PAL offset to 0 (current tree)

- **Good:** Matches pre-9f1ea9e EoD: solid black frame, continuous checker, no pad theory.
- **Bad:** Unbalanced 24/40 borders again; VICE 32/32 compare still asymmetric.
- **Status:** **Landed.** `VICII_PAL_FRAME_X_OFFSET = 0`. Mapping helpers remain so a future offset can be re-enabled without ripping out the paint path again.

---

## Assessment

1. **9f1ea9e’s goal is valid** (visual balance, VICE constants), but the implementation assumed the extra left 8 px are a harmless border extension. On open-border, cycle-timed demos they are a **different X band and paint cycle**.

2. **EoD is a hard kill test** for any left pad:
   - Solid black full-width frame lines (no 8 px foreign colour).
   - Fluid checker with **no** first-column period error (no duplicate, no B0C slab that merges with the first cell).

3. **You cannot invent the pad from:**
   - pure modular wrap (park / early pipes),
   - pure duplicate of X=0–7 (2× column),
   - pure B0C (gutter / merge with checker).

4. **VICE is not a free pass** until c64m’s crop is proven dbuf-identical. Local VICE `DBUF_OFFSET` suggests the published window is not “start at 496”; do not cargo-cult +8 without dumps.

5. **Preferred future direction** (not implemented):
   - Capture VICE and c64m the same frame of this snapshot (or a minimal open-border + park-at-480 test PRG).
   - Histogram left 8 vs display origin; identify VICE’s first VIC X in the 384 buffer.
   - Only then choose `fb = f(vic_x)` so display sits at 32 **and** the first 8 columns match VICE pixel-for-pixel on EoD and a closed-border screen.
   - Add a regression: load the snapshot (or a unit scene), assert y=245 left 8 are black and left 8 ≠ forced duplicate of x 8–15 on a coarse-checker frame.

6. **If balance is only cosmetic** for the SDL window, prefer frontend letterboxing / crop tweaks that do **not** change machine VIC-X paint — open-border content must stay hardware-correct. Machine frame is what `get-frame` and VICE compares use.

---

## Code touchpoints (as of this log)

| Item | Location |
|------|----------|
| Offset constant | `src/machine/vicii.c` — `VICII_PAL_FRAME_X_OFFSET` (**0**) |
| Map helpers | `vicii_frame_x_offset`, `vicii_vic_x_to_frame_x`, `vicii_frame_x_to_vic_x` |
| Live paint | `vicii_render_live_cycle` — landing via map; compose uses VIC X |
| Snapshot paint | `vicii_make_frame_snapshot_internal` — `fb_x` loop + inverse map |
| Sprite wrap | `vicii_sprite_dx_wrapped` (504 PAL / 520 NTSC) |
| Tests | `tests/machine/test_c64_vicii.c` — `TEST_PAL_FX` (0 when offset is 0) |
| Handoff | `agents/vicii.md` — notes 24/40 and why +8 was backed out |

---

## Minimal repro checklist for a future attempt

1. Build; run snapshot with `-P` and control port; `set-turbo 1`; grab several frames while the checker coarsens.
2. Assert **y=245** (and top black pair): every x in 0..383 is palette 0 (or document VICE ground truth if not).
3. Assert **no systematic** `pixels[y][0..7] == pixels[y][8..15]` on checker rows across many frames (occasional equality by phase is OK; every frame is not).
4. Run full `ctest --test-dir build --output-on-failure`.
5. Spot-check lft-nine / closed-border titles if the pad again includes high-X sprite coverage.

---

## One-line summary

**9f1ea9e centered the 320 display by mapping frame x 0–7 to VIC X 496–503; that band is sprite-parking / early-cycle territory on EoD, not “more left border,” and every pad substitute either reintroduced bleed or a 2× first checker column — so PAL offset is back to 0 until a VICE-proven crop exists.**
