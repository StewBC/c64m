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
- Published frame height is PAL 272 or NTSC 263; the fixed pixel buffer remains
  sized for the PAL maximum. Because NTSC has only 263 raster lines, the frontend
  selects PAL crop Y=31 or NTSC crop Y=23 so the 240-line crop keeps the display
  window and full bottom border on-screen without overrunning the shorter frame.
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
