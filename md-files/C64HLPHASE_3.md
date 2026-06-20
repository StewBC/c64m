# C64HLPHASE_3.md

# c64m Help System - Phase 3 Implementation Guide

## Purpose

Implement the visual C64-style polish for the in-emulator help system:

1. C64-inspired help colors.
2. Optional embedded C64-style TTF font.

Phase 3 must preserve the accepted Phase 1 rendering and Phase 2 navigation behavior. Do not rewrite wrapping, Markdown generation, table rendering, scroll behavior, or keyboard navigation as part of this phase.

## Required Reading

Before implementation, read these documents in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64HLPHASE_1.md`
5. `C64HLPHASE_2.md`
6. The current help implementation.

The important architectural constraints remain:

- Help rendering belongs in `frontend/`.
- Runtime control remains through `runtime_client`.
- The frontend must not access the live machine directly.
- Do not add SDL/Nuklear/frontend dependencies to `machine/`, `runtime/`, or `util/`.
- Do not parse Markdown at runtime.
- Do not change emulator behavior.

## Current Assumptions

Previous phases have already provided:

- `./manual/manual.md` as the manual source.
- Build-time compiled help data.
- Nuklear help overlay.
- OPTION+H and ESC help behavior.
- Correct pause/resume behavior.
- Fixed heading row.
- Scrollable middle content.
- Fixed bottom section row.
- Section buttons.
- Tables.
- Accepted line wrapping.
- Tunable section cutoff.
- Per-section scroll memory.
- Keyboard navigation.

Phase 3 is a visual pass only.

## Non-goals

Do not implement:

- New Markdown features.
- Runtime Markdown parsing.
- Live manual reload.
- Table layout changes.
- Code-block layout changes.
- Line wrapping changes.
- Per-section scroll changes.
- Keyboard navigation changes.
- Section cutoff changes.
- Runtime-configurable theme UI.
- Any change to `machine/`.
- Any change to emulator timing, audio, video, input, disk, CPU, CIA, VIC-II, or SID behavior.

## Feature 1: C64-inspired Help Theme

### Goal

Give the help overlay a Commodore 64 inspired look using compile-time selected colors.

The theme should be visually C64-like but not constrained to a real 40-column C64 text mode.

Expected style:

- Dark or medium blue help background.
- Light blue or white body text.
- Clear heading color.
- Distinct subsection heading color.
- Distinct bullet/number marker color.
- Readable code/table colors.
- Active section button clearly visible.
- Inactive section buttons still readable.

### Compile-time Only

Colors must be compile-time constants or static frontend theme data.

Do not add runtime color preferences.

Do not add an INI/config setting for help colors in this phase.

### Suggested Theme Structure

Use existing project style, but conceptually define:

```c
typedef struct frontend_help_theme {
    struct nk_color bg;
    struct nk_color panel;
    struct nk_color border;
    struct nk_color heading;
    struct nk_color body;
    struct nk_color h3;
    struct nk_color bullet_marker;
    struct nk_color number_marker;
    struct nk_color inline_code;
    struct nk_color code_block_text;
    struct nk_color code_block_bg;
    struct nk_color table_text;
    struct nk_color table_border;
    struct nk_color section_button;
    struct nk_color section_button_hover;
    struct nk_color section_button_active;
    struct nk_color section_button_text;
} frontend_help_theme;
```

If the current implementation already has a theme mechanism, extend that instead of introducing a competing structure.

### Suggested C64 Palette

Use a known C64-style palette already present in the project if one exists.

If not, define local frontend help colors. A reasonable starting point:

```c
#define C64_HELP_BLACK       nk_rgb(0x00, 0x00, 0x00)
#define C64_HELP_WHITE       nk_rgb(0xff, 0xff, 0xff)
#define C64_HELP_RED         nk_rgb(0x88, 0x00, 0x00)
#define C64_HELP_CYAN        nk_rgb(0xaa, 0xff, 0xee)
#define C64_HELP_PURPLE      nk_rgb(0xcc, 0x44, 0xcc)
#define C64_HELP_GREEN       nk_rgb(0x00, 0xcc, 0x55)
#define C64_HELP_BLUE        nk_rgb(0x00, 0x00, 0xaa)
#define C64_HELP_YELLOW      nk_rgb(0xee, 0xee, 0x77)
#define C64_HELP_ORANGE      nk_rgb(0xdd, 0x88, 0x55)
#define C64_HELP_BROWN       nk_rgb(0x66, 0x44, 0x00)
#define C64_HELP_LIGHT_RED   nk_rgb(0xff, 0x77, 0x77)
#define C64_HELP_DARK_GRAY   nk_rgb(0x33, 0x33, 0x33)
#define C64_HELP_GRAY        nk_rgb(0x77, 0x77, 0x77)
#define C64_HELP_LIGHT_GREEN nk_rgb(0xaa, 0xff, 0x66)
#define C64_HELP_LIGHT_BLUE  nk_rgb(0x00, 0x88, 0xff)
#define C64_HELP_LIGHT_GRAY  nk_rgb(0xbb, 0xbb, 0xbb)
```

These are acceptable approximations. Exact palette fidelity is not required unless the project already has a canonical C64 palette.

### Style Push/Pop Discipline

Do not permanently mutate global Nuklear style.

When rendering help:

1. Push temporary style colors as needed.
2. Render the help overlay.
3. Pop all pushed style changes.

Every push must have a matching pop.

If the current help code already centralizes style changes, adapt it carefully.

### Preserve Existing Rendering

Do not change layout, wrapping, tables, or spacing to achieve the theme.

Color changes are allowed.

Spacing changes are not part of this phase unless required because a new font changes metrics. If a font causes layout problems, fix only the font-specific issue.

### Acceptance Criteria for Theme

The C64 theme is complete when:

- Help background/panel has C64-style coloring.
- Heading, body, subsection, bullets/numbers, code, tables, and section buttons remain readable.
- Active section button is visually distinct.
- Colors do not leak into other Nuklear UI after help closes.
- Existing Phase 1 and Phase 2 behavior still works.
- Existing accepted wrapping and formatting remain intact.

## Feature 2: Optional Embedded C64-style TTF Font

### Goal

Optionally render the help overlay with an embedded C64-style TTF font.

This is optional. If font integration becomes invasive or unstable, skip it and complete Phase 3 with the C64 color theme only.

### Requirements

If implemented, the font must:

- Be embedded into the executable.
- Have a clear permissive license.
- Be initialized through the existing frontend/Nuklear font path.
- Be used only by the help overlay unless there is a deliberate project decision to use it elsewhere.
- Not require runtime file access.
- Not touch `machine/` or `runtime/`.

### Font Licensing

Before embedding any font, verify and record:

```text
font name
font source
font license
license file or URL
redistribution permission
```

Acceptable license families include:

```text
OFL
MIT
BSD-style
CC0
public domain dedication
```

Do not embed a font with unclear rights.

If no suitable font is selected, skip font embedding.

### Generated Font Include

If implementing font embedding, add a small binary-to-C generator if one does not already exist.

Possible path:

```text
tools/bin2c.py
```

Input:

```text
assets or manual/font source path
```

Output:

```text
src/frontend/generated/c64_help_font.inc
```

Generated shape:

```c
static const unsigned char c64_help_font_ttf[] = {
    ...
};

