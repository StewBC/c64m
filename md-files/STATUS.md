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
- D64 disk support for devices 8 and 9, KERNAL LOAD/SAVE traps, directory loads, wildcard matching, runtime mount/unmount, startup autorun flows, opt-in writable image flush, and optional real 1541 ROM/IEC LOAD and sector-WRITE path when `[disk] emulate_1541=1`.
- Generic 8K/16K CRT cartridge loading through tools parsing, machine bus mapping, runtime load command, drag/drop, Machine Load auto-detection, and `--crt`.
- Practical undocumented 6510 opcode coverage.

## Recent high-value handoff notes

- All host file-picker triggers (Load PRG/BAS, Disk mount/add, Config INI/symbol pickers, Assembler browse, Load/Save Bin browse, State Save As/Load) now open a single cross-platform in-app Nuklear file browser instead of shelling out to `osascript` (macOS-only, and a no-op on every other platform, so Linux never had a working file picker). New `src/platform/platform_fs.{c,h}` provides portable, non-`chdir()`-ing directory listing. See `docs/status/FRONTEND_DEBUGGER.md` § "File browser dialog".
- The assembler regained three capabilities from a2m, in a clean C64 form. (1) A standalone command-line assembler `c64masm` (`src/tools/c64masm/`) builds raw binaries from the shared `assembler` library; switches `-i -o -a -s -D -v -h`. (2) Named `.scope name file="..." dest="..."` redirects that scope's output to a separate target/file, so one source builds several binaries (loader + game, overlays); implemented via new optional `CB_ASM_CTX.target_open`/`target_release` callbacks + per-`TARGET` `ctx` (the in-emulator host provides no `target_open`, so it errors there rather than mis-routing). (3) Build-time detection of CLI vs. in-emulator assembly through the new `assembler_predefine()` API — the CLI predefines `C64MASM=1`, the runtime predefines `C64MASM=0`, tested with `.if C64MASM`. Also in this pass, the `*` (program counter) symbol was corrected to evaluate to the current instruction's address (standard convention; `jmp *` is a self-loop) instead of the old `address+1`. New tests: `tests/tools/test_assembler_targets.c` and `test_assembler_expressions.c`. See `md-files/ASMDESIGN.md` § "Output Targets and the Command-Line Tool", `docs/status/FRONTEND_DEBUGGER.md`, and the assembler section of `manual/manual.md`.
- The Machine->Programs Load and Save dialogs now have a **Basic Text** option that loads/saves ASCII BASIC listings instead of tokenized PRGs, using a new stock BASIC V2 tokenizer/detokenizer in `src/util/basic_v2.{c,h}`. Load reads a text file, tokenizes it host-side (REM/DATA/quote literal modes, ROM-ordered greedy keyword matching, operator tokens, case normalization to the uppercase set), writes the image at `$0801`, and sets TXTTAB/VARTAB/ARYTAB/STREND (`$2B-$32`). Save detokenizes the live program (`$2B/$2C`..`$2D/$2E`) to ASCII with no PRG header. Only stock BASIC V2 is handled (extension dialects out of scope). Non-printable PETSCII bytes (control/colour codes, cursor movement, CLR/HOME, `π`, graphics) round-trip losslessly as `{name}`/`{$hh}` escapes so control codes embedded in string literals survive. Basic Text and Basic Program are mutually exclusive in both dialogs. Plumbed through the existing intent/`runtime_client`/command/runtime path (`is_basic_text` flag); the remote control protocol is unchanged. Covered by `tests/util/test_basic_v2.c` and `test_load_and_save_basic_text` in `tests/runtime/test_runtime_fileio.c`. See `docs/status/FRONTEND_DEBUGGER.md`.
- SDL text input is now gated on UI edit-field focus. The main loop enables `SDL_StartTextInput` only while a Nuklear text field is active (via `frontend_wants_text_input`) and stops it otherwise. This removes the macOS "press and hold" accent popup that appeared when holding a key (e.g. `s`) for WASD keyboard-joystick emulation, since SDL text input is otherwise on by default. See `docs/status/FRONTEND_DEBUGGER.md`.
- Host-keyboard joystick input is implemented. In addition to the SDL game-controller path, the keyboard can drive a C64 joystick port. Two layouts are config-selectable: `numpad` (KP_8/2/4/6 + diagonals KP_7/9/1/3, fire KP_0; conflict-free, no key stealing) and `wasd` (W/A/S/D + Space; these are stolen from the C64 keyboard while assigned). `Alt+Shift+1`/`Alt+Shift+2` assign/toggle the keyboard joystick on port 1/2, and `Alt+Shift+0` disables it outright (the existing `Alt+1`/`Alt+2` still map real controllers). Both are also editable in the config dialog Emulator tab (port tri-state Off/1/2 + layout selector), applied live. Persisted in the `[input]` INI section (`keyboard_joystick_layout`, `keyboard_joystick_port`) and settable via `--kbdjoy <0|1|2>` / `--kbdjoy-layout <numpad|wasd>`. Implemented as `src/frontend/frontend_joystick_input.{c,h}`, OR'd into the existing `runtime_client_set_joystick` choke point in `src/main.c`; no runtime/machine changes were needed. See `docs/status/FRONTEND_DEBUGGER.md` and `md-files/C64MFEAT_KBDJOY_1.md`.
- The disassembly view now renders a trailing effective-address/value column, e.g. `LDA ($FB),Y   [$4050:25]`. It is computed in the frontend from the current CPU registers and the CPU-visible memory snapshot, and is shown only while paused. It appears for indexed/indirect modes (`zp,x`/`zp,y`/`abs,x`/`abs,y`/`(zp,x)`/`(zp),y`/`jmp (ind)`) and for direct/branch/`jmp`/`jsr` operands that were rendered as a label; plain literal addresses (`lda #$FF`, `lda $4000`, `lda $fb`) are left unannotated. Data accesses show `[$addr:value]`; control-flow targets show `[$addr]`. The disassembler tool now exposes `disasm_6502_opcode_mode()` for this. See `docs/status/FRONTEND_DEBUGGER.md`.
- The runtime now auto-pauses on a fetched BRK opcode (`RUNTIME_STOP_REASON_BRK`) instead of executing it, in every free-running execution path (continuous run, run-N-instructions/cycles, step-over, step-out). The CPU core's own BRK handling is unchanged and remains hardware-accurate (push PCH/PCL/flags, jump through `$FFFE`); only the runtime layer now refuses to let that execute unattended, since an unhandled BRK vector previously caused the stack pointer to wrap and overwrite `$0100-$01FF` indefinitely with no UI signal. A manual single-step still executes a BRK normally. The OS window title now also reflects live runtime state (`c64m - Running` / `c64m - Paused (<reason>)` / `c64m - Error`) so this is visible without the debugger UI open. See `docs/status/CPU_MACHINE.md` and `docs/status/FRONTEND_DEBUGGER.md`.
- 1541 ROM/IEC disk loads now work for the standard DOS 2.6 1541 ROM with mounted read-only D64 images. The KERNAL LOAD trap remains as fallback when 1541 emulation is disabled or no 1541 ROM is loaded. See `docs/status/IEC1541.md`.
- Breakpoint actions Tron, Swap, and Type now carry parameters persisted in the INI and editable in the Breakpoint Editor. Tron accepts an optional custom trace file path; Swap accepts `+N`/`-N` (relative) or `N` (absolute 1-based, wraps) for disk queue navigation on device 8; Type stores raw text in the input-encoding format; the translator is implemented in `util/paste_parser` and delivers events via `RUNTIME_COMMAND_PASTE_EVENTS`, including one-shot modifier and wait-token support. Tron and Troff are mutually exclusive. See `docs/status/FRONTEND_DEBUGGER.md` for parser syntax details.
- Control port Phases 1 through 7 are implemented as an opt-in localhost-only service with main-loop-owned runtime dispatch, execution/state commands, binary frame/memory/debug-memory responses, input injection, paste payloads, file/disk commands, breakpoint management, wait commands, and a `--headless --control-port PORT` mode. It also exposes `assemble` (async source assembly mirroring the Misc->Assembler tab settings, with auto-pause) and `find-symbol` (label lookup from a main-loop-cached symbol snapshot). See `docs/status/CONTROL.md`.
- Disk images are now persisted in the `[disk]` INI section on quit; paths are stored relative to the INI file and each drive holds an ordered queue (comma-separated). The disk UI shows `[N][Add][Eject] <combo>` per device; Shift+Eject clears the whole queue. See `docs/status/DISK_IO.md` for full semantics.
- D64 SAVE support is implemented for writable images through the KERNAL SAVE trap (`$FFD8`). The disk UI has a per-current-image `Write` checkbox; INI uses parallel `8_writable` / `9_writable` lists; successful SAVE mutates the in-memory D64, updates BAM/directory state, and flushes the host `.d64` file from the runtime thread.
- Real 1541 DOS writes now work via the job-level WRITE intercept (Phase 4, `md-files/C64IEC1541PHASE_4.md`). With `[disk] emulate_1541=1`, SAVE and sequential/relative file writes to a writable image are persisted by handling the drive's WRITE job (`$90`) at the same job-dispatch altitude as the existing READ intercept: `c1541_copy_job_buffer_to_sector()` copies the job buffer back into the mounted image via the new `c64_get_drive_slot_mut()` accessor, marks the slot dirty (runtime flush persists it), and returns write-protect (DOS 26) on read-only mounts. See `docs/status/IEC1541.md`.
- The DOS command channel and error/status channel now work via the real 1541 ROM (Phase 5, `md-files/C64IEC1541PHASE_5.md`). Scratch (`S0:`), rename (`R0:`), validate (`V0`), initialize, and the `OPEN 15,8,15` status readback needed no new code — they are ordinary directory/BAM READ/WRITE jobs the real ROM issues, satisfied by Phase 3 read + Phase 4 write. Only format (`N0:name,id`) needed code: the DOS FORMT EXECUTE job (`$E0`) is intercepted in `c1541_format_track()`, which erases the target track in the writable image and returns success so the ROM's own DOS writes the fresh BAM/directory (name/ID) via WRITE jobs — no `d64_image_format()` helper required. Verified end-to-end over the `--control-port` remote (screen scraped from `$0400`): SAVE/scratch/rename/format all confirmed against the real ROM. Still deferred: media-level GCR write fidelity / G64, cross-drive copy, and block/memory commands.
- Generic CRT support covers normal hardware type 0 8K/16K ROM cartridges only. Writes under cartridge ROM update shadow RAM, resets preserve the attached cartridge, and broader mappers/INI persistence are deferred. See `docs/status/CPU_MACHINE.md`, `docs/status/DISK_IO.md`, and `c64mcrt.md`.
- Save-state support now includes a chunked machine serializer, runtime-thread
  save/load commands, client APIs, `.c64state` drag/drop, Machine tab State
  Save As/Load dialogs, `Opt+Shift+>` quicksave, `Opt+Shift+<` quickload, and a
  persisted Emulator-tab quicksave folder option. Save-state files also carry
  the frontend keyboard-joystick layout/port as optional host metadata. CLI
  state loading, self-contained embedding mode, and full 1541 state capture
  remain deferred. See
  `docs/status/CPU_MACHINE.md`.
