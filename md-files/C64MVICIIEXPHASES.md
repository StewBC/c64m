# C64MVICIIEXPHASES — Per-scanline (FLI/badline-class) VIC-II accuracy

## Purpose

Bring the VIC-II live renderer to **per-scanline accuracy** so that per-line
`$D011` (YSCROLL/RSEL) manipulation driven from a raster IRQ produces the correct
display progression. The concrete acceptance driver is the `dkarcade2016.prg`
open-border **"expose" reveal**, which c64m currently cannot reproduce (it emits
~27 byte-identical frames during the reveal). See
[docs/status/VICII_EXPOSE_REVEAL.md](docs/status/VICII_EXPOSE_REVEAL.md) and
[docs/status/VICII.md](docs/status/VICII.md).

This work was previously deferred as out of scope in `AGENTS.md`. It is now **in
scope and required**; this document is the phase plan and running record.

## Root cause (from the code, `src/machine/vicii.c`)

There are two disconnected models:

```text
1. A badline SEQUENCER that is maintained but never drawn from.
   vicii_step_cycle tracks vc, rc, vc_base, display_state, bad_line and fills
   the line latches video_matrix[] / color_line[] during the c-access window
   (cycles 15-54 on bad lines). These latches are WRITTEN (vicii.c:937-938) and
   READ NOWHERE. vc/rc/display_state feed only BA stalling and the raster IRQ.

2. A POSITIONAL renderer that produces every pixel.
   vicii_render_live_cycle -> vicii_live_pixel -> vicii_background_pixel derives
   the displayed address GEOMETRICALLY from (x, y) and the CURRENT YSCROLL,
   against a fixed 51/251 window, and re-reads screen/bitmap/color RAM LIVE per
   pixel. It consults neither VC/RC nor the latched line buffer.
```

FLI/expose works by using per-line `$D011` writes to force/suppress bad lines,
which advance RC/VC differently per line. Because c64m's pixels come from a
geometric map that ignores those counters, per-line `$D011` writes only shift the
whole mapping uniformly and cannot reproduce the progression.

## What is already in place (do not rebuild)

```text
- Per-cycle register writes are cycle-accurate: CPU bus events carry a
  cycle_offset and are applied to the VIC BEFORE that VIC cycle steps
  (c64.c:1041). A mid-line $D011 write already lands at the correct cycle.
- Cycle -> pixel-column mapping exists, so per-cycle COLOR changes (raster bars)
  already work. The gap is ADDRESS generation and badline STATE, not color timing.
- The badline sequencer counters already tick; they must become the renderer's
  source of truth.
- The idle-graphics path (vicii_idle_pixel) already exists; it must be selected
  by state instead of by the fixed y-window.
```

## Guiding constraints

```text
- Keep the tree working at every phase boundary (no destructive re-splitting of
  working code). Each phase leaves all existing tests green.
- Preserve per-cycle rendering (needed for mid-line color effects). Do NOT switch
  to whole-line end-of-line rendering.
- Regression guardrails first: static text/bitmap/mcm/ecm frames and existing
  sprite-BA and open-border tests must stay byte-identical unless a phase
  explicitly and intentionally changes them.
```

## Phase plan

### Phase 1 — Foundation: measurement harness and guardrails

Goal: pin the target and lock current-correct behavior before touching the
renderer.

```text
- Add a deterministic per-scanline injection rig to tests/machine: step the
  machine cycle-by-cycle and apply register writes ($D011, $D018, $D021, ...) at
  exact (raster_line, cycle_in_line) points, complete the frame, inspect pixels.
- Add a reveal-progression metric helper: count raster rows containing any lit
  pixel in x in [24, 344). This is the signal used to measure the reveal.
- Lean on existing make_live_frame static-screen tests as regression guardrails;
  add explicit coverage where thin.
- Add a characterization test for a minimal per-line-$D011 (FLI) pattern that
  records the CURRENT positional output, tagged as behavior later phases change.
```

Acceptance:

```text
- New harness compiles and runs under ctest.
- All existing VIC-II tests still pass.
- Baseline metrics (positional FLI output, reveal lit-row curve if measurable in
  a unit test) recorded in this document.
```

Risk: low. No renderer changes.

### Phase 2 — Counter-driven address generation

Goal: replace geometric `char_row` / `row_in_cell` / `cell` derivation with
VC/RC/VMLI-driven addressing; keep live RAM reads for isolation.

