# C64VICPHASE_H.md
# VIC-II Phase H — Cycle Steal Integration (BA Model)

## Status

Phase F (Light Pen) was skipped. Phase G (Open Bus / Last-Byte Behavior) is complete.
This document picks up Phase H, the last item in the original VIC-II phase plan.

## Resolved Team Decisions

These decisions override any broader wording in `C64MVICII.md`:

- **BA only**: AEC remains a hardware explanation / documentation concept. Do not add
  emulator-visible AEC state, an AEC predicate, an AEC snapshot field, or AEC tests.
  CPU stalling is driven only by the unified BA predicate.
- **Reuse Phase D sprite-active logic**: sprite BA windows must use the same per-sprite
  DMA-active/enabled determination already used by the Phase D sprite fetch path. Do
  not create a second Y-range/enable computation just for BA timing.

## Required Reading Order

```text
1. AGENTS.md
2. C64MVICII.md (Phase H section, plus "Notes for Phase Document Authors")
3. STATUS.md
```

Do not read this document in isolation. Phase H is the highest-risk phase in the
VIC-II plan because it changes how the CPU is stalled, not just what a register
returns. Re-read AGENTS.md's Thread Ownership and Snapshot Rule sections before
touching any code — this phase sits exactly on the machine/runtime boundary those
rules exist to protect.

## Goal

Connect the VIC-II's BA (Bus Available) signal to actual CPU cycle stalling so that
Bad Line c-accesses and sprite p-access/s-access fetch windows correctly halt the
6510 on read cycles. This closes the two gaps STATUS.md currently lists under "Not
Implemented": sprite-fetch BA events, and exact BA timing for sprite DMA windows.

This phase does **not** start from zero. Per STATUS.md, Bad Line BA handling already
routes through CPU event read/write classification (read cycles stall while BA is
low, write-only cycles continue, unknown/internal cycles remain conservatively
stalled). Phase H extends that existing mechanism to also cover sprite fetches; it
does not replace it.

## Current State (What Already Exists)

Confirm all of the following are true before writing new code. If any of these has
drifted since STATUS.md was last updated, stop and reconcile with the team before
proceeding — Phase H's design below assumes this foundation is accurate:

- A monotonic master cycle exists on the machine, and VIC-II/CIA/SID hooks are
  advanced to timestamped CPU bus event cycles before CPU-visible side effects apply
  (Phase 16 work).
- CPU bus-visible reads and writes are classified and timestamped per opcode; this
  classification already distinguishes read cycles from write-only cycles.
- Bad Line BA is asserted at cycle 12 and released after the 40-cycle c-access window
  (cycles 15–54), and this BA state already gates the CPU through the existing
  read/write classification: reads stall while BA is low, writes continue, unknown
  cycles stall conservatively.
- Sprite p-access (pointer fetch) and s-access (3 bytes/line/sprite) already exist and
  are currently materialized by `vicii_fetch_sprites()` once per raster line (at cycle
  0, per Phase D), but these fetches currently have no BA/stall effect on the CPU -
  they are "free" in the current model. Treat that call site as a data-production
  shortcut, not as the timing source for BA. Phase H must model BA from the VIC-II
  sprite fetch schedule, while leaving the existing fetch content and call site
  unchanged.
- Per-sprite DMA-active state (whether a sprite is currently being fetched/displayed
  on the current raster line, based on Y-range and enable) already exists as part of
  the Phase D sprite fetch logic.

## In Scope

1. **Sprite BA windows**: compute, per raster line, which cycle ranges have BA held
   low for sprite fetches, and feed those ranges into the same stall mechanism Bad
   Line BA already uses. The cycle schedule must come from the VIC-II sprite
   p-access/s-access timing table already used or documented for Phase D / Bauer,
   not from the current `vicii_fetch_sprites()` batch call position.
2. **Reuse of existing sprite-active logic**: the determination of which sprites are
   DMA-active on the current line (and therefore contribute a BA window) must reuse
   or expose the existing Phase D active-sprite check — not a new, parallel
   computation. If that check is currently private/inline within
   `vicii_fetch_sprites()` or equivalent, factor out the minimum needed (e.g. a
   small query function or an already-computed per-sprite active flag) rather than
   re-deriving Y-range/enable logic a second time.