static const unsigned int c64_help_font_ttf_len = ...;
```

Do not manually edit the generated font include.

Do not expose or distribute font files without following the font license and repository policy.

### Font Initialization

Find the existing Nuklear font atlas setup.

Integrate the help font there.

Do not create/load the font in the render loop.

Conceptual flow:

```c
nk_font_atlas_add_from_memory(
    atlas,
    c64_help_font_ttf,
    c64_help_font_ttf_len,
    font_size,
    &config
);
```

Use the actual backend and project patterns.

Store the resulting font pointer/handle in frontend UI state.

### Font Use

When rendering help:

1. Save or rely on existing current font state.
2. Push/select the help font.
3. Render the help overlay.
4. Restore the previous font.

If the Nuklear backend does not support clean per-panel font switching, either:

- use the font globally only if the project owner explicitly approves; or
- skip the font for this phase.

Prefer skipping over destabilizing the UI.

### Font Size

The goal is C64-like, but not 40-column-limited.

Use a small but readable size.

Guidelines:

- Start around the current Nuklear default font size.
- Adjust only enough to preserve readability.
- Do not shrink text so much that the help becomes hard to read.
- Verify tables remain usable.

### Font Fallback

If font loading fails or is disabled at compile time:

- Help should render with the existing default font.
- The app should not crash.
- The build should still work if the font is intentionally not configured.

A compile-time guard is acceptable:

```c
#ifdef C64M_HELP_FONT_ENABLED
...
#endif
```

Only add such a guard if it simplifies the implementation.

### Acceptance Criteria for Font

The embedded font is complete only if:

- Font license is documented.
- Font data is compiled into the executable.
- Help uses the font without runtime file access.
- Other UI is not accidentally damaged.
- Help remains readable.
- Existing help behavior still works.
- Clean builds work.
- The executable remains standalone.

If any of these cannot be met cleanly, defer font embedding and complete only the color theme.

## Suggested Implementation Order

1. Inspect the current accepted help renderer.
2. Identify all help color usage.
3. Add or clean up a frontend-only help theme structure.
4. Apply C64-inspired colors with strict Nuklear style push/pop discipline.
5. Validate that colors do not leak to other UI.
6. Stop here if the theme is sufficient.
7. If doing the font, select a licensed font and record license information.
8. Add or reuse a binary-to-C generator.
9. Generate the embedded font include.
10. Integrate font with existing Nuklear font atlas initialization.
11. Use the font only while rendering help.
12. Validate layout, wrapping, tables, scroll, and keyboard navigation.
13. Update `STATUS.md`.

## Manual Validation Checklist

Run this checklist before claiming Phase 3 complete:

```text
[ ] Build succeeds from a clean tree.
[ ] Help opens with OPTION+H.
[ ] Help closes with OPTION+H.
[ ] Help closes with ESC.
[ ] Pause/resume behavior is unchanged.
[ ] Mouse scrolling still works.
[ ] Per-section scroll memory still works.
[ ] PageUp/PageDown/Home/End still work.
[ ] Left/Right section navigation still works.
[ ] Section buttons still work.
[ ] Section cutoff behavior still works.
[ ] Tables remain readable.
[ ] Long-line wrapping remains accepted.
[ ] Code blocks remain readable.
[ ] Help colors look C64-inspired.
[ ] Active section button is visually distinct.
[ ] Help colors do not leak into other UI.
[ ] If font was implemented, the font is embedded and requires no runtime file.
[ ] If font was implemented, font license is recorded.
[ ] If font was implemented, the rest of the UI is not damaged.
[ ] Existing smoke tests still pass.
```

## STATUS.md Update

After implementation and validation, update `STATUS.md` with a concise factual entry.

If only colors are implemented:

```text
- Help UI Phase 3: added compile-time C64-inspired help theme colors while preserving existing help rendering and navigation behavior.
```

If colors and font are implemented:

```text
- Help UI Phase 3: added compile-time C64-inspired help theme colors and an embedded licensed C64-style help font while preserving existing help rendering and navigation behavior.
```

Mention the font name and license in the repository if the font is embedded.

Do not claim font support if it was skipped.
