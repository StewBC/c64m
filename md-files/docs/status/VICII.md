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

- C64MENH Phase 2 added per-standard NTSC sprite BA timing.
- Sprite BA now selects a PAL 6569 or NTSC 6567R8 BA-assert table from the machine video standard.
- Existing PAL sprite BA tests still cover single, adjacent, split-window, cross-line, inactive, and unified BA-predicate behavior.
- NTSC tests cover the 65-cycle late sprite window and sprite 4 cross-line window.

## Known limitations / deferred

- VIC-II light pen is stubbed at `$D013/$D014`; Phase F was skipped.
- Cycle-perfect video timing is not complete.
- Last-byte-on-bus open-bus behavior is not implemented.
- Unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access fetch behavior from `$3FFF` / `$39FF` in the renderer is deferred.
- Exact RDY/AEC sub-cycle CPU pin timing is deferred.

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
