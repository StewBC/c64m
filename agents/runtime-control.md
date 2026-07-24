# Runtime and control-port handoff

## Runtime ownership

`src/runtime/runtime_thread.c` owns the runtime thread and live machine. Commands
arrive through `runtime_client`/message queues; runtime publishes copied events,
CPU/machine/debug-memory/frame/symbol snapshots. Runtime supports run, pause, reset,
cycle/instruction stepping, step-over/out, run-to-cursor, finite run counts,
breakpoints, input, paste, disk/file operations, assembler, save/load state, and
direct selection of the active turbo mode (1=normal, 2=max, 3=warp).

The frontend must use `runtime_client`. The control socket thread must not poll
runtime-client single-consumer surfaces or touch the machine directly.

## Frame and audio flow

The runtime polls/publishes completed frame copies. A step can publish a current
frame snapshot so debugger views reflect writes made by that step. Runtime audio
production is cycle-driven and uses the shared audio buffer described in
`sid-audio.md`.

## UI telemetry cadence (Phase 4)

While free-running, the main loop must **not** poll five fat snapshots every
frame. Cadence:

| Stream | When | Mechanism |
|--------|------|-----------|
| **Machine telemetry** | Once per UI present while running | `request_machine_state` / `RUNTIME_EVENT_MACHINE_STATE_RESPONSE` — includes CPU regs, VIC/CIA/SID, drive hardware, frame/cycle stamps |
| **Breakpoint definitions** | On mutation (create/update/clear/enable/load/rearm) and on full debug refresh (startup, step, pause) | `runtime_publish_breakpoints` / `request_breakpoints` |
| **Disk metadata** | On mount/unmount/writable/status request and full debug refresh | `runtime_publish_drive_status` / `request_disk_status` |

Each machine telemetry event carries:

- `runtime_seq` — monotonic publish sequence on the runtime thread
- `cycle` / `cpu_cycles` / `frame_number` / `frame_cycle` — machine time stamps

Use these for Phase 6 cache coherence barriers. Do not treat “looks paused” alone
as freshness.

`request_debug_state()` in `main.c` still does a **full** refresh (telemetry +
tables) for startup and after stop/step. Free-run frame path calls
`request_debug_telemetry()` only.

## Message contracts (identity, delivery, ownership)

These rules are the IPC contract for UI↔runtime and control-port deferred work.
They are enforced in code. Source remains authoritative if prose drifts.

### Request tokens

Every **solicited** runtime command carries an opaque internal `request_token`
(`uint64_t`, monotonic allocator on the producer side: main / control dispatch /
UI when needed).

| Token | Meaning |
|-------|---------|
| **0** | Unsolicited notification or free-running telemetry (no waiter owns it) |
| **non-zero** | Completes exactly one waiter that owns that token |

Rules:

- Runtime results and errors for a solicited command **echo the same token**.
- Main-thread control deferred matching keys on `request_token` first. Type-only
  matching of `CPU_STATE` / similar is forbidden for control completions.
- Control-originated deferred work always uses non-zero tokens.
- UI free-run / poll telemetry may use token 0. A token-0 (or wrong-token) event
  **must never** complete a control deferred entry.
- Wire request ids are a **client/session** concern and are not the same as
  `request_token`. Correlation across threads:

  - Client-facing: `(connection_epoch, wire_request_id)`
  - Runtime-facing: `request_token`

### Connection epoch

On each accepted control connection, main bumps a `connection_epoch` (session
generation). Deferred table entries and in-flight responses are tagged with that
epoch.

On disconnect / `quit-client` teardown:

- Cancel all outstanding deferred entries for that epoch.
- Free any owned RPC payloads for those entries.
- Drain or tag-drop in-flight responses so they cannot be delivered to the next
  client.
- Queues that outlive connections must not leak pointer payloads across sessions.

Duplicate outstanding wire request ids within one epoch: reject with `bad-id`.

### Telemetry slots vs RPC results

| Channel class | Semantics | Examples |
|---------------|-----------|----------|
| **Telemetry / snapshot slots** | Latest-wins; intermediate values may drop; generation may detect replacement | `frame_slot`, free-run machine/CPU telemetry, UI debug-memory *display* slot |
| **RPC results** | Exactly-once delivery to the waiter that owns the token, or explicit cancel/error | Solicited `get-memory`, `get-cpu`, assemble complete, save/load state complete |

A single latest-wins slot is **not** a reliable multi-outstanding RPC channel.
Generation numbers detect overwrite; they do **not** preserve lost results.

Bulk RPC data uses a **bounded result pool keyed by `request_token`** (or
individually owned payloads on a reliable completion path). Do not enlarge
`runtime_event` unions to carry 64K memory or write-history arrays.

### Lossy vs reliable delivery

