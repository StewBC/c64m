#include "c64.h"
#include "vicii.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
    TEST_COLOR_GREEN = 0xff56ac4du,
    TEST_COLOR_BLUE = 0xff2e2c9bu,
};

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
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

static void expect_not_u32(const char *name, uint32_t unexpected, uint32_t actual) {
    if (unexpected == actual) {
        fprintf(stderr, "%s: expected value other than %u\n", name, unexpected);
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
    roms->character[1 * 8 + 0] = 0x80;
    roms->character[1 * 8 + 1] = 0x40;
    roms->character[1 * 8 + 2] = 0x20;
    roms->character[1 * 8 + 3] = 0x10;
}

static void reset_machine(c64_t *machine) {
    c64_rom_set roms;
    char error[256];

    build_roms(&roms);
    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void test_vicii_reset_state(void) {
    vicii v;
    c64_vicii_snapshot snapshot;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_reset(&v);
    vicii_copy_snapshot(&v, &snapshot);

    expect_u32("reset raster line", 0, snapshot.raster_line);
    expect_u32("reset cycle in line", 0, snapshot.cycle_in_line);
    expect_u64("reset frame number", 0, snapshot.frame_number);
    expect_u8("reset border color", 6, snapshot.border_color);
    expect_u8("reset background color", 14, snapshot.background_color);
}

static void test_raster_progression(void) {
    vicii v;
    char error[256];
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE; i++) {
        vicii_step_cycle(&v);
    }
    expect_u32("line cycle wraps", 0, v.timing.cycle_in_line);
    expect_u32("raster increments", 1, v.timing.raster_line);

    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE * (VICII_NTSC_LINES_PER_FRAME - 1); i++) {
        vicii_step_cycle(&v);
    }
    expect_u64("frame increments", 1, v.timing.frame_number);
    expect_true("frame complete set", v.timing.frame_complete);
    expect_true("frame complete consumed", vicii_consume_frame_complete(&v));
    expect_true("frame complete cleared", !vicii_consume_frame_complete(&v));
}

static void test_frame_snapshot_geometry_and_regions(void) {
    c64_t machine;
    c64_frame frame;
    uint32_t corner;
    uint32_t active;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x05);

    expect_true("make frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("frame width", C64_FRAME_WIDTH, frame.width);
    expect_u32("frame height", C64_FRAME_HEIGHT, frame.height);
    expect_u32("frame stride", C64_FRAME_WIDTH * sizeof(frame.pixels[0]), frame.stride_bytes);
    expect_u32("frame format", C64_FRAME_PIXEL_FORMAT_ARGB8888, frame.pixel_format);
    expect_u64("frame number", 0, frame.frame_number);
    expect_u64("frame cycle", 0, frame.machine_cycle);

    corner = frame.pixels[0];
    active = frame.pixels[(VICII_ACTIVE_Y + 10) * C64_FRAME_WIDTH + VICII_ACTIVE_X + 10];
    expect_true("corner is visible border", corner != 0);
    expect_not_u32("active differs from border", corner, active);
}

static void test_reset_screen_starts_clear(void) {
    c64_t machine;

    reset_machine(&machine);

    expect_u8("reset screen byte", 0, c64_bus_vic_read_screen(&machine.bus, 0));
    expect_u8("reset color byte", 0, c64_bus_vic_read_color(&machine.bus, 0));
    expect_u8("reset later screen byte", 0, c64_bus_vic_read_screen(&machine.bus, 39));
}

static void test_character_rendering_uses_screen_char_rom_and_color_ram(void) {
    c64_t machine;
    c64_frame frame_a;
    c64_frame frame_b;
    uint32_t glyph_pixel;
    uint32_t background_pixel;
    uint32_t next_row_glyph_pixel;

    reset_machine(&machine);

    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;

    expect_u8("character rom glyph fetch", 0x80, c64_bus_vic_read_char_glyph(&machine.bus, 1, 0));
    expect_u8("screen ram fetch", 1, c64_bus_vic_read_screen(&machine.bus, 0));
    expect_u8("color ram fetch", 5, c64_bus_vic_read_color(&machine.bus, 0));

    expect_true("make glyph frame", c64_make_frame_snapshot(&machine, &frame_a));
    expect_true("make second glyph frame", c64_make_frame_snapshot(&machine, &frame_b));

    glyph_pixel = frame_a.pixels[VICII_ACTIVE_Y * C64_FRAME_WIDTH + VICII_ACTIVE_X];
    background_pixel = frame_a.pixels[VICII_ACTIVE_Y * C64_FRAME_WIDTH + VICII_ACTIVE_X + 1];
    next_row_glyph_pixel = frame_a.pixels[(VICII_ACTIVE_Y + 1) * C64_FRAME_WIDTH + VICII_ACTIVE_X + 1];

    expect_u32("glyph foreground color", TEST_COLOR_GREEN, glyph_pixel);
    expect_u32("glyph background color", TEST_COLOR_BLUE, background_pixel);
    expect_u32("second glyph row foreground", TEST_COLOR_GREEN, next_row_glyph_pixel);
    expect_u32("deterministic glyph pixel", frame_a.pixels[VICII_ACTIVE_Y * C64_FRAME_WIDTH + VICII_ACTIVE_X],
        frame_b.pixels[VICII_ACTIVE_Y * C64_FRAME_WIDTH + VICII_ACTIVE_X]);
}

int main(void) {
    test_vicii_reset_state();
    test_raster_progression();
    test_frame_snapshot_geometry_and_regions();
    test_reset_screen_starts_clear();
    test_character_rendering_uses_screen_char_rom_and_color_ram();
    return 0;
}
