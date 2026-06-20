# C64HLPHASE_1.md

# c64m Help System - Phase 1 Implementation Guide

## Purpose

Implement the first working version of the in-emulator help system for c64m.

The help system must:

- Open when the user presses OPTION+H.
- Pause the emulator while help is open.
- Close when the user presses ESC or OPTION+H again.
- Resume the emulator only if the help system caused the pause.
- Render through the existing SDL2/Nuklear frontend.
- Use `./manual/manual.md` as the single manual source.
- Generate compiled-in C help data at build time.
- Keep the final executable standalone.

Phase 1 is intentionally limited. Do not implement custom fonts, rich Markdown, runtime Markdown parsing, or visual polish beyond simple compile-time colors.

## Required Reading

Before implementation, read these project documents in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`

The important architectural constraints are:

- This feature belongs in `frontend/`.
- The UI must command pause/resume through `runtime_client`.
- The frontend must not access the live machine directly.
- Do not add SDL, Nuklear, or frontend dependencies to `machine/`, `runtime/`, or `util/`.
- Runtime owns the live machine.
- Frontend owns presentation.

## Non-goals

Do not implement any of these in Phase 1:

- Runtime Markdown parsing.
- Live reload of the manual.
- Custom C64 TTF font.
- Full CommonMark support.
- Tables.
- Images.
- Links.
- Nested lists.
- HTML passthrough.
- Per-section scroll memory.
- Horizontal section-button scrolling.
- Rich syntax highlighting.
- Runtime-configurable help colors.
- Any changes to `machine/`.

## Source Manual

The help source file is:

```text
./manual/manual.md
```

The first `#` heading is the fixed help heading.

Each `##` heading starts a help section.

The bottom row of the help view is built from the `##` section titles.

The content area displays the currently selected section.

Example:

```md
# c64m Help

## Getting Started

This is the first section.

### Loading a PRG

Use the Load button from the Machine tab.

## Keyboard

Keyboard help goes here.
```

## Supported Markdown Subset

Phase 1 must support only this subset:

```text
# heading
## section
### subsection heading
paragraph text
blank lines
- bullet
* bullet
1. numbered item
3) numbered item
``` fenced code blocks ```
```

Inline backtick code spans may be treated as normal paragraph text in Phase 1 unless they are easy to preserve. Full inline span parsing is Phase 2.

Unsupported Markdown must either:

- be treated as normal text, or
- cause the generator to fail with a clear error.

Prefer clear failure for constructs that would render misleadingly.

## Generated Data Model

Add a build-time generator:

```text
tools/gen_help.py
```

Input:

```text
manual/manual.md
```

Output:

```text
src/frontend/generated/help_content.inc
```

The generated file should contain static C data only. It should not contain behavior.

Recommended generated model:

```c
typedef enum help_span_kind {
    HELP_SPAN_TEXT,
    HELP_SPAN_BLANK,
    HELP_SPAN_H3,
    HELP_SPAN_BULLET,
    HELP_SPAN_NUMBER,
    HELP_SPAN_CODE_BLOCK
} help_span_kind;

typedef struct help_span {
    help_span_kind kind;
    const char *text;
} help_span;

typedef struct help_section {
    const char *title;
    const help_span *spans;
    int span_count;
} help_section;

static const char *help_heading = "...";
static const help_section help_sections[] = { ... };
static const int help_section_count = ...;
```

The exact names may be adjusted to fit project style, but keep the model small and explicit.

## Generator Requirements

`tools/gen_help.py` must:

1. Read `manual/manual.md`.
2. Require exactly one top-level `#` heading before the first `##`.
3. Require at least one `##` section.
4. Split sections on `##`.
5. Preserve section order.
6. Convert `###` lines to `HELP_SPAN_H3`.
7. Convert bullet lines beginning with `- ` or `* ` to `HELP_SPAN_BULLET`.
8. Convert ordered-list lines beginning with decimal digits followed by `. ` or `) ` to `HELP_SPAN_NUMBER`.
9. Convert fenced code blocks to `HELP_SPAN_CODE_BLOCK`.
10. Convert normal text to `HELP_SPAN_TEXT`.
11. Escape C string literals correctly.
12. Emit deterministic output.
13. Fail with a clear message if the input is structurally invalid.

Keep the parser deliberately simple. Do not import a Markdown library for Phase 1.

## Build Integration

Add a CMake custom command that regenerates:

```text
src/frontend/generated/help_content.inc
```

whenever either of these change:

```text
manual/manual.md
tools/gen_help.py
```

Use Python from CMake if the project already locates Python. If not, add the smallest practical `find_package(Python3 COMPONENTS Interpreter REQUIRED)` change at the appropriate root or tool level.

The generated include should be treated as a build artifact. If the project convention prefers generated files in the build directory, place it there and include it through the build include path. If the project already keeps generated frontend files in source, follow the existing convention.

Do not introduce a runtime dependency on Python.

## Frontend Files

Add:

```text
src/frontend/help_view.h
src/frontend/help_view.c
```

Optional Phase 1 helper:

```text
src/frontend/help_theme.h
```

Do not place this feature in `machine/`, `runtime/`, `platform/`, or `tools/` except for the offline generator under `tools/`.

## Frontend State

Add a frontend-owned help state.

Recommended shape:

```c
typedef struct frontend_help_state {
    bool open;
    bool paused_by_help;
    int section_index;
} frontend_help_state;
```

If the existing frontend state naming differs, adapt to it.

Do not store live machine pointers here.

## Runtime Pause/Resume Semantics

When OPTION+H opens help:

1. Determine whether the emulator is currently running using existing frontend/runtime snapshot or state.
2. If running, send the existing runtime pause command through `runtime_client`.
3. Set `paused_by_help = true`.
4. Open the help overlay.

