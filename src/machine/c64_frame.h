#pragma once

#include <stdint.h>

enum {
    C64_FRAME_WIDTH = 384,
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
