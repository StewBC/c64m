#include "c64.h"
#include "vicii.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
    TEST_COLOR_GREEN = 0xff56ac4du,
    TEST_COLOR_BLUE = 0xff2e2c9bu,
};

#define TEST_PALETTE_0   0xff000000u  /* black      */
#define TEST_PALETTE_5   0xff56ac4du  /* green      */
#define TEST_PALETTE_6   0xff2e2c9bu  /* blue       */
#define TEST_PALETTE_10  0xffc46c71u  /* light red  */
#define TEST_PALETTE_11  0xff4a4a4au  /* dark gray  */

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
    memset(&cfg, 0, sizeof(cfg));
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

static void test_irq_status_high_bit_reports_enabled_pending_irq(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd012, 0x00);
    vicii_step_cycle(&v, NULL);
    expect_u8("d019 pending disabled irq", 0x71, vicii_read_register(&v, 0xd019));

    vicii_write_register(&v, 0xd01a, 0x01);
    expect_u8("d019 pending enabled irq", 0xf1, vicii_read_register(&v, 0xd019));

    vicii_write_register(&v, 0xd019, 0x01);
    expect_u8("d019 cleared irq", 0x70, vicii_read_register(&v, 0xd019));
    expect_u8("d01a high nibble", 0xf1, vicii_read_register(&v, 0xd01a));
}

