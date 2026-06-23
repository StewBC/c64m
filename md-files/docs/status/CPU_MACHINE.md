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

## Banking and memory

- RAM/ROM/banking/address decode are implemented.
- CPU-visible memory map is used for normal bus access and debugger Map source mode.
- Physical ROM and raw RAM debugger views are available through dedicated debug read paths.
- VIC bank selection is cached from CIA #2 port state as an accepted optimization.
- CPU opcode writes maintain a 64K write-history table keyed by CPU-visible
  16-bit address. Each entry stores the last four opcode PCs in 16-bit lanes,
  oldest retained in bits 63..48 and newest in bits 15..0. Direct debugger
  writes, loader injection, reset/init writes, and other non-opcode writes are
  not recorded in this first version.

## Startup load behavior

- `--disk` / `-d <drive>=<image>` mounts D64 images at startup.
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
- The debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather than illegal-opcode mnemonics.
- No local Harte corpus or harness is present in the repository.

## Tests / smoke checks

- Local tests cover documented CPU execution, bus integration, trace timing, IRQ/NMI entry, banking, and BA read/write stalling.
- Local tests do not provide per-opcode undocumented Harte-style semantic coverage.
- Practical undocumented opcode coverage is sufficient for the current milestone.

## Files likely involved

- `src/machine/c6510.c`
- `src/machine/c6510_inln.h`
- `src/machine/c64*`
- `src/runtime/*`
- CPU, bus, banking, IRQ/NMI, and BA tests under `tests/`
