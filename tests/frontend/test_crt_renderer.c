#include "crt_renderer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_pixel(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %08x, got %08x\n",
            name, (unsigned int)expected, (unsigned int)actual);
        exit(1);
    }
}

static void test_disabled_is_identity(void)
{
    uint32_t source[16];
    uint32_t destination[16];
    frontend_crt_effects effects = {0};
    int i;

    for (i = 0; i < 16; ++i) source[i] = 0xff000000u | (uint32_t)i;
    memset(destination, 0, sizeof(destination));
    frontend_crt_process(source, destination, 4, 4, 0, 0, 4, 4, 1, &effects);
    if (memcmp(source, destination, sizeof(source)) != 0) {
        fprintf(stderr, "disabled CRT processing changed the frame\n");
        exit(1);
    }
}

static void test_scanline_strength(void)
{
    uint32_t source[4] = {
        0xffc86432u, 0xffc86432u,
        0xffc86432u, 0xffc86432u,
    };
    uint32_t destination[4] = {0};
    frontend_crt_effects effects = {
        .scanlines = true,
        .scanline_strength = 50,
    };

    frontend_crt_process(source, destination, 2, 2, 0, 0, 2, 2, 1, &effects);
    expect_pixel("bright scanline", 0xffc86432u, destination[0]);
    expect_pixel("dark scanline", 0xff783c1eu, destination[2]);
}

static void test_curvature_blacks_corners(void)
{
    uint32_t source[256];
    uint32_t destination[256];
    frontend_crt_effects effects = {
        .curvature = true,
        .curvature_amount = 100,
    };
    int i;

    for (i = 0; i < 256; ++i) source[i] = 0xffffffffu;
    frontend_crt_process(source, destination, 16, 16, 0, 0, 16, 16, 1, &effects);
    expect_pixel("curved corner", 0xff000000u, destination[0]);
    expect_pixel("curved center", 0xffffffffu, destination[8 * 16 + 8]);
}

static void test_curvature_interpolates_warped_edges(void)
{
    uint32_t source[32 * 32];
    uint32_t destination[64 * 64];
    frontend_crt_effects effects = {
        .curvature = true,
        .curvature_amount = 15,
    };
    int found_intermediate = 0;
    int x;
    int y;

    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 32; ++x) {
            source[y * 32 + x] = x < 16 ? 0xff000000u : 0xffffffffu;
        }
    }
    frontend_crt_process(source, destination, 32, 32, 0, 0, 32, 32, 2, &effects);
    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 64; ++x) {
            uint32_t rgb = destination[y * 64 + x] & 0x00ffffffu;
            if (rgb != 0u && rgb != 0x00ffffffu) {
                found_intermediate = 1;
            }
        }
    }
    if (!found_intermediate) {
        fprintf(stderr, "curvature did not interpolate a warped edge\n");
        exit(1);
    }
}

int main(void)
{
    test_disabled_is_identity();
    test_scanline_strength();
    test_curvature_blacks_corners();
    test_curvature_interpolates_warped_edges();
    return 0;
}
