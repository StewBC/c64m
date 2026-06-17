# STATUS.md

## Current State

Completed through Phase 16 plus VIC-II Phase D (sprites display).

Implemented:

- 6510 CPU integrated through C64 bus.
- RAM, ROMs, banking, and address decoding.
- ROM loading and reset-vector boot path.
- Runtime thread and command/event model.
- Run, pause, reset, cycle-step, instruction-step.
- Frame pipeline with copied runtime-to-frontend handoff.
- VIC-II skeleton:
  - register storage
  - register mirroring
  - raster timing foundation
  - frame generation
  - border/background rendering
- Character display bring-up:
  - screen RAM fetch from $0400
  - character ROM glyph fetch
  - color RAM nibble storage and fetch
  - 40x25 text rendering into the active display area
- VIC-II review/implementation state after the Phase A/B/C planning review:
  - `$D019` IRQ status reads now model bit 7 as the enabled-pending VIC IRQ summary;
    bits 6:4 read as 1, and `$D01A` still reads with the high nibble set
  - raster IRQ status/enable is wired into the CPU IRQ pending callback
  - Bad Line detection exists for DEN-enabled visible lines matching YSCROLL
  - Bad Line c-access fetches populate the internal 40-byte video matrix/color line
    buffers during cycles 15-54
  - BA is asserted at cycle 12 on Bad Lines and released after the c-access window;
    this is still an approximation of RDY/AEC because CPU read-vs-write cycle
    discrimination is not implemented
  - VC/VCBASE/RC/display-state bookkeeping exists for Bad Line/text-row progression
  - PAL/NTSC raster line and frame counts are selected from machine configuration
  - snapshot rendering now applies PAL border-window geometry with RSEL/CSEL:
    25/24 row vertical clamps and 40/38 column horizontal clamps
  - snapshot rendering applies XSCROLL/YSCROLL as delayed display-window edges:
    pixels before the scroll offset show background, then content begins from row/col 0
  - snapshot rendering supports the VIC-II graphics-mode dispatch for ECM/BMM/MCM:
    standard text, multicolor text, standard bitmap, multicolor bitmap, ECM text,
    and invalid modes 5/6/7 as black background/display-layer pixels
  - standard bitmap mode uses bitmap data from `$D018` bit 3 and colors from the
    video matrix byte; color RAM is not used for standard bitmap colors
  - multicolor bitmap and multicolor text color selection paths are implemented in
    the snapshot renderer
  - invalid graphics modes black the background/display layer while leaving border
    rendering unaffected
  - mode/scroll changes are reflected on the next snapshot render; per-cycle
    mid-frame mode switching is not implemented
  - frontend display presentation crops the internal 384x272 frame to a balanced
    352x240 view for both display-only and debugger-pane rendering, leaving the
    internal frame/raster geometry unchanged
- CIA foundations:
  - CIA #1 and CIA #2 machine-owned devices
  - `$DC00-$DCFF` and `$DD00-$DDFF` bus routing
  - register storage and mirroring
  - timer A/B latch, counter, and underflow foundations
  - interrupt mask/flag foundations
  - CIA #1 IRQ pending callback path
  - CIA #2 NMI pending foundation
  - deterministic no-key keyboard matrix reads
- ROM boot progression:
  - machine/runtime boot checkpoint counters
  - IRQ vector entry validation through the machine bus
  - IRQ stack push validation
  - ROM-driven screen RAM writes reflected in frames
  - ROM-driven color RAM writes reflected in frames
  - VIC-II `$D018` screen/character pointer support
  - real 64C ROM smoke checkpoint reaches VIC/CIA/screen activity
- Keyboard Pass 1 plumbing:
  - machine-owned C64 keyboard matrix
  - key press/release state
  - CIA #1 keyboard scan reads through `$DC00/$DC01`
  - runtime copied key down/up commands
  - SDL key mapping for letters, digits, space, return, delete, shift, and common BASIC punctuation keys
  - semantic host cursor arrows:
    - right/down map to C64 cursor keys
    - left/up synthesize Shift + C64 cursor keys
  - ESC maps to C64 RUN/STOP
  - Backspace maps to C64 DEL
  - host Delete maps to RESTORE
