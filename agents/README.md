# c64m agent handoff

This directory is the concise, implementation-oriented handoff for a fresh agent.
The C source and tests are authoritative. Do not read files under `md-files/` -
they are historical working notes kept for record-keeping only and are not
guaranteed to be accurate or current. If a handoff doc and the source disagree,
or if a handoff doc doesn't answer your question, trust the source, not md-files.

Read in this order:

1. `architecture.md`
2. The component handoff relevant to the task
3. `testing.md`
4. The source and tests named by that handoff

When using **VICE as the oracle** against titles under `assets/prg/`, also read
**`vice-oracle.md` before launching VICE**. Those files are one-load collection
PRGs (full inject, IRQ vector override); the wrong VICE flags look like an
emulator bug. See that note for the required `-autostartprgmode 1` / `-autoload`
command line.

No hacks allowed. This is an emulator and the goal is to meet hardware so all
future software that works on hardware (or vice) also "just works" here.

Docs must track source. If you change behavior in a way that makes a component
handoff inaccurate, update that document in the same change. Stale docs are
worse than no docs - don't leave them for the next agent to untangle.

Component handoffs:

- `machine.md` - C64 machine, CPU, bus, memory, interrupts, cartridges, snapshots
- `vicii.md` - VIC-II timing, rendering, sprites, PAL/NTSC, BA/AEC/RDY
- `cia.md` - CIA timers, interrupts, keyboard, joystick, IEC pins, TOD, serial
- `sid-audio.md` - SID behavior and runtime/platform audio transport
- `disk-iec1541.md` - D64/T64/CRT host I/O and optional 1541 ROM/media path
- `runtime-control.md` - runtime thread, commands, snapshots, control port
- `control-port.md` - wire protocol, Python client, command reference, payloads
- `frontend-debugger.md` - SDL/Nuklear UI, debugger, input, configuration, help
- `tools.md` - assembler, disassembler, symbols, D64/T64/CRT/G64 parsers, util
- `testing.md` - automated coverage, baseline command, known gaps, smoke checks
- `vice-oracle.md` - how to load `assets/prg/` one-load collection PRGs in VICE
  (`-autostartprgmode 1`, `-autoload`); required for c64m vs VICE compares

Current baseline is 51/51 passing. That baseline includes the real 1541 ROM/IEC,
G64, Arkanoid, and Robocop paths.

The verification command is:

```text
ctest --test-dir build --output-on-failure
```
