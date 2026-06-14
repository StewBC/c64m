# c64m Status

Read these first:

- `md-files/MASTER.md`: component boundaries and dependency rules.
- `md-files/DISPLAY.md`: single-window video/display model.
- `md-files/THREADS.md`: UI thread, runtime thread, queues, and SDL threading wrappers.

## Current State

This is still early scaffolding. The project builds as a C99 CMake app using SDL2, vendored helper libraries, and initial emulator components.

Top-level CMake:

- Requires CMake 3.28.
- Enables `CMAKE_EXPORT_COMPILE_COMMANDS`.
- Finds SDL2 only.
- Adds `external`, `util`, `machine`, `runtime`, `platform`, `tools`, `frontend`.
- Builds executable `c64m` from `src/app_options.c` and `src/main.c`.

Vendored libraries under `external/`:

- `stb/stb_ds.h`: dynamic arrays/hash maps, intended as internal implementation detail.
- `inih/ini.c`, `inih/ini.h`: ini parser only.
- `argparse/argparse.c`, `argparse/argparse.h`: command-line parsing.
- `logc/log.c`, `logc/log.h`: future logging backend.
- `whereami/whereami.c`, `whereami/whereami.h`: future executable/app path helper.

## Config And CLI

Project-owned config wrapper exists in `src/util/config.c/.h`.

- Public type is opaque `config`.
- Uses `inih` and `stb_ds` privately.
- Supports load/save, get/set string, int, bool.

Startup options live in `src/app_options.c/.h`.

Resolution order:

```text
built-in defaults < ini file < command line
```

Ini behavior:

- Default ini path: `c64m.ini`.
- `--noini` skips ini loading.
- `--inifile <name>` selects ini path.
- `--defaults` currently implies built-in defaults and no ini.

Implemented CLI surface:

- `--break <break>`, `-b`
- `--defaults`, `-f`
- `--disk <drive>=<image>`, `-d`, repeatable
- `--inifile <name>`, `-i`
- `--leds <on|off>`, `-l`
- `--noini`, `-n`
- `--nosaveini`, `-!`
- `--remember`, `-r`, implies save ini
- `--saveini`, `-v`
- `--turbo <csv>`, `-t`
- `--help`, `-h`

Current ini keys consumed:

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
system = path-to-combined-basic-plus-kernal-rom
basic = path-to-basic-rom
character = path-to-character-rom
kernal = path-to-kernal-rom

