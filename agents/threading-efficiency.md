# Threading, IPC, and control-port efficiency

**Evaluate-only review** (no code changes implied by this document). Captures the
two-thread ownership model, what data crosses threads, whether the design is
efficient, and recommendations — especially for remote/control-port latency.

For **phased implementation**, checkpoints, and exit criteria, see
`threading-efficiency-roadmap.md` (starts with Phase 0.5 message contracts —
request tokens, RPC vs telemetry delivery — before bulk memory or pipelining).

Source of truth remains the C code. If this note and the implementation disagree,
trust the source. Related docs: `architecture.md`, `runtime-control.md`,
`control-port.md`, `remote-improve.md` (open backlog items).

---

## 1. The two-thread model (what actually exists)

There are **more than two threads**, but the ownership split is the two-thread model:

| Thread | Owns | Must not touch |
|--------|------|----------------|
| **UI / main (SDL loop)** | SDL, Nuklear/frontend, control-port **dispatch**, deferred-response completion, caches | Live `c64_t` |
| **Runtime** | Live `c64_t`, stepping, file I/O for load/save/mount, frame publish | SDL/UI |
| **Control socket** (optional) | Blocking TCP I/O | Machine, `runtime_client` single-consumer surfaces |
| **SDL audio callback** | Reads `audio_buffer` only | Runtime/machine |

**Invariant (correct and worth keeping):** no live machine pointer crosses a
thread boundary. Consumers get **copied** snapshots, frames, memory, symbols,
events.

### Data paths between UI and runtime

```text
UI/main                         Runtime
  |                               |
  |-- command_queue (SPSC-ish) -->|  runtime_command (~7 KB each)
  |                               |  drain when paused (wait) or
  |                               |  between 1024-cycle run batches
  |                               |
  |<-- event_queue ---------------|  runtime_event (~36 KB each!)
  |                               |
  |<-- frame_slot (mutex) --------|  c64_frame (~634 KB), latest-wins
  |<-- debug_memory_slot (mutex) -|  ~960 KB snapshot + generation
  |<-- symbol_slot (mutex) -------|  ~270 KB snapshot
  |                               |
  audio_buffer (SPSC float)  <----|  samples only
```

Control port adds a third hop:

```text
Client TCP
  → socket thread (parse, push request, block wait response)
  → main loop (drain requests, often kick runtime, complete deferred)
  → runtime (if needed)
  → main (format response)
  → socket thread → client
```

Key source anchors:

- `src/runtime/runtime_internal.h` — queues, frame/debug/symbol slots
- `src/runtime/runtime_client.c` — command push, slot polls
- `src/runtime/runtime_thread.c` — command drain, publish paths, run batch
- `src/control/control_server.c` — socket one-in-flight request/response
- `src/main.c` — `poll_runtime_events`, `dispatch_control_requests`, deferred
  completion, headless `SDL_Delay(1)`

---

## 2. Measured payload sizes (arm64 build snapshot)

Sizes from a compile-time probe of the headers/structs (order of magnitude;
re-measure if layouts change):

| Structure | Size | Notes |
|-----------|------|--------|
| `runtime_cpu_snapshot` | 16 B | True payload for `get-cpu` |
| `runtime_machine_snapshot` | 528 B | Compact and good |
| `runtime_command` | **~7.3 KB** | Dominated by paste / path / config unions |
| `runtime_memory_snapshot` | ~9.2 KB | 1 KB data + write-history arrays |
| `runtime_event` | **~36 KB** | Dominated by **breakpoint snapshot** (paths/type text) |
| `c64_frame` | **~634 KB** | 520×312 ARGB (`C64_FRAME_WIDTH` × `C64_FRAME_HEIGHT`) |
| `runtime_debug_memory_snapshot` | **~960 KB** | 64K×map/ram/rom + drives + optional history |
| `runtime_symbol_snapshot` | **~270 KB** | |

Queue footprints (capacity 256): command ≈ 1.8 MB; event ≈ **9 MB**.

Limits that matter for remote:

- `RUNTIME_MEMORY_SNAPSHOT_MAX = 1024` (`runtime_event.h`)
- Protocol `get-memory` length **1..1024** (`control_protocol.c`)
- Control request/response queues capacity **32** (`control_server.c`)
- `RUNTIME_RUN_BATCH_CYCLES = 1024` between command drains when running

---

## 3. Mutex / copy cost vs scheduling cost

Uncontended `pthread_mutex` + `memcpy` order of magnitude on Apple Silicon
(illustrative microbench; not a substitute for product profiling):

