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
animation** is fixed for both NTSC (plateau is intended) and PAL (sprite BA
window 5→6). Full write-up: [../../C64MVICIIEXNEXT_UPD.md](../../C64MVICIIEXNEXT_UPD.md).

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

## Expose reveal — FIXED

### NTSC

The ~27-frame hold matches VICE and is **intended** hardware/game behaviour, not
a c64m defect.

### PAL (was broken)

Root cause: sprite BA window was 5 cycles; needs 6. Under-stall during the
8-sprite strip left the stable-raster kernel one line early at the first
`$D012` wait, so the wait never locked and the multiplex desynced permanently.
Fixed by `VICII_SPRITE_BA_WINDOW = 6`.

### Ruled out

- **Sprite Y-expansion "crunch."** The obvious hypothesis for a venetian expose.
  Disproven: `$D017` is 0 for every sprite the whole time, so there is no
  Y-expansion and no crunch. A full cycle-accurate MCBASE / expansion-flip-flop
  sequencer (toggle at cycle 55, MCBASE +2/+1 at cycles 15/16, fresh `$D017`
  reads) was prototyped anyway; it passed all 40 tests but **did not change the
  27-frame freeze**, confirming the reveal is not sprite-driven. That prototype
  was reverted to keep the tree focused on verified fixes.

### Actual mechanism (CORRECTED — see C64MVICIIEXNEXT_UPD.md)

> The earlier "FLI/badline-class effect" description below the line was **wrong**
> and is superseded. YSCROLL is constant at 3 the whole frame; this is not a
> bad-line effect. The reveal has since been traced end to end. Full diagnosis and
> fix plan: [../../C64MVICIIEXNEXT_UPD.md](../../C64MVICIIEXNEXT_UPD.md).

The reveal is driven by a **cycle-exact "stable raster" kernel** the title copies
into RAM at `$0400-$0590`. It waits for raster `$2E`, reads **CIA#2 Timer-A
(`$DD04`)** to cancel interrupt jitter (self-modified `BPL` into a `cmp #$C9`
NOP-sled), then walks down the screen with `cpy $D012` busy-waits, writing `$D011`
(the `$73/$33` RST8/RSEL values that re-arm the raster-IRQ chain) and **`$D01B`
(sprite-to-background priority)** per raster line from the `$CA00` table.

**The reveal itself is per-line `$D01B` (sprite-to-background priority)
animation.** The `$CA00` mask sweeps `FF -> 00` (even entries top-down, then odd
entries bottom-up) — exactly the "down then back up" motion. Where a line is `FF`
the sprites sit behind the bitmap; flipping to `00` inverts priority and opens the
line.

### Why c64m gets it wrong

c64m **runs the game logic perfectly** — the `$CA00` mask animates flawlessly
every frame, and sprite priority is rendered live per pixel
(`vicii_compose_pixel`). The defect is that the kernel is hand-cycle-counted
against a real NTSC per-line cycle budget, and c64m's **CPU<->VIC bus
cycle-stealing is not hardware-exact** in the sprite-dense region. A small
per-line stall-count error accumulates down the walk, so the per-line writes
desync and the lower band (odd rasters 171..251) freezes. Fix surface:
`c64_cpu_cycle_stalled_by_ba` (c64.c:989, RDY/AEC write tolerance) and the Phase-H
sprite BA windows (vicii.c:89-101, `vicii_sprite_dma_next_line`). The renderer is
not involved.

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

## Suggested next steps (SUPERSEDED)

> The badline-oriented steps below are obsolete — the mechanism is a cycle-exact
> stable-raster kernel doing per-line `$D01B` priority animation, not a bad-line
> effect. Follow the phased fix plan in
> [../../C64MVICIIEXNEXT_UPD.md](../../C64MVICIIEXNEXT_UPD.md) instead:

1. **Phase A — localise:** log per-line CPU-executed vs BA-stalled cycles across
   the kernel region (~raster 50..251) and diff against the Bauer NTSC 6567R8
   budget (65/line − 40 bad line − 2/active sprite). The first divergent raster is
   the bug site.
2. **Phase B — fix:** correct the specific stall rule found in Phase A — the
   sprite-DMA BA windows (vicii.c Phase H) and/or the RDY/AEC "up to 3 write
   cycles after BA low" rule (c64.c:989).
3. **Phase C — verify:** confirm against a VICE / real-hardware oracle and add a
   per-line cycle-budget regression test.
