# Sessions: multi-asker query foundation (Inspector prep)

**Status:** Closed (foundation). **No Inspector UI in this epic.**  
**Sibling:** Closed foundation in `../a2m` as **A2M/11** — see
`../a2m/agents/sessions.md` and commits `5e203fc`…`173873c`.  
**Product name (later UI):** Inspector (F9-like second view; not this campaign).  
**Depends on:** flight recorder + history wire (C64M/3+), frame ring (C64M/5+).  
**Unblocks:** Inspector UI / shared flight-recorder browsing between human UI and
socket agent without cursor stomp.  
**Wire:** **C64M/7** (unsolicited `state-changed` + `sessions` / `state-changed`
capabilities).

Related: [`control-port.md`](control-port.md) · [`using-c64m.md`](using-c64m.md) ·
[`cpu-flight-recorder.md`](cpu-flight-recorder.md) ·
[`runtime-control.md`](runtime-control.md) · [`testing.md`](testing.md).

Source is authoritative. If this brief and code disagree after implementation,
fix the brief in the same change.

---

## Sibling port notes (read first)

This epic is **product-shell work**, not C64 machine work. a2m already landed it;
c64m should adapt the same decisions and shapes.

| Piece | Portable from a2m? | c64m adaptation |
|-------|--------------------|-----------------|
| Session table + per-asker history cursors | **Yes** — nearly mechanical | Same `runtime_session` / capacity 4; replace global `rt->history_cursor` |
| `runtime_client` session open/close + history `session_id` | **Yes** | Mirror a2m APIs; history helpers currently omit session id |
| TCP client ↔ one control session | **Yes conceptually** | Bind in **`src/main.c`** (c64m has no `control_dispatch.c`; a2m put S2 there) |
| `state-changed` inform + wire event | **Yes** | Protocol name **C64M/7**; cycles from C64 total-cycle counter; teach `tools/c64_control_client.py` |
| History arena / HST1 engine | Already shared in spirit | `runtime_history.c` is **identical** today — do not fork it for sessions |
| Machine / VIC / CIA / 1541 | **N/A** | Out of scope |
| Deferred table | Do not conflate with sessions | c64m `CONTROL_DEFERRED_CAPACITY` is **16** (a2m is 1). Keep deferred semantics; sessions are a different axis |

**Naming collision:** `control-port.md` already uses “session” for the TCP
connection / `connection_epoch`. Runtime **sessions** (this epic) are asker slots
with per-cursor state. Keep the wire wording clear: connection epoch vs
`session=` id on informs.

**Reference implementation map (a2m):**

| Phase | a2m commit (approx) |
|-------|---------------------|
| Brief | `5e203fc` |
| S0/S1 session table + client | `37ecee8` |
| S2 control bind | `686b0f5` |
| S3 state-changed + A2M/11 | `6d6b379` |
| S4 docs | `173873c` |

Do not cold-cherry-pick those commits into c64m. Use them as a patch map against
this brief’s c64m anchors.

---

## Why

c64m accepts **one TCP control client**. Coop wants an **AI on that socket** and a
**human at the glass** both interrogating the same paused machine (history FIND
pages, frame scrub, CPU/mem snapshots) without a separate app.

Today the *capability surface* exists (C64M verbs + `runtime_client`). The
*session state* does not:

| Piece | Today | Problem |
|-------|--------|---------|
| History cursor | **One** `runtime_history_cursor` on `runtime` | Second FIND stomps the first |
| Control deferred | `CONTROL_DEFERRED_CAPACITY = 16` | Fine for concurrent token waits; **not** the multi-cursor problem |
| UI path | Intents → `runtime_client` (no session id) | Cannot own a cursor alongside the socket |
| Mutation awareness | Peer only sees effects if it re-polls / hits cursor stale | AI mid-analysis does not get an explicit nudge |

`cpu-flight-recorder.md` currently documents “one active history search cursor
per runtime.” That claim becomes false when S0 lands — update it in the same
change.

**Sessions** are the fix. Inspector UI is a later consumer of the same API.

---

## Decisions (pinned — do not re-litigate in implementation)

