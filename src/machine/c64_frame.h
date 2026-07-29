#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    /* Paint width equals the full VIC-II raster line: every dot the chip clocks
       out, HBLANK included (6569: 63 cycles x 8 = 504; 6567: 65 x 8 = 520). The
       buffer therefore holds the whole line in VIC-X order with no crop and no
       origin offset - framebuffer x IS VIC X, for every x. Windowing is entirely
       a frontend decision, which is what keeps the left border (VIC X 496..503
       on PAL, chronologically just before the wrap to 0) representable at all.
       This mirrors VICE's per-line draw buffer (VICII_DRAW_BUFFER_SIZE = 65*8),
       so a c64m line and a VICE line are directly comparable dot for dot. */
    C64_FRAME_PAL_WIDTH = 504,
    C64_FRAME_NTSC_WIDTH = 520,
    /* Row stride of the pixel array: the longer of the two lines, so one buffer
       shape serves both standards. frame->width carries the standard's real
       line length and is NOT the row pitch - index rows by C64_FRAME_WIDTH (or
       frame->stride_bytes), never by frame->width. */
    C64_FRAME_WIDTH = C64_FRAME_NTSC_WIDTH,
    /* PAL paint height equals the full VIC-II raster (6569: 312 lines, 0..311).
       Timing and paint coverage match so demo/border effects are never clipped
       by a short pixel buffer. Frontend still crops for normal display.
       NTSC paint height remains the full short frame (263). */
    C64_FRAME_PAL_HEIGHT = 312,
    C64_FRAME_NTSC_HEIGHT = 263,
    C64_FRAME_HEIGHT = C64_FRAME_PAL_HEIGHT,
    C64_FRAME_PIXEL_FORMAT_ARGB8888 = 1,
    C64_FRAME_PIXEL_FORMAT_INDEXED8 = 2,
    C64_FRAME_PALETTE_SIZE = 16,
    /* Internal-only marker for row padding and not-yet-painted pixels. Wire
       indexed8 maps it to index 0; ARGB expansion maps it to transparent zero,
       preserving the pre-indexed framebuffer behavior. Completed visible
       pixels must always be palette indices 0..15. */
    C64_FRAME_PIXEL_UNPAINTED = 0xff,
};

/* Pepto ARGB palette used at host presentation/control boundaries. */
extern const uint32_t c64_palette_argb[C64_FRAME_PALETTE_SIZE];

static inline uint32_t c64_frame_pixel_to_argb(uint8_t pixel)
{
    return pixel < C64_FRAME_PALETTE_SIZE ? c64_palette_argb[pixel] : 0u;
}

static inline uint8_t c64_frame_pixel_to_index(uint8_t pixel)
{
    return pixel < C64_FRAME_PALETTE_SIZE ? pixel : 0u;
}

/* Frames cross the runtime/frontend boundary by value. Visible completed-frame
   pixels are native VIC-II palette indices 0..15. */
typedef struct c64_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    uint64_t frame_number;
    uint64_t machine_cycle;
    uint8_t pixels[C64_FRAME_WIDTH * C64_FRAME_HEIGHT];
} c64_frame;

/* Expand a native indexed frame into a fixed-pitch ARGB buffer. rotate_x is a
   presentation-only cyclic origin within frame->width; pass zero for VIC-X
   order. Columns from frame->width to C64_FRAME_WIDTH become transparent zero,
   preserving legacy PAL row padding. */
bool c64_frame_expand_argb(
    const c64_frame *frame,
    uint32_t *destination,
    uint32_t destination_stride_pixels,
    uint32_t rotate_x);
