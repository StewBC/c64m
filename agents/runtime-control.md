# Runtime and control-port handoff

## Runtime ownership

`src/runtime/runtime_thread.c` owns the runtime thread and live machine. Commands
arrive through `runtime_client`/message queues; runtime publishes copied events,
CPU/machine/debug-memory/frame/symbol snapshots. Runtime supports run, pause, reset,
cycle/instruction stepping, step-over/out, run-to-cursor, finite run counts,
breakpoints, input, paste, disk/file operations, assembler, and save/load state.

The frontend must use `runtime_client`. The control socket thread must not poll
runtime-client single-consumer surfaces or touch the machine directly.

## Frame and audio flow

The runtime polls/publishes completed frame copies. A step can publish a current
frame snapshot so debugger views reflect writes made by that step. Runtime audio
production is cycle-driven and uses the shared audio buffer described in
`sid-audio.md`.

## Control port

`src/control` implements an opt-in localhost-only server enabled by
`--control-port PORT`. One socket client is accepted at a time. The socket thread
owns blocking network I/O; the SDL/main loop drains requests and sends responses.
`--headless` requires a control port and skips window, renderer, frontend, controller,
and host audio setup while retaining runtime frames for control clients.

Implemented protocol areas include introspection, execution, state/CPU/frame/memory/
debug-memory/call-stack, keyboard/joystick/RESTORE, paste, PRG/BIN/D64 operations,
breakpoints, waits, assemble, and find-symbol. Binary responses carry a typed header
and raw byte count. Deferred responses are serviced by the main-loop-owned cache.

For the actual wire format, command grammar, response payload layouts, and a working
Python client, read `control-port.md`.

## Save-state boundary

Runtime file I/O for machine snapshots happens on the runtime thread. Successful
save/load emits completion events; failed loads preserve the live machine. The
machine serializer does not capture SDL/frontend state or full 1541 CPU/VIA state.

## Common pitfalls

`--headless` is not a general multi-client service and `quit-client` only closes the
client. Do not introduce runtime fanout, non-local binding, or socket-thread machine
access without changing the architecture deliberately and adding tests.
