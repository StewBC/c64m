# CPU and machine status

## Current implementation

- Core C64 runtime is implemented.
- 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction stepping, and frame handoff are implemented.
- Real 64C ROM execution reaches BASIC READY with visible cursor and keyboard input.
- Machine owns monotonic master cycle.
- Machine coordinates CPU bus events with VIC, CIA, and SID hooks.

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

## Known limitations / deferred

- Exact RDY/AEC sub-cycle CPU pin timing is deferred.
- Perfect chip-revision/electrical behavior for unstable undocumented opcodes is deferred.
- Last-byte-on-bus behavior is deferred.
- Cartridge mappers beyond generic 8K/16K normal cartridges are deferred.
- Cartridge INI persistence, detach UI/status, cartridge RAM/flash writes, and
  freezer buttons are deferred.
- The debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather than illegal-opcode mnemonics.
- No local Harte corpus or harness is present in the repository.

## Tests / smoke checks

- Local tests cover documented CPU execution, bus integration, trace timing, IRQ/NMI entry, banking, and BA read/write stalling.
- Local bus tests cover generic 8K/16K cartridge mapping, shadow-RAM writes,
  debugger Map visibility, detach, and reset persistence.
- Local tests do not provide per-opcode undocumented Harte-style semantic coverage.
- Practical undocumented opcode coverage is sufficient for the current milestone.

## Files likely involved

- `src/machine/c6510.c`
- `src/machine/c6510_inln.h`
- `src/machine/c64*`
- `src/runtime/*`
- CPU, bus, banking, IRQ/NMI, and BA tests under `tests/`
