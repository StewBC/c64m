# CPU and machine status

## Current implementation

- Core C64 runtime is implemented.
- 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction stepping, and frame handoff are implemented.
- Real 64C ROM execution reaches BASIC READY with visible cursor and keyboard input.
- Machine owns monotonic master cycle.
- Machine coordinates CPU bus events with VIC, CIA, and SID hooks.
- Public instruction stepping and cycle stepping now use the same Phi2 arbiter,
  so both honor BA stalls and produce the same CPU/VIC timing result.
- The resumable CPU path now covers every documented NMOS 6502/6510 opcode and
  addressing form: immediate, zero-page, indexed zero-page, absolute, indexed
  absolute, `(zp,X)`, `(zp),Y`, branches, stack/control flow, indexed RMW, and
  indirect JMP page-wrap behavior. IRQ/NMI entry also uses this path. Implemented
  practical-undocumented opcode families remain on compatibility
  instruction-trace/replay pending their own trace gates.

## 6510 / undocumented opcodes

- C64MENH Phase 3 audited undocumented opcode coverage.
- `src/machine/c6510.c` has explicit dispatch for all 256 opcode slots.
- The implementation is adapted from the a2m cycle-accurate NMOS 6502 core.
- Practical undocumented execution families are implemented in `c6510_inln.h`:
  - SLO, RLA, SRE, RRA
  - SAX, LAX, DCP, ISC/ISB
  - unofficial NOP variants
  - alternate `SBC #$EB`
  - ANC, ALR, ARR, AXS/SBX, LAS
  - AHX/SHA, SHX, SHY, TAS/SHS
  - XAA/ANE
  - unstable `LAX #imm`
  - JAM/KIL
- C64 wrapper routes all CPU reads/writes through the machine bus.
- BA stalls use traced read/write events, so undocumented RMW/store opcodes follow the same integration path as official opcodes.
- CPU traces distinguish opcode fetches, operands, data, dummy cycles, RMW
  dummy writes, stack accesses, and vector reads. Mid-instruction snapshots are
  rejected for either the replay or resumable execution path.

## Interrupts

- CIA #1 routes to CPU IRQ.
- CIA #2 routes to CPU NMI through an edge latch.
- RESTORE is a separate one-shot NMI source.
- CPU samples NMI at instruction entry before IRQ.
- Known smoke trace after 1,000,000 cycles:

```text
PC=$FD7E
CIA #1 IRQ pending = true
CPU IRQ entries = 0
CIA #1 ICR reads = 0
```

This is expected when the CPU interrupt-disable flag remains set.

## BRK handling

- `src/machine/c6510.c` / `c6510_inln.h` still execute BRK exactly per 6502
  hardware: push PCH, PCL, flags (with B set), then jump through the
  `$FFFE`/`$FFFF` vector. This is unchanged and stays hardware-accurate for
  any code that legitimately uses BRK as a vectored trap.
- The runtime layer (`src/runtime/runtime_thread.c`) now treats a fetched
  BRK opcode (`$00`) the same way it treats an execute breakpoint: before
  letting the CPU execute the next instruction in any free-running loop
  (continuous run, run-N-instructions, run-N-cycles, step-over, step-out),
  it peeks the opcode at PC and, if it is BRK, pauses instead of executing
  with `RUNTIME_STOP_REASON_BRK` rather than running the instruction. No
  stack push happens for an auto-stopped BRK.
- This is unconditional (no opt-out yet) since real C64 software essentially
  never executes BRK on its normal control path. Previously, an unhandled
  BRK vector (e.g. pointing at uninitialized/zero-filled memory) caused the
  CPU to keep re-triggering BRK, wrapping the stack pointer and overwriting
  `$0100-$01FF` indefinitely with no way to tell from the UI that the
  machine had effectively run away.
- A manual single "step into" instruction is NOT gated by this check and
  still executes a BRK normally (explicit user action).
- The window title bar (`src/main.c`, `update_window_title`) now shows
  `c64m - Running`, `c64m - Paused (<reason>)` (e.g. `BRK`, `breakpoint`,
  `step`), or `c64m - Error`, sourced from the same `runtime_state` /
  `stop_reason` snapshot fields the debugger's status panel already used.
  `platform_window_set_title()` was added to `src/platform/platform.c` for
  this.

## Banking and memory

- RAM/ROM/banking/address decode are implemented.
- CPU-visible memory map is used for normal bus access and debugger Map source mode.
- Physical ROM and raw RAM debugger views are available through dedicated debug read paths.
- Visible `$D000-$DFFF` I/O writes update the mapped device and do not mutate RAM underneath; RAM under I/O remains accessible to VIC fetches and raw RAM/debug paths.
- Generic 8K/16K cartridge mapping is implemented for normal CRT hardware type
  0: ROML maps at `$8000-$9FFF`, ROMH maps at `$A000-$BFFF` for 16K mode,
  and EXROM/GAME line state derives the cartridge memory mode. CPU-visible
  reads and debugger Map reads see cartridge ROM; raw RAM reads still see the
  RAM underneath. Writes to cartridge ROM ranges update the underlying RAM and
  do not mutate cartridge ROM. Normal reset and config-apply reset preserve the
  attached cartridge.
