# Testing and smoke status

## Automated coverage called out by status

- SID: 63 tests in `tests/machine/test_sid.c` covering registers, voice behavior, sync/ring/combined waveform behavior, ADSR, exponential ADSR shape, mixer/filter/routing, filter cutoff LUT range, filter regression, output conditioning, output HF rolloff, audio-flow smoke, and per-standard rate tables (PAL bit-identical lock, NTSC clock-scaling, envelope absolute-time preservation).
- 1541 media (M0–M6): `test_c1541_gcr`, `test_g64` (parse/reject),
  `test_c1541_media` (D64/G64 track build, mount G64 read-only, mechanics),
  `test_c64_real_1541_load` media LOAD/SAVE.
- Runtime audio scheduler: tests verify sample-count accounting and absence of batch-sized identical-sample SID runs.
- CPU: local tests cover documented CPU execution, typed bus traces (opcode,
  operand, data, dummy, RMW, stack, and vector cycles), instruction/cycle-step
  Phi2-arbiter parity under BA, IRQ/NMI entry, banking, I/O-under-RAM
  preservation, and BA read/write stalling.
- VIC-II: PAL sprite BA tests cover single, adjacent, split-window, cross-line, inactive, and unified BA-predicate behavior. NTSC tests cover the 65-cycle late sprite window and sprite 4 cross-line window. Tests also verify the current per-cycle c-access and sprite-fetch schedule markers.
- CIA: tests confirm CIA #1 IRQ routing, CIA #2 NMI edge-latch routing, RESTORE isolation, ICR read side effects, and debugger-safe peeks.
- 1541/IEC: tests cover VIA IEC line modeling, ATN acknowledge DATA pull, queued READ/SEARCH jobs, queued WRITE jobs (persist to image + dirty, write-protect on read-only, out-of-range error; Phase 4), queued FORMT EXECUTE jobs (erase track + dirty, write-protect on read-only; Phase 5), direct real-ROM `LOAD"*",8` from `GALENCIA.D64`, and runtime autorun through the real 1541 ROM/IEC path. Phase 5 DOS command/error-channel behavior (scratch/rename/validate/format/status) was verified end-to-end via the control port against the real ROM.
- Control port: `tests/control/test_control_protocol.c` covers Phase 1 through 6 request parsing, `assemble`/`find-symbol` parsing, plus text/binary response formatting. `tests/test_app_options.c` covers `--control-port` and `--headless` parsing.
- Keyboard joystick: `tests/frontend/test_frontend_joystick.c` covers layout defaults, numpad/WASD direction accumulation, diagonal-vs-cardinal non-clobbering, consume gating (only while assigned), disable/layout-switch release, and layout string round-trip. `tests/test_app_options.c` covers `--kbdjoy`/`--kbdjoy-layout` parsing and `[input]` INI save/reload round-trip.
- Cartridge detach on program load and reset: `tests/runtime/test_runtime_crt.c` loads a CRT, confirms ROML/ROMH map at `$8000`/`$A000`, then loads a PRG and confirms `$8000` no longer reads cartridge ROM (the program boots instead). It then re-attaches the CRT and checks that `runtime_client_reset_ex(client, false)` keeps the cartridge mapped while `runtime_client_reset_ex(client, true)` detaches it.
- Machine save-state foundation: `tests/machine/test_c64_snapshot.c` covers
  `c64_snapshot_size/save/load`, representative CPU/RAM/bus/VIC/CIA/SID/cart/D64
  drive-slot restore, byte-identical re-save after load, bad magic rejection,
  ROM hash mismatch rejection, failed-load all-or-nothing behavior,
  mid-instruction save rejection, and ignoring unknown optional chunks.
- Runtime save-state commands: `tests/runtime/test_runtime_savestate.c` covers
  `runtime_client_save_state` / `runtime_client_load_state`, runtime-thread
  snapshot file I/O, completion/error events, successful restore, bad snapshot
  rejection preserving live RAM, ROM hash mismatch rejection, and save after a
  one-cycle mid-instruction run.
- Save-state frontend/config hooks: `tests/test_app_options.c` covers
  `[state] quicksave_folder` migration into the snapshot browse slot. The legacy
  key is now stripped on every save (both `app_options_save_shutdown` and
  `app_options_save_paths_only`). Hotkeys, the in-app file browser's Save As /
  Load flow, and `.c64state` drag/drop remain manual smoke coverage.
- ROM config: `tests/test_app_options.c` covers the `[roms] single_system`
  flag (derived default from present paths, explicit override, save/reload
  round-trip) and `app_options_save_paths_only` writing ROM endpoints plus the
  flag while stripping the retired `[state] quicksave_folder`. The Configure
  dialog's ROM path fields, the Single-ROM checkbox gating, the "Emulate 1541"
  checkbox, and reboot-on-ROM-change reload remain manual smoke coverage.
