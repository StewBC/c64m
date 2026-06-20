# C64HLPHASE_4.md

# c64m Help System - Phase 4 Implementation Guide

## Purpose

Implement the two help-system items that were missed when the revised Phase 2 and Phase 3 guides were generated:

1. Preserve the accepted Phase 1 help formatting, but add safe wrapping for lines that are too long.
2. Add a tunable section cutoff level to the manual generator, defaulting to level 2.

This phase must be conservative. The previous broad Phase 2 attempt caused rendering regressions, so this guide explicitly limits the implementation to these two features only.

## Required Reading

Before implementation, read these documents in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. The current help implementation.
5. This document.

The important architectural constraints remain:

- Help rendering belongs in `frontend/`.
- Runtime control must go through `runtime_client`.
- The frontend must not access the live machine directly.
- Do not add SDL/Nuklear/frontend dependencies to `machine/`, `runtime/`, or `util/`.
- Do not parse Markdown at runtime.
- Do not alter emulator core behavior.

## Current Assumptions

The current accepted implementation already has:

- `./manual/manual.md` as the manual source.
- Build-time generation of compiled help data.
- Nuklear help overlay.
- OPTION+H and ESC help behavior.
- Correct pause/resume behavior.
- Fixed heading row.
- Scrollable middle content.
- Fixed bottom section row.
- Section buttons.
- Acceptable Phase 1 text formatting.
- Acceptable table handling.
- Acceptable color handling.
- Acceptable code/block handling.

This phase must preserve those accepted behaviors.

## Non-goals

Do not implement any of the following in Phase 4:

- C64 palette redesign.
- Custom TTF font.
- Font embedding.
- Runtime Markdown parsing.
- Live reload.
- New Markdown features beyond the section cutoff behavior.
- Inline-code rendering changes.
- Code-block rendering changes.
- Table rendering changes.
- Help color changes.
- Help spacing changes, except where unavoidable for wrapping long lines.
- Per-section scroll memory.
- Keyboard navigation.
- Section button visual redesign.
- Generator tests/self-test unless trivial and already fitting the project.
- Any change to `machine/`.

C64 colors and TTF font work belong to Phase 3. Scroll memory and keyboard navigation belong to Phase 2.

## Feature 1: Safe Long-line Wrapping

### Goal

Keep the current accepted Phase 1 text formatting, but prevent overlong lines from running off the visible help content area.

The implementation should wrap only when lines are too long for the available content width.

This is not a typography rewrite. It is a safety fix for long lines.

### Preserve Current Formatting

Do not change:

- paragraph spacing;
- table formatting;
- code-block formatting;
- inline-code formatting;
- bullet color behavior;
- heading color behavior;
- existing line tightening;
- existing row structure;
- fixed heading row;
- fixed bottom section row;
- middle content scroll behavior.

If a line already fits, it should render exactly as before.

### Scope of Wrapping

Apply wrapping to normal readable help text where overflow is a problem.

Recommended wrapping targets:

```text
normal paragraphs
subsection headings
bullet item text
numbered item text
table cells, only if current table rendering supports it safely
```

Be careful with code blocks and tables.

For Phase 4, acceptable behavior is:

- normal text wraps;
- bullets/numbers wrap with continuation indentation if practical;
- code blocks may remain as currently rendered if wrapping them would change accepted behavior;
- tables should not be rewritten unless the current table renderer already has a safe cell wrapping path.

If code blocks or table cells overflow and fixing them would require a larger renderer rewrite, document that as deferred rather than destabilizing rendering.

### Wrapping Strategy

Prefer a frontend-only helper in the help renderer.

The helper should:

1. Receive the available content width.
2. Measure or estimate whether text fits.
3. Render the original line unchanged if it fits.
4. Split and render wrapped lines only if needed.

Use existing Nuklear text measurement if available.

If exact text measurement is not practical, use a conservative approximate character-width estimate based on the current font height or known average glyph width.

The important rule is to avoid making short lines look different.

### Suggested Helper Shape

Adapt names to the project style.

```c
static void help_label_wrap_if_needed(
    struct nk_context *ctx,
    const char *text,
    float available_width,
    nk_flags align
);
```

For bullets and numbered items, use a helper that preserves the marker:

