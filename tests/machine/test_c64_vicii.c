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
    c64_config  cfg;
    char error[256];

    build_roms(&roms);
    c64_init(machine);

    /* PAL is the canonical video standard for all tests: the 384×272 pixel
       buffer matches PAL dimensions and border compare values (top=51, left=24). */
    cfg.video_standard = C64_VIDEO_STANDARD_PAL;
    c64_set_config(machine, &cfg);

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
        vicii_step_cycle(&v, NULL);
    }
    expect_u32("line cycle wraps", 0, v.timing.cycle_in_line);
    expect_u32("raster increments", 1, v.timing.raster_line);

    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE * (VICII_NTSC_LINES_PER_FRAME - 1); i++) {
        vicii_step_cycle(&v, NULL);
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
    /* PAL CSEL=1/RSEL=1: display window starts at x=24, y=51 */
    active = frame.pixels[(51 + 10) * C64_FRAME_WIDTH + (24 + 10)];
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
    /* Force YSCROLL=0 so glyph row 0 appears at the first display-window line (sy=0).
       0x18 = DEN=1, RSEL=1, YSCROLL=0. Without this, the default YSCROLL=3 would
       place glyph row 5 at sy=0, and the synthetic ROM has no data in row 5. */
    c64_bus_write(&machine.bus, 0xd011, 0x18);
    /* CSEL=1 so the display window left edge is at x=24 (default $D016=0 gives CSEL=0, left=31). */
    c64_bus_write(&machine.bus, 0xd016, 0x18);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;

    expect_u8("character rom glyph fetch", 0x80, c64_bus_vic_read_char_glyph(&machine.bus, 1, 0));
    expect_u8("screen ram fetch", 1, c64_bus_vic_read_screen(&machine.bus, 0));
    expect_u8("color ram fetch", 5, c64_bus_vic_read_color(&machine.bus, 0));

    expect_true("make glyph frame", c64_make_frame_snapshot(&machine, &frame_a));
    expect_true("make second glyph frame", c64_make_frame_snapshot(&machine, &frame_b));

    /* PAL CSEL=1/RSEL=1: display window starts at x=24, y=51.
       Character 1 glyph row 0=0x80 (bit 7 set) → foreground at sx=0 (x=24).
       Glyph row 1=0x40 (bit 6 set) → foreground at sx=1 (x=25) on next line. */
    glyph_pixel          = frame_a.pixels[51 * C64_FRAME_WIDTH + 24];
    background_pixel     = frame_a.pixels[51 * C64_FRAME_WIDTH + 25];
    next_row_glyph_pixel = frame_a.pixels[52 * C64_FRAME_WIDTH + 25];

    expect_u32("glyph foreground color", TEST_COLOR_GREEN, glyph_pixel);
    expect_u32("glyph background color", TEST_COLOR_BLUE, background_pixel);
    expect_u32("second glyph row foreground", TEST_COLOR_GREEN, next_row_glyph_pixel);
    expect_u32("deterministic glyph pixel",
        frame_a.pixels[51 * C64_FRAME_WIDTH + 24],
        frame_b.pixels[51 * C64_FRAME_WIDTH + 24]);
}

