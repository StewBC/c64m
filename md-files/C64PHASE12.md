# Phase 12: Debugger UI Foundation

## Required reading

Before implementing Phase 12, read:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `md-files/a2m_cpu_register_view.md`
5. `md-files/a2m_memory_view.md`
6. `md-files/a2m_disassembly_view.md`
7. `md-files/a2m_misc_debugger_view.md`

Also inspect `md-files/a2m_png.png`.

The a2m files and image are behavioral references only. Do not copy a2m code structure. Preserve useful muscle memory and layout conventions where reasonable.

## Goal

Add the first c64m debugger UI pass.

The debugger should follow the same general layout style as the a2m screenshot:

* emulated video remains the main visual area
* CPU/register view is slim and about two lines tall
* disassembly view is a tall right-side view
* memory view is a bottom/side view
* misc/debugger view is a separate panel for status and breakpoint management
* exact layout may vary, but the functional grouping should remain recognizable

Phase 12 should implement the debugger as independent frontend views over runtime snapshots.

## Architecture rules

Do not violate these rules:

* frontend must not include or call machine directly
* frontend must talk to runtime through runtime_client
* runtime owns the live C64 machine
* runtime owns live CPU state, memory mutation, breakpoints, stepping, run/pause state, and stop reasons
* frontend receives copied snapshots only
* frontend may keep view state such as cursor address, selected row, edit buffers, selected bank, and scroll position
* disassembly logic belongs in tools, not machine and not runtime UI code
* tools must not own the machine
* platform may provide host services such as clipboard and input, but must not know C64 machine semantics

## Input and focus model

Implement this as a common debugger input layer before implementing individual views.

Preferred model:

* click-to-focus lock
* clicking inside a debugger view gives that view keyboard focus
* the focused view keeps focus even when the mouse moves away
* clicking another debugger view transfers focus
* clicking the emulated C64/video area clears debugger-view focus
* closing the debugger clears debugger-view focus

Fallback model, if click-to-focus is impractical with current Nuklear integration:

* hovered debugger view receives view-local keys
* if no debugger view is hovered, ordinary keys go to the C64 keyboard mapper

Global debugger keys:

* F9-F12 always go to the debugger/master control layer
* Option+key emulator controls always go to the debugger/master control layer
* these keys must not be consumed by memory/disassembly/register text editing
* existing master controls for toggle UI, run, pause/break, and step should remain authoritative

Ordinary keys:

* if a debugger view has focus, ordinary keys go to that view
* if no debugger view has focus, ordinary keys go to the C64
* examples:

  * focused memory view plus `a` over a hex nibble edits that nibble to `A`
  * no focused debugger view plus `a` types into the C64

Editing gates:

* register edits are allowed only while paused
* memory edits are allowed only while paused
* PC changes are allowed only while paused
* breakpoint edits are allowed only while paused unless an existing runtime-safe command already supports them while running
* while running, editable fields should render read-only or ignore edits

## Common runtime/debugger surface

Add only the runtime_client surface needed by the Phase 12 views.

Minimum needed snapshots:

* CPU snapshot:

  * PC
  * SP
  * A
  * X
  * Y
  * processor status
  * cycle count if already available
  * run/pause state
  * last stop reason if already available

* Memory snapshot:

  * requested address
  * selected view mode
  * copied bytes
  * response length
  * indication of CPU-map versus RAM/raw view

* Breakpoint snapshot:

  * execute breakpoint address list, minimum
  * enabled/disabled state if already supported
  * read/write watchpoints only if already supported

Minimum needed commands:

* request CPU snapshot
* request memory snapshot
* set PC
* set SP
* set A/X/Y
* set processor status
* write one memory byte
* write multiple memory bytes, optional but useful for paste
* toggle execute breakpoint at address
* run
* pause/break
* step instruction
* step over, only if already supported or small to add
* run to address, only if already supported or small to add

Do not add speculative debugger protocols for future devices, REU, cartridges, or full symbolic debugging.

## Common symbol surface

Symbols are expected later. Phase 12 should not require symbol loading to be complete, but should leave clean hooks.

Add a small frontend/tool-facing symbol resolver abstraction if one does not already exist:

