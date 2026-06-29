# C64M Control Port Design

## Purpose

Add an optional localhost control port that lets automated tools drive and
inspect a running emulator through the same safe command/snapshot model used by
the frontend.

The goal is not to remote-control the Nuklear UI. The goal is to make emulator
debugging easier by giving agents and scripts repeatable access to:

- execution control;
- C64 input injection;
- current frame pixels;
- CPU, runtime, and hardware snapshots;
- memory/debug-memory snapshots;
- breakpoint/watchpoint-style debugging surfaces.

Example target workflow:

```text
Start c64m with --control-port 6510.
Load a PRG or D64.
Run to frame N, breakpoint X, raster condition, or manual pause.
Fetch the C64 display frame.
Inspect VIC-II sprite registers, CPU state, memory, and write history.
Step forward and compare frame/state snapshots.
```

This should reduce one-off instrumentation, logging, and manual GUI driving
when investigating bugs such as corrupt sprites, incorrect raster timing,
loader hangs, or input-dependent failures.

## Non-Goals

- No remote GUI protocol in the first implementation.
- No commands for selecting Nuklear tabs, virtual memory views, or UI focus.
- No direct machine access from the socket layer.
- No live machine pointers crossing threads.
- No public network service by default.
- No multi-user collaborative debugger in the initial design.
- No attempt to replace normal unit tests or existing diagnostic programs.

If a later phase needs remote UI state, expose explicit semantic frontend
state through frontend-owned APIs. Do not scrape Nuklear draw commands and do
not mutate `frontend` internals from a socket thread.

## Architectural Fit

Existing rules from `AGENTS.md` and `MASTER.md` remain in force:

```text
UI thread:
    SDL
    renderer
    frontend

Runtime thread:
    runtime
    live machine

Frontend/socket/control surfaces:
    send commands through runtime_client
    consume copied snapshots/events only
```

The control port must be a peer of the frontend at the runtime-client boundary,
not a peer of the machine.

Allowed dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
platform -> util + SDL2
```

Recommended placement:

```text
src/control/
    control_server.c
    control_server.h
    control_protocol.c
    control_protocol.h
    control_dispatch.c
    control_dispatch.h
```

`src/control/` may depend on:

```text
runtime_client
runtime_event
c64_frame
util
platform_socket
```

It must not depend on:

```text
machine internals
frontend internals
Nuklear
SDL renderer/window APIs
```

Socket portability belongs behind a small `platform_socket` wrapper in
`src/platform/`. This keeps POSIX/WinSock conditionals out of the control
protocol and dispatch code, and matches the project's pattern of isolating host
platform services in `platform/`.

`platform_socket` should expose only the small blocking TCP operations needed by
the control server:

```text
listen localhost:port
accept one client
read bytes
write all bytes
close socket
wake/close listener during shutdown
```

It must not know about C64, runtime, frontend, or protocol semantics.

## Critical Event Ownership Constraint

Current `runtime_client` response surfaces are mostly single-consumer:

- one event queue;
- one frame slot;
- one debug-memory slot;
- one symbol slot.

A socket thread must not blindly call:

```text
runtime_client_poll_event
runtime_client_poll_frame
runtime_client_poll_debug_memory
runtime_client_poll_symbols
```

while the SDL frontend is also active, because it may steal data the UI expects.

This is the main design constraint.

## Runtime-Control Model

### Phase 1 Model: Main-Loop Dispatch

For dual-active SDL UI plus control port, the safest first design is:

```text
control socket thread:
    accepts client
    parses request
    pushes control_request into a control request queue
    waits for control_response
    writes response to client

main/UI thread:
    drains control requests once per frame
    calls runtime_client functions
    uses its existing debug_state/frame copies for immediate responses
    polls runtime events in the normal single-consumer path
    completes deferred control responses when requested data arrives

runtime thread:
    unchanged
