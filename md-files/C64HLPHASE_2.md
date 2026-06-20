# C64HLPHASE_2.md

# c64m Help System - Phase 2 Implementation Guide

## Purpose

Polish the in-emulator help system after Phase 1 is complete and working.

Phase 2 improves rendering quality, C64 visual identity, generator validation, and usability. It must not change emulator architecture or introduce runtime Markdown parsing unless a later plan explicitly approves it.

Phase 2 depends on Phase 1. Do not start Phase 2 until:

- `./manual/manual.md` is compiled into static help data.
- The Nuklear help overlay opens and closes correctly.
- Runtime pause/resume behavior is correct.
- The fixed heading, scrollable content area, and fixed section row are working.

## Required Reading

Before implementation, read:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64HLPHASE_1.md`

Preserve all Phase 1 architecture constraints:

- Help presentation stays in `frontend/`.
- Runtime control still goes through `runtime_client`.
- No live machine access from frontend help code.
- No SDL/Nuklear dependencies added to `machine/`, `runtime/`, or `util/`.
- No runtime dependency on Python or Markdown libraries.

## Phase 2 Goals

Implement some or all of the following polish items, in this priority order:

1. Better text wrapping and spacing.
2. Inline code-span rendering.
3. Better code-block rendering.
4. Per-section scroll position memory.
5. Improved section-button overflow handling.
6. Stronger generator validation and optional tests.
7. C64 palette theme polish.
8. Optional embedded C64-style TTF font.

The custom font is the last item, not the first.

## Non-goals

Do not implement:

- Runtime Markdown parsing.
- Live reload.
- Full CommonMark.
- HTML rendering.
- Image rendering.
- PDF generation inside the emulator.
- Runtime-configurable theme editing.
- Any changes to emulation fidelity.
- Any changes to `machine/`.

## Improved Text Model

If Phase 1 used only one span kind per line, Phase 2 may refine the generated model to support inline spans.

Recommended model:

```c
typedef enum help_text_style {
    HELP_TEXT_BODY,
    HELP_TEXT_INLINE_CODE
} help_text_style;

typedef struct help_text_run {
    help_text_style style;
    const char *text;
} help_text_run;

typedef enum help_block_kind {
    HELP_BLOCK_PARAGRAPH,
    HELP_BLOCK_BLANK,
    HELP_BLOCK_H3,
    HELP_BLOCK_BULLET,
    HELP_BLOCK_NUMBER,
    HELP_BLOCK_CODE_BLOCK
} help_block_kind;

typedef struct help_block {
    help_block_kind kind;
    const char *marker;
    const help_text_run *runs;
    int run_count;
} help_block;
```

This is optional. If a smaller extension to the Phase 1 model is sufficient, keep the smaller model.

Do not introduce complex dynamic allocation unless existing frontend conventions already support it cleanly.

## Inline Code Spans

Support:

```md
Use `LOAD "*",8,1` to load a program.
```

Rendering:

- Body text uses normal help body color.
- Inline code uses code color.
- Inline code may use a subtle background if practical.
- Inline code should not break layout badly if wrapping occurs.

The generator should parse balanced single-backtick spans in normal text, bullets, numbered items, and subsection headings if reasonable.

If a line has an unmatched backtick, fail generation with a clear error.

Do not support nested or multi-line inline code spans.

## Code Blocks

Improve fenced code block rendering.

Supported source:

````md
```text
LOAD "$",8
LIST
```
````

Rendering goals:

- Preserve line breaks.
- Use code color.
- Use code-block background color if practical.
- Add vertical padding before and after.
- Avoid scrolling the whole overlay; only the content group scrolls.

Do not implement horizontal scrolling unless easy. Long lines may wrap or be clipped according to existing Nuklear constraints.

## Wrapping and Layout

Improve wrapping so manual text remains readable across window sizes.

Preferred approach:

- Keep wrapping helper local to `frontend/help_view.c` unless broadly useful.
- Use Nuklear text width measurement if available in the existing backend.
- Fall back to approximate character width only if necessary.
- Avoid heap allocation in the render loop if possible.
- Keep behavior deterministic.

The content area should adapt to window resizing.

Heading and footer heights should remain stable.

## Per-section Scroll Memory

Phase 1 may reset or share scroll position between sections. Phase 2 may preserve a scroll position per section.

Recommended state:

```c
typedef struct frontend_help_state {
    bool open;
    bool paused_by_help;
    int section_index;
    float *section_scroll_y;
    int section_scroll_count;
} frontend_help_state;
```

Adapt to Nuklear's actual scroll API. If Nuklear exposes scroll as integers, use that.

Behavior:

- Switching away from a section stores its scroll position.
- Switching back restores that section's scroll position.
- Opening help may start at the first section or preserve the last selected section, depending on what is less disruptive to the existing UI.
- Closing help should not corrupt stored scroll state.

If this becomes messy with Nuklear groups, skip it and document it as deferred.

## Section Button Overflow

Improve the bottom section row if many `##` sections exist.

Acceptable approaches:

1. Wrap buttons into multiple rows.
2. Use a horizontally scrollable footer group.
3. Use a compact tab selector plus previous/next buttons.
4. Use a combo/dropdown when there are too many sections.

Preferred Phase 2 approach:

```text
[<] [Section Name] [>]    or    compact wrapping buttons
```

Keep it simple. The help view must remain easy to use with mouse and keyboard.

## Keyboard Navigation

Optional Phase 2 improvement:

```text
PageUp/PageDown    scroll content
Home/End           top/bottom of current section
Left/Right         previous/next section
```

Only implement this if it fits cleanly with existing input handling.

Do not let help keyboard events leak into the emulator while help is open.

