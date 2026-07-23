# Deus Ex Machina investigation handoff

Operational handoff for Deus Ex Machina (DEM) visual work.

Legend used below:

| Tag | Meaning |
| --- | --- |
| **CONCRETE** | Measured in c64m, VICE, pixel compare, or unit tests this session |
| **HYPOTHESIS** | Plausible model not fully proven for *both* scenes |
| **FALSE LEAD** | Tried; disproven or harmful as a sole fix |

---

## Running / reaching the water scene

```sh
./build/c64m -P -a -d 8="./agents/demo/deusexmachina/deus-s1.d64,./agents/demo/deusexmachina/deus-s2.d64"
```

- The demo runs to `PC=$50C3` (hit once per frame in this section) at roughly
  `CYCLES=122,983,230` and beyond. That is the **water woman** scene: a portrait
  whose dress is made of water, the whole image *shimmering* (the shimmer is in
  VICE too — it is the intended effect).
- **Fastest deterministic way to reach it (CONCRETE):** launch headless with a
  control port at `--turbo=2` and let ~37–40 s of *wall clock* elapse, then
  `pause` and `get-frame`. `run-cycles`/breakpoints on `$50C3` are unreliable
  here: `$50C3` fires every frame so a bare breakpoint parks at whatever frame
  turbo has already reached, and the published `get-frame` `cycle=`/`frame=`
  metadata lags the CPU by a lot while running. A snapshot save/restore to reach
  the scene **crashes c64m** (owner will fix separately — do not chase it).
- Full-frame `get-frame` is 384×312 PAL ARGB; the reference `dem-water-sliced.png`
  is the 384×272 crop (Y=20..291) aspect-corrected, so reference-x ÷ 6.198 ≈
  frame-x. Reference pixel (2135, 976) ⇒ frame **x≈344**, water band.

## The defect (CONCRETE)

A thin **blue vertical line at frame x=344**, running the full height of the
water, present on **every second frame** (it shimmers with the scene). Absent in
VICE. `dem-water-sliced.png` is a c64m capture that *contains* the line.

Located by capturing consecutive c64m frames and histogramming the seam columns:

- x=344 is the **first pixel of the right border** (40-col display window is
  `[24,344)`; the demo runs an **opened side border**, so graphics show past 344).
- On the "line" frames x=344 is **~82% colour 14**; on clean frames ~46% (same as
  its neighbours). Colour 14 = `$D021 & $0F` (`$D021=$FE`) = **B0C**. So the line
  is the background colour painted into the first over-border dot.
- Mode: `$D011=$3B` (BMM=1) + `$D016` MCM bit set ⇒ **multicolor bitmap**. Its
  over-border pair-0 colour is B0C — hence colour 14.

## Root cause (CONCRETE)

The demo shimmers by shifting the **whole composite ±1 dot every frame**,
toggling two registers together:

| Frame parity | `$D016` | XSCROLL | Sprite 6 X | x=344 |
| --- | --- | --- | --- | --- |
| "line" | `$19` | **1** | 345 | over-border **B0C** (sprite starts at 345, bitmap col 39 *should* reach 344) |
| "clean" | `$18` | **0** | 344 | sprite 6 covers x=344 |

On the XSCROLL=1 frames the last bitmap column (col 39) is delayed one dot and
should occupy x=337..**344**; sprite 6 moves to X=345. But c64m's over-border
cutoff was the **fixed constant `x >= 344`**, ignoring XSCROLL, so it forced B0C
at x=344 instead of rendering column 39's final pixel. VICE keeps emitting that
pixel: `viciisc/vicii-draw-cycle.c draw_graphics()` loads the graphics shift
register at `i == xscroll_pipe`, so column 39 is output at x=336+XSCROLL..
343+XSCROLL and `gbuf` only falls to 0 (→ B0C) at **x=344+XSCROLL**.

Method note: the layer breakdown was obtained with a temporary `C64M_SEAMLOG`
probe in `vicii_live_pixel` (frame/x/y, bg colour+fg, over-border flag, XSCROLL,
sprite hits), gated by a frame-number window. Removed after diagnosis.

## The fix (CONCRETE)

`src/machine/vicii.c`, `vicii_background_pixel_ex`: the right over-border boundary
is now XSCROLL-aware —

```c
if (x < VICII_HBORDER_LEFT_40 || x >= VICII_HBORDER_RIGHT_40 + xscroll) { /* over-border */ }
```

`xscroll` is the same value the display decode already uses (`xscroll_pipe` live /
`$D016` snapshot). For x in `[344, 344+xscroll)` the pixel now falls through to
the normal display decode, which renders column 39 (`col = (sx_raw-xscroll)/8 =
39`) — exactly VICE's shifted right edge. The left edge (x<24) is unchanged; the
xscroll left fill is still the `sx_raw < xscroll → B0C` case below.

No hack: this makes the over-border boundary match VICE's shift-register pipeline.
It is a **no-op** when XSCROLL=0 (`344+0=344`) or when the main border flip-flop
is closed (`vicii_compose_pixel` overrides with the border colour), so ordinary
screens and closed-border demos are untouched.

