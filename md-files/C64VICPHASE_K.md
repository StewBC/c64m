# C64VICPHASE_K.md
# VIC-II Phase K — Line-Buffer-Backed Display Rendering

## Purpose

Move VIC-II display rendering toward the hardware model where Bad Lines latch video
matrix and color RAM data into internal line buffers, and later display pixels render
from those latched buffers instead of rereading screen/color memory per pixel.

This phase is intentionally separate from Phase J. Phase J fixes `DEN=0` visual
blanking. Phase K is a broader accuracy and performance pass and can be revisited later.

## Required Reading

1. `md-files/AGENTS.md`
2. `md-files/STATUS.md`
3. `md-files/C64MVICII.md`
4. `md-files/C64VICPHASE_A.md`
5. `md-files/C64VICPHASE_B.md`
6. `md-files/C64VICPHASE_C.md`
7. `md-files/C64VICPHASE_J.md`

## Background

The current VIC-II implementation already has internal Bad Line buffers:

- `video_matrix[40]`
- `color_line[40]`

These are populated during Bad Line c-access cycles 15-54. However, the display pixel
path still generally rereads screen RAM, color RAM, character data, and bitmap data
directly from the bus for each output pixel.

That is useful as a simple renderer, but it is not how the VIC-II display pipeline
behaves. The real VIC-II latches the 40 video matrix/color entries for the current
character row on a Bad Line, then renders from those internal values while the row is
displayed.

## Goal

Use the Bad Line video matrix/color line buffers as the source of display metadata for
the active character row.

The immediate target is:

- standard text mode renders character codes and color nibbles from the latched line
  buffers,
- CPU writes to screen RAM/color RAM after the Bad Line do not affect the currently
  latched character row,
- existing scroll, border, sprite priority, collision, and `DEN` behavior continue to
  work.

After standard text mode is stable, extend the same line-buffer model to the other
Phase C modes.

## Scope

In scope:

- Live VIC-II rendering.
- The background/display pixel path in `src/machine/vicii.c`.
- Use of `video_matrix[40]` and `color_line[40]` for display metadata.
- Tests proving latch behavior for standard text mode.
- Tests proving existing Phase C modes still render correctly after the refactor.
- Performance cleanup that naturally falls out of reducing per-pixel RAM/color reads.

Out of scope:

- Replacing the whole renderer with a cycle-perfect pixel pipeline.
- Exact idle-state g-access data display from `$3FFF` / `$39FF`, unless needed as a
  small fallback for rows with no valid latched data.
- Light pen.
- Last-byte-on-bus behavior.
- AEC pin modeling.
- Sprite Y coordinate changes.
- Broad frontend/display presentation changes.

## Required Behavior

### Bad Line Latching

On Bad Lines:

- cycles 15-54 continue to fetch 40 video matrix bytes into `video_matrix[40]`,
- the corresponding 40 color RAM nibbles continue to be stored in `color_line[40]`,
- the latched data becomes the display metadata for the active character row.

### Standard Text Mode

For standard text mode:

- character code should come from `video_matrix[col]`,
- foreground color should come from `color_line[col]`,
- glyph data should still come from the current character generator base and current
  row counter/glyph row,
- background pixels should use `$D021`,
- sprite-background collision foreground classification should remain based on glyph
  foreground bits.

### Other Graphics Modes

After the standard text path is working, the same latching rule should apply to the
other display modes:

- multicolor text:
  - character code from `video_matrix[col]`,
  - color nibble from `color_line[col]`.
- ECM text:
  - character code from `video_matrix[col]`,
  - foreground color from `color_line[col]`.
- standard bitmap:
  - video matrix byte from `video_matrix[col]`,
  - bitmap data still fetched from bitmap memory.
- multicolor bitmap:
  - video matrix byte from `video_matrix[col]`,
  - color RAM nibble from `color_line[col]`,
  - bitmap data still fetched from bitmap memory.
- invalid modes:
  - visible display layer remains black as currently specified,
  - timing/memory fetch behavior should not regress.

### Mid-Row CPU Writes

CPU writes to screen RAM or color RAM after the Bad Line fetch for a character row must
not affect the visible pixels of that already-latched row.

Those writes may affect a later character row after a later Bad Line latches the changed
values.

### DEN Interaction

Phase J behavior must remain intact:

- `DEN=0` visually blanks the display area to `$D021`,
- sprites remain visible according to priority,
- sprite-background collision can still use the underlying foreground classification.

If `DEN=0` prevents Bad Lines, the renderer must have a clearly defined behavior for
the line-buffer validity state. Do not invent broad idle-state emulation unless it is
explicitly needed for this phase.

## Suggested Implementation Plan

### Step 1 — Make Latch Validity Explicit

Add minimal state to distinguish valid latched display metadata from reset/unlatched
data. Possible shape:

```c
bool video_matrix_valid;
```

or a small per-row/line state if the implementation needs more precision.

Keep the state private to `vicii`.

### Step 2 — Centralize Display Metadata Lookup

Introduce a helper for fetching display metadata for a column:

