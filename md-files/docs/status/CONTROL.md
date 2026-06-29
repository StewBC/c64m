# Control port status

## Current implementation

- Phase 0 design reconciliation is complete.
- Phase 1 localhost control server skeleton is implemented.
- `md-files/C64MCONTROL.md` is the current design source for this feature.
- `--control-port PORT` enables the server. The default is disabled.
- The server binds only to `127.0.0.1`.
- One client is accepted at a time.
- The socket thread owns blocking network I/O only.
- The SDL main loop drains control requests and posts responses.
- Implemented commands:
  - `hello`;
  - `version`;
  - `capabilities`;
  - `ping`;
  - `quit-client`.

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

## Phase 1 target

Complete.

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
- Runtime command dispatch.
- Binary frame and memory payloads.
- Deferred runtime responses and wait commands.
- Headless mode.
- Runtime fanout / independent runtime-client subscriptions.

## Tests / smoke checks

- Automated:
  - `ctest --test-dir build --output-on-failure -R '^(app_options|control_protocol)$'`
- Manual smoke:
  - launch `c64m --control-port 6510`;
  - connect to `127.0.0.1:6510`;
  - send `1 ping`;
  - receive `1 ok`.
