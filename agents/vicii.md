# VIC-II handoff

## Source of truth

Implementation: `src/machine/vicii.{c,h}` and its integration in `c64.c`/`c64_bus.c`.
Tests: `tests/machine/test_c64_vicii.c`, `tests/machine/test_c64_cpu_validation.c`,
runtime frame tests, and the selected `samples/lft-nine.prg` diagnostics.

The main entry points are `vicii_step_cycle()`, `vicii_ba_active()`,
`vicii_aec_active()`, `vicii_rdy_active()`, `vicii_bus_access()`, register read/write
functions, and the three frame functions (`copy_completed`, `make_frame_snapshot`,
`make_current_frame_snapshot`). `c64_step_cycle()` calls VIC-II inside the machine
Phi2 schedule; frontend frames are copies.

## Current implementation

- PAL 6569 and NTSC 6567R8 timing are selected per machine configuration.
- Full PAL/NTSC frame heights are published: PAL 312 lines, NTSC 263. The
  frontend crop is per-standard and always stays inside the published frame:
  PAL uses the normal-border 384x272 viewport from X=0, Y=20 (rows 20..291),
  which retains complete upper/lower border effects such as EoD's `FLIP`/`DISK`
  labels; NTSC uses 352x224 from X=8, Y=39 (rows 39..262). The display window is
  51..250 on both standards, so NTSC has only 12 border lines below it; the
  224-row crop takes 12 above and 12 below. Do not give NTSC a PAL-sized crop -
  it runs past raster 262 and exposes the frame's fill colour as a band under
  the picture.
- Display aspect follows the frame, since the crops differ in height. The
  `True Aspect Ratio` option applies the pixel aspect ratio (PAL 0.9365, NTSC
  0.7500 - see codebase64.c64.org/doku.php?id=vic:pixel_aspect_ratio), giving the
  real-world geometry: ~1.32 PAL, ~1.18 NTSC. Do not restore a hardcoded 4:3:
  it is only an approximation for PAL and stretches NTSC ~13% too wide. With
  the option off the picture is not
  corrected at all: it stretches to fill the view. Do not substitute a
  square-pixel aspect there - the display free-scales to its pane (there is no
  1:1 or integer-zoom mode), so square pixels would buy no pixel-exactness, just
  a second wrong shape.
- Unpainted pixels are filled with $D020. Everything outside the display window
  is border, and that includes the DEN=0 case: the vertical border flip-flop
  never opens, so the border covers the whole screen rather than showing B0C.
- Text, bitmap, multicolor, ECM, invalid modes, border state, DEN/bad-line state,
  sprites, priority, expansion, multicolor, pointers/data fetches, collisions,
  raster IRQ, and timed register writes are implemented.
- The bus scheduler distinguishes Phi1 idle/graphics/sprite-pointer work from
  Phi2 bad-line character and sprite-data work. Bad-line BA is low on cycles
  11..53; sprite BA uses VICE's explicit live PAL/NTSC per-cycle DMA masks, not
  a synthetic persistent six-cycle window. AEC and RDY are exposed at cycle
  granularity.
- Live rendering tracks main and vertical border flip-flops. The current source
  follows the Bauer 3.9 rule used by the `lft-nine` work: main border covers
  sprites with `$D020`; vertical border blanks graphics to B0C and does not blank
  sprites. DEN gates bad-line arming and the top vertical-border compare; it is
  not a live graphics blanking signal. Clearing DEN after the border/display
  sequencer has opened leaves the running VC/RC graphics pipeline visible. With
  DEN clear from the start, the vertical border never opens and `$D020` covers
  the frame; if software has already opened the border, idle/display output and
  sprites can remain visible underneath it.