## C64 Palette Theme Polish

Refine the help theme to use a C64-inspired palette.

Keep colors compile-time tunable.

Recommended categories:

```c
#define HELP_COLOR_BG
#define HELP_COLOR_PANEL
#define HELP_COLOR_BORDER
#define HELP_COLOR_HEADING
#define HELP_COLOR_BODY
#define HELP_COLOR_H3
#define HELP_COLOR_BULLET_MARKER
#define HELP_COLOR_NUMBER_MARKER
#define HELP_COLOR_INLINE_CODE
#define HELP_COLOR_CODE_BLOCK_TEXT
#define HELP_COLOR_CODE_BLOCK_BG
#define HELP_COLOR_SECTION_BUTTON
#define HELP_COLOR_SECTION_BUTTON_ACTIVE
```

Do not add a runtime settings UI for colors in Phase 2.

Use existing Nuklear style push/pop patterns so the help view does not permanently mutate global UI style after rendering.

## Optional Embedded C64-style Font

This is optional and should be done only after the help view is already solid.

Goal:

- Embed a permissively licensed C64-style TTF or bitmap-compatible font.
- Use it for the help overlay.
- Keep the executable standalone.
- Do not ship a font with unclear licensing.
- Do not place font logic in `machine/` or `runtime/`.

Recommended path:

```text
src/frontend/assets/c64_help_font_ttf.inc
```

Generated from a font binary by a small tool such as:

```text
tools/bin2c.py
```

The generated file should contain:

```c
static const unsigned char c64_help_font_ttf[] = { ... };
static const unsigned int c64_help_font_ttf_len = ...;
```

Integrate with the existing Nuklear font atlas initialization.

Important:

- Verify where the project currently initializes Nuklear fonts.
- Add the help font there, not in the render loop.
- Store a pointer/handle to the font in frontend UI state.
- Push the font only while rendering help, then restore the previous font.
- If the backend does not make font switching clean, defer custom font rather than destabilizing the UI.

## Font Licensing

Before embedding a font, record the font name, source, and license in the repository.

Accept only permissive licenses compatible with the project.

Examples of acceptable license families may include:

```text
OFL
MIT
BSD-style
CC0
public domain dedication
```

Do not embed a font without clear redistribution rights.

If there is any uncertainty, skip the font and keep the default Nuklear font.

## Generator Validation

Improve `tools/gen_help.py` validation.

Recommended checks:

- exactly one top-level `#` heading;
- top-level `#` appears before all `##` sections;
- at least one section;
- no empty section title;
- no duplicate section title unless intentionally allowed;
- fenced code blocks are closed;
- inline backticks are balanced if inline-code parsing is implemented;
- unsupported block constructs fail clearly if they would render incorrectly.

Error messages should include line numbers.

Example:

```text
manual/manual.md:42: unmatched inline code backtick
```

## Generator Tests

If the project has a suitable test pattern, add tests for the generator.

Suggested cases:

1. Minimal valid manual.
2. Two sections.
3. Subsection heading.
4. Bullets.
5. Numbered items with `1.`.
6. Numbered items with `1)`.
7. Fenced code block.
8. Inline code span.
9. Missing top-level heading fails.
10. Unclosed code fence fails.
11. Unmatched inline backtick fails.

If adding test infrastructure would be disproportionate, add a small self-test mode:

```text
python3 tools/gen_help.py --self-test
```

Do not make the normal application depend on test execution.

## Manual Authoring Notes

Add or update a short note near `manual/manual.md` or in a developer doc explaining the supported in-emulator Markdown subset.

The note should say:

- The same file may be used by Pandoc for PDF/manual generation.
- The emulator help renderer supports only a subset.
- Avoid unsupported Markdown if the content must appear correctly in-emulator.
- Use `##` headings for emulator help sections.
- Use `###` for subsection headings inside sections.

## Performance

The help view is not performance-critical, but avoid unnecessary churn.

Guidelines:

- Generated manual data should be static.
- Do not parse Markdown during rendering.
- Avoid per-frame heap allocation.
- Avoid repeatedly measuring unchanged text if that becomes expensive.
- Keep all rendering on the UI thread.

## Validation

At minimum, manually validate:

1. All Phase 1 acceptance criteria still pass.
2. Inline code is colored correctly if implemented.
3. Code blocks are readable.
4. Resizing the window keeps the help usable.
5. Section overflow behavior is acceptable.
6. Theme changes do not leak into other Nuklear UI.
7. If custom font is implemented, the rest of the UI still renders correctly.
8. If custom font is implemented, the executable remains standalone.
9. Existing emulator smoke tests still pass.

## Acceptance Criteria

Phase 2 is complete when the selected Phase 2 goals are implemented and documented.

Minimum acceptable Phase 2 completion:

- Better wrapping/layout is implemented.
- Code blocks are rendered more clearly than Phase 1.
- Compile-time C64 palette theme is cleaned up.
- Generator validation has line-numbered errors.
- Existing Phase 1 behavior remains correct.
- Existing tests continue to pass.
- `STATUS.md` is updated with the specific Phase 2 improvements completed.

Optional completion additions:

- Inline code span coloring.
- Per-section scroll memory.
- Improved section overflow handling.
- Embedded permissively licensed C64-style font.

Do not claim optional items unless implemented and validated.

## STATUS.md Update

After implementation and validation, update `STATUS.md` with a concise entry naming the actual completed polish.

Example:

```text
- Help UI Phase 2: improved help text wrapping, code-block styling, inline code coloring, C64 palette theme, and stricter manual generator validation.
```

If the custom font was not implemented, do not mention it.

If the custom font was implemented, include the font name and license in the repository and mention the embedded font briefly in `STATUS.md`.
