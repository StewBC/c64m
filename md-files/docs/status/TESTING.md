# Testing and smoke status

## Automated coverage called out by status

- SID: 60 tests in `tests/machine/test_sid.c` covering registers, voice behavior, sync/ring/combined waveform behavior, ADSR, exponential ADSR shape, mixer/filter/routing, filter cutoff LUT range, filter regression, output conditioning, output HF rolloff, and audio-flow smoke.
- Runtime audio scheduler: tests verify sample-count accounting and absence of batch-sized identical-sample SID runs.
- CPU: local tests cover documented CPU execution, bus integration, trace timing, IRQ/NMI entry, banking, I/O-under-RAM preservation, and BA read/write stalling.
- VIC-II: PAL sprite BA tests cover single, adjacent, split-window, cross-line, inactive, and unified BA-predicate behavior. NTSC tests cover the 65-cycle late sprite window and sprite 4 cross-line window.
- CIA: tests confirm CIA #1 IRQ routing, CIA #2 NMI edge-latch routing, RESTORE isolation, ICR read side effects, and debugger-safe peeks.
- 1541/IEC: tests cover VIA IEC line modeling, ATN acknowledge DATA pull, queued READ/SEARCH jobs, direct real-ROM `LOAD"*",8` from `GALENCIA.D64`, and runtime autorun through the real 1541 ROM/IEC path.
- Control port: `tests/control/test_control_protocol.c` covers Phase 1 request parsing and response formatting. `tests/test_app_options.c` covers `--control-port` parsing.

## Known test gaps

- No local Harte corpus or harness is present for exhaustive undocumented 6510 opcode semantics.
- CPU tests do not provide per-opcode undocumented semantic coverage.
- Perfect electrical/chip-revision behavior is not covered.
- Some remaining video/audio timing work is not cycle-perfect.

## Human smoke still useful

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
- Reset/PRG loader: verify reset-before-load and autostart collection PRGs.
- Autorun:
  - `--prg foo.prg --autorun` should boot and immediately run.
  - `--basic foo.bas --autorun` should boot and immediately run.
  - `--disk 8=game.d64 --autorun` should type `LOAD"*",8` and `RUN` automatically.
  - With 1541 emulation enabled, disk autorun should still be validated by loaded BASIC memory or visible program start, not by the host "RUN command received" log alone.
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
