# c64m Status

Read first:

- md-files/MASTER.md
- md-files/DISPLAY.md
- md-files/THREADS.md

These documents are authoritative. STATUS.md records the current project state and immediate direction, not the full architecture.

## Project Summary

c64m is a C99 Commodore 64 emulator using SDL2 for platform services and Nuklear for UI.

The project is currently in early machine bring-up. The CPU, memory system, ROM loading, runtime threading model, and initial UI shell exist. VIC-II, CIA, keyboard input, and real video output do not.

## Current Build Status

Verified:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/test_c64_bus
./build/c64m --noini
```

Build, machine tests, and runtime smoke tests pass.

## Current Configuration Surface

### CLI

Implemented and recognized:

```text
--break
--defaults
--disk
--inifile
--leds
--noini
--nosaveini
--remember
--saveini
--turbo
--help
```

### Configuration

Resolution order:

```text
built-in defaults < ini file < command line
```

Important ini keys:

```ini
[Video]
standard = NTSC
display_width = 384
display_height = 272
integer_scale = true
aspect_correct = true
filter = nearest

[ui]
leds = true

[runtime]
turbo = 1,2,4

[roms]
system =
basic =
character =
kernal =

[disk]
8 =
```

Default display size:

```text
384 x 272
```

Initial window:

```text
768 x 544
```

## Machine Status

### CPU

Implemented:

- Adapted `a2m` NMOS 6502 core.
- Used as the 6510 foundation.
- Undocumented opcodes retained.
- Per-cycle bus behavior retained.
- CPU accesses memory only through C64 bus callbacks.
- CPU snapshots available.

### Memory System

Implemented:

- 64 KiB RAM.
- BASIC ROM.
- KERNAL ROM.
- Character ROM.
- Address decoding.
- Placeholder I/O region.
- Initial `$0000/$0001` banking support.

### ROM Handling

Implemented:

- Combined BASIC+KERNAL ROM support.
- Split ROM support.
- Strict ROM size validation.
- ROM installation into machine-owned storage.
- Reset vector fetched through normal bus reads.

### Runtime Integration

Implemented:

- Runtime-owned machine instance.
- ROM loading on emulation thread.
- Reset command.
- Instruction stepping.
- CPU state requests.
- Runtime event publication.

### Machine Work Still Missing

- NMI path.
- Harte CPU validation suite.
- VIC-II integration.
- CIA integration.
- Global machine scheduler.
- Interrupt-driven system bring-up.

## Runtime Status

Implemented:

- Runtime thread.
- Runtime client API.
- Command queue.
- Event queue.
- CPU state snapshots.
- Reset and step commands.
- Clean startup/shutdown.

Current model:

```text
UI thread
    -> runtime_client
        -> command queue
            -> runtime thread
                -> machine

runtime thread
    -> events/snapshots
        -> UI thread
```

## Platform / UI Status

Implemented:

- SDL initialization/shutdown.
- Single resizable window.
- Accelerated renderer with vsync.
- Main event loop.
- Clean quit behavior.
- F9 UI visibility toggle.
- F10 turbo placeholder toggle.
- Placeholder rendering path.
- Initial pane/layout system.

Not implemented:

- Real emulator display.
- Texture upload pipeline.
- Keyboard matrix input.
- Debugger content.
- Real turbo execution behavior.

## Rules That Must Not Be Broken

Dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
tools    -> util
platform -> util + SDL2
```

Never allow:

```text
frontend -> machine
platform -> machine
runtime  -> frontend
runtime  -> platform
machine  -> runtime
```

Thread ownership:

```text
UI thread:
    SDL
    renderer
    frontend

Runtime thread:
    runtime
    live machine
```

No live machine pointers may cross threads.

## Immediate Priorities

Recommended next work:

1. Integrate Harte-style CPU validation tests.
2. Add NMI support.
3. Establish machine cycle scheduling.
4. Create frame publication pipeline.
5. Begin VIC-II integration.
6. Add CIA foundations when VIC-II timing exists.

## Definition Of Success For Next Milestone

The next meaningful milestone is:

```text
CPU
 -> machine scheduler
 -> VIC-II skeleton
 -> frame publication
 -> visible machine-generated output
```

The long-term bring-up target remains:

```text
Reach a visible Commodore 64 BASIC startup screen.
```
