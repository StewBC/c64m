# Framebuffer pixel format plan

**Status:** proposed (2026-07-28). Not implemented. **Priority: do this next**
(raised by the repo owner; parked only for lack of session budget at the time).

This is an analysis and a staged plan, not a work log. Stage 1 is small and
worth doing on its own; Stage 3 is a deliberate project, not a drive-by.

## The finding

The VIC-II is a **4-bit device**: 16 colours, no blending. c64m has the colour
index at every pixel decision and immediately widens it 8x to 32-bit ARGB at the
point of generation. Every paint decision in `src/machine/vicii.c` looks like:

```c
return vicii_bg_pixel_make(vicii_palette_argb[vbuf & 0x0f], false);
return vicii_sprite_pixel_make(vicii_palette_argb[color], true);
```

(`vicii_palette_argb[16]` is at `src/machine/vicii.c:133`.)

The remote API then runs a **reverse** lookup to recover the index that was
discarded microseconds earlier (`control_argb_to_index`, `src/main.c:1570`,
called per pixel at `src/main.c:1618`):

```c
for (i = 0; i < 16u; i++) if (control_palette_argb[i] == argb) return i;
return 0u;   /* unknown colour silently becomes black */
```

Two problems with that:

1. **It is lossy.** Any ARGB not exactly in the palette becomes index 0. The
   consumers that most need fidelity - the debugger, the remote API, and the
   VICE oracle compare, which uses `indexed8` precisely *because* c64m and VICE
   RGB values differ - are the ones fed a reconstruction rather than the truth.
2. **It is a linear scan of 16 per pixel** - up to ~2.5M comparisons per PAL
   frame. Scrubbing 60 frames out of the frame ring burns ~150M comparisons.

`c64_frame` already carries a `pixel_format` field with exactly one value
defined (`C64_FRAME_PIXEL_FORMAT_ARGB8888 = 1`, `src/machine/c64_frame.h:28`),
so the original design left room for this.

## What ARGB costs downstream

`c64_frame` is 520x312x4 = ~649 KB and crosses boundaries **by value**. Per
completed frame:

| # | Copy | Site |
|---|------|------|
| 1 | `c64_copy_completed_frame` -> `rt->publish_frame` | `runtime_thread.c:1216` |
| 2 | frame ring push | `runtime_publish_completed_frame` |
| 3 | `rt->frame_slot.frame = *frame` | `runtime_thread.c:1146` / `:1187` |
| 4 | `runtime_client_poll_frame` -> main loop local | `src/main.c:3438` |

About 2.6 MB per frame. In `indexed8` (162 KB) the same chain is ~650 KB.

## Honest perf note: the speed argument is weak

Measured, not assumed. Adding the frame-ring push - **one** extra 649 KB copy
per frame - cost **0.22%** of turbo-2 throughput (see `frame-ring-plan.md`). So
each copy is worth roughly 0.22%, and eliminating three of the four saves under
1%. **Frame copying is not a bottleneck.** Do not sell this refactor as a speed
win.

The arguments that do hold up:

- **Ring depth x4.** The same 128 MiB frame-ring budget would retain ~16 s of
  play instead of ~4 s. For "I noticed the glitch and paused several seconds
  later", that is the difference between catching it and missing it.
- **Lossless.** `indexed8` becomes the native format rather than a lossy
  reconstruction, which is what the API and the VICE oracle compare want.
- **Deletes the reverse scan entirely**, including the scrub cost above.
- Snapshot/state work and any future frame persistence get 4x smaller.

## What makes it less risky than it sounds

- **The save format is not affected.** `.c64state` does not serialize the render
  buffers - `grep 'frames\[' src/machine/c64_snapshot.c` is empty. The usual
  blocking compatibility risk is absent.
- **The SIMD paint path gets simpler, not harder.** `vicii_store8_u32` /
  `vicii_fill8_u32` (`src/machine/vicii.c:12` onward) currently write 8 dots as
  two NEON quad stores (32 bytes). In `indexed8`, 8 dots = 8 bytes = one 64-bit
  store.
