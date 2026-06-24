# Frontend, debugger, and UI status

## Current implementation

- Debugger UI is complete through Phase 13, with subsequent breakpoint action enhancements.
- Configuration UI is complete through Phase 14.
- CPU/registers, memory, disassembly, misc/debugger tabs, execute/read/write breakpoints/watchpoints, counters/actions, and INI persistence are implemented.
- Call stack view is implemented in the Misc|Debugger tab.
- Hardware view is implemented in the Misc|Hardware tab.
- Memory/disassembly source modes are implemented.
- Memory view virtual views are implemented.
- Assembler UI integration is implemented.
- Host file load/save UI is implemented.
- Help UI Phases 1 through 5 are implemented.
- Dialog modal input exclusivity is implemented.
- Breakpoint action parameters: Tron accepts an optional custom trace file path (empty = `trace.log`), Swap accepts a disk queue parameter (`+N` relative forward, `-N` relative backward, `N` absolute 1-based with wrap), Type accepts raw text (no-op pending translator). Tron and Troff are mutually exclusive in the editor. INI and UI both persist and restore these parameters.

## Runtime/frontend ownership

- Runtime owns machine state, breakpoints, watchpoints, stop reason, counters, and actions.
- Frontend renders copied snapshots only.
- Frontend sends intents/commands to runtime.
- Register and memory edits apply only while paused.
- Running edits are ignored.
- Debugger input focus is explicit: C64 display versus debugger views.
- Symbol table is tools/frontend/debug-session-owned, separate from emulator machine and assembler internals.

## Memory/disassembly source modes

Modes are independent per view:

- Map: CPU-visible address space through `c64_debug_read_cpu_map`.
- ROM: physical ROM bytes through `c64_debug_read_rom`, regardless of current mapping.
- RAM: raw RAM through `c64_debug_read_ram`, regardless of ROM overlay.

UI behavior:

- Right-click contextual popup selects source mode and shows active-mode dot indicator.
- The memory/disassembly contextual popup groups options under `Source`,
  `View`, and `Access` headings. Memory view popups include `Split` and, when
  multiple virtual views exist, `Join`. While stopped both popups show four
  16-bit write-history lanes for the cursor address; selecting a lane navigates
  the disassembly cursor to that writer PC. The history comes from
  runtime-published snapshots; live machine pointers do not cross into
  frontend. The popups are clamped to the app viewport and use an internal
  scrollable region when the full menu cannot fit.
- Opt+M cycles source mode in the focused memory/disassembly view.
- Opt+Tab cycles active view C64 -> Disassembly -> Misc -> Memory.
- Shift+Opt+Tab reverses that order.
- ROM mode shows amber border inside the content area.
- RAM mode shows blue border inside the content area.
- Map has no source-mode color.
- Active C64, disassembly, misc, or memory view shows a neutral border when no modal dialog is open.
- Memory view bottom status row shows active edit field, cursor address, and editability.

## Memory view virtual views

- Memory panel supports up to 16 independent virtual views stacked vertically.
- Opt+V splits the active view at the cursor.
- Shift+Opt+V splits row-aligned.
- Opt+J dissolves the active view; it is a no-op on the last view.
- Opt+Up/Down navigates between views.
- Each view has its own cursor, scroll, source mode, and edit state.
- Row height is distributed proportionally with at least one row per view.
- A 16-slot color palette assigns unique background per view.
- Slots are freed on dissolve and reused.
- Click activates a view.
- Mouse wheel scrolls the hovered view.
- ROM/RAM borders are inset per view.
- Active-panel selection border spans the whole panel.
- Scrollbar tracks the active view.

## Call stack and hardware views

- Runtime walks the 6510 stack each frame.
- It verifies JSR opcode at each candidate return address through the CPU memory map.
- It publishes up to 16 entries as `runtime_call_stack_snapshot`.
- UI displays `XXXX | JSR label/YYYY` rows.
- Clicking either column centers the disassembly view on that address.
- Hardware view uses collapsible `NK_TREE_TAB` sections for Memory/Banks, VIC-II, CIA #1/#2, SID, and counters.
- Hardware view rows use wider static layout so the tab can scroll horizontally for long diagnostics.

## Configuration UI

- Configure dialog supports PAL/NTSC, display, turbo, symbol, and INI options.
- Runtime config apply is implemented.
- Video-standard changes reboot the runtime.
- Invalid breakpoint INI entries are skipped while valid entries load.

## Assembler UI

