# Control-port improvements worth making

Not a handoff — a **backlog note** for a future agent. Current wire protocol and
verified capabilities live in `control-port.md`; this file lists only open work.
Items came from using the control port as the measuring instrument against VICE
(binary monitor: VICE `src/monitor/monitor_binary.c`; see `vice-oracle.md`).

Ordered by how much time they still cost in oracle sessions.

---

## 1. `run-to-raster` / conditional breakpoints

There is no way to run to a raster position. Frame-accurate capture is covered by
`step-frame`, but per-line run-to (and expression-guarded checkpoints, VICE
`CONDITION_SET`) are still missing. Useful for raster-IRQ demos: stop when the
beam hits a line without reverse-engineering a once-per-frame exec address.

---

## 2. Main-loop tick latency and bulk memory

Every command is dispatched on the SDL main loop, so latency is up to one ~16.7 ms
tick regardless of payload size (measured ~16–18 ms for `get-frame` paused or
running). Consequences while free-running:

- `wait-frame 1` → `get-frame` can sample every other PAL frame (frame deltas of 2)
  when one iteration exceeds the ~20 ms period — silent aliasing of double-buffered
  effects. Prefer `step-frame` when you need consecutive frames.
- `run` → `wait-frame 1` → `pause` overshoots by one frame (pause processed a tick
  after the wait returns).
- Reading 64K via `get-memory` is 64 calls (1024-byte cap) ≈ 1.1 s. VICE `MEM_GET`
  does 8KB in one call.

Worth considering: service several queued requests per tick; raise or remove the
`get-memory` length cap; or add a batch/pipeline form.

---

## 3. CPU instruction history

VICE has `CPUHISTORY_GET` (0x86). c64m has `get-call-stack`, which does not answer
"what path did we take into this handler?". Natural companion to load/store
watchpoints; lower priority than raster run-to and tick throughput.