- File browser directory listing: `tests/platform/test_platform_fs.c` covers
  `platform_fs_path_join` separator handling, `platform_fs_get_cwd`,
  `platform_fs_list_dir` on a missing directory (fails) and on a scratch
  directory (`..`-first, then directories, then files, each group
  case-insensitive alphabetical, correct `is_dir` per entry), and
  `platform_fs_is_dir` for a directory/file/missing path. The Nuklear dialog UI
  itself (`frontend_draw_file_browser`) is not exercised by automated tests and
  remains manual smoke coverage, same as the other modal dialogs.
- Runtime BRK auto-stop: `test_runtime_brk_pauses_without_executing` in `tests/runtime/test_runtime_scheduler.c` confirms a fetched BRK opcode pauses the runtime with `RUNTIME_STOP_REASON_BRK`, PC unchanged, and SP untouched (no stack push). The synthetic ROM builder in the same file (`write_runtime_roms`) now places `JMP $E000` at `$FFF0` so cycle-counted free-run tests loop inside ROM instead of running off the end into zero-filled RAM, which would now trip the same BRK auto-stop.
- Runtime frame publication: `test_step_instruction_publishes_updated_frame` and `test_step_instruction_publishes_updated_hires_frame` in `tests/runtime/test_runtime_frame.c` first create a completed live frame, then pause and confirm single-step completion publishes a current-state frame snapshot after text screen RAM (`STA $0400`) and high-res bitmap RAM (`STA $2000`) writes.
- Runtime run-to-cursor stepping: `test_run_to_cursor_at_current_pc_waits_for_next_hit` in `tests/runtime/test_runtime_stepping.c` confirms run-to-cursor on the current PC ignores the immediate match and stops on the next PC hit, matching loop-branch debugger use.
- Assembler: `tests/tools/` unit tests link the `assembler` library directly and cover conditionals, loops, macros, scopes/segments, expressions (`test_assembler_expressions.c`: `*` current-address semantics), and output targets (`test_assembler_targets.c`: named `.scope file="..."` routing to a separate target, graceful rejection when the host provides no `target_open`, and `assembler_predefine`/`C64MASM` build-flag detection). `tests/runtime/test_runtime_assembler.c` covers the in-emulator assemble-to-RAM path.
- D64 SAVE/write: `tests/tools/test_d64.c` covers PRG writes, BAM free-block
  accounting, duplicate rejection, and `@:` replacement. `tests/machine/test_c64_disk_load.c`
  covers the KERNAL SAVE trap writing a PRG to a writable D64. `tests/runtime/test_runtime_disk.c`
  covers writable mount/toggle status, and `tests/test_app_options.c` covers
  `[disk] *_writable` INI persistence.

## Known test gaps

- No local Harte corpus or harness is present for exhaustive undocumented 6510 opcode semantics.
- CPU tests do not provide per-opcode undocumented semantic coverage.
- Perfect electrical/chip-revision behavior is not covered.
- Some remaining video/audio timing work is not cycle-perfect.

## Human smoke still useful

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
- Writable D64 SAVE: mount a scratch D64, enable `Write`, type a small BASIC
  program, `SAVE "TEST",8`, restart with the same image, `LOAD "TEST",8`, and
  confirm it reloads. Keep user/original disks read-only unless testing writes.
- Reset/PRG loader: verify reset-before-load and autostart collection PRGs.
- Cartridge + reset popup: launch with `--crt <cart>.crt`, then drop a `.t64`/PRG and confirm the program boots (cart auto-detached). Separately, with a cart attached, click **Reset** and confirm the "Unmount cartridge on reset" popup appears (checked by default); confirm Reset-with-unmount drops to BASIC and Reset-with-checkbox-cleared re-runs the cart. With no cart attached, Reset must not show the popup.
- Autorun:
  - `--prg foo.prg --autorun` should boot and immediately run.
  - `--basic foo.bas --autorun` should boot and immediately run.
  - `--disk 8=game.d64 --autorun` should type `LOAD"*",8` and `RUN` automatically.
  - With 1541 emulation enabled, disk autorun should still be validated by loaded BASIC memory or visible program start, not by the host "RUN command received" log alone.
- Keyboard joystick:
  - `--kbdjoy 2` (or `Alt+Shift+2` at runtime) then verify numpad 8/2/4/6/diagonals + KP_0 fire drive a joystick game on port 2.
  - Switch to `--kbdjoy-layout wasd` and confirm W/A/S/D + Space drive the joystick while the C64 has keyboard focus, and that those letters do NOT reach the C64 keyboard while assigned; confirm they type normally when the joystick is disabled (`Alt+Shift+2` again) or when the debugger UI has focus.
  - Confirm `Alt+1`/`Alt+2` still map real game controllers independently.
  - Config dialog (Misc → Machine → Emulator → Configure... → Emulator tab): set the port tri-state (Off/Port 1/Port 2) and Numpad/WASD layout, Apply, and confirm the change takes effect live; confirm assigning a port via `Alt+Shift+2` first and then opening the dialog shows Port 2 (not Off).
