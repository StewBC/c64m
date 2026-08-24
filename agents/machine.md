# Machine, CPU, bus, and snapshots

## Source of truth

`src/machine/c64.{c,h}`, `c64_bus.{c,h}`, `c6510.{c,h}`, `c6510_inln.h`.
Tests under `tests/machine/`.

Start at `c64_init()`, `c64_set_config()`, `c64_install_roms()`, `c64_reset()`,
`c64_step_cycle()`, `c64_step_instruction()`. Bus callbacks in `c64.c` are the
CPU / C64 decode boundary. `c64_copy_*_snapshot` and `c64_debug_read_*` are the
supported inspection APIs. Normal bus reads can have I/O side effects.

`c64_t` owns RAM, color RAM, ROMs, banking, CPU, VIC-II, both CIAs, SID,
keyboard/joystick, IEC, drive slots, cartridge state, and the master cycle.

## Phi2 order

`c64_step_cycle_internal` / `c64_step_cycle_micro_hot`:

1. Snapshot `bus.cpu_open_bus_pc = cpu.pc`
2. `vicii_begin_cycle` (Phi1 + BA/AEC for this cycle)
3. Between-instruction: KERNAL trap, interrupt poll, `c6510_micro_begin`
4. CIA1, CIA2, SID +1, VIC IRQ delay
5. CPU Phi2, or BA stall (drive still runs)
6. `vicii_finish_cycle`, then `clock.cycle++`

Instruction stepping uses the same arbiter. A fetched BRK auto-pauses
free-running runtime loops before execution (`C64_STEP_STOP_BEFORE_BRK`).
Manual single-step still executes BRK as hardware.

## BA / RDY / AEC

- AEC low: no CPU access, including writes.
- RDY low stalls **reads**. Writes (`DATA_WRITE`, `RMW_DUMMY_WRITE`,
  `STACK_WRITE`) may complete while AEC is high.
- BA lead is 3 cycles. Until elapsed, a forced VIC c-access stores `vbuf=$ff`
  and `cbuf = ram[cpu_open_bus_pc] & 0x0f`. That is the only open-bus model.
- When the 6510 is BA-stalled **between instructions** and an interrupt is
  pending, the resume cycle is the interrupt's dummy opcode fetch; the
  sequence starts the **following** cycle. `cpu_prev_between_stall` /
  `cpu_deferred_interrupt` carry this. One cycle early on that boundary broke
  Edge of Disgrace's FLD scroller (`pad = ($28-$DC04)&7` wrapping `0 -> 7`).
  Mid-instruction stalls are unaffected.

IRQ is CIA #1 `cia_interrupt_line()` (delayed pin) OR VIC after a 2-cycle
`vic_irq_delay`. Do not sample `cia_irq_pending()` for the CPU pin. NMI is
RESTORE one-shot or CIA #2 delayed-pin **edge**; NMI is polled before IRQ.

I/O writes `$D000-$DFFF` do not overwrite RAM underneath. VIC fetches and raw
RAM debugger reads still see that RAM.

PAL 985248 Hz (63x312). NTSC 1022727 Hz (65x263). Drive clock is a fixed
1.000 MHz via `c64_clock.drive_accum`. IEC **read** `$DD00` syncs the drive to
`clock.cycle`; **write** `$DD00/$DD02` syncs to `cycle+1`.

Do not project CIA timers in deferred I/O reads (`c64_deferred_io_read`).
Projecting `$DD04` broke stable-raster. VIC `$D011/$D012` **are** projected;
`$D01E/$D01F` must be live.

## CPU

Documented NMOS 6510 plus resumable undocumented families SLO, RLA, SRE, RRA,
DCP, ISC/ISB, LAX, SAX. Unstable forms (XAA, AHX, SHX, SHY, TAS, LAS, LAX #imm,
JAM) use the compatibility executor. All 256 opcodes have explicit dispatch.

`c64_set_cpu_observer()` is the machine observation boundary used by the
runtime flight recorder. It survives `c64_reset()` but is host state: not
serialized. `begin` fires after the machine has committed to the path.
`access` reports resolved bus events (deferred compatibility suppresses
speculative callbacks). `complete` fires once. `host_trap` is successful
KERNAL LOAD/SAVE traps only. Cycle-step and instruction-step traces match.
The runtime records the main 6510 only.

## Cartridges

Attach via `c64_attach_*_cartridge` / `c64_bus_attach_*`. Parser is
`src/tools/crt/`. Cartridge ROM is read-only; writes hit shadow RAM. Plain
reset keeps the cart and re-applies bank 0. PRG/BASIC/T64 inject detaches
first.

| Type | Hardware | Bank select |
|------|----------|-------------|
| 0 Normal | 8K ROML, optional 16K ROMH | EXROM/GAME |
| 5 Ocean | multi-bank 8K, max 64 | write IO1; 512K => 8K ROML else 16K mirrored. CRT GAME ignored. PLA: ROML needs LORAM+HIRAM; 16K ROMH needs HIRAM (`$01=$25` shows RAM) |
| 7 Fun Play | 16 x 8K ROML | IO1 write scrambled; `(v & $C6)==$86` disables ROM |
| 8 Super Games | 4 x 16K | IO2: bits 0-1 bank, bit 2 disable, bit 3 write-protect latch |
| 15 C64GS | up to 64 x 8K ROML | any IO1 access latches `address & $3F` |
| 17 Dinamic | 16 x 8K ROML | IO1 **read** `$DE00-$DE0F` latches `address & $0F` |
| 19 Magic Desk | multi-bank 8K ROML, max 128 | write IO1 bits 0-6 bank, bit 7 disable |

Loaders: `runtime_client_load_prg()` (PRG/T64), `runtime_client_load_crt()`,
`runtime_client_load_bin()`, `runtime_client_load_state()`. CLI `--sna`
wins over `--crt`/`--prg`/`--basic`.

## Snapshots

`c64_snapshot.{c,h}`: versioned, chunked, all-or-nothing. Format version 13;
`VERSION_MIN` is 13. Older files are rejected and the machine is left
untouched.

Includes CPU, RAM/color RAM, banking, VIC chip state (not paint buffers), CIA
pin pipeline, SID, controls, cart, and **powered** 1541 cores (`DR8C`/`DR9C`:
CPU micro, VIAs, RAM, media scalars, GCR tracks). Unpowered units are a
`powered=false` stub. Unmounted carts are a one-byte `CART` stub. C64 ROMs
are hashed/referenced, not embedded; 1541 ROM stays host-side.

Save refuses if `micro_active` or a pending CPU trace is active. Load clears
host 6510 micro/BA-defer fields. Paint buffers are zeroed; the first correct
picture is the next completed frame. Observer pointer and HST1 are excluded.
Runtime load-state clears history and Inspector tape and starts a new epoch.

The main loop may append a trailing `HOST` chunk (keyboard joystick, disk
path queues) that the machine loader ignores.

## Do not claim

Perfect electrical RDY/AEC, exact chip-revision unstable opcodes, last-byte-
on-bus, Ultimax attach, or mappers beyond the table above.