## Verification (CONCRETE)

- Water scene, fixed build: x=344 blue-count spike (was 77 every 2nd frame) gone —
  uniform ~45–46 across all frames; both parities render continuous water at the
  seam, no vertical line (pixel dumps + zoomed seam crops).
- `ctest --test-dir build` → **51/51**.
- New guard `test_live_open_border_right_edge_xscroll_delayed`
  (`tests/machine/test_c64_vicii.c`): MCM bitmap, opened right border, a
  foreground pair in column 39. Asserts XSCROLL=0 ⇒ fg at x=343 / B0C at x=344,
  and XSCROLL=1 ⇒ fg carried to **x=344** / B0C at x=345. **Fails** with the fix
  reverted (x=344 = B0C) — proven this session. It replaced the old
  `test_live_open_border_ignores_xscroll`, whose premise ("over-border ignores
  XSCROLL") the VICE source and this bug both disprove; that test only passed
  because its text-mode background was also B0C.
- **EoD checker unaffected (CONCRETE):** its `$D016=$62` (XSCROLL=2) dodge is
  written at cycle 56 and excluded from `xscroll_pipe` (sampled only on g-access
  cycles 15..54), so the paint's effective XSCROLL stays 0 ⇒ boundary stays 344.
  `test_open_border_sprite_matrix_checker_joins` (both passes) still green.
- **lft-nine unaffected (CONCRETE):** its right side border is **closed (black)**
  in the credits/digits scene, so the opened-over-border path is overridden by the
  border colour there regardless of the boundary — visually clean at ~33 s.

## Column-0 orange bar: mid-line `$D016` MCM one-cycle paint skew (CONCRETE, fixed)

A second, unrelated defect in the same title's **band/eye transition** (the scene
between the credits scroller and the water-woman effect). Reachable directly from
snapshot `assets/screenshots/DEM/deus-s1-20260722-142842.c64state` (post-bug), or
watched being drawn from `assets/snapshots/deus-s1-20260722-191023.c64state` at
`--turbo=1`: ~1.5s of fade to an all-black centre, then at ~3.8s wall / frame
10506 an **8px-wide vertical bar** grows top-down in **character column 0**
(x=24..31) over the black centre. Colour is **8 (orange)**, not the D021
background. Absent in VICE.

**Root cause (CONCRETE).** The transition runs a per-line `$D016` toggle:
`$D016←MCM=1` at **cycle 15** (start of display) and `$D016←MCM=0`+XSCROLL-ramp at
**cycle 55** every raster line. The centre is drawn in an invalid/MC mode that
resolves to black. c64m paints a cycle's whole 8-dot span in `begin_cycle`, before
the CPU's cycle-15 Phi2 store, so **column 0 (cycle 15) used the pre-write MCM=0**
(hires text → the character's colour-8 foreground) while columns 1+ (cycle 16+)
saw MCM=1 (→ black). VICE resamples the `$D016` MCM bit mid-cycle
(`viciisc/vicii-draw-cycle.c draw_graphics` `vmode16_pipe`, updated at pixel 4),
so column 0 gets the write and is black. Located with an env-gated `SEAM`/`WREG`
probe (frame/x/y branch, colour, mode; `$D016` write cycle) — removed after
diagnosis.

**Fix (CONCRETE).** `src/machine/vicii.c` `vicii_finish_cycle`: after the existing
`$D020`/`$D021` same-cycle resolution, if the mode's **MCM bit** changed from the
just-painted span's mode on a g-access cycle (15..54, vertical border inactive),
re-decode that span (`hborder_pipe[1]`) with the post-store mode via
`vicii_live_pixel` (new `note_collisions=false` arg so collisions aren't
double-latched). Paint-time mode is stored per span (`hborder_pipe[].mode`); the
bus is stashed (`v->paint_bus`) so finish can rebuild the prep. Trigger is the MCM
bit only, so `$D011` FLI mode changes are untouched. No hack: it mirrors VICE
resolving mode-dependent colours after the CPU Phi2 store, and is a no-op unless
MCM actually flips inside the display window.

**Verification (CONCRETE).** Birth frame 10506 and the eye/water effect frame:
column-0 orange count 245→**0**, centre fully black like VICE; effect left edge
continuous. `ctest` **52/52** plus new guard
`test_live_mcm_toggle_reaches_column0_same_cycle` (fails with the fix reverted:
x=24 = hires palette 2 instead of MC palette 10). lft-nine digits scene unchanged;
EoD unaffected by construction (its `$D016=$62` is at cycle 56, outside 15..54,
and never flips MCM there). See `vicii.md`.

## Still open / not investigated

- **HYPOTHESIS** the "clean" parity relies on sprite 6 covering x=344; that path
  is unchanged and looked correct, but the sprites that build the water detail
  were not audited beyond the seam column.
- Snapshot save/restore crash on this title (owner to handle separately).