| Topic | Decision |
|-------|----------|
| Name (product, later) | **Inspector** — out of scope for UI here |
| Unit of multi-asker | **`session`**: endpoint + stateful query bits + reply routing |
| Where sessions live | **Runtime-authoritative** for history cursors / mutation notify. Control binds the TCP client to one session. UI will later bind via `runtime_client` session APIs. |
| Capacity | Fixed small **N = 4** slots (product needs 2: UI + socket; 4 avoids a resize story) |
| Ownership / who may step | **Open mutation** — UI and socket may both step/run/poke. **No ask-to-step. No “socket owns the machine.”** |
| Inform | On mutation, publish **`state-changed`** (runtime event → control wire push to other live sessions). Awareness only, not a lock. |
| Soft “lead” badge | **Non-goal** this epic |
| Soft hold / steal control | **Non-goal** this epic |
| Whole-session “invalid” | **No.** Stale **cursors / scrub anchors** only; session object stays alive until endpoint disconnect / explicit close |
| Deferred table | Keep current deferred capacity / multi-vs-exclusive rules. Do **not** conflate deferred slots with sessions. |
| Protocol bump | Bump **C64M/N** when the wire gains session id and/or unsolicited `state-changed` events clients must understand (target **C64M/7**) |
| Headless | Sessions must work headless (socket-only: one session). Dual-session tests may be in-process (two runtime session ids, no UI). |
| Separate control app | **Non-goal** — humans use future Inspector; agents keep using the socket |
| Extract shared lib with a2m | **Non-goal** this epic — port by brief + adaptation |

### Mutation set that invalidates history cursors

Already encoded in `runtime_command_invalidates_history_cursor()` in
`src/runtime/runtime_thread.c` (extend if a new mutator appears). Includes
step/run family, mem/reg writes, reset, history record/clear, save/load state,
assemble, media, machine config apply.

Inform (`state-changed`) should fire for the same class (and pause/run edge if
useful so agents notice freezes without only relying on wait latches).

---

## Shape (target model)

```text
                    ┌─────────────────────────────┐
  UI (later)  ─────►│ session[i]                  │
                    │  id, kind=ui|control        │
  TCP client  ─────►│  history_cursor             │
                    │  reply sink / epoch         │
                    └──────────────┬──────────────┘
                                   │ commands carry session_id
                                   ▼
                    runtime worker (serializes all cmds)
                    history arena + frame ring (shared, mostly stateless reads)
```

- **Stateless reads** (`get-cpu`, `get-memory`, `get-frame-at`, `history-read` by
  absolute id, info commands): any session; worker serializes; answer by
  request token / wire request id.
- **Stateful history page** (`history-find` / `next` / `close`): cursor lives
  **on the session**, not on a global `rt->history_cursor`.
- **Endpoints:**
  - `control` — created/replaced when TCP accepts; destroyed on disconnect
    (tie to existing `connection_epoch`).
  - `ui` — created via runtime API (tests now; Inspector later). At most one
    product UI session initially is fine; API should still be id-based.

Request correlation stays as today:

- Wire: client-chosen request id within the TCP connection.
- `runtime_client`: request tokens.
- Do **not** invent a second id taxonomy (“UI ids vs socket ids”). Session id
  names the asker; request id/token names the question.

---

## Non-goals (this epic)

- Inspector UI, key chord, layout, Nuklear panels
- Auto-pause-on-Inspector-open (UI policy later; may call existing `pause`)
- Multi-TCP clients
- Time travel / reverse execution
- Changing C64M command *semantics* for single-client scripts beyond additive
  session fields / events (old one-client scripts should keep working)
- Replacing `coop_watch` / `Ctl` (update them only as needed for new events)
- Extracting a shared a2m/c64m library
- Moving c64m’s control path out of `main.c` into a `control_dispatch.c`
  (optional cleanup; not required to claim sessions done)

---

## Current code anchors (start here)

| Area | Path |
|------|------|
| Single history cursor | `src/runtime/runtime_internal.h` (`runtime_history_cursor`, `history_cursor`) |
| Invalidate / FIND / NEXT / CLOSE | `src/runtime/runtime_thread.c` (`runtime_history_invalidate_cursor`, history_*_command, `runtime_command_invalidates_history_cursor`) |
| History client API | `src/runtime/runtime_client.c` / `.h` (helpers lack `session_id` today) |
| History engine | `src/runtime/runtime_history.c` (identical to a2m — leave alone for this epic) |
| Control deferred | `src/control/control_deferred.h` (capacity 16; exclusive vs multi) |
| Control accept / epoch / request loop | `src/control/control_server.c` + **`src/main.c`** (dispatch, deferred complete, hello/version/capabilities) |
| Wire parse/format | `src/control/control_protocol.*` |
| History tests | `tests/runtime/test_runtime_history.c`, `test_runtime_history_wire.c` |
| Control history integration | `tests/control/test_history_control.py` |
| Protocol / ops docs | `agents/control-port.md` · `agents/using-c64m.md` |
| Python | `tools/c64_control_client.py` · `tools/coop_watch.py` |
| One-cursor claim to retire | `agents/cpu-flight-recorder.md` (“Cursor” section) |

a2m reference (do not compile against; read for shape):