- Horizontal-border checks use the VICE `check_hborder` cycles and a delayed
  output pipeline. c64m retains two CSEL samples at the right compare because
  its CPU/VIC projection currently places the same VICE cycle-56 store at c64m
  cycle 56 in Edge of Disgrace and at cycle 55 in lft-nine's CIA-synchronised
  loop. Both in-flight 1-to-0 transitions dodge the compare; a CSEL=0 value that
  is stable for two samples still closes the 38-column border normally.
- Bad Line Condition is evaluated every cycle like VICE `check_badline`, from
  the frame-level DEN latch armed at raster `$30`, the `$30..$F7` range, and live
  YSCROLL. RC is cleared at UpdateVc when badline holds. **Machine order:**
  `vicii_begin_cycle` performs the VIC's
  Phi1/internal work and establishes current-cycle BA/AEC, the CPU may then use
  Phi2, and `vicii_finish_cycle` advances the raster. A same-cycle STA `$D011`
  is therefore too late for that cycle's UpdateVc/badline; the delay latch sees
  it on the next VIC cycle, in time for the subsequent g-access. UpdateVc is
  VICE `PAL_CYCLE(14)` (0-based **13**). UpdateRc is VICE Phi2(58), 0-based
  **57**, not a line-wrap shortcut.
- Raster compare IRQ is edge-triggered on non-match → match. Writing `$D011`
  only re-checks the compare when the 9-bit line actually changes (RST8). A
  mid-line `$D011` YSCROLL write on an already-matching raster must not re-assert
  IRQ (Arkanoid dual-zone soft-scroll chain). Writing `$D012` to the *current*
  line still triggers immediately (Galencia bottom-border chain).
- Sprite collision IRQs (IMMC/IMBC) edge-trigger only when `$D01E`/`$D01F` go from
  zero to non-zero (Bauer / VICE). Acking `$D019` while the collision latch is
  still set must not re-fire; a CPU read of `$D01E`/`$D01F` clears the latch so
  a later overlap can IRQ again. Sticky re-assert broke Potty Pigeon (`$D01A=$05`
  with an IRQ path that only acks raster `$01`).
- Sprite X wrapping uses `cycles_per_line * 8`: 504 PAL dots and 520 NTSC dots,
  not a fixed 512-dot wrap.
- Turbo 1..7 publishes the live per-cycle ARGB framebuffer. Turbo 8+ disables
  host pixel output while retaining raster, BA, IRQ, sprite-DMA, CIA, and SID
  timing; its published frame is geometric debug reconstruction, not visual
  evidence for timing-sensitive effects.

## Timing/debugging rules

Use the live path for timing-sensitive behavior. Snapshot rendering is a debugger
and presentation fallback and is not a substitute for live bus timing. Drain any
old frame payloads, use turbo 7 or lower, and discard one completed frame after
lowering turbo before judging a capture. Trace builds can emit `C64M_VICLOG`,
`C64M_BALOG`, and `C64M_SPRDMA`; the `lft-nine` workflow uses these to compare
against VICE.

For a timing investigation, classify the defect as (1) bus schedule/access kind,
(2) BA/RDY/AEC arbitration, (3) raster/register timing, or (4) pixel composition.
The tests separate these concerns. Do not fix a pixel symptom by changing CPU
stalls without a trace showing a bus defect. The current working-tree `lft-nine`
effort is sensitive to border flip-flops, `$D011/$D012/$D016/$D017` projection,
sprite MCBASE/data slots, and sprite X wrapping; preserve those edits.

## Known limits

- Light pen `$D013/$D014` is stubbed.
- Last-byte-on-bus behavior and analog/half-cycle AEC/RDY are not modeled.
- Idle g-access reads the ghost byte ($3FFF / $39FF in ECM) with c-data forced to
  0. MCM text idle is **hires** (colour-RAM bit 3 is 0); only MCM bitmap idle
  stays multicolor (matches VICE `draw_graphics` when `cbuf==0 && !BMM`).
  Invalid modes (ECM with BMM and/or MCM) are solid black in idle as well as
  display — otherwise the ghost-byte MCM-bitmap path stipples EoD plasma's
  post-FLI bottom frame (`$D011=$71`).
