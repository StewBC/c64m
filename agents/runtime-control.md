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

## Control port

`src/control` implements an opt-in localhost-only server enabled by
`--control-port PORT`. One socket client is accepted at a time. The socket thread
owns blocking network I/O; the SDL/main loop drains requests and sends responses.
`--headless` requires a control port and skips window, renderer, frontend, controller,
and host audio setup while retaining runtime frames for control clients.

Implemented protocol areas include introspection, execution, state/CPU/frame/memory/
debug-memory/call-stack, keyboard/joystick/RESTORE, paste, PRG/BIN/D64 operations,
machine snapshot save/load (`save-state` / `load-state`), breakpoints, waits,
assemble, find-symbol, and `set-turbo`. Binary responses carry a typed header and
raw byte count. Deferred responses are serviced by the main-loop-owned cache.
`set-turbo` changes the active mode without altering the configured Opt+T list;
mode 3 (warp) warns that the live ARGB framebuffer is disabled until turbo is
lowered to 1 or 2. CLI startup also accepts `--sna <path>` for the same snapshot
load path used by `load-state`.

### Turbo semantics and host throughput

Turbo is three discrete modes (not a MHz ladder). Field names still say
`active_turbo_multiplier` / `turbo_speeds` for historical compatibility; values
are mode IDs:

| Mode | Name  | Pacing   | Live ARGB | Notes |
|------|-------|----------|-----------|-------|
| 1    | normal | 1× real-time | yes | PAL ~0.985 MHz / NTSC ~1.023 MHz Φ2 |
| 2    | max   | free-run | yes | Full correctness (collisions, paint) |
| 3    | warp  | free-run | no  | Debug geometry only; collision latches skip |

**Max (mode 2) is the performance bar for full correctness.** On an Apple M2 Mac
Mini (headless PAL, measured 2026-07 after live-paint algorithmic opts), free-run
full paint reaches about **~5.2 MHz** machine Φ2 (~5.3× real-time). Pure
`c64_step_cycle` with video on is higher (~10 MHz); with video off (warp-like
core path) about ~14 MHz. The full product still pays runtime/thread overhead
and dual-1541 ROM stepping; 1541 cost is correctness-required and not an
optimization target here. Do not disable pixel output except in warp (mode 3).

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