- `../a2m/agents/sessions.md`
- `../a2m/src/control/control_dispatch.c` (S2/S3 bind + event flush — map onto `main.c`)
- `../a2m/tests/runtime/test_runtime_history_sessions.c`
- `../a2m/tests/runtime/test_runtime_state_changed.c`

---

## Phases

Implement in order. Each phase ends with **build + ctest green** and a short
note in this file (Landed line). Prefer small commits per phase. Mirror a2m’s
S0→S4 unless a c64m bind-site detail forces a tiny reorder (document if so).

### S0 — Session table in runtime (no wire change yet)

**Goal:** Replace the single `history_cursor` with a fixed session table; one
default session preserves today’s behaviour.

- Add `runtime_session` holding at least:
  - `uint32_t id` (never 0 when active)
  - `kind` (`none` / `ui` / `control`)
  - `active` flag
  - `runtime_history_cursor` (move fields from today’s global cursor)
  - optional `generation` / endpoint epoch for control binding
- `runtime` holds `sessions[RUNTIME_SESSION_CAPACITY]` with **CAPACITY = 4**
- APIs on runtime (worker-side helpers + client wrappers as needed):
  - allocate / release session by kind
  - lookup by id
  - “default” session id for backward-compatible commands that omit session
- Move FIND/NEXT/CLOSE / invalidate paths to **per-session** cursor
- `runtime_history_invalidate_cursor(rt)` becomes invalidate **all** active
  session cursors (generation bump stays global on the history arena as today)
- Commands that touch history must carry `session_id` (0 = default session)

**Exit:**

- Existing history ctests still green with default session
- New unit/runtime test: **two session ids**, interleaved `HISTORY_FIND` with
  `more`, each `HISTORY_NEXT` only advances its own cursor; no cross-stomp
- Global `rt->history_cursor` removed
- Update `cpu-flight-recorder.md` Cursor section: per-session, not per-runtime

**Landed:** Session table `sessions[4]`; default UI session at create;
FIND/NEXT/CLOSE per-session cursor; invalidate-all on mutation;
`runtime_history_sessions` ctest; global `rt->history_cursor` deleted;
`cpu-flight-recorder.md` Cursor section updated.

---

### S1 — `runtime_client` session surface (still no Inspector)

**Goal:** Host/tests can open a UI-kind session and address history RPCs by id.

- `runtime_client_session_open(kind)` → session id (prefer worker-allocated ids
  via token + `RUNTIME_EVENT_SESSION_RESPONSE`, matching a2m)
- `runtime_client_session_close(id)`
- History helpers (`history_find` / `next` / `read` / `close`) take `session_id`
- Non-history snapshot APIs may ignore session id for now
- Document: omitting session id uses default/control-compat session
- Optional: `runtime_client_set_command_session` stamp for mutation source id
  (a2m has this; useful for S3 `source_session_id`)

**Exit:**

- Test opens two sessions via client, dual FIND as in S0, then closes both
- No frontend changes
- Control path in `main.c` still passes 0 until S2

**Landed:** `runtime_client_session_open/close` + `RUNTIME_EVENT_SESSION_RESPONSE`;
history helpers take `session_id` (0 = default); dual-session test closes both;
control path in `main.c` still passes 0 until S2.

---

### S2 — Control port binds TCP client → one session

**Goal:** Socket client is a first-class session; disconnect frees it.

- On accept: allocate `kind=control` session; store id beside the control
  connection state used by **`main.c`**
- On disconnect / epoch bump: close that session; cancel deferred as today
- History wire commands use that session id when calling runtime
- Single-client scripts with no session syntax keep working (dispatch fills id)
- Still **one TCP client** max — do not add multi-TCP

**c64m-specific:** a2m implemented this in `control_dispatch.c`. Here the
analogous sites are the control request switch and deferred completion paths in
`src/main.c`, plus epoch handling already tied to `control_server`.

**Exit:**

- Headless: `history-find` / `next` behave as C64M/6 today
- Disconnect mid-FIND leaves no orphan cursor (slot reusable)
- `tools/c64_control_client.py` history smoke unchanged for happy path

**Landed:** Control TCP client binds `kind=control` session in `main.c`; history
FIND/NEXT/READ/CLOSE use that session id; disconnect/epoch/shutdown releases
the slot; single-client scripts omit session syntax as before.

---

### S3 — Inform: `state-changed` events

**Goal:** Other askers learn the machine/timeline moved — awareness only.

- Add `RUNTIME_EVENT_STATE_CHANGED` with a small payload:
  - `reason`: at least `step`, `run`, `pause`, `poke`, `reset`, `load-state`,
    `history-clear`, `media`, `other`
  - `source_session_id` (0 if unknown / internal)
  - useful anchors: `cycles`, `frame` (if cheap), history `epoch` if available
