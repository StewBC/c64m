#pragma once

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
};

/* Frames cross the runtime/frontend boundary by value. Pixels are ARGB8888. */
typedef struct c64_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    uint64_t frame_number;
    uint64_t machine_cycle;
    uint32_t pixels[C64_FRAME_WIDTH * C64_FRAME_HEIGHT];
} c64_frame;
