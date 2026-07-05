# MASTER.md

# c64m Architecture Master Plan

c64m is a Commodore 64 emulator written in C. It uses SDL2 for host platform
services and Nuklear for the UI. The emulator is organized around responsibility
boundaries, not around implementation convenience.

The current product milestone is PAL and NTSC C64 fidelity for ordinary software,
not complete hardware recreation and not full external peripheral emulation.

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

## Documentation Layout

The architecture source of truth is this file.

Agent/process rules live in:

```text
AGENTS.md
```

The current handoff entry point is:

```text
STATUS.md
```

`STATUS.md` is intentionally short. It routes agents to focused component handoff
files rather than duplicating every implementation fact.

Detailed current implementation status lives in:

```text
docs/status/README.md
docs/status/VICII.md
docs/status/CIA.md
docs/status/SID.md
docs/status/AUDIO.md
docs/status/CPU_MACHINE.md
docs/status/FRONTEND_DEBUGGER.md
docs/status/DISK_IO.md
docs/status/TESTING.md
docs/status/DEFERRED.md
docs/status/OPTIMIZATIONS.md
```

Historical monolithic status may be preserved as:

```text
docs/status/ORIGINAL_STATUS.md
```

Documentation ownership rule:

```text
MASTER.md                 architecture and responsibility boundaries
AGENTS.md                 agent workflow and process rules
STATUS.md                 short current handoff and routing notes
docs/status/<COMPONENT>   detailed current component facts
docs/status/DEFERRED.md   known gaps and future work
docs/status/TESTING.md    test and smoke-check expectations
```

Do not let `STATUS.md` grow back into a monolithic implementation log. When a
detailed fact belongs to a component, update that component file and keep only a
short top-level routing note in `STATUS.md`.

## Components

### machine/

The emulated Commodore 64 hardware. This is the source of truth for the machine
state.

Owns:

```text
CPU, RAM, ROM, memory map, VIC-II, SID, CIA, cartridge slot state,
keyboard matrix, joystick line state, interrupts, cycle timing, bus behavior.
```

Rules:

```text
No SDL.
No Nuklear.
No frontend code.
No platform code.
No debugger UI concepts.
No runtime ownership.
```

The machine may produce host-side data snapshots, such as CPU state, memory
copies, SID state, VIC-II state, CIA state, and completed video pixel buffers.
These are copied data snapshots, not SDL objects.

SID emulation lives here. SID register reads and writes are machine-visible C64
hardware behavior. SID sample generation may be driven by runtime stepping, but
runtime does not own SID behavior.

### runtime/

The execution/session controller. Runtime owns the live machine instance and is
the only component allowed to directly control it.

Owns:

```text
run, pause, reset, step, speed mode,
breakpoints, watchpoints,
frame/cycle/instruction stepping,
request/response access to machine state,
publishing snapshots/events to the UI thread,
feeding generated audio samples into an approved shared audio buffer.
```

Rules:

```text
Runtime and machine live on the emulation thread.
Frontend does not call machine directly.
Frontend talks to runtime through runtime_client.
Runtime never calls SDL or Nuklear.
Runtime never imports platform headers.
Runtime publishes copies/snapshots, not live machine pointers.
```

Runtime may write audio samples to a dependency-safe buffer type owned by `util/`
or another approved shared module. Runtime must not know which host audio API
will consume those samples.

### tools/

Reusable development tools. These do not own the machine and do not control
execution.

Owns:

```text
assembler, disassembler, symbol loading/parsing,
PRG/D64/CRT parsing if useful as reusable tooling,
small offline validation helpers where appropriate.
```

Rules:

```text
No machine ownership.
No SDL.
No Nuklear.
Can be used by frontend and/or runtime.
```

Disassembly logic belongs here. A disassembly view belongs in frontend.

File-format parsing can live here when the parser is reusable and has no machine
ownership. Runtime or frontend decides how parsed data is used.

### platform/

The host platform abstraction. This project uses SDL2.

Owns:

```text
SDL init/shutdown, window, renderer, audio device,
input polling, filesystem paths, clipboard, host timing.
```

Rules:

```text
SDL is isolated here as much as practical.
No machine logic.
No runtime ownership.
No Nuklear UI policy.
```

The platform layer may expose host services to frontend. It should not know about
C64 hardware semantics.

The SDL audio device and SDL audio callback live here. The callback may read from
an approved shared audio sample buffer. It must not call runtime, machine, tools,
or frontend code.