```

This preserves single-consumer event ownership. The main loop remains the only
consumer of runtime events and frame/debug-memory slots when the normal UI is
running.

### Deferred Responses

Some commands can be answered from the main loop's already-cached state. Others
need a runtime request and must complete after a future runtime event arrives.
Do not block the main loop while waiting.

Represent these waits explicitly. If the implementation later supports client
ids or reconnects, include a client id or connection generation in this record
so stale deferred responses cannot be delivered to a new client:

```c
typedef enum control_wait_kind {
    CONTROL_WAIT_NONE = 0,
    CONTROL_WAIT_CPU_STATE,
    CONTROL_WAIT_MACHINE_STATE,
    CONTROL_WAIT_MEMORY,
    CONTROL_WAIT_DEBUG_MEMORY,
    CONTROL_WAIT_BREAKPOINTS,
    CONTROL_WAIT_DISK_STATUS,
    CONTROL_WAIT_CALL_STACK,
    CONTROL_WAIT_PAUSED,
    CONTROL_WAIT_RUNNING,
    CONTROL_WAIT_FRAME_DELTA,
    CONTROL_WAIT_EVENT
} control_wait_kind;

typedef struct deferred_control_response {
    uint32_t client_generation;
    uint32_t request_id;
    control_command_type command_type;
    control_wait_kind wait_kind;
    uint64_t deadline_ms;
    uint64_t start_frame_number;
    uint16_t memory_address;
    uint16_t memory_length;
    runtime_memory_mode memory_mode;
    uint8_t disk_device;
    bool active;
} deferred_control_response;
```

Phase 1 may allow only one active deferred response per connected client. If a
second deferred command arrives while one is active, return:

```text
<id> error busy deferred-response-active
```

Later phases may replace this with a small bounded deferred-response array.

Lifecycle:

- socket thread parses request and pushes it to the main loop;
- main loop either answers immediately or records a deferred response;
- main loop issues any needed `runtime_client_request_*` command;
- normal runtime polling updates the cached debug state;
- after each poll, the main loop checks deferred responses and posts completed
  responses to the socket thread;
- if the client disconnects, all deferred responses for that client are
  cancelled without touching runtime state;
- if the deadline expires, complete with `error timeout`;
- shutdown cancels deferred responses before joining the socket thread.

This mechanism is also used by `wait-*` commands. A wait command is never a
busy loop and never blocks SDL rendering or event processing.

### Later Model: Runtime Fanout

If the control port becomes central, consider adding runtime fanout support:

```text
runtime_create_client(rt) -> independent runtime_client subscription
```

Each client would have its own event queue and optional frame/debug-memory
slots. That is a larger architectural change and should not be Phase 1.

## Headless Mode

Headless mode is useful but should not be the first milestone unless SDL window
requirements are blocking automated use.

Possible later CLI:

```text
c64m --headless --control-port 6510 --noaudio
```

In headless mode, the control server may be the sole runtime-client consumer.
This simplifies event ownership but requires a separate app loop for:

- runtime event polling;
- response dispatch;
- optional frame requests;
- clean shutdown;
- optional audio disabling.

Headless should reuse the same protocol and dispatch code as SDL mode.

Headless may require a non-trivial platform startup split. SDL can be initialized
without a visible window, but the current platform path should be audited for
assumptions that video/window creation always happens before runtime, event, or
audio setup.

## Protocol Requirements

Use a request/response protocol with explicit framing.

Properties:

- localhost-only by default;
- one client initially;
- line-oriented text commands for simple requests;
- binary-safe payload framing for frame/debug-memory responses and large input
  payloads;
- protocol version command;
- clear success/error replies;
- bounded request sizes;
- bounded response sizes where practical;
- no unbounded path or text payloads.

Recommended first protocol:

```text
C64M/1 line protocol

Request:
    <id> <command> [args...]\n

Text response:
    <id> ok <key=value ...>\n
    <id> error <code> <message>\n

Binary response:
    <id> data <type> <byte_count> <metadata...>\n
    <byte_count raw bytes>
    \n
```

Use monotonically increasing request ids supplied by the client. The server may
process Phase 1 commands serially and return one response per request.

JSON is acceptable if the project prefers it later, but a tiny C parser is
lower-risk for the first slice.

Large or arbitrary text inputs must use length-prefixed payload framing rather
than newline-delimited arguments:

```text
<id> paste-text-data <byte_count>\n
<byte_count raw UTF-8/control text bytes>
\n

