#include "c64.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
};

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u32(const char *name, uint32_t expected, uint32_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %llu, got %llu\n", name, (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

static void build_roms(c64_rom_set *roms) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffc] = (uint8_t)(TEST_RESET_VECTOR & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(TEST_RESET_VECTOR >> 8);
    roms->kernal[TEST_RESET_VECTOR - 0xe000] = 0xea;
}

static void reset_machine(c64_t *machine) {
    c64_rom_set roms;
    char error[256];

    build_roms(&roms);
    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static bool pixels_all_zero(const c64_frame *frame) {
    size_t i;

    for (i = 0; i < C64_FRAME_WIDTH * C64_FRAME_HEIGHT; i++) {
        if (frame->pixels[i] != 0) {
            return false;
        }
    }

    return true;
}

static void test_generate_frame(void) {
    c64_t machine;
    c64_frame frame;

    reset_machine(&machine);
    expect_true("generate test frame", c64_generate_test_frame(&machine, &frame));

    expect_u32("frame width", C64_FRAME_WIDTH, frame.width);
    expect_u32("frame height", C64_FRAME_HEIGHT, frame.height);
    expect_u32("frame stride", C64_FRAME_WIDTH * sizeof(frame.pixels[0]), frame.stride_bytes);
    expect_u32("frame pixel format", C64_FRAME_PIXEL_FORMAT_ARGB8888, frame.pixel_format);
    expect_u64("frame number", 0, frame.frame_number);
    expect_u64("frame machine cycle", 0, frame.machine_cycle);
    expect_true("frame has visible pixels", !pixels_all_zero(&frame));
}

static void test_frame_copy_stays_stable(void) {
    c64_t machine;
    c64_frame first;
    c64_frame second;
    uint32_t saved_pixel;

    reset_machine(&machine);
    expect_true("generate first frame", c64_generate_test_frame(&machine, &first));
    saved_pixel = first.pixels[40 * C64_FRAME_WIDTH + 40];
    expect_true("generate second frame", c64_generate_test_frame(&machine, &second));

    expect_u64("second frame number", 0, second.frame_number);
    expect_u64("first frame number stable", 0, first.frame_number);
    expect_u32("first frame pixel stable", saved_pixel, first.pixels[40 * C64_FRAME_WIDTH + 40]);
}

int main(void) {
    test_generate_frame();
    test_frame_copy_stays_stable();
    return 0;
}
