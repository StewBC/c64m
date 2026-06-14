# MASTER.md

# c64m Architecture Master Plan

c64m is a Commodore 64 emulator written in C. It uses SDL2 for host platform services and Nuklear for the UI. The emulator is organized around responsibility boundaries, not around implementation convenience.

## Top-Level Layout

```text
src/
    main.c
    machine/
    runtime/
    tools/
    platform/
    frontend/
    util/
```

## Components

### machine/

The emulated Commodore 64 hardware. This is the source of truth for the machine state.

Owns:

```text
CPU, RAM, ROM, memory map, VIC-II, SID, CIA, cartridge,
keyboard matrix, interrupts, cycle timing, bus behavior.
```

Suggested files:

```text
machine/
    c64.c
    c64.h
    c6510.c
    c6510.h
    memory.c
    memory.h
    ram.c
    ram.h
    rom.c
    rom.h
    vicii.c
    vicii.h
    sid.c
    sid.h
    cia.c
    cia.h
    cartridge.c
    cartridge.h
    keyboard_matrix.c
    keyboard_matrix.h
```

Rules:

```text
No SDL.
No Nuklear.
No frontend code.
No platform code.
No debugger UI concepts.
```

The machine may produce host-side snapshots, such as CPU state, memory copies, SID state, VIC-II state, and completed video pixel buffers. These are data snapshots, not SDL objects.

### runtime/

The execution/session controller. Runtime owns the live machine instance and is the only component allowed to directly control it.

Owns:

```text
run, pause, reset, step, speed mode,
breakpoints, watchpoints,
frame/cycle/instruction stepping,
request/response access to machine state,
publishing snapshots/events to the UI thread.
```

Suggested files:

```text
runtime/
    runtime.c
    runtime.h
    runtime_thread.c
    runtime_thread.h
    runtime_client.c
    runtime_client.h
    runtime_command.c
    runtime_command.h
    runtime_event.c
    runtime_event.h
    breakpoints.c
    breakpoints.h
    watchpoints.c
    watchpoints.h
```

Rules:

```text
Runtime and machine live on the emulation thread.
Frontend does not call machine directly.
Frontend talks to runtime through runtime_client.
Runtime never calls SDL or Nuklear.
Runtime publishes copies/snapshots, not live machine pointers.
```

### tools/

Reusable development tools. These do not own the machine and do not control execution.

Owns:

```text
assembler, disassembler, symbol loading/parsing,
PRG/CRT parsing if useful as reusable tooling.
```

