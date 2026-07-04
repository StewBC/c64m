# c64m status handoff

This file is intentionally short. It is the routing document for agents, not the full project encyclopedia.

Read first:

1. `AGENTS.md` - agent workflow, build/test rules, repository conventions.
2. `MASTER.md` - product/architecture source of truth.
3. This `STATUS.md` - current handoff summary and routing.
4. The relevant component file under `docs/status/`.

## Current stable baseline

The emulator currently includes:

- Core C64 runtime: 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction stepping, and frame handoff.
- VIC-II through Phase J, except light pen skipped.
- CIA through Phase G, including CIA #1 IRQ, CIA #2 NMI, timers, ICR behavior, keyboard/joystick/RESTORE, CIA #2 VIC bank, IEC pins, TOD, and alarm.
- SID functional audio plus SID improvement Phase 10 as the current measured baseline.
- Runtime audio infrastructure with cycle-stepped sample production, SDL output, recording, smoke tone, turbo mute, and overrun/underrun counters.
- Debugger/config/frontend UI through the documented phases, including hardware view, memory/disassembly source modes, virtual memory views, assembler tab, help UI, host load/save UI, and modal input isolation.
- D64 read-only disk support for devices 8 and 9, KERNAL LOAD traps, directory loads, wildcard matching, runtime mount/unmount, startup autorun flows, and optional real 1541 ROM/IEC LOAD path when `[disk] emulate_1541=1`.
- Generic 8K/16K CRT cartridge loading through tools parsing, machine bus mapping, runtime load command, drag/drop, Machine Load auto-detection, and `--crt`.
- Practical undocumented 6510 opcode coverage.

## Recent high-value handoff notes

- Host-keyboard joystick input is implemented. In addition to the SDL game-controller path, the keyboard can drive a C64 joystick port. Two layouts are config-selectable: `numpad` (KP_8/2/4/6 + diagonals KP_7/9/1/3, fire KP_0; conflict-free, no key stealing) and `wasd` (W/A/S/D + Space; these are stolen from the C64 keyboard while assigned). `Alt+Shift+1`/`Alt+Shift+2` assign/toggle the keyboard joystick on port 1/2 (the existing `Alt+1`/`Alt+2` still map real controllers). Both are also editable in the config dialog Emulator tab (port tri-state Off/1/2 + layout selector), applied live. Persisted in the `[input]` INI section (`keyboard_joystick_layout`, `keyboard_joystick_port`) and settable via `--kbdjoy <0|1|2>` / `--kbdjoy-layout <numpad|wasd>`. Implemented as `src/frontend/frontend_joystick_input.{c,h}`, OR'd into the existing `runtime_client_set_joystick` choke point in `src/main.c`; no runtime/machine changes were needed. See `docs/status/FRONTEND_DEBUGGER.md` and `md-files/C64MFEAT_KBDJOY_1.md`.
- The disassembly view now renders a trailing effective-address/value column, e.g. `LDA ($FB),Y   [$4050:25]`. It is computed in the frontend from the current CPU registers and the CPU-visible memory snapshot, and is shown only while paused. It appears for indexed/indirect modes (`zp,x`/`zp,y`/`abs,x`/`abs,y`/`(zp,x)`/`(zp),y`/`jmp (ind)`) and for direct/branch/`jmp`/`jsr` operands that were rendered as a label; plain literal addresses (`lda #$FF`, `lda $4000`, `lda $fb`) are left unannotated. Data accesses show `[$addr:value]`; control-flow targets show `[$addr]`. The disassembler tool now exposes `disasm_6502_opcode_mode()` for this. See `docs/status/FRONTEND_DEBUGGER.md`.
- The runtime now auto-pauses on a fetched BRK opcode (`RUNTIME_STOP_REASON_BRK`) instead of executing it, in every free-running execution path (continuous run, run-N-instructions/cycles, step-over, step-out). The CPU core's own BRK handling is unchanged and remains hardware-accurate (push PCH/PCL/flags, jump through `$FFFE`); only the runtime layer now refuses to let that execute unattended, since an unhandled BRK vector previously caused the stack pointer to wrap and overwrite `$0100-$01FF` indefinitely with no UI signal. A manual single-step still executes a BRK normally. The OS window title now also reflects live runtime state (`c64m - Running` / `c64m - Paused (<reason>)` / `c64m - Error`) so this is visible without the debugger UI open. See `docs/status/CPU_MACHINE.md` and `docs/status/FRONTEND_DEBUGGER.md`.
- 1541 ROM/IEC disk loads now work for the standard DOS 2.6 1541 ROM with mounted read-only D64 images. The KERNAL LOAD trap remains as fallback when 1541 emulation is disabled or no 1541 ROM is loaded. See `docs/status/IEC1541.md`.
- Breakpoint actions Tron, Swap, and Type now carry parameters persisted in the INI and editable in the Breakpoint Editor. Tron accepts an optional custom trace file path; Swap accepts `+N`/`-N` (relative) or `N` (absolute 1-based, wraps) for disk queue navigation on device 8; Type stores raw text in the input-encoding format; the translator is implemented in `util/paste_parser` and delivers events via `RUNTIME_COMMAND_PASTE_EVENTS`, including one-shot modifier and wait-token support. Tron and Troff are mutually exclusive. See `docs/status/FRONTEND_DEBUGGER.md` for parser syntax details.
- Control port Phases 1 through 7 are implemented as an opt-in localhost-only service with main-loop-owned runtime dispatch, execution/state commands, binary frame/memory/debug-memory responses, input injection, paste payloads, file/disk commands, breakpoint management, wait commands, and a `--headless --control-port PORT` mode. See `docs/status/CONTROL.md`.
- Disk images are now persisted in the `[disk]` INI section on quit; paths are stored relative to the INI file and each drive holds an ordered queue (comma-separated). The disk UI shows `[N][Add][Eject] <combo>` per device; Shift+Eject clears the whole queue. See `docs/status/DISK_IO.md` for full semantics.
- Generic CRT support covers normal hardware type 0 8K/16K ROM cartridges only. Writes under cartridge ROM update shadow RAM, resets preserve the attached cartridge, and broader mappers/INI persistence are deferred. See `docs/status/CPU_MACHINE.md`, `docs/status/DISK_IO.md`, and `c64mcrt.md`.
- Save-state foundation now includes a chunked machine serializer plus runtime-thread
  save/load commands and client APIs. No frontend UI, hotkeys, CLI option,
  self-contained embedding mode, or full 1541 state capture exists yet. See
  `docs/status/CPU_MACHINE.md`.
