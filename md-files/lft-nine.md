# lft-nine investigation

Date: 2026-07-11 (original diagnosis); updated through Session 11, 2026-07-13.

> ## START HERE (current state, 2026-07-13)
>
> **Latest finding: jump to "### Session 11" below.** Sprite crunch now works:
> the flanking-sprite DMA lifetime matches VICE (active R9-R73, off at R74) and
> the six-write `$D021/$D011` kernel runs stably every frame. Root cause was an
> off-by-one against VICE's 0-based `raster_cycle` (`VICII_PAL_CYCLE(n) = n-1`):
> the `$D017` crunch bit-magic checked Bauer cycle 15 instead of index 14, so
> lft-nine's `$D017<-$00` at C14 never mangled MC. Session 9's "VICE has no DMA
> at R8-R48" claim is **retracted** (wrong SPRDMA frame alignment).
>
> **Still open for visual 1:1:** the six-write kernel still starts ~11 lines
> later than VICE (c64m R36 vs VICE R25) and free-sprite Y values differ
> (animation/multiplex phase). Per-frame VIC writes ~159 vs VICE ~202. Align on
> register sequence / sprite Y, not raw frame numbers.
>
> **Tooling (committed):** `tools/c64_control_client.py` (control-port client)
> and `tools/dis6502.py` (6502 disassembler) drove the whole investigation; the
> trace build is `cmake -B build-trace -DCMAKE_C_FLAGS=-DC64M_VIC_TRACE`.
> **Capture lft-nine at `--turbo<=7`** or the live renderer is off and `get-frame`
> returns a misleading geometric debug frame (see Session 6).
>
> Earlier sessions below are kept for the full trail. Superseded leads: the
> "$61/$9B75 wait-path" as a defect (Session 6, frame-misalignment artifact)
> and Session 9's "VICE has no top-region DMA" (frame-misaligned SPRDMA).

> **See "## Implementation progress (2026-07-12)" below** for what has since been
> built (side-border flip-flop, dot-anchored render mapping, idle graphics in the
> opened border) and the remaining known shortcomings measured against VICE. The
> executable plan those steps followed is `md-files/C64MVICII_SIDEBORDER.md`.

## Result

The primary missing piece is horizontal-border timing/state in the VIC-II
renderer. c64m has a live vertical-border flip-flop, but the side border is
still a fixed geometric mask. Consequently a sprite that is correctly fetched
and positioned in the side border is always hidden by the border colour.

This is independent of the already implemented PAL/NTSC sprite BA tables. The
demo is therefore a useful next target for horizontal-border work, but it is
not yet evidence that the current sprite BA table is wrong.

## Implementation progress (2026-07-12)

The horizontal side-border opening was implemented per
`md-files/C64MVICII_SIDEBORDER.md`. The mechanism now works; the *content* shown
in the opened regions and the bottom-border behaviour are still wrong. A side-by-
side capture of VICE (x64sc PAL) vs c64m was taken at a settled frame.

### What has been achieved (works)

- **Dot-anchored render mapping (Step A, committed `116cde6`).** The live
  renderer maps a VIC cycle to output dots with `X = 24 + (cycle-15)*8` so
  `buffer_x == VIC X-coordinate`, replacing the old scaled
  `cycle*width/cycles_per_line`. A timed `$D016` write now lands on the correct
  border-compare column. Two pre-existing pixel tests were re-baselined to the
  new dot-exact timing (`test_expose_harness_midline_injection_hits_exact_column`,
  the D020 border test), plus one runtime frame test
  (`test_frame_while_running`).
- **Main (horizontal) border flip-flop (Steps B/C, committed `813ec51`).** New
  `vicii.main_border_ff`, evaluated per dot with the CSEL live for that cycle
  (Bauer 3.9 rules 1 & 6: set at the right compare, reset at the left compare
  when the vertical border is clear). Persists across cycles/lines/frames. A
  timed `$D016` CSEL 1->0 in the open window (c64m cycle 54) opens the right side
  border; the flip-flop staying clear also opens the next line's left region.
  Tests: `test_live_right_side_border_opens`,
  `test_live_side_border_wrong_cycle_stays_closed`,
  `test_live_side_border_flip_flop_persists_left`,
  `test_live_side_border_reveals_sprite`.
- **Sprites reveal in the opened side border.** Sprites now compose over the
  opened region instead of being masked. Confirmed in the VICE-vs-c64m capture:
  the numbered sprites do appear at the left/right edges (previously they were
  solid border).
- **Idle/ghost-byte graphics in the opened border (Step D).** The over-border
  region renders the idle `$3FFF`/`$39FF` byte instead of a flat colour. Test:
  `test_live_side_border_shows_ghost_byte`.
- **Write-path audit (Step E).** `$D016` needs no special handling: it is an
  ordinary register store, `vicii_get_border_geometry` reads it live each cycle,
  and `c64.c` applies the CPU write before the VIC renders that cycle
  (`c64_apply_pending_cpu_events_at_elapsed` then `c64_advance_one_cycle`). No
  code change.

All 47 ctest cases pass after a full rebuild.

### Known shortcomings (still wrong vs VICE)

1. **Opened side border shows blue, should be black -- because the black is made
   of SPRITES, not the border/background.** Per the author's own explanation
   (see reference below), "the black borders ... are assigned to Sprites 5 and 7."
   So the black frame is not an opened border rendered black, nor the idle colour:
   it is black sprites drawn in the opened region. c64m showing **blue** there
   means the black border sprites are missing / mis-timed at those positions, so
   the idle "ghostbyte shine-through" background (`b0c` = the blue playfield)
   shows instead. In the real demo that shine-through is *mostly covered* by the
   black sprites and only peeks through the gaps between digit sprites as thin
   stripes. **This is a sprite-multiplex/timing problem (items 4-6), not an
   idle-colour bug** -- an earlier version of this note wrongly chased the idle
   colour resolution. The Step D idle graphics are correct as the *substrate*; the
   fix is getting the border sprites to render over it.
2. **Bottom border clips sprites; should open to black with sprites.** VICE shows
   the lower numbered sprites (4/5/6) over a **black** bottom border. c64m cuts
   them off under the bottom border -- the vertical border opening this demo uses
   is not happening (or the sprites are clipped by the crop / vertical FF). Note
   the existing `test_live_*_bottom_border_can_be_opened_for_sprites` pass, so the
   generic bottom-open path works; this demo's specific top/bottom technique (the
   `$D011` chain in "## Demo timing observations" and item 5) is not yet
   reproduced. Investigate alongside item 5 (late top-border sprite multiplex).
3. **XSCROLL ghost-byte phase not applied.** `vicii_idle_pixel` ignores XSCROLL,
   matching the existing vertical-border idle approximation. lft-nine uses XSCROLL
   to align the ghost pattern with the sprites, so the ghost pattern phase will be
   off even once its colour (item 1) is fixed.
4. **Sprite vertical positions/multiplex differ.** The numbered sprites sit at
   different heights in the two captures, consistent with the unresolved late
   multiplex/timing failure (item 5) and the AEC/RDY sub-cycle write phase
   (item 6). Neither was touched by this work.

### Authoritative reference: the author's explanation

