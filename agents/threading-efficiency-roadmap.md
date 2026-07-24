# Threading / control-port efficiency — implementation roadmap

**Companion to** `threading-efficiency.md` (why) and `remote-improve.md` (oracle
backlog symptoms). This document is the **how and when**: phased plan, ordered
dependencies, checks, and exit criteria.

Not a handoff for a single component. Do not implement bulk slots or pipelining
before **Phase 0.5 (message contracts)** is designed and landed enough to
support later phases. Source of truth remains the C code and tests.

---

## 0. Goals, non-goals, and invariants

### Goals

1. Cut remote/oracle **RTT-bound** cost (especially bulk memory and multi-query
   polls) without breaking the two-thread ownership model.
2. Establish a clear **data-identity and delivery model** so pipelined and
   concurrent work cannot mis-correlate, drop, or steal results.
3. Reduce UI→runtime **chatty polling**, split by update cadence (not one fat
   mega-event every frame).
4. Keep headless automation **faster than UI-paced** work and document expected
   latency floors.
5. Leave the door open for functional control features (`run-to-raster`, CPU
   history) once the transport is not the bottleneck.

### Non-goals (for this roadmap)

- Cycle-perfect new hardware behavior.
- Multi-client control server, non-localhost bind, or runtime fan-out.
- Sharing live `c64_t` with the UI or socket thread “for speed.”
- Micro-optimizing uncontended mutexes on 16-byte CPU snapshots.
- Enlarging fat `runtime_event` unions to carry 64K memory + write-history
  (forbidden as a Phase 1 path; see §3).

### Invariants (must hold after every phase)

| Invariant | Check |
|-----------|--------|
| Live machine stays on the runtime thread | No `c64_*` call from frontend, control socket thread, or SDL audio callback |
| Consumers see copies / owned payloads only | No live machine pointers; heap ownership is explicit |
| Full ctest baseline green when verification is authorized | `ctest --test-dir build --output-on-failure` |
| Wire protocol remains backward compatible *or* is explicitly versioned | Old clients still work or fail clearly; `control-port.md` updated same change |
| One client at a time (unless deliberately changed later) | Documented in `control-port.md` |
| Telemetry vs RPC delivery rules (§1) | Tests for lossy overwrite vs reliable completion |

### Relationship to other docs

| Doc | Role |
|-----|------|
| `threading-efficiency.md` | Problem analysis and recommendations |
| `remote-improve.md` | Short symptom list from oracle use |
| `control-port.md` | **Must stay accurate** as wire behavior changes |
| `runtime-control.md` | Runtime ownership; update if command/event shapes change |
| `architecture.md` | Thread ownership; only change if model changes |
| `testing.md` | How to run gates |

Update handoffs in the **same change** as behavior they describe.

---

## 1. Design rules (data identity and delivery)

These rules are the **missing piece** relative to an RTT-only plan. They must be
decided and encoded (docs + types + tests) in Phase 0.5 before multi-outstanding
RPC or bulk result channels ship.

### 1.1 Correlation: internal `request_token`

**Problem today:** `runtime_command` / `runtime_event` have type + payload only
(no correlation id). Deferred completion matches event type and sometimes
arguments (`main.c`). Two identical `get-cpu` requests are indistinguishable
after entering the runtime. The UI also issues CPU/machine requests every frame
(`request_debug_state`); an unsolicited UI response can satisfy a control
deferred wait.

**Rule:**

- Every **solicited** runtime request carries an opaque internal
  `request_token` (monotonic `uint64_t` is enough; not the wire request id).
- Runtime results and errors for that request echo the same token.
- Unsolicited notifications (frame ready, pause, breakpoint hit, free-running
  telemetry) use **token 0**.
- Main-thread deferred table keys: `(connection_epoch, wire_request_id)` for
  the client-facing side, and `request_token` for matching runtime completions.
- Wire request ids remain a **client/session** concern. On each accepted
  connection, bump a `connection_epoch` (or session generation). Stale
  responses from a previous connection must not be delivered to the next.

UI-originated requests get their own tokens (or token 0 if purely lossy
telemetry). Control-originated deferred work always uses non-zero tokens.
**Never** complete a control deferred entry from a token-0 or wrong-token
event.

### 1.2 Telemetry slots vs RPC results

| Channel class | Semantics | Examples |
|---------------|-----------|----------|
| **Telemetry / snapshot slots** | Latest-wins; intermediate values may be dropped; generation may detect replacement | `frame_slot`, live debug-memory *display* slot, free-run machine telemetry |
| **RPC results** | Exactly-once delivery to the waiter that owns the token, or explicit cancel/error | `get-memory`, `get-cpu` (solicited), assemble complete, save/load state complete |

**Rule:** A single latest-wins slot is **not** a reliable multi-outstanding RPC
channel. Generation numbers detect overwrite; they do **not** preserve lost
results.

Bulk RPC data needs one of:

1. A **bounded result ring/pool keyed by `request_token`**, or
2. **Individually owned payloads** on a reliable completion queue (event
   carries token + handle / inline small data), or
3. An **explicit restriction**: at most one bulk request outstanding
   (documented `busy` if a second arrives).

Choose (1) or (2) for Phase 1 if Phase 2 will allow concurrent bulk reads;
choose (3) only as a temporary bridge with a hard limit and tests.

