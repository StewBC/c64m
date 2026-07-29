# Frame ring plan (Tier 1B)

**Status:** implemented (2026-07-28), Ring A and Ring B. The source and tests are
authoritative; the wire contracts live in `control-port.md` § Frame ring and
§ VIC ring.

## Ring B implementation record

The plan's premise held: c64m's VIC already maintained the latched per-line
sprite state this ring needed (`sprite_line_x[]`, `sprite_line_enabled[]`,
`sprite_visible[]`, `sprite_mc/mcbase[]`), so no new VIC modelling was required -
only a way to observe it. `sprite_line_x[]` *is* the "latched XMSB vs shadow
`$D010`" distinction the plan asked for.

- **Seam:** a per-line observer (`vicii_line_observer_fn`) called at end of line
  in `vicii_finish_cycle`, filling a `vicii_line_record` defined in `vicii.h`.
  The machine layer owns the shape of its own derived state; the runtime just
  copies the record into the ring. `vicii_begin_cycle` now stashes `abs_cycle`
  alongside the existing `paint_bus` stash, because `finish_cycle` has no
  `abs_cycle` of its own and the record must carry the shared machine-cycle axis.
- **Snapshot safety:** the observer lives on `vicii`, which is copied wholesale
  by `c64_snapshot_load`. It is saved and restored around that copy exactly like
  `c64_t::memory_access`, so a state load cannot silently stop recording. The
  serialized `.c64state` format is unaffected: `write_vic` enumerates fields.
- **Payload is text, not binary**, unlike the flight recorder's HST1. The
  recorder holds millions of records and needs a decoder; this holds hundreds
  per query, so `key=value` lines that an agent (or a human reading a coop snap)
  can consume directly are worth more than density.
- **Query shape:** `vic-ring-find [frame=] [raster=] [limit=]`, all optional.
  Omitting `frame=` matches the raster window in every retained frame, which is
  how a per-line effect is compared across frames.

Measured cost (Apple M2, headless, ROM enabling a sprite and toggling the
`$D010` MSB once per frame - so the VIC does real sprite work):

| Config | turbo 2 | turbo 1 |
|---|---|---|
| pre-Ring-B baseline | 14.142 MHz | 1.020 MHz |
| `vic_ring_memory_mb=0` (disabled) | 14.139 MHz | 1.018 MHz |
| ring enabled, recording on | 13.768 MHz | 1.019 MHz |
| ring enabled, recording off | 13.743 MHz | - |

**2.64%** of turbo-2 throughput, inside the recorder's ≤5% target but an order
of magnitude more than Ring A's 0.22% - expected, since this records ~280k
records/sec at turbo 2 versus ~900 frames/sec. Real-time is unaffected.

**Resident memory note.** With both rings on, `runtime_create` now reserves
144 MiB (128 frame + 16 VIC) on top of the recorder's 256 MiB, so a default
runtime asks for ~400 MiB. The ring allocations fail soft (capacity 0, emulator
runs on), but the recorder's allocation is fatal, so heavy memory pressure is
now marginally likelier to fail runtime creation. One transient
`runtime_scheduler` failure was seen while a second full build of the tree was
running concurrently; it did not reproduce across three subsequent full suite
runs or five standalone runs. Lower `frame_ring_memory_mb` if a host is tight.

**Honest wart:** `vic-ring-record off` stops *storing* but does not recover the
cost, because the record is still built each line before the ring rejects it.
Only `vic_ring_memory_mb=0` recovers it, and that measures identical to a build
without the feature (14.139 vs 14.142) because the observer is then never
installed. Both facts are documented rather than papered over. Making the toggle
a real perf control would require uninstalling the observer from the runtime
thread, which is more plumbing than the saving justifies.

Tests added:

- `tests/runtime/test_runtime_vic_ring.c` - capacity, wrap and drop accounting,
  range copy by frame and by raster window, no-frame-filter mode, limit, zero
  limit, recording toggle, clear, null safety.
