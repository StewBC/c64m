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

Every command is dispatched on the SDL main loop, so latency is up to one ~16.7 ms
tick regardless of payload size (measured ~16–18 ms for `get-frame` paused or
running). Consequences while free-running:

- `wait-frame 1` → `get-frame` can sample every other PAL frame (frame deltas of 2)
  when one iteration exceeds the ~20 ms period — silent aliasing of double-buffered
  effects. Prefer `step-frame` when you need consecutive frames.
- `run` → `wait-frame 1` → `pause` overshoots by one frame (pause processed a tick
  after the wait returns).
- ~~Reading 64K via `get-memory` is 64 calls (1024-byte cap)~~ **Done (C64M/2):**
  `get-memory` length is 1..65536 with `address+length <= 65536` in one RPC
  (token-keyed result pool). Remaining: pipeline / multi-deferred (roadmap
  Phase 2) and headless wake (Phase 3).

---

## 3. CPU instruction history

~~VICE has `CPUHISTORY_GET` (0x86).~~ **Done (basic):** `set-cpu-history on|off`
and `get-cpu-history [1..64]` with a 64-entry ring of instruction-start state.
Still thinner than VICE (no full disasm stream); enough for “path into this
handler?” with watchpoints.