### 1.3 Lossy vs reliable delivery

`message_queue_push` returns false when full; many publishers ignore failure.
That is acceptable for **redundant** notifications; it is not acceptable for
pipelined RPC completions.

| Class | On queue full | Examples |
|-------|----------------|----------|
| **Lossy notification** | Drop OK; optional drop counter | `FRAME_READY`, free-run telemetry |
| **Reliable completion** | Must not silently vanish: block with backpressure, fail the command with `busy`/`error` to the deferred owner, or use a completion path that cannot drop without cancel | Solicited memory/cpu/vic responses, assemble/save/load complete |
| **Reliable error/cancel** | Same as reliable completion | Runtime error for a token-bearing command; connection teardown cancels outstanding tokens |

**Rule:** Queue saturation for reliable traffic yields **deterministic
backpressure or a `busy`/error response**, not a later deferred timeout with no
explanation.

### 1.4 Memory length and address space (Phase 1 constraints)

Today length and address are `uint16_t` in protocol args, runtime command,
memory snapshot, and deferred state. **`65536` cannot be represented in
`uint16_t` length.**

**Decisions required in Phase 0.5 / Phase 1 (recommended defaults below):**

| Question | Recommended decision |
|----------|----------------------|
| Length type | `uint32_t` (or `size_t` with documented max) **throughout** protocol args, command, result metadata, deferred match fields |
| Max length | `65536` for a single `get-memory` covering the full 16-bit space |
| `address + length > 65536` | **Reject** with `bad-args` (no wrap, no silent truncate) |
| Full dump form | Allow any `address` in `0..65535` with `length` such that `address + length <= 65536`; full dump is typically `address=0 length=65536` |
| Write-history in bulk path | Either omit write-history from the 64K bulk response by default, or stream it as a separate optional payload — **do not** embed `uint64_t write_history[65536]` in `runtime_event` |

**Forbidden:** Phase 1 “Option A” enlarge `runtime_memory_snapshot` inside
`runtime_event` to 64K. Bytes + 64-bit write history ≈ **576 KiB per event**,
and a 256-entry event queue ≈ **~144 MiB** of queue storage alone.

### 1.5 Socket multiplexing (Phase 2b prerequisite)

Platform sockets are **blocking-only** today. Removing
`wait_pop(response)` from the current handle loop without a new I/O model
risks: block forever on the next `read` while completed responses sit unsent
after the client has finished sending a batch and is waiting.

Phase 2b **must** pick an explicit mechanism (document choice in the PR):

- nonblocking sockets + poll/select on read readiness and response-queue
  non-emptiness, or
- reader/writer split (one thread or task blocks on read, another on response
  queue + write), or
- another wakeable connection loop that can interleave “accept next request”
  and “flush ready responses.”

Also define **connection-epoch** handling:

- On disconnect: cancel all outstanding deferred entries for that epoch;
  free RPC payloads; drain or tag-drop in-flight responses so they cannot be
  delivered to the next client.
- Queues that outlive connections must not leak pointer payloads across
  sessions.
- Duplicate outstanding wire request ids within one epoch: reject with
  `bad-id` / `busy` (pick one; document).
- `quit-client` ordering: complete or cancel outstanding work, then close.

### 1.6 Concurrent wait semantics

Sticky latches are currently **destructive** (one waiter consumes and clears).
`wait_after_seq` is assigned but not consulted for matching in all paths.

**Rule (pick one and test):**

- **Restrict:** at most one outstanding waiter per wait-event name (or per
  wait class), reject a second with `busy`; **or**
- **Watermark:** non-destructive sequence numbers; each waiter stores
  `start_seq` and completes when `event_seq > start_seq` without clearing the
  latch for others.

Do not ship multi-deferred waits without this decision.

### 1.7 Heap ownership (Phase 5 prerequisite foreshadow)

If commands/events gain heap payloads, every **discard** path must release them:

- `message_queue_destroy` does not run item destructors today.
- Step-over/out drains and **drops** most commands (`runtime_flow_abort_requested`).
- Queue-full push failure must free the payload that was not enqueued.

Prefer a move-owned envelope with a release callback, or a bounded payload pool
with checkout/checkin, plus shutdown cancellation cleanup.

---

## 2. Baseline metrics (B0)

Record these **once** on a quiet machine. Prefer **headless** for transport
work; note GUI separately if measured.

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure -R control_protocol
```

| Metric ID | Procedure | What good looks like later |
|-----------|-----------|----------------------------|
| M1 | 100× `get-cpu` RTT (paused) | Mean + p99; Phases 2–3 / 6 should drop floor |
| M2 | 100× `get-frame` RTT (paused, cache warm) | Stay low (cache-served) |
| M3 | Wall time for full 64K via today’s 64×1024 `get-memory` | Post Phase 1: one call, ≪ 1 s headless |
| M4 | `run` → `wait-frame 1` → `pause` frame overshoot | Document; re-check after 2–3 |
| M5 | Headless idle tick | ~1 ms today |
| M6 | GUI present tick | ~16 ms vsync class |

**Checkpoint B0 — baseline locked**

- [x] M1–M3 (+ batch) written under §16 (main vs tip, 2026-07-24).
- [x] Headless used for M1–M3.
- [x] `test_control_protocol` green (and other targeted suites through the series).

B0 can run in parallel with Phase 0.5 design docs; do not start Phase 1
implementation without B0 if the goal is measurable latency improvement.

---

## 3. Phase overview (revised)

```text
Phase 0     Baseline metrics (B0)
   │
   ▼
