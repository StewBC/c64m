# C64VICPHASE_J.md
# VIC-II Phase J — DEN-Off Visual Blanking

## Purpose

Implement the VIC-II display-enable (`DEN`, `$D011` bit 4) visual blanking behavior.
This phase is intentionally small and focused. Phase I is intentionally skipped.

The current implementation uses `DEN` for Bad Line eligibility, but the pixel renderer
still draws character/bitmap foreground pixels when `DEN=0`. This makes screen blanking
appear not to work.

## Required Reading

1. `md-files/AGENTS.md`
2. `md-files/STATUS.md`
3. `md-files/C64MVICII.md`
4. `md-files/C64VICPHASE_E.md`, especially the DEN clear collision notes

## Goal

When `DEN=0`, the display output should be visually blanked to background color 0
(`$D021`) while sprites and collision detection continue to behave correctly.

## Scope

In scope:

- Live VIC-II rendering.
- Compatibility/snapshot rendering if it can be reached before a completed live frame.
- Regression tests in `tests/machine/test_c64_vicii.c`.
- A small refactor of the background/display pixel helper if needed to separate:
  - the underlying graphics foreground classification used for sprite-background
    collision, from
  - the visible background/display color emitted when `DEN=0`.

Out of scope:

- Reworking the renderer to use the Bad Line video matrix/color line buffers.
- Changing Bad Line timing, BA/AEC behavior, VC/RC state, or sprite DMA timing.
- Changing sprite Y positioning.
- Implementing light pen or last-byte-on-bus behavior.
- General VIC-II cleanup or broad performance rewrites.

## Required Behavior

### Visual Output

When `$D011` bit 4 is clear:

- Pixels inside the display window visually use background color 0 (`$D021`).
- Character, bitmap, ECM, MCM, and invalid-mode foreground pixels do not visually show.
- Border-visible pixels also visually use background color 0 (`$D021`) while `DEN=0`;
  `$D020` applies again when `DEN=1`.
- Sprites remain visible outside the border according to the existing sprite priority
  rules.

When `$D011` bit 4 is set:

- Existing display rendering behavior should remain unchanged.

### Collision Behavior

Do not disable collision detection just because `DEN=0`.

- Sprite-sprite collisions must continue to be detected when sprite pixels overlap.
- Sprite-background collision should continue to use the underlying background/display
  foreground classification where the renderer already computes it.
- Visual blanking must not by itself erase the underlying foreground classification used
  for `$D01F`.

This may require the background pixel path to compute an underlying candidate first and
then replace only the visible color with `$D021` when `DEN=0`.

## Suggested Implementation Approach

The likely implementation area is `src/machine/vicii.c`.

`vicii_background_pixel()` currently computes mode, scroll, screen/bitmap/character
addresses, color RAM, visible color, and foreground classification in one helper.

A conservative approach:

1. Keep the existing mode-specific foreground classification logic.
2. Add a `den` check after the underlying candidate has been computed.
3. If `DEN=0`, return a pixel whose visible color is background color 0 (`$D021`) but
   whose `foreground` field still reflects the underlying graphics candidate.
4. Make sure the early returns for border/outside-display and scroll padding stay
   sensible: those are background, not foreground.

Avoid changing sprite fetch, sprite activation, BA timing, or CPU event scheduling.

## Acceptance Criteria

- With `DEN=1`, an existing standard text foreground pixel still renders as the cell's
  color RAM color.
- With the same screen/character/color setup and `DEN=0`, that foreground pixel renders
  as `$D021`.
- With `DEN=0`, an adjacent background pixel also renders as `$D021`.
- With `DEN=0`, border-visible pixels render as `$D021`, not `$D020`.
- With `DEN=0`, a front-priority sprite in the display area still renders above the
  blanked background.
- With `DEN=0`, sprite-sprite collision still latches `$D01E`.
- With `DEN=0`, sprite-background collision still latches `$D01F` when an opaque sprite
  overlaps an underlying foreground graphics pixel.
- Existing VIC-II regression tests continue to pass.

## Suggested Tests

Add focused tests to `tests/machine/test_c64_vicii.c`.

Recommended test cases:

1. `test_den_clear_blanks_text_display`
   - Set standard text mode with `DEN=1`, `RSEL=1`, `YSCROLL=3`, `CSEL=1`,
     `XSCROLL=0`.
   - Set `$D020` to red, `$D021` to blue.
   - Put character 1 at screen cell 0 and color RAM 0 to green.
   - Verify the glyph foreground pixel is green with `DEN=1`.
   - Clear `DEN` while leaving the rest of `$D011` unchanged.
   - Render again and verify the same pixel is blue.
   - Verify a border-visible pixel is blue, not red.

2. `test_den_clear_keeps_sprite_visible`
   - Clear `DEN`.
   - Place a solid front-priority sprite in the display window.
   - Verify the sprite color is visible over the blanked display area.

3. `test_den_clear_keeps_sprite_collisions`
   - Clear `DEN`.
   - Put a sprite over an underlying text foreground pixel.
   - Verify `$D01F` latches for sprite-background collision.
   - Optionally overlap two sprites and verify `$D01E` still latches.

Prefer live-frame tests through the existing `make_live_frame()` helper. If the snapshot
fallback can be tested without a completed live frame, include a direct snapshot case as
well.

## Notes

This phase deliberately does not solve the larger accuracy/performance issue where the
renderer rereads screen/color/bitmap memory per pixel instead of consuming the Bad Line
line buffers. That should be a separate phase because it affects raster timing,
mid-frame memory writes, and rendering architecture more broadly.

Sprite Y activation currently has project-specific tests around the top border behavior.
Do not change sprite Y positioning in this phase.