- CIA #2 NMI is wired to the CPU NMI edge latch. RESTORE remains a separate one-shot NMI source.
- VIC-II sprite BA timing now uses per-standard PAL 6569 and NTSC 6567R8 tables selected from machine video configuration.
- Runtime audio production now advances from the cycle-stepping path, not from 1024-cycle batches.
- SID Phase 10 is the current audio fidelity baseline: score 1.2838 against `x64sc-20s.mp3`, with 16-22 kHz excess reduced but not eliminated.
- The CPU has explicit dispatch for all 256 opcode slots and practical undocumented opcode implementations, but not perfect analog/chip-revision behavior.

## Component files

- `docs/status/VICII.md` - raster/video/sprite/BA/VIC memory status.
- `docs/status/CIA.md` - CIA #1/#2 timers, ICR, IRQ/NMI, keyboard/joystick/RESTORE, IEC, TOD.
- `docs/status/SID.md` - SID register behavior, voices, waveforms, ADSR, filter, read-back, fidelity phases.
- `docs/status/AUDIO.md` - runtime/platform audio transport, scheduling, recording, smoke tone, turbo behavior.
- `docs/status/CPU_MACHINE.md` - 6510, bus, banking, reset/boot, IRQ/NMI, BA stalls, CLI startup load.
- `docs/status/FRONTEND_DEBUGGER.md` - UI, debugger, memory views, config, assembler, help, dialogs.
- `docs/status/CONTROL.md` - localhost control port protocol, server, main-loop dispatch, and deferred phases.
- `docs/status/DISK_IO.md` - D64 parser/runtime mounting/KERNAL LOAD/host file load-save.
- `docs/status/TESTING.md` - tests, smoke checks, known useful manual validation.
- `docs/status/IEC1541.md` - 1541 emulator and VIA 6522 implementation status.
- `docs/status/DEFERRED.md` - known gaps and intentionally deferred work.
- `docs/status/OPTIMIZATIONS.md` - accepted and rejected optimization notes.
- `docs/status/ORIGINAL_STATUS.md` - unmodified source handoff preserved for traceability.

## Update rule

Do not grow this file back into a full status dump. Add detailed facts to the relevant component file. Only add top-level facts here when they affect agent routing or the current baseline.
