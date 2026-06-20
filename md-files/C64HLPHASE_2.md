# C64HLPHASE_2.md

# c64m Help System - Phase 2 Implementation Guide

## Purpose

Implement the remaining Phase 2 help-system usability features:

1. Per-section scroll memory.
2. Keyboard navigation while help is open.

Do not change the Phase 1 text formatting, table formatting, color behavior, line tightening, or existing code/block rendering. Those are already considered satisfactory for now.

This phase is deliberately narrow because an earlier broad Phase 2 attempt regressed rendering. Preserve the current accepted help rendering unless a change is strictly required for this phase.

## Required Reading

Before implementation, read these documents in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. The current help implementation.
5. This document.

The important architectural constraints remain:

- The help system belongs in `frontend/`.
- Runtime control must go through `runtime_client`.
- The frontend must not access the live machine directly.
- Do not add SDL/Nuklear/frontend dependencies to `machine/`, `runtime/`, or `util/`.
- Do not parse Markdown at runtime.
- Do not alter emulator core behavior.

## Current Assumptions

Phase 1 and follow-up fixes have already provided:

- `./manual/manual.md` as the manual source.
- Build-time generation of compiled help data.
- Nuklear help overlay.
- OPTION+H opens/closes help.
- ESC closes help.
- Emulator pause/resume behavior through `runtime_client`.
- Fixed heading row.
- Scrollable help content area.
- Fixed bottom section row.
- Section buttons.
- Tables.
- Acceptable text/code formatting.
- Acceptable line wrapping for long lines.
- Tunable section-header cutoff in the generator.

This phase must not rewrite those pieces.

## Non-goals

Do not implement any of the following in Phase 2:

- C64 palette redesign.
- Custom TTF font.
- Font embedding.
- New Markdown parser.
- Runtime Markdown parsing.
- Live reload.
- Richer Markdown support.
- Inline-code rendering changes.
- Code-block rendering changes.
- Table rendering changes.
- Help color changes.
- Help spacing/line tightening changes.
- Section button redesign beyond what is needed for keyboard navigation.
- Generator tests/self-test unless trivial and already in place.
- Manual-authoring documentation.
- Changes to `machine/`.

C64 colors and TTF font work belong to Phase 3.

## Feature 1: Per-section Scroll Memory

### Goal

Each help section should remember its own scroll position.

Expected behavior:

1. Open help.
2. Select section A.
3. Scroll halfway down section A.
4. Select section B.
5. Scroll somewhere else in section B.
6. Select section A again.
7. Section A returns to its previous scroll position.

This makes section switching less disruptive in a long manual.

### State

Extend the existing frontend help state.

Use the project's current names and style, but the state should conceptually include:

```c
typedef struct frontend_help_state {
    bool open;
    bool paused_by_help;
    int section_index;

    /* Phase 2 */
    int *section_scroll_x;
    int *section_scroll_y;
    int section_scroll_count;
} frontend_help_state;
```

If Nuklear exposes scroll values as unsigned or another type, use the actual type needed by the existing Nuklear API.

If horizontal scrolling is not used in the current help view, `section_scroll_x` may be omitted or always set to zero. Preserve `section_scroll_y`.

### Allocation Policy

Prefer static or frontend-lifetime allocation.

Do not allocate every frame.

Options, in order of preference:

1. Fixed-size array if the generated help data has a known maximum section count.
2. Frontend-lifetime allocation sized to `help_section_count`.
3. Existing project allocator/helper if one is already used in frontend code.

The scroll-memory storage should be initialized once when the frontend help state is initialized, or lazily the first time help opens.

### Section Count Changes

The generated help section count is compile-time data. It should not change at runtime.

Still, implement defensive behavior:

- If `help_section_count <= 0`, do not crash.
- If scroll storage allocation fails, continue with shared/default scroll behavior rather than crashing.
- If the selected index is out of range, clamp it.

### Capturing Scroll