3. **Unified BA predicate**: BA must end up as a single per-cycle answer (low or not)
   that accounts for both Bad Line and sprite causes simultaneously — a cycle can be
   held low by either or both. The existing read/write classification consumes this
   single predicate; Phase H must not introduce a second, separate stall path that
   the CPU has to consult independently.
4. **3-cycle lead time**: BA must go low 3 cycles before the corresponding VIC bus
   takeover (sprite fetch or c-access), exactly as already implemented for Bad Lines.
   Apply the same lead-time rule to sprite fetch windows.

## Out of Scope (Do Not Implement)

- AEC as emulator-visible state. Per team decision, AEC remains a documentation/
  mental-model concept only (it explains *why* BA stalls the CPU, per Bauer) and is
  not modeled as a separate predicate, field, or signal anywhere in code. BA is the
  sole mechanism that gates the CPU in this implementation. Do not add a
  `vicii_aec_active()` function or equivalent.
- Any change to *what* data is fetched during sprite p-access/s-access, or *when*
  `vicii_fetch_sprites()` is called. Phase H only adds stalling around fetches that
  already happen; it does not change fetch content, ordering, or the per-line call
  site established in Phase D.
- Any change to Bad Line detection, the c-access window, or VC/VCBASE/RC bookkeeping.
  Those are already correct per Phase A/16 and must not be touched.
- Idle-state g-access fetch modeling (`$3FFF`/`$39FF`) — still separately listed as
  not implemented in STATUS.md and out of scope here; do not fold it into this phase.
