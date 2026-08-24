# VIC-II

## Source of truth

`src/machine/vicii.{c,h}`, integration in `c64.c` / `c64_bus.c`, geometry in
`c64_frame.{c,h}`. Tests: `tests/machine/test_c64_vicii.c`, `test_c64_frame.c`,
`test_c64_cpu_validation.c`.

Entry points: `vicii_begin_cycle()`, `vicii_finish_cycle()`, `vicii_ba_active()`,
`vicii_aec_active()`, `vicii_rdy_active()`, register read/write, and
`vicii_copy_completed_frame()` / snapshot reconstruct. Frontend frames are
copies. Snapshot reconstruct is a debugger fallback, not a timing oracle.

Read `README.md` diagnosis rules before naming a pixel mechanism. Highest split:
border vs field (VIC X outside `[24, 344+XSCROLL)` is a different paint path).
Next: sprite vs graphics.

## Geometry and framebuffer

PAL 6569: 63 cycles x 312 lines, 504 dots/line. NTSC 6567R8: 65 x 263, 520
dots/line. Sprite wrap is `cycles_per_line * 8` (504/520), not 512.
`c64_init` / `vicii_init` default to NTSC.

Native pixels are **indexed8** (0..15). `c64_frame.pixels` stride is always
`C64_FRAME_WIDTH` (520). `frame->width` is 504 PAL / 520 NTSC and is **not**
the row pitch. Unpainted sentinel `0xff` expands to ARGB 0 / wire index 0.
Framebuffer x **is** VIC X, full line including HBLANK. ARGB exists only at
presentation (`c64_frame_expand_argb`, Pepto).

Frontend PAL crop is VICE's 32/320/32: 384x272 from Y=16 (rasters 16..287),
rotated from VIC X **496**. That rotation is display-only; `get-frame` and
snapshots stay in VIC-X order. NTSC crop is 352x224 from X=8, Y=39. Do not
give NTSC a PAL-sized crop. Do not retry a modular +8 origin shift to fake
32/32: that invented a left pad and dropped X 376..383.

True Aspect Ratio uses the VIC-II pixel aspect (PAL 0.9365, NTSC 0.7500), not
a hardcoded 4:3.

Turbo 1 and 2 publish the live per-cycle indexed framebuffer. Turbo 3 (warp)
keeps raster, BA, IRQ, sprite-DMA, CIA, and SID; published frames are
geometric debug, not visual evidence. Sprite collision latches update only
while pixel output is on. After leaving warp, discard one completed frame
before judging pixels.

## Sequencer

Machine order: `vicii_begin_cycle` does Phi1/internal work and establishes
BA/AEC; CPU may then use Phi2; `vicii_finish_cycle` advances the raster. A
same-cycle STA `$D011` is too late for that cycle's UpdateVc/badline.

- Bad lines: DEN sampled at raster `$30` into `allow_bad_lines` for `$30-$F7`.
  Condition every cycle from that latch, range, and live YSCROLL.
- UpdateVc at cycle **13**. Phi2 c-access 14..53. Phi1 g-access 15..54, then
  VC/VMLI++. UpdateRc at cycle **57** (not line wrap).
- End of frame resets **only VC and VCBASE**. RC, VMLI, and display state
  carry. Forcing RC=0 here pinned EoD's rotating object. See
  `test_frame_boundary_carries_rc_vmli_display`.
- `reg11_delay` samples `$D011` at the end of `begin_cycle`. G-access address
  bits use the prior-cycle mode.
- Live paint uses `g_line`, not a re-read of RAM, so mid-line `$D018` changes
  do not re-decode already-fetched columns.

DEN gates bad-line arming and the top vertical-border compare. It is not a
live graphics blank. Clearing DEN after the sequencer has opened leaves the
running VC/RC pipeline visible.

## Borders, XSCROLL, colour

Main border (Bauer 3.9) covers sprites with `$D020`. Vertical border blanks
graphics to B0C and does **not** blank sprites. Vertical unit is VICE
two-stage: `set_vborder` (bottom only sets) and `vertical_border_active`.

Horizontal checks sit under a 2-cycle paint delay: left cycle 17; right 57
(CSEL=1) or 55 (CSEL=0). CSEL=0 follows VICE `draw_border8` (7 dots keep
previous, last takes the new flip-flop). 38-col edges are VIC X 31/335.

`xscroll_pipe` latches `$D016` XSCROLL **after** CPU Phi2, **only on g-access
cycles 15..54**. Cycles 0..14 re-latch a previous-line `$62` before column 0.
Cycles 55+ take EoD's open-border dodge `$D016=$62`. Either mistake paints a
solid B0C column at x=24. The right over-border starts at **344+XSCROLL**,
not a fixed 344.

Over-border (x < 24 or x >= 344+XSCROLL) has **no graphics data**: gbuf is
zero, not the `$3FFF` ghost byte. Ghost shine-through painted EoD plasma
black blocks in the opened side border. Idle **inside** the 40-column window
still uses the ghost byte. MCM text idle is hires (`cbuf==0`); only MCM
bitmap idle stays multicolor. Invalid modes force pixel **colour** black but
keep the graphics-derived **foreground/priority** bit (dkarcade venetian
sprites).

Colour pipes advance one VIC **dot** per cycle, including HBLANK. Sampling
only painted pixels left a 1px old-`$D020` stub at x=0. `$D016` MCM bit for
paint is resolved in `finish_cycle`; a mid-line MCM flip on cycles 15..54
re-decodes the just-painted span (Deus Ex Machina band transition).

## Sprites, IRQ, registers

Sprite-sprite priority is resolved before sprite-background: lowest opaque
sprite wins the mux, then that sprite's `$D01B` decides. Deus Ex Machina
spirals depend on this.

BA: bad-line low on cycles 11..53; sprite BA uses VICE per-cycle PAL/NTSC
masks, re-evaluated every cycle. AEC/RDY are cycle granularity.

Raster IRQ is edge-triggered non-match to match. `$D011` re-checks only when
RST8 changes the 9-bit line (Arkanoid). Writing `$D012` to the current line
still fires (Galencia). Collision IRQs edge 0 to nonzero; acking `$D019`
while the latch is still set must not re-fire (Potty Pigeon).

`$D016` reads `(reg & 0x3F) | 0xC0`. Bit 5 (RES) is real; forcing it high
made Deus Ex Machina `$F8` where VICE reads `$D8`. `vicii_read_register` and
`vicii_debug_read_register` must agree.

## Rings

Runtime frame ring stores completed **indexed8** frames, keyed by frame
number and `machine_cycle`. VIC ring stores per-line latched state
(`vicii_line_record`), including the sprite X used for that line. Warp
frames are not stored in the frame ring.

## Limits

Light pen stubbed. No analog AEC/RDY. Open bus only for BA-lead cbuf.
Snapshot files do not serialize paint pipes or paint buffers. Cycle-perfect
demo compatibility is not claimed; lft-nine, Edge of Disgrace, and Deus Ex
Machina are the load-bearing targets.

A remaining whole-line vs VICE disagreement on X 392..407 (row-boundary
offset) sits outside the 32/32 viewport.

After VIC changes: `ctest --test-dir build --output-on-failure`. For oracle
compares, match `-VICIImodel 6569` (`vice-oracle.md`).