| Payload | ~cost |
|---------|--------|
| mutex lock/unlock alone | ~6 ns |
| 16 B (CPU regs) | ~6 ns (mutex dominates) |
| 528 B (machine snap) | ~12 ns |
| 7 KB (command slot) | ~0.1 µs |
| 36 KB (event union) | ~0.7 µs |
| 634 KB (frame) | **~11 µs** |
| 960 KB (debug memory) | **~16 µs** |

**Conclusion:** for this product, **mutex initiation cost is noise**. Copying a
few bytes vs tens of KB is still cheap next to:

- main-loop tick (headless `SDL_Delay(1)` ≈ **1 ms**; windowed present/vsync ≈ **16 ms**)
- wire RTT and TCP framing
- runtime work (full debug-memory rebuild of 64K maps dwarfs the handoff copy)

So: **do not optimize “avoid a mutex for 16-byte CPU state” as a first-order
win.** Optimize **how many RTTs**, **how large the forced union is**, and **how
often you pay the big copies**.

Frame path: publish under mutex (~11 µs) + main poll copy (~11 µs) + optional
control cache copy. At 50 fps that is tens of microseconds per frame — fine for
correctness. Latest-wins drop semantics are right for display.

---

## 4. What is already efficient / well designed

1. **Ownership boundary is sound.** Copy-out model keeps the machine
   single-threaded; good for correctness and demos/timing.

2. **Frame handoff is the right pattern for video:** single slot, latest-wins,
   drop counter, short critical section (copy under mutex, not render under
   mutex). Warp path skips paint when the slot is full.

3. **Audio is properly SPSC** and off the machine hot path from the callback’s
   perspective (`util/audio_buffer`, platform SDL device).

4. **Runtime drains *all* commands** between run batches (not one-per-batch).
   Main **also drains the whole control request queue** per tick
   (`while (control_server_poll_request)`).

5. **Some control reads are already cache-served on the main thread:**
   - `get-state` → `debug_state`
   - `get-frame` → `control_cache.frame` when present (immediate response, no deferred)

6. **`RUNTIME_RUN_BATCH_CYCLES = 1024`** is a reasonable command-latency vs
   throughput tradeoff (~1 ms emulated at 1×).

---

## 5. Where it is *not* efficient (ranked)

### A. Remote API latency is mostly architectural RTT, not memcpy

From `remote-improve.md` and the code, the pain is real, but the root causes are
stacked:

**1. Socket thread is strictly one-in-flight**

In `control_server_handle_connection`: push request, then
`message_queue_wait_pop` for the matching response before reading the next line
from the client. Even if the main loop could process a queue of independent
reads, the socket **will not read the next request until the previous response
is sent**. No pipelining on the wire.

**2. Single deferred slot**

Any second deferred command (`get-cpu`, `get-memory`, `get-vic`, waits, etc.)
gets `busy deferred-response-active`. That serializes oracle scripts even more
than the socket already does.

**3. Deferred path needs ≥1 main-loop turn (often 2)**

Typical `get-cpu`:

1. Tick N: dispatch → `runtime_client_request_*` → deferred armed (no response yet)
2. Runtime publishes event
3. Tick N+1: `poll_runtime_events` completes deferred → posts response

Headless tick ≈ 1 ms → ~1–2 ms theoretical floor. Windowed/vsync loop ≈ **16 ms
floor**, which matches measured ~16–18 ms if measurements were against a
windowed process. Headless is better than a pure “always 60 fps” story, but still
RTT-bound.

**4. `get-memory` 1..1024 cap**

64 × RTT for 64K. At ~16 ms → **~1 s**; even at 1 ms headless → **~64 ms** of
pure scheduling, plus work. VICE bulk `MEM_GET` wins here because **payload
size**, not per-byte copy cost.

**5. `request_debug_state()` every running UI frame**

Five separate runtime commands (CPU, machine, breakpoints, disk8, disk9) → five
**~36 KB** event queue items, every present. Correctness is fine; efficiency is
poor for no gain over one combined “publish debug snapshot” command.

### B. Oversized message_queue items (cache pollution, not the 16 ms)

- Every `RUNTIME_EVENT_PONG` / `FRAME_READY` / `CPU_STATE` still **`memcpy`s
  ~36 KB** because the queue item is a fat tagged union dominated by breakpoint
  entries with large path/type text fields.
- Every keyboard/joystick command **`memcpy`s ~7 KB** for a few bytes of payload.

Absolute CPU time is still sub-microsecond to ~1 µs per message, so this is
**not** why oracle scripts feel 60 fps-bound. It *is* unnecessary L2 thrash and
makes “queue full” more painful when it happens. It also makes “should we copy
16 B or 64 KB?” the wrong framing: **you already copy ~36 KB for a 16 B answer.**