```c
typedef struct vicii_display_cell {
    uint8_t vm_byte;
    uint8_t color_nib;
    bool    valid;
} vicii_display_cell;
```

The helper should prefer latched buffers when valid. Any fallback to direct RAM reads
should be explicit and limited to compatibility/debug cases, not the normal live Bad
Line path.

### Step 3 — Convert Standard Text Mode First

Change standard text rendering to use the display metadata helper:

- `code = cell.vm_byte`
- `fg = cell.color_nib`

Keep glyph fetch behavior unchanged at first.

Add tests before converting the other modes.

### Step 4 — Add Mid-Row Write Regression Coverage

Create a test program or direct stepping scenario where:

1. A Bad Line latches character/color data for row 0.
2. The CPU writes a different character/color to the same screen cell after the c-access
   window.
3. The current row still renders the originally latched character/color.
4. A later row/frame can reflect the updated memory after a new latch.

Prefer live-frame testing if possible. If a direct stepping test is simpler, assert the
contents of `video_matrix`/`color_line` and rendered pixels around the affected row.

### Step 5 — Extend Phase C Modes

Once standard text is stable, convert:

- multicolor text,
- ECM text,
- standard bitmap color selection,
- multicolor bitmap color selection,
- invalid modes as needed to preserve current behavior.

Run all existing VIC-II tests after each mode conversion.

### Step 6 — Keep Snapshot Fallback Honest

The old whole-frame snapshot renderer cannot perfectly reconstruct historical line
buffer state for an already completed frame unless it simulates the frame timeline.

Acceptable options:

- keep snapshot fallback as a compatibility/debug path that does direct reads, but
  document that it is not latch-accurate, or
- make snapshot fallback simulate Bad Line latching across the frame, if that remains
  small and testable.

Do not let snapshot fallback mutate live IRQ/collision state while rendering.

## Acceptance Criteria

- Existing VIC-II tests pass.
- Standard text mode renders from `video_matrix[40]` and `color_line[40]` for active
  rows after Bad Line latching.
- A CPU write to screen RAM after a row's Bad Line does not alter the already-latched
  row's visible character code.
- A CPU write to color RAM after a row's Bad Line does not alter the already-latched
  row's visible foreground color.
- A later Bad Line can latch and display the updated screen/color memory.
- XSCROLL/YSCROLL/RSEL/CSEL behavior remains consistent with the current tests.
- `DEN=0` visual blanking from Phase J still works.
- Sprite priority and sprite-background collision still use correct foreground
  classification.
- Multicolor text, ECM text, standard bitmap, multicolor bitmap, and invalid modes keep
  their existing visible behavior after they are converted.
- Performance does not regress measurably in the frame generation path.

## Suggested Tests

Add focused tests to `tests/machine/test_c64_vicii.c`.

Recommended tests:

1. `test_text_row_uses_badline_latched_character`
   - Arrange a visible standard text glyph in cell 0.
   - Step through the Bad Line c-access window so the cell is latched.
   - Change screen RAM cell 0 before the row finishes rendering.
   - Verify the current row still shows the original glyph.

2. `test_text_row_uses_badline_latched_color`
   - Same structure as above, but change color RAM after latching.
   - Verify the current row still uses the original color nibble.

3. `test_later_badline_latches_updated_text`
   - Change screen/color memory after one row is latched.
   - Advance to a later Bad Line where the changed cell should be latched.
   - Verify the later row/frame reflects the update.

4. `test_line_buffer_standard_text_matches_existing_rendering`
   - With no mid-row writes, verify standard text output matches the current expected
     foreground/background pixels.

5. Phase C regression set
   - Re-run or extend the existing ECM, bitmap, multicolor text, multicolor bitmap, and
     invalid-mode tests after converting each mode to line-buffer metadata.

## Performance Opportunities

This phase should reduce repeated work in the hot pixel path:

- avoid rereading screen RAM/color RAM per pixel,
- cache `vic_bank`, `screen_base`, `char_base`, and `bitmap_base` per line or per relevant
  register change,
- avoid recomputing mode and scroll fields for every pixel when they have not changed
  during the same output span,
- keep per-pixel logic simple: decode already-latched metadata plus glyph/bitmap bits.

Do not turn these into speculative abstractions. Prefer small helpers that serve the
line-buffer rendering path directly.

## Risks And Bug Zones

- Mid-frame register writes can change mode, scroll, colors, and memory pointers. Be
  careful not to over-cache values that are supposed to take effect immediately.
- Color RAM is latched for text and multicolor bitmap metadata, but bitmap data itself
  is still fetched from bitmap memory.
- `DEN=0` suppresses Bad Lines, so validity/fallback behavior must be explicit.
- Snapshot fallback may not have enough historical information to be cycle-accurate.
- Collision code depends on foreground classification; do not reduce the display pixel
  to only a color too early.
- Existing sprite Y/top-border behavior is project-tested; leave it alone.

## Notes

This is a worthwhile long-term VIC-II accuracy phase, but it does not need to block
Phase J or other small bug fixes.

The most valuable first slice is standard text mode plus mid-row write tests. That
creates a clear correctness win and establishes the pattern for the remaining modes.
