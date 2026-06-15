# a2m misc/debugger view reference

This is a behavioral reference for Phase 12 planning only. Most behavior in this view is Apple II specific and should not be copied into c64m. The useful part for Phase 12 is the debugger status, call stack, and breakpoint management shape.

## Existing behavior, reference only

- Window title: `Miscellaneous`.
- The window is a scrollable tree/tab panel.
- Main sections include:
  - Hardware/device controls.
  - Debugger.
  - Soft switches.
  - Machine configuration and INI/file-browser dialogs.
- Modal dialogs disable input to the main misc window.

## Debugger section fields

- `Debug Status` group:
  - `Run to PC`: option indicator bound to runtime `run_to_pc`.
  - Run-to-PC address formatted as four uppercase hex digits.
  - `Step Out`: option indicator bound to runtime `run_step_out`.
  - `Step Cycles`: current CPU cycles minus previous stop cycles.
  - `Total Cycles`: current CPU cycle count.
- `Call Stack` group:
  - Scans Apple II stack memory looking for return addresses whose preceding opcode is `JSR`.
  - Each inferred entry shows:
    - Source address, formatted `%04X`.
    - Destination as `JSR %04X`, optionally followed by a symbol name.
  - Clicking the source address centers the disassembly view on the JSR address.
  - Clicking the destination centers the disassembly view on the call target.
  - The call stack group enables its own scrollbar only when enough entries exist; otherwise it disables the inner scrollbar so mouse wheel events scroll the parent panel.
- `Breakpoints` group:
  - If no breakpoints exist, shows `No breakpoints set`.
  - For each breakpoint, shows access letters:
    - `X` for execute.
    - `R` for read.
    - `W` for write.
  - Shows action text such as `Break`, `Fast`, `Restore`, `Slow`, `Swap sNdN`, `Troff`, `Tron`, or `Type`.
  - Shows address as `[%04X]`; range breakpoints use `[%04X-%03X]`.
  - Counter breakpoints show `(current/stop)`.
  - Disabled breakpoints are drawn purple and treated like bookmarks.
  - Buttons per breakpoint:
    - `Edit`: opens breakpoint edit dialog.
    - `Enable`/`Disable`: toggles disabled state and reapplies watch masks for non-execute breakpoints.
    - `View PC`: centers disassembly on the breakpoint address.
    - `Clear`: removes the breakpoint and reapplies masks.
  - `Clear All` appears when at least two breakpoints exist and removes all breakpoints.

## Misc view interactions and shortcuts

- The misc/debugger view itself does not define keyboard shortcuts in the inspected code.
- It is primarily mouse/button driven.
- Relevant cross-view button actions:
  - `View PC`:
    - View: Misc/debugger.
    - Changes: sets disassembly `cursor_address` to the breakpoint address, sets `cursor_line` to `rows / 2`, and regenerates the disassembly view around it.
  - Call stack source click:
    - View: Misc/debugger.
    - Changes: centers disassembly on the inferred JSR instruction address.
  - Call stack destination click:
    - View: Misc/debugger.
    - Changes: centers disassembly on the inferred call destination.
  - `Edit` breakpoint:
    - View: Misc/debugger.
    - Changes: copies the breakpoint into a dialog edit buffer, prepares address/counter/type text fields, and opens a modal dialog.
  - Breakpoint dialog accept:
    - View: Misc/debugger.
    - Changes: writes edited breakpoint fields back to the original breakpoint and reapplies breakpoint masks.

## Soft-switch and hardware behavior, reference only

- Hardware controls are Apple II device controls:
  - SmartPort and Disk II insert/eject/save/swap buttons.
  - Slot boot buttons reset and set PC to slot ROM addresses.
  - Mockingboard and display-card labels.
- Soft switches are Apple II display/memory switches:
  - `80STORE`, `RAMRD`, `RAMWRT`, `CXROM`, `ALTZP`, `C3ROM`, `80COL`, `ALTCHAR`, `TEXT`, `MIXED`, `PAGE2`, `HIRES`, `DHGR`, language-card flags.
  - Most are disabled indicators; display override enables some display-state controls.
- These concepts should not be mapped directly to C64. C64 equivalents, if needed later, would be CIA/VIC/SID/bank-state inspection panels.

## Scrolling behavior

- The Miscellaneous window uses Nuklear scroll auto-hide.
- It tracks whether its vertical scrollbar is being dragged by checking mouse-down state in the window scrollbar rectangle.
- Inner call-stack scrollbar is disabled when fewer than six inferred entries exist so parent scrolling still works naturally.
- No PageUp/PageDown/Home/End handling is implemented by this view.

## Recommended c64m design

- Keep debugger status and breakpoint management in `frontend`.
- Frontend renders from copied runtime/debugger snapshots:
  - run state,
  - current PC,
  - last stop reason,
  - cycle/instruction counters,
  - breakpoint list,
  - optional call stack or stack-byte snapshot.
- Frontend sends breakpoint edits, run-to-PC, step, pause, and clear commands through `runtime_client`.
- Runtime owns the live machine, breakpoint truth, stepping state, and stop reasons.
- The frontend should not scan live stack memory. If a call-stack view is wanted, request copied stack bytes and use frontend/tool logic to infer display-only call targets, or have runtime publish a copied debugger snapshot.
- Any disassembly shown from call-stack or breakpoint navigation should be generated in `tools` from copied memory snapshots.
