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
- When the 6510 is BA-stalled *between instructions* (RDY low at an instruction
  boundary) and an interrupt is pending, the resume cycle is the interrupt's
  opcode (dummy) fetch and the interrupt sequence begins the *following* cycle -
  one cycle later than a same-cycle begin. This matches VICE (`DO_INTERRUPT`'s
  leading `FETCH_PARAM_DUMMY` absorbs the BA steal) and the mid-instruction-stall
  path, where the stalled read occupies the resume cycle before the interrupt.
  `cpu_prev_between_stall` / `cpu_deferred_interrupt` carry this across the two
  cycles. Without it the IRQ enters one cycle early on that boundary, which read
  a stable-raster timer one cycle early and broke EoD's FLD scroller (a
  `pad = ($28-$DC04)&7` residual wrapping `0 -> 7`). Mid-instruction stalls are
  unaffected.
- A fetched BRK auto-pauses free-running runtime loops before execution. Manual
  single-step still executes BRK as real 6502 hardware. The CPU BRK implementation
  pushes the return state and vectors through `$FFFE/$FFFF` when actually run.
- CPU-visible `$D000-$DFFF` I/O writes do not overwrite RAM underneath. VIC fetches
  and raw RAM debugger reads still see the underlying RAM.
- PAL and NTSC are selected in `c64_config`; clock and frame constants are exposed
  by `c64_config_clock_hz()` and `c64_config_cycles_per_frame()`.

## CPU observation contract

`c64_set_cpu_observer()` installs the machine-level observation boundary used by
the runtime CPU flight recorder. The observer survives `c64_reset()` but is host
state: it is neither serialized nor restored by machine snapshots.

- `begin` reports the pre-entry CPU state for a normal instruction, IRQ, or NMI.
  It fires only after the machine has committed to that execution path.
- `access` reports actual CPU bus accesses with their resolved address, value,
  read/write direction, and `c6510_bus_access_kind`. Deferred compatibility
  execution suppresses speculative callbacks and reports each replayed access
  once.
- `complete` fires once when the matching instruction or interrupt entry
  finishes.
- `host_trap` reports only successful KERNAL LOAD/SAVE host traps.

Instruction stepping and repeated cycle stepping produce the same observer
trace. Reset-vector reads are outside any execution record. The existing memory
access callback and breakpoint behavior remain available independently.

The runtime owns the recorder and records only the main C64 6510; drive CPU
execution is outside this feature.

## Cartridges and startup

- **CRT type 0 (Normal):** 8K ROML at `$8000-$9FFF` and optional 16K ROMH at
  `$A000-$BFFF`; EXROM/GAME mapping is modeled.
- **CRT type 5 (Ocean type 1):** multi-bank 8K banks (max 64). Write to IO1
  `$DE00–$DEFF` selects bank (`value & mask & $3F`); bit 7 is ignored. Mode follows
  image size only (VICE): 512K => 8K ROML; otherwise 16K with the same bank mirrored
  at ROML+ROMH (CRT GAME is ignored). PLA: ROML needs LORAM+HIRAM; 16K ROMH needs
  HIRAM — so `$01=$25` shows underlay RAM for Ocean loaders. Power-on / plain reset: bank 0.
- **CRT type 19 (Magic Desk / Domark / HES):** multi-bank 8K ROML at `$8000-$9FFF`
  (EXROM=0, GAME=1). Bank select is write-only at IO1 `$DE00–$DEFF`: bits 0–6
  select bank (masked per VICE bankmask from highest bank index), bit 7 disables
  cart ROM so RAM appears at `$8000`. Power-on / plain reset: bank 0, cart enabled.
  Up to 128 × 8 KiB banks (VICE max). See `crt-type19-plan.md` for the mapper roadmap.
- Cartridge ROM is read-only; writes update shadow RAM underneath. Plain reset
  preserves a cartridge (Magic Desk re-applies bank 0). PRG/BASIC/T64 injection
  detaches it first. The frontend reset flow can explicitly detach or preserve it.
- CLI startup supports `--disk`, `--crt`, `--prg`, `--basic`, `--sna`, `--autorun`,
  and `--video PAL|NTSC`. `--sna <path>` loads a `.c64state` snapshot at startup
  (takes priority over `--crt`/`--prg`/`--basic` when present). Control-port
  equivalents are `load-state` / `save-state`.

The loader distinction matters: `runtime_client_load_prg()` handles PRG/T64-style
content, `runtime_client_load_crt()` attaches a cartridge and resets with it,
`runtime_client_load_bin()` is the generic host binary/BASIC path with explicit
address, file-header, reset, and BASIC flags, and `runtime_client_load_state()` /
`runtime_client_save_state()` restore or write machine snapshots. For debugger
reads use `c64_debug_read_cpu_map`, `c64_debug_read_ram`, `c64_debug_read_rom`, and
`c64_debug_read_drive_map`; normal bus reads can have I/O side effects.

## Save states

`c64_snapshot.{c,h}` provides a versioned, chunked, all-or-nothing machine
serializer (format version 13). It includes CPU, RAM/color RAM, banking, VIC-II
chip state (not the ARGB paint buffers), CIA, SID, controls, cartridge, and
per-drive slot data. When real 1541 emulation is on with a drive ROM loaded,
**powered** units also store full live 1541 drive-object state (CPU including mid-
instruction micro fields, both VIAs, 2 KiB RAM, media scalars, and verbatim GCR
track buffers) as `DR8C`/`DR9C`, plus `clock.drive_accum` / `drive_synced_cycle`.
Unpowered units store only a one-byte `DRV*` stub (`powered=false`); power-off
already ejects media and power-on resets the 1541, so cold core state is not
load-bearing. Unmounted carts are a one-byte `CART` stub; mounted carts store
hardware type, bank count/mask/IO latch, mode lines, multi-bank ROML blob, and
optional ROMH. C64 ROM bytes are referenced and hash-validated; 1541 ROM bytes
stay host-side (not embedded). SDL/frontend/runtime presentation state is mostly
out of band; the main loop appends a trailing `HOST` chunk (ignored by the
machine loader) for keyboard joystick layout/port and disk path queues for
devices 8/9. CLI loading and self-contained ROM/media embedding are not part of
the machine format.

Versions 12 and earlier are **sunset** (no migration). `VERSION_MIN` moves with
`VERSION`; the header check rejects older files and leaves the machine untouched
(`test_legacy_versions_rejected`). The reader carries no pre-v13 branches. The
`1541_STATE_DEFERRED` flag path stays live - a current-version snapshot saved
without the included-core flag still hard-resets the drives on load
(`test_synthetic_deferred_resets_drives`).

Paint buffers are zeroed on load; the first correct picture is the next completed
frame. That is intentional - frames are display cache, not machine state.

The CPU observer pointer and all flight-recorder state are also excluded from
snapshots. A successful runtime load-state clears history and begins a new epoch;
save-state does not alter it.

CIA chunks store the delayed IRQ/NMI pin pipeline (`interrupt_ff`,
`interrupt_pending_latched`, `interrupt_line`).

## Do not claim

Do not claim perfect electrical RDY/AEC timing, exact chip-revision behavior for
unstable opcodes, last-byte-on-bus behavior, or broader cartridge mappers.

## Change checklist

Timing/address changes should be checked against the bus-trace and PAL/NTSC
baseline tests. Serializer changes require the chunk inventory and all-or-nothing
failure cases. Loader changes require cartridge detach/reset and runtime loader
coverage.