If the emulator was already paused:

1. Do not send a pause command unless the existing code requires it harmlessly.
2. Set `paused_by_help = false`.
3. Open the help overlay.

When ESC or OPTION+H closes help:

1. Close the help overlay.
2. If `paused_by_help == true`, send the existing runtime run/resume command through `runtime_client`.
3. Clear `paused_by_help`.

Do not resume if the user was already paused before opening help.

If the runtime has stopped, reset, or changed state while help was open, prefer safe behavior: close help but do not force-run unless the existing runtime state clearly supports resuming.

## Input Handling

Add input handling for:

```text
OPTION+H
ESC
```

Use the project's existing SDL event and keyboard modifier conventions.

Treat left and right Option/Alt consistently with the rest of the emulator. If the project already maps Mac Option to SDL Alt, follow the existing convention.

Behavior:

```text
OPTION+H while help closed -> open help
OPTION+H while help open   -> close help
ESC while help open        -> close help
ESC while help closed      -> existing behavior
```

When help is open, help UI should consume relevant keyboard/mouse interaction so underlying emulator input does not also act on it.

## Nuklear Layout

Render a full-window or near-full-window overlay above the emulator view.

The layout is:

```text
+---------------------------------------------+
| HEADING TEXT                                |
+---------------------------------------------+
| selected section content                    |
| scrollable                                  |
|                                             |
+---------------------------------------------+
| [section] [section] [section]               |
+---------------------------------------------+
```

The heading row must stay visible.

The bottom section row must stay visible.

Only the middle content area scrolls.

Recommended Nuklear structure:

```c
if(nk_begin(ctx, "Help", bounds, flags)) {
    nk_layout_row_dynamic(ctx, heading_h, 1);
    render_help_heading(ctx, help_heading);

    nk_layout_row_dynamic(ctx, content_h, 1);
    if(nk_group_begin(ctx, "HelpContent", NK_WINDOW_BORDER)) {
        render_help_section(ctx, &help_sections[state->section_index]);
        nk_group_end(ctx);
    }

    nk_layout_row_dynamic(ctx, footer_h, 1);
    render_help_section_buttons(ctx, state);
}
nk_end(ctx);
```

Use project style and existing helper functions where available.

## Rendering Rules

For each `help_span`:

- `HELP_SPAN_TEXT`: render as normal wrapped text.
- `HELP_SPAN_BLANK`: add vertical spacing.
- `HELP_SPAN_H3`: render with subsection color.
- `HELP_SPAN_BULLET`: render bullet marker plus text.
- `HELP_SPAN_NUMBER`: render number marker plus text.
- `HELP_SPAN_CODE_BLOCK`: render in code color, preserving line breaks if practical.

Nuklear text wrapping may be limited. Use the simplest existing Nuklear wrapped-label mechanism available in the project. If no wrapper exists, implement a small frontend-only helper that splits text by approximate available width.

Do not overbuild wrapping in Phase 1. The help must be readable and usable; typographic perfection is Phase 2.

## Compile-time Theme

Add a simple compile-time theme for help rendering.

Recommended colors may be based on the C64 palette, but keep this minimal:

```c
#define HELP_COLOR_BG         ...
#define HELP_COLOR_HEADING    ...
#define HELP_COLOR_BODY       ...
#define HELP_COLOR_H3         ...
#define HELP_COLOR_BULLET     ...
#define HELP_COLOR_NUMBER     ...
#define HELP_COLOR_CODE       ...
```

Use these only in frontend help rendering.

Do not add runtime theme configuration.

## Section Buttons

The bottom row contains one Nuklear button per `##` section.

When clicked:

```text
state->section_index = clicked_section;
```

If there are too many section buttons to fit, Phase 1 may allow wrapping to multiple rows or clipping according to the simplest Nuklear layout that remains usable. Better scrolling or overflow behavior is Phase 2.

## Tests and Validation

At minimum, validate manually:

1. Build succeeds from a clean tree.
2. `manual/manual.md` generates help data.
3. Executable does not need `manual/manual.md` at runtime.
4. OPTION+H opens help.
5. Emulator pauses when help opens while running.
6. ESC closes help.
7. OPTION+H closes help.
8. Emulator resumes only if help caused the pause.
9. Emulator remains paused if it was paused before help opened.
10. Heading remains fixed while content scrolls.
11. Section row remains fixed while content scrolls.
12. Section buttons switch content.
13. Existing debugger, load/save, audio, and video smoke behavior still works.

Add automated tests for `tools/gen_help.py` if the project has a lightweight pattern for tool tests. If not, keep the generator small and include clear error paths.

## Acceptance Criteria

Phase 1 is complete when:

- `./manual/manual.md` is the source manual.
- The manual is compiled into the executable through generated C data.
- The application has a help overlay rendered through Nuklear.
- OPTION+H toggles the overlay.
- ESC closes the overlay.
- Opening help pauses the emulator through `runtime_client`.
- Closing help resumes only if help caused the pause.
- The top heading does not scroll.
- The bottom section row does not scroll.
- The middle content view scrolls.
- Section buttons change the displayed section.
- The implementation stays within the project dependency rules.
- No live machine pointers cross into frontend help code.
- Existing tests continue to pass.
- `STATUS.md` is updated with a concise note describing the completed help-system phase.

## STATUS.md Update

After implementation and validation, update `STATUS.md` with a concise entry such as:

```text
- Help UI Phase 1: build-time manual/manual.md to compiled help data, Nuklear help overlay, OPTION+H/ESC toggle, runtime-client pause/resume, fixed heading/footer with scrollable section content.
```

Do not claim Phase 2 features.