Linus Akesson explains the demo at
<https://www.linusakesson.net/scene/nine/explanation.php>. Key facts that reframe
the remaining c64m work (all are the demo's design, confirmed by the author):

- **The black frame is sprites.** Wizard = Sprites 1 & 3; **black borders =
  Sprites 5 & 7**; the three freely-moving digits = Sprites 2, 4, 6. All nine
  digits are **multiplexed** across the eight hardware sprites, with the
  front-to-back priority mapping constantly re-assigned (lower sprite number =
  in front).
- **Stable timing via flanking sprites.** Freely-moving sprites are flanked by
  **four always-on sprites (0, 2, 4, 6)** so the BA/sprite-fetch pattern stays
  fixed. PAL line = 63 cycles, **16 at risk** of sprite fetches, BA asserted **3
  cycles ahead** (CPU may still write, halts on read) -- exactly c64m's existing
  BA model.
- **Invalid mode `$70`, not plain ECM `$50`.** "Mode $70 behaves like ECM as far
  as the ghost byte is concerned." c64m's idle path keys the ghost address off the
  ECM bit ($39FF), which `$70` sets, so the address is right; the invalid-mode
  colour resolution in the idle/over-border path should be checked against this.
- **Cycle-exact `$D011` + `$D021` kernel.** `sta $d011` at **cycle 15**, then
  `sta $d021-$70,x` at **cycle 21**; there are **three variants one cycle apart**
  to cover VIC-chip version and XSCROLL position (a one-cycle pixel-pipeline
  delay). The `$D021` **background splits** paint parts of the black frame too.
- **`$D018` bank switching** updates all four sprite pointers with a single
  register write (pointers live in the char-graphics region of the VIC bank).
- **XSCROLL alignment.** The colour change is "delayed by one pixel to line up
  with horizontal scroll position 1"; at position 0 the delayed change and a grey
  stripe land just right of the covered gap. This is the ghost-byte phase detail
  (item 3).

### Suggested next steps for a future agent

Reprioritised after reading the author's explanation:

1. **Sprite multiplex + cycle-exact raster kernel is now the main event** (items
   4-6), not an idle-colour tweak. The visible blue-vs-black and the clipped
   bottom sprites both come from border sprites (5/7 and the multiplexed digits)
   not being rendered at the right raster positions. Get the multiplex kernel and
   BA/`$D018`/`$D011`/`$D021` cycle timing right; the black frame follows.
2. Verify the invalid-mode `$70` idle/over-border colour resolution against VICE
   for the gaps where the ghost-byte shine-through actually shows (thin stripes
   between digit sprites), once the covering sprites render.
3. XSCROLL ghost-byte phase (item 3) last -- a one-pixel alignment detail.

The Steps A-D border-opening substrate (main-border flip-flop, dot-anchored
mapping, idle over-border graphics) is a prerequisite and appears correct; it is
not the remaining blocker.

## Root cause found via VICE write-stream diff (2026-07-12, session 2)

Using the VICE-oracle method (`md-files/vice-oracle-tracing.md`) a bidirectional
VIC-register write trace was built in **both** emulators and diffed at the
multiplex phase (~frame 6000, ~1m49s after RUN, reached fast with `--turbo=200`;
note `RUNTIME_TURBO_MAX_MULTIPLIER=256`).

Instrumentation (env-gated at runtime, **compile-gated behind `C64M_VIC_TRACE`**
so it is inert and absent from normal builds; committed as the oracle harness):
- c64m `vicii_write_register` logs `F<frame> R<raster> C<cycle> r<reg> v<val>`
  via `C64M_VICLOG` / `C64M_VICLOG_F0`/`F1`/`_EXIT`.
- c64m `c64.c` logs per-raster CPU budget `F R exec=<n> stall=<n>` via
  `C64M_BALOG` (reuses the same frame-window env vars).
- VICE `src/viciisc/vicii-mem.c` `vicii_store` mirrors the write log via
  `VICE_VICLOG` / `VICE_VICLOG_F0`/`F1`/`_EXIT` (frame counter derived from
  raster wraps).

### The measured divergence

- VICE emits a stable **202-204 VIC writes/frame**; c64m only **134**. Sprite
  setup, IRQ acks (10), and `$D012` writes (5) match exactly; the deficit is
  entirely in the timed kernel (`$D021` 86 vs 30, `$D011` 49 vs 21).
- Per raster line in the top "device" region: **VICE runs the 6-write kernel on
  every line R24-R44** (`$D021=07 @C16, $D011=18 @C23, $D021=18 @C30,
  $D021=02 @C34, $D021=70 @C38, $D011=70 @C42`). **c64m runs only `$D018` once
  per line R24-R37**, then crams a desynced, cycle-walking burst into R38-R43.
  The kernel *exit* (~R44/R50) re-aligns because it is raster-IRQ triggered.
- The c64m per-raster CPU budget is decisive:
  `R20-R30 exec=48 stall=15` (sprites stealing) then **`R31-R40 exec=63
  stall=0`** -- c64m hands the CPU the entire line because the flanking sprites
  have **stopped stealing DMA cycles** at R31, exactly where the kernel needs
  their theft to stay cycle-locked.

### Root cause: no cycle-accurate sprite crunch (MCBASE) modelling

The demo keeps its four flanking sprites (0,2,4,6) DMA-active across the whole
device by **crunching** them: it toggles `$D017` (Y-expand) `$35`->`$00`
*mid-line* (measured at c64m R11 C30->C54; VICE R11 C38 -> R12 C14) so the toggle
straddles the VIC's real per-cycle sprite-sequencer steps (the cycle 15/16
MCBASE increment + `MCBASE==63` DMA-off check, and the cycle 55/56 Y-expand
flip-flop toggle). This corrupts MCBASE so the sprite never reaches 63 and stays
DMA-active (stretched), preserving the fixed BA/cycle-theft pattern the timed
kernel depends on.

c64m's `vicii_prepare_sprite_line` (`src/machine/vicii.c:401`) collapses **all**
of that -- `sprite_mc` advance, the `sprite_y_exp_ff` toggle, and the
`mc >= 63 -> sprite_active=false` check -- into a **single batch at cycle 0**,
reading `$D017` at that one instant (`vicii.c:435`). A mid-line `$D017` write
therefore has no effect: the flanking sprites deactivate after the normal 21
rows (R31), stop stealing cycles, and the cycle-exact kernel loses sync. This is
the source of both the "blue where it should be black" (the black-frame `$D021`
kernel never runs per-line) and the downstream multiplex/bottom-border errors.

### Fix direction (the authorised core rework)

Model the VIC-II sprite sequencer per cycle instead of batching at cycle 0, at
least for the MCBASE/MC path:
- cycle 15: `MCBASE` increment gated by the Y-expand flip-flop (Bauer 3.8.1);
- cycle 16: second increment + `MCBASE==63` -> clear sprite DMA;
- cycle 55/56: Y-expand flip-flop toggle and DMA-on check (sprite becomes
  active when `$D015` set and `MCBASE` reload conditions hold);
- read `$D017` live at each of those cycles so a mid-line toggle lands correctly.

This must stay guarded by the existing sprite-BA/DMA tests (PAL/NTSC tables) and
the boot/robocop suites so other titles do not regress. Re-verify with the same
VICE write-stream diff: success is the c64m budget showing `stall>0` continuing
through R31-R40 and the 6-write kernel running on every line R24-R44.

### Decision on resume (2026-07-12)

Chosen approach: **Full per-cycle sprite sequencer** (not the targeted-crunch
approximation). The implementation is now in progress in `src/machine/vicii.c`;
the existing renderer row latch remains temporarily as the presentation path
while the DMA/MCBASE state becomes cycle-driven.

### Instrumentation in the tree (committed, compile-gated behind `C64M_VIC_TRACE`)

