# Deferred and not implemented

This file centralizes known gaps so agents do not rediscover or misclassify them as accidental omissions.

## VIC-II

- Light pen registers `$D013/$D014` are stubbed; Phase F was skipped.
- Cycle-perfect video timing is not complete. The current Phi2 arbiter and
  resumable CPU subset improve BA-visible timing, and the VIC-II now schedules
  character, graphics, idle, sprite-pointer, and sprite-data fetch types. BA
  is derived from the scheduled CPU-visible Phi2 accesses, but electrical
  RDY/AEC sub-cycle detail remains outside this model.
- Last-byte-on-bus open-bus behavior is not implemented.
- Unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access (`$3FFF` / `$39FF`) is now rendered for the region
  outside the vertical display window (needed for opened-border pictures); it is
  a per-mode approximation, not a full cycle-exact idle sequencer.
- Broader FLI-class mid-line `$D011`/badline accuracy beyond what ordinary
  software needs is still not claimed. The dkarcade2016 PAL "expose" reveal is
  fixed (sprite BA window = 6 cycles + deferred `$D012` projection); see
  [../../C64MVICIIEXNEXT_UPD.md](../../C64MVICIIEXNEXT_UPD.md).
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
  full 1541 CPU/VIA/drive-side state capture (including media GCR track buffers)
  are deferred.
- Debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather than illegal-opcode mnemonics.
- No local Harte corpus or harness is present.

## Disk / IEC

- Real 1541 DOS sector writes are supported via the job-level WRITE intercept
  (C64IEC1541PHASE_4): SAVE, sequential/relative file writes, and BAM/directory
  updates persist to writable images while the drive ROM is active. D64 PRG SAVE
  is additionally supported through the compatibility KERNAL SAVE trap when the
  1541 ROM is absent.
- Opt-in media path (`[disk] media_1541=1`, see `c64m1541media.md` M0–M8): GCR
  track synthesis from D64, G64 read-only mount, rotation, SYNC, disk-controller
  VIA motor/stepper/WPS, Port A GCR read, BYTE READY→SO, hybrid WRITE/FORMT,
  v1 loader matrix. Still deferred: pure job-level Port A write fidelity, G64
  write-back, broad commercial/protection/fast-loader playthroughs (matrix
  expansion).
- DOS command channel (scratch/rename/format/validate/initialize) and the
  error/status channel work via the real 1541 ROM when `emulate_1541=1`
  (C64IEC1541PHASE_5). Format is handled by a FORMT EXECUTE-job intercept.
  Still deferred: media-level format fidelity, cross-drive copy (`C`), and
  block/memory-execute commands (`B-*`/`M-*`). In the KERNAL-trap world
  (`emulate_1541=0`) there is no command/error channel — SAVE trap only.
- Fast loaders are not broadly validated; loaders that require unmodeled write
  timing, G64-only layouts, or nonstandard drive ROM behavior may fail.
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
- The in-app file browser dialog (`docs/status/FRONTEND_DEBUGGER.md` § "File
  browser dialog") has no Windows drive-letter enumeration/switching UI; typing a
  full path including a different drive letter into the Path field is the
  workaround. (Keyboard navigation and remembered per-slot directories are now
  implemented.)

## Timing

- Cycle-perfect video/audio timing is not complete.
- Exact RDY/AEC sub-cycle CPU pin timing is not modeled.