```c
static void help_bullet_wrap_if_needed(
    struct nk_context *ctx,
    const char *marker,
    const char *text,
    float available_width
);
```

Expected bullet rendering:

```text
* first wrapped line starts after marker
  continuation line aligns with text where practical
```

If continuation indentation is awkward in Nuklear, simpler wrapping is acceptable as long as it remains readable and does not damage existing formatting.

### Avoid Per-frame Heap Churn

Do not allocate large temporary buffers every frame.

Acceptable approaches:

1. Small stack buffers for one wrapped segment at a time.
2. Reusable frontend scratch buffer if one exists.
3. Direct rendering from substring ranges if convenient.

If dynamic allocation is unavoidable, keep it local, bounded, and checked, but prefer avoiding it.

### Window Resize Behavior

Wrapping must respond to window size changes.

When the help content width changes, newly rendered frames should wrap according to the new width.

Do not pre-wrap text in the generator based on a fixed width. The emulator window size can change.

### Acceptance Criteria for Long-line Wrapping

This feature is complete when:

- Long normal text lines wrap inside the visible help content area.
- Lines that already fit are visually unchanged.
- Existing tables remain no worse than before.
- Existing code blocks remain no worse than before.
- Existing colors remain unchanged.
- Existing spacing remains unchanged except for additional wrapped continuation lines.
- Wrapping adapts to window width changes.
- No per-frame unbounded allocation is introduced.
- The help overlay still scrolls only in the middle content area.
- Existing OPTION+H, ESC, pause/resume, and section button behavior still works.

## Feature 2: Tunable Section Cutoff Level

### Goal

Make the help generator decide which Markdown headings become bottom-row help sections based on a tunable heading level.

Default behavior:

```text
section level = 2
```

That means:

```text
## headings become bottom-row sections
### and deeper headings do not become bottom-row sections
```

The source manual remains:

```text
./manual/manual.md
```

### Option Name

Add a command-line option to the generator:

```text
--level N
```

Where:

```text
N = Markdown heading level used as the help section level
```

Default:

```text
--level 2
```

Examples:

```text
python3 tools/gen_help.py ./manual/manual.md --level 2
python3 tools/gen_help.py ./manual/manual.md --level 3
```

If the current generator uses a different argument order, keep the existing order and add `--level N` in the least disruptive way.

### Meaning

If `--level 2`:

```md
# Manual Title

## Section A
content

### Subsection A.1
content

## Section B
content
```

Generated bottom-row sections:

```text
Section A
Section B
```

`### Subsection A.1` remains content inside Section A.

If `--level 3`:

```md
# Manual Title

## Chapter A

### Section A.1
content

### Section A.2
content
```

Generated bottom-row sections:

```text
Section A.1
Section A.2
```

The `## Chapter A` heading should remain content or context according to the current renderer's behavior. Do not discard useful text unless the current implementation already does so.

### Default and Build Integration

The CMake/custom build rule should preserve existing behavior by default.

That means if the build rule does not specify a level, the generator defaults to 2.

Optionally, CMake may expose a variable:

```text
C64M_HELP_SECTION_LEVEL
```

Default:

```text
2
```

Then invoke:

```text
tools/gen_help.py ./manual/manual.md --level ${C64M_HELP_SECTION_LEVEL}
```

This CMake variable is optional. The required part is the generator `--level` option with default 2.

### Valid Values

Accept heading levels:

```text
2 through 6
```

Reject:

```text
0
1
7+
non-integers
negative numbers
```

Reason:

- Level 1 is reserved for the fixed help heading.
- Levels 2 through 6 are Markdown section/subsection headings.
- Level 0 and deeper than 6 are not Markdown headings.

Error messages should be clear.

Example:

```text
tools/gen_help.py: --level must be an integer from 2 to 6
```

### Structural Rules

The manual should still have exactly one top-level `#` heading used as the fixed help heading.

The generator should require at least one heading at the selected section level.

If no headings at the selected level exist, fail clearly.

Example:

```text
manual/manual.md: no level-3 headings found for help sections
```

### Content Grouping

The selected heading level controls section splitting.

For a selected level `N`:

