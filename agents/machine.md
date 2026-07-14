# Machine, CPU, bus, and snapshots

## Source of truth

Primary files are `src/machine/c64.{c,h}`, `c64_bus.{c,h}`, `c6510.{c,h}` and
`c6510_inln.h`. Tests are under `tests/machine/` and `tests/runtime/`.

Start with `c64_init()`, `c64_set_config()`, `c64_install_roms()`, `c64_reset()`,
`c64_step_cycle()`, and `c64_step_instruction()`. The bus callbacks in `c64.c` are
the boundary between the CPU and C64-visible address decoding. The
`c64_copy_*_snapshot` functions are the supported read-only inspection boundary.

## Current behavior

- `c64_t` owns RAM, color RAM, ROMs, banking, CPU, VIC-II, both CIAs, SID,
  keyboard/joystick state, IEC state, drive slots, cartridge state, and the
  monotonic master cycle.
- CPU bus access is arbitrated with VIC-II Phi2 ownership. BA/RDY stalls reads;
  AEC prevents CPU bus access during an actual VIC takeover. Pending CPU writes
  and RMW writes retain their timing behavior.
- Instruction stepping and cycle stepping use the same Phi2 arbitration path.
- The documented NMOS 6502/6510 instruction set has resumable microcycle paths.
  Practical undocumented families SLO, RLA, SRE, RRA, DCP, ISC/ISB, LAX, and SAX
  have resumable paths. Chip-dependent unstable forms use compatibility replay;
  all 256 opcode slots have explicit dispatch.
- IRQ is the OR of VIC-II and CIA #1 sources. CIA #2 and RESTORE use separate NMI
  sources; CIA #2 goes through the CPU NMI edge latch. NMI is sampled before IRQ
  at instruction entry.
- A fetched BRK auto-pauses free-running runtime loops before execution. Manual
  single-step still executes BRK as real 6502 hardware. The CPU BRK implementation
  pushes the return state and vectors through `$FFFE/$FFFF` when actually run.
- CPU-visible `$D000-$DFFF` I/O writes do not overwrite RAM underneath. VIC fetches
  and raw RAM debugger reads still see the underlying RAM.
- PAL and NTSC are selected in `c64_config`; clock and frame constants are exposed
  by `c64_config_clock_hz()` and `c64_config_cycles_per_frame()`.

## Cartridges and startup

- Generic CRT hardware type 0 supports 8K ROML at `$8000-$9FFF` and 16K ROMH at
  `$A000-$BFFF`; EXROM/GAME mapping is modeled.
- Cartridge ROM is read-only; writes update shadow RAM underneath. Plain reset
  preserves a cartridge. PRG/BASIC/T64 injection detaches it first. The frontend
  reset flow can explicitly detach or preserve it.
- CLI startup supports `--disk`, `--crt`, `--prg`, `--basic`, `--autorun`, and
  `--video PAL|NTSC`.

The loader distinction matters: `runtime_client_load_prg()` handles PRG/T64-style
content, `runtime_client_load_crt()` attaches a cartridge and resets with it, and
`runtime_client_load_bin()` is the generic host binary/BASIC path with explicit
address, file-header, reset, and BASIC flags. For debugger reads use
`c64_debug_read_cpu_map`, `c64_debug_read_ram`, `c64_debug_read_rom`, and
`c64_debug_read_drive_map`; normal bus reads can have I/O side effects.

## Save states

`c64_snapshot.{c,h}` provides a versioned, chunked, all-or-nothing machine
serializer. It includes CPU, RAM/color RAM, banking, VIC-II, CIA, SID, controls,
cartridge, and D64 drive-slot data. ROM bytes are referenced and hash-validated;
full 1541 CPU/VIA state, SDL/frontend/runtime state, CLI loading, and self-contained
ROM/media embedding are not part of the current format.

## Do not claim

Do not claim perfect electrical RDY/AEC timing, exact chip-revision behavior for
unstable opcodes, last-byte-on-bus behavior, broader cartridge mappers, or full
1541 drive-state restoration.

## Change checklist

Timing/address changes should be checked against the bus-trace and PAL/NTSC
baseline tests. Serializer changes require the chunk inventory and all-or-nothing
failure cases. Loader changes require cartridge detach/reset and runtime loader
coverage.
