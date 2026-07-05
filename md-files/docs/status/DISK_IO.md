# Disk, PRG, tape-container, and host file I/O status

## D64 current implementation

- D64 disk support is complete through Phase G plus disk-queue, INI-persistence,
  and opt-in KERNAL SAVE additions.
- Tools parser and PRG writer are implemented for standard D64 images.
- Runtime mount/unmount supports devices 8 and 9.
- KERNAL LOAD traps for PRG loads are implemented.
- KERNAL SAVE traps for PRG writes are implemented when the mounted image is writable.
- Optional real 1541 ROM/IEC LOAD path is implemented when `[disk] emulate_1541=1`
  and a standard 1541 DOS 2.6 ROM is loaded.
- `LOAD "$"` directory loads are implemented.
- Exact and wildcard filename matching are implemented.
- Machine-tab disk UI is implemented with per-drive disk queues.
- Machine-tab disk UI includes a per-current-image `Write` checkbox.
- Generic 8K/16K `.CRT` host loading is implemented as cartridge attachment,
  not as PRG/BASIC memory injection.

## D64 parser behavior

- Standard 35-track D64s are supported.
- Common appended error-info bytes are supported.
- BAM metadata is parsed.
- Directory chain is parsed.
- Raw PETSCII names and ASCII debug names are parsed.
- PRG file chains and PRG load address are parsed.
- PRG writes allocate sectors, update the BAM, write file-sector chains, and add
  PRG directory entries. `@:` replacement is supported for PRG SAVE replacement.

## Runtime behavior

- Devices 8 and 9 can mount independent images. Images are read-only by default.
- Writable images accept standard KERNAL `SAVE "NAME",8` / `SAVE "NAME",9`
  through the `$FFD8` trap. The trap writes a PRG with the two-byte load address
  header and marks the mounted image dirty.
- Runtime flushes dirty writable images to the host `.d64` file after successful
  SAVE, and also before replacing/ejecting/quit. Failed host flushes leave the
  image dirty and publish an error.
- Runtime/frontend exchange copied disk status only.
- LOAD supports device 8/9 PRG exact names.
- LOAD supports `*`, prefix wildcards, `?`, and directory synthesis.
- With 1541 emulation enabled and a ROM present, standard KERNAL LOADs for devices
  8/9 go through the C64 KERNAL IEC routines and real 1541 DOS ROM serial handlers.
  Without a 1541 ROM, or when emulation is disabled, the D64-backed KERNAL trap is
  still used as a compatibility fallback.
- Failure paths preserve unrelated memory for no disk, missing file, unsupported type/mode, malformed chains, loops, out-of-range sectors, and target overflow.

## Disk queue model (app_disk_slot)

Each device (8, 9) has an `app_disk_slot` holding an ordered list of image paths and a
`current` index:

- `app_disk_slot_set` — replaces the entire queue with a single path (GUI mount button).
- `app_disk_slot_eject_current` — removes the current entry; advances with round-robin
  wrap; returns the next path to mount, or NULL if the queue is now empty.
- `app_disk_slot_add_after_current` — inserts a new path immediately after `current`.
  If the queue was empty, the path becomes index 0 and is mounted immediately.
- `app_disk_slot_select` — sets `current` to a chosen index; returns the path to mount.
- `app_disk_slot_clear` — removes all entries; drive is unmounted.
- `current` is never written to or read from the INI (always restarts at 0 on launch).

## INI disk persistence

- Session-mounted and GUI-mounted disks are saved to the `[disk]` section on quit (when
  save-on-quit is active).
- Each drive's queue is serialized as a comma-separated list of paths: `8=path1,path2`.
- Paths are stored relative to the INI file directory; absolute paths are converted to
  relative before writing.
- On load, relative paths are resolved against the INI file directory to absolute paths.
- CONFIG_APPLY replaces `options` with the Configure-dialog snapshot; the live
  `disk_slots` are merged back in before options is destroyed so GUI-mounted disks
  survive a Configure → OK round-trip.
- Writable state is serialized as a parallel comma-separated list:
  `8_writable=0,1`. Missing writable lists default all images to read-only.
- `frontend_set_disk_queue` must be called from main.c after any disk-slot change to
  keep the frontend's `disk_queue[2]` mirror in sync.

## Startup load behavior

- `--disk` / `-d <drive>=<image>` mounts D64 images at startup; comma-separated lists
  are accepted to pre-populate the queue (`--disk 8=side1.d64,side2.d64`).
- `--crt <file>` loads a generic 8K/16K CRT cartridge at startup. The CRT path
  is kept as a literal path, so spaces and parentheses are supported.
- `--prg` / `-p <file>` loads any file as PRG on startup.
- `--basic` / `-B <file>` loads any file as a BASIC program on startup.
- `--prg` resets, boots to BASIC, injects at embedded load address, and resumes running automatically.
- `.T64` files passed through PRG-style host load paths are parsed as tape
  containers and the first loadable entry is extracted into PRG-style bytes
  before injection. This is host-file convenience loading only, not mounted
  tape state or Datasette/KERNAL tape emulation.