- Keyboard Pass 2 / Phase 11 BASIC typing polish:
  - SDL-to-C64 key translation moved out of `main.c` into frontend-owned input mapping
  - runtime still receives copied project-level keyboard/RESTORE commands, not SDL events
  - default mapping is semantic host typing rather than physical C64 key layout
  - focused frontend mapper regression tests cover shifted punctuation, remembered synthetic releases, cursor keys, CONTROL, Commodore, and RESTORE
  - C64 CONTROL is mapped from host Control
  - C64 Commodore is mapped from host Tab
  - emulator controls use Option+R run, Option+S step, and Option+P pause
  - F10/F11/F12 remain available for run/step/pause
  - host quote/double-quote, colon, plus, parentheses, asterisk, @, cursor arrows, HOME/CLR HOME, RUN/STOP, RESTORE, left-arrow, and up-arrow have semantic mappings
  - Shift+letter preserves the C64 left graphics character set
  - Tab+letter provides the C64 Commodore graphics character set
  - manual BASIC validation transcript added for:
    - `10 PRINT "HELLO"`
    - `20 GOTO 10`
    - `RUN`
- IRQ/CIA boot compatibility:
  - CIA #1 ICR read/write diagnostics
  - CIA interrupt assertion diagnostics
  - CPU IRQ entry diagnostics
- CPU NMI entry path for RESTORE
  - CIA zero-latch timer reload behavior
  - CIA one-shot timer stop behavior
  - normal runtime RUN pacing at roughly 60 Hz frame cadence
- App startup:
  - reset screen starts clear
  - frontend queues Run automatically after initialization
- SDL display of machine-generated frames.
- Phase 12 debugger UI foundation is complete:
  - CPU/register, disassembly, memory, misc/debugger, breakpoint list, debugger input routing, and runtime snapshot/command plumbing are implemented
- Phase 12 debugger UI foundation, View 1:
  - slim CPU/register view renders from copied runtime CPU snapshots
  - PC, SP, A, X, Y, and `N V - B D I Z C` flags display in fixed-width uppercase/readable form
  - paused register/status edits emit frontend debugger intents
  - `main.c` translates debugger intents into runtime_client commands
  - runtime owns and applies CPU register mutations only while paused
  - running register mutations are ignored by runtime
  - regression coverage validates paused CPU register setters and running-state rejection
- Phase 12 debugger UI foundation, View 3:
  - memory view renders copied runtime memory snapshots in compact uppercase 16-byte hex rows plus ASCII
  - CPU-map and raw RAM memory modes are supported
  - CPU-map snapshots use debugger-safe peeks that do not perform side-effecting CIA reads
  - cursor movement, PageUp/PageDown, Home/End, and hex/ASCII/address modes are wired
  - custom visible cursor is drawn for address, hex-nibble, and ASCII edit positions while paused
  - custom scrollbar thumb can be dragged across the 64K address space
  - paused hex and ASCII byte edits emit frontend debugger intents and accept key repeat
  - runtime owns and applies memory writes only while paused
  - running memory writes are ignored by runtime
  - regression coverage validates memory snapshots, paused writes, and running-state rejection
- Phase 12 debugger UI foundation, View 2:
  - disassembly view renders compact decoded 6502 lines from copied runtime memory snapshots
  - decoder lives in `src/tools/disasm_6502` and does not own or read machine state
  - CPU-map and raw RAM disassembly modes are supported
  - current PC rows are highlighted and drive the view while running or stepping
  - transient user cursor is created only by paused navigation, is cleared by run, and preserves its address while off-screen
  - PageUp/PageDown, Home/End, Up/Down, follow-PC, scrollbar, and basic address-entry navigation are wired
  - running debugger refreshes CPU/machine snapshots at frame cadence
  - symbol resolver hooks exist and currently default to not found
  - breakpoint rendering/toggling uses runtime-owned execute breakpoint snapshots
  - regression coverage validates core decoder formatting and symbol lookup behavior
- Phase 12 debugger UI foundation, View 4 breakpoint pass:
  - misc/debugger view is now organized as scrollable tabs: Programs, Debugger, Breakpoints, and Hardware
  - Programs tab can select a `.prg` file and send it to runtime for direct RAM loading at the PRG load address
  - D64 disk mounting and CRT cartridge loading remain deferred
  - runtime owns execute breakpoints with stable IDs, enabled/disabled state, and hit counters
  - runtime owns and publishes a copied stop reason in machine snapshots
  - runtime_client supports set, clear, clear-all, enable/disable, and snapshot request commands
  - runtime checks enabled execute breakpoints while running, stepping, and bounded-running, then pauses and publishes copied state
  - frontend renders breakpoint snapshots only, with disabled breakpoints kept visible as bookmarks
  - disassembly View 2 shows copied breakpoint snapshots in the gutter and Option+B toggles an execute breakpoint at the cursor while paused
  - misc/debugger view shows debug status, stop reason, cycle/frame counters, breakpoint rows, View PC, Enable/Disable, Clear, and conditional Clear All
  - Phase 12 execute-only breakpoint behavior is retained as the quick-toggle path