Suggested structure (may evolve under development so don't fixate on this):

```text
tools/
    assembler/
    symbols/
    prg/
    1541/
```

Rules:

```text
No machine ownership.
No SDL.
No Nuklear.
Can be used by frontend and/or runtime.
```

Disassembly logic belongs here. A disassembly view belongs in frontend.

### platform/

The host platform abstraction. This project uses SDL2.

Owns:

```text
SDL init/shutdown, window, renderer, audio device,
input polling, filesystem paths, clipboard, host timing.
```

Suggested files:

```text
platform/
    platform.c
    platform.h
```

Rules:

```text
SDL is isolated here as much as practical.
No machine logic.
No runtime ownership.
No Nuklear UI policy.
```

The platform layer may expose host services to frontend. It should not know about C64 hardware semantics.

### frontend/

The Nuklear UI and user-facing views.

Owns:

```text
menus, memory view, register view, disassembly view,
video output view, debugger panels, settings UI,
command dispatch to runtime_client.
```

Suggested files:

```text
frontend/
    frontend.c
    frontend.h
    frontend_views.c
    frontend_views.h
    view_memory.c
    view_memory.h
    view_registers.c
    view_registers.h
    view_disasm.c
    view_disasm.h
    view_video.c
    view_video.h
    nuklear.h
    nuklear_sdl.h
```

Rules:

```text
Frontend talks to runtime_client, not machine.
Frontend consumes snapshots/events from runtime.
Frontend may use tools for display formatting, such as disassembly.
Frontend owns presentation, not emulation truth.
```

Examples:

```text
Step button:
    frontend -> runtime_client_step()

Memory view:
    frontend -> runtime_client_request_memory()
    runtime -> copies machine memory into response
    frontend renders response

Disassembly view:
    frontend gets memory bytes through runtime_client
    frontend uses tools/disasm_6502 to decode
    frontend renders the decoded lines

Video output:
    frontend consumes latest published VIC-II pixel snapshot
    frontend uploads pixels to SDL texture
    frontend composites Nuklear UI
```

### util/

Small shared support code.

Owns:

```text
logging, ring buffers, message queues, strings,
small containers, assertions, common helpers.
```

Suggested contents:

```text
    logging
    message queues
    ring buffers
    dynamic arrays
    hash tables
    string helpers
    path helpers
    config file helpers
    CRC/checksum code
    thread wrappers
    mutex wrappers
```

Rules:

```text
No SDL unless explicitly named as platform-specific.
No machine ownership.
Keep this boring and small.
```

## Threading Model

The emulator uses two threads from the beginning.

```text
UI/main thread:
    SDL event polling
    SDL rendering
    Nuklear frontend
    sends commands to runtime_client
    consumes runtime events/snapshots

Emulation thread:
    runtime
    machine
    services command queue
    runs the C64
    publishes events/snapshots/frames
```

Runtime and machine must stay on the same thread.

```text
Do not split runtime and machine.
Do not let UI touch the live machine.
Do not pass live machine pointers across threads.
```

## Message Passing

Communication between UI and runtime is through message queues.

UI to runtime commands:

```text
RUN
PAUSE
RESET
STEP_INSTRUCTION
STEP_CYCLE
STEP_FRAME
SET_SPEED_MODE
SET_BREAKPOINT
CLEAR_BREAKPOINT
SET_WATCHPOINT
CLEAR_WATCHPOINT
REQUEST_MEMORY
REQUEST_CPU_STATE
REQUEST_VIC_STATE
REQUEST_SID_STATE
REQUEST_FRAME
LOAD_PROGRAM
QUIT
```

Runtime to UI events/responses:

```text
STARTED
PAUSED
STOPPED
BREAKPOINT_HIT
WATCHPOINT_HIT
FRAME_READY
MEMORY_RESPONSE
CPU_STATE_RESPONSE
VIC_STATE_RESPONSE
SID_STATE_RESPONSE
ERROR
```

The UI should treat runtime as asynchronous even when an operation is quick.

## Video Model

The VIC-II belongs to machine. It produces emulated video state and host-side pixel snapshots.

```text
machine/VIC-II -> host-side pixel snapshot
runtime publishes snapshot
frontend uploads snapshot to SDL texture
frontend draws Nuklear UI
```

The machine does not create SDL textures. The platform/frontend side does that.

For turbo mode, runtime may produce more frames than UI displays. The UI should consume the latest available complete frame and may drop older frames.

## Debug Views

Debug views are frontend presentations of runtime-provided snapshots.

```text
register view     = frontend rendering CPU snapshot
memory view       = frontend rendering copied memory bytes
disassembly view  = frontend + tools/disasm over copied memory bytes
text screen view  = frontend rendering copied screen/color memory
video output      = frontend rendering VIC-II pixel snapshot
```

The frontend never reads live machine memory directly.

## Dependency Direction

Allowed dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
tools    -> util
platform -> util + SDL2
```

Forbidden dependency direction:

```text
machine  -> runtime
machine  -> frontend
machine  -> platform
runtime  -> frontend
runtime  -> platform
platform -> machine
frontend -> machine
```

## Build Notes

The project is C, using CMake.

Recommended C standard:

```text
C99 or C11
```

External libraries:

```text
SDL2
Nuklear, vendored directly into frontend
```

SDL2 discovery may live at the root CMake level because this application is intentionally an SDL2 emulator. Individual targets should still link only what they use.

## Naming and Headers

Use opaque structs for major components.

Example:

```c
typedef struct runtime runtime;
typedef struct runtime_client runtime_client;
typedef struct c64 c64;
typedef struct cpu_6510 cpu_6510;
typedef struct vicii vicii;
typedef struct sid sid;
```

Headers are contracts. They should be self-contained and expose the smallest useful interface.

Avoid exposing implementation structs unless there is a deliberate performance/debug reason.

## Core Rule

The architecture is:

```text
machine = what the C64 is
runtime = how the emulation session runs
tools = reusable development helpers
platform = how the host computer is accessed
frontend = how the user sees and commands it
util = boring shared support
```

The UI commands the emulator through runtime. Runtime owns the machine. The machine is the truth.
