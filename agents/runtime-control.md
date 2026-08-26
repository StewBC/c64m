# Runtime, Inspector, recorder, and rings

## Ownership

`src/runtime/runtime_thread.c` owns the runtime worker and the live `c64_t`.
Commands arrive through `runtime_client` and message queues. Runtime publishes
copied events and snapshots. The frontend must use `runtime_client`. The
control socket thread must not poll those surfaces or touch the machine.

Runtime supports run, pause, reset, cycle/instruction stepping, step-over/out,
run-to-cursor, `step-frame`, `run-to-raster`, breakpoints, input, paste,
disk/file ops, assembler, save/load state, and turbo 1/2/3.

Turbo field names still say `active_turbo_multiplier` / `turbo_speeds`; values
are mode IDs: 1 normal (real-time, live pixels), 2 max (free-run, live pixels),
3 warp (free-run, paint off). Max is the correctness and throughput bar
(`testing.md`).

While free-running, the main loop must not poll fat snapshots every frame.
Machine telemetry is once per UI present. Breakpoint and disk tables refresh
on mutation or a full debug refresh (startup, step, pause).
`request_debug_state()` is the full refresh; free-run uses
`request_debug_telemetry()`.

## Message contracts

Every solicited runtime command carries a `request_token` (`uint64_t`). Token
**0** is unsolicited telemetry and must never complete a control deferred
entry. Non-zero tokens complete exactly one waiter. Wire request ids are a
client/connection concern and are not the same as `request_token`.

Three identities, never mixed:

| Axis | What |
|------|------|
| Client | `(connection_epoch, wire_request_id)` |
| Runtime | `request_token` |
| Asker | runtime session id (HST1 cursor owner) |

On each accepted TCP connection, main bumps `connection_epoch` and binds one
`kind=control` runtime session. Disconnect cancels that epoch's deferred
work, closes the session, and must not leak payloads to the next client.
Duplicate outstanding wire ids in one epoch: `bad-id`.

Default UI session is slot 0, id 1, never released. Capacity is 4
(`RUNTIME_SESSION_CAPACITY`). Open mutation: UI and socket may both step;
there is no lock. `state-changed` is awareness only.

Telemetry slots (frame, free-run machine/CPU) are latest-wins and may drop.
RPC results (get-memory, HST1 pages, assemble, save/load) are exactly-once to
the token owner, or explicit cancel/error. Bulk data uses
`runtime_rpc_payload_pool`, keyed by token and kind. Do not enlarge
`runtime_event` unions to carry 64K memory or history pages.

`message_queue_push` may return false when full. Lossy notifications (frame
ready, free-run telemetry) may drop. Reliable completions must not vanish:
backpressure, `busy`, or a path that cannot drop without cancel.

At most one outstanding control wait (`wait-paused` / `wait-running` /
`wait-frame` / `wait-event`). A second wait gets `busy`. No live `c64_t`
pointers in queue items.

Touchpoints: `runtime_command.h`, `runtime_event.h`, `runtime_thread.c`,
`runtime_client.c`, deferred match in `src/main.c`, pipeline in
`control_server.c` (high-water 16).

## Control port

Opt-in localhost server: `--control-port PORT`. One client. Socket thread
owns I/O; SDL/main drains requests. `--headless` requires a control port and
skips window/renderer/frontend/host audio; the headless loop wakes when a
control request is queued. `quit-client` closes the socket, not the process.

Wire is **C64M/8**. Grammar, payloads, and client sketch: `control-port.md`.
Recipes: `using-c64m.md`.

## CPU flight recorder (HST1)

Forensic instruction log for the main 6510. Answers "who wrote `$22` to
`$D020`". It does **not** restore the machine. Inspector is a different
product. The in-emulator UI for FIND is **Forensics** (`frontend-debugger.md`);
HST1 remains the data name.

Runtime owns `runtime_history`. Default 256 MiB; `[debug] history_memory_mb`
/ `--history-memory` accept 0 (off) or 16..4096. Allocation failure is
nonfatal and visible through `history-info`. Observer installed only while
available and recording. Inspector Record does **not** arm or stop HST1.

Resets retain records and advance `timeline`. Successful state load clears
the arena and advances `epoch`. Save-state never serializes recorder state.
FIND/NEXT/READ require an explicitly paused runtime. One cursor **per
session**; mutation (run/step/reset/load/poke/media/history-clear) stamps
cursors stale (`state-changed` then re-FIND). Pages are HST1 in the RPC
pool (`runtime_history_wire.h`: 24-byte header, 48-byte record, 8-byte
accesses). Encode and decode: `runtime_history_wire_*`. Find-option grammar:
`runtime_history_query_parse.*` (duplicate keys last-wins; public key/access
tables). Python: `tools/c64_control_client.py` (`Ctl.decode_hst1`). Forensics
reuses default UI session `0` and closes the history cursor on leave.

## Frame ring and VIC ring

Frame ring (`runtime_frame_ring`): rolling completed **indexed8** frames so
a glitch still exists after a late human pause. Default 128 MiB (~827 PAL
frames). Keyed by frame number **and** `machine_cycle`. Warp geometric dumps
are not stored. Generic lookup: nearest at-or-before; older than the window
is `not-found`. Inspector CRT uses **exact** `machine_cycle` / cell-film join
only (never a neighbour still labeled as this cycle).