- CIA #2 NMI is wired to the CPU NMI edge latch. RESTORE remains a separate one-shot NMI source.
- VIC-II sprite BA timing now uses per-standard PAL 6569 and NTSC 6567R8 tables selected from machine video configuration.
- Runtime audio production now advances from the cycle-stepping path, not from 1024-cycle batches.
- Fixed PAL live-audio distortion: the runtime frame pacer was hardcoded to 60 fps, so PAL (~50 fps) emulated ~20% faster than wall-clock and over-ran the audio ring buffer, dropping samples. The pacer now paces to the active standard's real frame rate via `c64_config_cycles_per_frame()`. NTSC (~60 fps) was already correct and is unchanged. This also fixes PAL previously running ~20% fast in wall-clock. See `docs/status/AUDIO.md`.
- SID Phase 10 is the current audio fidelity baseline: score 1.2838 against `x64sc-20s.mp3`, with 16-22 kHz excess reduced but not eliminated.
- SID rate tables are now clock-parameterized. `sid_init(sid *, uint32_t cpu_clock_hz)` selects per-standard envelope tables, filter cutoff LUT, and HF-rolloff coefficient. PAL is bit-identical to the prior baseline (verified: identical PAL capture bytes); NTSC now uses NTSC-timed constants instead of PAL ones. See `docs/status/SID.md`.
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