- Loading a program that boots to BASIC and injects (PRG/T64 via
  `runtime_load_prg`, and BASIC/host binary via `runtime_load_bin` with
  `reset_first`) now calls `c64_detach_cartridge` before the reset. A reset alone
  preserves the cartridge, so without the detach the cartridge would take the
  reset vector and boot instead of BASIC, and the `$E38B` injection would never
  fire. Loading another CRT still replaces the cartridge; mounting a D64 does not
  detach (mounting is passive and does not reset/boot).
- The Reset command carries an optional `detach_cartridge` flag
  (`RUNTIME_COMMAND_RESET` / `runtime_client_reset_ex`). Plain
  `runtime_client_reset` keeps the cartridge (hardware-accurate: reset re-runs
  the cart). The frontend Reset button, when a cartridge is attached, opens a
  confirmation popup with an "Unmount cartridge on reset" checkbox (checked by
  default) so the user can drop back to BASIC to reach a mounted disk, or uncheck
  to keep the cart. This lets the "mount a disk while a cart is attached" flow
  become useful (mount disk, Reset with unmount, then LOAD). `cartridge_attached`
  is published in the machine-state snapshot so the frontend knows whether to
  prompt.
- VIC bank selection is cached from CIA #2 port state as an accepted optimization.
- CPU opcode writes maintain a 64K write-history table keyed by CPU-visible
  16-bit address. Each entry stores the last four opcode PCs in 16-bit lanes,
  oldest retained in bits 63..48 and newest in bits 15..0. Direct debugger
  writes, loader injection, reset/init writes, and other non-opcode writes are
  not recorded in this first version.

## Startup load behavior

- `--disk` / `-d <drive>=<image>` mounts D64 images at startup.
- `--crt <file>` loads a generic 8K/16K CRT cartridge at startup, resets with
  the cartridge attached, and runs.
- `--prg` / `-p <file>` loads any file as PRG on startup.
- `--basic` / `-B <file>` loads any file as a BASIC program on startup.
- File extension is irrelevant for `--prg` and `--basic`; the flag determines interpretation.
- `--autorun` / `-a` can be combined with `--disk`, `--prg`, or `--basic`.
- With `--prg` or `--basic`, autorun buffer-injects `RUN\r` after bytes land at `$E38B`.
- With `--disk 8=...`, autorun uses a two-phase `$E38B` trap: first `LOAD"*",8\r`, then `RUN\r`.
- Autorun paste uses `use_buffer=true`, writing PETSCII directly to `$0277-$00C6`.
- `--video PAL|NTSC`, `-P` / `--pal`, and `-N` / `--ntsc` apply after INI loading and override `[Video] standard` for the current launch.
- Invalid `--video` values fail startup with a clear error.

## Save-state serializer foundation

- Phase 1 of save-state support is implemented in `src/machine/c64_snapshot.{c,h}`.
  Phase 2 adds runtime-thread `RUNTIME_COMMAND_SAVE_STATE` /
  `RUNTIME_COMMAND_LOAD_STATE` dispatch plus `runtime_client_save_state()` /
  `runtime_client_load_state()`. Phase 3 adds frontend hooks:
  `Opt+Shift+>` quicksave, `Opt+Shift+<` quickload, `.c64state` drag/drop load,
  and Machine tab `State` `Save As...` / `Load...` dialogs. The Emulator config
  tab now exposes `Quicksave Folder`, persisted as `[state] quicksave_folder`
  with default `.`. Save-state files include optional frontend host metadata for
  keyboard-joystick layout/port. No CLI load-state path exists yet.
- The format is versioned and chunked: header plus tagged chunks for metadata,
  RAM/color RAM, bus banking/counters, CPU, VIC-II, CIA #1/#2, SID, machine
  controls, cartridge, and drive slots.
- The header records `C64_SNAPSHOT_CONTENT_REFERENCED` and external-object flags.
  v1 validates loaded BASIC/KERNAL/character ROM contents by hash instead of
  embedding ROM bytes. This leaves room for a later UI/runtime "self-contained"
  mode that embeds ROM/media/cart bytes and records
  `C64_SNAPSHOT_CONTENT_SELF_CONTAINED`.
- `c64_snapshot_load` is all-or-nothing: it parses into a temporary machine,
  validates magic/version/chunks/ROM hashes, and only then applies loaded state.
  Failed loads leave the target machine unchanged.
- Runtime save/load performs file I/O on the runtime thread, emits
  `RUNTIME_EVENT_SAVE_STATE_COMPLETE` / `RUNTIME_EVENT_LOAD_STATE_COMPLETE` on
  success, and uses normal `RUNTIME_EVENT_ERROR` messages on failure. Successful
  load publishes refreshed CPU, machine, and debug-frame state. Runtime save
  finishes any active deferred CPU trace or resumable CPU micro-instruction first, so
  UI/runtime save requests land on an instruction boundary even if the command arrived
  mid-instruction.