- Emit from the same places that invalidate history cursors (and pause/run
  edges as needed). Coalesce per command completion, not per Φ0.
- Control path (`main.c`): when appropriate, push an **unsolicited** wire line,
  e.g.

  ```text
  0 event state-changed reason=step session=2 cycles=12345 epoch=1
  ```

  Pin exact framing in `control_protocol` + `control-port.md` + `using-c64m.md`.
  Using request id `0` for events is fine if documented; pick one and test it.
- Bump **C64M/7** (`hello` / `version` strings in `main.c`); advertise in
  `capabilities` (e.g. `sessions` and/or `state-changed`).
- Python: teach `Ctl` in `tools/c64_control_client.py` to parse/ignore or
  surface events without breaking `cmd()` request matching.

**Exit:**

- Test: session A steps (or write mem) → session B (or control client) observes
  `state-changed` with expected reason
- Dual history test: after step, NEXT on old cursor → cursor stale; re-FIND works
- Existing coop smoke still works; watcher may log events

**Landed:** `RUNTIME_EVENT_STATE_CHANGED` on history-invalidating commands and
pause; wire `0 event state-changed reason=… session=… cycles=… frame=… epoch=…`;
C64M/7 + capabilities `sessions` / `state-changed`; `Ctl` skips events;
`runtime_state_changed` ctest (step → stale → re-FIND).

---

### S4 — Polish + docs gate (still no UI)

**Goal:** Agent-ready surface; Inspector discussion can start.

- Update [`control-port.md`](control-port.md), [`using-c64m.md`](using-c64m.md),
  [`runtime-control.md`](runtime-control.md), [`cpu-flight-recorder.md`](cpu-flight-recorder.md),
  [`testing.md`](testing.md), manual remote section as needed
- Confirm rules: no live `c64_t *` across threads; sessions hold no live machine
  pointers — only cursor metadata + ids
- Optional: frame-ring scrub “focus” is **not** required in sessions yet —
  absolute frame queries are enough for UI later

**Exit:**

- Checklist below all checked
- This file: Status → Closed (foundation); UI remains follow-up

**Landed:** Docs + manual + README; checklist complete; epic foundation closed.
Inspector UI remains follow-up. Sessions hold cursor metadata + ids only (no
live `c64_t *`).

---

## Acceptance checklist (foundation done)

- [x] Fixed session table (N=4) in runtime; no global history cursor
- [x] Two in-process sessions can page history independently
- [x] TCP client maps to one control session; disconnect cleans up
- [x] Open mutation; no permission protocol
- [x] `state-changed` inform reaches the socket client; `Ctl` safe with events
- [x] C64M version bumped + capabilities + docs
- [x] Full ctest green
- [x] **No** Inspector UI / host chord / frontend layout work required to claim done

---

## Agent script (follow exactly)

```text
1. Read agents/sessions.md (this file), agents/control-port.md (history +
   versioning), agents/cpu-flight-recorder.md (cursor section), agents/testing.md.
   Skim ../a2m/agents/sessions.md + the S0–S3 commits for shape only.
2. Implement S0 → ctest. Update "Landed" line. Fix cpu-flight-recorder.md cursor claim.
3. Implement S1 → ctest. Update "Landed".
4. Implement S2 in main.c (+ control_server epoch hooks as needed) → ctest +
   quick Ctl history-find smoke. Update "Landed".
5. Implement S3 → ctest + Ctl event parse. Bump C64M/7. Update docs in same
   change as the wire bump. Update "Landed".
6. S4 doc/status polish. Mark epic foundation closed in this file + README.
7. Stop. Do not start Inspector UI.
```

If blocked (e.g. event framing choice), pick the smallest option that keeps
request/response matching correct for `Ctl.cmd`, document it, continue. Prefer
matching a2m’s `0 event state-changed …` framing unless a c64m constraint forbids it.

---

## Out-of-scope notes for the *next* discussion (UI)

Not binding — only so implementers do not “helpfully” build UI now:

- Inspector toggle key (distinct from existing debugger chords)
- Auto-pause on enter; resume policy on exit
- Scrub UX for frame ring + history; how human says “look at frame N” to the AI
- Whether UI talks C64M verbs locally or only `runtime_client` session APIs
  (both are viable once S0–S3 exist; prefer one mental model with the agent)

---

## Protocol versioning note

Current product wire at epic start: **C64M/6**. This epic bumps to **C64M/7**
when S3 (or S2 if session ids become visible on the wire) lands. Update
`control-port.md`, `using-c64m.md`, `hello`/`version` strings, and
`tools/c64_control_client.py` in the same commit.
