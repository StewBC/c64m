# Disk, PRG, and host file I/O status

## D64 current implementation

- D64 disk support is complete through Phase G.
- Read-only tools parser is implemented.
- Runtime mount/unmount supports devices 8 and 9.
- KERNAL LOAD traps for PRG loads are implemented.
- `LOAD "$"` directory loads are implemented.
- Exact and wildcard filename matching are implemented.
- Machine-tab disk UI/status is implemented.

## D64 parser behavior

- Standard 35-track D64s are supported.
- Common appended error-info bytes are supported.
- BAM metadata is parsed.
- Directory chain is parsed.
- Raw PETSCII names and ASCII debug names are parsed.
- PRG file chains and PRG load address are parsed.

## Runtime behavior

- Devices 8 and 9 can mount independent read-only images.
- Runtime/frontend exchange copied disk status only.
- LOAD supports device 8/9 PRG exact names.
- LOAD supports `*`, prefix wildcards, `?`, and directory synthesis.
- Failure paths preserve unrelated memory for no disk, missing file, unsupported type/mode, malformed chains, loops, out-of-range sectors, and target overflow.

## Startup load behavior

- `--disk` / `-d <drive>=<image>` mounts D64 images at startup.
- `--prg` / `-p <file>` loads any file as PRG on startup.
- `--basic` / `-B <file>` loads any file as a BASIC program on startup.
- `--prg` resets, boots to BASIC, injects at embedded load address, and resumes running automatically.
- `--basic` resets, boots, writes at embedded load address, and updates TXTTAB/VARTAB at `$2B-$2E`.
- `--autorun` with `--prg` or `--basic` buffer-injects `RUN\r` after bytes land at `$E38B`.
- `--autorun` with `--disk 8=...` uses a two-phase `$E38B` trap: `LOAD"*",8\r`, then `RUN\r`.

## Host file load/save UI

- Machine tab has Disks, Programs, and Emulator sections.
- Disks section has device 8/9 mount and Eject controls.
- Programs section has Load and Save buttons.
- Emulator section has Configure and Reset.
- Load dialog has Name + Browse, From File, Reset, and Basic Program options.
- From File reads a 2-byte address header by default.
- Manual hex field is active when From File is unchecked.
- Reset waits for `$E38B` before injecting.
- Basic Program fixes TXTTAB and VARTAB after load.
- Save dialog has Name + Browse, Basic Program, Write address header, and Start/End hex fields.
- Basic Program save reads `$2B/$2C` and `$2D/$2E`, treats end as exclusive, and forces Write address header.

## Known limitations / deferred

- D64 writes are not implemented.
- SAVE to disk is not implemented.
- Error channel is not implemented.
- 1541 CPU/ROM emulation is not implemented.
- IEC timing/protocol is not implemented.
- Fast loaders are not implemented.
- Devices beyond 8/9 are not implemented.
- Full Commodore DOS pattern/type suffix semantics are not implemented.

## Tests / smoke checks

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
- Reset/PRG loader: verify reset-before-load and autostart collection PRGs.
- Autorun:
  - `--prg foo.prg --autorun` should boot and immediately run.
  - `--basic foo.bas --autorun` should boot and immediately run.
  - `--disk 8=game.d64 --autorun` should type `LOAD"*",8` and `RUN` automatically.
- Host load/save:
  - Load with file-address header.
  - Load with Basic Program and check `$2B-$2E`.
  - Save as Basic Program and reload.
  - Save raw range with and without header.
  - Verify Eject button and Machine tab section order.

## Files likely involved

- `src/tools/d64*`
- `src/runtime/*disk*`
- `src/machine/c64*`
- `src/frontend/*`
- Disk, loader, host load/save, and CLI tests under `tests/`
