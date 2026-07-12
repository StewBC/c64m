# lft-nine investigation

Date: 2026-07-11

## Result

The primary missing piece is horizontal-border timing/state in the VIC-II
renderer. c64m has a live vertical-border flip-flop, but the side border is
still a fixed geometric mask. Consequently a sprite that is correctly fetched
and positioned in the side border is always hidden by the border colour.

This is independent of the already implemented PAL/NTSC sprite BA tables. The
demo is therefore a useful next target for horizontal-border work, but it is
not yet evidence that the current sprite BA table is wrong.

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