[disk]
8 = path-or-image
```

Video defaults when no ini is used:

- `standard = NTSC`
- `display_width = 384`
- `display_height = 272`
- `integer_scale = true`
- `aspect_correct = true`
- `filter = nearest`

PAL/NTSC-specific `[PAL]` and `[NTSC]` display sections were considered and rejected for now. Display dimensions live under `[Video]`.

## Machine

`src/machine` now contains the first real machine slice:

- `c64.h` / `c64.c`: initial machine wrapper owning CPU plus bus and wiring CPU memory callbacks through the bus.
- `c64_bus.h` / `c64_bus.c`: C64 CPU bus, RAM, ROM storage, address decoding, banking, and placeholder I/O dispatch.
- `c64_rom.h` / `c64_rom.c`: strict ROM file loading and physical ROM layout normalization into split BASIC/KERNAL/character storage.
- `cpu.h`: public 6510/6502 register state and callback API.
- `cpu.c`: initialization, callback wiring, and reset-vector handling.
- `c6510.c` / `c6510_inln.h`: adapted cycle-accurate NMOS 6502 core from `a2m`.

CPU direction chosen:

- Reuse and adapt the proven `a2m` NMOS 6502 core instead of writing a fresh CPU from scratch.
- Keep undocumented 6502 opcodes and per-cycle bus accesses from the existing core.
- Expose memory reads, memory writes, IRQ pending, and trace through C64-side callbacks.
- Treat the core as the C64's 6510 CPU foundation. The 6510 I/O port at `$0000/$0001` belongs in the C64 memory/bus layer that services the callbacks.

Implemented:

- Machine-owned 64 KiB RAM, initialized deterministically.
- BASIC, character, and KERNAL ROM storage.
- Exact-size ROM loading helpers for BASIC, character, KERNAL, and combined BASIC+KERNAL system ROMs.
- Ini-configured ROM paths under `[roms]` for `system`, `basic`, `character`, and `kernal`, with `[rom]` aliases still accepted.
- Machine ROM installation refuses missing BASIC, KERNAL, or character ROMs.
- Machine reset clears RAM/ports, preserves installed ROMs, and fetches the reset vector through bus reads.
- Machine instruction stepping and copied CPU snapshots.
- Runtime startup copies configured ROM paths and loads available ROMs on the emulation thread.
- Runtime bring-up commands for reset, instruction step, and CPU state request.
- Runtime bring-up events for reset completion, step completion, and copied CPU state response.
- Address decoding for RAM, BASIC ROM, character ROM, KERNAL ROM, and placeholder I/O.
- Initial 6510 `$0000/$0001` processor port banking for RAM/ROM/I/O/character ROM visibility.
- CPU reset vector fetches through the C64 bus.
- Focused CTest coverage for RAM, ROM visibility, banking, ROM loading, ROM installation, reset-vector behavior, ROM ini parsing, and bounded runtime ROM stepping.

Not yet implemented:

- NMI input path.
- Harte-test harness inside this repo.
- VIC-II/CIA integration with CPU cycle stepping.

## Platform And Display

`src/platform` currently owns SDL initialization plus initial window/renderer creation.

Implemented:

- `platform_init()`
- `platform_shutdown()`
- opaque `platform_window`
- `platform_window_config`
- `platform_window_create()`
- `platform_window_destroy()`
- renderer clear/present helpers
- SDL window/renderer accessors for frontend-owned Nuklear integration

Window behavior implemented:

- Single SDL window.
- Resizable.
- Renderer created with accelerated + vsync flags.
- Initial window size is `display_width * 2` by `display_height * 2`.
- Default startup size is therefore `768 x 544`.
- Main/UI event loop keeps the window open.
- SDL quit event exits cleanly.
- macOS Command+Q and Windows/Linux Ctrl+Q exit cleanly.
- F9 toggles emulator UI visibility.
- F10 toggles frontend turbo state placeholder/logging.
- F1-F8 are not consumed by host UI.
- Placeholder display-only render clears/presents each frame.
- Placeholder Nuklear UI renders when visible.
- C64 layout manager provides display, CPU registers, disassembly, memory, and misc panes.
- Layout splitters are visible and draggable.
- C64 display pane corner can be dragged freely in X/Y.
- Clicking the C64 display pane corner snaps the pane to the closest sane display aspect.

Not yet implemented:

- Aspect-preserving scaling.
- Letterbox/pillarbox.
- Texture/frame upload.
- Real Nuklear UI content.
- Real turbo behavior.
- Real keyboard matrix input.

## Runtime And Threads

Threading model is defined in `md-files/THREADS.md`.

Current source tree includes initial runtime and util threading/queue files:

- `src/util/thread.c/.h`
- `src/util/mutex.c/.h`
- `src/util/cond.c/.h`
- `src/util/message_queue.c/.h`
- `src/runtime/runtime_command.c/.h`
- `src/runtime/runtime_event.c/.h`
- `src/runtime/runtime_client.c/.h`
- `src/runtime/runtime_thread.c`
- `src/runtime/runtime_internal.h`

Respect the rule:

```text
SDL window/events/rendering stay on main/UI thread.
runtime + live machine stay on emulation thread.
frontend talks to runtime through runtime_client.
no live machine pointers cross threads.
```

## Architecture Rules To Preserve

Allowed direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
tools    -> util
platform -> util + SDL2
```

Forbidden direction:

```text
machine  -> runtime/frontend/platform
runtime  -> frontend/platform
platform -> machine
frontend -> machine
```

## Verified

Most recent checks performed:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/test_c64_bus
./build/c64m --noini
```

Build, machine bus tests, and smoke tests passed.

## Likely Next Work

Implement the next machine slice:

- Wire in a Harte-style CPU test harness for this adapted core.
- Add NMI callback support before CIA/VIC integration needs it.

Display/runtime presentation work remains open:

- Add fixed 384x272 display rectangle scaling logic.
- Add aspect-preserving best-fit inside assigned display pane.
- Add letterbox/pillarbox.
- Add texture/frame upload path.
- Replace placeholder Nuklear panes with real view content over time.
- Keep all SDL calls on main/UI thread.