### frontend/

The Nuklear UI and user-facing views.

Owns:

```text
menus, memory view, register view, disassembly view,
video output view, debugger panels, settings UI,
configuration dialogs,
command dispatch to runtime_client.
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
small containers, assertions, common helpers,
dependency-safe audio sample buffer types.
```

Rules:

```text
No SDL unless explicitly named as platform-specific.
No machine ownership.
Keep this boring and small.
```

The generic audio sample buffer should live here if both runtime and platform need
to access it. It should be a narrow single-producer/single-consumer interface or
another explicitly documented thread-safe design.

## Threading Model

The emulator has two primary application threads plus the SDL audio callback
thread when audio is enabled.

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
    writes generated SID samples to the audio sample buffer

SDL audio callback thread:
    owned by SDL/platform
    reads samples from the audio sample buffer
    fills the host audio stream
```

Runtime and machine must stay on the same thread.

```text
Do not split runtime and machine.
Do not let UI touch the live machine.
Do not pass live machine pointers across threads.
Do not let the SDL audio callback call runtime or machine.
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
REQUEST_CIA_STATE
REQUEST_FRAME
LOAD_PROGRAM
MOUNT_D64
UNMOUNT_D64
APPLY_CONFIG
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
CIA_STATE_RESPONSE
DISK_STATUS_RESPONSE
CONFIG_APPLIED
ERROR
```

The UI should treat runtime as asynchronous even when an operation is quick.

Audio output is not routed through the command/event queue. SID samples flow
through an approved shared audio buffer. That buffer is a narrow data path, not a
back channel for control messages.

## Video Model

The VIC-II belongs to machine. It produces emulated video state and host-side
pixel snapshots.

```text
machine/VIC-II -> host-side pixel snapshot
runtime publishes snapshot
frontend uploads snapshot to SDL texture
frontend draws Nuklear UI
```

The machine does not create SDL textures. The platform/frontend side does that.

For turbo mode, runtime may produce more frames than UI displays. The UI should
consume the latest available complete frame and may drop older frames.

PAL and NTSC are both first-class configuration modes for the current milestone.
Any timing table that differs between PAL and NTSC must be selected through the
machine configuration path, not by hard-coded frontend policy.

## Audio Model

The SID belongs to machine. It produces emulated SID state and generated audio
samples.

```text
machine/SID -> generated samples
runtime writes samples into util audio buffer
platform SDL audio callback reads samples from util audio buffer
host audio device plays samples
```

Rules:

```text
Machine does not call SDL.
Runtime does not call SDL.
Platform does not know SID semantics.
Frontend does not generate audio.
The audio callback does not control emulation.
```

Audio generation must tolerate normal host jitter. Buffer underrun should produce
silence or a documented fallback, not undefined behavior. Buffer overrun should
drop or replace samples according to a documented policy.

## Debug Views

Debug views are frontend presentations of runtime-provided snapshots.

```text
register view     = frontend rendering CPU snapshot
memory view       = frontend rendering copied memory bytes
disassembly view  = frontend + tools/disasm over copied memory bytes
text screen view  = frontend rendering copied screen/color memory
video output      = frontend rendering VIC-II pixel snapshot
SID view          = frontend rendering copied SID snapshot
CIA view          = frontend rendering copied CIA snapshot, if present
```

The frontend never reads live machine memory directly.

Debugger memory reads of side-effecting hardware registers must use safe peek or
snapshot paths, not normal CPU-visible reads.

## Dependency Direction

Allowed dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> tools + util
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

SDL2 discovery may live at the root CMake level because this application is
intentionally an SDL2 emulator. Individual targets should still link only what
they use.

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
typedef struct cia cia;
typedef struct audio_ring audio_ring;
```

Headers are contracts. They should be self-contained and expose the smallest
useful interface.

Avoid exposing implementation structs unless there is a deliberate performance or
debug reason.

## Milestone Boundary

The current milestone is not allowed to silently grow into peripheral emulation.

The following remain future work unless a later milestone explicitly changes the
scope:

```text
IEC protocol
1541 CPU/ROM emulation
fast loaders
real 1541 DOS writes and broad disk mutation
full Commodore DOS behavior
advanced cartridge banking
bit-perfect SID analog modeling
full CIA pin/race accuracy
VIC-II light pen
open-bus recreation
```

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

The UI commands the emulator through runtime. Runtime owns the machine. The
machine is the truth.
