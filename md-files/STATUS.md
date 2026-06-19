# STATUS_CLEAN.md

## Current status

The emulator is complete through:

- Core C64 runtime: 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction step, frame handoff.
- VIC-II through Phase J except skipped light pen: live raster timing, timed bus-visible writes, PAL/NTSC frame sizes, text/bitmap/multicolor/ECM/invalid modes, sprites, sprite priority/collisions, open/unused register reads, sprite BA stealing, DEN-off blanking.
- CIA through Phase G: CIA #1/#2 routing, timers, ICR/IRQ/NMI behavior, keyboard/joystick/RESTORE, CIA #2 VIC bank and IEC port pins, TOD/alarm.
- Debugger UI through Phase 13: CPU/registers, memory, disassembly, misc/debugger tabs, execute/read/write breakpoints/watchpoints, counters/actions, INI persistence.
- Configuration UI through Phase 14: Configure dialog, PAL/NTSC setting, display/turbo/symbol/INI options, runtime config apply and reboot on video-standard change.
- D64 disk support through Phase G: read-only tools parser, runtime mount/unmount for devices 8 and 9, KERNAL LOAD traps for PRG loads, LOAD "$" directory loads, exact/wildcard filename matching, Machine-tab disk UI/status.
- PRG loader polish: reset-before-load, pending injection after BASIC warm-start at $E38B, keyboard-buffer autostart PRGs supported.
- Assembler UI integration: Assembler tab, file picker, address/run address, auto-run, reset/run-to-BASIC assembly flow, assembler error event/dialog, symbol snapshot handoff to disassembler.

## Important implemented details

### VIC-II

- Machine owns monotonic master cycle; VIC/CIA/SID hooks advance to timestamped CPU bus events before visible side effects.
- Live frame publication uses completed live VIC-II frame buffers; snapshot renderer remains only as fallback/debug before a live frame exists.
- Bad Line BA and sprite-fetch BA both stall CPU reads using CPU event read/write classification; writes continue where allowed.
- AEC is intentionally not modeled as emulator state; BA is the stall predicate.
- Sprite system supports 8 sprites, X/Y position, X/Y expansion, multicolor, bank-aware sprite pointer/data fetch, priority, collisions, and IRQs.
- VIC memory reads are bank-aware via CIA #2 port A; char ROM is visible only in VIC banks 0 and 2 at the normal ranges.
- `$D011` DEN=0 blanks visible display/border color to `$D021` while preserving sprite visibility and collision behavior.

### CIA

- CPU-visible CIA reads have side effects; debugger-safe reads avoid side effects.
- Timer A/B use project-level cycle countdown semantics, separate latch/live counters, force-load strobe, one-shot/continuous modes, CNT and cascade sources, PB6/PB7 output behavior.
- ICR masks and flags are separate; normal reads clear reported flags; debugger peeks do not.
- CIA #1 drives IRQ; CIA #2 drives NMI edge latch.
- CIA #1 handles bidirectional keyboard matrix, joystick ports, and RESTORE isolation.
- CIA #2 handles VIC bank selection and IEC ATN/CLK/DATA open-collector line modeling.
- TOD uses BCD tenths/seconds/minutes/hours, 12-hour AM/PM, 50/60 Hz source policy, coherent read latch, alarm ICR source.

### D64

- Parser supports standard 35-track D64s and common appended error-info bytes.
- Parses BAM metadata, directory chain, raw PETSCII names, ASCII debug names, PRG file chains, and PRG load address.
- Devices 8 and 9 can mount independent read-only images; runtime/frontend exchange copied status only.
- LOAD supports device 8/9 PRG exact names, `*`, prefix wildcards, `?`, and LOAD "$" directory synthesis.
- Failure paths preserve unrelated memory for no disk, missing file, unsupported type/mode, malformed chains, loops, out-of-range sectors, and target overflow.

### Debugger / UI / config

- Runtime owns machine state, breakpoints, watchpoints, stop reason, counters, and actions.
- Frontend renders copied snapshots only and sends intents/commands to runtime.
- Register and memory edits apply only while paused; running edits are ignored.
- Debugger input focus is explicit: C64 display vs debugger views.
- Symbol table is tools/frontend/debug-session-owned, separate from emulator machine and assembler internals.
- INI supports config and breakpoint persistence; invalid breakpoint entries are skipped while valid entries load.

## Not implemented / deferred

- SID.
- Full CIA accuracy and pin/race-level timing.
- Cycle-perfect video/audio timing.
- VIC-II light pen (`$D013/$D014` stubbed; Phase F skipped).
- Last-byte-on-bus open-bus behavior; unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access fetch behavior from `$3FFF` / `$39FF` in renderer.
- Exact RDY/AEC sub-cycle CPU pin timing.
- NTSC sprite BA timing table; current sprite BA table is PAL-only.
- D64 writes, SAVE to disk, error channel, 1541 CPU/ROM emulation, IEC timing/protocol, fast loaders, devices beyond 8/9, full Commodore DOS pattern/type suffix semantics.
- Phase 13 deferred breakpoint actions: Type, Swap, and trace output/details.
- Host load/save UI beyond current PRG and D64 paths.

## Runtime note

Real 64C ROM execution reaches BASIC READY with visible cursor and keyboard input.

Known smoke-trace observation after 1,000,000 cycles:

```text
PC=$FD7E
CIA #1 IRQ pending = true
CPU IRQ entries = 0
CIA #1 ICR reads = 0
```

This is expected in that trace because the CPU interrupt-disable flag remains set, so the pending CIA #1 IRQ is not entered.

## Human smoke still useful

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
- Reset/PRG loader: verify reset-before-load and autostart collection PRGs.
- Assembler tab: assemble success, assemble error dialog, auto-run, and symbol display in disassembly.
