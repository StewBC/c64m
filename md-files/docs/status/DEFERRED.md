# Deferred and not implemented

This file centralizes known gaps so agents do not rediscover or misclassify them as accidental omissions.

## VIC-II

- Light pen registers `$D013/$D014` are stubbed; Phase F was skipped.
- Cycle-perfect video timing is not complete.
- Last-byte-on-bus open-bus behavior is not implemented.
- Unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access fetch behavior from `$3FFF` / `$39FF` in renderer is deferred.
- Exact RDY/AEC sub-cycle CPU pin timing is deferred.

## CIA

- Full CIA accuracy and pin/race-level timing are deferred.

## SID

- Exact 6581/8580 analog combined-waveform blending is deferred.
- Paddle/potentiometer behavior is not connected; `$D419/$D41A` return 0xFF.
- Further high-frequency audio work should be a new measured SID/audio fidelity phase.

## CPU / machine

- Perfect chip-revision/electrical behavior for unstable undocumented opcodes is deferred.
- Last-byte-on-bus and analog-dependent CPU perfection are deferred.
- Cartridge mappers beyond generic 8K/16K normal CRT cartridges are deferred.
- Cartridge INI persistence, detach UI/status, cartridge RAM/flash writes, and
  freezer buttons are deferred.
- Save-state CLI, self-contained snapshot embedding, cross-version migration, and
  full 1541 CPU/VIA/drive-side state capture are deferred.
- Debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather than illegal-opcode mnemonics.
- No local Harte corpus or harness is present.

## Disk / IEC

- D64 writes are not implemented.
- SAVE to disk is not implemented.
- Error channel is not implemented.
- Fast loaders are not broadly validated; loaders that require unmodeled
  disk-controller VIA motor/SYNC/head behavior or nonstandard drive ROM behavior may fail.
- Devices beyond 8/9 are not implemented.
- Full Commodore DOS pattern/type suffix semantics are not implemented.

## Debugger / UI

- Phase 13 deferred breakpoint actions: Type, Swap, and trace output/details.

## Timing

- Cycle-perfect video/audio timing is not complete.
- Exact RDY/AEC sub-cycle CPU pin timing is not modeled.