* resolve address to optional label
* resolve label to optional address
* enumerate labels for a future lookup dialog

Initial implementation may return "not found" for all lookups.

Views that should call the resolver when available:

* disassembly labels and operands
* memory go-to symbol
* breakpoint list labels
* misc call-stack labels

Do not block Phase 12 on real symbol loading.

## Common C64 memory view modes

For Phase 12, keep bank viewing simple:

* CPU map: what the 6510 would read at that address
* RAM: raw underlying RAM

Optional only if already easy:

* ROM/I/O display labels for visible CPU-map regions

Do not implement REU, cartridge banking UI, or advanced expansion memory in Phase 12.

## View 1: CPU/register view

Implement this first.

Purpose:

* slim paused-state CPU view
* about two lines tall
* similar to a2m CPU view
* editable while paused
* read-only while running

Location:

* frontend debugger UI
* no machine dependency

Layout:

First row:

```text
PC FCE2  SP FF  A 7F  X 09  Y FF
```

Second row:

```text
N 0  V 1  - 1  B 0  D 0  I 1  Z 0  C 1
```

Formatting:

* `PC`: uppercase hex, four digits
* `SP`: uppercase hex, two digits
* `A`, `X`, `Y`: uppercase hex, two digits
* flags shown in order: `N V - B D I Z C`
* bit 5 may be displayed as `-`
* internally preserve the actual status byte bit layout
* flag values display as `0` or `1`

Editing behavior:

* when paused, fields are editable
* when running, fields are read-only
* register edit accepts hex only
* PC accepts 1 to 4 hex digits, commits as 16-bit
* SP/A/X/Y accept 1 to 2 hex digits, commits as 8-bit
* flag edit accepts only `0` or `1`
* Enter commits
* Escape cancels local edit and restores snapshot value
* invalid input leaves the runtime unchanged and restores the last snapshot value
* in-progress edit buffer must not be overwritten by new snapshots

Runtime behavior:

* commit sends a runtime_client command
* UI updates when the next copied CPU snapshot reflects the change
* frontend may show the pending value locally, but runtime remains source of truth

Acceptance criteria:

* CPU view renders from copied CPU snapshot
* no frontend-to-machine dependency is introduced
* PC/SP/A/X/Y display correctly
* SP displays as two hex digits, not `01FF`
* flags display as `N V - B D I Z C`
* paused register edits work
* running register edits do not mutate CPU state
* invalid edits do not mutate CPU state
* existing tests pass
* STATUS.md is updated

## View 2: Disassembly view

Implement after the register view.

Purpose:

* show decoded instructions around current PC or cursor address
* preserve a2m-style navigation muscle memory
* keep decode logic in tools
* render copied memory only

Location:

* frontend view
* disassembly decoder in tools
* runtime supplies CPU and memory snapshots

Core state owned by frontend:

* selected memory mode: CPU map or RAM
* cursor address
* cursor row
* top visible address
* decoded line table
* address-entry mode
* active address digit
* follow-PC behavior
* symbol display mode placeholder
* selected breakpoint highlight state from copied breakpoint snapshot

Display:

* window title: `Disassembly`
* one decoded instruction per row
* address at left
* bytes and mnemonic/operand after address
* current PC highlighted
* cursor row highlighted
* execute breakpoint rows highlighted
* forced-byte/invalid decode rows dimmed if needed
* footer includes selected memory mode

Decode:

* frontend requests copied memory bytes from runtime
* tools disassembler decodes from the copied bytes
* do not read machine memory directly
* do not put UI policy into tools

Follow PC:

* when debugger opens or runtime stops, center PC if possible
* if PC is already visible and line-aligned, shift visible lines so PC lands near middle
* if PC is not visible, regenerate around PC
* Right outside address-entry mode is manual follow-PC

Keyboard behavior:

* PageUp:

  * ignored in address-entry mode
  * previous top visible line becomes bottom visible line
  * fill upward above it
* PageDown:

  * ignored in address-entry mode
  * previous bottom visible line becomes top visible line
  * fill downward from it
* Home:

  * normal mode: cursor moves to first visible line
  * Ctrl+Home: jump to `$0000`
  * address-entry mode: active digit becomes 0
