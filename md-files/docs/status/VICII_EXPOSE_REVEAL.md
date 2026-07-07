# VIC-II open-border "expose" reveal — investigation and status

## Summary

`samples/dkarcade2016.prg` (NTSC) shows a Donkey Kong title picture that is
**taller than the normal 25-row display window**. It does this with the classic
"open the vertical border and put picture data outside the display window"
technique, and it introduces itself with an **"expose" reveal**: on real
hardware (VICE / a real NTSC C64) the picture wipes in every second raster line
going down, then fills the remaining lines coming back up, as one continuous
motion.

Three separate rendering bugs behind the **static** picture were found and
fixed (see "Fixed" below); the picture now matches VICE. The **reveal
animation** is *not* yet correct and is documented here as deferred work.

This file is the detailed record so the next agent does not re-derive it.
See also [VICII.md](VICII.md) and [DEFERRED.md](DEFERRED.md).

## How the title is drawn (register-level facts)

Captured live over the control port while the title is on screen:

- `$D011 = $33` almost the whole frame → **RSEL=0** (24-row window), BMM=1,
  DEN=1, YSCROLL=3. The game flips to `$D011 = $3B` (**RSEL=1**) for a few
  raster lines around raster 46 only.
- `$D016` → multicolour, CSEL=1 (40 columns).
- `$D020 = 0` (black border), `$D021` is raster-split: **black** inside the
  display window, **brown (8)** in the border region.
- `$D017 (MxYE) = 0` for **all** sprites, for the whole frame — the sprites are
  **never Y-expanded**.
- VIC bank = `$C000`; idle g-access byte `$FFFF = $FF`.
- Sprite Y positions cycle through `{0, 29, 250}`; all eight sprites share a Y
  and form a horizontal strip. Y=29 → a strip at rasters 30..50; Y=250 → a strip
  at rasters 251..262. The bitmap fills rasters 51..250.
- A raster IRQ at `$04xx` walks a table at `$CA00` and writes `$D011` (and other
  registers) at raster-timed points; the main thread waits in a small loop
  (`$8000: LDX $CAFB; STX $D01B; BIT $EA; RTS`) driven from that IRQ.

## Fixed (static picture now matches VICE)

All three are in `src/machine/vicii.c` (+ one crop constant in
`src/frontend/frontend.c`):

1. **Vertical border compares are PAL/NTSC-identical** (51/251, 55/247). The
   earlier NTSC-specific 41/241 values shifted the background up 10 lines
   relative to sprites. See [VICII.md](VICII.md).
2. **Vertical-border flip-flop persists across frames.** `vicii_begin_live_frame`
   no longer force-sets `vertical_border_active = true` each frame. On hardware
   the flip-flop only toggles at the top/bottom compares, so a program that
   dodges the bottom compare keeps the border open across the frame boundary —
   which is what lets sprites multiplexed into the upper/lower border draw at
   all. Power-on/reset still establishes the closed default via `vicii_reset()`.
3. **Idle-state graphics render outside the display window.** `vicii_idle_pixel`
   emulates the idle g-access (fetch `$3FFF`/`$39FF`, c-data forced to 0). For
   this title the idle byte `$FF` in multicolour bitmap mode resolves to black,
   which is what fills the un-sprited gaps in the opened border — previously
   c64m painted them with the live background colour (brown).
4. **Bitmap display window decoupled from RSEL.** The graphics/badline range that
   produces the picture is fixed at the 25-row window (51..250) and does not
   depend on RSEL; only the *border overlay* uses the RSEL-dependent compares.
   Anchoring the bitmap vertical window and `sy` to the fixed 51/251 values
   stops the top/bottom four display lines being dropped when the program runs
   RSEL=0 but opens the border to show them. (Previously the picture split into
   three pieces with black seams at rasters 51..54 and 248..250.)

Frontend NTSC crop is `Y=23` (263-line frame) so the 240-line crop keeps the
51..250 window plus the full bottom border on screen.

## NOT fixed: the expose reveal (deferred)

### Symptom

Measured frame-by-frame over the control port (`get-frame`, count of raster
rows containing any lit pixel in the display width):

- Even lines wipe in top-to-bottom (≈1 line/frame) up to ~197 lit rows.
- Then the output is **byte-identical for ~27 consecutive frames** (a hard
  freeze; the frames hash the same).
- Then the remaining (odd) lines fill in and the picture settles (~238 rows).

On VICE the 27 "frozen" frames are instead spent actively revealing (down, then
back up, with ~6 cycles of idle between down and up), so it looks like one 
smooth motion. In c64m it looks like: partial down-wipe → hold → the rest snaps
in.

### Ruled out

- **Sprite Y-expansion "crunch."** The obvious hypothesis for a venetian expose.
  Disproven: `$D017` is 0 for every sprite the whole time, so there is no
  Y-expansion and no crunch. A full cycle-accurate MCBASE / expansion-flip-flop
  sequencer (toggle at cycle 55, MCBASE +2/+1 at cycles 15/16, fresh `$D017`
  reads) was prototyped anyway; it passed all 40 tests but **did not change the
  27-frame freeze**, confirming the reveal is not sprite-driven. That prototype
  was reverted to keep the tree focused on verified fixes.

### Actual mechanism

The reveal is driven by the **raster IRQ writing `$D011` per raster line** from
the `$CA00` table (per-scanline control-register manipulation, i.e. an
**FLI/badline-class effect**), plus the two sprite strips. During the 27-frame
"freeze" the game's main thread is spin-waiting while that IRQ keeps repainting;
on hardware the per-scanline `$D011` timing advances the picture during those
frames, but c64m emits identical frames because its **badline / mid-line
`$D011` raster timing does not reproduce the progression**.

### Why it is hard / why it is deferred

Reproducing it needs **cycle-exact `$D011`/badline handling** (when a mid-line
`$D011` write forces or suppresses a badline, and the exact raster/cycle at
which the display state and VC/RC latches change). This is FLI-class raster
accuracy — beyond the current milestone's "video output correct enough for
normal PAL and NTSC software," and it touches the same timing code as the
existing sprite-BA tests. It should be its own scoped task, not a tack-on.

## Reproduction recipe

```
./build/c64m --headless --control-port 6510 --ntsc -p samples/dkarcade2016.prg
```

Then, over the control port: `run`, advance with `wait-frame 1`, and pull
`get-frame format=argb8888` each frame. The frame is 384×263 ARGB8888.
Useful signals:

- Count raster rows with any lit pixel in x∈[24,344): the reveal grows, then
  holds at ~197 for ~27 frames, then grows again.
- Hash consecutive plateau frames — they are identical (frozen, not flicker).
- `get-memory $d017 1 map` → always 0 (no sprite expansion).
- `get-memory $d011 1 map` sampled across rasters → mostly `$33`, briefly `$3B`.

The static picture can be checked at any settled frame (barrel top visible, no
brown border bar, no black seams through the picture).

## Suggested next steps

1. Treat the expose reveal as a dedicated **`$D011`/badline raster-timing**
   task, not a sprite task.
2. Trace the game's per-raster `$D011` writes against the display-state / badline
   transitions in `vicii_render_live_cycle` and compare to VICE's per-scanline
   output to find where the progression is lost.
3. Only then decide whether FLI-class accuracy is in scope for the milestone.
