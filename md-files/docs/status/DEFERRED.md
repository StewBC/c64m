# Deferred and not implemented

This file centralizes known gaps so agents do not rediscover or misclassify them as accidental omissions.

## VIC-II

- Light pen registers `$D013/$D014` are stubbed; Phase F was skipped.
- Cycle-perfect video timing is not complete.
- Last-byte-on-bus open-bus behavior is not implemented.
- Unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access (`$3FFF` / `$39FF`) is now rendered for the region
  outside the vertical display window (needed for opened-border pictures); it is
  a per-mode approximation, not a full cycle-exact idle sequencer.
- Per-scanline `$D011`/badline (FLI-class) raster accuracy is deferred. The
  open-border "expose" reveal in `samples/dkarcade2016.prg` depends on it and is
  not reproduced; see [VICII_EXPOSE_REVEAL.md](VICII_EXPOSE_REVEAL.md).
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

- Real 1541 DOS sector writes are supported via the job-level WRITE intercept
  (C64IEC1541PHASE_4): SAVE, sequential/relative file writes, and BAM/directory
  updates persist to writable images while the drive ROM is active. D64 PRG SAVE
  is additionally supported through the compatibility KERNAL SAVE trap when the
  1541 ROM is absent. Still deferred: track-level operations (format) and
  media-level write fidelity (VIA #1 head / GCR / rotation) and G64.
- DOS command channel (scratch/rename/format/validate/initialize) and the
  error/status channel work via the real 1541 ROM when `emulate_1541=1`
  (C64IEC1541PHASE_5). Format is handled by a FORMT EXECUTE-job intercept.
  Still deferred: media-level format fidelity, cross-drive copy (`C`), and
  block/memory-execute commands (`B-*`/`M-*`). In the KERNAL-trap world
  (`emulate_1541=0`) there is no command/error channel — SAVE trap only.
- Fast loaders are not broadly validated; loaders that require unmodeled
  disk-controller VIA motor/SYNC/head behavior or nonstandard drive ROM behavior may fail.
- Devices beyond 8/9 are not implemented.
- Full Commodore DOS pattern/type suffix semantics are not implemented.

## Debugger / UI

- Phase 13 deferred breakpoint actions: Type, Swap, and trace output/details.
- Basic Text load/save (`util/basic_v2`) handles stock BASIC V2 only. Extension
  dialects (Simon's BASIC and other cartridge/extension token sets) are not
  tokenized or detokenized. Non-printable bytes (control/colour codes, `π`,
  graphics) round-trip losslessly through `{name}`/`{$hh}` escapes, but source is
  normalized to the uppercase/graphics character set, so lowercase-mode listings
  do not round-trip to the same display case. Load assumes a `$0801` start with
  lines in ascending order (no insertion-sort/renumber).

## Timing

- Cycle-perfect video/audio timing is not complete.
- Exact RDY/AEC sub-cycle CPU pin timing is not modeled.