- **An expansion stage already exists at the display boundary.** The frontend
  keeps a separate `ui->crt_pixels` ARGB buffer and `crt_renderer` takes
  `const uint32_t *source` (`src/frontend/crt_renderer.h:18`). Index -> ARGB
  there is a *forward* table lookup, essentially free, and happens once instead
  of being carried through four copies.

## The real risks

- This is the hottest path in the emulator plus the whole display pipeline, and
  the component whose correctness was hardest won: border flip-flop, colour
  pipe, one-pixel colour latency, XSCROLL, sprite priority. Regression risk is
  genuine.
- The per-dot colour pipe (`color_pipe_d020` / `color_pipe_d021`), border logic,
  and sprite/graphics priority currently traffic in ARGB *values*. They would
  all become index values. Mechanical, but broad.
- **Audit required before committing:** confirm nothing anywhere *synthesises*
  an ARGB value rather than looking one up in `vicii_palette_argb`. Spot checks
  during analysis found only palette lookups, but that is not a proof. If any
  path can emit an off-palette colour, `indexed8` cannot represent it and the
  whole premise needs revisiting.
- Check the geometric debug snapshot path (`c64_make_current_frame_snapshot`)
  and the frontend crop for the same assumption.

## Staged plan

### Stage 1 - fix the reverse lookup (do this regardless, ~10 lines)

Replace the 16-entry linear scan in `control_argb_to_index` with a direct map:
a 4096-entry table keyed on packed RGB, or a small perfect hash. Kills ~2.5M
comparisons per converted frame and makes scrubbing cheap. Independent of
everything below, near-zero risk.

Keep the lossy-fallback behaviour explicit and documented (unknown -> 0) so the
change is purely a speed fix, not a semantic one.

### Stage 2 - measure before committing to Stage 3

Profile what fraction of turbo-2 time is frame copy plus conversion. The 0.22%
datapoint says "small". If Stage 2 confirms that, Stage 3 must be justified on
ring depth and fidelity alone - which is a legitimate case, but a different one,
and it should be made explicitly rather than by implication.

### Stage 3 - make `indexed8` the native format

`c64_frame.pixels` becomes `uint8_t`; the VIC writes indices; one index -> ARGB
expansion at the display boundary. Set `pixel_format` accordingly and keep the
field meaningful.

Kill tests to write **before** implementing, per the diagnosis discipline in
`README.md`:

- Pixel-exact equality against the current build for the known demos
  (lft-nine, EoD checker, DEM pillar/spirals) - same frames, same dots.
- `get-frame format=argb8888` output must be byte-identical to today's for the
  same frame, since the expansion is a forward map of the same palette.
- `get-frame format=indexed8` must change only where the old reverse map was
  *wrong* (off-palette -> 0). Enumerate those pixels; if the set is non-empty,
  that is the lossiness this change fixes and it should be reported, not hidden.
- VICE oracle compare unchanged or improved.
- Perf: re-measure against `perf-baseline-turbo2.md`; expect a small win, accept
  neutral, investigate any loss.

## Files this touches

- `src/machine/vicii.c`, `src/machine/vicii.h` - paint path, palette, colour
  pipe, priority, SIMD helpers
- `src/machine/c64_frame.h` - pixel array type, `pixel_format`
- `src/machine/c64.c` - frame snapshot helpers
- `src/runtime/runtime_thread.c`, `runtime_frame_ring.{c,h}` - copies, ring
  slot size
- `src/main.c` - `control_format_frame_response_ex`, the reverse map
- `src/frontend/frontend.c`, `crt_renderer.{c,h}` - the single expansion point
- `agents/vicii.md`, `agents/control-port.md` - update in the same change

## Non-goals

- Changing the palette itself, or adding palette variants.
- Any change to `.c64state`.
- Packing to 4 bits per pixel. Half the memory again, but it complicates every
  read and the VIC paints per dot; `indexed8` is the sane stopping point.
