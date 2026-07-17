# Disk, file formats, and IEC/1541 handoff

## Host formats and compatibility path

Tools live in `src/tools/d64`, `t64`, `crt`, and `g64`; machine/runtime integration
is in `src/machine/c64.c`, `c1541.c`, `c1541_media.c`, and runtime disk code.

- D64 supports standard 35-track images, common error-info tails, BAM/directory
  parsing, PRG extraction, wildcard matching, directory synthesis, PRG writes,
  BAM allocation, and `@:` replacement.
- Devices 8 and 9 have independent ordered disk queues. Images are read-only by
  default. Writable KERNAL SAVE updates the in-memory image and runtime flushes it
  to the host path; failed flushes leave the image dirty and report an error.
- The compatibility KERNAL LOAD/SAVE traps support D64 PRG operations. This is the
  fallback when 1541 emulation is disabled or no usable 1541 ROM is loaded.
- T64 is host convenience loading only: the first loadable entry is extracted.
  There is no mounted tape/Datasette state or T64 KERNAL trap.
- CRT supports generic hardware type 0 8K/16K cartridges through the machine path;
  it is not a disk/file injection.

Useful runtime entry points are `c64_mount_d64_ex()`, `c64_mount_g64()`,
`c64_set_drive_writable()`, `c64_unmount_drive()`, `c64_copy_drive_status()`, and
the corresponding `runtime_client_*` functions. Runtime owns host flushing;
machine code only mutates an in-memory image and marks its slot dirty. Devices
other than 8 and 9 must be rejected rather than silently treated as 8.

## Real 1541 ROM/IEC path

Enable `[disk] emulate_1541=1` and provide a supported 16 KiB combined DOS 2.6
ROM (`[roms] 1541`, or auto-discovered `1541.rom` in `.`, `rom`, or `roms`). The
drive has a 6502-compatible CPU, RAM, two VIAs, IEC wiring, and a fractional true
1.000 MHz drive clock. With a ROM present, device 8/9 KERNAL LOAD uses real C64
KERNAL IEC handlers and the drive ROM. D64 sector READ/SEARCH and WRITE jobs are
intercepted at the DOS job layer; writes honor read-only mounts and mark slots dirty.
The DOS command/error channel supports scratch, rename, validate, initialize,
format, and status through the ROM plus the FORMT intercept.

### Which drives sit on the IEC bus

A 1541 ROM is loaded into both drive objects, but **device 9 only drives the IEC
bus once a disk is mounted on it** (`c64_drive9_bus_pull()` in `c64.c` gates both
`c64_refresh_iec_external_pull()` and `c64_get_iec_pull_excluding_drive()`).
Device 8 is always present. This mirrors VICE, whose default `drive9type` is none,
so it resolves to `iecbus_cpu_write_conf1` — "only unit 8 enabled".

This is not cosmetic. An idle 1541 still answers ATN by pulling DATA through the
ATN acknowledge gate (`DATA = PB1 | (ATN XOR ATNA)`, see `c1541_iec_pull_from_orb`).
A drive the user never asked for therefore clamps DATA low on every ATN assert,
which destroys loaders that use ATN as a transfer clock — it corrupted Edge of
Disgrace's post-swap streaming depacker while leaving CLK and the delivered byte
stream perfect, so only the depacked output was wrong.

## Optional media path

`[disk] media_1541=1` requires `emulate_1541=1`. It provides D64-to-GCR track
synthesis, rotation/SYNC/BYTE READY, disk VIA motor/stepper/WPS behavior, physical
read, hybrid write/format, and read-only G64 mounting. The validated fast-loader
matrix includes Arkanoid V-MAX PAL dumps and Robocop NTSC load-to-game byte checks;
this is not broad commercial compatibility.

## Explicit limits

G64 write-back, pure Port-A GCR write fidelity, cross-drive copy, block/memory
commands, devices beyond 8/9, 1571/other ROM variants, full drive save-state, and
exhaustive fast-loader support are deferred. Do not add machine-to-host file I/O;
runtime owns flushing.

## Practical workflows

```sh
./build/c64m --disk 8=path/to/game.d64 --autorun
./build/c64m --disk 8=path/to/game.d64 --control-port 6510
./build/c64m --crt path/to/cart.crt
```

For the control-port workflow, mount with `mount-d64 8 path`, query
`get-disk-status 8`, and use `load-prg` or BASIC keyboard input. Enable the disk
write flag through the frontend/runtime API before SAVE; a normal mount is
read-only. Real 1541 testing additionally requires a ROM and `emulate_1541=1`;
media/GCR testing also requires `media_1541=1`.

When debugging a failed real-1541 load, inspect `get-drive-cpu 8`, ROM-loaded and
media-track state, and whether the KERNAL trap was bypassed. Do not infer success
from a host-side RUN log; inspect C64 memory or a frame.

## Multi-disk / custom EXECUTE notes

Job `$E0` (EXECUTE) means the DOS ROM jumps into the job buffer (`$0300+n*$100`).
DOS `NEW`/FORMT and custom fastloaders (e.g. Edge of Disgrace after disk 1A) both
use this. With `media_1541=1`, EXECUTE must **not** be completed as a synthetic
`format_track()`: that short-circuits buffer entry (often as write-protect on a
read-only mount) and freezes multi-stage loaders after a disk swap. Sector-intercept
mode (media off) still maps EXECUTE to the hybrid D64 track erase for FORMT tests.

Disk swap (invalidate then rebuild) starts a VICE-style attach blanking window
(`C1541_MEDIA_ATTACH_DELAY`): Port A reads as 0 and WPS is closed until the delay
elapses. Inter-sector gaps on synthetic D64 tracks match VICE zone sizes
(8/17/12/9 by density).

Runtime autorun (`-a`) bootstraps media mounted into an empty device 8 with
`LOAD"*",8` / `RUN`. Replacing an image already mounted on device 8 is a disk
swap and must not re-arm autorun: injecting the command through the KERNAL
keyboard buffer can overwrite live multi-disk loader code at `$0277-$0280`.

Read path matches VICE’s split: G64 uses the flux-transition + UE7/UF4 decoder
(`rotation_1541_gcr`); synthetic D64 uses NRZ GCR bitstream + immediate BYTE
READY (`rotation_1541_simple`). BYTE READY is a sticky SO edge applied after
drive Phi2; **CLV discards any pending edge** (VICE `LOCAL_SET_OVERFLOW(0)`),
which dual-BVC loaders (`BVC *` / `CLV` / `LDA $1C01`) require so a byte that
lands during CLV cannot re-assert V and cause repeated GCR samples. No
title-specific assists.
