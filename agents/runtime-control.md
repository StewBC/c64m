# Runtime and control-port handoff

## Runtime ownership

`src/runtime/runtime_thread.c` owns the runtime thread and live machine. Commands
arrive through `runtime_client`/message queues; runtime publishes copied events,
CPU/machine/debug-memory/frame/symbol snapshots. Runtime supports run, pause, reset,
cycle/instruction stepping, step-over/out, run-to-cursor, finite run counts,
breakpoints, input, paste, disk/file operations, assembler, save/load state, and
direct selection of the active 1..256 turbo multiplier.

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
breakpoints, waits, assemble, find-symbol, and `set-turbo`. Binary responses carry a
typed header and raw byte count. Deferred responses are serviced by the
main-loop-owned cache. `set-turbo` changes the active multiplier without altering
the configured Opt+T list; its 8+ response warns that the live ARGB framebuffer is
disabled until turbo is lowered.

### Turbo semantics and host throughput

Turbo multiplies wall-clock progress of the whole machine (not a CPU-only clock
change): the pacer divides the real video-frame period by the active multiplier
(`frame_counter_step ∝ 1/multiplier`). `turbo=1` tracks PAL ~0.985 MHz / NTSC
~1.023 MHz Φ2; `turbo=2` is exact 2× when the host keeps up. Multipliers that
exceed host capacity free-run at the pacer slip path.

**Turbo 7 is the performance bar for full correctness** (live ARGB, collisions,
full VIC paint). On an Apple M2 Mac Mini (headless PAL, measured 2026-07 after
live-paint algorithmic opts), turbo 7 free-runs at about **~5.2 MHz** machine Φ2
(~5.3× real-time). Pure `c64_step_cycle` with video on is higher (~10.5 MHz)
because the full product still pays runtime/thread overhead and dual-1541 ROM
stepping; 1541 cost is correctness-required and not an optimization target here.
Do not raise free-run speed by disabling pixel output below turbo 8.

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