`message_queue_push` may return false when full. Classification:

| Class | On queue full | Examples |
|-------|----------------|----------|
| **Lossy notification** | Drop OK; optional drop counter | `FRAME_READY`, free-run telemetry |
| **Reliable completion** | Must not silently vanish: backpressure, `busy`/error to the deferred owner, or a completion path that cannot drop without cancel | Token-bearing memory/cpu/vic responses, assemble/save/load complete |
| **Reliable error/cancel** | Same as reliable completion | Runtime error for a token-bearing command; connection teardown cancels outstanding tokens |

Queue saturation for reliable traffic yields **deterministic backpressure or a
`busy`/error response**, not a later deferred timeout with no explanation.

### Wait concurrency

At most **one outstanding control wait** is active at a time (any
`wait-paused` / `wait-running` / `wait-frame` / `wait-event`). A second wait
while one is deferred receives `busy`. Sticky completion latches remain
destructive (one waiter consumes and clears). Continuous events (`frame`, etc.)
only match while a wait is active.

### Heap ownership (payloads)

If commands or events carry heap payloads (Phase 5; bulk pool Phase 1):

- Ownership is explicit: move-owned envelope with release, or pool checkout/checkin.
- Every **discard** path must release: queue-full push failure, step-over/out
  drain drops, `message_queue_destroy`, shutdown, connection epoch cancel.
- No live `c64_t` or other machine pointers in queue items.

### Implementation touchpoints

- Types: `src/runtime/runtime_command.h`, `runtime_event.h`
- Publish / drain: `src/runtime/runtime_thread.c`, `runtime_client.c`
- Deferred match / epoch: `src/main.c`
- Wire concurrency / pipeline: `src/control/control_server.c` (nonblocking
  multiplex, high-water 16)

## Control port

`src/control` implements an opt-in localhost-only server enabled by
`--control-port PORT`. One socket client is accepted at a time. The socket thread
owns network I/O (may pipeline requests); the SDL/main loop drains requests and
sends responses. `--headless` requires a control port and skips window, renderer,
frontend, controller, and host audio setup while retaining runtime frames for
control clients; the headless loop wakes when a control request is queued.

Implemented protocol areas include introspection, execution (`step-frame`,
`run-to-raster`), state/CPU/VIC/CIA/frame/memory (`get-memory` up to 64K bulk /
`set-memory`)/debug-memory/call-stack, CPU history, keyboard/joystick/RESTORE,
paste, PRG/BIN/D64 operations, machine snapshot save/load (`save-state` /
`load-state`), breakpoints (exec/read/write and count-only), waits with sticky
completion events, assemble, find-symbol, and `set-turbo`. Binary responses carry a
typed header and raw byte count. Deferred responses use a multi-entry table and
must follow the message contracts above (`request_token`, epoch, lossy vs reliable).
`set-turbo` changes the active mode without altering the configured Opt+T list;
mode 3 (warp) warns that the live ARGB framebuffer is disabled until turbo is
lowered to 1 or 2. CLI startup also accepts `--sna <path>` for the same snapshot
load path used by `load-state`. Wire protocol is **C64M/2** — see `control-port.md`.

### Turbo semantics and host throughput

Turbo is three discrete modes (not a MHz ladder). Field names still say
`active_turbo_multiplier` / `turbo_speeds` for historical compatibility; values
are mode IDs:

| Mode | Name  | Pacing   | Live ARGB | Notes |
|------|-------|----------|-----------|-------|
| 1    | normal | 1× real-time | yes | PAL ~0.985 MHz / NTSC ~1.023 MHz Φ2 |
| 2    | max   | free-run | yes | Full correctness (collisions, paint) |
| 3    | warp  | free-run | no  | Debug geometry only; collision latches skip |

**Max (mode 2) is the performance bar for full correctness.** Absolute free-run
Φ2 rates and the pure-core vs product cost ladder live in
**`perf-baseline-turbo2.md`** (host, recipes, and comparison rows). Do not
disable pixel output except in warp (mode 3). 1541 units are soft-powered (see
`disk-iec1541.md`); unpowered units are not stepped. When a unit is powered with
ROM loaded, dual-drive cost can dominate free-run — re-measure against the
baseline after changes.

For the actual wire format, command grammar, response payload layouts, and a working
Python client, read `control-port.md`.

## Save-state boundary

Runtime file I/O for machine snapshots happens on the runtime thread. Successful
save/load emits completion events; failed loads preserve the live machine. The
machine serializer does not capture SDL/frontend state; with real 1541 + ROM it
does capture full drive-object state (v9).

## Common pitfalls

`--headless` is not a general multi-client service and `quit-client` only closes the
client. Do not introduce runtime fanout, non-local binding, or socket-thread machine
access without changing the architecture deliberately and adding tests.
