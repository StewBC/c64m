# Disk, IEC, and 1541

## Formats

Parsers in `src/tools/d64`, `t64`, `g64`, `crt`. Machine/runtime integration
in `c64.c`, `c1541.c`, `c1541_media.c`, runtime disk code.

- D64: 35-track, error tails, BAM/directory, PRG extract/write, wildcards,
  `@:` replacement.
- Devices 8 and 9 have independent ordered disk queues. Images are
  read-only by default. Writable KERNAL SAVE updates the in-memory image;
  runtime flushes to the host path. Failed flushes leave the image dirty.
- T64 is host convenience: first loadable entry. No Datasette, no T64
  KERNAL trap.
- CRT is not disk I/O. Mapper list and attach rules: `machine.md`.

Runtime owns host flushing. Machine mutates the in-memory image and marks
the slot dirty. Devices other than 8 and 9 must be rejected.

Entry points: `c64_mount_d64_ex()`, `c64_mount_g64()`,
`c64_set_drive_writable()`, `c64_unmount_drive()`, `c64_copy_drive_status()`,
matching `runtime_client_*`.

## Three load paths

1. **KERNAL trap** at `$FFD5`/`$FFD8` when `emulate_1541` is off or no 1541
   ROM is loaded. D64 PRG (and `$` directory) only. G64 has no trap path.
2. **Real 1541 ROM + IEC** when `[disk] emulate_1541=1` and a 16 KiB DOS 2.6
   ROM is present (`[roms] 1541` or `1541.rom` next to the binary / in
   `rom` / `roms`). Drive 6502, RAM, two VIAs, IEC, fractional 1.000 MHz
   drive clock. D64 sector READ/SEARCH/WRITE jobs are intercepted at the
   DOS job layer unless media mode is on.
3. **Media GCR** when `media_1541=1` (requires `emulate_1541=1`): D64-to-GCR
   synthesis or G64 attach, rotation/SYNC/BYTE READY, motor/stepper/WPS.
   Physical READ/SEARCH/VERIFY run the ROM path. D64 WRITE is hybrid
   (sector + GCR poke). G64 WRITE is Port-A flux only.

DOS command/error channel (scratch, rename, validate, initialize, format,
status) goes through the ROM plus the FORMT intercept.

Validated fast-loader samples: Arkanoid V-MAX PAL and Robocop NTSC
load-to-game. That is not broad commercial coverage.

## Soft power

Each unit has a sticky `c64_drive_slot.powered` latch.

| Event | Effect |
|-------|--------|
| Cold start | Off: not stepped, does not pull IEC |
| First successful D64/G64 mount | Powers on (DOS reset if ROM loaded) |
| Explicit power-on (UI / `power-drive` / CLI `-d N=`) | On, even without media |
| Eject / unmount | Media cleared; **stays powered** |
| Power-off | Ejects media, then powers off |

Loading a 1541 ROM is not power-on. An idle powered 1541 still answers ATN
by pulling DATA (`DATA = PB1 | (ATN XOR ATNA)`). A drive the user never
asked for therefore clamps DATA on every ATN, which destroyed Edge of
Disgrace's post-swap streaming depacker. Keep unused units (especially
device 9) cold.

## G64 write-back

G64 mounts are read-only by default. Writable requires `media_1541`. Live
bit ring is `halves[].data`; host blob is `slot->image_bytes`. Export is
copy-only (no phase-rotate of the live ring). Triggers: leave write gate,
seek-off-dirty, unmount, media disable. Length-preserving in-place patch;
no empty-slot grow / format rebuild.

## Lessons that stay load-bearing

- Job `$E0` (EXECUTE) jumps into the job buffer. With `media_1541=1` it
  must **not** complete as synthetic `format_track()`: that froze
  multi-stage loaders after a disk swap. Sector-intercept mode (media off)
  still maps EXECUTE to hybrid D64 track erase for FORMT tests.
- Disk swap starts a VICE-style attach blanking window
  (`C1541_MEDIA_ATTACH_DELAY`).
- Runtime `-a` autorun injects `LOAD"*",8` / `RUN` only when mounting into
  an **empty** device 8. Replacing an already-mounted image is a swap and
  must not re-arm autorun: injecting into the KERNAL keyboard buffer can
  overwrite live loader code at `$0277-$0280`.
- G64 uses the flux-transition decoder; synthetic D64 uses NRZ GCR +
  immediate BYTE READY. BYTE READY is a sticky SO edge after drive Phi2;
  **CLV discards any pending edge**. Dual-BVC loaders require that.
- While PC is in drive RAM and no job is queued, VIA2 T1 is acked so
  custom code is not stolen by `$F2B0` (Robocop). Intentional, not
  hardware-accurate.
- Full drive-object save-state is snapshot v13 `DR8C`/`DR9C` for **powered**
  units only.

When a real-1541 load fails, inspect `get-drive-cpu`, ROM-loaded and media
state, and whether the KERNAL trap ran. Do not infer success from a host
RUN log.

```sh
./build/c64m --disk 8=path/to/game.d64 --autorun
./build/c64m --crt path/to/cart.crt
```
