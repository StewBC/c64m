# Architecture and ownership

## Product boundary

c64m is a Commodore 64 emulator. C sources are written as C99; the build uses
C11 for private atomics in `util/audio_buffer`. The current target is useful PAL
and NTSC execution for BASIC, PRG, D64/G64, selected games and demos,
recognizable SID audio, keyboard/joystick, CRT types 0 (Normal), 5 (Ocean),
7 (Fun Play), 8 (Super Games), 15 (C64GS), 17 (Dinamic), and 19 (Magic Desk),
and the optional real-1541 path.

It is not a promise of cycle-perfect demo-scene behavior, full drive mechanics,
exact analog SID, REU, EasyFlash, or every cartridge mapper. See `known-gaps.md`.

`c64_init()` defaults to NTSC. PAL is a config-and-reset choice.

## Dependency direction

```text
main.c    -> control + frontend + runtime + platform + tools + util
frontend  -> runtime + platform + tools + util
control   -> platform + util
runtime   -> machine + tools + util   (also uses SDL for Phi2 pacing)
machine   -> tools + util
tools     -> util
platform  -> util + SDL2
```

Do not add reverse dependencies. Machine code must not include SDL, Nuklear,
frontend, platform, or control headers. Runtime must not include frontend or
control headers. Format parsers under `src/tools/` must not call runtime, SDL,
or frontend.

`src/control` is parse, TCP, and framing. Bind, deferred matching, and command
dispatch live in `src/main.c`. There is no `control_dispatch.c`.

## Thread and state ownership

Four threads; one live `c64_t`.

| Thread | Owns | Must not |
|--------|------|----------|
| SDL / main | Window, Nuklear, frontend, `runtime_client` dispatch, control request drain, deferred table | Touch `c64_t` |
| Runtime worker `"c64m-runtime"` | Live `c64_t`, HST1, Inspector, breakpoints, assembler, save/load, SID into the audio buffer, frame/VIC rings | Be polled from the socket thread |
| SDL audio callback | Read the shared audio buffer | Call runtime or machine |
| Control socket | TCP I/O, request/response queues | `runtime_client` single-consumer surfaces or the machine |

Frontend and control consumers receive copied snapshots, frames, memory,
symbols, and events. No live machine pointer crosses a thread boundary.

Use `c64_copy_*_snapshot` / debug peeks rather than reaching into live machine
state from UI code. Use `runtime_client` rather than calling machine functions
from the frontend.

## Where to change code

- `src/machine/`: emulated hardware and machine-visible state
- `src/runtime/`: execution, sessions, Inspector, recorder, rings, commands
- `src/control/`: wire parse/format and the localhost TCP server
- `src/frontend/`: presentation, dialogs, debugger views, host input mapping
- `src/platform/`: SDL window/input/audio/filesystem/socket
- `src/tools/`: assembler, disassembler, symbols, D64/T64/CRT/G64 parsers
- `src/util/`: queues, config, audio buffer, BASIC V2, paste data
- `src/main.c`: SDL loop, control bind/dispatch, CLI wiring

## Operational rules

- Preserve timing-visible side effects when optimizing.
- Turbo 1 (normal) and 2 (max) keep live pixels and full correctness. Turbo 3
  (warp) may drop paint; returning to 1 or 2 must leave a valid turbo-2 world.
- A direct launch of `./build/c64m` opens a blocking SDL window. Prefer
  `./build/c64m --help`, ctest, or `--headless --control-port N` for automation.
- Run from the repository root so ROM fallback lookup finds `roms/`.
- The control server binds `127.0.0.1` only and accepts one client at a time.
  `quit-client` closes the socket; it does not kill a headless process.
