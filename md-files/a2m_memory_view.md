# a2m memory view reference

This is a behavioral reference for Phase 12 planning only. Do not reuse the a2m implementation structure: it reads and writes live memory from UI code. In c64m, memory display and editing must go through `runtime_client` requests/commands and copied snapshots.

## Address layout

- Window title: `Memory`.
- The memory view can be split into up to 16 stacked subviews.
- Each subview has:
  - `view_address`: address at the top-left byte of that subview.
  - `cursor_address`: selected byte address, which may be off-screen.
  - `rows`: visible rows allocated to that subview.
  - `flags`: memory/bank selection.
- Each rendered row has:
  - Four uppercase address hex digits.
  - A colon.
  - Hex byte cells.
  - ASCII cells.
- Row format is effectively:
  - `AAAA:` followed by `XX ` for each visible byte column, followed by one ASCII character per byte.
- The number of byte columns is calculated from view width:
  - Uses monospaced font width.
  - Leaves 5 characters for `AAAA:`.
  - Each byte consumes 4 display columns overall: three hex characters plus one ASCII character.
- Addresses wrap as 16-bit values.

## ASCII column behavior

- Printable bytes are shown as their host printable character.
- Non-printable bytes are shown as `.`.
- Clicking the ASCII column selects ASCII edit mode and sets `cursor_address` to that byte.
- Typing printable SDL key mappings in ASCII edit mode writes that byte and advances the cursor one byte.
- `Return` maps to byte `$0D`; `Backspace` maps to byte `$08`; common US punctuation is translated with Shift/Caps handling.

## Bank/map selection behavior

- Each memory subview has independent memory selection flags.
- Footer displays current cursor address as `Address: %04X`.
- Apple II selectors:
  - `RAM: Map/Main/Aux`.
  - `C100: Map/ROM`.
  - `D000: Map/LC1/LC2/ROM`.
- Apple II+ disables unavailable selectors:
  - RAM only `Map` and `Main`.
  - C100 only `Map`.
- Clicking a selector changes the flags for the active subview.
- The disassembly view uses the same selector widget, but without the `Address:` label.
- For c64m, this should translate conceptually to C64 bank/map modes such as CPU-visible map, RAM, BASIC ROM, KERNAL ROM, character ROM, I/O, color RAM, and cartridge areas as appropriate.

## Scrolling behavior

- Cursor movement recenters the view only when the cursor leaves the visible area.
- Recenter behavior:
  - If cursor moves above the first visible row, `view_address` becomes the cursor row's column-zero address.
  - If cursor moves below the last visible row, `view_address` becomes the address that places the cursor on the last row in the same column.
- Mouse wheel:
  - View: Memory.
  - Changes: moves the active subview `view_address` by `wheel_delta * scroll_wheel_lines * cols`.
  - Edge cases: address math wraps at 16 bits; negative modulo is corrected back into `0..65535`.
- Scrollbar thumb drag:
  - View: Memory.
  - Changes: maps thumb position directly to the active subview top `view_address`.
  - Edge cases: total range is `0x10000`; visible range is `rows * cols`.
- Scrollbar track click:
  - View: Memory.
  - Changes: jumps top address by one visible subview page toward the click, wrapping modulo the total range.

## PageUp/PageDown/Home/End behavior

- `PageUp`:
  - View: Memory.
  - Changes: subtracts `cols * rows` from `view_address`.
  - Edge cases: ignored in address-entry mode. Cursor address does not move, so the cursor may become off-screen. Address wraps at 16 bits.
- `PageDown`:
  - View: Memory.
  - Changes: adds `cols * rows` to `view_address`.
  - Edge cases: ignored in address-entry mode. Cursor address does not move, so the cursor may become off-screen. Address wraps at 16 bits.
- `Home`:
  - View: Memory.
  - Changes: in hex/ASCII mode, moves cursor to column 0 of the current row and sets active digit to 0.
  - With `Ctrl`: moves cursor to the top-left byte of the visible subview.
  - In address-entry mode: moves active address digit to digit 0.
- `End`:
  - View: Memory.
  - Changes: in hex/ASCII mode, moves cursor to the last byte column of the current row and sets active digit to 0.
  - With `Ctrl`: first moves cursor to the first byte of the last visible row, then to the last column of that row.
  - In address-entry mode: moves active address digit to digit 3.

## Cursor behavior

- `Up`:
  - View: Memory.
  - Changes: subtracts `cols` from `cursor_address`.
  - Edge cases: ignored in address-entry mode; recenters if cursor leaves visible area; wraps at 16 bits.
- `Down`:
  - View: Memory.
  - Changes: adds `cols` to `cursor_address`.
  - Edge cases: ignored in address-entry mode; recenters if cursor leaves visible area; wraps at 16 bits.
- `Left`:
  - View: Memory.
  - In hex mode: if on high nibble, moves to previous byte low nibble; if on low nibble, moves to high nibble of same byte.
  - In ASCII mode: moves to previous byte.
  - In address-entry mode: moves active digit left unless already at digit 0.
  - Edge cases: hex/ASCII movement recenters and wraps; address-entry movement does not recenter.