static void test_border_rsel_csel(void) {
    c64_t machine;
    c64_frame frame;
    uint32_t red;

    red = 0xff813338u; /* palette index 2 */

    /* RSEL=0: top border extends to y=54 (compare moves from 51 to 55).
       $D011 = 0x13: DEN=1, RSEL=0, YSCROLL=3. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x05);
    c64_bus_write(&machine.bus, 0xd011, 0x13);
    expect_true("make frame rsel0", c64_make_frame_snapshot(&machine, &frame));
    /* y=52 is inside the extended top border (RSEL=0 top=55, was 51) */
    expect_u32("rsel0 extended top border at y=52", red,
               frame.pixels[52 * C64_FRAME_WIDTH + 50]);
    /* y=56 is inside the display window with RSEL=0 (top=55 clears vborder) */
    expect_not_u32("rsel0 display at y=56", red,
                   frame.pixels[56 * C64_FRAME_WIDTH + 50]);

    /* CSEL=0: left border extends to x=30 (compare moves from 24 to 31).
       $D016 = 0x00: bit 3 (CSEL)=0 → 38 columns, left=31. XSCROLL=0. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x05);
    c64_bus_write(&machine.bus, 0xd016, 0x00);
    expect_true("make frame csel0", c64_make_frame_snapshot(&machine, &frame));
    /* x=26 is inside the extended left border (CSEL=0 left=31, was 24) */
    expect_u32("csel0 extended left border at x=26", red,
               frame.pixels[60 * C64_FRAME_WIDTH + 26]);
    /* x=32 is inside the display window with CSEL=0 */
    expect_not_u32("csel0 display at x=32", red,
                   frame.pixels[60 * C64_FRAME_WIDTH + 32]);
}

static void test_xscroll_shifts_content(void) {
    c64_t machine;
    c64_frame frame0, frame1;
    uint32_t green;

    green = 0xff56ac4du; /* palette index 5 */

    /* Character 1 glyph row 0 = 0x80: only bit 7 set → foreground only at sx=0.
       YSCROLL=0 so glyph row 0 is at sy=0 (y=51). */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x18); /* DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;
    expect_true("xscroll0 frame", c64_make_frame_snapshot(&machine, &frame0));

    c64_bus_write(&machine.bus, 0xd016, 0x19); /* XSCROLL=1 */
    expect_true("xscroll1 frame", c64_make_frame_snapshot(&machine, &frame1));

    /* XSCROLL=0: foreground at x=24 (left=24, sx=(24-24)+0=0, glyph bit 7 set) */
    expect_u32("xscroll0 fg at x=24", green, frame0.pixels[51 * C64_FRAME_WIDTH + 24]);

    /* XSCROLL=1: sx=(x-24)+1. At x=24: sx=1 → bit 6 of glyph=0 → background.
       The only set bit (bit 7) would need sx=0 → x=23, which is in the border.
       The foreground pixel is shifted off the left edge of the display window. */
    expect_not_u32("xscroll1 no fg at x=24", green, frame1.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_not_u32("xscroll1 no fg at x=25", green, frame1.pixels[51 * C64_FRAME_WIDTH + 25]);
}

static void test_yscroll_shifts_content(void) {
    c64_t machine;
    c64_frame frame0, frame1;
    uint32_t green;

    green = 0xff56ac4du; /* palette index 5 */

    /* Character 1 glyph row 0 = 0x80: foreground at sx=0 (x=24).
       YSCROLL=0: row_in_cell at sy=0 = (0+8-0)&7 = 0 → glyph row 0 at y=51. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x18); /* DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;
    expect_true("yscroll0 frame", c64_make_frame_snapshot(&machine, &frame0));

    c64_bus_write(&machine.bus, 0xd011, 0x19); /* DEN=1, RSEL=1, YSCROLL=1 */
    expect_true("yscroll1 frame", c64_make_frame_snapshot(&machine, &frame1));

    /* YSCROLL=0: glyph row 0 at y=51 */
    expect_u32("yscroll0 fg at y=51", green, frame0.pixels[51 * C64_FRAME_WIDTH + 24]);

    /* YSCROLL=1: sy=0 < yscroll=1 → background (first char row starts at sy=1).
       Glyph row 0 appears at adjusted=sy-yscroll=0 → sy=1 → y=52. */
    expect_not_u32("yscroll1 no fg at y=51", green, frame1.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("yscroll1 fg at y=52", green, frame1.pixels[52 * C64_FRAME_WIDTH + 24]);
}

int main(void) {
    test_vicii_reset_state();
    test_raster_progression();
    test_frame_snapshot_geometry_and_regions();
    test_reset_screen_starts_clear();
    test_character_rendering_uses_screen_char_rom_and_color_ram();
    test_border_rsel_csel();
    test_xscroll_shifts_content();
    test_yscroll_shifts_content();
    return 0;
}
