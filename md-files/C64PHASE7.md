# C64PHASE7.md

# Phase 7 - Character Display Bring-Up

## Purpose

Replace the Phase 6 synthetic inner-screen pattern with the first real C64 character display path.

This phase introduces:

```text
screen RAM
    + character ROM
    + color RAM
        -> visible PETSCII character display
```

The goal is not a complete BASIC screen.

The goal is to prove that VIC-II can fetch character data from machine memory and render real characters into the existing frame pipeline.

---

# Source Documents

Read:

```text
AGENTS.md
STATUS.md
HIGHLEVEL.md
```

Phase 6 is assumed complete.

---

# Goal

Render characters from screen RAM using character ROM glyphs.

At completion the emulator should be capable of displaying a machine-generated text screen composed of real C64 character glyphs.

The display should use:

```text
screen RAM
character ROM
color RAM
```

through the normal machine and VIC-II paths.

---

# Non-Goals

Do not implement:

```text
sprites
CIA
SID
keyboard matrix
raster IRQs
badlines
cycle-perfect VIC-II
scrolling
multicolor text
bitmap mode
BASIC startup completion
```

---

# Deliverable 1 - Color RAM

Add Color RAM ownership to the machine.

Requirements:

```text
1000 bytes
nibble-based storage
deterministic reset state
```

Suggested size:

```c
uint8_t color_ram[1024];
```

Only the lower four bits are currently significant.

---

# Deliverable 2 - Screen RAM Access

Provide a stable VIC-II path for reading screen memory.

Initial assumptions are acceptable:

```text
screen RAM base = $0400
40 columns
25 rows
```

Do not implement dynamic VIC bank selection yet unless already easy.

---

# Deliverable 3 - Character ROM Access

Allow VIC-II rendering code to read character ROM data.

Requirements:

```text
8x8 glyphs
256 character codes
character ROM remains machine-owned
```

Do not duplicate ROM contents into VIC-II storage.

---

# Deliverable 4 - Character Renderer

Implement a text renderer.

For each visible cell:

```text
screen RAM -> character code
character ROM -> glyph
color RAM -> foreground color
background -> D021
```

Render into the existing frame buffer.

Use existing Phase 6 geometry.

```text
320 x 200 active area
384 x 272 output frame
```

---

# Deliverable 5 - Default Text Pattern

Populate screen RAM during reset with a deterministic test screen.

Example:

```text
C64M PHASE 7
CHARACTER DISPLAY
ABCDEFGHIJKLMNOPQRSTUVWXYZ
0123456789
```

The exact content is not important.

The display must visibly prove that character rendering works.

---

# Deliverable 6 - Rendering Rules

Phase 7 text mode only.

Per pixel:

```text
glyph bit set:
    foreground color

glyph bit clear:
    background color
```

No multicolor mode.

No blinking.

No reverse-video handling unless trivial.

---

# Deliverable 7 - Runtime Integration

Keep the existing runtime publication path.

Do not change:

```text
runtime
    -> frame snapshot
        -> copied handoff
            -> frontend
```

Frontend remains unchanged.

---

# Deliverable 8 - Tests

Add machine tests.

Test:

```text
character ROM fetch
screen RAM fetch
color RAM fetch
glyph decode
frame generation
```

Verify:

```text
non-empty frame
stable dimensions
deterministic output
```

Add runtime tests where practical.

---

# Acceptance Criteria

Phase 7 is complete when:

- Screen RAM participates in rendering.
- Character ROM glyphs are rendered.
- Color RAM affects character color.
- Existing frame pipeline remains unchanged.
- Frontend still consumes copied frames only.
- Visible text appears in the emulator window.
- Existing tests continue to pass.
- New rendering tests pass.

---

# End State

```text
runtime
    -> machine
        -> VIC-II
            -> screen RAM
            -> character ROM
            -> color RAM
                -> character pixels
                    -> frame snapshot
                        -> copied handoff
                            -> frontend
```

The emulator does not yet need to reach the BASIC screen.

The milestone is achieved when real C64 character glyphs are visible on-screen.
