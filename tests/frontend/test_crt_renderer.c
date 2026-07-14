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
    frontend_crt_process(source, destination, 4, 4, 0, 0, 4, 4, &effects);
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

    frontend_crt_process(source, destination, 2, 2, 0, 0, 2, 2, &effects);
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
    frontend_crt_process(source, destination, 16, 16, 0, 0, 16, 16, &effects);
    expect_pixel("curved corner", 0xff000000u, destination[0]);
    expect_pixel("curved center", 0xffffffffu, destination[8 * 16 + 8]);
}

int main(void)
{
    test_disabled_is_identity();
    test_scanline_strength();
    test_curvature_blacks_corners();
    return 0;
}