- Phase 13 breakpoint/watchpoint system:
  - runtime breakpoint model supports stable runtime IDs, duplicate addresses/ranges, enabled state, start/end address ranges, access masks, mapping filters, action masks, hit counts, and counters
  - runtime_client supports create, update, duplicate, clear, clear-all, enable/disable, and copied breakpoint snapshot commands
  - runtime evaluates execute/read/write breakpoints and watchpoints, including inclusive ranges and Map/ROM/RAM filters
  - machine reports generic CPU memory access events to runtime; machine does not know debugger UI concepts
  - runtime uses machine-side visibility decoding for ROM/RAM filters, with IO matching Map only
  - counters are runtime-owned; count zero triggers immediately, reset zero triggers every later match, and disabled breakpoints do not decrement counters
  - runtime action framework supports Break, Fast, Slow, Tron, Troff, Type, and Swap action masks
  - Break pauses before later state-changing actions; non-Break actions do not pause
  - Fast switches runtime pacing to maximum turbo mode; Slow restores normal paced mode
  - Tron/Troff update runtime trace state; Type and Swap are Phase 13 no-ops pending later implementation
  - `[DEBUG]` INI persistence loads and saves `break.<address>` entries, supports duplicate suffixes such as `.1` and `.2`, and skips invalid entries while loading remaining valid breakpoints
  - breakpoint editor modal supports create, edit, duplicate, access checkboxes, start/end range, mapping selection, counters, action checkboxes, validation, cancel, and apply
  - frontend renders copied breakpoint snapshots only and sends edits through runtime_client
- Debugger input routing:
  - C64 display input is the initial/default focus, including the first time the debugger UI is opened
  - clicking the C64 display while the debugger UI is visible returns ordinary key input to the emulated C64
  - clicking debugger views returns ordinary key input to the focused debugger view, and that choice survives UI hide/show toggles
- Phase 14 INI configuration system and configure dialog:
  - Misc view Programs tab has been renamed to Machine and now opens a Configure dialog
  - Configure dialog owns original and edited configuration copies; edits are temporary until OK and Cancel discards them
  - Machine tab exposes video standard, display width/height, integer scaling, aspect correction, and filter settings
  - Emulator tab exposes scroll wheel speed, turbo speeds, disk LED visibility, symbol file paths, and persistent auto-save
  - global INI file path editing and native picker flow are wired, including existing-file Yes/No/Cancel parse behavior
  - changing the INI filename automatically enables the one-shot Save INI on Quit flag when saving is allowed
  - `--nosaveini` disables save controls; `--noini` skips startup INI loading without disabling later saves
  - OK applies immediate frontend/app settings, sends copied machine config through runtime_client, and reboots on PAL/NTSC changes
  - VIC-II timing now supports NTSC and PAL line/frame timing selected from machine configuration
  - `[config]` INI keys now persist scroll wheel speed, turbo speeds, symbol files, and `Save=yes`
  - turbo speed CSV is parsed into runtime-owned available multipliers; the first entry becomes the active paced multiplier
  - symbol file changes currently trigger view refresh plumbing only; real symbol unload/load remains future work
- Phase 16 timed bus event and live VIC-II raster foundation:
  - machine owns a monotonic master cycle and advances VIC-II/CIA/SID hooks to
    timestamped CPU bus event cycles before applying CPU-visible side effects
  - CPU instruction stepping remains the external runtime/debugger API, while
    bus-visible reads/writes are classified and timestamped within each opcode
  - CPU writes are not applied twice: deferred/timed paths record writes first and
    apply RAM/I/O/device side effects only when machine time reaches the event
  - VIC-visible writes to registers such as `$D020` take effect at their event cycle
    rather than only after opcode completion or at whole-frame snapshot time
  - runtime running-frame publication now copies completed live VIC-II frame buffers;
    the old whole-frame snapshot renderer remains only as a compatibility/debug path
    before a live frame has completed
  - live raster rendering emits border/background and current Phase C display pixels
    as VIC time advances, including standard text, multicolor text, standard bitmap,
    multicolor bitmap, ECM text, and invalid modes 5/6/7
  - mid-frame border color changes are visible in completed/published frame output;
    regression coverage proves a timed `$D020` write changes only later border pixels
  - current Bad Line BA handling now routes through CPU event read/write
    classification: read cycles stall while BA is low, write-only cycles continue,
    and unknown/internal cycles remain conservatively stalled