<id> paste-events-data <byte_count>\n
<byte_count raw paste-event syntax bytes>
\n
```

The server decodes the framed payload and then passes it to the existing paste
parser/runtime paths. This avoids inventing a second escaping scheme and allows
newlines or backslash-heavy paste syntax without confusing the line parser.

## Initial Command Set

### Connection / Introspection

```text
hello
version
capabilities
ping
quit-client
```

`capabilities` should report optional features, for example:

```text
frame state memory debug-memory breakpoints input disk step
```

### Execution

```text
reset
run
pause
step-cycle
step-instruction
step-over
step-out
run-cycles <count>
run-instructions <count>
run-to <addr>
```

All execution commands should return after command acceptance, not necessarily
after the emulator reaches a new state, unless the command name explicitly says
`wait`.

Add wait commands separately:

```text
wait-paused [timeout_ms]
wait-running [timeout_ms]
wait-frame <frame_delta> [timeout_ms]
wait-event <event_name> [timeout_ms]
```

### State / Snapshots

```text
get-state
get-cpu
get-frame [format=argb8888]
get-memory <addr> <length> <mode>
get-debug-memory [write-history=0|1]
get-breakpoints
get-disk-status <device>
get-call-stack
```

`get-frame` should return the latest completed `c64_frame` copied through the
normal runtime/frontend path. Metadata should include at least:

```text
width height stride format frame_number machine_cycle dropped_frames
```

In SDL mode, `get-frame` returns the main loop's cached latest submitted frame.
The control server must not poll the runtime frame slot directly. The acceptable
staleness is the latest frame observed by the main loop, normally at most one UI
tick behind runtime publication.

`get-memory` should use existing runtime memory modes:

```text
map
ram
rom
```

### Input

```text
key-down <c64_key>
key-up <c64_key>
restore
joystick <port> <mask>
paste-text <escaped_text>
paste-events <encoded_events>
paste-text-data <byte_count>
paste-events-data <byte_count>
```

Reuse existing C64 input paths. Do not inject host SDL keycodes directly into
runtime.

`paste-text` and `paste-events` line forms are for short payloads that contain no
raw newline. The `*-data` forms are the required path for arbitrary text.

### Files / Media

```text
load-prg <path>
load-bin <path> <addr> <use_file_addr> <reset_first> <is_basic>
save-bin <path> <start> <end> <write_file_addr> <is_basic>
mount-d64 <device> <path>
unmount-disk <device>
```

Paths should be bounded to the existing runtime command path length. The first
implementation may trust local paths because the service is localhost opt-in,
but it should still reject empty paths and overlong paths.

`save-bin` can overwrite host files. This is an accepted risk for the
localhost-only, opt-in MVP, but it must be documented in user-facing help once
the command exists. A later hardening phase may add an allowed output directory.

### Breakpoints

Minimum:

```text
break-exec <addr>
break-clear <id>
break-enable <id> <0|1>
break-list
```

Later:

```text
break-create <definition>
break-update <id> <definition>
break-clear-all
rearm-oneshots
```

Use the existing `runtime_breakpoint_definition` semantics. Do not invent a
second breakpoint engine.

## Response Data Model

The control port should expose semantic emulator data, not GUI render data.

Good responses:

- CPU registers and cycle counters;
- runtime running/paused state;
- stop reason;
- frame metadata and pixels;
- memory bytes;
- VIC-II/CIA/SID hardware snapshots already present in `runtime_machine_snapshot`;
- breakpoint definitions/snapshots;
- disk status.

Avoid:

- current Nuklear tab;
- mouse hover state;
- virtual memory panel scroll position;
- rendered debugger text;
- host window geometry.

If an agent needs disassembly text later, add a control command that combines
memory bytes with the existing tools disassembler and returns semantic rows:

```text
disassemble <addr> <count> <mode>
```

In SDL mode, frame and debug-memory responses come from main-loop-owned cached
copies. If no cached copy exists, the main loop issues the matching runtime
request and completes a deferred response when the normal runtime event/slot
polling path has updated the cache. The socket thread never consumes the
single-slot runtime handoff objects directly.

## Phased Development Plan

### Phase 0: Design Reconciliation

Deliverables:

- Confirm component placement and dependency direction.
- Add CLI option design:

```text
--control-port 6510
--control-bind 127.0.0.1
--control-disable
```

- Confirm the `platform_socket` wrapper API.
- Confirm text protocol details and the length-prefixed payload forms for
  arbitrary paste data.
- Confirm deferred response state storage and timeout behavior.

Acceptance:

- No code required, but design must name threading and event ownership.
- Update `docs/status/FRONTEND_DEBUGGER.md` or a new control status file if
  implementation begins.

### Phase 1: Localhost Control Server Skeleton

Scope:

- Add optional server startup controlled by CLI/INI.
- Bind only to `127.0.0.1`.
- Accept one client.
- Implement `hello`, `version`, `capabilities`, `ping`, `quit-client`.
- Add request and response queues between socket thread and main loop.
- Main loop drains requests and posts responses.
- Clean shutdown joins the socket thread.

Out of scope:

- runtime commands;
- frame/memory binary payloads;
- headless mode.

Acceptance:

- Build succeeds.
- Unit tests cover protocol parsing/formatting.
- Manual smoke:

```text
c64m --control-port 6510
connect to localhost:6510
send: 1 ping
receive: 1 ok
```

### Phase 2: Execution Control and Basic State

Scope:

- Implement:

```text
run
pause
reset
step-cycle
step-instruction
step-over
step-out
run-cycles
run-instructions
run-to
get-state
get-cpu
```

- All runtime commands dispatch through `runtime_client`.
- The main loop remains the single event consumer.
- `get-state` may read from the main loop's cached `frontend_debug_state` copy.
- `get-cpu` should request a fresh CPU state unless the caller explicitly asks
  for cached state in a later protocol extension.
- Add the deferred response mechanism described above for commands that need a
  fresh runtime response.
- `step-over` and `step-out` are forwarded to existing runtime-client commands.
  The control layer must not reimplement call-depth or stack-walk logic.

Acceptance:

- Automated or integration test can issue `reset`, `step-instruction`,
  `get-cpu` and observe PC/cycle movement.
- SDL UI still receives runtime events normally while the control client is
  connected.

### Phase 3: Frame and Memory Inspection

Scope:

- Implement:

```text
get-frame
get-memory
get-debug-memory
get-call-stack
```

- `get-frame` returns the latest submitted `c64_frame`.
- In SDL mode, `get-frame` reads the main loop's cached frame copy, not the
  runtime frame slot.
- `get-memory` can either:
  - request through runtime and complete response when the event arrives, or
  - use the latest cached debug-memory snapshot when already available and
    valid for the requested mode.
- `get-debug-memory` returns the main loop's most recently received cached
  debug-memory snapshot. If no cached copy exists, issue one runtime request and
  complete a deferred response after the main loop's normal slot polling updates
  the cache.
- The acceptable staleness for cached frame/debug-memory responses is the latest
  copy observed by the main loop. Freshness-sensitive clients should use
  `wait-frame` before `get-frame`, or request a deferred fresh memory snapshot.

Acceptance:

- Client can fetch a binary ARGB8888 frame with correct byte count.
- Client can fetch Map/RAM/ROM memory bytes.
- Frame-fetch tests verify the full binary payload is delivered exactly, using
  `width * height * 4` bytes for ARGB8888.
- A sprite-debugging script can run, pause, fetch frame, fetch VIC snapshot,
  and inspect sprite register bytes.

### Phase 4: Input, Disk, and File Commands

Scope:

- Implement:

```text
key-down
key-up
restore
joystick
paste-text
paste-events
paste-text-data
paste-events-data
load-prg
load-bin
save-bin
mount-d64
unmount-disk
get-disk-status
```

- Reuse existing paste parser/event types.
- Reuse existing runtime disk/load/save commands.
- Use length-prefixed payload forms for arbitrary paste text or paste-event
  syntax containing raw newlines.

Acceptance:

- Client can type BASIC commands through the C64 input path.
- Client can mount a D64 and run existing autorun or manual LOAD workflow.
- Existing UI disk status remains coherent.

### Phase 5: Breakpoint Management

Scope:

- Implement:

```text
break-exec
break-list
break-clear
break-enable
break-clear-all
break-create
break-update
rearm-oneshots
```

- Reuse `runtime_breakpoint_definition`.
- Add protocol helpers for breakpoint action fields.

Acceptance:

- Client can set an execute breakpoint, run, wait for pause, inspect stop
  reason, and clear the breakpoint.
- Existing Breakpoint Editor sees changes after refresh.

### Phase 6: Wait / Automation Quality

Scope:

- Implement wait commands:

```text
wait-paused
wait-running
wait-frame
wait-event
```

- Add timeouts and cancellation on disconnect.
- Add structured stop reasons and event summaries.
- Implement waits as deferred responses checked by the main loop after normal
  runtime polling. The socket thread may block waiting for the response queue,
  but the main loop must never block waiting for a wait condition.

Acceptance:

- Client scripts do not need arbitrary sleeps for normal workflows.
- A script can:

```text
reset
mount disk
run
wait-frame 10 5000
get-frame
pause
get-state
```

### Phase 7: Headless Mode

Scope:

- Add `--headless --control-port PORT`.
- Run without creating a visible SDL window if platform/audio constraints allow.
- Keep runtime event polling and control dispatch alive.
- Support `get-frame` from runtime frame snapshots.
- Audit platform initialization so headless mode does not require SDL window or
  renderer creation. Audio should be explicitly disabled or initialized through
  a path that does not assume a window exists.

Acceptance:

- Automated control-port workflows can run in CI-like environments.
- No dependency on Nuklear/frontend state.

### Phase 8: Optional Runtime Fanout

Scope:

- Add support for multiple independent runtime clients or event subscribers.
- Give control server its own event queue/frame slot/debug-memory slot.
- Reduce dependence on main-loop cached state.

Acceptance:

- SDL UI and socket client can independently request and consume frames/events.
- No event stealing between clients.

Only do this if earlier phases prove the control port is valuable enough to
justify the architectural change.

## Threading Details

Recommended Phase 1 structures:

```c
typedef struct control_request {
    uint32_t id;
    control_command_type type;
    control_args args;
} control_request;

