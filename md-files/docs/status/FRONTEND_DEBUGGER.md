# Frontend, debugger, and UI status

## Current implementation

- Debugger UI is complete through Phase 13, with subsequent breakpoint action enhancements.
- PAL and NTSC C64 frames use a common 352x248 display crop from frame Y=28
  (rows 28..275). NTSC's shorter frame is uploaded into a PAL-height frontend
  texture, using its initialized border-color padding rows so the same crop
  origin works for both standards.
- Configuration UI is complete through Phase 14.
- CPU/registers, memory, disassembly, misc/debugger tabs, execute/read/write breakpoints/watchpoints, counters/actions, and INI persistence are implemented.
- Call stack view is implemented in the Misc|Debugger tab.
- Hardware view is implemented in the Misc|Hardware tab.
- Memory/disassembly source modes are implemented. Memory views also support
  read-only `1541 Map 8` / `1541 Map 9` inspection modes.
- Memory view virtual views are implemented.
- Assembler UI integration is implemented.
- Machine Reset preserves the runtime state at the time the UI reset is requested:
  running machines resume after reset, while paused machines remain paused.
- Shift+Opt+A assembles the configured source file globally, using the same
  settings and action as the Misc|Assembler Assemble button.
- Shift+Opt+M toggles the live keyboard joystick layout between Numpad and WASD.
- Host file load/save UI is implemented.
- In-app file browser has keyboard navigation/type-ahead and remembers a default
  folder per browse slot (INI `[browse]`), editable on the Configure dialog's
  Paths tab; the `snapshot` slot doubles as the unified quicksave folder.
- The Configure dialog's Paths tab also edits the ROM file endpoints (INI
  `[roms]`): System, Kernal, Basic, Character, and 1541. A "Single Basic/Kernal
  ROM" checkbox (INI `[roms] single_system`) selects the combined System ROM and
  greys out the separate Basic/Kernal fields, or vice versa; Character and 1541
  are always editable. ROM edits apply on OK by rebooting and reloading the ROM
  set, and are persisted by both OK (full save) and "Save Paths Only". The
  Configure dialog's Emulator tab adds an "Emulate 1541" checkbox (INI
  `[disk] emulate_1541`) that applies live without a reboot.