VIC ring (`runtime_vic_ring`): per-line latched VIC state, including the
sprite X used for paint. Default 16 MiB. `vic-ring-record off` stops
**store**; the observer still builds the record until budget is 0.

Join film, VIC lines, HST1, and Inspector checkpoints by **`machine_cycle`**,
not frame number. Frame numbers have gaps.

## Guarded breakpoints

`runtime_breakpoint_condition.{c,h}`: after address/access/mapping already
matched, evaluate a bounded AND-list (max 4 terms). No OR, no grouping. If
a case needs OR, arm two breakpoints. `value` plus exec is rejected at
create. Invalid `when=` is sanitized to unguarded. INI uses `;` because the
list is comma-separated. Same list in live and Inspect. Wire syntax:
`control-port.md`.

## Inspector

Opt-in time travel: checkpoint ring + input log + land + sealed re-execute
into the **one true** `c64_t`. Names: `runtime_inspector_*` only.

Default off. `--inspector` / `[debug] inspector`. Memory
`--inspector-memory` / `inspector_memory_mb` (0 or 16..4096; default 128).
Does **not** arm or stop HST1. `--inspector-off-on-max` (default true):
turbo 2/3 wipes Record (and film if Record was on) and remembers it for
leave-max; turbo 1 restores Record into an empty window.

| Term | Meaning |
|------|---------|
| Record | Opt-in checkpoint + input log (+ film if the frame-ring budget is > 0) |
| Inspect | Mode: the live `c64_t` **is** the past. Views keep talking to it. |
| Land | Quantized: nearest checkpoint `<=` cycle. Exact: `land_to_cycle` (checkpoint + sealed reexecute). Far right / live = restore NOW. |
| Film | Preferred Indexed8 still for a Record cell (`film_cycle`). Scrub miss = full pink; committed miss = reconstruct. Never invent a neighbour still. |
| Sealed | During re-execute: CPU observer off, mem-access CB off, no frame-ring push, no host audio, no host media write-through |
| NOW | Blob of live state taken on enter. Leave restores it, paused. |

### Record clock and timeline

Normal checkpoints birth on the **frame publish path**
(`runtime_publish_completed_frame`): push film (when not turbo-display) →
non-reentrant instruction-boundary finish → checkpoint with that
`film_cycle` (0 when the ring did not push). Free-running
`cycles_per_frame` cadence on `after_step` is **not** the Record clock.
Non-frame allow-list takes (`film_cycle = 0`): Record enable startup, enter
Inspect (LIVE-adjacent), media-empty refill, history-invalidate refill.
Sealed Inspect does not push film or birth CPs. Warp/FAST stall film but
still birth CPs when recording; MAX can still push film.

**Checkpoints are the timeline index** for scrub / `[-]` / `[+]`. Film is the
preferred picture per cell; retention budgets may differ. Compact shared
`(cycle, film_cycle)` index supports UI cell-film join (local read, no scrub
RPC); index survives enter disarm and clears only with the tape.

### CRT / honesty

| Situation | CRT |
|-----------|-----|
| Scrub thumb-down, cell-film join hits | Blit that still |
| Scrub thumb-down, no film | **Full pink** (no reconstruct on drag) |
| After land / `[-]` / `[+]`, focus on a CP with exact `film_cycle` hit | Blit film |
| After mid-frame focus between CP cells (`land_to_cycle` etc.) | Reconstruct only — no neighbour still |
| After land / `[-]` / `[+]`, no usable film | **Reconstruct** from landed machine; **no** pink watermark |
| Any path | **Never** neighbour still labeled as this cycle |

Worker land/± completion uses `runtime_inspector_publish_committed_head`
(film-first, else present/reconstruct). LIVE / NOW is the right-edge
exception (enter/leave presentation), not "missing film ⇒ pink."

Pinned product rules:

- One debugger skin. Enter Inspect starts at live. Leave restores NOW.
- Pokes/media/save-state/history-record fail with `read-only-inspector`.
- `[+]` / `[-]` = Record checkpoint walk (strict next/prev CP; `[+]` past
  newest → LIVE/NOW). F10-family / F12 remain sealed execute toward live.
  Nothing executes past live.
- F12 stops on the one breakpoint list or at live; stays in Inspect.
- Guest media **write that succeeds** cuts the window (older checkpoints,
  inputs, and film drop). A refused write-protect does not cut. Housekeeping
  (eject flush, save-state, export) must not cut.
- Timeline is oldest retained checkpoint -> live. A media cut or max/warp
  wipe moves `oldest`; never leave islands.
- Promote / Branch is out (`known-gaps.md`).

Control: `get-state` reports `mode=live|inspector` and `focus_cycle`.
`enter-inspector` / `leave-inspector`. Tests: `runtime_inspector`,
`runtime_inspector_replay`, `runtime_inspector_mode`,
`inspector_control_integration`. UI: `frontend-debugger.md`.
Design history: `design/inspector-frame-synced-record.md`.

## Save-state

Runtime file I/O on the worker. Failed load preserves the live machine.
Successful load clears HST1, frame/VIC rings, and Inspector tape. Format:
`machine.md`.
