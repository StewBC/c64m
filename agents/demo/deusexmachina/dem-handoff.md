# Deus Ex Machina investigation handoff

Operational handoff for Deus Ex Machina (DEM) visual work.

## Pillar "grey -> black dropout" — RESOLVED (2026-07-23)

**Symptom (as reported):** In the pillar scene, once the vertical striped column
parks hard-left, the bottom ~10px of the pillar drop from grey to black; VICE
showed grey there. Snapshot `assets/snapshots/deus-s1-20260722-211000.c64state`
sits at the scene (loads ~fb16185; column parks ~fb16195, dropout appears the
instant it parks).

**Root cause: PAL viewport crop, NOT the VIC-II.** The pillar grey is a
multiplexed **sprite 7** (light grey `$7B`, Y-expanded). Parked, sprite 7 does a
single copy from Y=`$F7`(247) and correctly retracts at **raster 288** when its
mcbase reaches `$3F`(63). Below that the pillar area is black / sprite-6 dark
stripes in **both** c64m and VICE framebuffers — sprite-7 retract at 288 is
textbook VIC-II and byte-identical between the two emulators. The user only saw
a difference because the two viewports crop differently:

| | Visible PAL rasters | Pillar bottom shown |
| --- | --- | --- |
| VICE normal | **16..287** (`VICII_PAL_NORMAL_FIRST/LAST_DISPLAYED_LINE`) | grey to edge; dropout (289+) cropped |
| c64m (old)  | **20..291** (`FRONTEND_DISPLAY_PAL_CROP_Y=20`) | grey to 288, then 3 dropout lines 289..291 exposed |

**Fix (landed):** `src/frontend/frontend.c` `FRONTEND_DISPLAY_PAL_CROP_Y` 20 -> 16
(viewport now 16..287, exactly VICE). 52/52 ctest green. EoD `FLIP`/`DISK` labels
still fully visible (inside 16..287) — verified identical to VICE side-by-side.
**Do not touch the sprite sequencer** to "fix" this; the retract is correct.

### How it was found (repeatable)
- `get-frame` over the control port is the **uncropped** 384x312 frame (y == VIC
  raster). Load the snapshot, capture a settled frame, and read pixels directly:
  solid grey to y288, dropout y289..298, black below. That located the defect in
  observables before any mechanism talk.
- The frontend then applies `crop_y`/`crop_h`, so the bug lived entirely in *what
  the window shows*, not in rendering. Comparing crop windows (c64m 20..291 vs
  VICE 16..287) settled it; VICE's number is in vice src `src/vicii/vicii-timing.h`.

### Dead ends (do not re-run)
- The earlier "c64m loses sprite 7 at 289..291, VICE keeps it — fix sprite-7
  bottom-band lifetime in vicii.c" diagnosis is **WRONG**. Two prior sessions
  burned budget diffing VICE against c64m at *misaligned animation phases* and
  drilling the sprite sequencer. The framebuffer content was always identical.

Companion memory: `project_dem_pillar_root_cause`. VICE compare snapshots the
user saved: `deus-vice-pillar.vsf`, `deus-vide-pillar.vsf`.
