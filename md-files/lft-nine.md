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