- Assembler tab:
  - Assemble with Reset on.
- Save-state UI:
  - Configure Misc -> Machine -> Emulator -> Configure... -> Emulator tab ->
    Quicksave Folder, Apply, quit, and confirm `[state] quicksave_folder` is
    persisted.
  - Press `Opt+Shift+>` twice and confirm timestamped `.c64state` files are
    created in the quicksave folder without overwriting.
  - Press `Opt+Shift+<` and confirm the newest `.c64state` in that folder loads.
  - Use Misc -> Machine -> State -> Save As... / Load... and drag/drop a
    `.c64state` file to confirm both named paths load/save.
  - Enable keyboard joystick with `Alt+Shift+2`, set WASD layout, save state,
    relaunch with defaults, then load that state and confirm keyboard joystick
    is restored to port 2/WASD.
  - Assemble with Reset off.
  - Test live inject plus auto-run.
  - Verify error dialog.
  - Verify symbol display in disassembly.
- Host load/save:
  - Load with file-address header.
  - Load with Basic Program and verify `$2B-$2E`.
  - Save as Basic Program and reload.
  - Load a `.bas`/`.txt` ASCII listing with Basic Text and verify it LISTs and
    RUNs; save it back with Basic Text and diff against the source (stock BASIC
    V2 only). Covered automatically by the `basic_v2` and `runtime_fileio`
    (`test_load_and_save_basic_text`) tests.
  - Save raw range with and without header.
  - Verify Eject button and Machine tab section order.
- File browser dialog (Linux, macOS, Windows): open each of the 10 triggers
  (Load PRG dialog, Disk mount/add, Config INI/Symbol pickers, Assembler
  Browse, Load/Save Bin Browse, State Save As/Load) and confirm directory
  navigation via double-click and via typing a path in the Path field,
  extension filtering hides non-matching files where configured (`d64`, `ini`,
  `c64state`), Save-mode default-extension append and overwrite-confirm work,
  Cancel/ESC/titlebar-close leave the originating field unchanged, and that it
  blocks input to the base view the same way the Config/Symbol Lookup dialogs
  do.
- UI/debugger:
  - Verify modal dialogs block base-view focus changes on outside clicks.
  - Verify memory/disassembly source modes and virtual views.
  - Verify call stack and hardware view are populated from runtime snapshots.
- Control port:
  - `./build/c64m --control-port 6510`, connect to `127.0.0.1:6510`, send `1 ping`, and expect `1 ok`.
  - Verify Phase 2 with `reset`, `step-instruction`, and deferred `get-cpu`.
  - Verify Phase 3 binary payloads: `get-frame` returns `height * stride`
    bytes (`384 * 312 * 4` for PAL, `384 * 263 * 4` for NTSC),
    `get-memory $0400 64 map` returns 64 bytes, and `get-debug-memory`
    returns 196608 bytes without write history.
  - Verify Phase 4 commands: key up/down, joystick, RESTORE, `paste-text-data`, D64 mount/unmount/status, `load-prg`, `load-bin`, and `save-bin`.
  - Verify Phase 5 breakpoint commands: `break-exec`, `break-enable`, `break-list`, `break-clear`, `break-clear-all`, `break-create`, `break-update`, and `rearm-oneshots`.
  - Verify Phase 6 wait commands: `wait-paused`, `wait-running`, `wait-frame`, and `wait-event`, including timeout behavior.
  - Verify Phase 7 with `./build/c64m --headless --control-port 6511`; connect to localhost and confirm `wait-running`, `wait-frame`, `get-frame`, `pause`, and `wait-paused`.
  - Verify `assemble`/`find-symbol`: `assemble reset=0 address=$c000 samples/test1.asm` returns `ok address=$C000`, then `find-symbol loop` returns `ok address=$C004 name=loop`. A bad source returns `error assemble-error <message>`; a missing/absent label returns `error not-found`.
  - Verify normal SDL UI still runs without `--control-port`.
  - Verify quitting the emulator joins the control socket thread cleanly with no connected client and with an idle connected client.

## Audio measurement practice

- Use `tools/capture_sid_audio.py` for c64m capture.
- Use `tools/compare_sid_audio.py` for reference/candidate metrics.
- Current reference path described by status is `x64sc-20s.mp3`.
- Current baseline should be compared against SID Phase 10 metrics unless deliberately starting a new measured phase.
- Do not claim fidelity improvement from subjective listening alone.

## Regression notes

- Some accepted changes regressed scalar score slightly but replaced known placeholder behavior with more correct behavior.
- Treat measured regressions honestly. Record whether the regression is accepted and why.