static void test_bad_line_ba_asserts_at_cycle_12(void) {
    vicii v;
    char error[256];
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_write_register(&v, 0xd011, 0x13); /* DEN=1, YSCROLL=3 */
    v.timing.raster_line = 0x33;

    for (i = 0; i < 12; i++) {
        expect_true("ba high before cycle 12", !vicii_ba_active(&v));
        vicii_step_cycle(&v, NULL);
    }

    vicii_step_cycle(&v, NULL);
    expect_true("ba asserts at cycle 12", vicii_ba_active(&v));

    while (v.timing.cycle_in_line <= 54) {
        expect_true("ba remains low through c-access window", vicii_ba_active(&v));
        vicii_step_cycle(&v, NULL);
    }

    vicii_step_cycle(&v, NULL);
    expect_true("ba released after cycle 54", !vicii_ba_active(&v));
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
    c64_bus_write(&machine.bus, 0xd016, 0x08);
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
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;
    expect_true("xscroll0 frame", c64_make_frame_snapshot(&machine, &frame0));

    c64_bus_write(&machine.bus, 0xd016, 0x09); /* CSEL=1, MCM=0, XSCROLL=1 */
    expect_true("xscroll1 frame", c64_make_frame_snapshot(&machine, &frame1));

    /* XSCROLL=0: foreground at x=24 (left=24, sx=0, glyph bit 7 set) */
    expect_u32("xscroll0 fg at x=24", green, frame0.pixels[51 * C64_FRAME_WIDTH + 24]);

    /* XSCROLL=1: output is delayed by one pixel, so the same glyph bit moves to x=25. */
    expect_not_u32("xscroll1 no fg at x=24", green, frame1.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("xscroll1 fg at x=25", green, frame1.pixels[51 * C64_FRAME_WIDTH + 25]);
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
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;
    expect_true("yscroll0 frame", c64_make_frame_snapshot(&machine, &frame0));

    c64_bus_write(&machine.bus, 0xd011, 0x19); /* DEN=1, RSEL=1, YSCROLL=1 */
    expect_true("yscroll1 frame", c64_make_frame_snapshot(&machine, &frame1));

    /* YSCROLL=0: glyph row 0 at y=51 */
    expect_u32("yscroll0 fg at y=51", green, frame0.pixels[51 * C64_FRAME_WIDTH + 24]);

    /* YSCROLL=1: sy=0 samples glyph row 7, and glyph row 0 appears at sy=1. */
    expect_not_u32("yscroll1 no fg at y=51", green, frame1.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("yscroll1 fg at y=52", green, frame1.pixels[52 * C64_FRAME_WIDTH + 24]);
}

static void test_ecm_text_mode(void) {
    c64_t    machine;
    c64_frame frame;
    uint32_t green, blue, cyan;

    green = TEST_PALETTE_5;
    blue  = TEST_PALETTE_6;
    cyan  = 0xff75cec8u; /* palette index 3 */

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, char=$0000 */
    /* ECM=1, DEN=1, RSEL=1, YSCROLL=0.  0x58 = 0101 1000 */
    c64_bus_write(&machine.bus, 0xd011, 0x58);
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */
    c64_bus_write(&machine.bus, 0xd022, 0x05); /* B1C = green */
    c64_bus_write(&machine.bus, 0xd024, 0x03); /* B3C = cyan */

    /* Cell 0: code=0x01 → ecm_sel=0 (B0C), char 1.  char[1*8+0]=0x80 → bit7 at sx=0. */
    machine.bus.ram[0x0400] = 0x01;
    machine.bus.color_ram[0] = 0x05; /* fg = green */

    /* Cell 1: code=0xC1 → ecm_sel=3 (B3C), char 1 (0xC1&0x3F=1).  Same glyph. */
    machine.bus.ram[0x0401] = 0xC1;
    machine.bus.color_ram[1] = 0x05; /* fg = green */

    expect_true("make ecm frame", c64_make_frame_snapshot(&machine, &frame));

    /* Cell 0, sx=0 (x=24): glyph bit7 set → fg (green) */
    expect_u32("ecm cell0 fg pixel", green, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* Cell 0, sx=1 (x=25): glyph bit6 clear → B0C (blue) */
    expect_u32("ecm cell0 bg is b0c", blue, frame.pixels[51 * C64_FRAME_WIDTH + 25]);

    /* Cell 1, sx=8 (x=32): glyph bit7 set → fg (green) */
    expect_u32("ecm cell1 fg pixel", green, frame.pixels[51 * C64_FRAME_WIDTH + 32]);
    /* Cell 1, sx=9 (x=33): glyph bit6 clear → B3C (cyan) */
    expect_u32("ecm cell1 bg is b3c", cyan, frame.pixels[51 * C64_FRAME_WIDTH + 33]);
}

static void test_standard_bitmap_mode(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, bitmap=$2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x38); /* BMM=1, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */

    machine.bus.ram[0x2000] = 0x80; /* cell 0, row 0: bit 7 set */
    machine.bus.ram[0x0400] = 0xAB; /* vm_byte: fg=palette[10], bg=palette[11] */

    expect_true("make bitmap frame", c64_make_frame_snapshot(&machine, &frame));

    /* sx=0 (x=24): bitmap bit7=1 → fg = palette[10] */
    expect_u32("bitmap fg at x=24", TEST_PALETTE_10, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* sx=1 (x=25): bitmap bit6=0 → bg = palette[11] */
    expect_u32("bitmap bg at x=25", TEST_PALETTE_11, frame.pixels[51 * C64_FRAME_WIDTH + 25]);
}

static void test_basic_hires_circle_setup_selects_bitmap_mode(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 59); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd018, 24); /* screen=$0400, bitmap=$2000 */

    /* The user's program clears bitmap RAM, but standard bitmap colors come from
       screen RAM. Make one bit visible as white-on-black so the mode switch is
       unambiguous. */
    machine.bus.ram[0x0400] = 0x10;
    machine.bus.ram[0x2000] = 0x80; /* YSCROLL=3: sy=3 maps to bitmap row 0. */

    expect_true("make basic hires setup frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("basic hires setup foreground", 0xffffffffu,
               frame.pixels[54 * C64_FRAME_WIDTH + 31]);
    expect_u32("basic hires setup background", TEST_PALETTE_0,
               frame.pixels[54 * C64_FRAME_WIDTH + 32]);
}

static void test_multicolor_bitmap_mode(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  blue;

    blue = TEST_PALETTE_6;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, bitmap=$2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x38); /* BMM=1, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */

    machine.bus.ram[0x2000] = 0xE4; /* pairs: (11)(10)(01)(00) at sx=0,2,4,6 */
    machine.bus.ram[0x0400] = 0xAB; /* vm_byte: high=0xA→palette[10], low=0xB→palette[11] */
    machine.bus.color_ram[0] = 0x05; /* pair 11 → palette[5] = green */

    expect_true("make mcm bitmap frame", c64_make_frame_snapshot(&machine, &frame));

    /* sx=0 (x=24): pair=3 → palette[color_ram[0]] = palette[5] = green */
    expect_u32("mcmbm pair11 at x=24", TEST_PALETTE_5, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* sx=2 (x=26): pair=2 → palette[vm_byte & 0x0F] = palette[0xB] = palette[11] */
    expect_u32("mcmbm pair10 at x=26", TEST_PALETTE_11, frame.pixels[51 * C64_FRAME_WIDTH + 26]);
    /* sx=4 (x=28): pair=1 → palette[(vm_byte>>4) & 0x0F] = palette[0xA] = palette[10] */
    expect_u32("mcmbm pair01 at x=28", TEST_PALETTE_10, frame.pixels[51 * C64_FRAME_WIDTH + 28]);
    /* sx=6 (x=30): pair=0 → B0C = blue */
    expect_u32("mcmbm pair00 at x=30", blue, frame.pixels[51 * C64_FRAME_WIDTH + 30]);
}

static void test_mcm_text_mode(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  green, blue;

    green = TEST_PALETTE_5;
    blue  = TEST_PALETTE_6;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, char=$0000 */
    c64_bus_write(&machine.bus, 0xd011, 0x18); /* DEN=1, RSEL=1, ECM=0, BMM=0, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */
    c64_bus_write(&machine.bus, 0xd023, 0x05); /* B2C = green */

    /* Cell 0: char 1, color_nib=5 (bit3=0 → hires). Glyph row 0=0x80: bit7 at sx=0. */
    machine.bus.ram[0x0400] = 0x01;
    machine.bus.color_ram[0] = 0x05;

    /* Cell 1: char 1, color_nib=0x0D (bit3=1 → MCM, color bits=5).
       Glyph row 0=0x80=1000 0000 → pairs: (10)(00)(00)(00).
       sx=8 (col 1 start, x=32): sx&6=0, pair=(0x80>>6)&3=2 → B2C = green.
       sx=10 (x=34): sx&6=2, pair=(0x80>>4)&3=0 → B0C = blue. */
    machine.bus.ram[0x0401] = 0x01;
    machine.bus.color_ram[1] = 0x0D;

    expect_true("make mcm text frame", c64_make_frame_snapshot(&machine, &frame));

    /* Cell 0 hires: sx=0 (x=24) → glyph bit7=1 → fg = palette[5] = green */
    expect_u32("mcm hires fg at x=24", green, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    /* Cell 0 hires: sx=1 (x=25) → glyph bit6=0 → B0C = blue */
    expect_u32("mcm hires bg at x=25", blue, frame.pixels[51 * C64_FRAME_WIDTH + 25]);

    /* Cell 1 multicolor: sx=8 (x=32) → pair=2 → B2C = green */
    expect_u32("mcm color pair2 at x=32", green, frame.pixels[51 * C64_FRAME_WIDTH + 32]);
    /* Cell 1 multicolor: sx=10 (x=34) → pair=0 → B0C = blue */
    expect_u32("mcm color pair0 at x=34", blue, frame.pixels[51 * C64_FRAME_WIDTH + 34]);
}

static void test_invalid_mode_forces_black(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  black, border_color;

    black = TEST_PALETTE_0; /* 0xff000000 */

    /* Mode 6: ECM=1, BMM=1 → $D011 bit6=1, bit5=1 */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x78); /* ECM=1, BMM=1, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* border = red (not black) */

    expect_true("make invalid frame", c64_make_frame_snapshot(&machine, &frame));

    border_color = frame.pixels[0]; /* top-left is always border */
    expect_not_u32("invalid border not black", black, border_color);

    /* Display window pixel: must be black */
    expect_u32("invalid display black at (51,24)", black,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("invalid display black at (100,100)", black,
               frame.pixels[100 * C64_FRAME_WIDTH + 100]);

    /* Mode 5: ECM=1, MCM=1 */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x58); /* ECM=1, BMM=0, DEN=1, RSEL=1, YSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02);

    expect_true("make mode5 frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("mode5 display black", black, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
}

int main(void) {
    test_vicii_reset_state();
    test_raster_progression();
    test_irq_status_high_bit_reports_enabled_pending_irq();
    test_bad_line_ba_asserts_at_cycle_12();
    test_frame_snapshot_geometry_and_regions();
    test_reset_screen_starts_clear();
    test_character_rendering_uses_screen_char_rom_and_color_ram();
    test_border_rsel_csel();
    test_xscroll_shifts_content();
    test_yscroll_shifts_content();
    test_ecm_text_mode();
    test_standard_bitmap_mode();
    test_basic_hires_circle_setup_selects_bitmap_mode();
    test_multicolor_bitmap_mode();
    test_mcm_text_mode();
    test_invalid_mode_forces_black();
    return 0;
}