- Vertical border uses VICE's two-stage unit: `set_vborder` latch (bottom only
  sets) and `vertical_border_active` (applied at cycle 0 and left compare).
  Top+DEN clears both. This is required for the classic RSEL lower-border open.
- Bad lines: DEN is sampled on raster `$30` into `allow_bad_lines` for the rest
  of `$30–$F7` (Bauer/VICE). Live DEN gates top open but does not blank an
  already-running graphics pipeline.
- g-access stores the fetched glyph/bitmap byte into `g_line[col]` using
  `reg11_delay` (prior-cycle `$D011`) for BMM/ECM address bits — VICE's one-cycle
  mode delay for fetches. The live paint path uses `g_line` rather than re-reading
  RAM, so mid-line `$D018`/mode changes do not re-decode already-fetched columns.
  Snapshot rendering still re-reads RAM (no sequencer history).
- Live paint uses `xscroll_pipe`: after each cycle's CPU Phi2 store, on g-access
  cycles **15..54** only, latch `$D016` XSCROLL for the next cycle's paint.
  Cycles **0..14** are excluded (would re-latch a still-live previous-line
  `$62` before the first matrix cell). Cycles **55+** are excluded (EoD's
  open-border dodge `$D016=$62` / XSCROLL=2 at the right compare). Either
  mistake pads x=24 with B0C — the solid vertical fine-checker line. Snapshot
  path uses live `$D016`.
- `reg11_delay` samples `$D011` at the end of `vicii_begin_cycle`, after this
  cycle's VIC fetches but before the CPU's same-cycle Phi2 write. A same-cycle
  CPU store therefore cannot affect the following cycle's g-fetch; it reaches
  the delay latch on the next cycle instead.
- UpdateVc sets `VC=VCBASE` and `VMLI=0` at cycle 13. Bad-line c-accesses run on
  Phi2 cycles 14..53. Display-state g-accesses run on Phi1 cycles 15..54, use
  the current VC/VMLI, then increment both; the same cycle's following c-access
  fills the newly selected matrix/colour slot. This per-cycle ordering is what
  makes late `$D011`/`$D018` changes, FLI, and line crunch agree without bulk
  matrix reloads or scene-specific `$D018` repair.
- At cycle 57, UpdateRc enters idle and copies `VCBASE=VC` when RC was 7, then
  increments RC and re-enters display state when the VICE condition holds.
- Badline BA/RDY is low on cycles 11..53 for c-accesses 14..53. It has no
  artificial post-fetch hold: AEC blocks the final Phi2 on cycle 53 and the CPU
  resumes at 54. Sprite BA is re-evaluated from the VICE masks every cycle,
  including cross-line starts and releases. `c64_robocop_g64` still passes with
  this schedule.
- The combined EoD model is now verified in one tree: the earlier face/3D band
  stays coherent, eod-3 remains contained in its black frame like VICE, and the
  following sister imagery plus the earlier open-border/checker/plasma scenes
  advance normally. See `eod-handoff.md` for captures and the resolved trace.
- General cycle-perfect demo-scene compatibility is not claimed. `lft-nine` is a
  selected milestone target. Edge of Disgrace's checker requires both the
  full-bleed side-border windows and live VC/RC graphics to continue while DEN
  is low; the left-edge solid column was an XSCROLL=2 B0C pad from sampling the
  open-border `$D016=$62` dodge into `xscroll_pipe` (fixed: sample only on
  g-access cycles 15..54 after the CPU store).

## Verification

Preserve PAL sprite BA coverage, NTSC late sprite windows and fetch slots
(`58,60,62,64,1,3,5,7`), cross-line sprite windows, frame timing constants, and
the current border/`lft-nine` regressions. Run `ctest --test-dir build
--output-on-failure` after VIC changes.