The two c64m traces are compiled out of normal builds and are additionally
inert at runtime unless their env var is set. To use them, configure a build
with the flag, e.g. `cmake -B build-trace -DCMAKE_C_FLAGS=-DC64M_VIC_TRACE` (or
add it to your existing build's flags), then run the capture commands below.
They are the verification harness, so do not delete them before re-verifying.

- **c64m VIC write log** -- `src/machine/vicii.c`, top of
  `vicii_write_register` (just after `reg = addr & 0x3F`), inside
  `#ifdef C64M_VIC_TRACE`; `#include <stdlib.h>` is present. Env: `C64M_VICLOG`,
  `C64M_VICLOG_F0`, `C64M_VICLOG_F1`, `C64M_VICLOG_EXIT`.
- **c64m per-raster CPU budget** -- `src/machine/c64.c`: `c64_balog_*`
  helpers above `c64_advance_one_cycle` (inside `#ifdef C64M_VIC_TRACE`, with
  no-op macro fallbacks otherwise), with `c64_balog_mark(...)` calls in
  `c64_step_micro_cycle`, `c64_step_host_ba_stall`, and the pending-trace
  advance branch. Env: `C64M_BALOG` (reuses the `C64M_VICLOG_F0/F1/EXIT`
  window). exec+stall sums to 63/line (classification is complete).
- **VICE VIC write log** -- `/Users/swessels/Develop/svm/vice-emu-code/vice/`
  `src/viciisc/vicii-mem.c`, top of `vicii_store` (just after `addr &= 0x3f`).
  Env: `VICE_VICLOG`, `VICE_VICLOG_F0`, `VICE_VICLOG_F1`, `VICE_VICLOG_EXIT`.
  Rebuild per `md-files/vice-oracle-tracing.md` (the doc-gen step fails on
  macOS bash 3.2 *after* `src/x64sc` links -- that is expected; `rm -f
  src/x64sc` first and check the timestamp).

### Exact capture commands (multiplex phase, ~frame 6000)

```sh
# c64m -- VIC writes for one frame (turbo <=256; 300 silently falls back to 1x)
rm -f /tmp/c64m_vic.txt
C64M_VICLOG=/tmp/c64m_vic.txt C64M_VICLOG_F0=6000 C64M_VICLOG_F1=6000 \
  C64M_VICLOG_EXIT=1 timeout 90 ./build/c64m --headless --control-port 17652 \
  --pal -a --turbo=200 -p samples/lft-nine.prg

# c64m -- per-raster CPU budget (same window)
rm -f /tmp/c64m_ba.txt
C64M_BALOG=/tmp/c64m_ba.txt C64M_VICLOG_F0=6000 C64M_VICLOG_F1=6000 \
  C64M_VICLOG_EXIT=1 timeout 90 ./build/c64m --headless --control-port 17652 \
  --pal -a --turbo=200 -p samples/lft-nine.prg

# VICE -- VIC writes (warp; frame counter has a boot-offset vs c64m, so grab a
# window and align by content/structure, not by absolute frame number)
cd /Users/swessels/Develop/svm/vice-emu-code/vice
rm -f /tmp/vice_vic.txt
VICE_VICLOG=/tmp/vice_vic.txt VICE_VICLOG_F0=6000 VICE_VICLOG_F1=6010 \
  VICE_VICLOG_EXIT=1 timeout 90 src/x64sc -directory data -console \
  -sounddev dummy -warp \
  -autostart /Users/swessels/Develop/github/personal/c64m/samples/lft-nine.prg
```

Useful diffs: per-raster write counts
`grep '^F6000 ' f | grep -oE '^F6000 R[0-9]+' | sort ... | uniq -c`; per-line
detail `grep -E '^F6000 R2[4-8] '`; content sequence
`grep '^F6000 ' f | grep -oE 'r[0-9A-F]{2} v[0-9A-F]{2}'` then `diff`.

### Full-sequencer implementation plan (resume here)

Goal: replace the batch-at-cycle-0 sprite DMA/MCBASE evaluation in
`vicii_prepare_sprite_line` (`src/machine/vicii.c:401-454`) with per-cycle
sprite-sequencer steps so a mid-line `$D017` write lands correctly. Reference:
Bauer "The MOS 6567/6569 video controller" sections 3.8.1-3.8.2. Per-sprite
state today: `sprite_mc[n]` (=MC), `sprite_active[n]` (=DMA), `sprite_y_exp_ff[n]`
(=expansion flip-flop), `sprite_visible[n]`, `sprite_mcbase` does **not** exist
yet -- add `sprite_mcbase[n]` (MC and MCBASE are distinct on hardware).

Cycle rules to implement (PAL cycle numbers; NTSC shares the logic):
1. **Sprite s-accesses** (already scheduled via `vicii_sprite_slot`): each
   active sprite reads 3 bytes across its two cycles; MC increments by 1 per
   byte during the accesses (0x00..0x3F). Keep the existing fetch, but drive MC
   per access rather than `+3` in one shot if needed for exactness.
2. **Cycle 15**: `MCBASE = MC` for every sprite (latch back).
3. **Cycle 16**: for each sprite, if the expansion flip-flop is set, toggle it;
   then if the flip-flop is now clear, `MCBASE += 2` and, immediately after, if
   `MCBASE == 63` (i.e. crossed) advance by 1 more and **turn DMA off**
   (`sprite_active=false`). This cycle-16 `MCBASE==63` check is what the crunch
   defeats. (Follow Bauer exactly -- the +2/+1 and the 63 test order matter.)
4. **Cycle 55 and 58**: expansion flip-flop toggle for expanded sprites
   (cycle 55), and the **DMA-on check** (cycles 55/56): if `$D015` bit set and
   `sprite_y == raster & 0xFF` and DMA off, turn DMA on, `MCBASE=0`, and set the
   flip-flop from `$D017`.
5. **Cycle 58**: `MC = MCBASE` (reload for the display row); DMA-off recheck.
6. Read `$D017`/`$D015` **live** at cycles 16/55/56/58 (do not cache at cycle 0).

Integrate with the existing BA schedule: `vicii_derive_ba_from_schedule` /
`vicii_sprite_dma_current_line`/`_next_line` must consult the new per-cycle DMA
state so BA windows track crunched (still-active) sprites. The renderer's
"pre-latch a full sprite row at cycle 0" shortcut (comment at
`vicii.c:422-426`) will need to move to the real per-cycle fetch or be proven
equivalent under crunch.

Verification (must all hold):
- `ctest` green, esp. the sprite-BA/DMA PAL/NTSC tests, `c64_boot_progression`
  region, and the side-border live tests. (Pre-existing unrelated failures at
  session start: `c64_boot_progression` irq-vector and `c64_robocop_g64`
  gap-table -- confirm they are unchanged, not newly broken.)
- c64m budget capture shows **`stall>0` continuing through R31-R40** (sprites
  still stealing), matching VICE.
- c64m VIC-write capture shows the **6-write kernel on every line R24-R44** with
  values `$D021=07/$D011=18/$D021=18/$D021=02/$D021=70/$D011=70`, and the
  per-frame write count rising from 134 toward VICE's ~202.
- Then re-check the settled frame visually (goal = settled visual parity): the
  black frame should render black, not blue.

Optional first step chosen-against this session but still cheap if wanted:
add the same `exec/stall` budget trace to VICE's per-cycle core to pin the exact
target stall pattern per line before coding.

## Implementation progress (2026-07-12, session 3)

- Added per-sprite MCBASE, live `$D017` edge sampling, pending edge delivery at
  the cycle-15/16 and cycle-55/56 sequencer boundaries, and explicit crunched
  DMA state. The state is included in snapshot version 4.
- Preserved the existing pre-latched sprite-row presentation while the bus/BA
  path uses the new sequencer lifetime. This keeps the existing sprite display
  tests stable while isolating the timing change.
- The c64m oracle budget now matches the required timing: at frame 6000,
  raster lines R24-R40 each report `exec=48 stall=15`, rather than the prior
  `exec=63 stall=0` failure.
- Full `ctest` remains 48/49: all VIC-II and runtime tests pass; the sole
  failure is the pre-existing `c1541_media` `stepper_and_head_stop` test.

Remaining validation is the VIC-register write-stream comparison and settled
frame visual comparison against VICE. The renderer's row presentation latch and
the exact per-access MC progression still need to be reconciled before claiming
full visual parity.

### Session 4 handoff (2026-07-12)

Two follow-up commits refined the sequencer without changing the remaining
oracle result:

- `9e36917 Align sprite DMA with VIC raster match` moves DMA activation to the
  raster-Y match while keeping the first visible row on the following line.
- `eba06c4 Model D017 falling edges without forced sprite crunch` removes the
  incorrect forced-MC reset on a live `$D017` falling edge. VICE only toggles
  the expansion flip-flop when the live expand bit is set; treating a falling
  edge as a reset was not hardware-faithful.

Focused `test_c64_vicii`, `test_c64_snapshot`, and
`test_c64_cpu_validation` pass (the last needs an unlimited process stack on
this macOS host because the enlarged `c64_t` test fixture exceeds the default
stack limit). The corrected DMA lifetime gives the intended sprite contention:
c64m reports `exec=48 stall=15` through R24-R42. This is necessary but not
sufficient.

The remaining measured divergence is the raster-kernel entry path:

- VICE performs the six-write `$D021/$D011` kernel on every R24-R44 line.
- c64m instead repeatedly writes `$D018` and polls `$D012` at PC `$9B7C`; at
  R24 it reads `$18` and remains in the wait loop until approximately R42-R44,
  when it finally emits a delayed six-write burst.
- The two emulators poll `$D012` with the same values through R21. VICE leaves
  that path after R21; c64m continues polling. The c64m interrupt-disable flag
  is set during this wait, so this is not an IRQ-acceptance race.
- The self-modifying code block `$9B70-$9BAF` was captured from both emulators
  and is byte-identical, including the `$2D` comparison operand at `$9B7D`.

Do **not** compare identical frame numbers as though they were equivalent demo
states. A zero-page state byte (`$61`) differed in a trial (`$1e` in c64m frame
6000 versus `$0a` in VICE's frame-6000 capture), proving the emulators' frame
counters have a substantial boot/demo-phase offset. A trial around c64m frame
5960 still showed the delayed kernel, but it did not align `$61`; a later
capture window at frame 9500 produced no writes because the current env-gated
write logger exits only when a write occurs beyond its requested frame window.

Next work, in order:

1. Make the oracle capture stop on a semantic state (for example `$61==$0a` at
   the R74 update), rather than a raw frame number, and capture both VIC writes
   and the `$D012` polling sequence there.
2. Trace the producer of `$61` (c64m reports writes from PC `$8D0D`) and the
   control-flow state that selects the `$9B75` wait path. Compare the update
   sequence with VICE at the matched state.
3. Fix that upstream phase/state divergence, then re-run the exact acceptance:
   six writes on each R24-R44 line, approximately 202 VIC writes per frame,
   and settled visual parity. Do not change broad BA release timing or sprite
   composition without new trace evidence.

All temporary c64m and VICE tracing additions from this session were removed;
the c64m worktree was clean before this documentation update.

### Session 5 handoff (2026-07-13): peer review + cleanup of `ca94696..7089137`

The `ca94696..7089137` sprite-sequencer series was peer reviewed and the
following cleanup was applied on top (no behavior change; full `ctest` stays
48/49, only the pre-existing `c1541_media` `stepper_and_head_stop` failure):

- **Dead crunch scaffolding removed.** `eba06c4` ("Model D017 falling edges
  without forced sprite crunch") removed the forced-crunch mechanism but left
  its state behind. `sprite_crunched` was never set true, `sprite_y_expand_prev`
  was write-only, and `sprite_y_expand_pending` was fully dead. All three arrays
  are removed from `struct vicii`, `vicii_reset`, the sequencer, and the
  snapshot. `sprite_mcbase` (still used) is kept. **Snapshot version bumped 4 ->
  5** so intermediate v4 states are rejected cleanly rather than mis-parsed.
- **Oracle instrumentation is now compile-gated behind `C64M_VIC_TRACE`** (it
  was committed in `ca94696` but still called `getenv`/`exit()` from
  `vicii_write_register` in normal builds). It is absent from normal builds and
  still runtime-env-gated when compiled in. The VIC-log exit path now uses
  `exit(0)` (was `_Exit(0)`) to match the budget-log path and flush stdio.
- **Docs corrected for overclaims.** `VICII.md`/`DEFERRED.md` claimed the
  crunched DMA lifetime was modeled and lft-nine "implemented"; they now say the
  crunch is not yet reproduced and the target is in progress. `TESTING.md`
  snapshot version corrected to v5. The "uncommitted" instrumentation labels in
  this file were corrected to "committed, compile-gated".

**Net effect on the actual bug: unchanged.** The removed state carried no
behavior, so the visual divergence documented above is still the open problem.

### Session 6 (2026-07-13): control-port introspection retargets the bug

Investigated live via a new reusable control-port client (see "Control-port
tooling" below). Two findings materially change the plan:

**1. The Session-4 `$61`/`$9B75` lead is a dead end (frame-misalignment
artifact).** Disassembling the depacked demo from a running instance:
- `$8D08: LDA $3D / LSR / STA $61` -> **`$61 = $3D >> 1`**.
- `$9B75: LDY $61 / ... / LDX $D012 / CPX #$2D / BCS exit / STA $D018 / DEY /
  BNE` -> the `$61`-count loop Session 4 saw is a per-line `$D018` multiplex
  loop; its length is `$61`.
- `$3D` is written at `$8B3F` as `$3D = $AE72[$3E] + f($B000[$3B])` -- a
  **computed animation position** indexed by the digit-animation phase counters
  `$3B`/`$3E`, not a raster/timing sample. Measured live it drifts smoothly
  ($3D=$4A..$49, $61=$25..$24 across frames 5593-5605).

Because `$61` is animation state that changes every frame, Session 4's "c64m
`$61`=$1e vs VICE `$0a` at frame 6000" was almost certainly comparing two
**different animation phases** (the known frame-counter offset), not a bug. Do
not resume the `$9B75` wait-path chase as a suspected defect. (Aligning both
emulators on the same `$61` is still a *valid semantic anchor* for a matched
comparison -- just not evidence of a fault by itself.)

**2. CRITICAL METHODOLOGY FINDING: `get-frame` under turbo returns the geometric
debug render, NOT the live output.** Under `--turbo>=8` (`RUNTIME_TURBO_DISPLAY_
THRESHOLD`, `runtime_thread.c`) the live per-cycle ARGB renderer is *disabled*
(`c64_set_video_output_enabled(false)`) and frames are published via
`c64_make_current_frame_snapshot` -- the **snapshot/debug renderer that draws
borders geometrically (closed CSEL border)**, which **masks every sprite in the
border region**. Because lft-nine puts the digits and the black frame in the
opened border, the turbo/debug frame shows only the central wizard on a closed
black border and *no digits* -- a rendering artifact, not the emulator's real
output. **Always capture lft-nine with `--turbo<=7`** (or no turbo) so the live
completed-frame path (`c64_copy_completed_frame`) is used.

An earlier revision of this note claimed the "nine multiplexed digits are
entirely absent" and the border renders black; **that was the turbo/debug-render
artifact and is retracted.** Confirmed against the user's real SDL screenshots
and reproduced headless at `--turbo=7`.

**Real live-render symptoms (turbo<=7, device phase ~frame 5850-6200):**
- The digit sprites (2,4,6 multiplexed to nine) **do render** but the **multiplex
  is unstable frame-to-frame**: in some frames the nine digits spread roughly
  correctly around the four borders; in others they **clump into the top border**
  (all jammed in one row), **tear**, or **duplicate** (e.g. two 8s). Up to ~12
  distinct sprite colours appear on good frames, ~1 on bad ones.
- The **wizard** (sprites 1&3) is sometimes rendered, sometimes **garbled or
  gone** on the very next frame.
- The **black frame border** (sprites 5&7) is frequently **missing** (blue field
  extends to the display edge) -- so the Session-2 "blue-where-black = missing
  border sprites 5/7" theory is back in play; the *debug*-render black border
  that seemed to refute it was the geometric CSEL border, not the sprites.

So the real open bug is an **unstable sprite multiplex / raster kernel**: the
per-scanline sprite repositioning is not landing consistently, so digits pile up
/ tear / duplicate and the border+wizard sprites drop in and out frame to frame.
This is consistent with the original cycle-timing thesis (items 4-6), not a
simple "sprites off" fault.

### Session 7 (2026-07-13): the device kernel runs only every OTHER frame

VIC write-stream capture (build `-DC64M_VIC_TRACE`, `C64M_VICLOG*`, `--turbo=200`
-- turbo does not affect emulation/register writes, only the display path) over
11 consecutive frames in the multiplex window (F5850-5860) shows a **strict,
deterministic alternation**:

| frame parity | VIC writes | raster range | content |
| --- | --- | --- | --- |
| even (5850,5852,...) | **192** | R9-R294  | full device kernel: per-line `$D018` bank-switch (R9-R25) + `$D021`/`$D011` six-write kernel (R27+) + sprite colour/data (R249-294) |
| odd  (5851,5853,...) | **16**  | R249-R261 | only the `$F9` (R249) end-of-frame handler: sets sprite Y (`$D001/03/05/...`), arms `$D012=$09` |

VICE emits ~202 writes **every** frame (session 4). So **c64m executes the
lft-nine device kernel only every other frame.** On the "off" frames the sprites
keep their base Y positions (no per-line multiplex), which is the clumping /
flicker in the rendered output.

**The IRQ chain (measured from `$D012` writes):**
- even frame: `$09`(R9, device kernel) -> arms `$48`(R53) -> `$50`(R73) ->
  `$C8`(R80) -> `$F9`(R200). The even-frame `$F9` handler (R249-294) does the
  sprite colour/data end-work but **does NOT re-arm `$D012`** (no `$09` write in
  even frames), so the compare stays `$F9`.
- odd frame: `$F9` fires **again** at R249 (compare still `$F9`); this handler
  (R249-261) arms `$D012=$09` for the next even frame.

So the `$F9` (R249) raster IRQ fires on **every** frame, but its handler only
arms `$09` on odd frames -- a two-frame cycle where VICE has a one-frame cycle.
The decision therefore lives in the `$F9` handler: it branches on some
frame-parity/state variable (or an extra/late `$F9` IRQ corrupts that state) and
takes the "arm `$09`" branch only every other execution.

**Handler decoded (control-port disasm; IRQ vector `$FFFE`=`$4000` stub, real
chained handlers via `$FFFE/$FFFF` rewrites; `$01`=$35 so I/O in, ROMs out):**

The chained raster-IRQ handlers each rewrite `$FFFE/$FFFF` for the next link. The
`$C8`(R200) handler vectors the `$F9`(R249) IRQ to the shared dispatcher
**`$99BF`**. That dispatcher is the decision point:

```
$99BF: PHA/TXA/PHA/TYA/PHA
$99C4: LDA $CD  / BNE $99CE          ; $CD!=0 -> raster stabiliser path
$99C8: LDA $1D  / CMP #$14 / BCC $99FD
$99CE: ... stabiliser: LDA $D012 / CMP #$FA / BCC (spin) + NOP sled ...
$99FD: LDA #$00 / STA $D015          ; sprites off
$9A02: JSR $1003
$9A05: LDA $CD  / BEQ $9A46          ; *** DECISION ***  $CD==0 -> colour/data path
$9A09: (arm path) set sprite Y=$09, D011=$73, D021/D025=0,
       vector := $9B00 (device kernel), STA $D012=$09, D018=$80, JMP $9AF1
```

So **`$CD` gates whether the device kernel is armed.** Measured live at `$9A05`
across the multiplex window, `$CD` toggles a clean **`0,1,0,1,...` every frame**:
`$CD=1` -> arm `$09` (device runs next frame); `$CD=0` -> colour/data path (no
device). That is exactly the 192/16 alternation.

`$CD` writers: `INC $CD` at **`$904E`** (tail of a sub that also sets sprite Y
from `$48/$49`) and `STA $CD` at `$1C61`. Main-loop sync at **`$931A`**:
`LDA $CD / ORA $CE / ORA $CF / BNE $931A` -- the main thread spins until
`$CD|$CE|$CF == 0` before doing its sprite setup (`STA $D001` ...). So `$CD/$CE/
$CF` are a main-loop <-> raster-IRQ handshake: the IRQ side sets/consumes them,
the main loop waits on them.

**CONFIRMED A c64m BUG (not demo design) -- VICE oracle, 2026-07-13.** VICE
(`x64sc`, `VICE_VICLOG`) emits a rock-steady **202 VIC writes on every frame**
(F5980-F6000: 203,202,202,...,202 with zero alternation) in the same multiplex
window where c64m alternates **192/16**. So the device kernel is meant to run
**every frame**; c64m running it every other frame is the defect.

**Where it's gated in c64m (measured):**
- `INC $CD @ $904E` fires only **every other frame** (frame deltas +2). `$CD` is 0
  before each INC. So the thing that sets `$CD=1` runs half as often as it should.
- `JSR $1003` (right before the `$9A05` `$CD` test) does **not** modify `$CD`.
- The `INC $CD` sub is reached, every time, via the same chain (control-port
  call-stack at `$904E`): the **animation sequencer at `$8600+`**
  (`... $8619: LDA #$6E / JSR $88DA -> $88DA -> $88DC -> $8A1C -> $904E`). The
  `$8600` sequencer is a run of `LDA #imm / JSR $88DA` sprite-positioning calls
  with `$22` bit-toggles (`EOR #$02`/`#$01`). (The call-stack's outer
  `$0909->$8000` frame is a stale/bogus entry -- `$0900` is the exit-to-BASIC
  path, `$093C JMP $A474`.)

**Precise arming divergence (VICE vs c64m, same window):**
- VICE arms **all five** raster compares in **every** frame:
  `R53 v48 / R73 v50 / R80 vC8 / R200 vF9 / R261 v09`. So every VICE frame runs
  the device kernel AND the `$F9` handler arms `$09` for the next frame.
- c64m **device frame** arms `v48/v50/vC8/vF9` but **NOT `v09`** (its `$F9`/`$99BF`
  handler hits `$9A05` with `$CD=0` -> colour/data path). c64m **off frame** arms
  only `v09`. So the two c64m frames together arm what VICE arms in one.
- Both c64m frames are normal 0-311 frames (frame_number increments only at the
  R311->R0 wrap, `vicii.c:1438`); there is **no** frame-boundary/line-count bug.
  Each frame simply passes through all rasters; the writes land where they land.
- `INC $CD @ $904E` runs at **raster ~$22-$25 (34-37)**, i.e. *inside* the device
  kernel (R9-R200) -- so the `$CD`-setting sequencer runs only on device frames.

### Session 7 (cont.): full mechanism -- a 3-flag IRQ producer/consumer that c64m can't complete every frame

Interleaved live breaks (`$904E` INC, `$9A05` CHK, `$9B75` device kernel,
`$9A09` arm-path) reconstruct the exact 2-frame cycle. **Correcting earlier
guesses in this note:** the `$99BF`/`$9A05` check runs at **raster ~2-6 (top of
frame), NOT R249**; and **`STA $CD @ $1C61` NEVER executes** in this phase, so
`INC $CD @ $904E` is `$CD`'s only *direct* writer.

Measured 2-frame cycle:
- **Device frame:** CHK@R~5 reads `$CD=1` -> arm path (`$9A09`) arms `$09`, vector
  `$9B00`; **device kernel runs** (`$9B75` seen at R12+). No `INC`. `$CD` is
  cleared to 0 later in this frame.
- **Non-device frame:** CHK@R~3 reads `$CD=0` -> **colour path** (`$9A46`); then
  **`INC $CD @ $904E`** at R34 sets `$CD=1` (via the `$8600` sprite sequencer).

So c64m runs **either** the device kernel **or** the `$CD`-setting sequencer per
frame, alternating. VICE runs **both every frame** (device kernel + the sprite
sequencer + arms `$09`), which is why VICE displays every frame.

**It's a three-flag state machine.** The `$99BF` dispatcher and colour path branch
on `$CD`, `$CE`, `$CF`: `$CD` gates the device kernel; the colour path
(`$9A46: LDA $CE / BEQ; ... $9A92: LDA $CF / BEQ`) gates two colour-split work
items (it arms handler `$9CBA` at ~R111). The main loop `$931A` spins until
`$CD|$CE|$CF == 0`. These flags are the main-loop <-> IRQ work queue.

**`$CD` clear:** only-`INC` can't produce 0->1->0, so the clear is an
indexed/indirect store or a zero-page memset. Strong candidate: the routine at
**`$8000`** (reached per frame) does `JSR $8017`, and `$8017` runs
`LDA #$00 / LDX #$02 / $801B: STA $00,X / INX / BNE` -- a **zero-page memset of
`$02-$FF` that clears `$CD/$CE/$CF`** -- then sets IRQ vector `$99BF`, `CLI`,
`JMP $8304`. (Unverified: confirm `$8000`/`$8017` runs once per frame and is the
clear.)

**ROOT CAUSE (now firmly localized):** c64m fails to complete all of the demo's
per-frame IRQ work; specifically the device-kernel + sprite-sequencer pair does
not both fit in one c64m frame, so `$CD` (device) is serviced only every other
frame. VICE fits both every frame. This is a **CPU-budget / cycle-timing
shortfall** -- almost certainly the sprite-DMA/BA stall accounting or raster-IRQ
acceptance cycle (items 4-6): c64m spends more stolen/stalled cycles per device
frame than real hardware, pushing the sequencer's work into the next frame.

**BALOG budget measured (device vs off frame), 2026-07-13:**
- Device frame F5850: **total exec=14887, stall=4769** (sum 19656 = 312x63 ok).
- Off frame F5851: total exec=18556, stall=1100.
- Per-raster in the device kernel: R23-R25 `exec=48 stall=15`, then **R26-R44
  `exec=44-45 stall=18-19`**. Session 4 documented `stall=15` through R24-R42 as
  the *intended* VICE-matching sprite contention -- so c64m now shows **~3-4 extra
  stall cycles/line through R26-R44** (~600+ extra stolen cycles over the device
  kernel). That is exactly the kind of over-theft that would push the demo's
  per-frame work (device kernel + colour splits + `$8600` sequencer) past the
  frame budget, so it completes only every other frame.

**LEADING HYPOTHESIS:** c64m **over-stalls the CPU in the six-write-kernel region
(R26-R44)** vs hardware/VICE -- a sprite-DMA/BA cycle-accounting error (items 4-6).
The demo keeps its four flanking sprites (0,2,4,6) DMA-active across the device
via the **sprite crunch** (Session 2's original MCBASE thesis). `eba06c4` removed
the forced-crunch model and the c64m stall in this region appears to have drifted
from `15` to `18-19`; the crunch / MCBASE per-cycle DMA-active accounting is the
prime suspect for the extra theft. (Verify the stall=15 baseline wasn't just a
narrower window in Session 4 before concluding.)

**SMOKING GUN (cycle-position diff, c64m vs VICE, no VICE core mod needed):**
- Six-write kernel *internal* gaps are identical (7,6,4,4,4) -- the timed loop is
  right. But c64m starts each line **~7-9 cycles later** than VICE (VICE flat at
  C21 every line R26-R44; c64m ramps **C25@R27 -> +1/line -> C30@R32**, then holds
  C30). A clean `+1 cycle/line for 5 lines` ramp = **one extra stall cycle per
  line while sprites activate**, then stable -- i.e. c64m over-stalls during the
  sprite-DMA activation ramp.
- The demo does a **sprite crunch**: `$D017 <- $35` at R11 then `$D017 <- $00` at
  R12 (Y-expand toggled on sprites 0,2,4,5 across the sequencer's cycle-15/16
  MCBASE steps to keep them DMA-active). **VICE writes it at a rock-stable
  R11 C38 / R12 C14 every frame; c64m jitters +/-2 cycles (C36 or C38 / C12 or
  C14) frame-to-frame.** The crunch is cycle-exact, so c64m's jittery write timing
  lands the toggle on the wrong sequencer cycle intermittently, corrupting which
  sprites stay DMA-active -> variable stall -> kernel drift -> device kernel only
  completes every other frame.

**ROOT CAUSE (confirmed region):** c64m's sprite-DMA/BA cycle accounting in the
crunch/activation region (R11-R44) diverges from hardware -- extra stall on the
activation ramp AND a +/-2 cycle jitter in when the `$D017` crunch write lands.
Both are the sprite-crunch / per-cycle MCBASE DMA-active modelling (Session 2's
MCBASE thesis; `eba06c4` removed the forced-crunch model). The current
`vicii_step_sprite_sequencer` samples `$D017` live at cycles 15/16/55 but does not
reproduce the crunch's cycle-exact DMA-active outcome, and the surrounding BA
schedule over/under-counts stolen cycles during activation.

**Next work (the fix):**
1. In `vicii.c`, make the sprite-DMA-active state (and thus the derived BA/stall
   schedule) reproduce the crunch exactly: a mid-line `$D017` toggle straddling
   cycles 15/16 must prevent the `MCBASE==63` DMA-off so sprites 0,2,4,5 stay
   active with the hardware cycle pattern. Target: c64m's six-write kernel flat at
   a fixed cycle every line (no +1/line ramp), the `$D017` write cycle-stable, and
   ~202 VIC writes every frame. Guard with the sprite-BA/DMA PAL/NTSC tests +
   boot/robocop suites (no regressions).
2. Re-verify: BALOG `stall` flat across R26-R44, and the multiplex renders stably
   (turbo<=7 `get-frame`, or on your SDL build).
3. (Lower priority) confirm the `$CD` clear is the `$8017` memset via `$8000`.

Caveat: the six-write kernel *values* ($D021=02 VICE vs 05 c64m, etc.) differ,
but that is the known frame-counter phase offset between the two captures -- the
robust signals are the cycle-position drift and the `$D017` jitter, which do not
depend on frame alignment.

VICE recipe: `VICE_VICLOG=/tmp/v.txt VICE_VICLOG_F0=5980 VICE_VICLOG_F1=6000
VICE_VICLOG_EXIT=1 timeout 120 src/x64sc -directory data -console -sounddev dummy
-warp -autostart <prg>` from `/Users/swessels/Develop/svm/vice-emu-code/vice`
(x64sc built, VICE_VICLOG patch in `src/viciisc/vicii-mem.c`). c64m BALOG:
`C64M_BALOG=/tmp/ba.txt C64M_VICLOG_F0=5850 C64M_VICLOG_F1=5853 C64M_VICLOG_EXIT=1
./build-trace/c64m --headless --control-port PORT --pal -a --turbo=200 -p <prg>`.

NOTE: control-port `break-create exec` sometimes times out on addresses in
banked/low-RAM code that runs under IRQ banking (`$8619`, `$8606`, `$9322`, `$1C61`
never hit) while `$904E`/`$9A05`/`$9B75`/`$9A09` work -- prefer VIC-write-trace or
addresses proven by a call-stack `dest`. Also: turbo blows past the ~5700-6200
window fast; relaunch fresh before each measurement.

**Control-port tooling (committed).** `tools/c64_control_client.py` is a minimal
Python client for the C64M/1 control protocol (connect, `get-state`/`get-cpu`,
`get-memory`, `get-frame`, `break-create exec`, `run`/`pause`/`wait-paused`);
`tools/dis6502.py` is a small 6502 disassembler for reading depacked demo code.
Both have usage headers. Reuse notes: addresses take `$`-hex (base-0 parse);
`get-memory` length must be decimal (max 1024/call); `break-create` via the
control port only supports **`exec`** breakpoints (the runtime engine supports
read/write access, but `control_parse_breakpoint_definition` in `src/main.c`
hard-requires the `exec` keyword) -- a read/write watchpoint (which would let a
future agent break on writes to `$CD`) needs a small control-protocol extension.

## Reproduction

The sample is a BASIC wrapper:

```text
$0801: 10 SYS 2061
```

I ran the PAL build through the headless control path:

```sh
./build/c64m --headless --control-port 17652 --pal -a \
  -p samples/lft-nine.prg
```

At settled frames the published frame is 384x312. The visible result has a
blue central field and the expected central sprite/text material, but the
left and right side columns remain solid border pixels. Frame hashes were
stable across consecutive frames in the failed state, for example:

```text
frame 1135..1146: a103d96c5da311e65b53fc61286be272218684025d29069f218e59e00d1307f6
border pixel:     ff000000
```

The exact frame number varies with startup scheduling; the failure itself is
deterministic after the demo settles.

## Code evidence

The current live renderer does this for every output pixel:

```c
bool hborder = x < g->left || x >= g->right;
...
return vicii_compose_pixel(v, v->vertical_border_active || hborder, ...);
```

`g->left`/`g->right` come from the current CSEL geometry (24/344 for the
40-column layout, 31/335 for 38 columns). There is no horizontal-border state
in `struct vicii`; the only live border state is `vertical_border_active`.

`vicii_compose_pixel()` returns the border colour as soon as `border_active`
is true, before sprite priority is considered. That is correct for a closed
border, but makes side-border sprites impossible when the horizontal border
has not been opened by a timed VIC operation.

The relevant implementation is in `src/machine/vicii.c`:

- side-border geometry: lines 638-643;
- horizontal border decision and composition call: lines 874-906;
- border precedence over sprites: lines 837-853;
- vertical border flip-flop update: lines 909-919.

The existing component handoff already calls this out as deferred: horizontal
border opening is “not modeled as a cycle-exact VIC dot flip-flop”; side borders
currently use CSEL geometry.

## Demo timing observations

A temporary register-write trace was used and then removed. In one PAL run the
demo’s setup and raster code produced writes including:

```text
frame 328, raster 1,  cycle 35: $D016 <- $08
frame 328, raster 1,  cycle 41: $D011 <- $5C
frame 328, raster 51, cycle 8:  $D011 <- $1C
frame 328, raster 52, cycle 11: $D011 <- $1C
frame 328, raster 52, cycle 12: $D011 <- $1B
```

The demo also enables and updates sprites and uses `$D011`/`$D016` during its
raster setup. The write trace confirms that these are timed VIC register
writes, not merely a static screen that happens to contain sprites. The
current implementation stores ordinary `$D016` writes in `registers[0x16]`,
but does not turn them into a horizontal border-unit transition at a dot
position.

## Missing-piece inventory

### 1. Horizontal-border flip-flop and timed opening — definite blocker

Implement an explicit horizontal-border state, analogous to the existing
vertical state, with the correct PAL/NTSC dot/cycle transition rules. A timed
`$D016` write must be able to open the side border for the relevant portion of
a raster line, and the state must carry correctly until the hardware closes it.

The renderer must use that state rather than `x < left || x >= right` alone.
The state should be sampled per rendered dot/pixel, not once for the whole
line, because the demo relies on exact horizontal timing.

### 2. Output-coordinate timing — definite supporting gap

The live renderer currently maps a VIC cycle to an output span using
`cycle * C64_FRAME_WIDTH / cycles_per_line`. That is a useful scaled display
mapping, but it is not a 504-dot PAL / 520-dot NTSC horizontal sequencer. Side
border opening needs a dot-level horizontal counter (or a documented exact
mapping from VIC dots to the published 384-pixel buffer) so a write near the
border transition affects the correct dots.

### 3. `$D016` write phase and border-unit semantics — needs a hardware oracle

`vicii_write_register()` currently treats `$D016` as an ordinary register write.
The missing behavior is not simply “CSEL changes the width”: the border unit
has timing-sensitive state and the write’s CPU/VIC bus phase matters. Capture
the demo against VICE or real hardware and record, for each relevant write,
the raster, VIC cycle/dot, CSEL value, and first/last pixel affected.

### 4. Sprite DMA/latch timing — secondary, not yet localized

The existing sprite scheduler has PAL/NTSC BA tables and the demo reaches a
stable sprite setup, so this investigation found no direct BA failure. Still,
the renderer documentation notes that the sprite-row pre-latch is an
implementation pipeline rather than a literal previous-line DMA latch. Once
horizontal-border state is implemented, compare sprite first-pixel timing and
the side-border reveal against VICE; only then adjust BA, sprite data slots,
or sprite latch timing if a measured divergence remains.

### 5. Late top-border sprite multiplex timing — definite additional failure

The supplied capture is a second failure, not just another view of the
side-border problem. Around the late phase reached roughly 1m49s after RUN,
c64m alternates between materially different frames. In one captured pair the
top-border sprites remain while the central sprite disappears; the next frame
contains the central sprite again. The frame hashes were different on every
frame in the sequence, including consecutive frames 5882 and 5883.

The late trace shows that the program is doing a raster-synchronised sprite
multiplex. The sprite-register rewrite block moves substantially between
frames even though the code is repeating the same operation:

```text
frame 6000: sprite rewrite begins around raster 216
frame 6001: sprite rewrite begins around raster 216
frame 6003: sprite rewrite begins around raster 222
frame 6008: sprite rewrite begins around raster 221
```

The top-border `$D011` chain also moves at the cycle level and changes its
values in the affected sequence. For example, one frame has `$D011` values
`$1D/$70` around rasters 39-44, while a later frame has `$15/$70` at the same
part of the chain. Sprite Y/X/color register writes then occur around
rasters 216-245, followed by the border cleanup at rasters 249-265.

This is evidence of a timing-budget failure in the raster kernel or its
CPU/VIC arbitration, not a random frame-buffer corruption. The central sprite
being present or absent is controlled by whether the multiplexed sprite setup
and fetch/use window stays aligned with the intended raster. The existing
sprite BA implementation was validated against other titles, but this trace
proves it is not yet sufficient to claim this PAL multiplex kernel works.

The immediate missing pieces to investigate are:

- exact CPU cycle budget through the late sprite rewrite block;
- sprite BA assertion/release and AEC/RDY behavior at the top-border and
  cross-line sprite windows used by this title;
- the literal timing of the sprite pointer/data fetch relative to the first
  visible line after each rewrite;
- whether the live renderer's per-line sprite latch is retaining the correct
  previous-line DMA result at frame boundaries.

The first diagnostic should be a per-raster trace of CPU-executed cycles,
BA-stalled cycles, VIC sprite-data slots, and every `$D011`/sprite-register
write. Compare the first frame where the rewrite block moves from raster 216
to 222 against a VICE PAL run. Do not start by changing sprite composition:
the alternating central-sprite result is already visible in the live output
and points upstream to timing/latching.

### 6. AEC/RDY sub-cycle timing — possible dependency

The project documents AEC/RDY as cycle-level rather than half-cycle accurate.
If the demo’s `$D016` write lands near a VIC ownership boundary, the exact
CPU-write acceptance phase may matter. This should be investigated only after
the horizontal border unit exists, using a cycle-stamped write trace; it is
not justified as the first fix from the current evidence.

## Recommended implementation order

1. Add a focused horizontal-border state model and unit tests for a timed
   `$D016` transition on PAL.
2. Add a per-raster CPU/VIC/sprite trace for the late multiplex phase and
   compare it with VICE PAL. Identify the first raster where the rewrite block
   moves or the central sprite disappears.
3. Add a dot/cycle-aware render test with a sprite crossing the left and right
   side-border boundaries.
4. Trace `lft-nine` against VICE and compare the first/last revealed dots and
   sprite fetch/use timing.
5. Only then tune sprite DMA/latch or AEC/RDY timing if the comparison still
   shows a measured mismatch.

## Validation status

The source tree was restored after temporary tracing. The focused VIC-II test
binary passes. The full CTest run completed with 45/47 tests passing; the two
failures were existing unrelated failures:

```text
c64_boot_progression: irq vector entered: expected e100, got e001
c64_robocop_g64: stage-3 gap table self-check would FAIL
```

No source change was made by this investigation.

## Additional details

This demo uses the "Idle Graphics" or "Ghost Byte" and "Ghost Byte Shine Through"
technique, ie using the last byte the Vic chip can fetch graphics from (normally
$3FFF).  But this demo uses ECM mode that moves the ghost bytes to address
$39FF.  Also uses the horz scroll position to delay the fetch of the ghost byte
to align the pattern of the ghost byte with the sprites.  So perfect timing is
needed here.

### Session 8 (2026-07-13): sprite-DMA rewrite fixes the alternation (branch `lft-nine-sprite-crunch`)

Ported VICE's viciisc sprite DMA/MCBASE model into `src/machine/vicii.c`
(commit `df7f955`): cycle-16 MCBASE=MC + DMA-off at 63, cycles 55/56 DMA-on,
cycle-56 exp-flop toggle, cycle-58 MC=MCBASE, MC += 3/line in
`vicii_prepare_sprite_line`, and a `$D017` write handler (force exp-flop set on a
Y-expand clear, plus the cycle-15 MC crunch bit-magic from VICE `d017_store`).

**Result -- primary bug FIXED:**
- VIC write stream goes from strict **192/16 alternation to the device kernel
  running EVERY frame** (~139+ writes/frame). The `$CD` toggle is gone.
- The **wizard renders cleanly and stably** (was garbled/flickering).
- Early-region sprite stall is now correct: BALOG `stall=15` through R23-R30
  (was 18-19).
- All VIC-II/snapshot/boot tests pass; full ctest 48/49 (only the pre-existing
  `c1541_media`). No regressions.

**Remaining (incomplete):**
- The flanking-sprite **crunch does not yet keep DMA active through R42**: BALOG
  shows sprites stop stealing at R31 (stall dips to 0 at R31-R32, 5-9 through
  R38, back to 15 at R39) -- Session 4's "sprites stop at R31" symptom. So c64m
  runs the `$D018` multiplex loop on R26-R39 instead of VICE's `$D021/$D011`
  six-write kernel, the per-frame write count is ~139 vs VICE's 202, and the
  **digits still clump at the top** instead of multiplexing around all four
  borders.
- The crunch bit-magic never fires: the demo's `$D017<-$00` write lands at C14,
  but VICE's `ChkSprCrunch` is at cycle 15 -- so for THIS demo the crunch must
  work through the exp-flop/MCBASE dance, not the bit-magic. Replicating exactly
  how VICE keeps the flanking sprites (0,2,4,5) DMA-active past 21 rows is the
  final piece.
- Also still present: the `$D017` write-cycle jitter (c64m C12/C14 vs VICE stable
  C14) -- a CPU-timing residue to chase after the DMA lifetime is right.

**Next:** trace VICE's per-line `sprite_dma` bitmask (add a counter to viciisc
`check_sprite_display`; my attempt this session had an env/frame-counter issue --
verify VICE_SPRDMA fopen and the independent frame counter) to see exactly which
sprites stay active through R42 and reproduce that lifetime. Then re-measure:
BALOG stall flat ~15 across R26-R44, ~202 writes/frame, digits spread. The branch
is a strict improvement (alternation fixed, tests green) pending that final piece.

### Session 9 (2026-07-13): VICE sprite-DMA ground truth reframes the remaining bug

Merged the Session-8 sprite-DMA fix to `main` (every-other-frame alternation
fixed; the user confirms the nine digits now multiplex correctly around all four
borders in the SDL build). Remaining visible issue: the **side borders are not
black** -- they should form a black rectangle framing the blue field ("full
border mode"), but show blue.

Added a per-line `sprite_dma` bitmask trace to VICE viciisc `check_sprite_display`
(env `VICE_SPRDMA`/`_F0`/`_F1`/`_EXIT`; **must rebuild `src/viciisc` then relink,
`make -C src/viciisc && make -C src x64sc`** -- relinking x64sc alone silently
keeps the old lib). Captured VICE's true multiplex (one frame, cross-checked
against VICLOG in the same run so frames align):

- **VICE has NO sprites DMA-active at R8-R48** (dma=00). Sprites multiplex in a
  **staircase** down the screen: R63-84 (spr 6,7) -> R96-120 (5,4) -> R149-173
  (3,2) -> R196-236 (0,1,7). Eight sprites reused for nine digits.
- **c64m over-activates**: BALOG sprite theft is **continuous R9-R60** (plus
  sparse single-line theft every ~8 lines after). So c64m keeps sprites
  DMA-active in the top-border region where VICE has none.

**This corrects the earlier model:** the crunch's job is NOT to keep flanking
sprites active through R42 -- it is (at least in part) to keep the top region
CLEAR of sprites so the $D021/$D011 six-write kernel can paint the black frame,
and to free sprites for the staircase multiplex. c64m's residual over-activation
at R9-R60 steals cycles the top-border kernel needs and is the likely cause of
the non-black side borders.

Tried firing the `$D017` crunch bit-magic at the write cycle (C14/15 instead of
hardcoded 15): made it WORSE (theft R9-R75). Reverted. So the bit-magic is not
the mechanism here; the over-activation is driven by sprite **Y positions / DMA
on-timing** during the multiplex, not the crunch bit-magic.

**Next:** trace c64m's per-line sprite `enable`/Y/`sprite_active` vs VICE's
staircase to find why c64m activates sprites at R9-R60 (wrong Y latch timing in
the multiplex? DMA-on not matching VICE's cycle-55/56 exactly?). Target: c64m
sprite theft matches VICE's staircase (none R8-R48; bands R63+). VICE trace tool:
`VICE_SPRDMA` in `src/viciisc/vicii-cycle.c` check_sprite_display.

### Session 10 (2026-07-13): terminal sprite-DMA line no longer steals from a stale renderer latch

Added a compile-gated c64m counterpart to the VICE sprite-DMA trace:
`C64M_SPRDMA=<path>` (with the usual `C64M_VICLOG_F0/F1` frame window) emits a
cycle-58 line record with the active-DMA mask, `$D015`, sprite Y values, and
MCBASE values. It makes the distinction between the renderer's row latch and
the bus sequencer observable.

The trace exposed one concrete arbitration bug: when the cycle-16 MCBASE check
reached 63, c64m correctly cleared `sprite_active`, but
`vicii_sprite_dma_current_line()` continued to schedule the later Phi2 sprite
slots from `sprite_visible`. `sprite_visible` is a cycle-0 renderer/data latch,
not the VIC's live DMA bit. VICE clears its `sprite_dma` mask at cycle 16, so
those later slots must not steal CPU cycles. The predicate now follows the live
DMA state; its existing Y-match case still covers DMA turning on at cycles 55/56.

Measured result in the lft-nine device capture: the terminal R30 line changes
from `stall=15` to `stall=6`. The remaining six are the legitimate early
cross-line sprite-3/4 slots before the R30 cycle-16 DMA-off check. This is a
narrow correctness fix, not a claim that the black-frame issue is solved.

Focused `c64_vicii`, `c64_snapshot`, `c64_boot_progression`, and
`c64_cpu_validation` pass. The full-suite run again reached the known unrelated
`c1541_media` `step out` failure; no VIC-II regression was observed before it.

The larger diagnosis remains open: c64m's top device phase enables sprites at
Y=9 (`dma=35` at R9-R29 in the new trace), whereas a properly semantic-aligned
VICE capture is still required before changing the program-visible Y/multiplex
timing. Do not compare the two trace helpers' raw frame counters: VICE's
register logger begins counting at its first VIC store while the temporary DMA
logger counts raster wraps from process start, so their numbers have different
origins. Align on the `$D015/$D017` handshake and register sequence instead.

### Session 11 (2026-07-13): sprite crunch bit-magic cycle + VICE 0-based sequencer

**Aligned oracle (same `$D015=FF` / `$D017 $35@$R11C38` / `$D017 $00@$R12C14`
handshake):**

| | VICE (device phase) | c64m before Session 11 | c64m after |
| --- | --- | --- | --- |
| flanking DMA (spr 0,2,4,5) | R9-R73 (`dma` includes `35`) | R9-R29 only (21 rows) | R9-R73 |
| DMA segs shape | 35 → 37 → 3f → ff → fd → f5 → 35 | truncated at R30 | same shape as VICE |
| six-write kernel | every line ~R25-R45, C20 stable | mostly `$D018` only | every frame, C22 stable, starts ~R36 |
| VIC writes/frame | ~202 | ~139 | ~159 |

**Root cause:** VICE indexes sprite-sequencer events with 0-based `raster_cycle`
where `VICII_PAL_CYCLE(n) = (n) - 1` (Bauer/doc cycle n). In particular
`ChkSprCrunch` is on Bauer cycle 15 → **index 14**. Both emulators log the
demo's `$D017<-$00` at **C14**; c64m previously required `cycle_in_line == 15`
for the MC bit-magic, so crunch never fired. Session 8 had already observed that
applying bit-magic at C14 produced theft through ~R75, then reverted it because
Session 9's misaligned SPRDMA suggested VICE had no top-region DMA.

**Fix (hardware model, not a demo special case):**

1. `$D017` crunch bit-magic fires at cycle **14** (VICE `VICII_PAL_CYCLE(15)`).
2. Align the rest of `vicii_step_sprite_sequencer` to the same 0-based indices:
   MCBASE update 15, DMA-on 54/55, expand toggle 55, MC reload/display 57.
3. Regression tests: crunch keeps DMA past 21 rows; off-crunch-cycle clear does not.

**Session 9 correction:** when SPRDMA is taken from the same process exit as
VICLOG (SPRDMA frames near the end of the capture, not early boot frames), VICE
**does** keep `dma=35` through the top device region via crunch. The earlier
"no DMA R8-R48" sample was a different demo phase / misaligned frame counter.

**Still open:**

- Six-write kernel entry ~11 lines late (R36 vs R25). Free-sprite Y values also
  differ (e.g. c64m `1D/1F/23` vs VICE `12/14/17` for the non-flanking sprites)
  so the staircase of extra DMA joins is shifted by the same ~11 lines. Flanking
  Y=9 matches. Treat as residual multiplex/CPU-phase timing, not MCBASE lifetime.
- Per-frame write count ~159 vs ~202 (missing ~9 six-write lines × 6 writes).
- Settled visual black frame (sprites 5/7 + `$D021` kernel coverage) still needs
  re-check at `--turbo<=7` once the kernel line range matches.

**Next:** keep VICE as gold standard; compare multiplex Y-rewrite block timing
(R215-R264 region) and any remaining BA over/under-count once Y sequences are
phase-matched. Do not reintroduce forced-crunch hacks.
