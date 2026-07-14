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

static uint32_t crt_crop_pixel(
    const uint32_t *source,
    int frame_width,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    int x,
    int y)
{
    if (x < 0 || y < 0 || x >= crop_width || y >= crop_height) {
        return 0xff000000u;
    }
    return source[(size_t)(crop_y + y) * (size_t)frame_width +
        (size_t)(crop_x + x)];
}

static unsigned int crt_bilinear_channel(
    uint32_t p00,
    uint32_t p10,
    uint32_t p01,
    uint32_t p11,
    unsigned int shift,
    float fx,
    float fy)
{
    float top = (float)((p00 >> shift) & 0xffu) * (1.0f - fx) +
        (float)((p10 >> shift) & 0xffu) * fx;
    float bottom = (float)((p01 >> shift) & 0xffu) * (1.0f - fx) +
        (float)((p11 >> shift) & 0xffu) * fx;
    return (unsigned int)(top * (1.0f - fy) + bottom * fy + 0.5f);
}

static uint32_t crt_bilinear_sample(
    const uint32_t *source,
    int frame_width,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    float x,
    float y)
{
    int x0 = (int)x;
    int y0 = (int)y;
    float fx;
    float fy;
    uint32_t p00;
    uint32_t p10;
    uint32_t p01;
    uint32_t p11;

    if (x < (float)x0) x0--;
    if (y < (float)y0) y0--;
    fx = x - (float)x0;
    fy = y - (float)y0;
    p00 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0, y0);
    p10 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0 + 1, y0);
    p01 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0, y0 + 1);
    p11 = crt_crop_pixel(source, frame_width, crop_x, crop_y,
        crop_width, crop_height, x0 + 1, y0 + 1);

    return
        (crt_bilinear_channel(p00, p10, p01, p11, 24, fx, fy) << 24) |
        (crt_bilinear_channel(p00, p10, p01, p11, 16, fx, fy) << 16) |
        (crt_bilinear_channel(p00, p10, p01, p11, 8, fx, fy) << 8) |
        crt_bilinear_channel(p00, p10, p01, p11, 0, fx, fy);
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
    int output_scale,
    const frontend_crt_effects *effects)
{
    int x;
    int y;
    int output_width;
    int output_height;
    int output_frame_width;
    int output_frame_height;
    int curvature;
    float curve;

    if (source == NULL || destination == NULL || effects == NULL ||
        frame_width <= 0 || frame_height <= 0 || crop_width <= 0 || crop_height <= 0 ||
        output_scale <= 0 ||
        crop_x < 0 || crop_y < 0 || crop_x + crop_width > frame_width ||
        crop_y + crop_height > frame_height) {
        return;
    }

    output_width = crop_width * output_scale;
    output_height = crop_height * output_scale;
    output_frame_width = frame_width * output_scale;
    output_frame_height = frame_height * output_scale;
    memset(destination, 0,
        (size_t)output_frame_width * (size_t)output_frame_height * sizeof(*destination));
    curvature = effects->curvature ? crt_clamp_percent(effects->curvature_amount) : 0;
    curve = (float)curvature * 0.0015f;

    for (y = 0; y < output_height; ++y) {
        float ny = ((float)y + 0.5f) * 2.0f / (float)output_height - 1.0f;
        for (x = 0; x < output_width; ++x) {
            float nx = ((float)x + 0.5f) * 2.0f / (float)output_width - 1.0f;
            float sxn = nx * (1.0f + curve * ny * ny);
            float syn = ny * (1.0f + curve * nx * nx);
            uint32_t pixel = 0xff000000u;

            if (sxn >= -1.0f && sxn < 1.0f && syn >= -1.0f && syn < 1.0f) {
                float sx = (sxn + 1.0f) * 0.5f * (float)crop_width - 0.5f;
                float sy = (syn + 1.0f) * 0.5f * (float)crop_height - 0.5f;
                pixel = crt_bilinear_sample(source, frame_width, crop_x, crop_y,
                    crop_width, crop_height, sx, sy);
                if (effects->scanlines && ((y / output_scale) & 1) != 0) {
                    pixel = crt_darken(pixel, effects->scanline_strength);
                }
            }
            destination[(size_t)(crop_y * output_scale + y) * (size_t)output_frame_width +
                (size_t)(crop_x * output_scale + x)] = pixel;
        }
    }
}
