# Framebuffer pixel format plan

**Status:** Stages 1–3 implemented and measured (2026-07-28).

Stage 1 shipped as a standalone control-path improvement. Stage 2 confirmed the
improvement was meaningful for indexed-frame reads. Stage 3 then made
`indexed8` the native machine/runtime representation, primarily for 4x frame
ring depth and a stronger representation invariant; emulator throughput stayed
neutral, as expected.

## The finding

The VIC-II is a **4-bit device**: 16 colours, no blending. Before Stage 3, c64m
had the colour index at every pixel decision and immediately widened it to
32-bit ARGB at the point of generation. Stage 3 retains the index through the
machine and runtime and expands it only at presentation/control boundaries.
The former paint decisions in `src/machine/vicii.c` looked like:

```c
return vicii_bg_pixel_make(vicii_palette_argb[vbuf & 0x0f], false);
return vicii_sprite_pixel_make(vicii_palette_argb[color], true);
```

(The former `vicii_palette_argb[16]` lived in `src/machine/vicii.c`; the central
presentation palette now lives in `src/machine/c64_frame.c`.)

Before Stage 1, the remote API ran a **reverse** lookup to recover the index that
was discarded microseconds earlier (`control_argb_to_index`, `src/main.c`,
called per pixel by `control_format_frame_response_ex`):

```c
for (i = 0; i < 16u; i++) if (control_palette_argb[i] == argb) return i;
return 0u;   /* unknown colour silently becomes black */
```

Two problems with that:

1. **It is lossy.** Any ARGB not exactly in the palette becomes index 0. The
   consumers that most need fidelity - the debugger, the remote API, and the
   VICE oracle compare, which uses `indexed8` precisely *because* c64m and VICE
   RGB values differ - are the ones fed a reconstruction rather than the truth.
2. **It was a linear scan of 16 per pixel** - up to ~2.5M comparisons per PAL
   frame. Scrubbing 60 frames out of the frame ring could burn ~150M
   comparisons. Stage 1 removed this scan.

`c64_frame.pixel_format` now reports
`C64_FRAME_PIXEL_FORMAT_INDEXED8`; `C64_FRAME_PIXEL_FORMAT_ARGB8888` remains an
explicit legacy format identity for validation and wire expansion.

## What ARGB costs downstream

Before Stage 3, `c64_frame` was 520x312x4 = ~649 KB and crossed boundaries
**by value**. Per completed frame:

| # | Copy | Site |
|---|------|------|
| 1 | `c64_copy_completed_frame` -> `rt->publish_frame` | `runtime_thread.c:1216` |
| 2 | frame ring push | `runtime_publish_completed_frame` |
| 3 | `rt->frame_slot.frame = *frame` | `runtime_thread.c:1146` / `:1187` |
| 4 | `runtime_client_poll_frame` -> main loop local | `src/main.c:3438` |

That was about 2.6 MB per frame. Native `indexed8` makes the same chain about
650 KB.

## Honest perf note: the speed argument is weak

Measured, not assumed. Adding the frame-ring push - **one** extra 649 KB copy
per frame - cost **0.22%** of turbo-2 throughput (see `frame-ring-plan.md`). So
each copy is worth roughly 0.22%, and eliminating three of the four saves under
1%. **Frame copying is not a bottleneck.** Do not sell this refactor as a speed
win.

The arguments that do hold up:

- **Ring depth x4.** The same 128 MiB frame-ring budget retains about 827 PAL
  frames / 16.5 s instead of about 206 / 4.1 s. For "I noticed the glitch and
  paused several seconds
  later", that is the difference between catching it and missing it.
- **Native invariant.** `indexed8` becomes the source representation rather
  than a reconstruction. The completed writer audit found no current
  off-palette output, so this removes latent risk rather than known bad pixels.
- **The reverse-scan win is already shipped in Stage 1.** Do not count it again
  when deciding whether Stage 3 is worth its broader risk.
- Snapshot/state work and any future frame persistence get 4x smaller.

## What makes it less risky than it sounds

- **The save format is not affected.** `.c64state` does not serialize the render
  buffers - `grep 'frames\[' src/machine/c64_snapshot.c` is empty. The usual
  blocking compatibility risk is absent.
- **The paint stores got simpler.** The former `vicii_store8_u32` /
  `vicii_fill8_u32` wrote 8 dots as 32 bytes. In `indexed8`, 8 dots = 8 bytes,
  handled by `memcpy` / `memset`.
- **The expansion is at the display boundary.** The frontend retains native
  `ui->current_frame` data and expands once into `ui->display_pixels`; both the
  plain SDL texture and CRT renderer consume that ARGB staging data.

## The real risks

- This is the hottest path in the emulator plus the whole display pipeline, and
  the component whose correctness was hardest won: border flip-flop, colour
  pipe, one-pixel colour latency, XSCROLL, sprite priority. Regression risk is
  genuine.
- The per-dot colour pipe (`color_pipe_d020` / `color_pipe_d021`), border logic,
  and sprite/graphics priority all changed from ARGB values to indices. This was
  mechanical but broad, which is why frozen-binary and `c64_vicii` pixel gates
  were required.
- **Writer audits in Stages 2 and 3:** live paint, buffer initialization, and
  geometric debug snapshot paths are palette-derived. Any future path that can
  emit an off-palette colour must revisit the representation premise.
- The frontend crop/rotation audit found one presentation path for both plain
  SDL and CRT output, fed by the shared expansion helper.