- The raw machine serializer still rejects mid-instruction cycle-stepping state
  (`pending_cpu_trace_active` or `cpu.micro_active`). CPU bus trace buffers and
  write-history are debugger scratch and are cleared/rebuilt rather than serialized.

### Save-state inventory

Serialize as mutable live state:

```text
- CPU core scalar state: PC/opcode PC, A/X/Y/SP/P, address/scratch registers,
  IRQ deferral bits, opcode-active/class, cycle/IRQ/NMI counters.
- RAM and color RAM, including RAM underneath I/O and cartridge ROM.
- Bus banking state: 6510 port direction/data, VIC bank cache, write counters,
  cartridge mapping flags/lines/mode, and generic ROML/ROMH bytes.
- VIC-II registers, timing, raster compare, BA expiry, display/bad-line state,
  VC/VCBASE/RC, video/color line latches, IRQ state, sprite sequencer/latches,
  vertical border state, and live/completed frame buffers.
- CIA #1/#2 registers, timer latches/counters/underflow/output pulse state,
  ICR flags/mask/counters, TOD/alarm/latch/coherent-read state, TOD cycle
  accumulators, CNT pulse, and configured TOD rates.
- SID register mirror, per-voice decoded registers, phase, 23-bit noise LFSR,
  ADSR envelope/state/fractional counter, filter/DC/output state, read-back
  shadows, last sample, and sample-output enable.
- Keyboard matrix rows, joystick ports, IEC pull-line mirrors, master clock,
  keyboard/RESTORE counters, RESTORE/NMI latch state, ready/config flags,
  instruction-complete and remaining-cycle bookkeeping.
- Mounted drive slot metadata, copied D64 image bytes, directory entries, disk
  title/id/type/free-block fields, and last mount result.
```

Reconstruct, reference, or validate externally:

```text
- BASIC, KERNAL, and character ROM bytes are not embedded in v1 snapshots.
  The loader requires the existing machine to have matching ROM hashes.
- Runtime-owned source paths and future path/hash manifests are not available
  in `machine/` yet; they belong in the later runtime command phase.
- Full self-contained snapshots are intentionally reserved for a later UI/runtime
  option and should use the existing content-mode/flags structure.
```

Do not serialize:

```text
- SDL/window/audio-device/frontend state.
- Runtime/frontend copied debugger snapshots and UI memory views.
- Live callbacks/pointers: CPU bus callbacks, bus device pointers, CIA input
  callbacks, memory-access callback, and 1541 back-pointers are rebound or
  preserved during load.
- CPU bus trace scratch (`last_cpu_trace`, `pending_cpu_trace`) and write-history.
- Full 1541 CPU/VIA/RAM/ROM state in v1; if 1541 emulation is active, load resets
  the drive-side emulator and documents that exact drive-side resume is deferred.
```

## Known limitations / deferred

- Exact RDY/AEC sub-cycle CPU pin timing is deferred.
- Perfect chip-revision/electrical behavior for unstable undocumented opcodes is deferred.
- Last-byte-on-bus behavior is deferred.
- Cartridge mappers beyond generic 8K/16K normal cartridges are deferred.
- Cartridge INI persistence, detach UI/status, cartridge RAM/flash writes, and
  freezer buttons are deferred.
- Save-state CLI, self-contained snapshot mode, and full 1541 state capture are
  deferred.
- The debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather than illegal-opcode mnemonics.
- No local Harte corpus or harness is present in the repository.

## Tests / smoke checks

- Local tests cover documented CPU execution, bus integration, trace timing,
  direct/indexed/indirect addressing, indirect-JMP wrap behavior, IRQ/NMI entry,
  banking, and BA read/write stalling.
- Local bus tests cover generic 8K/16K cartridge mapping, shadow-RAM writes,
  debugger Map visibility, detach, and reset persistence.
- `tests/machine/test_c64_snapshot.c` covers machine snapshot size/write,
  representative round-trip restore, byte-identical re-save, bad magic rejection,
  ROM hash mismatch rejection, failed-load all-or-nothing behavior, and
  mid-instruction save rejection.
- `tests/runtime/test_runtime_savestate.c` covers runtime client save/load
  commands, runtime-thread snapshot file I/O, successful restore through the
  public runtime event API, bad snapshot rejection, failed-load preservation of
  live machine state, ROM hash mismatch rejection, and runtime save after a
  one-cycle mid-instruction run.
- Local tests do not provide per-opcode undocumented Harte-style semantic coverage.
- Practical undocumented opcode coverage is sufficient for the current milestone.

## Files likely involved

- `src/machine/c6510.c`
- `src/machine/c6510_inln.h`
- `src/machine/c64*`
- `src/runtime/*`
- CPU, bus, banking, IRQ/NMI, and BA tests under `tests/`
