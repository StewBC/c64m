# a2m disassembly view reference

This is a behavioral reference for Phase 12 planning only. Do not reuse the a2m implementation structure: it decodes through runtime/machine helpers that can read live machine memory. In c64m, disassembly logic belongs in `tools`, and the frontend must decode copied memory snapshots only.

## Fields shown

- Window title: `Disassembly`.
- Main line area:
  - One disassembled instruction or forced byte per visible row.
  - Line text is produced by `rt_disassemble_line`.
  - Rows are backed by a `LINE_INFO` table containing an address and `force_byte` flag.
- Footer selector:
  - Memory/bank view selector shared with memory view.
  - Apple II selectors are `RAM: Map/Main/Aux`, `C100: Map/ROM`, and `D000: Map/LC1/LC2/ROM`.
  - Disabled selector entries are drawn disabled.
- Custom vertical scrollbar at the right.

## Current PC selection and highlight

- On dirty refresh, the view tries to center the current CPU PC.
- It first sets `cursor_address` to `m->cpu.address_16`, then tries to find `m->cpu.pc` among visible lines.
- If the PC is already visible and exactly matches a line address, the line table is shifted so that PC lands at `rows / 2`.
- If the PC is not visible or not line-aligned, the view regenerates lines around the cursor address and current cursor line.
- If execution stopped because of an access breakpoint, the cursor is moved to the access address and that address is centered.
- Highlight colors:
  - PC line: yellow background, purple foreground.
  - Cursor line: cyan background, red/orange foreground.
  - Breakpoint line: red background, green foreground.
  - PC plus cursor or PC plus breakpoint blends the colors.
  - Forced-byte rows are dimmed.
- If address-entry mode is active on the cursor line, the active hex digit is drawn as an inverse one-character overlay.

## Scrolling behavior

- The view keeps decoded line boundaries in `line_info`; it does not assume one byte per row.
- Scrolling up/down by decoded rows uses previous-instruction discovery for upward fill and opcode length for downward fill.
- Mouse wheel:
  - View: Disassembly.
  - Changes: shifts the top visible decoded line.
  - Edge cases: wheel delta is multiplied by configured `scroll_wheel_lines`; the effective wheel movement is clamped to `rows - 1`, so one wheel event never scrolls more than almost a page.
  - Wheel down uses the line currently `wheel` rows below the top as the new top.
  - Wheel up keeps the current top address visible but moves it down by `wheel` rows, filling previous decoded rows above it.
- Scrollbar thumb drag:
  - View: Disassembly.
  - Changes: maps thumb position to an address-like top row from `0x0000` to `0xFFFF`.
  - Edge cases: because variable instruction length makes line count unknown, a2m approximates visible rows as `rows * 2` bytes. Dragging to an address regenerates disassembly with that address at the top.
- Scrollbar track click:
  - View: Disassembly.
  - Changes: jumps the scrollbar top by its `rows_visible` value toward the click, wrapping modulo the total range in the shared scrollbar helper.
  - Edge cases: for disassembly, `rows_visible` is `rows * 2`, so the jump is byte-address based, not exact decoded-line based.

## PageUp/PageDown/Home/End behavior

- `PageUp`:
  - View: Disassembly.
  - Changes: makes the previously top visible line become the bottom visible line, then fills upward above it and downward as needed.
  - Exact state change: captures address at visible row `0`, subtracts `rows - 1` from `cursor_line`, then places that captured address on visible row `rows - 1`.
  - Edge cases: ignored while editing an address. Cursor may become off-screen because only `cursor_line` is adjusted arithmetically.
- `PageDown`:
  - View: Disassembly.
  - Changes: makes the previously bottom visible line become the top visible line, then fills downward from it.
  - Exact state change: captures address at visible row `rows - 1`, adds `rows - 1` to `cursor_line`, then places that captured address on visible row `0`.
  - Edge cases: ignored while editing an address. Cursor may become off-screen.
- `Home`:
  - View: Disassembly.
  - Changes: cursor moves to the first visible line.
  - With `Ctrl`: first regenerates the view with address `$0000` at the first visible line, then selects the first line.
  - In address-entry mode: moves the address digit cursor to digit 0 and does not scroll.
- `End`:
  - View: Disassembly.
  - Changes: cursor moves to the last visible line.
  - With `Ctrl`: first regenerates the view with address `$FFFF` at the last visible line, then selects the last line.
  - In address-entry mode: moves the address digit cursor to digit 3 and does not scroll.

## Cursor behavior

- `Up`:
  - View: Disassembly.
  - Changes: moves cursor to the previous decoded line.
  - Edge cases: if the cursor is off-screen, it is first centered. If already on the top visible row, the previous decoded instruction is found and inserted above, keeping the cursor on row 0.
