# VIC-II status

## Current implementation

- VIC-II is complete through Phase J except skipped light pen.
- Live raster timing is implemented.
- Timed bus-visible writes are implemented.
- PAL and NTSC frame sizes are supported.
- Text, bitmap, multicolor, ECM, and invalid modes are implemented.
- Sprites are implemented, including priority, collisions, expansion, multicolor, sprite pointer/data fetch, and IRQs.
- Open/unused register reads are implemented according to the current Phase G policy.
- PAL and NTSC sprite BA stealing are implemented.
- The VIC-II has an explicit per-cycle, two-phase fetch schedule: Phi1 selects
  graphics/idle/sprite-pointer work and Phi2 selects bad-line c-access or
  sprite-data work. BA is derived directly from the scheduled Phi2 accesses:
  three-cycle lead time and the tested two-cycle release margin.
- DEN-off blanking is implemented.
- The live renderer models the vertical border as state, so timed `$D011`/RSEL changes can open the top/bottom border area and reveal sprites in the central display-width region.
- The live renderer models the horizontal border flip-flop for timed `$D016`/CSEL
  side-border opening. The per-cycle sprite sequencer tracks MCBASE and the live
  `$D017` Y-expand bit at VICE's 0-based cycle indices (MCBASE update 15, DMA-on
  54/55, expand toggle 55, display reload 57). Sprite DMA becomes active on the
  raster line that matches the sprite Y. The `$D017` Y-expand clear crunch
  bit-magic fires on cycle 14 (`VICII_PAL_CYCLE(15)`), so mid-line expand toggles
  used by `samples/lft-nine.prg` keep flanking sprites DMA-active past 21 rows
  (matches VICE R9-R73). The timed six-write kernel still starts a few lines
  later than VICE; see `md-files/lft-nine.md` Session 11.
- Sprite bus arbitration follows the live DMA state, not the renderer's
  per-line `sprite_visible` data latch. After the MCBASE==63 DMA-off check,
  later sprite slots on that line no longer steal Phi2 cycles. The renderer
  retains its data latch independently.

## Turbo scales back rendering (capture trap)

At `--turbo>=8` (`RUNTIME_TURBO_DISPLAY_THRESHOLD` in `runtime_thread.c`) the
runtime **disables the live per-cycle ARGB renderer**
(`c64_set_video_output_enabled(false)`) and publishes frames via
`c64_make_current_frame_snapshot` -- the geometric/debug renderer, which draws a
**closed CSEL border and masks every sprite in the border region**. So a
control-port `get-frame` (or the on-screen display) under turbo>=8 is NOT the
real live output: anything drawn in an opened border (side-border demos, sprites
in the border) disappears and the border looks solid. **To inspect real VIC
output via `get-frame`, run at `--turbo<=7` or no turbo** (the live
`c64_copy_completed_frame` path). A settled paused `get-frame` also uses the
geometric snapshot. This trap cost a full lft-nine debugging pass; see
`md-files/lft-nine.md` Session 6.

## Important invariants

- The machine owns the monotonic master cycle.
- VIC, CIA, and SID hooks advance to timestamped CPU bus events before visible side effects.
- Live frame publication uses completed live VIC-II frame buffers.
- Snapshot rendering remains only as fallback/debug before a live frame exists.
- BA/RDY is the early warning that holds CPU read cycles; AEC falls only when
  the VIC owns an actual scheduled Phi2 slot.
- CPU writes may continue during BA/RDY-only lead/release cycles, but AEC-low
  blocks every CPU bus access, including writes.
- VIC memory reads are bank-aware through CIA #2 port A.
- Character ROM is visible only in VIC banks 0 and 2 at the normal ranges.
- `$D011` DEN=0 blanks the visible display and border color to `$D021`, while preserving sprite visibility and collision behavior.

## Recent changes

- Fetch scheduling is now explicit for character/color c-accesses, graphics
  g-accesses, idle g-accesses, sprite pointers, and sprite data. Bad-line
  character/color latches are updated in their individual cycles, and sprite
  pointer/data reads run in their PAL/NTSC slots (including the existing
  cross-line sprite 3/4 layout). `vicii_bus_access()` reports Phi2 work and
  `vicii_bus_access_phi1()` reports Phi1 work. The live renderer retains its
  prior complete-row sprite latch as a presentation pipeline, avoiding a visual
  regression while the scheduled reads become the timing authority.

- Raster IRQ now re-triggers when `$D011`/`$D012` updates make the 9-bit compare
  equal the *current* raster mid-line (hardware behaviour). Previously IRQ was
  only evaluated at cycle 0 of a matching line, so Galencia NTSC's bottom-border
  IRQ chain skipped its cleanup slice every other frame (flashing status bar,
  half-rate music). Regression:
  `test_raster_compare_write_triggers_same_line_irq`.