Before switching away from the current section, save its current scroll position.

Pseudo-flow:

```c
static void help_select_section(frontend_help_state *help, int next_index)
{
    if(next_index == help->section_index) {
        return;
    }

    help_store_scroll_for_current_section(help);

    help->section_index = next_index;

    help_restore_scroll_for_current_section(help);
}
```

The exact implementation depends on how the current help content group is named and how Nuklear scroll is accessed.

Use Nuklear's group scroll API if available in the project/backend. The usual Nuklear pattern is conceptually:

```c
nk_group_get_scroll(ctx, "HelpContent", &x, &y);
nk_group_set_scroll(ctx, "HelpContent", x, y);
```

Adjust for the actual Nuklear version and wrappers used by c64m.

### Restoring Scroll

After switching to a section, restore the stored scroll position for the content group.

Important timing detail:

- Nuklear may require the group to exist before scroll can be set.
- If setting scroll immediately during section switch does not work, store a pending restore request in help state and apply it during the next help render after `nk_group_begin()`.

Recommended defensive state:

```c
bool pending_scroll_restore;
int pending_scroll_x;
int pending_scroll_y;
```

Then, inside the content group render path:

```c
if(help->pending_scroll_restore) {
    nk_group_set_scroll(ctx, "HelpContent", help->pending_scroll_x, help->pending_scroll_y);
    help->pending_scroll_restore = false;
}
```

Use the project's preferred bool and naming style.

### Opening and Closing Help

Do not reset all per-section scroll positions each time help opens unless that is already the established Phase 1 behavior and preserving them would be surprising.

Recommended behavior:

- Preserve selected section and section scroll positions while the app is running.
- If help is opened for the first time, default to section 0 at scroll 0.
- If help is closed and reopened, return to the last selected section and its previous scroll position.

If the existing help implementation already resets to section 0 on open, do not change that unless required. In that case, still preserve section scroll positions within one open help session.

### Acceptance Criteria for Scroll Memory

This feature is complete when:

- Each section remembers its own vertical scroll position.
- Switching between sections restores the previously stored position.
- No per-frame allocation is introduced.
- Invalid or missing scroll storage does not crash the app.
- Existing mouse-wheel/content scrolling still works.
- Existing section buttons still work.
- Existing OPTION+H and ESC behavior still works.
- Current accepted text/table/code formatting is unchanged.

## Feature 2: Keyboard Navigation While Help Is Open

### Goal

Add keyboard navigation that operates only while help is open.

Required keys:

```text
PageUp      scroll current help section up
PageDown    scroll current help section down
Home        scroll current help section to top
End         scroll current help section to bottom
Left        previous section
Right       next section
```

Optional aliases, only if they fit existing conventions:

```text
Up          small scroll up
Down        small scroll down
Shift+Tab   previous section
Tab         next section
```

Do not implement optional aliases if they interfere with existing Nuklear focus behavior.

### Input Ownership

When help is open, help navigation keys must not leak into emulator input.

Behavior:

```text
Help open:
    PageUp/PageDown/Home/End/Left/Right are consumed by help.

Help closed:
    Existing emulator/frontend behavior is unchanged.
```

ESC and OPTION+H behavior remains as already implemented.

### Section Navigation

Left/Right should switch sections.

Expected behavior:

```text
Right:
    section_index = min(section_index + 1, help_section_count - 1)

Left:
    section_index = max(section_index - 1, 0)
```

Do not wrap around unless the existing UI convention strongly favors wrapping.

When switching sections by keyboard:

1. Store current section scroll.
2. Change section.
3. Restore target section scroll.

This must use the same code path as clicking section buttons where practical.

### Content Scrolling

PageUp and PageDown should scroll the middle content area, not the entire help overlay.

Recommended behavior:

```text
PageDown: scroll down by approximately one visible content page
PageUp:   scroll up by approximately one visible content page
Home:     scroll to y = 0
End:      scroll to a large y value or known content maximum
```