- VIC-II Phase D — Sprites Display:
  - all 8 hardware sprites can be independently positioned and displayed
  - 9-bit X coordinate (`$D000`/`$D002`…`$D00E` + MSB register `$D010`) and 8-bit Y
    coordinate (`$D001`/`$D003`…`$D00F`) with per-sprite enable via `$D015`
  - hires mode (1 bit/pixel, 24×21): sprite color from `$D027`–`$D02E`
  - X-expand (`$D01D`): doubles sprite width to 48 pixels
  - Y-expand (`$D017`): doubles sprite height to 42 raster lines via per-sprite
    flip-flop that gates `mc` advance on alternate lines
  - multicolor mode (`$D01C`): 2 bits per logical pixel pair — transparent / MM0
    (`$D025`) / sprite color / MM1 (`$D026`); combined X+Y expand works correctly
  - sprite pointer (p-access) fetched from `vic_bank + screen_base + $03F8 + n`;
    sprite data (s-access) fetched as 3 bytes from `vic_bank + pointer × 64 + mc`
  - VIC bank base derived from CIA 2 port A bits 1–0 (inverted) via
    `c64_bus_vic_bank_base()`; default bank is `$0000`
  - `vicii_fetch_sprites()` called at cycle 0 of each raster line; sets
    `sprite_visible[n]` and fills `sprite_data[n][3]` for the live renderer
  - sprite overlay composited in `vicii_live_pixel()` after background pixel
    computation; sprite 0 has highest priority; sprites are hidden behind the border
  - `vicii_snapshot_sprite_line()` computes sprite row data statically per raster
    line for the snapshot renderer; overlay loop mirrors live-pixel compositing
  - horizontal wraparound (modulo 512) handled by `vicii_sprite_dx_wrapped()`
  - sprites above background in all 5 valid graphics modes (priority via `$D01B`
    is Phase E)
  - regression test `test_sprite_hires_appears_at_position` confirms yellow pixels
    appear at the correct frame coordinates for a fully-opaque hires sprite

## Not Implemented

- VIC-II remaining accuracy/features:
  - sprite-background priority (`$D01B`) and sprite collision detection (`$D01E`/`$D01F`)
    are not implemented (Phase E)
  - light pen is not implemented
  - open-bus / last-byte-on-bus behavior is not implemented
  - exact BA/AEC/RDY cycle stealing is not implemented; current Bad Line BA handling
    distinguishes CPU read and write event kinds, but sprite-fetch BA events and
    exact AEC/RDY timing remain deferred
  - sprite fetch BA events are not implemented
  - idle-state g-access fetch behavior from `$3FFF` / `$39FF` is not modeled in the
    renderer
- Phase 13 deferred breakpoint action details:
  - Type text injection is not implemented yet
  - Swap disk behavior is not implemented yet
  - Trace output/details are not implemented yet
- Full CIA accuracy.
- SID.
- Cycle-perfect video/audio timing.

## Current Runtime Notes

Real 64C ROM execution reaches the BASIC READY prompt with a visible cursor and keyboard input.

After a 1,000,000-cycle smoke trace:

```text
PC=$FD7E
CIA #1 IRQ pending = true
CPU IRQ entries = 0
CIA #1 ICR reads = 0
```

The pending CIA #1 IRQ is not currently observed as a CPU IRQ entry because the CPU interrupt-disable flag remains set during the trace.

## Phase 12 UI Notes

- Compact debugger text views should use the small Nuklear default font currently installed in `frontend_create` and row heights based on `ctx->style.font->height`, not oversized fixed row heights.
- For dense text views, temporarily zero Nuklear window padding, spacing, and group padding inside the view body, then restore the saved style before leaving the window.
- Custom scrollbars should live in their own layout column/group, not as an overlay drawn on top of the text area or near splitter hit zones.
- Scrollbar drag state must be exclusive with layout splitter drag state. `frontend_render` suppresses layout dragging while the memory scrollbar owns the mouse.
- Scrollbar thumb dragging should start only from the thumb, track grab offset, and map thumb movement back to the debugger view address. Track clicks may page-jump separately.
- Avoid `nk_input_has_mouse_click_in_rect` for row selection in custom text views; it can keep matching the original click position while mouse state is tracked. Use a press-edge plus current hover test instead.
- Artificial cursors for debugger text views should be drawn on the window canvas at computed font-cell coordinates. Do not rely on Nuklear edit widgets for hex dump/disassembly cursors.
- Editable debugger cursors should be hidden while the runtime is running if edits are gated to paused state.
- Repeated keydown events are allowed for focused debugger text editing, while global emulator controls and Option/F-key commands remain owned by the master input layer.