- `Down`:
  - View: Disassembly.
  - Changes: moves cursor to the next decoded line.
  - Edge cases: if off-screen, it is first centered. If already on the bottom row, the line table shifts up by one and the next decoded line is filled at the bottom, keeping cursor on the bottom row.
- `Left`:
  - View: Disassembly.
  - Changes: in address-entry mode, moves the active address digit left unless already at digit 0.
  - With `Ctrl` outside address-entry mode: sets CPU PC to `cursor_address`.
  - Edge cases: outside address-entry mode without `Ctrl`, it only ensures the cursor is on-screen.
- `Right`:
  - View: Disassembly.
  - Changes: in address-entry mode, moves the active address digit right; after digit 3 it exits address-entry mode.
  - Outside address-entry mode: recenters the view on the current CPU PC if PC is not already at the middle row.
  - With `Ctrl` outside address-entry mode: also sets the cursor to the CPU PC at the middle row.
  - Edge cases: after recentering on PC, if the previous cursor address is outside the new visible range, `cursor_line` is set to an off-screen sentinel.
- Mouse left click on a row:
  - View: Disassembly.
  - Changes: selects the clicked row's address.
  - Edge cases: clicking within the first four character columns enters address-entry mode and selects the clicked hex digit; clicking elsewhere exits to normal line cursor mode.

## Follow-PC behavior

- Dirty refresh is the main follow-PC mechanism.
- If PC is in the visible decoded range and aligned to a displayed line, the existing decode table is shifted so PC lands in the middle.
- If not, the view regenerates around the current cursor address/line rather than blindly throwing away the decode context.
- `Right` outside address mode is a manual "follow PC" action: it places the CPU PC at the center line.

## Address entry and search behavior

- `Ctrl+A`:
  - View: Disassembly.
  - Changes: toggles address-entry mode.
  - Entering address mode sets active digit to 0 and ensures the cursor is on-screen.
  - Leaving address mode restores normal line cursor mode.
- Hex digits `0`-`9` and `a`-`f` while in address-entry mode:
  - View: Disassembly.
  - Changes: replace the active nibble of `cursor_address`, regenerate the view with the new address on the cursor line, then advance the active digit.
  - Edge cases: if cursor line is off-screen before typing, it is first centered.
- `Return` while in address-entry mode:
  - View: Disassembly.
  - Changes: exits address-entry mode.
- `Ctrl+S`:
  - View: Disassembly.
  - Changes: opens symbol lookup dialog.
  - On accepted symbol, cursor moves to the symbol address and the address is centered.
## Breakpoint interactions

- `F9`:
  - View: Disassembly.
  - Changes: toggles an execute breakpoint at `cursor_address`.
  - Edge cases: ignored while runtime is running. New breakpoints inherit the current disassembly bank/view flags and default counter values of 1. If a breakpoint already exists at that address, it is removed.
- Breakpoint rendering:
  - View: Disassembly.
  - Changes: execute breakpoints are highlighted in the line area.
  - Edge cases: read/write-only breakpoints are not highlighted in the disassembly.
- Misc/debugger breakpoint list `View PC` button:
  - View: Misc/debugger affects disassembly.
  - Changes: cursor moves to the breakpoint address and centers that address in the disassembly.

## Other debugger shortcuts handled by this view

- `F1`: opens help, remembers prior run state, pauses runtime. Pressing `F1` again while help is open closes help and resumes if it was previously running.
- `F2`: toggles debug view. With `Alt` or `Ctrl`, resets the machine; `Alt` selects the reset mode used by a2m.
- `F3`: cycles turbo mode unless `Ctrl` is held.
- `Ctrl+B`: assembles the configured source into memory.
- `Ctrl+Shift+B` or `Ctrl+Shift+F4`: opens assembler configuration.
- `Ctrl+E`: opens assembler error dialog.
- `Ctrl+P`: sets CPU PC to the disassembly cursor address.
- `F5`: runs. With `Ctrl`, starts host clipboard paste into the emulated machine.
- `F6`: run to cursor address if cursor address differs from CPU PC.
- `F10`: step over; ignored while an existing run-to-PC step-over is active.
- `F11`: step one opcode; with `Shift` while stopped, step out.
- `F12`: toggles monitor type. With `Shift`, toggles Franklin 80-column mode if installed.

## Recommended c64m design

- Keep the disassembly view in `frontend`.
- Frontend talks only to `runtime_client` for commands and copied snapshots.
- Runtime owns the live C64 machine, breakpoints, stepping state, and run/pause state.
- Frontend uses copied CPU snapshots for PC and copied memory snapshots for disassembly bytes.
- Disassembly decode must live in `tools`, for example a `tools/disasm_6502` module that accepts a byte source/snapshot and returns line records.
- The frontend can own view state: top address, cursor address, cursor row, follow-PC mode, selected bank/map, scroll state, and pending address-entry text.
- Breakpoint toggles should be runtime commands; rendering should use copied breakpoint snapshots or runtime events, never direct runtime internals.
