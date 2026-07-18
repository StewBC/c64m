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
- Text, bitmap, multicolor, ECM, invalid modes, border state, DEN/bad-line state,
  sprites, priority, expansion, multicolor, pointers/data fetches, collisions,
  raster IRQ, and timed register writes are implemented.
- The bus scheduler distinguishes Phi1 idle/graphics/sprite-pointer work from
  Phi2 bad-line character and sprite-data work. BA is derived from scheduled Phi2
  accesses with the tested lead/release behavior; AEC and RDY are exposed at
  cycle granularity.
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
- Bad Line Condition is evaluated every cycle like VICE `check_badline` (set or
  clear from DEN + range + **live** YSCROLL; not sticky for the whole line). RC is
  cleared only at UpdateVc (cycle 13 / VICE `VICII_PAL_CYCLE(14)`) if the
  condition holds with **prior-cycle** YSCROLL (`reg11_delay`) — c64m runs the
  CPU before VIC in a cycle, so live `$D011` can already show a same-cycle Phi2
  store; VICE samples Phi1 before that store. Without the delayed RC sample,
  eod-3's mid-line `$5F` (YSCROLL=7) on lines where `(raster&7)==7` restarted RC
  and froze VCBASE for the FLI band. End-of-line advances VC in display state,
  then applies VICE UpdateRc:
  `if (RC==7) idle+VCBASE; if (!idle || bad_line) RC=(RC+1)&7`.
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
- `reg11_delay` is updated from `$D011` at the end of each cycle after fetches.
- Video-matrix/colour latches are bulk-filled at the cycle-14 VC/RC phase when
  badline is already true. Per-cycle c-access does not rewrite the latch (BA is
  still scheduled). That avoids late mid-line badlines (e.g. EoD plasma `$D011`
  at ~cycle 52) from overwriting only the last columns with a mismatched `$D018`
  bank.
- On non-badline display lines, if `$D018` screen bits change **and** BMM is set,
  the matrix latch is reloaded once (`matrix_d018_scr` tracks the last fill).
  Intended for eod-3 FLI→bitmap (`$9x`→`$30`) so FLI char codes are not reused as
  bitmap colour nybbles. Colour is never refreshed off badline (that re-broke
  eod-2). Broader reloads (every line, or every BMM line) also re-broke eod-2.
- eod-3 FLI vertical address (VCBASE): WIP uses delayed YSCROLL on UpdateVc RC
  clear (see Bad Line bullet). VICE oracle R103 `$D011=$5B` @ UpdateVc, RC 4…7,
  VCBASE `$0F0→$118→…`; pre-fix c64m had `$5F`/RC restart and VCBASE stuck.
  **Uncommitted as of 2026-07-18:** user reports earlier-demo visual regressions;
  sister effect after eod-3 still broken. See `eod-handoff.md`. Do not treat this
  as closed or commit until those are resolved. The BA/display vs RC-clear YSCROLL
  **split** is a phase-order compromise (CPU-before-VIC), not a full Phi1 model.
- Badline BA uses RELEASE=3 (sprite windows keep RELEASE=2) for dual-bit IEC
  resume (Robocop). That is a deliberate divergence from classic RELEASE=2; do
  not drop it without another IEC fix — it is covered by `c64_vicii` and
  `c64_robocop_g64`.
- General cycle-perfect demo-scene compatibility is not claimed. `lft-nine` is a
  selected milestone target. Edge of Disgrace's checker requires both the
  full-bleed side-border windows and live VC/RC graphics to continue while DEN
  is low; phase `$01/$79` has been compared directly with a VICE raw screenshot.

## Verification

Preserve PAL sprite BA coverage, NTSC late sprite windows, cross-line sprite
windows, fetch schedule markers, frame timing constants, and the current border/
`lft-nine` regressions. Run `ctest --test-dir build --output-on-failure` after VIC
changes.
