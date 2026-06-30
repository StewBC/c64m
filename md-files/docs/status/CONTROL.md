# Control port status

## Current implementation

- Phase 0 design reconciliation is complete.
- Phases 1 through 7 are implemented.
- `md-files/C64MCONTROL.md` is the current design source for this feature.
- `--control-port PORT` enables the server. The default is disabled.
- `--headless --control-port PORT` runs without creating a window or opening
  host audio.
- The server binds only to `127.0.0.1`.
- One client is accepted at a time.
- The socket thread owns blocking network I/O only.
- The SDL main loop drains control requests and posts responses.
- Implemented commands:
  - `hello`;
  - `version`;
  - `capabilities`;
  - `ping`;
  - `quit-client`;
  - `reset`, `run`, `pause`, `step-cycle`, `step-instruction`,
    `step-over`, `step-out`, `run-cycles`, `run-instructions`, `run-to`;
  - `get-state`, `get-cpu`;
  - `get-frame`, `get-memory`, `get-debug-memory`, `get-call-stack`;
  - `key-down`, `key-up`, `restore`, `joystick`;
  - `paste-text`, `paste-events`, `paste-text-data`, `paste-events-data`;
  - `load-prg`, `load-bin`, `save-bin`, `mount-d64`, `unmount-disk`,
    `get-disk-status`;
  - `break-exec`, `break-clear`, `break-enable`, `break-list`,
    `get-breakpoints`, `break-clear-all`, `break-create`, `break-update`,
    `rearm-oneshots`;
  - `wait-paused`, `wait-running`, `wait-frame`, `wait-event`.
- Binary responses use:
  - `<id> data <type> <byte_count> <metadata...>`;
  - `<byte_count>` raw bytes;
  - a trailing newline.
- In SDL mode, frame and debug-memory responses are served only from main-loop
  cached copies or from deferred responses completed after normal main-loop
  runtime polling.

## Phase 0 reconciliation

- Component placement remains:
  - `src/control/` for protocol parsing, response formatting, server/request
    orchestration, and main-loop dispatch helpers.
  - `src/platform/platform_socket.*` for the small blocking TCP wrapper.
  - `src/main.c` for SDL main-loop integration and ownership of runtime-client
    polling.
- The first implemented CLI option should be `--control-port PORT` only.
  It is startup-only, disabled by default, and hard-bound to `127.0.0.1`.
- `--control-bind` and `--control-disable` remain planned follow-ons.
  Non-local binding is intentionally deferred.
- `app_options` is the right place to carry the parsed control port. The option
  should not be persisted to the INI in the first slice.
- `main.c` is currently the sole consumer of:
  - `runtime_client_poll_event`;
  - `runtime_client_poll_frame`;
  - `runtime_client_poll_debug_memory`;
  - `runtime_client_poll_symbols`.
  The control port must preserve that ownership in SDL mode.
- `run_main_loop()` already owns the cached `frontend_debug_state` and calls
  `poll_runtime_events()` once per frame. Phase 1 should drain control requests
  in this loop and respond without touching runtime-client poll surfaces from
  the socket thread.
- `runtime_client` already exposes most commands needed by later phases:
  run/pause/reset, cycle/instruction stepping, step-over/out, run-to-cursor,
  memory/debug-memory/frame requests, call stack, paste/input, disk/file I/O,
  breakpoints, and snapshot polling.
- `util/message_queue` is suitable for bounded socket-thread/main-loop
  request and response queues, including shutdown wakeups.
- `util/thread` is suitable for the control socket thread. It currently uses
  SDL threads, so the server should start after SDL/thread facilities are ready.
- `platform` currently has window and audio services only. Socket portability
  needs a new narrow `platform_socket` wrapper in `src/platform/`.
- CMake currently builds a single `c64m` executable from `src/app_options.c`
  and `src/main.c` plus static component libraries. Phase 1 should add a
  `control` static library and link it into `c64m`; protocol unit tests can
  link that library without `frontend`.

## Completed phases

- Phase 1: localhost control server skeleton.
- Phase 2: execution control, cached `get-state`, fresh deferred `get-cpu`.
- Phase 3: binary `get-frame`, `get-memory`, `get-debug-memory`, and
  `get-call-stack`.
