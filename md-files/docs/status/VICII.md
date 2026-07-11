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
  sprite-data work. BA continues to use the existing tested lead-window
  predicate; it is not yet derived from individual scheduled accesses.
- DEN-off blanking is implemented.
- The live renderer models the vertical border as state, so timed `$D011`/RSEL changes can open the top/bottom border area and reveal sprites in the central display-width region.

## Important invariants

- The machine owns the monotonic master cycle.
- VIC, CIA, and SID hooks advance to timestamped CPU bus events before visible side effects.
- Live frame publication uses completed live VIC-II frame buffers.
- Snapshot rendering remains only as fallback/debug before a live frame exists.
- Bad Line BA and sprite-fetch BA both stall CPU reads using CPU event read/write classification.
- CPU writes continue where allowed.
- AEC is intentionally not modeled as emulator state. BA is the stall predicate.
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
- Sprite BA now selects a PAL 6569 or NTSC 6567R8 BA-assert table from the machine video standard.
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
- Horizontal border opening is not modeled as a cycle-exact VIC dot flip-flop; side borders still use the current CSEL geometry in the live renderer.
- Exact RDY/AEC sub-cycle CPU pin timing is deferred.
- The renderer's sprite-row pre-latch remains an implementation pipeline rather
  than a literal representation of the previous-line DMA latch. The bus schedule
  itself performs the pointer/data accesses in their cycle slots.
- Sprite BA window is 6 cycles per assert (not 5). That length is required for
  the `samples/dkarcade2016.prg` PAL stable-raster reveal to stay locked to the
  raster (matches VICE x64sc `$D001` multiplex at r53/c~30 and r272/c~36).

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
