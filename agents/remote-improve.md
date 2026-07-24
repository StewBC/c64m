# Control-port improvements worth making

Not a handoff — a **backlog note** for a future agent. Current wire protocol and
verified capabilities live in `control-port.md`; this file lists only open work.
Items came from using the control port as the measuring instrument against VICE
(binary monitor: VICE `src/monitor/monitor_binary.c`; see `vice-oracle.md`).

For analysis see `threading-efficiency.md`. For a **phased plan** (message
contracts → bulk memory RPC → multi-deferred + multiplexed socket → …, then
functional items below) see `threading-efficiency-roadmap.md`.

Ordered by how much time they still cost in oracle sessions.

---

## 1. `run-to-raster` / conditional breakpoints

~~There is no way to run to a raster position.~~ **Done:** `run-to-raster <line>
[cycle]` (runtime + control port). Frame-accurate capture remains `step-frame`.
Still missing: expression-guarded checkpoints (VICE `CONDITION_SET`).

---

## 2. Main-loop tick latency and bulk memory

**Mostly closed by the threading/control efficiency series (C64M/2, on `main`).**
Details and phase checklist: `threading-efficiency-roadmap.md` §16 measured
baselines.

### Done

| Pain (pre-series) | Fix |
|-------------------|-----|
| 64×1K `get-memory` for a 64K dump (~165 ms headless) | Bulk `get-memory` 1..65536, one RPC (~1.6 ms) |
| One-in-flight socket + single deferred | Multi-deferred (cpu/memory) + multiplexed pipeline (high-water 16); `pipeline()` in `c64_control_client.py` |
| Headless `SDL_Delay(1)` quantize | Wake on control request; poll hard while deferred is active (~1.3 ms mean `get-cpu` paused) |
| UI-paced feeling for automation | Prefer **headless** for oracle; windowed present is still ~16 ms by design |
| Chatty five-way debug poll every frame | Cadence-split telemetry (machine only while free-running) |
| Hot reads always RTT | `get-cpu` / `get-vic` / `get-cia` cache when paused barrier is sealed |

### Still open (oracle UX, not transport)

These were never purely “memcpy / RTT” bugs; they remain product timing races:

- `wait-frame 1` → `get-frame` while **free-running** can still alias frames if one
  main-loop turn exceeds a frame period. Prefer `step-frame` for consecutive frames.
- `run` → `wait-frame 1` → `pause` can still overshoot by a frame (pause accepted
  after the wait completes). Not fixed by pipelining alone.

If those bite a script, fix the wait/pause sequencing (or add a dedicated
“pause-after-N-frames” / barrier), not another bulk-memory pass.

---

## 3. CPU instruction history

~~VICE has `CPUHISTORY_GET` (0x86).~~ **Done (basic):** `set-cpu-history on|off`
and `get-cpu-history [1..64]` with a 64-entry ring of instruction-start state.
Still thinner than VICE (no full disasm stream); enough for “path into this
handler?” with watchpoints.
