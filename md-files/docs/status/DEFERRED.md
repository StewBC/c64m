# Deferred and not implemented

This file centralizes known gaps so agents do not rediscover or misclassify them as accidental omissions.

## VIC-II

- Light pen registers `$D013/$D014` are stubbed; Phase F was skipped.
- General cycle-perfect video timing is not complete. The current Phi2 arbiter and
  resumable CPU subset improve BA-visible timing, and the VIC-II now schedules
  character, graphics, idle, sprite-pointer, and sprite-data fetch types. BA
  is derived from the scheduled CPU-visible Phi2 accesses. AEC/RDY are modeled
  at cycle granularity; analog/half-cycle electrical detail and unbounded
  demo-scene coverage remain outside this model. The selected `lft-nine`
  sprite/raster timing path is in-scope and implemented in the live path.
- Last-byte-on-bus open-bus behavior is not implemented.
- Unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access (`$3FFF` / `$39FF`) is now rendered for the region
  outside the vertical display window (needed for opened-border pictures); it is
  a per-mode approximation, not a full cycle-exact idle sequencer.
- Broader FLI-class mid-line `$D011`/badline accuracy beyond what ordinary
  software needs is still not claimed. The dkarcade2016 PAL "expose" reveal is
  fixed (sprite BA window = 6 cycles + deferred `$D012` projection); see
  [../../C64MVICIIEXNEXT_UPD.md](../../C64MVICIIEXNEXT_UPD.md).
- Exact analog or half-cycle RDY/AEC waveform timing is deferred.

## CIA

- FLAG (ICR bit 4), serial SDR/CNT/SP shift (ICR bit 3), PC handshake, delayed
  interrupt-line model, and **Option-2 CPU wiring** (`cia_interrupt_line` drives
  CIA #1 IRQ and CIA #2 NMI edge) are implemented (`C64MFULL_CIA.md`).
- **Corpus / remaining fidelity:** VICE + hardware reference and c64m PRG runner
  under `md-files/corpus/cia-timing/` (`run_c64m.sh`). Still deferred: greening
  the c64m priority FAIL matrix, cycle-stamped dual-emulator logs, explicit
  6526 vs 6526A vs 8521 variant policy, and sub-cycle SP/CNT analog edge timing.
- The FLAG/SP/PC machine-side seams are not yet wired to concrete peripherals
  (cassette FLAG, RS-232, user-port handshake); tape (`.TAP`) and RS-232 work
  will consume them.

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
- Exact analog or half-cycle RDY/AEC waveform behavior is not modeled.