- `--basic` resets, boots, writes at embedded load address, and updates TXTTAB/VARTAB at `$2B-$2E`.
- `--autorun` with `--prg` or `--basic` buffer-injects `RUN\r` after bytes land at `$E38B`.
- `--autorun` with `--disk 8=...` uses a two-phase `$E38B` trap: `LOAD"*",8\r`, then `RUN\r`.

## Host file load/save UI

- Machine tab has Disks, Programs, and Emulator sections.
- Disks section: each device row shows `[N][Add][Eject] <selector> [Write]`.
  - `[8]` / `[9]` — open file dialog; replaces the entire queue with one new image.
  - `[Add]` — open file dialog; inserts a new image after the current entry.
  - `[Eject]` — ejects the current disk and advances to the next (round-robin); if the
    queue drains to empty, the drive is unmounted.
  - `[Eject!]` (Shift held) — ejects all disks from the queue and unmounts the drive.
  - The selector is a combo box while the queue has entries: it shows the current
    disk basename and opens a drop-down with all queued images; clicking one mounts it.
    When the queue is empty it degrades to a plain status label.
  - `Write` toggles whether the current queued image may be modified by KERNAL SAVE.
- Programs section has Load and Save buttons.
- Emulator section has Configure and Reset.
- Load dialog has Name + Browse, From File, Reset, and Basic Program options.
- From File reads a 2-byte address header by default.
- Manual hex field is active when From File is unchecked.
- Reset waits for `$E38B` before injecting.
- Basic Program fixes TXTTAB and VARTAB after load.
- Selecting a `.T64` in the Load dialog routes to PRG-style T64 extraction and
  ignores the raw binary dialog options.
- Selecting a `.CRT` in the Load dialog routes to cartridge attach/reset/run
  and ignores the raw binary dialog options.
- Save dialog has Name + Browse, Basic Program, Write address header, and Start/End hex fields.
- Basic Program save reads `$2B/$2C` and `$2D/$2E`, treats end as exclusive, and forces Write address header.

## Known limitations / deferred

- Real 1541 DOS sector writes are implemented (Phase 4, job-level WRITE
  intercept): with `[disk] emulate_1541=1`, SAVE and sequential/relative file
  writes to a writable image persist via the real 1541 path. The compatibility
  KERNAL SAVE trap still handles PRG SAVE when no 1541 ROM is handling the device.
  Read-only mounts return write-protect (DOS 26). Still deferred: track-level
  format (Phase 5) and media-level write fidelity / G64.
- Mounted tape/T64 state, T64 entry selection UI, and BASIC/KERNAL `LOAD` traps
  for T64 are not implemented. Current T64 support extracts the first loadable
  entry only through host load/drop paths.
- CRT support is limited to generic 8K/16K normal cartridges. Cartridge mappers,
  INI persistence, detach UI/status, cartridge RAM/flash writes, and freezer
  buttons are not implemented.
- Error channel is not implemented beyond generic 1541 ROM intercept errors.
- DOS command channel scratch/rename/format/validate is not implemented.
- Fast loaders are not broadly validated; loaders that depend on unmodeled
  disk-controller VIA motor/SYNC/head mechanics may still fail.
- Devices beyond 8/9 are not implemented.
- Full Commodore DOS pattern/type suffix semantics are not implemented.

## Tests / smoke checks

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
- Writable D64 SAVE: mount a scratch D64, enable `Write`, `SAVE "TEST",8`,
  restart with the same image, and confirm `LOAD "TEST",8` reloads the PRG.
- Reset/PRG loader: verify reset-before-load and autostart collection PRGs.
- Autorun:
  - `--prg foo.prg --autorun` should boot and immediately run.
  - `--basic foo.bas --autorun` should boot and immediately run.
  - `--disk 8=game.d64 --autorun` should type `LOAD"*",8` and `RUN` automatically.
  - With `[disk] emulate_1541=1` and `[roms] 1541=...`, the same disk autorun path should load through the real 1541 ROM/IEC path, not the KERNAL trap.
- Host load/save:
  - Load with file-address header.
  - Load/drop `.T64`; first loadable entry should inject at its directory load address.
  - Load/drop `.CRT`; generic 8K/16K cartridges should attach, reset, and run.
  - Smoke `assets/crt/International Soccer (1983)(Commodore).crt` to verify
    spaces and parentheses in paths.
  - Load with Basic Program and check `$2B-$2E`.
  - Save as Basic Program and reload.
  - Save raw range with and without header.
  - Verify Eject button and Machine tab section order.

## Files likely involved

- `src/tools/d64*`
- `src/tools/t64*`
- `src/tools/crt*`
- `src/runtime/*disk*`
- `src/machine/c64*`
- `src/frontend/*`
- Disk, loader, host load/save, and CLI tests under `tests/`