- Open bus / last-byte-on-bus behavior — that is Phase G, already complete.
- Any frontend or debugger UI change. This phase is purely a machine/runtime-internal
  timing correction; nothing here should require new copied snapshot fields unless a
  debugging need is explicitly identified during implementation (see "Debugger
  Visibility" note below).

## Design

### Timing source of truth

Do not infer sprite BA timing from the current `vicii_fetch_sprites()` call being made
once per line at cycle 0. That call may remain where it is for data availability, but
BA must be evaluated against the actual VIC-II sprite bus-takeover schedule.

The implementation must encode the sprite fetch timing explicitly using the following
PAL 6569 schedule (Bauer). 1-based VIC cycle numbers; 0-based absolute line index
= cycle_number − 1; line length = 63.

```
  sprite 0: p-access cycle 58, steal [57, 59), BA [54, 59)
  sprite 1: p-access cycle 60, steal [59, 61), BA [56, 61)
  sprite 2: p-access cycle 62, steal [61, 63), BA [58, 63)
  sprite 3: p-access cycle  1, steal [ 0,  2), BA [-3,  2)  ← BA starts prev-line cycle 60
  sprite 4: p-access cycle  3, steal [ 2,  4), BA [-1,  4)  ← BA starts prev-line cycle 62
  sprite 5: p-access cycle  5, steal [ 4,  6), BA [ 1,  6)
  sprite 6: p-access cycle  7, steal [ 6,  8), BA [ 3,  8)
  sprite 7: p-access cycle  9, steal [ 8, 10), BA [ 5, 10)
```

Formula (all sprites): `ba_start = p_cycle_0based − 3`, `ba_end = p_cycle_0based + 2`
(exclusive). Window width is always 5 cycles.

NTSC must be implemented through a per-standard table (cycles_per_line,
sprite_p_access_cycle[8], sprite_ba_start_cycle[8]). Do not guess NTSC values from
PAL offsets. If the project currently only targets PAL, implement the PAL table and
leave NTSC as an explicit TODO/unsupported timing variant.

Tests must refer to these explicit constants/table entries (not inline magic numbers)
so failures are diagnosable as timing errors rather than undocumented expectations.

Cross-line BA windows (sprites 3 and 4) require absolute-cycle representation — see
"BA as a single per-cycle predicate" below. Tests must cover the sprite 2→3 and
sprite 4 boundary cases because those catch premature BA clearing at line wrap.

### BA as a single per-cycle predicate

The target shape is: for the current raster line and X-cycle position, there is one
function (extending, not duplicating, the existing Bad Line BA logic) that answers
"is BA low right now," accounting for:

- Bad Line c-access window (existing, unchanged).
- Sprite fetch windows (new): for each enabled, DMA-active sprite, the pointer fetch
  plus the sprite data fetches use the PAL timing table above. BA goes low 3 cycles
  before the first CPU-phase bus takeover (i.e., at `p_cycle_0based − 3`) and
  remains low for 5 cycles (`ba_end = p_cycle_0based + 2`, exclusive).

BA windows for sprites 3 and 4 start during the *previous* raster line (at 0-based
line cycles 60 and 62 respectively). These cross-line windows must be represented
with absolute cycles, not line-relative cycle numbers:

```c
uint64_t ba_low_until_abs;         /* bad-line BA: abs cycle at which window expires */
uint64_t sprite_ba_low_until_abs;  /* sprite BA:   abs cycle at which window expires */
```

`vicii_ba_active(v, abs_cycle)` returns true when:
  `abs_cycle < ba_low_until_abs || abs_cycle < sprite_ba_low_until_abs`

The sprite BA window for each sprite is asserted at its specific BA-start cycle
(not all at cycle 0), and `sprite_ba_low_until_abs` is updated by taking the
maximum of its current value and the new window end. This naturally handles
overlapping windows within the early group (sprites 3–7) and the late group
(sprites 0–2), while preserving the gap between those groups without
over-asserting BA.

Multiple simultaneous causes (e.g. a Bad Line and an active sprite both wanting the
bus) must still resolve to a single low/not-low answer per cycle — BA is a wired-OR
of all VIC bus-takeover reasons, not a per-cause stack the CPU has to reason about.

### Reusing Phase D sprite-active state

Per team decision, do not recompute sprite DMA-active state from scratch. If Phase D's
`vicii_fetch_sprites()` (or equivalent) already determines, per sprite, whether it is
enabled and within its active Y-range for the current line, expose that determination
(e.g. as a small per-sprite boolean array/query already computed once per line) and
consume it when building the sprite BA windows. Avoid a second pass that re-checks
`$D015` enable bits and Y-coordinates independently of the existing fetch logic — if
the two checks ever disagree, that is a bug, not an acceptable second source of truth.

### Read-vs-write classification: no change needed

Per STATUS.md, the CPU read/write classification that consumes BA already exists and
already handles: reads stall while BA is low, write-only cycles continue, unknown/
internal cycles remain conservatively stalled. Phase H does not change this
consumption logic — it only changes what feeds into the BA predicate the consumer
already reads from. Do not modify the stall-application code path unless a genuine
defect is found there during implementation; if so, treat that as a separate
unplanned bugfix, note it explicitly in the PR/commit, and confirm it doesn't regress
Bad Line behavior.

### Adjacent sprite windows and apparent extra stalls

Do not assume each active sprite contributes an isolated, independently countable
stall block. Adjacent sprite fetch windows can keep BA low across what would otherwise
look like a gap, and disabling a later sprite may not immediately return cycles if an
earlier active sprite's lead time or the next active sprite's lead time keeps BA low.
The acceptance tests should check the resulting union of BA-low cycles, not a naive
sum of per-sprite constants.

### Debugger Visibility (only if needed)

If, during implementation or testing, it becomes useful to see sprite-fetch BA state
in the debugger (e.g. to visually confirm raster-bar stalls line up with sprite
fetches), that data must go through the existing copied-snapshot publication path
(per AGENTS.md Snapshot Rule) — no live BA/sprite-active pointers may be read by the
frontend directly. This is optional and should only be added if it materially helps
verify acceptance criteria; do not add new debugger UI surface as a goal in itself.

## Files Likely Touched

- The VIC-II BA computation (wherever Bad Line BA assertion/release at cycles 12 and
  15–54 currently lives) — extended to also account for sprite fetch windows.
- The Phase D sprite fetch logic (`vicii_fetch_sprites()` or equivalent) — only to the
  extent needed to expose existing per-sprite active/enabled state for reuse by the
  BA computation; the fetch behavior itself is unchanged.
- Existing CPU event read/write classification call site, only if it needs to query
  the now-extended BA predicate at additional cycle offsets within a line (no change
  to the classification logic itself). Prefer changing the BA predicate provider over
  changing the CPU stall consumer.
- Regression test file(s) covering Bad Line BA stalling — extended with new tests for
  sprite-fetch BA stalling and combined Bad Line + sprite scenarios.

Do not touch: rendering (`vicii_live_pixel`, `vicii_make_frame_snapshot`), graphics
mode dispatch, sprite pixel decoding/compositing, collision/priority logic ($D01B/
$D01E/$D01F), open-bus masking (Phase G), or anything in `runtime`/`frontend` beyond
the optional debugger visibility noted above.

## Required Test Matrix

Add or extend regression tests to cover at least these cases:

1. **Bad Line baseline**: no active sprites; existing Bad Line read-cycle stall count
   remains unchanged.
2. **Single active sprite**: one sprite active on a non-Bad-Line raster; read cycles
   stall only for the documented sprite BA window, with the 3-cycle lead time.
3. **Multiple adjacent sprites**: several active sprites on the same raster; expected
   stalls are the union of their BA-low windows, not a duplicated second stall path.
4. **All 8 sprites active**: worst-case sprite DMA line; stall count matches the
   documented sprite schedule.
5. **Inactive sprites**: disabled sprites and sprites outside the existing Phase D
   DMA-active Y range contribute no sprite BA window.
6. **Bad Line plus sprites**: overlapping/adjacent Bad Line and sprite windows produce
   one unified BA-low answer with no gap and no double counting.
6a. **Sprite 2→3 boundary** (cross-line): sprite 2's window ends at cycle 63 of line N;
    sprite 3's BA starts at cycle 60 of line N for a fetch on line N+1. Verify that BA
    is correctly asserted across the line boundary and not prematurely cleared.
6b. **Sprite 4 boundary** (cross-line): sprite 4's BA starts at cycle 62 of the previous
    line. Verify that the two-cycle lead into the next line is correctly asserted and
    that `sprite_ba_low_until_abs` extends past the line wrap.
7. **Write-cycle escape**: a write-only CPU cycle proceeds while BA is low for a
   sprite-caused stall, matching the existing Bad Line behavior.
8. **AEC absence**: a codebase grep or equivalent test/check confirms no AEC-named
   emulator state, predicate, field, or snapshot surface was added.

## Acceptance Criteria

- Bad Line stall cycles continue to match the theoretical 40-cycle steal per Bad Line
  (no regression from current behavior).
- A sprite fetch window stalls CPU read cycles for its expected duration, with BA
  going low exactly 3 cycles before the fetch window begins, matching the same
  lead-time rule already used for Bad Lines.
- When a Bad Line and one or more active sprites occupy overlapping or adjacent cycle
  ranges on the same line, the CPU is stalled for the union of both windows — no gap
  where BA should be low but isn't, and no double-counted/duplicated stall logic.
- Write-only cycles continue to proceed even while BA is low, for both Bad Line and
  sprite-caused stalls — this existing behavior must not regress.
- A cycle-counted raster routine (raster bar test program) produces a stable raster
  bar at the correct screen position when sprites are active, where it would
  previously have drifted due to missing sprite-fetch stalls.
- Sprite-fetch stall cycle counts match expected counts for 1, several, and all 8
  simultaneously active sprites on one line.
- Disabled or off-screen (inactive) sprites contribute no BA window, consistent with
  the existing Phase D active/enabled determination being reused rather than
  reimplemented.
- All existing boot, keyboard, and prior VIC-II phase (A–E, G) regression tests
  continue to pass unmodified — stalls introduced here must not break existing
  execution paths.
- No AEC-named state, field, or function is introduced anywhere in the codebase.

## Definition of Done

- Sprite BA windows are computed from the documented VIC-II sprite fetch schedule
  and reuse the existing Phase D sprite-active determination, not a parallel
  enable/Y-range computation.
- BA is a single unified predicate consumed by the existing, unmodified read/write
  stall-classification logic.
- All acceptance criteria above pass; all pre-existing tests continue to pass.
- STATUS.md is updated to record:
  - Phase H complete: sprite-fetch BA events now stall the CPU using the same
    mechanism as Bad Line BA.
  - The "Not Implemented" entries for "sprite fetch BA events are not implemented"
    and the "exact BA/AEC/RDY cycle stealing" caveat are revised to reflect that BA
    now covers both Bad Line and sprite causes, while explicitly noting AEC remains
    an unmodeled, documentation-only concept by design (not a remaining gap to track).
  - Whether any debugger visibility was added for BA/sprite-fetch state, and if so,
    confirmation it was published only through the existing copied-snapshot path.