* End:

  * normal mode: cursor moves to last visible line
  * Ctrl+End: jump to `$FFFF`
  * address-entry mode: active digit becomes 3
* Up:

  * move to previous decoded line
  * if already at top, scroll one decoded line upward
* Down:

  * move to next decoded line
  * if already at bottom, scroll one decoded line downward
* Left:

  * address-entry mode: move active digit left
  * Ctrl+Left outside address mode: set PC to cursor address if paused
* Right:

  * address-entry mode: move active digit right; after digit 3, exit address-entry mode
  * normal mode: follow PC
  * Ctrl+Right: follow PC and set cursor to PC
* Ctrl+A:

  * toggle address-entry mode
* hex digit in address-entry mode:

  * replace selected nibble of cursor address
  * regenerate around cursor row
  * advance digit
* Return in address-entry mode:

  * exit address-entry mode
* Ctrl+S:

  * future symbol lookup; for Phase 12 may open placeholder or no-op
* Tab / Shift+Tab:

  * cycle symbol display mode placeholder
* F9:

  * global debugger layer toggles execute breakpoint at disassembly cursor address
  * ignored while running unless runtime already supports safe live breakpoint changes

Mouse behavior:

* left click row selects row
* left click address digits enters address-entry mode at that digit
* wheel scrolls decoded rows
* scrollbar may approximate by address, not exact decoded-line count

Acceptance criteria:

* disassembly renders from copied memory snapshots
* decoder lives in tools
* PC highlight works
* cursor highlight works
* PageUp/PageDown behavior matches a2m muscle memory
* basic address entry works
* F9 toggles execute breakpoint at cursor address
* no frontend-to-machine dependency is introduced
* existing tests pass
* STATUS.md is updated

## View 3: Memory view

Implement after disassembly.

Purpose:

* show copied C64 memory in hex plus ASCII
* allow paused memory edits
* preserve a2m-style hex/ASCII navigation where reasonable

Location:

* frontend view
* runtime supplies copied memory snapshots
* runtime owns writes

Core state owned by frontend:

* active subview index
* each subview:

  * view address
  * cursor address
  * selected memory mode: CPU map or RAM
  * visible rows
  * visible columns
  * edit field: hex, ASCII, or address
  * active nibble/digit
  * last find state placeholder
* split views may be deferred unless easy

Initial display:

```text
AAAA: XX XX XX XX XX XX XX XX  ........
```

Rules:

* uppercase four-digit row address
* hex bytes uppercase, two digits
* printable ASCII displayed as character
* non-printable as `.`
* address math wraps at 16 bits
* footer shows current cursor address and memory mode

Memory modes:

* CPU map
* RAM

Editing:

* allowed only while paused
* hex mode:

  * typing hex digit changes active nibble only
  * high nibble entry advances to low nibble
  * low nibble entry writes byte and advances to next byte high nibble
* ASCII mode:

  * typing printable character writes byte and advances
  * Return writes `$0D`
  * Backspace writes `$08`
* writes go through runtime_client
* frontend updates from resulting copied memory snapshot
* large paste operations should be bounded

Keyboard behavior:

* PageUp:

  * ignored in address-entry mode
  * subtract `cols * rows` from view address
  * cursor address does not move
* PageDown:

  * ignored in address-entry mode
  * add `cols * rows` to view address
  * cursor address does not move
* Home:

  * hex/ASCII mode: move cursor to column 0 of current row
  * Ctrl+Home: move cursor to top-left visible byte
  * address-entry mode: active address digit becomes 0
* End:

  * hex/ASCII mode: move cursor to last byte column of current row
  * Ctrl+End: move cursor to last byte of last visible row
  * address-entry mode: active address digit becomes 3
* Up:

  * subtract cols from cursor address
  * recenter only if cursor leaves visible area
* Down:

  * add cols to cursor address
  * recenter only if cursor leaves visible area
* Left:

  * hex high nibble: previous byte low nibble
  * hex low nibble: same byte high nibble
  * ASCII: previous byte
  * address-entry mode: previous address digit
* Right:

  * hex high nibble: same byte low nibble
  * hex low nibble: next byte high nibble
  * ASCII: next byte
  * address-entry mode: next address digit; after digit 3, leave address-entry mode
* Ctrl+A:

  * toggle address-entry mode
