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
- Phase 2: DONE.
    * Root sub-bug found and fixed: the sequencer never reloaded VC from VCBASE
      per line, so VCBASE accumulated +320 per character row instead of +40. It
      was invisible only because the renderer ignored the counters. Fixed to
      Bauer semantics: VC = VCBASE at the start of EVERY line; at the RC==7 line
      end VCBASE advances by 40 and display state leaves; RC increments only while
      below 7 (reset by the next bad line).
    * Renderer is now counter-driven. vicii_background_pixel takes a per-line
      vicii_line_ctx {display_active, cell_base, row_in_cell}:
        - live path fills it from the sequencer (v->display_state / v->vc_base /
          v->rc) so forced/suppressed bad lines shift the address like hardware;
        - snapshot/debug path fills it geometrically (unchanged output);
        - DEN=0 falls back to geometric addressing in the live path too, to
          preserve the documented DEN=0 collision/foreground quirk (FLI needs
          DEN=1, so this never affects the bad-line path).
    * Proven equivalence: for a static screen the counter model's active span is
      exactly [48+YSCROLL, 248+YSCROLL) and the (char_row, RC, col) decomposition
      matches the old positional mapping, so all static tests stay byte-identical.
    * Guardrails: full suite 40/40 green (incl. live text/bitmap/mcm/ecm, DEN,
      xscroll/yscroll, sprite-BA, snapshot round-trip).
    * New acceptance test test_expose_forced_badline_resets_row_counter: a bad
      line forced at raster 53 (then YSCROLL restored) shifts raster 54 from RC=3
      to RC=1 — a badline-memory effect the old (y, YSCROLL)-keyed renderer could
      not produce.
    * Not done here (by design): render still reads RAM live per pixel (latch is
      Phase 3); display/idle is still gated by the fixed 51..251 window (Phase 4);
      mid-line bad-line CONDITION timing is cycle-0 only (Phase 5). End-to-end
      dkarcade visual check is deferred to its phases.
- Phase 3: DONE.
    * The live renderer now takes the character code and colour nibble from the
      latched line buffers (video_matrix[] / color_line[]) instead of reading
      screen/colour RAM live. The g-access (glyph / bitmap byte) stays a live
      per-line fetch, as on hardware. vicii_line_ctx gained {vm_latch, color_latch}
      pointers: non-NULL on the live path (point at the latches), NULL on the
      snapshot/debug path (keeps live RAM reads, since it has no sequencer state).
    * Ordering subtlety resolved: the latch is now filled for the whole line (all
      40 columns) at cycle 0 of a bad line (vicii_fill_line_latch), replacing the
      per-cycle 15-54 c-access loop. The cycle->pixel map draws the left columns
      before cycle 15, so a spread-out fill would have shown stale data there;
      filling atomically at line start guarantees a consistent latch with
      identical values (same RAM, same VC). BA is modelled separately (cycle 12),
      so BA timing is unchanged.
    * Guardrails: full suite 40/40 green. The live text/bitmap/mcm/ecm tests now
      exercise the latch and stayed byte-identical; savestate still round-trips the
      latch arrays.
    * New acceptance test test_expose_video_matrix_latched_at_badline: the same
      screen-RAM write yields green (latched) when applied BEFORE the row's bad
      line at raster 51 and red (old value retained) when applied AFTER it,
      proving the row is frozen to its bad-line latch.
- Phase 4: DONE.
    * Idle vs display is now selected by the sequencer's display state on the live
      path, not the fixed 51..251 window. vicii_line_ctx gained idle_when_inactive:
      true on the live path (any line not in display state renders idle-state
      graphics), false on the snapshot/debug path (legacy geometry: idle outside
      the fixed window, B0C blank for inactive rows inside it).
    * Why it is safe: at YSCROLL=3 (which every existing test and the dkarcade
      title use) display state spans exactly 51..250, so the idle region is
      identical to the old fixed window -- normal output is unchanged. The
      behaviour only diverges once per-line $D011 forces/suppresses bad lines mid
      window, which is precisely what the reveal needs (Phase 5).
    * Guardrails: full suite 40/40 green, including the PAL and NTSC open-border /
      bottom-border regressions that codify the dkarcade-class technique.
    * New acceptance test test_expose_idle_state_shows_idle_graphics_in_window:
      suppressing the bad line at raster 59 (YSCROLL change) leaves display state
      off inside the window, so that line renders idle graphics (black) rather than
      the bitmap or the red B0C background the pre-Phase-4 blank produced.
    * End-to-end smoke: the current build renders a settled dkarcade picture
      deterministically over the control port (frame 442, black border, ~242 lit
      rows; two runs byte-identical) with no crash. A rigorous pre/post pixel diff
      was attempted via a worktree of the pre-refactor baseline but the control
      path's run-cycles/wait-frame timing is not consistent across builds; the
      pixel-exact VICE-referenced check is deferred to Phase 6 as planned. Capture
      scripts kept in the session scratchpad for reuse.
- Phase 5: MECHANISM DONE; does NOT fix the dkarcade reveal (see finding).
    * Implemented per-cycle Bad Line Condition evaluation. The condition is now
      re-checked every cycle (v->bad_line acts as the per-line "already committed"
      latch, cleared at cycle 0), so a $D011 write after cycle 0 can still force a
      bad line on its own line -- entering display state, resetting RC and latching
      the row. An ordinary bad line still commits at cycle 0, so all prior output
      is unchanged.
    * Guardrails: full suite 40/40 green.
    * New acceptance test test_expose_midline_d011_forces_badline: a $D011 write at
      cycle 20 of raster 53 restarts the row so raster 54 shows RC=1 -- impossible
      with cycle-0-only evaluation. The mechanism works.

- FINDING (blocks the original premise): Phase 5 does NOT change dkarcade at all.
    * Deterministic control-port sweeps of the reveal are byte-identical between
      Phase 4 and Phase 5. The documented ~27-frame plateau (here frames 270-297,
      lit=194, one repeated hash) persists unchanged.
    * A register-write trace of a plateau frame shows YSCROLL is CONSTANT at 3 for
      the whole frame. The per-line $D011 writes are $33/$73 (and one $3b/$7b):
      identical except bit 7 (RST8, the raster-IRQ compare high bit). $D016/$D018
      are essentially static. There is NO YSCROLL/bad-line manipulation.
    * Therefore the reveal is NOT the FLI/bad-line effect that
      VICII_EXPOSE_REVEAL.md hypothesised. The per-scanline-badline premise behind
      Phases 2-5 does not apply to this title. The reveal's per-frame progression
      is driven by something else (raster-IRQ scheduling and/or sprite multiplex,
      or progressive bitmap drawing plus a subtle per-frame hardware effect).
    * Note: the reveal already LARGELY animates in c64m (the sweep shows lit rows
      growing 123 -> 194 across frames); only the ~27-frame plateau remains.

- Value retained regardless: Phases 2-5 made the VIC renderer counter-driven,
  latch-based, sequencer-idle, and per-cycle bad-line accurate -- all real
  accuracy improvements, unit-tested and regression-clean, and prerequisites for
  genuine FLI titles. They are worth keeping independent of the reveal.

- RECOMMENDATION: pause the phase plan and re-diagnose the actual reveal mechanism
  against a VICE frame-by-frame capture before writing more code. Update
  VICII_EXPOSE_REVEAL.md once the true mechanism is known (its current "Actual
  mechanism" section is contradicted by the YSCROLL-constant trace).

- Phase 6: NOT STARTED (VICE-referenced validation; blocked on the re-diagnosis).
```
