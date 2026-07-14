#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct frontend_crt_effects {
    bool scanlines;
    int scanline_strength;
    bool curvature;
    int curvature_amount;
} frontend_crt_effects;

/* Produces a full ARGB8888 frame, applying effects only inside the displayed
   crop. Source and destination must not overlap. */
void frontend_crt_process(
    const uint32_t *source,
    uint32_t *destination,
    int frame_width,
    int frame_height,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height,
    const frontend_crt_effects *effects);