- `tests/control/test_vic_ring_control.py` - end to end against a ROM that
  enables sprite 0 and toggles the `$D010` MSB once per frame, so the latched X
  must alternate `$0150`/`$0050` across frames while the live register settles on
  one value. A ring that sampled the registers instead of the per-line latch
  could not produce both, so that assertion is the real proof. Also full-frame
  line coverage and ordering, raster windows, cross-frame queries, limits,
  cross-reference fields, bad arguments, record toggle, and clear.

`tools/coop_watch.py` gained a `vic <frame> [first-last]` inbox verb that appends
the per-line state to the snap file, with a header noting that `spr_x` is the
latched value.

---

## Ring A implementation record

(What shipped, and where it differs from the plan below.)

- **Frames are stored as ARGB, not reduced to `indexed8`.** The plan chose
  indexed8 for a 4x memory saving, but the existing ARGB->index converter is a
  per-pixel linear scan of the 16-entry palette (~2.5M comparisons per PAL
  frame) and is lossy for any colour outside the palette, mapping it to index 0.
  Running that on every completed frame was the wrong trade. Conversion now
  happens on read, reusing the same code path as `get-frame`, so a ring frame
  and a live frame convert identically. Cost is 649 KB per frame instead of
  162 KB; the default 128 MiB budget still holds ~206 PAL frames (~4 s).
- **No RPC plumbing.** The plan specified token-keyed RPC like bulk memory. The
  ring instead carries its own mutex, the same shape as `runtime_frame_slot`'s
  existing cross-thread frame handoff, so control commands answer immediately
  from the main thread. Readers copy out rather than borrowing, so no pointer
  outlives the lock.
- **Lookups work while running**, not paused-only as planned: the lock makes it
  race-free, and forbidding it would only add friction. The retained window
  moves under a running machine, which is documented.
- **`get-frame-at` names its target** (`frame=` or `cycle=`) rather than taking
  a bare number. A bare number is ambiguous between a frame index and a machine
  cycle, and guessing wrong returns a plausible but wrong frame.
- A target past the newest clamps to the newest; one older than the window is
  `not-found` rather than a substituted neighbour.

Measured cost (Apple M2, headless, a ROM bumping the border once per frame):

| Config | MHz |
|---|---|
| pre-1B baseline, turbo 2 | 15.510 |
| turbo 2, ring recording off | 15.933 |
| turbo 2, ring recording on | 15.898 |
| turbo 1 (real-time), ring on | 1.027 |

The push costs **+0.22%** of turbo-2 throughput and nothing measurable at
turbo 1 (50 copies/sec). Post-change figures sit above the pre-change baseline,
i.e. no regression outside noise. The real cost is resident memory: 128 MiB by
default, tunable via `[debug] frame_ring_memory_mb` (`0` disables).

Tests added:

- `tests/runtime/test_runtime_frame_ring.c` - budget/capacity, push and info,
  wrap with drop accounting, nearest-at-or-before lookup by frame and by cycle,
  recording toggle, clear, and null/empty safety.
- `tests/control/test_frame_ring_control.py` - end to end against a ROM that
  bumps the border once per frame, so consecutive retained frames must differ by
  exactly one border step; also cycle lookup, format equivalence, out-of-window
  and bad-argument handling, record toggle, clear, and that warp records nothing
  and recording resumes afterwards.

`tools/coop_watch.py` gained `scrub [count]` and `frame <n>` inbox verbs, which
write retained frames to `snap-NNN-frames/*.png` (stdlib-only paletted PNG
writer) and record each frame's machine cycle for cross-referencing the
recorder.

---

The original plan follows.

## Why this exists

The CPU flight recorder answers "what *executed* before it went wrong". It cannot
answer the two questions that matter for VIC / sprite-multiplexer / one-frame
glitches:

> What did the screen actually **show** three frames ago?
>
> What VIC **internal** state produced that frame — which sprite DMA'd on line N,
> was it a badline, was the XMSB latched at display time different from the
> shadow register the CPU wrote?

