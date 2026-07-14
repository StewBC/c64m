#include "crt_renderer.h"

#include <stddef.h>
#include <string.h>

static int crt_clamp_percent(int value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

static uint32_t crt_darken(uint32_t pixel, int strength)
{
    unsigned int factor = (unsigned int)(1000 - crt_clamp_percent(strength) * 8);
    unsigned int r = ((pixel >> 16) & 0xffu) * factor / 1000u;
    unsigned int g = ((pixel >> 8) & 0xffu) * factor / 1000u;
    unsigned int b = (pixel & 0xffu) * factor / 1000u;

    return (pixel & 0xff000000u) | (r << 16) | (g << 8) | b;
}

void frontend_crt_process(
    const uint32_t *source,
    uint32_t *destination,
    int frame_width,
    int frame_height,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    const frontend_crt_effects *effects)
{
    int x;
    int y;
    int curvature;
    float curve;

    if (source == NULL || destination == NULL || effects == NULL ||
        frame_width <= 0 || frame_height <= 0 || crop_width <= 0 || crop_height <= 0 ||
        crop_x < 0 || crop_y < 0 || crop_x + crop_width > frame_width ||
        crop_y + crop_height > frame_height) {
        return;
    }

    memcpy(destination, source,
        (size_t)frame_width * (size_t)frame_height * sizeof(*destination));
    curvature = effects->curvature ? crt_clamp_percent(effects->curvature_amount) : 0;
    curve = (float)curvature * 0.0015f;

    for (y = 0; y < crop_height; ++y) {
        float ny = ((float)y + 0.5f) * 2.0f / (float)crop_height - 1.0f;
        for (x = 0; x < crop_width; ++x) {
            float nx = ((float)x + 0.5f) * 2.0f / (float)crop_width - 1.0f;
            float sxn = nx * (1.0f + curve * ny * ny);
            float syn = ny * (1.0f + curve * nx * nx);
            uint32_t pixel = 0xff000000u;

            if (sxn >= -1.0f && sxn < 1.0f && syn >= -1.0f && syn < 1.0f) {
                int sx = (int)((sxn + 1.0f) * 0.5f * (float)crop_width);
                int sy = (int)((syn + 1.0f) * 0.5f * (float)crop_height);
                pixel = source[(size_t)(crop_y + sy) * (size_t)frame_width +
                    (size_t)(crop_x + sx)];
                if (effects->scanlines && (y & 1) != 0) {
                    pixel = crt_darken(pixel, effects->scanline_strength);
                }
            }
            destination[(size_t)(crop_y + y) * (size_t)frame_width +
                (size_t)(crop_x + x)] = pixel;
        }
    }
}