### C. Debug-memory path is a bulk operation pretending to be a slot update

`runtime_publish_debug_memory` walks **all 64K** several ways (map/ram/rom/
drive8/drive9), optionally write history, under the slot mutex; then the main
thread copies **~960 KB** out. Appropriate as an explicit bulk dump; a bad
default for frequent debugger refresh. The control port correctly forces a fresh
snapshot (stale cache was a real bug), but the cost remains large by design.

### D. Frame copies for control are tripled when the port is active

Runtime → `frame_slot` → main local `c64_frame` → `control_cache.frame` → (on
`get-frame`) malloc + memcpy or ARGB→indexed8 for the TCP payload. Display needs
the first hops; control does not need a third full ARGB resident copy if the last
published slot were readable under a generation counter, or if indexed8 were
produced once and cached.

### E. UI vs control scheduling coupling

Control dispatch lives on the **same** loop as render/present. That is simple and
safe, but it **ties automation latency to UI rate** whenever a window exists.
Headless already decouples with `SDL_Delay(1)`; that is the right direction for
an automation-first mode.

---

## 6. Could calls be bundled? Yes — highest-value theme

### Bundle class 1: Runtime “debug snapshot” (UI path)

Replace per-frame:

```text
request_cpu + request_machine + request_breakpoints + disk×2
```

with one command that publishes one event carrying the small snapshots (or one
generation-tagged shared slot, like frame/debug_memory).

**Win:** 5× fewer queue ops and no 5× fat event posts. UI still updates once per
present.

### Bundle class 2: Control-port batch / pipeline (remote path)

| Layer | Idea | Effect |
|-------|------|--------|
| Wire | Allow N requests without waiting (tag by request id; socket posts responses as ready) | Removes one-in-flight serialization |
| Deferred | Multi-slot deferred table (id → pending) instead of one global deferred | Parallel `get-cpu` + `get-memory` while waiting |
| Protocol | `batch` / multi-command line or `get-memory` length up to 64K (or 8K like VICE) | Cuts 64 RTTs to 1–8 |
| Protocol | `get-regs` returning CPU+VIC+CIA in one payload | Common oracle poll in one RTT |
| Main loop | Already drains queue; keep that; optional early-out when only control is active (no vsync wait) | Lower floor latency |

**Important nuance:** main already drains multiple control requests per tick
**when they are already in the queue**. The socket never puts more than one in
the queue because it blocks for the response. **Pipelining is blocked at the
socket, not at the main loop.**

### Bundle class 3: Memory

- Raise cap to at least 8K–64K for `get-memory` (runtime already clamps to 1024;
  protocol enforces 1..1024).
- Or add a block form that writes into a **side buffer / slot** (like
  debug_memory) and returns metadata, so the event queue does not need a 64K
  union member.
- For full RAM, prefer **one** debug-memory (or a single-map subset) over 64×1K.

Copying 64K once (~1 µs class) is **vastly** cheaper than 64× (main tick +
deferred + TCP header).

### Bundle class 4: Small commands

Keyboard matrix / joystick could be coalesced (last-wins per key or one matrix
blob per tick). Today each key edge is a fat command-queue item. Fine at human
typing rates; not a problem unless synthetic spam.

---

## 7. Remote API recommendations (aligned with `remote-improve.md`)

Priority order for **oracle / automation** value:

### P0 — Throughput and latency

1. **Raise or remove the 1024-byte `get-memory` cap** (to 8K or 64K). Smallest
   change, huge win for bulk dumps.
2. **Wire pipelining + multi-deferred** (or at least multi-deferred while still
   one-at-a-time on wire — less useful alone). True win is both.
3. **Headless-aware servicing:** if headless, consider no fixed delay when a
   control request is pending (wake main from socket via cond/SDL user event).
   Today `SDL_Delay(1)` caps at ~1 kHz, which is good, but a wake would make
   deferred closer to runtime-bound instead of 1 ms-bound.
4. **Serve hot state from main-thread cache when acceptable:**
   - `get-cpu` / `get-vic` / `get-cia` could answer from last `debug_state` when
     paused (and optionally when running with a generation/frame stamp).
   - `get-frame` already uses cache — good.
   - Document freshness: `as-of frame=N cycle=C`.

### P1 — Correctness tooling (already listed in backlog)

5. **`run-to-raster`** — architectural (runtime condition), not a threading
   issue; highest functional gap for raster demos.
6. **CPU history** — companion to watchpoints; lower urgency than raster run-to
   and tick throughput.

### P2 — Structure hygiene