A human pausing "a second late" is ≈50 PAL frames past the glitch, and the bad
frame's pixels are gone the instant the beam moves on. Sprite **register** writes
(`$D000..$D02E`) are already in the flight recorder (they are CPU bus writes, and
`coop_watch.py` already decodes them), so this plan does **not** re-record those.
It records the two things that are genuinely unreconstructable: the **pixels over
time** and the **VIC derived state over time**.

"Modern machines have 16 GB, just log everything" is practical here — see sizing.

## Two rings, one cycle axis

Both rings are keyed by `machine_cycle` **and** `frame_number` so they
cross-reference each other and the flight recorder. Given a bad `frame#` you can
pull: its pixels (frame ring), its per-line VIC state (VIC ring), and the CPU
records for its cycle span (flight recorder) — the full picture of one frame.

### Ring A — framebuffer ring (the pixels black box)

An N-deep ring of **completed `indexed8` frames** (palette index 0..15, one byte
per pixel — the oracle-compare format, and 4x smaller than ARGB).

- Tap point: `runtime_publish_completed_frame` /
  `runtime_publish_completed_frame_turbo` (`src/runtime/runtime_thread.c`, ~1160,
  ~1207). The completed snapshot already exists there; the ring stores an
  `indexed8` reduction (or stores ARGB and converts on read — decide by cost;
  indexed8 storage is preferred for size).
- Each slot: `{ frame_number, machine_cycle, width, height, uint8 pixels[] }`.
- **Only records at turbo 1/2.** Warp (turbo 3) disables the live ARGB renderer
  (`agents/control-port.md` § turbo), so there are no real pixels to store; the
  ring pauses and `frame-ring-info` reports `live=0`. Coop play is turbo 1, which
  is the target workflow, so this is not a practical limit.

### Ring B — VIC derived-state ring (the "why" black box)

A per-line (or per-line-of-interest) ring of VIC **internal** state that CPU
writes cannot reveal. Fields (superset of `c64_vicii_hardware_snapshot`,
`src/machine/c64.h:253`):

- `frame_number`, `raster_line`, `machine_cycle`
- sprite enable / x / y / **latched msb at display time** / x-expand / y-expand /
  priority / multicolor
- which sprite(s) actually DMA'd this line, sprite pointer bytes
- `badline`, display-vs-idle, `d011`/`d016`/`mem`, IRQ status/enable
- border/bg colors as latched

The value over Ring A: pixels say *that* a frame is wrong; Ring B says *why* —
e.g. "on line 130, sprite 0's latched XMSB was 0 while the shadow `$D010` bit 0
was 1" is the entire one-frame-left bug, and it is invisible to both the pixel
ring and the CPU recorder.

Sampling granularity is configurable: `all` lines (full fidelity, ~3 MB/3 s) or
`sprite-active` lines only (cheaper). Default `all` — the budget is trivial.

## Sizing (the "gobs of RAM" check)

| Ring | Per unit | 3 s real-time (150 PAL frames) | 10 s |
|------|----------|-------------------------------|------|
| A framebuffer (`indexed8`, 504×312) | 157,248 B/frame | ~23 MB | ~77 MB |
| B VIC state (`all` lines, ~64 B/line × 312) | ~20 KB/frame | ~3 MB | ~10 MB |

Both are governed by a configurable byte budget (like the recorder's 256 MiB
budget) with a documented default (proposal: A = 64 MiB ≈ 8 s, B = 16 MiB). The
ring drops oldest-first when full and reports `dropped=` so a scrub knows if the
window undershot the glitch.

## Cost (perf gate)

Ring A: one reduce+copy of an already-produced frame snapshot, at frame rate
(50/s at turbo 1). Negligible. At turbo 2 frames complete faster but the copy is
still O(frame) and dwarfed by the frame's own composition cost.

Ring B: per-line, so it is on a hotter path than Ring A. It must be a plain field
capture into a preallocated ring slot — **no allocation, formatting, or locking on
the per-line path**, same discipline as the flight recorder hot path. **Gate:
turbo-2 throughput loss ≤ the recorder's own budget (target ≤5%, hard ceiling
10%) with both rings enabled**, measured against `agents/perf-baseline-turbo2.md`.
If Ring B `all` cannot meet the gate, ship `sprite-active` as the default and
`all` as opt-in.

