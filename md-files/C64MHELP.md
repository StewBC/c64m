# C64MHELP.md

# c64m Help System Implementation Guide

## Purpose

Implement an in-emulator help system for c64m.

The help system opens when the user presses `OPTION+H`, pauses the emulator while help is visible, and closes when the user presses `ESC` or `OPTION+H` again. The help content is generated from the Markdown manual at:

```text
./manual/manual.md
```

The same Markdown manual should remain usable with Pandoc or another documentation tool to generate an external PDF/manual. The emulator help view should use a build-time generated C representation of a supported Markdown subset and render it through the existing Nuklear frontend.

This guide is intended for an implementation agent working inside the c64m codebase.

## Required project context

Before implementing, read these files in this order:

```text
1. AGENTS.md
2. MASTER.md
3. STATUS.md
```

The help system is a frontend feature. It must preserve the architecture boundaries documented by the project:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
tools    -> util
platform -> util + SDL2
```

The help implementation must not introduce any forbidden dependency direction.

In particular:

```text
- frontend must not call machine directly
- frontend must not read live machine state directly
- runtime must not know about Nuklear, SDL UI, or the help view
- machine must not know about the help system
- platform must not know about C64 help/manual semantics
```

The help overlay belongs in `src/frontend/`.

The Markdown-to-C generator belongs in `tools/` or another existing build/tooling location that is consistent with the project layout.

Generated help data may live under `src/frontend/generated/`.

## User-visible behavior

### Open help

When the user presses `OPTION+H` and the help view is closed:

```text
1. Record whether the emulator was running before help was opened.
2. If the emulator was running, send a pause command through runtime_client.
3. Open the help overlay.
4. Set the selected help section to the first section unless a previous section should be preserved.
5. Focus help input.
```

Do not pause by touching the live machine directly. Pause through the normal runtime command path.

### Close help

When the help view is open and the user presses either:

```text
- ESC
- OPTION+H
```

then:

```text
1. Close the help overlay.
2. If the help system paused the emulator, send a run/resume command through runtime_client.
3. If the emulator was already paused before help opened, leave it paused.
4. Restore normal emulator/debugger input routing.
```

The distinction between "already paused" and "paused by help" is important.

### While help is open

While the help overlay is open:

```text
- help receives keyboard and mouse input first
- the emulator display should not receive C64 keyboard input
- debugger/editor controls behind help should not be interacted with
- runtime remains paused unless the user had already paused it before opening help
```

The overlay may still be rendered over the latest visible emulator frame.

## Visual layout

The help view should be rendered as a large Nuklear panel over the emulator UI.

Target layout:

```text
+---------------------------------------------+
| HEADING TEXT                                |
+---------------------------------------------+
| help contents go here                       |
|                                           ^ |
|                                           | |
|                                           | |
|                                           | |
|                                           | |
|                                           v |
+---------------------------------------------+
| [section] [section] [section]               |
+---------------------------------------------+
```

The top heading row is fixed.

The middle content region scrolls.

The bottom section row is fixed.

Only the middle content region should scroll. The top heading and bottom section buttons must remain visible at all times.

The heading text is taken from the first level-one Markdown heading:

```md
# HEADING TEXT
```

The heading text does not change when sections change.

Each level-two Markdown heading becomes one bottom-row section button:

```md
## SECTION 1 TEXT
```

Clicking a section button changes the content shown in the middle scroll region.

## Markdown source format

The canonical help/manual source file is:

```text
./manual/manual.md
```

The file should be valid enough Markdown for Pandoc to generate the external manual/PDF.

The emulator help system should support a deliberately small subset first.

Supported required subset:

```text
# document heading
## section heading
### sub-heading
paragraph text
blank lines
- unordered bullet
* unordered bullet
1. ordered bullet
3. ordered bullet
3) ordered bullet
inline `code spans`
fenced code blocks using triple backticks
```

Unsupported constructs may be ignored, flattened, or rejected by the generator with a clear build-time error. Prefer build-time errors for constructs that would render misleadingly.

Initial implementation does not need to support:

```text
- tables
- images
- raw HTML
- links with special behavior
- nested lists
- blockquotes
- footnotes
- Markdown extensions
```

Links may initially render as plain text.

## Recommended architecture

Use a build-time preprocessing step.

Do not parse Markdown at runtime for the first implementation.

Recommended pipeline:

```text
./manual/manual.md
    -> tools/gen_help.py
        -> src/frontend/generated/help_content.inc
            -> src/frontend/help_view.c
                -> Nuklear rendering
