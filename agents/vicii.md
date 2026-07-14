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
- Full PAL/NTSC frame heights are published: PAL 312 lines, NTSC 263. Frontend
  normally displays a 352x248 crop beginning at Y=28.
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
  sprites. DEN=0 uses B0C for main-border pixels while sprites still mux.
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