## Wire protocol

Mirror the flight-recorder command shape (`history-*`). New commands:

```text
frame-ring-info
    -> ok depthA=<n> oldestA=<frame> newestA=<frame> droppedA=<n>
          depthB=<n> oldestB=<frame> newestB=<frame> droppedB=<n>
          live=0|1 budgetA=<bytes> budgetB=<bytes>

frame-ring-record <on|off>          # default on; symmetric with history-record
frame-ring-clear

get-frame-at <frame|cycle> [format=indexed8|argb8888]
    -> data frame (nearest slot with frame_number/machine_cycle <= target),
       same metadata shape as get-frame plus target= and actual= keys

vic-ring-find [frame=<n>] [raster=<a>[-<b>]] [limit=1..312]
    -> data vic-ring (counted records, Ring B slots for that frame/line window)
```

`get-frame-at` and `vic-ring-find` require a **paused** machine (like
`history-find`) and otherwise return `busy machine-running`. Payload ownership is
token-keyed and released on claim/timeout/disconnect, reusing the bulk-memory RPC
pool pattern (never a fat event-queue union). **Protocol bump to C64M/4** (shared
with the guarded-breakpoints change if they land together; otherwise C64M/5).
Add capability tokens `frame-ring vic-ring` to the `capabilities` string.

State-load and reset clear both rings (like the recorder — history is never
serialized into a snapshot).

## coop_watch.py integration

- On freeze, `frame-ring-info` into the snap header (window coverage + `dropped=`
  so the pack states whether the glitch is even in range).
- New inbox verbs:
  - `scrub [count]` — walk Ring A backward from newest, writing each
    `indexed8` frame to `build/debug/snap-NNN/frame-<f>.idx8` (+ a tiny PNG via
    the palette) so the human can eyeball which frame is the bad one.
  - `frame <f>` — pull `get-frame-at <f>` and `vic-ring-find frame=<f>` into the
    pack, and pull flight-recorder records for that frame's cycle span. This is
    the one-shot "assemble everything about frame f" convenience (Tier 3 glue).

No game change; pure control-port orchestration, consistent with coop's charter.

## Tests (test-first)

1. Ring A: after N completed frames, `frame-ring-info` reports correct
   depth/oldest/newest; `get-frame-at <newest>` returns that frame; `get-frame-at`
   a cycle between two frames returns the nearest `<=` slot with `actual=`.
2. Ring A wrap: exceed budget → oldest dropped, `droppedA` increments, newest
   still exact.
3. Ring A warp: turbo 3 → `live=0`, ring does not advance; back to turbo 1/2 →
   resumes.
4. Ring B: per-line records for a known frame have expected raster coverage;
   `vic-ring-find raster=a-b` returns only that window.
5. Ring B captures **latched** msb distinct from shadow `$D010` on a crafted
   frame that writes `$D010` mid-line (the one-frame-left repro pin).
6. Cross-index: a `frame#` maps to a cycle span that the flight recorder can
   query (integration test, paused).
7. Wire: `busy machine-running` when not paused; token-keyed payload release on
   disconnect; `hello` reports the bumped protocol.

## Acceptance checklist

- [ ] `ctest --test-dir build` green (baseline + new tests).
- [ ] Perf gate met with both rings enabled (turbo-2, vs perf baseline).
- [ ] Default budgets documented and configurable; `dropped=` surfaced.
- [ ] `agents/control-port.md` updated: new commands, C64M bump, capability
      tokens, paused-only rule, and the § Oracle/automation traps note about
      one-frame aliasing gains a pointer to the frame ring.
- [ ] `agents/README.md` baseline count / protocol version updated.
- [ ] `coop_watch.py` `scrub`/`frame` verbs documented in its module docstring.

## Non-goals

- Not reverse execution or state restore — the ring is read-only forensic pixels
  and VIC state, not a time machine (that stays a separate checkpoint/replay
  feature, per the recorder spec).
- Not audio/SID history (separate ring if ever needed).
- Not drive-VIC (there is none) or drive-CPU frames.
