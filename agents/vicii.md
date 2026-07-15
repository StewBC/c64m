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
  PAL 352x248 from Y=28 (rows 28..275), NTSC 352x224 from Y=39 (rows 39..262).
  The display window is 51..250 on both standards, so NTSC has only 12 border
  lines below it; the 224-row crop takes 12 above and 12 below. Do not give NTSC
  a PAL-sized 248-row crop - it runs 13 rows past raster 262 and exposes the
  frame's fill colour as a band under the picture.
- Display aspect follows the frame, since the crops differ in height. The
  `True Aspect Ratio` option applies the pixel aspect ratio (PAL 0.9365, NTSC
  0.7500 - see codebase64.c64.org/doku.php?id=vic:pixel_aspect_ratio), giving the
  real-world geometry: ~1.33 PAL, ~1.18 NTSC. Do not restore the old hardcoded
  4:3: it is right for PAL only by coincidence (352x248 at PAL's PAR *is* 4:3)
  and stretches NTSC ~13% too wide. With the option off the picture is not
  corrected at all: it stretches to fill the view. Do not substitute a
  square-pixel aspect there - the display free-scales to its pane (there is no
  1:1 or integer-zoom mode), so square pixels would buy no pixel-exactness, just
  a second wrong shape.
- Unpainted pixels are filled with $D020. Everything outside the display window
  is border, and that includes the DEN=0 case: the vertical border flip-flop
  never opens, so the border covers the whole screen rather than showing B0C.
- Text, bitmap, multicolor, ECM, invalid modes, border state, DEN-off blanking,
  sprites, priority, expansion, multicolor, pointers/data fetches, collisions,
  raster IRQ, and timed register writes are implemented.
- The bus scheduler distinguishes Phi1 idle/graphics/sprite-pointer work from
  Phi2 bad-line character and sprite-data work. BA is derived from scheduled Phi2
  accesses with the tested lead/release behavior; AEC and RDY are exposed at
  cycle granularity.
- Live rendering tracks main and vertical border flip-flops. The current source
  follows the Bauer 3.9 rule used by the `lft-nine` work: main border covers
  sprites with `$D020`; vertical border blanks graphics to B0C and does not blank
  sprites. DEN=0 blanks graphics to B0C the same way, sprites still muxing - but
  it does NOT put B0C in the border. Main border can only clear while the
  vertical border flip-flop is inactive, and DEN=0 keeps that flip-flop set, so a
  DEN=0 frame is `$D020` throughout and its B0C never reaches the screen. The B0C
  is visible only where DEN is cleared mid-frame, after the border has opened.
- Bad Line Condition is evaluated every cycle like VICE `check_badline` (set or
  clear from DEN + range + YSCROLL; not sticky for the whole line). RC is
  cleared only at cycle 14 if the condition still holds (Bauer 3.7.2). End-of-line
  advances VC in display state, then applies VICE UpdateRc:
  `if (RC==7) idle+VCBASE; if (!idle || bad_line) RC=(RC+1)&7`.
- Raster compare IRQ is edge-triggered on non-match → match. Writing `$D011`
  only re-checks the compare when the 9-bit line actually changes (RST8). A
  mid-line `$D011` YSCROLL write on an already-matching raster must not re-assert
  IRQ (Arkanoid dual-zone soft-scroll chain). Writing `$D012` to the *current*
  line still triggers immediately (Galencia bottom-border chain).
- Sprite X wrapping uses `cycles_per_line * 8`: 504 PAL dots and 520 NTSC dots,
  not a fixed 512-dot wrap.
- Turbo can disable host pixel output while retaining raster, BA, IRQ, sprite-DMA,
  CIA, and SID timing.

## Timing/debugging rules

Use the live path for timing-sensitive behavior. Snapshot rendering is a debugger
and presentation fallback and is not a substitute for live bus timing. Trace builds
can emit `C64M_VICLOG`, `C64M_BALOG`, and `C64M_SPRDMA`; the `lft-nine` workflow
uses these to compare against VICE.

For a timing investigation, classify the defect as (1) bus schedule/access kind,
(2) BA/RDY/AEC arbitration, (3) raster/register timing, or (4) pixel composition.
The tests separate these concerns. Do not fix a pixel symptom by changing CPU
stalls without a trace showing a bus defect. The current working-tree `lft-nine`
effort is sensitive to border flip-flops, `$D011/$D012/$D016/$D017` projection,
sprite MCBASE/data slots, and sprite X wrapping; preserve those edits.

## Known limits

- Light pen `$D013/$D014` is stubbed.
- Last-byte-on-bus behavior and analog/half-cycle AEC/RDY are not modeled.
- Idle g-access and some snapshot rendering are approximations.
- General cycle-perfect demo-scene compatibility is not claimed. `lft-nine` is a
  selected milestone target and remains a focused regression/diagnostic area.

## Verification

Preserve PAL sprite BA coverage, NTSC late sprite windows, cross-line sprite
windows, fetch schedule markers, frame timing constants, and the current border/
`lft-nine` regressions. Run `ctest --test-dir build --output-on-failure` after VIC
changes.