## Staged plan

### Stage 1 - fix the reverse lookup (implemented)

Stage 1 changed `control_argb_to_index` to a 4096-byte RGB444 candidate table.
Entries stored palette index + 1 so zero remained the no-candidate sentinel. A
candidate was accepted only after comparing the complete 32-bit ARGB value
against the palette, preserving the documented unknown -> index 0 behavior even
when an unknown colour shared an RGB444 bucket with a palette colour. Stage 3
subsequently removed this reverse converter because the native source is now
already indexed.

`tests/control/test_frame_ring_control.py` now retrieves 16 consecutive frames
from a ROM that advances `$D020` once per frame and proves all palette indices
0..15 appear in order. It also compares every visible `indexed8` pixel with the
corresponding ARGB pixel and the Pepto palette.

### Stage 2 - measure before committing to Stage 3 (completed)

Three serial before/after runs used
`tools/measure_control_latency.py --bin ./build/c64m` against a paused PAL
machine. Each run measured 30 warmed `get-frame format=indexed8` round trips:

| Metric | Before | After | Change |
|---|---:|---:|---:|
| mean of run means | 2.315 ms | 1.679 ms | **-27.5%** |
| mean of run p50s | 2.282 ms | 1.675 ms | **-26.6%** |

This is end-to-end latency including control framing, loopback transport, and
the 157,248-byte response, so it does not pretend to isolate converter cycles.
The same runs' `get-cpu` means stayed effectively flat (1.280 ms before,
1.280 ms after), supporting that the improvement is local to frame conversion.
The previously measured frame-ring push cost remains 0.22% of turbo-2
throughput.

The source writer audit found current live and geometric framebuffer writes are
palette-derived. Consequently, the old reverse lookup was lossy by contract but
appears exact for frames c64m currently produces. Stage 3's demonstrated case is
therefore 4x ring depth plus removal of a latent representation hazard, not
evidence of currently wrong indexed pixels.

Verification: focused `frame_ring_control_integration` passed, followed by
`ctest --test-dir build --output-on-failure` at **69/69 passing**.

### Stage 3 - make `indexed8` the native format (implemented)

`c64_frame.pixels` is now `uint8_t`; the VIC writes palette indices and the
machine/runtime/frame ring retain them. A shared `c64_frame_expand_argb`
performs the single forward palette expansion used by the frontend and legacy
ARGB control responses. The frontend uses one ARGB staging buffer for both the
SDL texture and CRT renderer. Native PAL row padding uses an internal `0xff`
unpainted sentinel, which expands to transparent zero; `indexed8` wire payloads
map it to index 0 and expose no sentinel.

The Stage 1 reverse lookup and RGB444 table are gone: native indexed responses
copy/map the source indices, while ARGB responses expand through the central
Pepto palette. The frame ring still has the same 128 MiB budget but now holds
about 827 PAL frames.

Verification:

- Frozen pre-Stage-3 versus post-Stage-3 PAL and NTSC complete and partial-frame
  payloads were byte-identical in both `argb8888` and `indexed8`.
- An Edge of Disgrace checker capture reached frame 7271 in both builds and
  emitted byte-identical PPMs
  (`254437cf73a5b1072a8cabfc1795e4df8b71daf511dd01b9548c658e06cd92ac`).
- No `indexed8` differences were found in the captured live/geometric frames,
  confirming the writer audit: current output was palette-derived.
- VICE-facing `indexed8` payloads are unchanged byte-for-byte, so existing
  oracle comparisons are unchanged; no VIC model/timing expectation moved.
- `c64_vicii` pixel/timing regression coverage passed, followed by the complete
  suite at **69/69**.
- Matched 20M-cycle hot-loop measurements were neutral: host paint-on
  **16.394 -> 16.258 MHz**, paint-off **22.175 -> 21.995 MHz**, and drive rows
  varied between -0.3% and +0.8%. The paint-on/off relationship did not move,
  so this is host-run noise rather than a Stage 3 loss.
- Three matched PAL control runs against the frozen Stage 1 binary reduced
  warmed `indexed8` mean latency from **1.972 ms to 1.528 ms (-22.5%)** while
  `get-cpu` stayed flat at about 1.52 ms.

Stage 3's faster response generation exposed a pre-existing nonblocking socket
bug: a temporarily full send buffer was treated as disconnect. The write helper
now retries `EINTR` and waits (bounded to five seconds) for `EAGAIN` /
`EWOULDBLOCK`. The PAL frame-ring integration test covers the larger response.

## Files this touches

- `src/machine/vicii.c`, `src/machine/vicii.h` - paint path, palette, colour
  pipe, priority, SIMD helpers
- `src/machine/c64_frame.{c,h}` - native pixel contract, central palette,
  expansion and rotation
- `src/machine/c64_snapshot.c` - framebuffer reset after state load
- `src/runtime/runtime_thread.c`, `runtime_frame_ring.{c,h}` - copies, ring
  slot size
- `src/main.c` - native indexed responses and legacy ARGB expansion
- `src/frontend/frontend.c` - the single display expansion point
- `agents/vicii.md`, `agents/control-port.md` - update in the same change

## Non-goals

- Changing the palette itself, or adding palette variants.
- Any change to `.c64state`.
- Packing to 4 bits per pixel. Half the memory again, but it complicates every
  read and the VIC paints per dot; `indexed8` is the sane stopping point.
