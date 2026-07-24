# Control-port improvements worth making

Not a handoff — a **backlog note** for a future agent. Everything here came out of
one real session (Deus Ex Machina "spirals" moiré, c64m vs VICE, 2026-07-23) where
the control port was the instrument. Items are ordered by how much time they would
have saved. Nothing here is implemented; `control-port.md` describes what exists
today.

The comparison baseline is VICE's **binary monitor** (`src/monitor/monitor_binary.c`
in the VICE tree; see `vice-oracle.md`). It is not a better protocol than ours —
ours is friendlier to write a client for — but it exposes state we do not.

---

## 1. Correctness bugs (fix before adding features)

### `get-debug-memory` returns a stale cached snapshot, silently

This produced a **confidently wrong conclusion** that cost most of a session. Its
`generation=` field stayed pinned at 759 across many frames while RAM was
demonstrably changing; a frame-to-frame diff of the payload therefore reported
**0 bytes changed**, which read as "the demo's CPU does no work this frame". The
correct answer (via `get-memory ... ram`) was ~180 bytes/frame.

It fails silently — no error, no staleness flag in-band beyond a `generation` that
a caller has no reason to check. Either refresh it on demand, or refuse to answer
when the cached snapshot is older than the current frame. At minimum, the response
should be self-describing enough that a stale answer cannot be mistaken for a
fresh one.

### `wait-event load-state-complete` times out on a successful load

`load-state` followed immediately by `wait-event load-state-complete 5000` returns
`error timeout` even though the load succeeded. The event appears to fire before
the wait is registered, and events are not queued or sticky. Any request/response
pair with this shape is a race. Consider latching events with a sequence number so
a waiter can ask for "the next event after N".

### `get-state` reports `stop=none` on real breakpoint hits

Stepping an exec breakpoint, the first hit of each frame reported
`state=paused ... stop=none` while later hits in the same frame reported
`stop=breakpoint`. The PC was correct in both cases, so the stop *happened*; only
the reason was wrong. Worth a look — automation branches on `stop=`.

---

## 2. Missing capability that forced a source rebuild

### Memory watchpoints (break on load/store), not just exec

This is the biggest gap. VICE's `CHECKPOINT_SET` takes an address **range** plus an
op mask (`load=1, store=2, exec=4`), so "stop whenever anything writes `$D012`" is
one call. c64m's control port is exec-only (`break-create exec`), so the equivalent
question required rebuilding with `-DC64M_VIC_TRACE` and using the `C64M_VICLOG`
file trace — a full re-link and an out-of-band log to correlate by hand.

A store/load watchpoint over an address range would have removed that entire step.
The machine-side write-history already exists (`get-debug-memory write-history=1`),
so the data is there; what is missing is stopping on it.

### Structured VIC state (`get-vic`), and CIA state (`get-cia`)

VICE's monitor `io d000` returns raster line *and cycle*, video mode, scroll, RC,
idle state, VC/VCBASE/VMLI, video/charset bases, and per-sprite DMA/display state.
c64m can only be asked for the register bytes, which is strictly less: several
pieces of VIC state **cannot be recovered from the registers at all**.

Concretely, two things blocked diagnosis:

- **The raster compare latch.** Reading `$D012` returns the current raster, not the
  value last written. There is no way to ask "what line is the VIC armed for?", so
  a missing raster IRQ cannot be distinguished from a never-armed one without
  guessing from the code.
- **The CIA ICR mask.** Not readable on real hardware either, and not exposed by
  us. The session's final diagnostic gap — "is c64m failing to *deliver* a CIA
  timer IRQ, or is the demo's mask simply off?" — could not be closed from the
  control port. It had to be inferred indirectly (a latched-but-unmasked flag
  would re-enter the handler continuously; it did not).

Both are debug-only reads of internal state, so they cost nothing in fidelity.
`get-vic` is the higher-value one and pairs directly with the oracle workflow.

### Current raster/cycle in `get-state`