- `Right`:
  - View: Memory.
  - In hex mode: if on low nibble, moves to next byte high nibble; if on high nibble, moves to low nibble of same byte.
  - In ASCII mode: moves to next byte.
  - In address-entry mode: moves active digit right; after digit 3 it exits address-entry mode and restores the previous hex/ASCII field and digit.
  - Edge cases: hex/ASCII movement recenters and wraps; address-entry movement does not recenter.
- Mouse left click:
  - View: Memory.
  - Changes: makes clicked subview active, then selects address, hex, or ASCII field based on x position.
  - Edge cases: clicking first four columns enters address-entry mode; clicking hex or ASCII exits address-entry mode if needed. Right-click address is recorded for the context menu only when a byte column is clicked.
- `Alt+Down`:
  - View: Memory.
  - Changes: selects next split memory subview, wrapping to zero.
- `Alt+Up`:
  - View: Memory.
  - Changes: selects previous split memory subview.
  - Edge cases: a2m uses unsigned active index with modulo; c64m should implement this carefully to avoid unsigned underflow surprises.

## Address entry and search behavior

- `Ctrl+A`:
  - View: Memory.
  - Changes: toggles address-entry mode.
  - Entering address mode remembers the previous edit field/digit and starts at address digit 0.
  - Leaving restores the previous hex/ASCII field/digit.
- `Ctrl+Shift+A`:
  - View: Memory.
  - Changes: adjusts `view_address` by the cursor column, effectively making the cursor's current row line up as column zero for the displayed address.
  - Edge cases: the code adds the cursor column to `view_address`; this is odd behavior and should be treated as a2m-specific reference only.
- Hex digits `0`-`9` and `a`-`f` in address-entry mode:
  - View: Memory.
  - Changes: replace the selected nibble of the row base address, preserve the cursor column, recompute both `cursor_address` and `view_address`, then advance the address digit.
- `Ctrl+F`:
  - View: Memory.
  - Changes: opens find dialog, clears current search text, and makes the dialog modal.
  - Dialog options can interpret search text as raw text or as hex bytes, and can enable case-insensitive search.
  - On accept, search starts after the current cursor address.
- `Ctrl+N`:
  - View: Memory.
  - Changes: find next from `last_found_address + 1`.
  - On match, sets `cursor_address`, `view_address`, and `last_found_address` to the match address.
  - Edge cases: searches wrap through up to 65536 bytes.
- `Ctrl+Shift+N`:
  - View: Memory.
  - Changes: find previous from `last_found_address - 1`.
  - On match, sets `cursor_address`, `view_address`, and `last_found_address` to the match address.
- `Ctrl+S`:
  - View: Memory.
  - Changes: opens symbol lookup dialog.
  - On accepted symbol, sets both `view_address` and `cursor_address` to the symbol address.

## Editing behavior

- Hex edit mode:
  - Typing a hex digit changes only the active nibble of the selected byte.
  - After high-nibble entry, cursor advances to low nibble of the same byte.
  - After low-nibble entry, cursor advances to high nibble of the next byte.
- ASCII edit mode:
  - Typing a mapped character writes that byte and advances to the next byte.
- `Ctrl+T`:
  - View: Memory.
  - Changes: toggles between ASCII and hex edit fields.
  - Edge cases: a2m computes this as `CURSOR_ASCII - cursor_field`, relying on enum values; c64m should use explicit state transitions.
- `Shift+Insert`:
  - View: Memory.
  - Changes: pastes host clipboard text.
  - In ASCII mode: writes each character byte and advances.
  - In hex/address mode: lowercases each pasted character and feeds it through hex/address entry.
  - Edge cases: clipboard text is consumed until NUL; c64m should bound large paste operations and send batched runtime write commands.
- `Ctrl+V`:
  - View: Memory.
  - Changes: splits the memory view if fewer than 16 subviews exist.
  - New view starts at the current cursor address, inherits flags, then becomes active.
  - Edge cases: after array reallocation a2m re-fetches the active view pointer. The original view cursor is moved to its last visible row before switching active view.
- `Ctrl+J`:
  - View: Memory.
  - Changes: joins/removes the active split view if more than one exists.
  - Edge cases: if the active index was after the last remaining item, it decrements; rows are recalculated.
- `Ctrl+H`:
  - View: Memory.
  - Changes: reserved/commented as find and replace, no behavior.
- `Escape`:
  - View: Memory.
  - Changes: closes the right-click menu if it is open.

## Right-click context behavior

- Right-clicking a byte opens a context menu.
- The menu shows up to four last-write addresses associated with that byte, formatted as `$%04X`.
- Selecting one moves the disassembly cursor to that address and centers it.
- This is tightly coupled to a2m memory instrumentation and should be reference only.

## Recommended c64m design

- Keep the memory view in `frontend`.
- Frontend talks to `runtime_client` for memory snapshots and write commands.
- Runtime owns the live C64 machine and all memory mutation.
- The frontend must render copied memory snapshots only. Editing should enqueue explicit write requests, then update display when runtime publishes the resulting snapshot or acknowledgement.
- Bank/map selection should be frontend view state included in snapshot requests, not direct machine access.
- Memory decoding and disassembly cross-links should use `tools` disassembly on copied bytes.
- Avoid a2m's live last-write context menu for Phase 12 unless runtime already exposes copied write-history data.