- Help UI Phases 1 through 5 are implemented.
- Dialog modal input exclusivity is implemented.
- The OS window title reflects live runtime state without the debugger UI being open: `c64m - Running`, `c64m - Paused (<reason>)` (reason text reuses `frontend_stop_reason_name()`, e.g. `BRK`, `breakpoint`, `step`, `reset`), or `c64m - Error`. Updated only on state/reason change in `run_main_loop()` (`src/main.c`) via the new `platform_window_set_title()`. See `docs/status/CPU_MACHINE.md` for the BRK auto-stop behavior that feeds the `BRK` reason.
- Shared disk activity LEDs are drawn in the bottom-right corner of the application window (over display-only and debugger layouts). Green (read) is flush to the right and bottom edges; red (write) sits immediately to its left. Devices 8 and 9 share the same pair of LEDs. Machine side only counts discrete R/W events (`activity_read_seq` / `activity_write_seq`); the frontend arms a ~300 ms host-time hold on each sequence change so LEDs remain visible across frames and expire even when the machine is paused. LEDs are forced off on `RUNTIME_EVENT_RESET_COMPLETE` (machine counters cleared in `c64_reset`, frontend hold cleared via `frontend_clear_disk_activity_leds`). Event sources: KERNAL LOAD/SAVE traps, 1541 READ/WRITE/SEARCH/VERIFY/FORMT job intercepts, and media GCR byte-ready pulses (not continuous motor-on). Write hold suppresses green while red is lit. Embedded PNG assets live in `src/frontend/disk_led_data.{c,h}`.
- Breakpoint action parameters: Tron accepts an optional custom trace file path (empty = `trace.log`), Swap accepts a disk queue parameter (`+N` relative forward, `-N` relative backward, `N` absolute 1-based with wrap), Type accepts raw text in the input-encoding format parsed by `util/paste_parser`. Tron and Troff are mutually exclusive in the editor. INI and UI both persist and restore these parameters.
- Breakpoint counter repeat value: `reset=0` / Repeat `0` auto-disables the breakpoint after it fires once. `reset=1` (the default for new breakpoints) repeats on every subsequent hit. `reset=N` repeats every N hits. The editor label is "Repeat" (previously "Reset").
- Breakpoint Type action translator (`util/paste_parser`) is implemented. The parser converts stored text into `paste_event_t[]` events (up to 128) consumed by the runtime via `RUNTIME_COMMAND_PASTE_EVENTS` through `runtime_advance_paste_events()`. Supported syntax: literal printable chars (0x20–0x7E); named keys `\[KEYNAME]`, `\[KEYNAME+]` (assert), `\[KEYNAME-]` (deassert); wait tokens `\[W:N]` and `\[WAIT:N]` where `N` is a normal-keypress-duration multiplier and `0` is a no-op; PETSCII escapes `\xHH` (hex), `\dDDD` (decimal), `\oOOO` (octal); direct matrix address `\mR,C`; joystick events `\jPD[,B]`. Bare `SHIFT`, `CTRL`, `CBM`, and `RUNSTOP` tokens are one-shot modifiers released after the next non-modifier key/RESTORE event completes; explicit `+`/`-` holds override one-shot cleanup. Keys without a physical matrix position (F2/F4/F6/F8, cursor-up, cursor-left, PI) are encoded as `needs_shift` variants of their base key, and synthetic SHIFT cleanup preserves already-held one-shot or explicit SHIFT. OPT+SHIFT+INS clipboard paste routes through the same parser. Key aliases: `RT`=RETURN, `RE`=RESTORE (NMI), `RS`=RUNSTOP, `SH`=SHIFT, `CB`=CBM, `CT`=CTRL, `CH`=CLRHOME, `ID`=INSDEL, `SP`=SPACE, `PO`=POUND, `LA`=LEFTARROW, `UA`=UPARROW, `AS`=ASTERISK. RESTORE ignores `+`/`-` modifiers — `\[RE]`, `\[RE+]`, and `\[RE-]` all fire the NMI identically. RUN/STOP+RESTORE soft reset shorthand: `\[RS]\[RE]`.

## Runtime/frontend ownership

- Runtime owns machine state, breakpoints, watchpoints, stop reason, counters, and actions.
- Frontend renders copied snapshots only.
- Frontend sends intents/commands to runtime.
- Memory and disassembly views render from the same runtime-published full debugger memory snapshot generation. The snapshot includes 64K CPU-visible Map bytes, 64K raw RAM bytes, and 64K ROM-source bytes; write-history is included only for context-menu access lookup requests.
- Runtime step completion publishes a paused current-state debug frame after updating CPU/machine state, so the C64 view refreshes immediately during single-step debugging, including text screen RAM and high-res bitmap writes that occur long before the next live VIC-II frame completes. This path deliberately bypasses the last completed live VIC-II frame; otherwise a paused post-boot screen would keep redisplaying stale completed-frame pixels while step-time RAM writes were happening.
- Run-to-cursor (`Shift+F12`) uses the disassembly user cursor when present, otherwise the active PC highlight. The runtime implements it as a temporary breakpoint. If the target address is already the current PC, the temporary breakpoint ignores that current residency until the PC leaves the address, then stops on the next hit. This lets a user stand on a loop branch and repeatedly run one full loop iteration back to that branch.
- Register and memory edits apply only while paused.
- Running edits are ignored.
- Debugger input focus is explicit: C64 display versus debugger views.
- Symbol table is tools/frontend/debug-session-owned, separate from emulator machine and assembler internals.

## Host input: keyboard and joystick

- Host keyboard events map to the C64 keyboard matrix through `frontend_input`
  (`src/frontend/frontend_input.{c,h}`), dispatched from `handle_keyboard_input`
  in `src/main.c`.