If the exact content height is hard to know, setting End to a large y value is acceptable if Nuklear clamps it safely.

Use actual Nuklear scroll APIs if available.

Suggested page size:

```text
content area height minus a small margin
```

If that value is not conveniently available, use a fixed conservative value such as 300 to 500 pixels, based on current UI scale.

### Event Handling Location

Implement keyboard navigation in the same frontend input path that already handles OPTION+H and ESC, unless the existing architecture has a better help-specific input handler.

Recommended structure:

```c
if(help->open) {
    if(frontend_help_handle_key(help, event)) {
        return true; /* consumed */
    }
}
```

Where `frontend_help_handle_key()` returns true only when it consumed the event.

Keep the function frontend-only.

### Interaction With Nuklear Text Input

The help view is read-only. There should be no text-editing focus inside it.

Therefore consuming navigation keys while help is open should be safe.

If a future help search field is added, this would need to be revisited. Do not add search in this phase.

### Acceptance Criteria for Keyboard Navigation

This feature is complete when:

- PageUp scrolls the current help section upward.
- PageDown scrolls the current help section downward.
- Home scrolls to the top of the current section.
- End scrolls to the bottom of the current section.
- Left selects the previous section.
- Right selects the next section.
- Section switching by keyboard preserves/restores per-section scroll state.
- Navigation keys are consumed while help is open.
- The same keys behave as before when help is closed.
- ESC still closes help.
- OPTION+H still toggles help.
- Opening/closing help still preserves correct pause/resume behavior.

## Regression Rules

This phase must not regress the accepted Phase 1/follow-up rendering.

Specifically, do not change:

- existing line spacing;
- existing table rendering;
- existing color choices;
- existing code block styling;
- existing inline-code handling;
- existing section cutoff behavior;
- existing Markdown generation behavior, except as needed to expose section count safely if not already exposed.

If a change to rendering appears necessary to implement scroll memory or keyboard navigation, keep it minimal and document why.

## Suggested Implementation Order

1. Inspect the current help view and identify the current scroll group name.
2. Add scroll-memory fields to frontend help state.
3. Initialize scroll memory based on `help_section_count`.
4. Implement helper to store current section scroll.
5. Implement helper to request/restore section scroll.
6. Route all section changes through one helper.
7. Update section-button clicks to use that helper.
8. Add keyboard section navigation with Left/Right.
9. Add PageUp/PageDown/Home/End scroll operations.
10. Validate that help key events are consumed.
11. Run build/tests/manual smoke checks.
12. Update `STATUS.md`.

## Manual Validation Checklist

Run this checklist before claiming Phase 2 complete:

```text
[ ] Build succeeds from a clean tree.
[ ] Help opens with OPTION+H.
[ ] Help closes with OPTION+H.
[ ] Help closes with ESC.
[ ] Opening help pauses only as before.
[ ] Closing help resumes only as before.
[ ] Mouse scrolling still works.
[ ] Section buttons still work.
[ ] Section cutoff behavior still works.
[ ] Long-line wrapping remains as accepted.
[ ] Tables remain as accepted.
[ ] Colors remain as accepted.
[ ] PageDown scrolls down.
[ ] PageUp scrolls up.
[ ] Home goes to top.
[ ] End goes to bottom.
[ ] Right moves to next section.
[ ] Left moves to previous section.
[ ] Scrolling section A, switching to B, then returning to A restores A's scroll position.
[ ] Help navigation keys do not affect emulator input while help is open.
[ ] Existing smoke tests still pass.
```

## STATUS.md Update

After implementation and validation, update `STATUS.md` with a concise factual entry.

Example:

```text
- Help UI Phase 2: added per-section help scroll memory and keyboard navigation for PageUp/PageDown/Home/End plus Left/Right section switching, while preserving accepted Phase 1 rendering.
```

Do not mention C64 colors or TTF font in Phase 2.