```text
- Feed the render path from the sequencer: row-in-cell = RC, video-matrix column
  index from VC/VMLI, instead of (y, yscroll) geometry.
- Establish display_state correctly at the top of the visible region (first bad
  line), so the first displayed row uses the right counters.
- Keep reading RAM live for now to isolate the addressing change from the latch
  change (Phase 3).
```

Acceptance:

```text
- All static-screen guardrails byte-identical (normal YSCROLL scrolling and
  vertical positioning unchanged).
- New test: a forced mid-frame bad line shifts the displayed row (RC reset) the
  way hardware does and the positional model did not.
```

Risk: highest-correctness change. YSCROLL smooth-scroll and vertical alignment
for ordinary software must not regress.

### Phase 3 — Render from the latched line buffer

Goal: draw characters/colors from `video_matrix[]` / `color_line[]` (plus a
g-data latch) instead of per-pixel live RAM reads.

```text
- Persist the VM/color latch across non-bad lines (hardware behavior) and refresh
  it on bad lines.
- Add g-access latching for the displayed byte.
- Resolve per-cycle render vs c-access ordering: the column rendered at a given
  cycle must use the latch value valid at that cycle.
```

Acceptance:

```text
- Static guardrails byte-identical.
- New test: screen RAM mutated mid-frame AFTER the bad line shows the latched
  (old) data for the rest of the frame, matching hardware.
```

Risk: medium. Ordering between the c-access fill and the per-cycle draw.

### Phase 4 — Display/idle state from the sequencer

Goal: select display vs idle graphics from `display_state`/badline, not the fixed
51/251 y-window.

```text
- Choose the idle path (vicii_idle_pixel) by sequencer state.
- Leave the vertical-border overlay driven by the RSEL compares (separate, already
  correct concern).
```

Acceptance:

```text
- Open-border idle-fill tests still pass.
- The dkarcade2016 STATIC picture still matches (no regression from decoupling the
  fixed window).
```

Risk: medium. Must not reopen the border/idle fixes recorded in
VICII_EXPOSE_REVEAL.md.

### Phase 5 — Mid-line `$D011` badline-condition timing (the FLI mechanism)

Goal: make a `$D011` YSCROLL write at the correct cycle force/suppress a bad line
per the Bauer cycle-13/14 badline-condition semantics, driving RC/VC/display_state.

```text
- Re-evaluate the badline condition when $D011 is written mid-line.
- This is where the expose reveal should begin animating.
```

Acceptance:

```text
- The Phase 1 reveal-progression metric advances during the previously frozen
  ~27 frames; plateau frames are no longer byte-identical.
```

Risk: high. Touches the same timing code as the sprite-BA tests.

### Phase 6 — Validation, tuning, documentation

Goal: match the reveal against a VICE reference and finalize.

```text
- Compare against a VICE capture of dkarcade2016 (reproduction recipe in
  VICII_EXPOSE_REVEAL.md); user to provide the capture or the expected curve.
- Tune remaining discrepancies.
- Update STATUS.md, docs/status/VICII.md, docs/status/VICII_EXPOSE_REVEAL.md,
  docs/status/DEFERRED.md, docs/status/TESTING.md.
```

Acceptance:

```text
- Reveal visually matches VICE within tolerance.
- All guardrails green.
- Docs updated; deferred entry removed/downgraded.
```

## Status log

```text
- Phase 1: DONE.
    * Added a per-scanline measurement harness to tests/machine/test_c64_vicii.c:
        - run_vic_frame_with_injections(): drives the VIC cycle-by-cycle over one
          frame against a real bus, applying register writes at exact
          (raster_line, cycle_in_line) points BEFORE the VIC steps that cycle
          (mirrors c64.c event ordering).
        - count_lit_rows(): reveal-progression metric (rows with any non-bg pixel
          in x in [x0, x1)).
        - setup_full_white_bitmap(): full-window foreground fixture.
    * Tests: test_expose_harness_renders_bitmap_and_metric (validates rendering +
      metric == 200 display rows) and
      test_expose_harness_midline_injection_hits_exact_column (proves per-cycle
      injection lands at the exact pixel column — the capability Phases 2/5 need).
    * Guardrails: full suite 40/40 green; no renderer changes yet.
    * Baseline confirmed: display window is the fixed 200 rows (raster 51..250);
      per-line register injection is pixel-accurate. FLI address/latch behavior is
      unchanged (still positional) as expected pre-Phase-2.
- Phases 2-6: NOT STARTED.
```