Phase 0.5   Message contracts ────────────────────── tokens, lossy vs reliable,
   │         (BLOCKING for 1–2)                       epochs, ownership, waits
   │
   ▼
Phase 1     Bulk get-memory ──────────────────────── uint32 length, per-request
   │         (RPC result storage — not fat events)    result pool; no Option A
   │
   ▼
Phase 2a    Multi-deferred (token-matched)
   │
   ▼
Phase 2b    Multiplexed socket pipeline ──────────── nonblocking/poll or R/W split
   │                                                 + connection epoch cancel
   │
   ▼
Phase 3     Headless wake / lower tick floor
   │
   ├──────────────────────────────┐
   ▼                              ▼
Phase 4     Cadence-split UI       Phase 5  Slim IPC + heap ownership
   │         telemetry                           │
   ▼                              │
Phase 6     Cache-served hot reads ◄── depends on Phase 4 stamps (+ 0.5 tokens)
   │
   ▼
Phase 7     run-to-raster (functional)
   │
   ▼
Phase 8     CPU history (functional)
```

| Phase | Theme | Depends on | Primary risk |
|-------|--------|------------|--------------|
| 0.5 | Message contracts | B0 optional | Under-spec → silent mis-correlation later |
| 1 | Bulk memory RPC | **0.5** | uint16 limits; slot overwrite; write-history bloat |
| 2a | Multi-deferred | **0.5**, 1 recommended | Token matching, UI vs control interleave |
| 2b | Socket pipeline | **2a**, **0.5** §1.5 | Blocking I/O deadlock; epoch leaks |
| 3 | Headless wake | 2b optional | CPU spin; missed wake |
| 4 | Cadence-split telemetry | 0.5 tokens (for solicited paths) | Debugger staleness |
| 5 | Slim IPC | 0.5 ownership; 1 without fat events | Leaks on drain/destroy |
| 6 | Cache-served reads | **4** (stamps) + **0.5** | Stale cache without barrier |
| 7 | run-to-raster | 2 helpful | Timing vs VICE |
| 8 | CPU history | 7 optional | Free-run cost |

**Do not implement Phase 2 before Phase 0.5.**  
**Do not implement Phase 1 as “bigger `runtime_event`.”**

---

## 4. Phase 0.5 — Message contracts

### Intent

Define and land the **identity, delivery, ownership, and wait** model before any
bulk RPC channel or pipelining. This is design + minimal plumbing, not the full
performance win.

### Deliverables

1. **Spec section** in `runtime-control.md` and/or `control-port.md`:
   - `request_token` (0 = unsolicited)
   - connection/session epoch
   - wire id rules (duplicate outstanding ids)
   - lossy vs reliable classification table (§1.3)
   - telemetry slot vs RPC result rule (§1.2)
   - wait concurrency rule (§1.6)
   - cancel/disconnect/drain rules (§1.5)

2. **Types / plumbing (minimal):**
   - Add `request_token` to `runtime_command` and to result/error events that
     complete solicited work.
   - Token allocator on the producer side (main/UI/control dispatch).
   - Deferred completion matches **token first**, not “any CPU_STATE.”
   - Unsolicited publishers keep token 0.

3. **Classification audit:** annotate publish sites (frame, memory, cpu,
   breakpoints, errors) as lossy or reliable; fix at least the paths that
   Phase 1–2 will use so reliable completions cannot be dropped silently
   (or document temporary single-outstanding bulk if still lossy).

4. **Ownership hooks (stubs OK if unused):** define how a queue item with a
   payload is released on drop/destroy (even if Phase 5 fills it in).

### Touch points

- `src/runtime/runtime_command.h`, `runtime_event.h`
- `src/runtime/runtime_thread.c` — publish helpers
- `src/runtime/runtime_client.c`
- `src/main.c` — deferred matching, `request_debug_state` tokens
- Docs: `runtime-control.md`, `control-port.md`, this file

### Implementation checklist

- [x] Written contract (§1) accepted in review / documented in tree.
      (`runtime-control.md` § Message contracts; `control-port.md` identity
      section; locked defaults §18). Code enforcement is PR-0.5b.
- [x] Token on solicited command → completion path for at least one command
      used by control (`get-cpu`) end-to-end (`request_token` on command/event;
      `runtime_client_request_cpu_state_token`; main deferred stores token).
- [x] Control deferred **cannot** complete from UI token-0 / other-token
      CPU_STATE (`control_deferred_token_matches` + tests:
      `test_control_protocol`, `test_runtime_request_token`).
- [x] Connection epoch field exists (even if pipeline not yet enabled):
      `control_server_connection_epoch` / `has_client`; deferred stores epoch;
      session cancel on disconnect/epoch change.
- [x] Wait rule chosen and documented (§1.6): single outstanding wait → `busy`.
      (Enforcement of second-wait `busy` remains single-deferred today; multi-
      deferred wait policy lands with PR-2a.)

### Checks and checkpoints

**Checkpoint 0.5A — unit / deterministic**

- [x] Two back-to-back solicited CPU requests with distinct tokens: each
      response echoes its token (`runtime_request_token` test).
- [x] UI-style unsolicited (token 0) does not match a non-zero deferred token
      (`control_deferred_token_matches` unit test).
- [x] Documented behavior for queue-full on a reliable completion path
      (error/busy, not silent) — contract in `runtime-control.md`;
      implementation of push-failure → busy for all reliable paths is phased
      with bulk/pipeline (Phase 1–2).

**Checkpoint 0.5B — exit Phase 0.5**

- [x] `runtime-control.md` + relevant `control-port.md` sections landed.
- [x] Targeted suites green: `control_protocol`, `runtime_*`.
- [ ] Full ctest if verification authorized.

### Exit criteria

Identity and delivery rules are **code-enforced** for solicited control paths,
not only prose. Phase 1 and 2 implementers have a clear place to put bulk
results and match them.

---

## 5. Phase 1 — Bulk `get-memory` (RPC, not fat events)

### Intent

Stop paying **64 RTTs** for 64K. Copy cost is microseconds; RTT is
milliseconds. Representation must respect §1.2 and §1.4.

### Scope

- Length becomes **`uint32_t`** (max **65536**); address stays 16-bit space.
- Reject `length == 0`, `length > 65536`, and `address + length > 65536`
  (use 32-bit arithmetic).
- **Per-request RPC result storage** (ring/pool keyed by `request_token`), or
  temporary **one bulk outstanding** with documented `busy` — **not** a single
  latest-wins slot if multi-outstanding is planned for Phase 2.
- Completion event is small: token + address + length + mode + status; payload
  lives in the result pool until the owner pops it.
- Optional write-history: separate request or omitted on bulk path (do not
  ship 64K×`uint64_t` in the event union).
- Protocol, parser tests, client examples, `control-port.md`.

### Explicitly removed

- **Option A:** enlarge in-event `runtime_memory_snapshot` to 64K — **rejected.**

### Touch points

- `src/control/control_protocol.{c,h}` — `length` type and validation
- `src/runtime/runtime_command.h`, `runtime_event.h`, result pool (new)
- `src/runtime/runtime_thread.c` — publish path
- `src/main.c` — deferred match by token; format response from pool
- `tests/control/test_control_protocol.c` — boundaries
- New tests for `$FFFF` + length 1, length 2 (reject), length 65536 at 0, etc.

### Implementation checklist

- [x] All length fields that can be 65536 are wider than `uint16_t`.
- [x] Boundary tests: `0`, `1`, `1024` (compat), `65536`, `65537`,
      `address=0xFFFF length=1`, `address=0xFFFF length=2` (reject),
      `address=0xFF00 length=0x100` (ok), `address=0xFF00 length=0x101` (reject).
- [x] RPC result pool keyed by token; claim transfers ownership once (second
      claim fails); pool full → `busy` status on completion event.
- [x] Reliable completion path: queue-full publish frees pool slot and emits
      busy status when possible.
- [x] Docs + protocol C64M/2; Python client length note can lag until tools
      touch (max still works for ≤1024 and for 65536 if client sends decimal).

### Checks and checkpoints

**Checkpoint 1A — unit**

```sh
ctest --test-dir build --output-on-failure -R 'control_protocol|runtime_'
```

- [x] All boundary cases above (`test_control_protocol` + client reject paths).
- [x] Token correlation: pool keyed by token; second claim fails; pool full →
      `busy` (`test_runtime_memory_rpc`).

**Checkpoint 1B — headless smoke**

- [ ] M3 after ≪ M3 before (one full-space dump). *(not measured formally)*
- [x] Payload path exercised in-process (`test_runtime_memory_rpc` full 64K claim).

**Checkpoint 1C — exit Phase 1**

- [x] `control-port.md` documents max length, reject rules, bulk concurrency.
- [x] `remote-improve.md` item 2 partially closed (bulk portion).
- [ ] Full ctest if verification authorized.

### Exit criteria

Full 64K dump is **one command**, headless time dominated by copy/format/TCP,
with RPC semantics that will not break under Phase 2 multi-outstanding (or an
explicit single-bulk limit that Phase 2 will lift using the same pool).

---

## 6. Phase 2 — Multi-deferred + multiplexed socket pipeline

### Intent

Remove **one deferred** and **one in-flight socket request** as hard
serialization points — only after tokens and delivery classes exist.

### 2a — Multi-deferred (token-matched)

- Deferred table (e.g. 8–32 entries) keyed by `(connection_epoch, wire_id)` with
  stored `request_token`.
- Complete only on matching token (or explicit cancel).
- Table full → `busy`, not hang; per-entry timeout retained.
- Wait commands: enforce §1.6 (single waiter or watermark).
- UI telemetry remains token 0 and never steals control completions.

### 2b — Multiplexed socket loop

- Implement §1.5 (poll/nonblocking or reader/writer). Interleave:
  - read next request when budget allows,
  - write completed responses as soon as available.
- High-water mark on outstanding requests (= deferred capacity / request queue).
- Connection epoch bump on accept; cancel + drain on disconnect.
- Sequential client remains valid; optional pipelining client for tests.

### Touch points

- `src/main.c` — deferred table, token complete paths, waits
- `src/control/control_server.c` — connection loop redesign
- `src/platform/platform_socket.*` — if nonblocking/poll APIs needed
- `tools/c64_control_client.py` — pipeline mode for tests
- Docs: concurrency, ordering, busy, disconnect

### Implementation checklist

- [x] No socket-thread machine or single-consumer runtime poll.
- [x] Responses matched by wire id (socket outstanding ids); runtime matched by
      token for solicited cpu/memory paths.
- [x] Document completion-order vs request-order (prefer completion order + ids).
- [x] Duplicate wire id while outstanding → `bad-id`.
- [x] Mid-pipeline disconnect: epoch cancel + drain leftover responses on
      connection end *(formal stress harness not added)*.

### Checks and checkpoints

**Checkpoint 2A — multi-deferred (no pipeline yet)**

- [x] Two solicited CPU requests with distinct tokens complete correctly
      (`test_runtime_request_token`). Multi-deferred table allows concurrent
      `get-cpu` / `get-memory` slots.
- [x] UI/token-0 cannot complete a non-zero control deferred
      (`control_deferred_token_matches` + token tests).
- [x] Second wait → `busy wait already active` (exclusive wait policy).

**Checkpoint 2B — pipeline + I/O multiplex**

- [x] Client can pipeline: `tools/c64_control_client.py` `pipeline()`.
- [x] Multiplexed nonblocking loop flushes responses while accepting more reads
      (up to high-water 16).
- [ ] N=16 wall time ≪ N × serial RTT under headless. *(not measured)*
- [ ] Disconnect/reconnect stress harness. *(basic drain/epoch only)*
- [x] Queue / table / pool saturation → `busy` (not hang-only).

**Checkpoint 2C — exit Phase 2**

- [x] `control-port.md` concurrency model complete.
- [x] Deterministic identity tests for tokens, memory boundaries, deferred gate
      *(full §13 matrix not every row)*.
- [ ] Re-measure M1 batch vs serial.
- [ ] Full ctest if verification authorized.
- [x] `remote-improve.md` item 2 pipeline portion closed or updated.

### Exit criteria

Multiple outstanding control requests work with correct correlation; socket can
send completions while accepting further requests; disconnect is clean.

### Risks / rollback

- Land **2a** without 2b if I/O multiplex slips — still requires tokens.
- Rollback must not reintroduce type-only deferred matching.

---

## 7. Phase 3 — Headless wake / lower tick floor

### Intent

Stop quantizing deferred completion on a fixed `SDL_Delay(1)` when work is
pending. Wake main when a control request arrives or a reliable completion is
posted.

### Scope

- Wake primitive: SDL user event, self-pipe, condvar, etc.
- Headless: sleep with timeout **or** block until wake; drain on wake.
- GUI: do not break vsync; optional opportunistic drain between presents.
- Idle CPU must not peg at 100%.

### Checks and checkpoints

**Checkpoint 3A**

- [x] Idle headless uses wait (not busy-spin); deferred path uses 0 ms wait.
- [ ] M1 mean drops toward runtime+copy cost under load. *(not measured)*
- [x] Control request push wakes main via SDL user event (pipeline can progress
      without fixed 1 ms quantize on arrival).

**Checkpoint 3B — exit Phase 3**

- [x] Documented headless latency floor in `control-port.md`.
- [x] GUI path unchanged (wake hook is headless-only).

### Exit criteria

Headless control latency is wake-driven with bounded idle CPU.

---

## 8. Phase 4 — Cadence-split UI telemetry (not one mega-bundle)

### Intent

Reduce per-frame chatter **without** stuffing the full breakpoint table and
disk metadata into every refresh.

**Problem with “bundle all five”:** `runtime_machine_snapshot` already includes
CPU registers, VIC, both CIAs, SID, and both drive **hardware** snapshots. A
separate per-frame CPU request is redundant. Breakpoint **definitions** and
disk **metadata** have different lifetimes from CPU telemetry. Bundling the
full breakpoint table every frame keeps the ~36K dominant payload.

### Scope (by cadence)

| Stream | When published | Contents |
|--------|----------------|----------|
| **Machine telemetry** | Once per UI refresh (solicited or free-run publish) | Small coherently stamped snapshot: cycle/frame, regs, VIC/CIA/SID/drive HW as already in machine snapshot (or a slimmed view) |
| **Breakpoint definitions** | On mutation only (create/update/clear/enable/load) | Full table |
| **Breakpoint hit counters** | Optional high-frequency compact array if UI needs live counts | ids + counters only |
| **Disk metadata** | On mount/unmount/writable/dirty transitions | Per-device status |

- Replace `request_debug_state()` five-command fan-out with: one telemetry
  request (or consume free-run publishes) + ensure mutation paths already
  publish breakpoints/disk.
- Stamps: each telemetry snapshot carries a **runtime sequence** and
  **machine cycle** (and frame if available) for Phase 6.

### Checks and checkpoints

**Checkpoint 4A**

- [x] Debugger regs/VIC still update while running (machine telemetry only).
- [x] Editing a breakpoint still refreshes the full list (mutation publish).
- [x] Disk LED via drive HW in machine telemetry; mount labels via status
      publish on mount + full refresh — not five-way every frame.
- [x] Free-run path drops from 5 queue ops/frame to 1 machine-state event
      (no ~36K breakpoint table per present).

**Checkpoint 4B — exit Phase 4**

- [x] `runtime-control.md` describes cadence model.
- [x] Coherent stamp fields: `runtime_seq` + cycle/frame on machine snapshot.

### Exit criteria

UI refresh uses one small stamped telemetry path; large tables move on mutation
(and optional compact counters), not every frame.

---

## 9. Phase 5 — Slim IPC + heap ownership

### Intent

Stop `memcpy` of ~36 KB events / ~7 KB commands for tiny payloads. Hygiene and
cache pressure — not the main oracle fix (Phases 0.5–2).

### Scope

1. Small tagged queue items; large data via RPC pools (Phase 1) or mutation
   publishes (Phase 4).
2. Paths/paste: heap or payload pool with **release on every discard path**
   (queue full, step-over drain, `message_queue_destroy`, shutdown).
3. Optional capacity shrink after sizes drop.

### Rules

- No live machine pointers.
- Destructor/release callback or pool checkin on all drop paths (§1.7).
- Never reintroduce 64K into the event union.

### Checks and checkpoints

**Checkpoint 5A**

- [x] Breakpoint table no longer rides in every `runtime_event` (side
      `breakpoint_slot` + slim `breakpoints_ready` meta). Dominant ~36K union
      member removed from the hot event path.
- [ ] ASan / flood + step-over drain + shutdown with pending paste/path
      commands — no leaks. *(PR-5b heap envelope not landed; paths/paste still
      inline in `runtime_command`.)*
- [ ] Queue destroy with heap items present releases payloads. *(N/A until heap
      envelopes; RPC memory pool frees on claim/destroy.)*

**Checkpoint 5B — exit Phase 5**

- [ ] Full ctest if verification authorized.
- [ ] Update size table in `threading-efficiency.md` or mark dated.

### Exit criteria

**Partial:** event hot path is much slimmer (breakpoints off union; bulk memory
not in events). Full “leak-free heap ownership for paste/path commands” remains
backlog (PR-5b).

---

## 10. Phase 6 — Cache-served hot control reads

### Intent

Serve `get-cpu` / `get-vic` / `get-cia` from main-thread cache when a
**coherence barrier** says it is safe — not merely “emulator looks paused.”

### Depends on

- **Phase 4** stamped telemetry (sequence + machine cycle).
- **Phase 0.5** tokens for an explicit `fresh` / forced runtime path.

### Cache contract

1. Cache entry carries: `runtime_seq`, `machine_cycle`, `frame_number` (if any),
   payload, and `valid` flag.
2. **Invalidate** (or mark stale) when a **mutating** control/UI command is
   accepted (run, step, poke, load, reset, etc.), not only when a response
   arrives.
3. **Paused means fresh** only after a runtime completion/barrier proves
   preceding mutations were processed (e.g. last mutating command’s token
   completed, or a dedicated barrier token).
4. While running: either serve last snapshot with explicit `as-of` metadata, or
   require `fresh` (tokenized runtime read).
5. Always keep **`fresh` / forced** path for oracle work (token-matched RPC).

### Checks and checkpoints

**Checkpoint 6A**

- [x] Design: mutating commands mark hot cache stale; paused machine snapshot
      re-seals. Cache hit only when `has_hot && !hot_stale && hot_paused`.
- [x] Mutating command then immediate cache read does not claim fresh
      (`hot_stale` set on accept).
- [x] When not cache-fresh, `get-cpu` still uses tokenized runtime path
      (forced/fresh by default for oracle).
- [x] Documented in `control-port.md`.
- [ ] Dedicated automated test for “step then cache PC matches” (logic is in
      main; not a separate ctest case).

**Checkpoint 6B — exit Phase 6**

- [ ] M1 for paused cache-hit path competitive with `get-state`. *(not measured)*
- [x] Oracle: non-fresh falls through to runtime; no dual “as-of” wire flag
      (implicit: cache hit only when barrier-safe).

### Exit criteria

Hot reads can skip RTT when the barrier says so; oracle still hits runtime when
stale/running.

---

## 11. Phase 7 — `run-to-raster` (functional)

### Intent

`remote-improve.md` item 1. Prefer after transport (0.5–2) is trustworthy.

### Scope

- Runtime: run until raster line (+ optional cycle-in-line), then pause and
  publish as other step completes.
- Control grammar + docs.
- Tests: stop on line N; VICE oracle on a known PRG (`vice-oracle.md`).

### Checks

- [x] Runtime + control `run-to-raster <line> [cycle]`; breakpoints/BRK win
      (documented in `control-port.md`). Line range is standard-dependent
      (PAL/NTSC noted in docs).
- [x] Docs; `remote-improve.md` item 1 closed for basic run-to-raster.
- [x] Unit test: `test_runtime_run_to_raster`.
- [ ] Manual VICE oracle on a known PRG (`vice-oracle.md`) — not run in this
      series.

---

## 12. Phase 8 — CPU instruction history (functional)

### Intent

`remote-improve.md` item 3. Lower priority than 0.5–2 and 7.

### Scope

- Optional ring buffer on runtime; control `get-cpu-history`.
- Disabled by default; enable via command.

### Checks

- [x] Order/length after N steps (`test_runtime_cpu_history`); off by default;
      free-run cost noted in `control-port.md` (small, nonzero when on).
- [x] `remote-improve.md` item 3 basic history closed (thinner than VICE).

---

## 13. Deterministic test matrix (identity and delivery)

Script-level smoke is **not enough** for ownership and identity bugs. Prefer
in-process tests (ctest) where possible; headless integration where needed.

| Test theme | Required by | Assertion | Status |
|------------|-------------|-----------|--------|
| Identical pipelined reads | 0.5, 2 | Two `get-cpu` / two `get-memory` same args → both correct, no cross-wire | **Partial** — tokens + pool + `pipeline()` client; no full headless N=16 timing test |
| UI/control interleave | 0.5, 2, 4 | UI telemetry cannot complete control deferred | **Done** — token gate + unit tests |
| Disconnect/reconnect | 2b | Epoch cancel; no payload leak; no deadlock | **Partial** — epoch + drain; no stress harness |
| Queue saturation | 0.5, 2 | Reliable path → busy/error; no silent loss | **Partial** — busy on table/pool/queue full |
| Duplicate wire ids | 2 | Defined reject while outstanding | **Done** — `bad-id` |
| Multiple waits | 0.5, 2 | Single-waiter busy **or** watermark both complete | **Done** — single waiter → busy |
| Slot/pool overwrite | 1, 2 | Second bulk RPC does not erase first waiter’s result without cancel/busy | **Done** — pool claim once / busy when full |
| Memory boundaries | 1 | `$FFFF+1`, `length=65536`, `65537`, `0` | **Done** — protocol unit tests |
| Token mismatch ignore | 0.5 | Wrong-token event leaves deferred active | **Done** — gate + tests |
| Cache barrier | 6 | Mutate then read without barrier ≠ fresh | **Partial** — implemented in main; no dedicated ctest |
| Heap drop paths | 5 | Drain/destroy/full push free payloads | **Open** — PR-5b not landed |

### Standard gates

| Gate | Command / action | Phases |
|------|------------------|--------|
| Protocol unit | `ctest -R control_protocol` | 0.5, 1, 2, 6–8 |
| Runtime unit | `ctest -R 'runtime_'` | 0.5, 1, 4, 5, 7 |
| Full baseline | `ctest --test-dir build --output-on-failure` | end of phase if authorized |
| Headless smoke | `c64m --headless --control-port …` | 1–3, 6–8 |
| GUI smoke | Debugger open, step, run | 4, 5, 7 |
| Oracle (manual) | `vice-oracle.md` | 7, optional 8 |

### Regression watchlist

- Frame aliasing: `wait-frame 1` + `get-frame` while free-running (prefer
  `step-frame` for consecutive frames until fixed).
- Pause-after-wait one frame late (re-check after 2–3).
- `get-debug-memory` must not silently serve a wrong generation as “fresh RPC.”
- Pipeline flood → deterministic busy, not hang.

---

## 14. Definition of done (whole roadmap)

**Transport / oracle ready** when:

1. [x] Phase **0.5**, **1**, and **2** are done (contracts + bulk RPC + multi-
   outstanding with multiplexed socket).
2. [x] Phase **3** landed (wake-driven headless); formal M1 numbers still open.
3. [x] `control-port.md` / `runtime-control.md` match behavior (C64M/2).
4. [x] Core identity/delivery tests green (`control_protocol`, `runtime_*`
   including token, memory RPC, run-to-raster, cpu history). Not every §13 row
   has a dedicated harness.
5. [x] `remote-improve.md` items 1–3 updated (basic run-to-raster, bulk+pipeline,
   basic CPU history).
6. [x] Full baseline `ctest --test-dir build --output-on-failure` green
   (56/56, 2026-07-24, pre-merge on tip).

Phases **4–6** also landed (cadence telemetry, slim breakpoint events, cache
barrier). Phases **7–8** landed as product features.

**Honest residual backlog** (see also unchecked items above): formal M1–M6
baselines; full ctest; PR-5b heap ownership for paste/path; disconnect stress;
explicit wire `fresh`/as-of flag; VICE oracle manual for run-to-raster;
optional compact breakpoint hit counters while free-running.

---

## 15. Suggested PR slicing

| PR | Content |
|----|---------|
| PR-0.5a | Spec docs: tokens, lossy/reliable, slots vs RPC, waits, epochs |
| PR-0.5b | Token plumbing + deferred match by token + interleave tests |
| PR-1 | Bulk memory: uint32 length, result pool, boundary tests, docs |
| PR-2a | Multi-deferred table + wait policy + UI/control interleave tests |
| PR-2b | Multiplexed socket I/O + pipeline + disconnect/epoch tests |
| PR-3 | Headless wake |
| PR-4 | Cadence-split telemetry + stamps |
| PR-5a | Slim events / breakpoint off hot path |
| PR-5b | Heap envelope + drain/destroy ownership |
| PR-6 | Cache contract + fresh path |
| PR-7 | run-to-raster |
| PR-8 | CPU history |

Do not combine 0.5 + 2b + 7 in one PR. Do not ship 2b without 0.5 tokens.

---

## 16. Measured baselines (fill in during B0 / phase exits)

Probe: `python3 tools/measure_control_latency.py --bin ./build/c64m --port PORT --label …`
Headless `--pal`, quiet machine, 2026-07-24.

| Date | Metric | Mode | Value | Notes |
|------|--------|------|-------|-------|
| 2026-07-24 | M1 get-cpu mean | headless | **2.474 ms** | `main` pre-stack (C64M/1) |
| 2026-07-24 | M1 get-cpu p99 | headless | **2.655 ms** | `main` |
| 2026-07-24 | M2 get-frame mean | headless | **2.200 ms** | `main`, indexed8 warm |
| 2026-07-24 | M3 64K memory | headless | **165.1 ms** | `main` 64×1024 |
| 2026-07-24 | M1 batch N=16 wall | headless | **41.3 ms** | `main` serial (no pipeline) |
| 2026-07-24 | M1 get-cpu mean | headless | **1.268 ms** | tip `threading/8-cpu-history` (C64M/2) |
| 2026-07-24 | M1 get-cpu p99 | headless | **1.310 ms** | tip |
| 2026-07-24 | M2 get-frame mean | headless | **2.157 ms** | tip, indexed8 warm |
| 2026-07-24 | M3 64K memory | headless | **1.64 ms** | tip bulk `get-memory 65536` |
| 2026-07-24 | M1 batch N=16 wall | headless | **2.64 ms** | tip pipeline (~7.7× vs serial mean) |

**Verdict:** tip is same or better on all measured metrics (large wins on M1, M3, batch).

---

## 17. Quick reference — do not violate

1. No machine access from socket or audio threads.
2. No reverse dependencies (`architecture.md`).
3. Docs track source in the same change.
4. **Solicited work uses `request_token`; unsolicited uses 0.**
5. **Telemetry may be latest-wins; RPC results must be exactly-once or explicitly cancelled.**
6. **Reliable completions never silently drop on full queues.**
7. **No 64K (or 576 KiB) payloads inside `runtime_event` / 256-deep event queues.**
8. **`uint16_t` length cannot represent 65536 — widen length; reject wrap.**
9. **Phase 2b needs real socket multiplexing, not “remove wait_pop only.”**
10. **Phase 6 cache requires barrier + stamps from Phase 4, not “paused-looking.”**
11. Prefer one bulk copy over many RTTs — always.
12. Prefer headless for latency claims; do not call GUI vsync “the protocol rate”
    after Phase 3 without measuring.

---

## 18. Locked implementation defaults (2026-07)

Decisions for the full roadmap execution (no dual-path compat; stacked PRs;
docs-first 0.5a):

| Topic | Locked default |
|-------|----------------|
| Scope | Phases 0.5 through 8 |
| Compat | No backward-compat shims; explicit protocol version bump when wire/client rules change |
| Wait concurrency (§1.6) | **Restrict:** one outstanding wait; second → `busy` |
| Bulk RPC storage | Token-keyed result pool; not fat `runtime_event` |
| Bulk length | `uint32_t`, max 65536; reject wrap / oversize |
| Bulk write-history | Omit on bulk `get-memory` |
| Bulk concurrency | Pool capacity (aligned with deferred); full → `busy` |
| Duplicate wire id | `bad-id` while outstanding |
| Phase 2b I/O | Nonblocking + poll/select (revisit only if platform forces R/W split) |
| Response order | Completion order + wire ids |
| UI tokens in 0.5 | Control uses non-zero tokens; UI free-run/poll may stay token 0 |
| Connection epoch | Field + cancel stubs in 0.5; full drain with 2b |
| Python pipeline client | Only when 2b lands |
| Verification | Targeted ctest suites per phase; full baseline when authorized |
| PR slicing | §15 table; stacked branches/PRs |

Contract prose landed in `runtime-control.md` and `control-port.md` (PR-0.5a).

**Series status (2026-07, branch tip `threading/8-cpu-history`):** implementation
of phases 0.5–8 is **landed in code** on stacked branches (not necessarily
merged to `main`). Checkboxes above reflect that work: checked = done or
intentionally partial with note; unchecked = still open (metrics, full ctest,
PR-5b heap ownership, formal stress/oracle).

## 19. Feedback incorporation log

This revision responds to design review of the first roadmap draft:

| Feedback | Roadmap change |
|----------|----------------|
| No correlation token; UI can satisfy control deferred | §1.1 + Phase 0.5; tests for interleave |
| uint16 cannot hold 65536; Option A explodes queue | §1.4; Option A removed; uint32 length |
| Latest-wins slot ≠ multi RPC | §1.2; Phase 1 result pool / single-bulk policy |
| Lossy vs reliable push | §1.3; saturation → busy/error |
| Blocking socket + naive pipeline | §1.5; Phase 2b multiplex mandatory |
| Wait latch destructive; `wait_after_seq` unused | §1.6; wait policy before multi-wait |
| Phase 4 mega-bundle wrong cadence | Phase 4 rewritten (telemetry vs mutation) |
| Phase 6 weak freshness | Phase 6 depends on 4; barrier + invalidate |
| Heap + drain/destroy leaks | §1.7; Phase 5 ownership tests |
| Insert contracts before bulk/pipeline | Phase 0.5 blocking |
| Stronger identity tests | §13 deterministic matrix |