- A heading exactly at level `N` starts a new bottom-row section.
- Headings deeper than `N` are content inside the current section.
- Headings shallower than `N`, except the top-level `#`, are not bottom-row sections.

Recommended behavior for headings shallower than `N`:

- Keep them as content/context inside the following or current section if doing so is simple and consistent with current rendering.
- Do not invent new bottom-row sections for them.

The safest implementation is:

```text
# title -> fixed heading
heading level N -> starts a new section
all other lines/headings -> content in the current section once the first level-N section has started
```

If content appears after `#` but before the first selected section heading, handle it according to current generator behavior. Prefer failing clearly if the structure is ambiguous.

### Generated Data

The generated data should remain compatible with the existing help renderer.

Do not require rendering changes unless the current generated format assumes only `##`.

If the generated model stores a heading level for content headings, preserve or add that field only if already useful. The main requirement is that bottom-row sections come only from headings matching `--level`.

### Examples

Input:

```md
# c64m Help

## Basics

Text.

### Loading

Text.

### Saving

Text.

## Keyboard

Text.
```

With default `--level 2`, section buttons:

```text
Basics
Keyboard
```

With `--level 3`, section buttons:

```text
Loading
Saving
```

Depending on current content grouping, `Basics` and `Keyboard` may appear as normal content headings or context. Keep behavior simple and document it in a comment if necessary.

### Acceptance Criteria for Section Cutoff

This feature is complete when:

- Generator supports `--level N`.
- Default section level is 2.
- `--level 2` preserves current behavior.
- `--level 3` uses `###` headings as bottom-row sections.
- Headings deeper than the selected level stay inside the current section as content.
- Invalid levels fail clearly.
- Missing selected-level headings fail clearly.
- CMake/build integration still works without requiring a new option.
- The help executable remains standalone.
- Existing help rendering is otherwise unchanged.

## Suggested Implementation Order

1. Inspect current `tools/gen_help.py`.
2. Add argument parsing for `--level N`, default 2.
3. Replace any hard-coded `##` section split logic with selected-level split logic.
4. Preserve `#` as the fixed help heading.
5. Preserve deeper headings as content.
6. Add clear validation for invalid levels and missing selected-level headings.
7. Confirm `--level 2` output is equivalent to current behavior.
8. Add frontend wrapping helper for overlong normal text.
9. Apply wrapping narrowly to normal text paths first.
10. Apply wrapping to bullets/numbers if safe.
11. Avoid touching table/code rendering unless current implementation already supports safe wrapping.
12. Validate manually.
13. Update `STATUS.md`.

## Manual Validation Checklist

Run this checklist before claiming Phase 4 complete:

```text
[ ] Build succeeds from a clean tree.
[ ] Default build still uses level 2 sections.
[ ] `--level 2` preserves existing section buttons.
[ ] `--level 3` uses `###` headings as section buttons.
[ ] Invalid levels fail clearly.
[ ] Missing selected-level headings fail clearly.
[ ] Help opens with OPTION+H.
[ ] Help closes with OPTION+H.
[ ] Help closes with ESC.
[ ] Pause/resume behavior is unchanged.
[ ] Existing section buttons work.
[ ] Existing colors are unchanged.
[ ] Existing tables are no worse than before.
[ ] Existing code blocks are no worse than before.
[ ] Short lines are visually unchanged.
[ ] Long paragraph lines wrap within the content area.
[ ] Long bullet/number lines wrap acceptably.
[ ] Wrapping adapts when the window is resized.
[ ] Only the middle content area scrolls.
[ ] Existing smoke tests still pass.
```

## Regression Rules

This phase must not regress accepted help rendering.

If a wrapping change makes normal lines, tables, code blocks, or colors worse, revert the wrapping change and narrow the scope.

If the section-level option changes the default generated output, fix it before proceeding.

The default `--level 2` behavior must match the current accepted behavior.

## STATUS.md Update

After implementation and validation, update `STATUS.md` with a concise factual entry.

Example:

```text
- Help UI Phase 4: added safe long-line wrapping for help text and a generator `--level N` option for selecting which Markdown heading level becomes the bottom-row help section list, defaulting to level 2.
```

Do not mention C64 colors, TTF fonts, scroll memory, or keyboard navigation in this Phase 4 status entry unless those were separately implemented under their own phase.