- Assembler tab, file picker, address/run address, auto-run, reset/run-to-BASIC assembly flow, assembler error event/dialog, and symbol snapshot handoff to disassembler are implemented.
- Reset checkbox is above Assemble and defaults on.
- When Reset is unchecked, assembly writes directly into live RAM in any execution state.
- If Auto Run is set with Reset unchecked, the runtime jumps to run address and resumes running.

## Host load/save UI

- Machine tab layout is Disks, Programs, Emulator.
- Unified Load and Save buttons are on Machine tab.
- Load dialog has Name + Browse, From File address, Reset, and Basic Program checkboxes.
- Save dialog has Name + Browse, Basic Program, Write address header, and Start/End range fields.
- Basic Program save reads `$2B/$2C` as start and `$2D/$2E` as exclusive end, and forces Write address header.

## Help UI

- Build-time `manual/manual.md` is converted to compiled help data.
- Nuklear help overlay is implemented.
- OPTION+H and ESC toggle help.
- Runtime-client pause/resume is implemented for help.
- Fixed heading/footer with scrollable section content is implemented.
- Per-section scroll memory and keyboard navigation are implemented.
- PageUp/PageDown/Home/End and Left/Right section switching are implemented.
- C64-inspired help theme colors are compile-time constants.
- Safe long-line wrapping is implemented.
- Generator supports `--level N` to choose which Markdown heading level becomes the bottom-row section list.
- Embedded C64 Pro Mono TrueType font is compiled in and used only for help view text at 10 px.
- Character-level hard-wrap fallback prevents unbroken tokens from overflowing.
- Code blocks route through the same inline wrap path as body text.

## Dialog modal input exclusivity

- When any dialog is open, SDL mouse-down events outside open dialog bounds are swallowed before reaching Nuklear.
- Base views cannot be brought forward or gain input focus by outside clicks while a dialog is open.
- Mouse motion and button-up events still pass through so dialog dragging continues to work.
- Custom input code is gated by `frontend_any_dialog_open`.
- Dialog bounds are queried each frame through `nk_window_find` using registered window names.

## Symbol lookup dialog

- Opt+S in the active Disassembly or Memory view opens the Symbol Lookup modal.
- Dialog shows all symbols from the frontend symbol table with columns: ADDR, SCOPE, LABEL, SOURCE.
- ADDR is 4 hex digits; SCOPE, LABEL, and SOURCE are truncated to 15 characters each.
- SCOPE is the assembler scope path (e.g. `anon_0001`); LABEL is the display name (leaf portion); SOURCE is the file basename without extension, or "assembler" for assembler-defined symbols.
- Search box has default focus on open; pattern is `nk_strfilter`-style regex (`.`, `*`, `^`, `$` supported) matched against a combined `"XXXX scope label source"` string for each row.
- Column header buttons show the active sort column with `^` (ascending) or `v` (descending); clicking the active column toggles direction; clicking another column sets it as primary sort ascending.
- Default sort is by address ascending.
- TAB toggles keyboard focus between search box and table. Arrow Up/Down navigate the table when it has keyboard focus; Enter commits the selection.
- Clicking any cell in a row commits that row's address and closes the dialog.
- Close button or ESC dismisses without navigation.
- On DASM selection: jumps cursor to the address (equivalent to Opt+A goto).
- On Memory selection: scrolls the active memory view so the address is row-aligned and places the cursor on the exact address.

## Known limitations / deferred

- Breakpoint Type action: text storage and INI round-trip are implemented; the translator that converts stored text into timed C64 keystrokes is deferred.
- Breakpoint Swap action always targets device 8; per-device selection is deferred.
- Tron trace output always appends to the named file; rotation, size limits, and per-breakpoint handle management are deferred.

## Tests / smoke checks

- Verify memory/disassembly Map/ROM/RAM source modes independently per view.
- Verify virtual memory views preserve per-view cursor, scroll, source mode, and edit state.
- Verify modal dialogs prevent base view focus changes from outside clicks.
- Verify assembler reset-on and reset-off flows.
- Verify host load/save paths, especially Basic Program TXTTAB/VARTAB behavior.
- Verify symbol lookup opens from both Disassembly and Memory views (Opt+S).
- Verify search filters symbols with regex patterns; verify column header sorting.
- Verify DASM selection jumps cursor; verify Memory selection row-aligns view and places cursor.

## Files likely involved

- `src/frontend/*`
- `src/runtime/*`
- `src/tools/*`
- `manual/manual.md`
- UI/debugger/config/help tests under `tests/`