```

The generated `.inc` file should contain only static C data.

The emulator executable remains standalone because all help text is compiled into the binary.

This avoids:

```text
- runtime file I/O
- runtime Markdown parser dependencies
- live reload complexity
- platform-specific asset lookup
```

It also lets the generator attach semantic span kinds for color treatment.

## New files

Add these files:

```text
manual/manual.md
    Source manual/help Markdown.

tools/gen_help.py
    Build-time Markdown subset parser and C emitter.

src/frontend/help_view.h
    Public frontend help view interface.

src/frontend/help_view.c
    Nuklear rendering and help state behavior.

src/frontend/help_theme.h
    Compile-time color/theme constants.

src/frontend/generated/help_content.inc
    Generated C data. This file may be generated, checked in, or both depending on project convention.
```

If the project prefers generated files not to be committed, add the generated path to the appropriate ignore file.

If the project prefers committed generated sources for simple builds, commit the generated `.inc` and still keep the generator as the source of truth.

## Generated C model

Use a small semantic model rather than embedding preformatted text.

Example shape:

```c
typedef enum help_span_kind {
    HELP_SPAN_PARAGRAPH,
    HELP_SPAN_TEXT,
    HELP_SPAN_H3,
    HELP_SPAN_BULLET,
    HELP_SPAN_NUMBER,
    HELP_SPAN_CODE,
    HELP_SPAN_CODE_BLOCK,
    HELP_SPAN_BLANK
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
```

The generator should emit something conceptually like:

```c
static const char *c64m_help_heading = "c64m Help";

static const help_span c64m_help_section_0_spans[] = {
    { HELP_SPAN_PARAGRAPH, "Welcome to c64m." },
    { HELP_SPAN_H3, "Loading programs" },
    { HELP_SPAN_BULLET, "Use Load to open a PRG file." },
    { HELP_SPAN_CODE_BLOCK, "LOAD \"*\",8,1\nRUN" },
};

static const help_section c64m_help_sections[] = {
    {
        "Getting Started",
        c64m_help_section_0_spans,
        (int)(sizeof(c64m_help_section_0_spans) / sizeof(c64m_help_section_0_spans[0]))
    },
};

static const int c64m_help_section_count =
    (int)(sizeof(c64m_help_sections) / sizeof(c64m_help_sections[0]));
```

The exact names can be adjusted to match project style.

Keep generated strings escaped safely:

```text
- escape backslashes
- escape double quotes
- preserve newlines inside code blocks as \n
- avoid non-ASCII output unless the project explicitly allows it
```

The user prefers ASCII-only project text unless non-ASCII is explicitly requested, so keep the generated guide and implementation comments ASCII unless the manual itself contains non-ASCII content.

## Generator behavior

`tools/gen_help.py` should:

```text
1. Read ./manual/manual.md.
2. Find exactly one leading # heading.
3. Treat each ## heading as a help section.
4. Collect content after each ## until the next ##.
5. Recognize ### headings as colored content spans.
6. Recognize unordered bullets beginning with "- " or "* ".
7. Recognize ordered bullets beginning with digits followed by ". " or ") ".
8. Recognize fenced code blocks using triple backticks.
9. Recognize inline code spans delimited with backticks.
10. Emit C data into src/frontend/generated/help_content.inc.
```

For inline code, there are two acceptable first-version options.

Simpler option:

```text
Emit the whole paragraph as one paragraph span and do not color inline code yet.
```

Better option:

```text
Split paragraph content into multiple spans, alternating normal text and HELP_SPAN_CODE spans.
```

Prefer the simpler option if the frontend text layout would become too complex. Code blocks and headings provide most of the visual value.

The generator should fail with a clear error if:

```text
- no # heading exists
- no ## sections exist
- content appears before the first ## section, except blank lines or comments
- a fenced code block is unterminated
```

## CMake integration

Add a Python discovery if the project does not already have one:

```cmake
find_package(Python3 COMPONENTS Interpreter REQUIRED)
```

Add a custom command similar to:

```cmake
set(C64M_HELP_MD
    ${CMAKE_CURRENT_SOURCE_DIR}/manual/manual.md
)

set(C64M_HELP_INC
    ${CMAKE_CURRENT_BINARY_DIR}/generated/help_content.inc
)

add_custom_command(
    OUTPUT ${C64M_HELP_INC}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_help.py
            ${C64M_HELP_MD}
            ${C64M_HELP_INC}
    DEPENDS
            ${C64M_HELP_MD}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_help.py
    COMMENT "Generating c64m help content"
    VERBATIM
)

add_custom_target(c64m_help_content
    DEPENDS ${C64M_HELP_INC}
)
```

Then make the frontend target depend on `c64m_help_content` and ensure the generated include directory is visible to `help_view.c`:

```cmake
target_include_directories(c64m_frontend_target PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

add_dependencies(c64m_frontend_target c64m_help_content)
```

Use the actual frontend target name from the existing CMake files.

Prefer generating into the build directory instead of the source directory unless the project already checks in generated `.inc` files.

In `help_view.c`, include it as:

```c
#include "help_content.inc"
```

## Frontend state

Add help state to the main frontend/UI state object.

Suggested struct:

```c
typedef struct frontend_help_state {
    bool open;
    bool paused_by_help;
    bool was_running_before_help;
    int section_index;
} frontend_help_state;
```

If Nuklear scroll position needs explicit preservation per section, extend later:

```c
float section_scroll_y[MAX_HELP_SECTIONS];
```

Do not overbuild this in the first version.

## Public help view interface

`src/frontend/help_view.h` should expose a narrow interface.

Example:

```c
#ifndef C64M_FRONTEND_HELP_VIEW_H
#define C64M_FRONTEND_HELP_VIEW_H

#include <stdbool.h>

struct nk_context;
struct runtime_client;

typedef struct frontend_help_state frontend_help_state;

void frontend_help_init(frontend_help_state *help);
bool frontend_help_is_open(const frontend_help_state *help);
void frontend_help_toggle(frontend_help_state *help, struct runtime_client *client);
void frontend_help_close(frontend_help_state *help, struct runtime_client *client);
void frontend_help_render(frontend_help_state *help, struct nk_context *ctx, float x, float y, float w, float h);

#endif
```

Adjust names to match existing frontend naming conventions.

Avoid exposing generated help data outside `help_view.c` unless another frontend file needs it.

## Pause/resume integration

The help view must use runtime/client commands.

Implementation agent should inspect the existing frontend code for current pause/run command functions. Use those existing helpers rather than inventing a second command path.

Pseudo-code:

```c
void frontend_help_open(frontend_help_state *help, runtime_client *client)
{
    if(help->open) {
        return;
    }

    help->was_running_before_help = frontend_runtime_is_running_snapshot_or_state(...);
    help->paused_by_help = false;

    if(help->was_running_before_help) {
        runtime_client_pause(client);
        help->paused_by_help = true;
    }

    help->open = true;
}

void frontend_help_close(frontend_help_state *help, runtime_client *client)
{
    if(!help->open) {
        return;
    }

    help->open = false;

    if(help->paused_by_help) {
        runtime_client_run(client);
    }

    help->paused_by_help = false;
    help->was_running_before_help = false;
}
```

Use the actual runtime state query or frontend cached runtime status already present in the project.

Do not assume a pause command has completed synchronously unless the existing UI already treats that path synchronously. The UI should be tolerant of runtime asynchrony.

## Input handling

Find the existing SDL event handling path for keyboard shortcuts.

Add handling for:

```text
OPTION+H
ESC
```

SDL key/modifier notes:

```text
- On macOS, OPTION is usually represented by KMOD_ALT.
- Treat left and right option/alt the same.
- Use the existing project shortcut conventions if already present.
```

Pseudo-code:

```c
static bool is_option_h(const SDL_Event *event)
{
    SDL_Keymod mod;

    if(event->type != SDL_KEYDOWN) {
        return false;
    }

    if(event->key.repeat) {
        return false;
    }

    mod = SDL_GetModState();

    return event->key.keysym.sym == SDLK_h && (mod & KMOD_ALT) != 0;
}
```

When help is open:

```text
- ESC closes help and consumes the event
- OPTION+H closes help and consumes the event
- other input should be offered to Nuklear/help before emulator keyboard input
```

When help is closed:

```text
- OPTION+H opens help and consumes the event
```

Do not let `OPTION+H` also type into the emulated C64 keyboard matrix.

## Nuklear rendering details

Render the help as a full-window or near-full-window overlay.

Suggested flags:

```c
NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR
```

Do not make the outer help window scroll. Only the content group scrolls.

Pseudo-code:

```c
void frontend_help_render(frontend_help_state *help, struct nk_context *ctx, float x, float y, float w, float h)
{
    struct nk_rect bounds;
    float heading_h;
    float footer_h;
    float content_h;

    if(!help || !help->open) {
        return;
    }

    bounds = nk_rect(x, y, w, h);
    heading_h = 32.0f;
    footer_h = 38.0f;
    content_h = h - heading_h - footer_h - 24.0f;

    if(nk_begin(ctx, "Help", bounds, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, heading_h, 1);
        render_help_heading(ctx, c64m_help_heading);

        nk_layout_row_dynamic(ctx, content_h, 1);
        if(nk_group_begin(ctx, "HelpContent", NK_WINDOW_BORDER)) {
            render_help_section(ctx, &c64m_help_sections[help->section_index]);
            nk_group_end(ctx);
        }

        nk_layout_row_dynamic(ctx, footer_h, 1);
        render_help_section_buttons(ctx, help);
    }

    nk_end(ctx);
}
```

Clamp the current section index before rendering:

```c
if(help->section_index < 0) {
    help->section_index = 0;
}
if(help->section_index >= c64m_help_section_count) {
    help->section_index = c64m_help_section_count - 1;
}
```

If section count is zero, render a clear fallback message rather than crashing.

## Rendering spans

Use span kind to choose color and layout.

Pseudo-code:

```c
static void render_help_span(struct nk_context *ctx, const help_span *span)
{
    switch(span->kind) {
        case HELP_SPAN_H3:
            nk_label_colored(ctx, span->text, NK_TEXT_LEFT, help_theme_h3);
            break;

        case HELP_SPAN_BULLET:
            render_colored_prefix_line(ctx, "*", help_theme_bullet, span->text, help_theme_body);
            break;

        case HELP_SPAN_NUMBER:
            render_colored_number_line(ctx, span->text);
            break;

        case HELP_SPAN_CODE:
            nk_label_colored(ctx, span->text, NK_TEXT_LEFT, help_theme_code);
            break;

        case HELP_SPAN_CODE_BLOCK:
            render_code_block(ctx, span->text);
            break;

        case HELP_SPAN_BLANK:
            nk_spacing(ctx, 1);
            break;

        case HELP_SPAN_PARAGRAPH:
        case HELP_SPAN_TEXT:
        default:
            nk_label_wrap(ctx, span->text);
            break;
    }
}
```

Use `nk_label_wrap` for normal paragraph text so wrapping responds to window size changes.

For code blocks, preserve line breaks. A simple first version may split the code block on `\n` and render one colored label per line.

## Section buttons

The bottom row should show one button per `##` section.

For a small number of sections, a dynamic row with `section_count` columns is fine:

```c
nk_layout_row_dynamic(ctx, 28.0f, c64m_help_section_count);
for(i = 0; i < c64m_help_section_count; i++) {
    if(nk_button_label(ctx, c64m_help_sections[i].title)) {
        help->section_index = i;
    }
}
```

If the manual grows many sections, improve this later by making the bottom row horizontally scrollable or wrapping buttons across two fixed rows.

First version can assume a modest number of sections.

## Theme and C64 palette

Create `src/frontend/help_theme.h` with compile-time color constants.

Example:

```c
#ifndef C64M_FRONTEND_HELP_THEME_H
#define C64M_FRONTEND_HELP_THEME_H

#include "nuklear.h"

#define C64M_HELP_C64_BLACK       nk_rgb(0x00, 0x00, 0x00)
#define C64M_HELP_C64_WHITE       nk_rgb(0xff, 0xff, 0xff)
#define C64M_HELP_C64_RED         nk_rgb(0x88, 0x00, 0x00)
#define C64M_HELP_C64_CYAN        nk_rgb(0xaa, 0xff, 0xee)
#define C64M_HELP_C64_PURPLE      nk_rgb(0xcc, 0x44, 0xcc)
#define C64M_HELP_C64_GREEN       nk_rgb(0x00, 0xcc, 0x55)
#define C64M_HELP_C64_BLUE        nk_rgb(0x00, 0x00, 0xaa)
#define C64M_HELP_C64_YELLOW      nk_rgb(0xee, 0xee, 0x77)
#define C64M_HELP_C64_ORANGE      nk_rgb(0xdd, 0x88, 0x55)
#define C64M_HELP_C64_BROWN       nk_rgb(0x66, 0x44, 0x00)
#define C64M_HELP_C64_LIGHT_RED   nk_rgb(0xff, 0x77, 0x77)
#define C64M_HELP_C64_DARK_GREY   nk_rgb(0x33, 0x33, 0x33)
#define C64M_HELP_C64_GREY        nk_rgb(0x77, 0x77, 0x77)
#define C64M_HELP_C64_LIGHT_GREEN nk_rgb(0xaa, 0xff, 0x66)
#define C64M_HELP_C64_LIGHT_BLUE  nk_rgb(0x00, 0x88, 0xff)
#define C64M_HELP_C64_LIGHT_GREY  nk_rgb(0xbb, 0xbb, 0xbb)

#define C64M_HELP_COLOR_BG        C64M_HELP_C64_BLUE
#define C64M_HELP_COLOR_HEADING   C64M_HELP_C64_WHITE
#define C64M_HELP_COLOR_BODY      C64M_HELP_C64_LIGHT_BLUE
#define C64M_HELP_COLOR_H3        C64M_HELP_C64_YELLOW
#define C64M_HELP_COLOR_BULLET    C64M_HELP_C64_CYAN
#define C64M_HELP_COLOR_NUMBER    C64M_HELP_C64_CYAN
#define C64M_HELP_COLOR_CODE      C64M_HELP_C64_LIGHT_GREEN
#define C64M_HELP_COLOR_CODE_BG   C64M_HELP_C64_DARK_GREY

#endif
```

Exact colors may be adjusted. Keep this compile-time and simple.

Do not add runtime color preferences for this feature.

## C64 font stretch goal

Do not implement the C64 TTF font in the first pass unless the help system is otherwise complete.

Stretch implementation:

```text
1. Place the chosen permissively licensed C64-style TTF under a suitable asset path.
1.1. The font is availale in ./manual/c-64.ttf.
1.2. The bitmap version of the font is also available as ./manual/C-64 Font.png, if that is needed or helpful.
2. Add a tool to bin-include it as a generated C array.
3. Load it into the existing Nuklear font atlas during frontend initialization.
4. Use the font only for the help view or expose it as a frontend font option.
```

Important:

```text
- Verify the font license before vendoring.
- Do not put font loading into machine or runtime.
- Do not complicate platform unless existing Nuklear font atlas setup already lives there.
```

The first version should render correctly with the existing Nuklear font.

## Tests and validation

Add generator-level tests if the project already has a place for tool tests.

At minimum, manually validate with a sample `manual/manual.md` containing:

```md
# c64m Help

## Getting Started
Welcome to c64m.

### Loading PRGs
Use the Load button.

- Open Machine tab
- Click Load
- Select a PRG

1. Load
2. Run

```basic
LOAD "*",8,1
RUN
```

## Keyboard
Press OPTION+H to open help.
Press ESC to close help.
```

Validation checklist:

```text
- build generates help_content.inc from ./manual/manual.md
- emulator launches with no runtime file lookup for help
- OPTION+H opens help
- emulator pauses when help opens while running
- ESC closes help
- OPTION+H closes help when already open
- emulator resumes only if help caused the pause
- opening help while already paused leaves emulator paused after close
- top heading remains fixed while scrolling content
- bottom section buttons remain fixed while scrolling content
- clicking section buttons changes the middle content only
- heading text does not change between sections
- long paragraphs wrap when the window size changes
- bullets, ordered numbers, H3 headings, and code blocks use distinct colors
- no live machine pointer crosses into frontend help code
- runtime and machine remain SDL/Nuklear-free
```

Run existing tests after implementation. The project status says the emulator currently has broad implemented runtime, UI, disk, audio, SID, and debugger functionality. This feature should not regress those areas.

## Definition of done

The implementation is complete when:

```text
- ./manual/manual.md is the single source for emulator help content
- the build generates static C help data from that Markdown file
- the application compiles help content into the executable
- OPTION+H opens help and pauses appropriately
- ESC and OPTION+H close help
- pause/resume behavior preserves pre-existing paused state
- the help overlay is rendered through Nuklear
- the heading row is fixed
- the content region scrolls
- the section row is fixed
- section buttons switch content
- compile-time C64 palette color choices exist for headings, bullets, numbers, code, and body text
- the implementation stays inside the documented architecture boundaries
- existing tests still pass
```

## Non-goals for first implementation

Do not implement these in the first pass:

```text
- runtime Markdown parsing
- live manual reload
- full CommonMark support
- tables
- images
- nested Markdown layout
- runtime theme editor
- PDF generation inside the emulator
- C64 TTF font embedding unless all core behavior is already complete
```

## Preferred implementation sequence

Use this order:

```text
1. Add a minimal ./manual/manual.md if one does not already exist.
2. Add tools/gen_help.py.
3. Generate a simple help_content.inc with heading, sections, and paragraph spans.
4. Add help_view.h/help_view.c and render a static overlay.
5. Wire OPTION+H and ESC input handling.
6. Wire pause/resume through runtime_client.
7. Add scrollable middle content group.
8. Add bottom section buttons.
9. Add H3, bullet, ordered-list, and code-block span support.
10. Add compile-time C64 palette theme constants.
11. Test pause/resume edge cases.
12. Run the normal test suite.
13. Update STATUS.md only if project convention requires recording this frontend feature.
```

Keep each step small and buildable.