* Ctrl+F:

  * future find dialog; may be placeholder in Phase 12
* Ctrl+N / Ctrl+Shift+N:

  * future find next/previous; may be placeholder
* Ctrl+S:

  * future symbol lookup; may be placeholder
* Ctrl+T:

  * toggle hex/ASCII edit mode
* Shift+Insert:

  * paste host clipboard if platform support is already clean
  * otherwise defer
* Ctrl+V / Ctrl+J:

  * split/join memory subviews may be deferred unless easy

Mouse behavior:

* click address area enters address-entry mode
* click hex byte selects byte and hex nibble
* click ASCII column selects byte and ASCII mode
* wheel scrolls by configured line count times columns

Acceptance criteria:

* memory view renders copied memory snapshots
* CPU map and RAM modes work
* cursor movement works
* PageUp/PageDown preserve a2m behavior
* paused hex edit works
* paused ASCII edit works
* running edits do not mutate memory
* no frontend-to-machine dependency is introduced
* existing tests pass
* STATUS.md is updated

## View 4: Misc/debugger view

Implement last and keep it minimal.

Purpose:

* debugger status and breakpoint management
* C64-specific future home for hardware/debug panels
* do not copy Apple II device or soft-switch controls

Location:

* frontend view
* runtime supplies debugger snapshot
* runtime owns breakpoints and stepping state

Initial sections:

1. Debug Status
2. Breakpoints
3. Call Stack placeholder
4. C64 Hardware placeholder

Debug Status fields:

* run state: running or paused
* current PC
* last stop reason if available
* step cycles if available
* total cycles if available
* run-to-PC state if available
* step-out state if available

Breakpoints section:

* if none: `No breakpoints set`
* for each breakpoint:

  * access letters: X, R, W as supported
  * address as `$FFFF`
  * enabled/disabled state if available
  * optional hit counter if available
  * buttons:

    * View PC: centers disassembly on breakpoint address
    * Enable/Disable if supported
    * Clear
* Clear All if two or more breakpoints exist
* Edit dialog may be minimal or deferred

Call Stack:

* placeholder only unless runtime already exposes copied stack/debug info
* do not scan live stack memory
* if implemented, use copied stack bytes and tools/frontend display logic only

C64 Hardware:

* placeholder only
* do not map Apple II hardware controls
* future candidates: VIC-II, CIA, SID, memory banking, cartridge state

Keyboard behavior:

* misc view has no custom ordinary-key shortcuts in Phase 12
* buttons are mouse/UI driven
* global debugger keys still work

Acceptance criteria:

* misc view renders debugger status from copied runtime/debugger snapshot
* breakpoint list works
* View PC centers disassembly
* Clear breakpoint works
* no Apple II-specific controls are copied
* no frontend-to-machine dependency is introduced
* existing tests pass
* STATUS.md is updated

## Suggested implementation order

1. Common debugger focus/input routing
2. Runtime_client CPU snapshot and CPU mutation commands, if missing
3. CPU/register view
4. Runtime_client memory snapshot/write commands, if missing
5. Tools disassembler surface, if missing
6. Disassembly view
7. Execute breakpoint display/toggle
8. Memory view
9. Misc/debugger status and breakpoint list
10. STATUS.md update

## Non-goals for Phase 12

Do not implement:

* REU or memory expander UI
* full symbol loader
* full assembler integration
* source-level debugging
* Apple II soft switches
* Apple II hardware controls
* live frontend reads from machine
* direct SDL/Nuklear dependency in runtime or machine
* speculative debugger abstractions not used by these views
* cycle-perfect call stack reconstruction

## Final acceptance criteria

Phase 12 is complete when:

* debugger can be opened and closed
* CPU/register view works first
* disassembly view renders around PC
* memory view renders and edits while paused
* misc/debugger view shows status and breakpoints
* global debugger keys still work
* ordinary keys go to focused debugger view only when appropriate
* ordinary keys still go to C64 when no debugger view is focused
* frontend never reads live machine state
* runtime remains the only owner of the live C64 machine
* all existing tests pass
* new focused tests cover register formatting, focus routing, memory edit behavior, and disassembly navigation
* STATUS.md accurately describes Phase 12 completion