- C64 joystick ports have two host sources, combined in the frontend and pushed
  through the single `runtime_client_set_joystick` choke point
  (`sdl_c64_controller_send_ports` in `src/main.c`):
  1. SDL game controllers. `Alt+1` / `Alt+2` assign/swap a connected controller
     to C64 port 1 / 2.
  2. The host keyboard, via `src/frontend/frontend_joystick_input.{c,h}`.
     `Alt+Shift+1` / `Alt+Shift+2` assign or toggle-off the keyboard joystick on
     port 1 / 2, and `Alt+Shift+0` disables it on whichever port it holds. A real
     controller and the keyboard can share a port (their masks are OR'd).
- Two config-selectable layouts:
  - `numpad`: `KP_8/2/4/6` cardinal, `KP_7/9/1/3` diagonal, `KP_0` fire. These
    keypad digits are not C64 keys, so they are consumed without stealing any
    C64 keystroke and need no mode toggle.
  - `wasd`: `W/A/S/D` + `Space` fire. These are C64 letter keys, so while the
    keyboard joystick is assigned to a port they are stolen from the C64
    keyboard; when unassigned (or when the debugger UI holds keyboard focus)
    they type normally.
- The joystick only consumes keys under the same focus condition as the C64
  keyboard (`!ui_visible || frontend_routes_keyboard_to_c64(ui)`).
- Both settings are also editable in the config dialog (Misc → Machine →
  Emulator → Configure... → Emulator tab): a tri-state port selector
  (Off / Port 1 / Port 2) and a Numpad/WASD layout selector. Applying the dialog
  updates the live keyboard-joystick source immediately (`dispatch_debugger_intents`
  re-runs `frontend_joystick_set_layout`/`set_port`). The layout can only be
  changed here; the port can be changed here or at runtime via `Alt+Shift+1/2` (or cleared via `Alt+Shift+0`).
- Persistence: `[input]` INI section keys `keyboard_joystick_layout`
  (`numpad`|`wasd`) and `keyboard_joystick_port` (`0`|`1`|`2`), plus CLI
  `--kbdjoy <0|1|2>` and `--kbdjoy-layout <numpad|wasd>`. Runtime port changes
  via `Alt+Shift+1/2` update the option (and re-seed the closed config dialog so
  it cannot later overwrite the choice) so they persist on quit. Dialog changes
  persist through the normal config-apply + INI-save path.
- Known limitation: numpad matching uses `keysym.sym`, so numpad directions
  depend on NumLock being on (consistent with how the rest of keyboard input is
  matched). WASD is unaffected.
- SDL text input is only enabled while a Nuklear text field actually holds
  focus. Each frame, after `frontend_render` builds the UI, `src/main.c`'s main
  loop calls `frontend_wants_text_input(ui)` (walks `ctx->begin` checking each
  window's `edit.active`) and toggles `SDL_StartTextInput`/`SDL_StopTextInput`
  only on change. Rationale: SDL enables text input by default, and on macOS an
  active text-input context turns a held key into the "press and hold" accent
  popup instead of key repeat — which fired when holding e.g. `s` for WASD
  joystick emulation. With text input off during normal gameplay/typing to the
  C64, the popup no longer appears; it re-enables automatically when a debugger
  dialog, assembler field, or help search box gains focus.

## Disassembly effective-address column

- Each disassembly row can carry a trailing annotation showing the resolved
  target address and, for data accesses, the current byte at that address, e.g.
  `LDA ($FB),Y   [$4050:25]` or `STA screen   [$0400:20]`.
- The column is computed entirely in the frontend
  (`frontend_disassembly_compute_target` in `src/frontend/frontend.c`) from the
  current `runtime_cpu_snapshot` registers (X/Y) and the CPU-visible Map memory
  snapshot (`mem_cache[RUNTIME_MEMORY_MODE_CPU_MAP]`), regardless of the row's
  active source mode, so the value reflects what the running CPU would actually
  read/write. Values are always read from the CPU Map.
- It is only rendered while the machine is paused. Register and zero-page
  pointer values are a coherent snapshot only when stopped; while running the
  column is suppressed.
- With decision (b), it is populated on every visible line using the current
  register snapshot, not just the PC line. For indexed/indirect modes this means
  non-PC rows show an "as-if-executed-now" target rather than the value those
  rows will see when execution reaches them.
- Shown for:
  - indexed/indirect modes: `zp,x`, `zp,y`, `abs,x`, `abs,y`, `(zp,x)`,
    `(zp),y` (all data accesses, show `[$addr:value]`);
  - `jmp (ind)` (control-flow target, shows `[$addr]`, replicates the 6502
    page-boundary wrap bug);
  - direct `abs`, branch, `jmp`, and `jsr` operands that were rendered as a
    label (data `abs` shows `[$addr:value]`; `jmp`/`jsr`/branch show `[$addr]`).
- Not shown for immediate/implied/accumulator modes, or for plain literal
  direct addresses (`lda #$FF`, `lda $4000`, `lda $fb`) where the address is
  already visible in the operand text. Zero-page direct operands are never
  label-substituted by the decoder, so they are never annotated.
- Addressing mode is obtained from the disassembler via the newly exposed
  `disasm_6502_opcode_mode()` / `disasm_6502_mode` in
  `src/tools/disasm_6502/disasm_6502.h`.

## Memory/disassembly source modes

The C64 source modes are independent per memory/disassembly view:

- Map: CPU-visible address space through `c64_debug_read_cpu_map`.
- ROM: physical ROM bytes through `c64_debug_read_rom`, regardless of current mapping.
- RAM: raw RAM through `c64_debug_read_ram`, regardless of ROM overlay.

Memory views also support read-only drive inspection modes:

- `1541 Map 8` and `1541 Map 9` show the selected drive's side-effect-safe
  address map from the runtime debug-memory snapshot.
- `$0000-$07FF` shows drive RAM, `$0800-$0FFF` shows the RAM mirror, `$1800-$1BFF`
  shows serial VIA registers, `$1C00-$1FFF` shows disk-controller VIA registers,
  and `$C000-$FFFF` shows ROM when that drive has a ROM loaded.
- Unmapped or unavailable drive addresses render as `--` in the hex column and
  blank ASCII cells.
- Drive map views are read-only. Hex/ASCII edit keystrokes are ignored and the
  footer reports `read-only`.
- The disassembly view remains C64 Map/ROM/RAM only in this phase; 1541
  disassembly, drive CPU register display, drive stepping, and drive breakpoints
  are deferred.

UI behavior:

- Right-click contextual popup selects source mode and shows active-mode dot
  indicator. Memory popups include `1541 Map 8` and `1541 Map 9`; disassembly
  popups do not.
- The memory/disassembly contextual popup groups options under `Source`,
  `View`, and `Access` headings. Memory view popups include `Split` and, when
  multiple virtual views exist, `Join`. While stopped both popups show four
  16-bit write-history lanes for the cursor address; selecting a lane navigates
  the disassembly cursor to that writer PC. The history comes from
  runtime-published snapshots; live machine pointers do not cross into
  frontend. The popups are clamped to the app viewport and use an internal
  scrollable region when the full menu cannot fit.
- Opt+M cycles source mode in the focused view. Memory cycles through
  Map/ROM/RAM/1541 Map 8/1541 Map 9; disassembly cycles through Map/ROM/RAM.
- Opt+Tab cycles active view C64 -> Disassembly -> Misc -> Memory.
- Shift+Opt+Tab reverses that order.
- ROM mode shows amber border inside the content area.
- RAM mode shows blue border inside the content area.
- 1541 Map 8/9 modes show a gray border inside the memory view content area.
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

- Configure dialog has three tabs — Machine, Emulator, Paths — supporting
  PAL/NTSC, display, turbo, symbol, and INI options.
- The Paths tab (`frontend_draw_config_paths_tab`) exposes the six `[browse]`
  default folders as editable, INI-relative fields, each with a `[...]` folder
  picker, plus a "Save Paths Only" button that rewrites just the `[browse]`
  section of the named INI (`app_options_save_paths_only`, via
  `FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY`); silent no-op with no INI. Fields
  bind directly to the live `ui->browse_dirs`, so edits take effect on the next
  browse immediately (not gated by OK/Cancel).
- The former Emulator-tab "Quicksave Folder" is gone: the quicksave/quickload
  shortcuts now use the `snapshot` browse slot, so one path serves both. The
  legacy `[state] quicksave_folder` key is migrated into `[browse] snapshot` on
  load and dropped on save.
- Runtime config apply is implemented.
- Video-standard changes reboot the runtime.
- Invalid breakpoint INI entries are skipped while valid entries load.

## Assembler UI

- Assembler tab, file picker, address/run address, auto-run, reset/run-to-BASIC assembly flow, assembler error event/dialog, and symbol snapshot handoff to disassembler are implemented.
- The Assembler source field displays the selected file relative to the INI
  directory; assembly and persistence resolve that display path back to the
  absolute source path.
- Reset C64 checkbox is above Assemble and defaults on (previously labelled "Reset").
- When Reset C64 is unchecked, assembly writes directly into live RAM in any execution state.
- If Auto Run is set with Reset C64 unchecked, the runtime jumps to run address and resumes running.
- Rearm one-shots checkbox (default off): when checked, any breakpoint with `reset_count == 0` that is currently disabled is re-enabled and its counter reset to `initial_count` before assembly runs. Designed for iterative development sessions where auto-disabled one-shot breakpoints need to fire again on the next run.
- The runtime path (`runtime_assembler.c`) predefines `C64MASM=0` so source can detect it is being assembled live in the emulator (vs. the `c64masm` CLI, which predefines `1`). It provides no `target_open` callback, so a named `.scope file="..."`/`dest="..."` redirect reports an error rather than writing files.

## Command-line assembler (c64masm)

- `src/tools/c64masm/` builds the standalone `c64masm` executable from the shared `assembler` library. Switches: `-i` input, `-o` output binary, `-a` origin address, `-s` symbol/segment listing (`-`=stdout), `-D name[=value]` predefined defines (repeatable), `-v` hex dump, `-h` help.
- It gives each output target a flat 64K image, tracks the actual written address extent per target, and writes exactly that range. Named `.scope file="..."` targets are written to their own files. `C64MASM` is predefined to `1`.
- Multi-target output rests on `CB_ASM_CTX.target_open`/`target_release`/`default_target` and per-`TARGET` `ctx`; `assembler_predefine()` seeds the build-flag define. See `ASMDESIGN.md` § "Output Targets and the Command-Line Tool".

## File browser dialog

- Every host "Browse..."/file-picker trigger opens a single in-app Nuklear
  modal (`frontend_draw_file_browser` / `frontend_open_file_browser`,
  `src/frontend/frontend.c`) instead of shelling out to an OS-native dialog.
  This replaces the previous macOS-only `osascript`/AppleScript `choose file`
  implementation (`choose_file_path`/`choose_save_path` in `src/main.c`, now
  removed) and gives Linux and Windows the same picker macOS had, with
  identical behavior on all three platforms.
- Directory listing is provided by `src/platform/platform_fs.{c,h}`
  (`platform_fs_list_dir`, `platform_fs_get_cwd`, `platform_fs_is_dir`,
  `platform_fs_path_join`): POSIX `opendir`/`readdir`/`stat`, Windows
  `FindFirstFileA`/`FindNextFileA`, matching this project's existing
  `discover_rom_path` (`src/app_options.c`) convention. It deliberately never
  calls `chdir()` — the dialog tracks its own current directory string instead
  of mutating the shared process CWD. Entries are sorted `..` first, then
  directories, then files, each group case-insensitive alphabetical.
- Dialog behavior: single-click selects a row; double-click (or the footer
  Open/Save button) on a directory navigates into it; on a file it commits
  (Open mode) or copies the name into the filename field (Save mode). The path
  row is directly editable and re-lists on Enter. Save mode auto-appends a
  purpose-specific default extension (e.g. `.c64state`) when missing, and
  requires a second Save click to overwrite an existing file
  (`frontend_file_browser_commit_save`).
- One dialog instance is reused for all triggers via a `purpose` field
  (reusing `frontend_debugger_intent_type`) plus an optional single
  `filter_extension` that hides non-matching regular files (directories always
  show): Load PRG/BAS dialog (no filter), Disk mount/add (`d64`), Config
  INI picker (`ini`), Config symbol-file picker (no filter), Assembler browse
  (no filter), Load/Save Bin browse (no filter), State Save As/Load
  (`c64state`, save mode auto-appends it), plus the Paths-tab folder picker
  (`CONFIG_PICK_PATH_DIALOG`).
- Keyboard support: type-ahead incremental search (case-insensitive prefix match
  with a 0.5 s sliding-window buffer that resets the search after a pause),
  Up/Down (line), PageUp/PageDown (page), Home/End (and macOS Cmd+Up/Down) to
  move the selection, and Enter to activate the highlighted entry. Type-ahead
  jumps anchor the selection ~1/4 down the list; key navigation scrolls the
  minimum needed to keep it visible. Scroll offset is computed in whole rows so
  the last row is never partially clipped. SDL text input is kept enabled while
  the dialog is open (`frontend_wants_text_input`).
- Folder-select mode (`pick_dir`, set for `CONFIG_PICK_PATH_DIALOG`): the footer
  button reads "Use This Folder" and commits the current directory instead of a
  file; used by the Paths tab's per-row `[...]` buttons, routed back via
  `frontend_set_picked_browse_dir`.
- Round trip: a `*_BROWSE`/`*_DIALOG` intent now only calls
  `frontend_open_file_browser`; committing the dialog pushes a new
  `FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT` intent carrying
  `file_browser_purpose` + `file_browser_path` (and `disk_device` for the two
  disk purposes). `src/main.c`'s intent switch handles that one new case with
  an inner switch on `file_browser_purpose`, running the same
  `runtime_client_*`/`app_disk_slot_*`/`frontend_set_*_path` logic each
  original case ran after its synchronous `choose_*_path` call used to return.
- Remembered default folders: six slots (`frontend_browse_slot`:
  `assembler`, `disk`, `program`, `basic`, `text`, `snapshot`) each remember the
  last-used directory. Load/Save-Binary map to `program`/`basic`/`text` by the
  dialog's Basic Program / Basic Text checkboxes; disk 8 & 9 share `disk`; state
  save/load share `snapshot`. On a committed selection the slot records that
  file's directory. `frontend_open_file_browser` seeds the browser from the slot
  (falling back to the shell cwd). Folders are stored **relative to the INI
  directory** (`app_options_path_relative_to_ini` /
  `app_options_path_absolute_from_ini`) and resolved to absolute on open; paths
  outside the INI tree fall back to absolute. `main.c` seeds the slots from
  `options.browse_dirs[]` at startup and pulls them back before saving; keys live
  in the INI `[browse]` section.

## Host load/save UI

- Machine tab layout is Disks, Programs, State, Emulator.
- Unified Load and Save buttons are on Machine tab.
- Load dialog has Name + Browse, From File address, Reset, Basic Program, and
  Basic Text checkboxes.
- Load dialog auto-detects `.CRT` paths and sends them through the runtime CRT
  cartridge load command instead of the raw binary loader.
- Save dialog has Name + Browse, Basic Program, Basic Text, Write address
  header, and Start/End range fields.
- Basic Program save reads `$2B/$2C` as start and `$2D/$2E` as exclusive end, and forces Write address header.
- Basic Text (Load) treats the file as ASCII stock BASIC V2 source, tokenizes it
  host-side via `util/basic_v2`, writes the tokenized image at `$0801`, and sets
  TXTTAB/VARTAB/ARYTAB/STREND (`$2B`-`$32`) to leave a clean post-CLR program.
  The From-File/Address controls are disabled in this mode.
- Basic Text (Save) detokenizes the live BASIC program (`$2B/$2C`..`$2D/$2E`)
  back to ASCII source via `util/basic_v2` and writes it with no PRG header;
  Start/End are disabled. Only stock BASIC V2 tokens are handled (extension
  dialects such as Simon's BASIC are out of scope). Basic Program and Basic Text
  are mutually exclusive in both dialogs.
- Non-printable PETSCII bytes (control codes, colour codes, cursor movement,
  CLR/HOME, reverse, `π`, graphics) are written as `{name}` escapes on save and
  translated back to the raw byte on load, so control codes embedded in string
  literals round-trip. Named codes include `{clr/home}` (`$93`), `{home}`
  (`$13`), `{down}`/`{up}`/`{left}`/`{right}`, `{rvon}`/`{rvoff}`, the colour
  names, and `{pi}` (`$FF`); load also accepts aliases (`{clr}`, `{clear}`,
  `{clr_home}`, `{rvson}`, `{rvsoff}`). Any unnamed byte uses a hex escape
  `{$hh}` (decimal `{147}` also accepted on load), so every byte is lossless; a
  literal `{` is therefore emitted as `{$7b}`. Escapes are honored in every
  context (strings, REM/DATA, and normal code); an unknown `{name}` is a load
  error. Implemented in `src/util/basic_v2.c`.
- State has Save As... and Load... buttons wired to runtime save/load state
  commands. Dropping a `.c64state` file loads it.
- `Opt+Shift+>` quicksaves to the `snapshot` browse folder (Configure → Paths,
  read live from the frontend) using a content/timestamp filename;
  `Opt+Shift+<` quickloads the newest `.c64state` there. Saved state files also
  restore the frontend keyboard-joystick layout/port when the optional host
  metadata chunk is present.

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
- Generator supports `--level N` to choose which Markdown heading level defines the help sections.
- Embedded C64 Pro Mono TrueType font is compiled in and used only for help view text at 10 px.
- Character-level hard-wrap fallback prevents unbroken tokens from overflowing.
- Code blocks route through the same inline wrap path as body text.
- Footer navigation bar: `[Prev]` / section-name index button / `[Next]` replace the old section-button grid. `[Prev]`/`[Next]` are inert (styled gray) at the document boundaries.
- Index button opens a floating pop-up window (separate Nuklear window, not clipped by the help overlay) listing all sections; clicking an entry navigates directly to it. Clicking outside or clicking the button again closes the pop-up.
- Footer search controls: `Search:` label, free-text edit box (fills remaining width, supports regular expressions via `external/tiny-regex-c`), `[<-]` (backward), `[->]` (forward). Enter in the edit box triggers a forward search. Case-insensitive (both pattern and text are lowercased before matching). Wraps at document boundaries in both directions. No-match state turns the edit-box text red; cleared on next edit. Search position tracks section navigation and resets to the top of the new section when Prev/Next/Index is used.
- `external/tiny-regex-c` (`re.h` / `re.c`, Unlicense) vendored as `c64m_tiny_regex` static library; linked by frontend only.

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

- Breakpoint Type action in single-step mode: elapsed-cycle timing per step is not integrated with the paste sequencer; Type fires correctly when the machine is running, not during cycle-accurate stepping.
- Breakpoint Swap action always targets device 8; per-device selection is deferred.
- Tron trace output always appends to the named file; rotation, size limits, and per-breakpoint handle management are deferred.

## Tests / smoke checks

- Verify memory/disassembly Map/ROM/RAM source modes independently per view.
- Verify memory `1541 Map 8` / `1541 Map 9` source modes render drive RAM/VIA/ROM
  bytes, show unmapped holes as `--`, and ignore hex/ASCII edits.
- Verify virtual memory views preserve per-view cursor, scroll, source mode, and edit state.
- Verify modal dialogs prevent base view focus changes from outside clicks.
- Verify assembler reset-on and reset-off flows.
- Verify host load/save paths, especially Basic Program TXTTAB/VARTAB behavior.
- Verify Basic Text load tokenizes ASCII source to `$0801` and Basic Text save
  detokenizes the live program back to ASCII (`basic_v2` unit test and the
  `runtime_fileio` round-trip test), and that Basic Program / Basic Text are
  mutually exclusive in both dialogs.
- Verify drag/drop and Machine Load for generic `.CRT`, including paths with
  spaces and parentheses.
- Verify `Opt+Shift+>` / `Opt+Shift+<`, Machine State Save As/Load,
  `.c64state` drag/drop, and keyboard-joystick layout/port restore after
  loading a saved state from a fresh launch.
- Verify symbol lookup opens from both Disassembly and Memory views (Opt+S).
- Verify search filters symbols with regex patterns; verify column header sorting.
- Verify DASM selection jumps cursor; verify Memory selection row-aligns view and places cursor.

## Files likely involved

- `src/frontend/*`
- `src/runtime/*`
- `src/tools/*`
- `src/platform/platform_fs.{c,h}` (file browser directory listing)
- `manual/manual.md`
- UI/debugger/config/help tests under `tests/`