7. **Slim message types:** queue items as small headers + heap/side payload, or
   split event types into separate queues/slots. Especially: do not put
   breakpoint path tables in every event’s union.
8. **One `RUNTIME_COMMAND_REQUEST_DEBUG_STATE`** for the UI poll.
9. **Frame for control:** prefer indexed8 production once; avoid third full
   ARGB copy when possible.

### What *not* to do

- Do **not** let the socket thread call into the machine (docs correctly forbid
  this).
- Do **not** share live `c64_t` with the UI for “efficiency.” Frame-accurate
  bugs will follow.
- Do **not** micro-optimize mutexes on 16-byte CPU snaps; measure RTTs first.
- Do **not** assume “copy 64K is expensive so chunk to 1K” — **opposite** under
  a 1–16 ms RTT.

---

## 8. Direct answers

| Question | Answer |
|----------|--------|
| Is the two-thread model efficient? | **Yes for emulation correctness and real-time video/audio.** Display frame handoff and audio SPSC are appropriate. |
| Is mutex overhead a problem for small messages? | **No.** Uncontended mutex ~ns; scheduling/RTT is ms. |
| Is copying ~634 KB frames a problem? | **Usually no** (~11 µs/copy). Drop-on-busy is correct. Triple-copy for control is mildly wasteful, not the bottleneck. |
| Is copying ~36 KB for a PONG/CPU event a problem? | **Not for FPS; yes as design debt.** Fix when touching the queue. |
| Can UI→machine requests be bundled? | **Yes; they should.** Per-frame five-way `request_debug_state` is the main UI→runtime chatter pattern. |
| Can control requests be bundled? | **Yes; and the socket currently forbids it.** Main loop already drains; socket + single deferred are the gates. |
| Why does the API feel “60 fps”? | Dispatch is on the main loop; windowed present ≈ 16 ms; deferred needs another turn; memory is 1K-chunked; socket is 1-in-flight. Headless is already ~1 ms/tick — use it for automation, then still fix bulk memory + pipelining. |

---

## 9. Suggested target architecture (if you invest)

Keep the two-thread ownership model. Improve the **IPC and control façade**:

```text
                    ┌─────────────────────────────┐
  TCP client  ───►  │ socket I/O (pipeline OK)    │
                    │ request Q  /  response Q    │
                    └─────────────┬───────────────┘
                                  │ wake
                    ┌─────────────▼───────────────┐
                    │ main: multi-deferred table  │
                    │ cache: cpu/machine/frame    │
                    │ bulk slots: mem64 / debug   │
                    └─────────────┬───────────────┘
                                  │ slim cmds / 1 debug snapshot
                    ┌─────────────▼───────────────┐
                    │ runtime: owns c64_t         │
                    │ frame_slot, bulk mem slots  │
                    └─────────────────────────────┘
```

**Efficiency rule of thumb for this codebase:**

- **ms-scale problems** → reduce RTTs, ticks, deferred serialization, memory chunking
- **µs-scale problems** → slim unions, avoid triple frame copies, batch debug polls
- **ns-scale problems** → ignore (mutex on small snaps)

---

## 10. Bottom line

The two-thread, copy-out design is **the right product architecture**. It is not
“inefficient” in the sense that the emulator is mutex-bound or drowning in
16-byte copies.

What **is** inefficient is the **control/oracle path layered on top of a
UI-paced, one-deferred, one-in-flight, 1K-memory protocol**, plus **fat union
copies** and **unbundled debug refresh**. Those match what `remote-improve.md`
already smells; the code confirms the socket and deferred slots are the real
chokepoints, while main-loop request draining is already better than a naive
“one request per tick” reading of the backlog.

If only three things are done later:

1. **Bulk `get-memory` (8K–64K)**
2. **Pipeline control requests + multi-deferred**
3. **Single runtime debug-snapshot publish for the UI poll**

…almost all practical pain is addressed without abandoning the two-thread model.

---

## 11. Implementation touchpoints (for a future agent)

| Area | Files |
|------|--------|
| Ownership / queues | `src/runtime/runtime_internal.h`, `runtime.c`, `runtime_client.c`, `runtime_thread.c` |
| Event/command shapes | `src/runtime/runtime_event.h`, `runtime_command.h` |
| Message queue | `src/util/message_queue.{c,h}` |
| Control wire + one-in-flight | `src/control/control_server.c`, `control_protocol.c` |
| Main dispatch / deferred / cache | `src/main.c` (`dispatch_control_*`, `poll_runtime_events`, `request_debug_state`, headless loop) |
| Frame geometry | `src/machine/c64_frame.h` |
| Backlog list | `agents/remote-improve.md` |
