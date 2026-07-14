# Architecture and ownership

## Product boundary

c64m is a C99 Commodore 64 emulator. The current target is useful PAL and NTSC
execution for ordinary BASIC, PRG, D64, selected games/demos, recognizable SID
audio, usable keyboard/joystick input, generic 8K/16K CRT loading, and the
optional real-1541 path. It is not a promise of cycle-perfect demo-scene behavior,
full drive mechanics, exact analog SID behavior, or every cartridge mapper.

## Dependency direction

```text
frontend -> runtime + platform + tools + util
runtime  -> machine + util
machine  -> tools + util
tools    -> util
platform -> util + SDL2
```

Do not add reverse dependencies. In particular, machine/runtime code must not
include SDL, Nuklear, frontend, or platform headers.

## Thread and state ownership

- The UI/main thread owns SDL, renderer, Nuklear, frontend state, and control-port
  request dispatch.
- The runtime thread owns the live `c64_t` and executes the machine.
- The SDL audio callback only reads the shared audio buffer. It must not call
  runtime or machine code.
- Frontend and control-port consumers receive copied snapshots, frames, memory,
  symbols, and events. No live machine pointer crosses a thread boundary.
- Runtime writes audio samples into the dependency-safe `util/audio_buffer` path;
  platform owns the SDL device.

## Where to change code

- `src/machine/`: emulated hardware and machine-visible state.
- `src/runtime/`: execution/session control and runtime-client request/response
  plumbing.
- `src/frontend/`: presentation, dialogs, debugger views, and host input mapping.
- `src/platform/`: SDL window/input/audio/filesystem/socket services.
- `src/tools/`: reusable offline parsers, assembler, disassembler, symbols.
- `src/util/`: dependency-safe queues, config, audio buffer, BASIC V2, paste data.

Use `src/machine/c64.h` snapshot/debug APIs rather than reaching into live machine
state from UI code. Use `runtime_client` rather than calling machine functions
from the frontend.

## Operational rules

- Build vertically and keep changes inside the current milestone.
- Preserve timing-visible side effects when optimizing.
- A direct launch of `./build/c64m` opens a blocking SDL window. Prefer
  `./build/c64m --help`, ctest, or a time-limited launch for automation.
- Run from the repository root so ROM fallback lookup finds `roms/`.