VICE exposes `LIN` and `CYC` as pseudo-registers in `REGISTERS_GET`, so every
register query answers "where is the beam?" for free. c64m needs a separate
`get-memory $D012` round trip, which yields 8 bits and then needs `$D011` bit 7
for the ninth. Adding `raster=` and `cycle=` to `get-state` (or `get-cpu`) would
remove a round trip from every single iteration of a raster-anchored loop.

### `run-to-raster <line>` / conditional breakpoints

There is no way to run to a raster position. Every frame-anchored capture had to
begin by reverse-engineering the target program to find an address it executes
once per frame. VICE also has `CONDITION_SET` for expression-guarded checkpoints;
`run-to-raster` alone would cover most of the need.

---

## 3. Throughput and stepping rough edges

### Every command costs up to one ~16.7 ms main-loop tick

Commands are dispatched on the SDL main loop, so latency is one tick regardless of
payload size — measured at 16.3–17.6 ms for `get-frame` whether paused or running.
Consequences:

- A `wait-frame 1` → `get-frame` loop **samples every other PAL frame** (frame
  deltas of 2), because one iteration exceeds the 20 ms frame period. That silently
  aliases anything that alternates per frame, such as a double-buffered effect.
- `run` → `wait-frame 1` → `pause` **overshoots by exactly one frame**, because the
  pause is processed a tick after the wait returns.
- Reading 64K via `get-memory` takes 64 calls ≈ 1.1 s (the 1024-byte cap). VICE's
  `MEM_GET` does 8KB in one call.

Worth considering: allow several queued requests to be serviced per tick; raise or
remove the `get-memory` length cap; or add a batch/pipeline form.

### No way to advance exactly one frame and get its pixels

`run-cycles` steps the CPU but **never republishes the frame** — confirmed, the
`frame=` and `cycle=` metadata on `get-frame` stay frozen across repeated
`run-cycles 19656`. (`pal-border.md` already records this; it is repeated here
because it is the root of the problem.) Combined with the tick latency above, the
only reliable way to capture N consecutive frames is to anchor on an exec
breakpoint in the target program and grab each frame while halted.

**A `step-frame` command that advances one full VIC frame and publishes it would
be the single most useful addition to the control port.** It makes frame-accurate
capture independent of the target's code, and it is exactly what the VICE side
achieves with a checkpoint plus `LIN == 0`.

### `get-frame` is 4x larger than it needs to be

The payload is 504x312 ARGB8888 at stride 2080 = 649KB per frame. VICE's
`DISPLAY_GET` returns the same 504x312 geometry as **indexed-8** = 157KB, which is
both smaller and *easier to compare* — it removes palette matching from the
comparison entirely (c64m and VICE do not use identical RGB values, so an ARGB
capture must be quantised back to colour indices before any diff). A
`get-frame format=indexed8` would be a small change with a large payoff for
oracle work.

### Breakpoints cannot count without stopping

`break-list` reports `hits=`, but there is no way to let a breakpoint accumulate
hits while the machine free-runs. "How many times per frame is this handler
entered?" — the question that located the defect in this session — required
stepping the emulator once per hit, several hundred round trips. A
count-only/non-stopping breakpoint action would answer it in one run.

### No CPU instruction history

VICE has `CPUHISTORY_GET` (0x86). c64m has `get-call-stack`, which is useful but
does not answer "what path did we take into this handler?". Lower priority than
the rest, but it is the natural companion to the watchpoints above.

---

## What is already good (do not regress it)

- The line-oriented text protocol with a raw-payload escape hatch is much easier to
  write a client for than VICE's binary framing, and the request-id echo makes
  desync obvious.
- `get-frame` publishing the **full raster line in VIC-X order** with no frontend
  crop is exactly right for oracle work — it means framebuffer x = VIC X with no
  origin to get wrong, and it is why the VICE mapping is a single modular rotation.
- Snapshot load/save over the wire (`load-state`) is what makes deterministic
  re-runs possible at all. VICE needs its binary monitor for the equivalent, and
  its *text* monitor cannot do it at all.