- PAL published frame height is the full 6569 raster: 312 (lines 0..311), matching
  `lines_per_frame`. Frontend still crops 352x248 at Y=28 for normal display so
  top-border scores and bottom-border HUDs (e.g. Galencia) remain visible without
  showing full blanking. No change to BA windows or raster numbering
  (`frame Y == raster_line`). Regression:
  `test_live_deep_bottom_border_sprite_is_painted`.
- Fixed PAL dkarcade2016 "expose" reveal: sprite BA window widened from 5 to 6
  cycles so the stable-raster kernel's first wait locks to `$D012`. Also project
  deferred free-run reads of `$D011`/`$D012` to the bus-access cycle offset.
  See [../../C64MVICIIEXNEXT_UPD.md](../../C64MVICIIEXNEXT_UPD.md).
- C64MENH Phase 2 added per-standard NTSC sprite BA timing.
- Sprite data slots select PAL 6569 or NTSC 6567R8 timing tables; the common
  scheduler derives BA/RDY lead and release from those slots rather than using
  an independent BA-assert table.
- Vertical border compares are raster-line numbers and are identical for PAL
  (6569) and NTSC (6567): top/bottom 51/251 for RSEL=1 and 55/247 for RSEL=0.
  Only the total line count differs, not the display-window position, so the
  display window opens at line 51/55 on both standards. This keeps the
  background (mapped through the top compare) aligned with sprites, which are
  placed by absolute raster line.
- Published frame height is PAL 312 or NTSC 263 (full raster for each standard).
  The fixed pixel buffer is sized for the PAL maximum. Frontend shows a 352x248
  crop starting at Y=28 (rows 28..275) so the 200-line display window,
  top-border scores, and deep bottom-border sprites stay on-screen without a
  full-overscan window. NTSC frames are padded in the display texture to the PAL
  height so both standards share the crop.
- Existing PAL sprite BA tests still cover single, adjacent, split-window, cross-line, inactive, and unified BA-predicate behavior.
- NTSC tests cover the 65-cycle late sprite window and sprite 4 cross-line window.
- Galencia NTSC sprite multiplex corruption was fixed by preserving RAM underneath visible `$D000-$DFFF` I/O writes; VIC sprite data fetched from RAM under I/O is no longer overwritten by VIC register writes.
- Bottom-border opening is covered by live-frame regressions for both standards: after the 24-row bottom compare has been missed, clearing RSEL before the 25-row bottom compare keeps the vertical border open and allows sprites in the bottom-border region to show.

## Known limitations / deferred

- VIC-II light pen is stubbed at `$D013/$D014`; Phase F was skipped.
- Cycle-perfect video timing is not complete.
- Last-byte-on-bus open-bus behavior is not implemented.
- Unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access (`$3FFF` / `$39FF`) is now rendered outside the vertical display window (opened-border pictures); it is a per-mode approximation, not a full cycle-exact idle sequencer.
- The snapshot/debug renderer still uses geometric side borders; the live path
  uses the horizontal border flip-flop and dot-anchored CSEL timing.
- General demo-scene cycle-perfect behavior remains unclaimed beyond selected
  milestone targets; `samples/lft-nine.prg` is now an in-scope VIC-II timing
  target.
- AEC/RDY are cycle-level signal states, not an analog or half-cycle waveform
  simulation.
- The renderer's sprite-row pre-latch remains an implementation pipeline rather
  than a literal representation of the previous-line DMA latch. The bus schedule
  itself performs the pointer/data accesses in their cycle slots.
- A single sprite's derived BA/RDY interval remains six cycles where required
  for the `samples/dkarcade2016.prg` PAL stable-raster reveal (matches VICE
  x64sc `$D001` multiplex at r53/c~30 and r272/c~36).
- The trace build (`-DC64M_VIC_TRACE`) has `C64M_SPRDMA=<path>` in addition to
  `C64M_VICLOG` and `C64M_BALOG`. It records the cycle-58 active-DMA mask,
  `$D015`, sprite Y values, and MCBASE latches for lft-nine/VICE comparison.

## Tests / smoke checks

- Preserve PAL sprite BA coverage.
- Preserve NTSC sprite BA coverage.
- Verify display mode behavior when changing `$D011`, `$D016`, `$D018`, sprite registers, and video standard.
- Use the hardware view to inspect raster, IRQ, register, color, BA, and sprite state when debugging.

## Files likely involved

- `src/machine/vic*`
- `src/machine/c64*`
- `src/runtime/*`
- VIC and runtime tests under `tests/`