- Phase 4: input, paste, file/media, disk mount/status commands.
- Phase 5: breakpoint list/create/update/clear/enable helpers plus one-shot
  rearm.
- Phase 6: wait commands for paused/running state, frame deltas, and named
  runtime events.
- Phase 7: headless control-port mode with runtime/control polling and frame
  snapshots without Nuklear/frontend state.

Implementation files:

- `src/control/control_protocol.*`
- `src/control/control_server.*`
- `src/platform/platform_socket.*`
- `src/main.c`
- `src/app_options.*`
- `tests/control/test_control_protocol.c`

## Deferred control behavior

- `--control-bind`.
- `--control-disable`.
- Runtime fanout / independent runtime-client subscriptions.
- `save-bin` can overwrite host files; this is accepted for the current
  localhost-only opt-in MVP and needs user-facing help text before broader use.

## Breakpoint control syntax

- `break-exec ADDR` creates an enabled execute breakpoint with the default
  break action.
- `break-create exec ADDR [enabled=0|1] [end=ADDR] [actions=...] [counter=N] [reset=N]`
  creates a breakpoint from an explicit definition.
- `break-update ID exec ADDR [enabled=0|1] [end=ADDR] [actions=...] [counter=N] [reset=N]`
  replaces an existing breakpoint definition.
- `actions` is a comma-separated list of `break`, `fast`, `slow`, `tron`,
  `troff`, `type`, and `swap`.
- Breakpoint list responses use `data breakpoints` with newline-separated text
  rows containing id, enabled state, address range, actions, and counter state.

## Wait command behavior

- `wait-paused [timeout_ms]` completes when the cached frontend runtime state
  becomes paused or is already paused.
- `wait-running [timeout_ms]` completes when the cached frontend runtime state
  becomes running or is already running.
- `wait-frame FRAME_DELTA [timeout_ms]` completes when the main-loop frame
  counter advances by at least `FRAME_DELTA`.
- `wait-event EVENT_NAME [timeout_ms]` completes when a runtime event with the
  matching protocol name is observed. Examples include `running`, `paused`,
  `reset-complete`, `step-complete`, `run-complete`, `frame`, `breakpoints`,
  `disk-status`, and `debug-memory`.
- The default timeout is 2000 ms. Explicit timeouts must be 1..600000 ms.
- Waits use the same single active deferred-response slot as fresh CPU,
  memory, disk, call-stack, and breakpoint responses.

## Headless mode

- `--headless` requires `--control-port PORT`.
- Headless mode initializes SDL timer/event services, starts the runtime and
  control server, and skips platform window, renderer, frontend, controller,
  and host audio-device setup.
- Runtime frame snapshots are still polled by the main loop and cached for
  `get-frame` and `wait-frame`.
- `quit-client` closes the socket client, not the emulator process. Current
  automated headless workflows should terminate the process externally after
  the final client command.

## Tests / smoke checks

- Automated:
  - `ctest --test-dir build --output-on-failure -R '^(app_options|control_protocol)$'`
- Manual smoke:
  - launch `c64m --control-port 6510`;
  - connect to `127.0.0.1:6510`;
  - send `1 ping`;
  - receive `1 ok`.
  - issue `reset`, `step-instruction`, `get-cpu`; verify accepted responses
    and a CPU snapshot response.
  - issue `get-frame`; verify byte count is `384 * 272 * 4`.
  - issue `get-memory $0400 64 map`; verify a 64-byte payload.
  - issue `get-debug-memory`; verify a `196608` byte payload when write
    history is not requested.
  - issue input and media commands such as `key-down return`, `paste-text-data`,
    `mount-d64 8 assets/disks/blank.d64`, `get-disk-status 8`,
    `unmount-disk 8`, and `load-prg samples/el_cartero.prg`.
  - issue breakpoint commands such as `break-exec 0xC000`,
    `break-enable ID 0`, `break-list`, `break-clear ID`,
    `break-create exec 0xC001 actions=break`, `break-update ID exec 0xC002
    enabled=0 actions=break`, and `break-clear-all`.
  - issue wait commands such as `run`, `wait-running 2000`,
    `wait-frame 10 5000`, `pause`, and `wait-paused 2000`.
  - launch `c64m --headless --control-port 6511`; verify `wait-frame` and
    `get-frame` work without creating a visible window.
