# lft-nine investigation

Date: 2026-07-11 (original diagnosis); progress update 2026-07-12.

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

Instrumentation (throwaway, env-gated, **uncommitted**):
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

### Instrumentation still in the tree (uncommitted, env-gated, throwaway)

All three are debug-only and inert unless their env var is set. Remove (or keep
behind the env gate) once the fix lands; they are the verification harness so do
not delete them before re-verifying.

- **c64m VIC write log** -- `src/machine/vicii.c`, top of
  `vicii_write_register` (just after `reg = addr & 0x3F`), plus `#include
  <stdlib.h>`. Env: `C64M_VICLOG`, `C64M_VICLOG_F0`, `C64M_VICLOG_F1`,
  `C64M_VICLOG_EXIT`.
- **c64m per-raster CPU budget** -- `src/machine/c64.c`: `c64_balog_*`
  helpers above `c64_advance_one_cycle`, with `c64_balog_mark(...)` calls in
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
