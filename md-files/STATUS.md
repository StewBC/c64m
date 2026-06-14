# STATUS.md

## Current State

Completed through Phase 6.

Implemented:

- 6510 CPU integrated through C64 bus.
- RAM, ROMs, banking, and address decoding.
- ROM loading and reset-vector boot path.
- Runtime thread and command/event model.
- Run, pause, reset, cycle-step, instruction-step.
- Frame pipeline with copied runtime-to-frontend handoff.
- VIC-II skeleton:
  - register storage
  - register mirroring
  - raster timing foundation
  - frame generation
  - border/background rendering
- SDL display of machine-generated frames.

## Not Implemented

- Character/screen RAM rendering.
- Character ROM display.
- Color RAM.
- CIA #1.
- CIA #2.
- IRQ/NMI bring-up.
- Keyboard matrix.
- Sprites.
- SID.
- BASIC startup screen.

## Next Phase

Phase 7.

Goal:

```text
screen RAM
    + character ROM
    + color RAM
        -> visible PETSCII character display
```
