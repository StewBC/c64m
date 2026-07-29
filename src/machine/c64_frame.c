#include "c64_frame.h"

#include <stddef.h>

const uint32_t c64_palette_argb[C64_FRAME_PALETTE_SIZE] = {
    0xff000000u, 0xffffffffu, 0xff813338u, 0xff75cec8u,
    0xff8e3c97u, 0xff56ac4du, 0xff2e2c9bu, 0xffedf171u,
    0xff8e5029u, 0xff553800u, 0xffc46c71u, 0xff4a4a4au,
    0xff7b7b7bu, 0xffa9ff9fu, 0xff706debu, 0xffb2b2b2u,
};

bool c64_frame_expand_argb(
    const c64_frame *frame,
    uint32_t *destination,
    uint32_t destination_stride_pixels,
    uint32_t rotate_x)
{
    uint32_t y;

    if (frame == NULL || destination == NULL ||
        frame->width == 0u || frame->width > C64_FRAME_WIDTH ||
        frame->height > C64_FRAME_HEIGHT ||
        frame->stride_bytes != C64_FRAME_WIDTH ||
        frame->pixel_format != C64_FRAME_PIXEL_FORMAT_INDEXED8 ||
        destination_stride_pixels < C64_FRAME_WIDTH) {
        return false;
    }
    rotate_x %= frame->width;
    for (y = 0; y < frame->height; ++y) {
        const uint8_t *src =
            frame->pixels + (size_t)y * (size_t)frame->stride_bytes;
        uint32_t *dst =
            destination + (size_t)y * (size_t)destination_stride_pixels;
        uint32_t x;
        for (x = 0; x < frame->width; ++x) {
            dst[x] = c64_frame_pixel_to_argb(
                src[(rotate_x + x) % frame->width]);
        }
        for (; x < C64_FRAME_WIDTH; ++x) {
            dst[x] = 0u;
        }
    }
    return true;
}
