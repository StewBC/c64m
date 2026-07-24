# Control-port improvements worth making

Not a handoff — a **backlog note** for a future agent. Everything here came out of
one real session (Deus Ex Machina "spirals" moiré, c64m vs VICE, 2026-07-23) where
the control port was the instrument. Items are ordered by how much time they would
have saved.

**Status (2026-07-23 implementation pass):** correctness bugs, watchpoints,
`get-vic`/`get-cia`, raster on `get-state`, `step-frame`, `format=indexed8`, and
count-only breakpoints are **done** (see `control-port.md`). Remaining items are
below. The comparison baseline is still VICE's binary monitor.

---

## 1. Correctness bugs — DONE

### `get-debug-memory` stale cache — fixed

Always requests a fresh snapshot; no longer serves a pinned generation.

### `wait-event load-state-complete` race — fixed

Sticky latch for completion events; wait can consume an event that already fired.

### `get-state` / `wait-paused` `stop=none` on breakpoint — fixed

`wait-paused` completes only after `MACHINE_STATE_RESPONSE` so `stop=` is current.

---

## 2. Missing capability — largely done

### Memory watchpoints — fixed

`break-create write|read|store|load|read-write|load-store <addr> [end=…]`.

### Structured VIC / CIA — fixed

`get-vic`, `get-cia 1|2` (includes raster compare latch and CIA ICR mask).

### Current raster/cycle in `get-state` — fixed

`raster=` and `vic_cycle=` when hardware snapshot is available.

### `run-to-raster` / conditional breakpoints — still open

There is no way to run to a raster position. `step-frame` covers frame-anchored
capture; per-line run-to and expression-guarded checkpoints (VICE `CONDITION_SET`)
are still missing. Useful next step for raster-IRQ demos.

---

## 3. Throughput and stepping

### Every command costs up to one ~16.7 ms main-loop tick — still open

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

**Mitigation available now:** `step-frame` advances one frame and publishes without
depending on free-run + wait-frame aliasing. Prefer it for frame-accurate capture.

### `step-frame` — done

Advances one full VIC frame, publishes it, pauses. Frame-accurate capture no longer
requires an exec breakpoint in the target. `run-cycles` also republishes completed
frames when a boundary is crossed.

### `get-frame format=indexed8` — done

Indexed-8 palette indices; ~4× smaller and easier to compare with VICE
`DISPLAY_GET`.

### Count-only / non-stopping breakpoints — done (and re-fixed)

`actions=none` on `break-create` / `break-update`: hits accumulate while free-running;
read via `break-list` `hits=`.

Runtime validation originally rejected a zero action mask, so installs never
happened and the control deferred wait hung (parser-only test passed). Fixed:
zero mask is valid; runtime install/count regression covers it; failed installs
return `error runtime …` instead of timing out.

### No CPU instruction history — still open

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
- Sticky completion events keep `load-state` / `wait-event` races from producing
  false timeouts.
- Fresh `get-debug-memory` and `get-vic` / `get-cia` make the control port a
  trustworthy measuring instrument against VICE.