typedef struct control_response {
    uint32_t id;
    control_response_type type;
    char text[...];
    uint8_t *payload;
    size_t payload_size;
} control_response;
```

Queues:

```text
socket thread -> main loop: control_request_queue
main loop -> socket thread: control_response_queue
```

Rules:

- Socket thread owns sockets and blocking network I/O.
- Main thread owns frontend/debug-state copies.
- Runtime thread owns live machine.
- Do not block the main loop indefinitely waiting for runtime responses.
- Deferred control responses should carry request ids and complete on later
  main-loop ticks.

## Security / Safety

Default behavior:

```text
control port disabled
bind address 127.0.0.1
one client
no remote network bind unless explicitly requested
```

Validation:

- reject overlong lines;
- reject overlong paths;
- reject unknown commands;
- reject invalid numeric values;
- cap binary response sizes;
- close client on protocol desynchronization if recovery is unsafe.

Future:

- optional auth token if non-local bind is ever allowed;
- explicit warning log for non-local bind;
- consider compile-time disable flag for release builds if desired.

## Testing Plan

Unit tests:

- protocol tokenizer/parser;
- numeric parsing;
- escaping/unescaping;
- response formatting;
- binary framing header generation;
- invalid command rejection.

Integration tests:

- start runtime without relying on SDL UI where possible;
- issue command requests through the control dispatch layer;
- assert runtime-client calls or observed runtime state.

Manual smoke:

- connect with a small script or netcat-like client;
- `ping`;
- `reset`;
- `step-instruction`;
- `get-cpu`;
- `get-frame`;
- `get-memory 0400 0040 map`;
- set breakpoint, run, wait pause, inspect state.

Regression checks:

- normal SDL UI still runs without `--control-port`;
- normal SDL UI still runs with a connected idle control client;
- disconnected client does not hang shutdown;
- full request queue does not crash or corrupt runtime state.

## Documentation Updates During Implementation

When implementation begins, update:

- `md-files/STATUS.md` only for top-level routing/current baseline facts;
- `md-files/docs/status/FRONTEND_DEBUGGER.md` if the control port is treated
  as frontend/debugger infrastructure;
- or add `md-files/docs/status/CONTROL.md` if the feature becomes large enough
  to deserve its own handoff file;
- `md-files/docs/status/TESTING.md` for new test and smoke expectations.

## Recommended MVP

The smallest useful version is:

```text
--control-port 6510
hello/version/ping
run/pause/reset/step-instruction
get-state/get-cpu
get-frame
get-memory
```

That MVP is enough to validate the core claim:

```text
An agent can drive the emulator, look at the display buffer, step execution,
and inspect machine state without adding one-off logs or instrumentation.
```

If that proves valuable on a real bug, continue into breakpoints, waits, and
headless mode.
