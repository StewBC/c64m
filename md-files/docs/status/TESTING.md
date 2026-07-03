# Testing and smoke status

## Automated coverage called out by status

- SID: 60 tests in `tests/machine/test_sid.c` covering registers, voice behavior, sync/ring/combined waveform behavior, ADSR, exponential ADSR shape, mixer/filter/routing, filter cutoff LUT range, filter regression, output conditioning, output HF rolloff, and audio-flow smoke.
- Runtime audio scheduler: tests verify sample-count accounting and absence of batch-sized identical-sample SID runs.
- CPU: local tests cover documented CPU execution, bus integration, trace timing, IRQ/NMI entry, banking, I/O-under-RAM preservation, and BA read/write stalling.
- VIC-II: PAL sprite BA tests cover single, adjacent, split-window, cross-line, inactive, and unified BA-predicate behavior. NTSC tests cover the 65-cycle late sprite window and sprite 4 cross-line window.
- CIA: tests confirm CIA #1 IRQ routing, CIA #2 NMI edge-latch routing, RESTORE isolation, ICR read side effects, and debugger-safe peeks.
- 1541/IEC: tests cover VIA IEC line modeling, ATN acknowledge DATA pull, queued READ/SEARCH jobs, direct real-ROM `LOAD"*",8` from `GALENCIA.D64`, and runtime autorun through the real 1541 ROM/IEC path.
- Control port: `tests/control/test_control_protocol.c` covers Phase 1 through 6 request parsing plus text/binary response formatting. `tests/test_app_options.c` covers `--control-port` and `--headless` parsing.
- Keyboard joystick: `tests/frontend/test_frontend_joystick.c` covers layout defaults, numpad/WASD direction accumulation, diagonal-vs-cardinal non-clobbering, consume gating (only while assigned), disable/layout-switch release, and layout string round-trip. `tests/test_app_options.c` covers `--kbdjoy`/`--kbdjoy-layout` parsing and `[input]` INI save/reload round-trip.
- Cartridge detach on program load and reset: `tests/runtime/test_runtime_crt.c` loads a CRT, confirms ROML/ROMH map at `$8000`/`$A000`, then loads a PRG and confirms `$8000` no longer reads cartridge ROM (the program boots instead). It then re-attaches the CRT and checks that `runtime_client_reset_ex(client, false)` keeps the cartridge mapped while `runtime_client_reset_ex(client, true)` detaches it.
- Runtime BRK auto-stop: `test_runtime_brk_pauses_without_executing` in `tests/runtime/test_runtime_scheduler.c` confirms a fetched BRK opcode pauses the runtime with `RUNTIME_STOP_REASON_BRK`, PC unchanged, and SP untouched (no stack push). The synthetic ROM builder in the same file (`write_runtime_roms`) now places `JMP $E000` at `$FFF0` so cycle-counted free-run tests loop inside ROM instead of running off the end into zero-filled RAM, which would now trip the same BRK auto-stop.
- Runtime frame publication: `test_step_instruction_publishes_updated_frame` and `test_step_instruction_publishes_updated_hires_frame` in `tests/runtime/test_runtime_frame.c` first create a completed live frame, then pause and confirm single-step completion publishes a current-state frame snapshot after text screen RAM (`STA $0400`) and high-res bitmap RAM (`STA $2000`) writes.
- Runtime run-to-cursor stepping: `test_run_to_cursor_at_current_pc_waits_for_next_hit` in `tests/runtime/test_runtime_stepping.c` confirms run-to-cursor on the current PC ignores the immediate match and stops on the next PC hit, matching loop-branch debugger use.

## Known test gaps

- No local Harte corpus or harness is present for exhaustive undocumented 6510 opcode semantics.
- CPU tests do not provide per-opcode undocumented semantic coverage.
- Perfect electrical/chip-revision behavior is not covered.
- Some remaining video/audio timing work is not cycle-perfect.

## Human smoke still useful

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
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
  - Assemble with Reset off.
  - Test live inject plus auto-run.
  - Verify error dialog.
  - Verify symbol display in disassembly.
- Host load/save:
  - Load with file-address header.
  - Load with Basic Program and verify `$2B-$2E`.
  - Save as Basic Program and reload.
  - Save raw range with and without header.
  - Verify Eject button and Machine tab section order.
- UI/debugger:
  - Verify modal dialogs block base-view focus changes on outside clicks.
  - Verify memory/disassembly source modes and virtual views.
  - Verify call stack and hardware view are populated from runtime snapshots.
- Control port:
  - `./build/c64m --control-port 6510`, connect to `127.0.0.1:6510`, send `1 ping`, and expect `1 ok`.
  - Verify Phase 2 with `reset`, `step-instruction`, and deferred `get-cpu`.
  - Verify Phase 3 binary payloads: `get-frame` returns `384 * 272 * 4` bytes, `get-memory $0400 64 map` returns 64 bytes, and `get-debug-memory` returns 196608 bytes without write history.
  - Verify Phase 4 commands: key up/down, joystick, RESTORE, `paste-text-data`, D64 mount/unmount/status, `load-prg`, `load-bin`, and `save-bin`.
  - Verify Phase 5 breakpoint commands: `break-exec`, `break-enable`, `break-list`, `break-clear`, `break-clear-all`, `break-create`, `break-update`, and `rearm-oneshots`.
  - Verify Phase 6 wait commands: `wait-paused`, `wait-running`, `wait-frame`, and `wait-event`, including timeout behavior.
  - Verify Phase 7 with `./build/c64m --headless --control-port 6511`; connect to localhost and confirm `wait-running`, `wait-frame`, `get-frame`, `pause`, and `wait-paused`.
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
