# c64m status handoff

This file is intentionally short. It is the routing document for agents, not the full project encyclopedia.

Read first:

1. `AGENTS.md` - agent workflow, build/test rules, repository conventions.
2. `MASTER.md` - product/architecture source of truth.
3. This `STATUS.md` - current handoff summary and routing.
4. The relevant component file under `docs/status/`.

## Current stable baseline

The emulator currently includes:

- Core C64 runtime: 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction stepping, and frame handoff.
- VIC-II through Phase J, except light pen skipped.
- CIA through Phase G, including CIA #1 IRQ, CIA #2 NMI, timers, ICR behavior, keyboard/joystick/RESTORE, CIA #2 VIC bank, IEC pins, TOD, and alarm.
- SID functional audio plus SID improvement Phase 10 as the current measured baseline.
- Runtime audio infrastructure with cycle-stepped sample production, SDL output, recording, smoke tone, turbo mute, and overrun/underrun counters.
- Debugger/config/frontend UI through the documented phases, including hardware view, memory/disassembly source modes, virtual memory views, assembler tab, help UI, host load/save UI, and modal input isolation.
- D64 read-only disk support for devices 8 and 9, KERNAL LOAD traps, directory loads, wildcard matching, runtime mount/unmount, and startup autorun flows.
- Practical undocumented 6510 opcode coverage.

## Recent high-value handoff notes

- Disk images are now persisted in the `[disk]` INI section on quit; paths are stored relative to the INI file and each drive holds an ordered queue (comma-separated). The disk UI shows `[N][Add][Eject] <combo>` per device; Shift+Eject clears the whole queue. See `docs/status/DISK_IO.md` for full semantics.
- CIA #2 NMI is wired to the CPU NMI edge latch. RESTORE remains a separate one-shot NMI source.
- VIC-II sprite BA timing now uses per-standard PAL 6569 and NTSC 6567R8 tables selected from machine video configuration.
- Runtime audio production now advances from the cycle-stepping path, not from 1024-cycle batches.
- SID Phase 10 is the current audio fidelity baseline: score 1.2838 against `x64sc-20s.mp3`, with 16-22 kHz excess reduced but not eliminated.
- The CPU has explicit dispatch for all 256 opcode slots and practical undocumented opcode implementations, but not perfect analog/chip-revision behavior.

## Component files

- `docs/status/VICII.md` - raster/video/sprite/BA/VIC memory status.
- `docs/status/CIA.md` - CIA #1/#2 timers, ICR, IRQ/NMI, keyboard/joystick/RESTORE, IEC, TOD.
- `docs/status/SID.md` - SID register behavior, voices, waveforms, ADSR, filter, read-back, fidelity phases.
- `docs/status/AUDIO.md` - runtime/platform audio transport, scheduling, recording, smoke tone, turbo behavior.
- `docs/status/CPU_MACHINE.md` - 6510, bus, banking, reset/boot, IRQ/NMI, BA stalls, CLI startup load.
- `docs/status/FRONTEND_DEBUGGER.md` - UI, debugger, memory views, config, assembler, help, dialogs.
- `docs/status/DISK_IO.md` - D64 parser/runtime mounting/KERNAL LOAD/host file load-save.
- `docs/status/TESTING.md` - tests, smoke checks, known useful manual validation.
- `docs/status/DEFERRED.md` - known gaps and intentionally deferred work.
- `docs/status/OPTIMIZATIONS.md` - accepted and rejected optimization notes.
- `docs/status/ORIGINAL_STATUS.md` - unmodified source handoff preserved for traceability.

## Update rule

Do not grow this file back into a full status dump. Add detailed facts to the relevant component file. Only add top-level facts here when they affect agent routing or the current baseline.
