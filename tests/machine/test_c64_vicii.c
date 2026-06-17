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
#define TEST_PALETTE_2   0xff813338u  /* red        */
#define TEST_PALETTE_5   0xff56ac4du  /* green      */
#define TEST_PALETTE_6   0xff2e2c9bu  /* blue       */
#define TEST_PALETTE_7   0xffedf171u  /* yellow     */
#define TEST_PALETTE_10  0xffc46c71u  /* light red  */
#define TEST_PALETTE_11  0xff4a4a4au  /* dark gray  */

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
    roms->character[1 * 8 + 7] = 0x80;
}

static void copy_to_kernal(c64_rom_set *roms, uint16_t address, const uint8_t *program, size_t size) {
    size_t offset = address - 0xe000u;
    size_t i;

    if (address < 0xe000u || offset + size > C64_KERNAL_ROM_SIZE) {
        fail("test program does not fit in KERNAL ROM");
    }

    for (i = 0; i < size; i++) {
        roms->kernal[offset + i] = program[i];
    }
}

static void reset_machine_with_roms(c64_t *machine, const c64_rom_set *roms) {
    c64_config  cfg;
    char error[256];

    c64_init(machine);

    /* PAL is the canonical video standard for all tests: the 384×272 pixel
       buffer matches PAL dimensions and border compare values (top=51, left=24). */
    memset(&cfg, 0, sizeof(cfg));
    cfg.video_standard = C64_VIDEO_STANDARD_PAL;
    c64_set_config(machine, &cfg);

    expect_true("install synthetic ROMs", c64_install_roms(machine, roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void reset_machine(c64_t *machine) {
    c64_rom_set roms;

    build_roms(&roms);
    reset_machine_with_roms(machine, &roms);
}

static void step_until_frame_complete(c64_t *machine) {
    char error[256];
    uint32_t i;
    uint32_t max_cycles = VICII_PAL_CYCLES_PER_LINE * VICII_PAL_LINES_PER_FRAME + 1000u;

    for (i = 0; i < max_cycles; i++) {
        if (c64_consume_frame_complete(machine)) {
            return;
        }
        expect_true("step live frame cycle", c64_step_cycle(machine, error, sizeof(error)));
    }

    fail("live frame did not complete");
}

static void make_live_frame(c64_t *machine, c64_frame *frame, const char *name) {
    (void)c64_consume_frame_complete(machine);
    step_until_frame_complete(machine);
    expect_true(name, c64_make_frame_snapshot(machine, frame));
}

static void setup_solid_sprite(c64_t *machine, int sprite, uint16_t data_addr, uint16_t x, uint8_t y, uint8_t color) {
    int i;
    uint8_t enable;

    for (i = 0; i < 63; i++) {
        machine->bus.ram[(uint16_t)(data_addr + (uint16_t)i)] = 0xffu;
    }

    machine->bus.ram[0x07f8u + (uint16_t)sprite] = (uint8_t)(data_addr / 64u);
    c64_bus_write(&machine->bus, (uint16_t)(0xd000u + (uint16_t)sprite * 2u), (uint8_t)(x & 0xffu));
    c64_bus_write(&machine->bus, (uint16_t)(0xd001u + (uint16_t)sprite * 2u), y);
    if (x & 0x100u) {
        c64_bus_write(&machine->bus, 0xd010, (uint8_t)(machine->vic.registers[0x10] | (uint8_t)(1u << sprite)));
    } else {
        c64_bus_write(&machine->bus, 0xd010, (uint8_t)(machine->vic.registers[0x10] & (uint8_t)~(uint8_t)(1u << sprite)));
    }
    c64_bus_write(&machine->bus, (uint16_t)(0xd027u + (uint16_t)sprite), color);

    enable = (uint8_t)(machine->vic.registers[0x15] | (uint8_t)(1u << sprite));
    c64_bus_write(&machine->bus, 0xd015, enable);
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

static void test_sprite_collision_registers_read_clear(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd01b, 0xa5);
    expect_u8("d01b priority readback", 0xa5, vicii_read_register(&v, 0xd01b));

    v.sprite_sprite_collision = 0x03;
    expect_u8("d01e first read", 0x03, vicii_read_register(&v, 0xd01e));
    expect_u8("d01e clears on read", 0x00, vicii_read_register(&v, 0xd01e));
    vicii_write_register(&v, 0xd01e, 0xff);
    expect_u8("d01e write ignored", 0x00, vicii_read_register(&v, 0xd01e));

    v.sprite_background_collision = 0x04;
    expect_u8("d01f first read", 0x04, vicii_read_register(&v, 0xd01f));
    expect_u8("d01f clears on read", 0x00, vicii_read_register(&v, 0xd01f));
    vicii_write_register(&v, 0xd01f, 0xff);
    expect_u8("d01f write ignored", 0x00, vicii_read_register(&v, 0xd01f));
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
    /* Normal startup geometry: DEN=1, RSEL=1, YSCROLL=3. */
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
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
       YSCROLL=3 so glyph row 0 is at sy=0 (y=51). */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
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
    c64_frame frame3, frame4;
    uint32_t green;

    green = 0xff56ac4du; /* palette index 5 */

    /* Character 1 glyph row 0 = 0x80: foreground at sx=0 (x=24).
       YSCROLL=3 is the normal alignment: glyph row 0 appears at y=51. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400]  = 1;
    machine.bus.color_ram[0] = 5;
    expect_true("yscroll3 frame", c64_make_frame_snapshot(&machine, &frame3));

    c64_bus_write(&machine.bus, 0xd011, 0x1c); /* DEN=1, RSEL=1, YSCROLL=4 */
    expect_true("yscroll4 frame", c64_make_frame_snapshot(&machine, &frame4));

    /* YSCROLL=3: glyph row 0 at y=51 */
    expect_u32("yscroll3 fg at y=51", green, frame3.pixels[51 * C64_FRAME_WIDTH + 24]);

    /* YSCROLL=4: output is delayed by one raster line, so glyph row 0 moves to y=52. */
    expect_not_u32("yscroll4 no fg at y=51", green, frame4.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("yscroll4 fg at y=52", green, frame4.pixels[52 * C64_FRAME_WIDTH + 24]);
}

static void test_default_yscroll_fills_display_rows(void) {
    c64_t machine;
    c64_frame frame;
    uint32_t green;

    green = TEST_PALETTE_5;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    machine.bus.ram[0x0400 + 24 * 40] = 1;
    machine.bus.color_ram[24 * 40] = 5;

    expect_true("default yscroll frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("default yscroll top row starts at display top",
               green, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("default yscroll bottom row reaches display bottom",
               green, frame.pixels[250 * C64_FRAME_WIDTH + 24]);
}

static void test_ecm_text_mode(void) {
    c64_t    machine;
    c64_frame frame;
    uint32_t green, blue, cyan;

    green = TEST_PALETTE_5;
    blue  = TEST_PALETTE_6;
    cyan  = 0xff75cec8u; /* palette index 3 */

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x15); /* screen=$0400, char=$1000 (ROM) */
    /* ECM=1, DEN=1, RSEL=1, YSCROLL=3. */
    c64_bus_write(&machine.bus, 0xd011, 0x5b);
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

    make_live_frame(&machine, &frame, "make live ecm frame");

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
    c64_bus_write(&machine.bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */

    machine.bus.ram[0x2000] = 0x80; /* cell 0, row 0: bit 7 set */
    machine.bus.ram[0x0400] = 0xAB; /* vm_byte: fg=palette[10], bg=palette[11] */

    make_live_frame(&machine, &frame, "make live bitmap frame");

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
    machine.bus.ram[0x2000] = 0x80;

    make_live_frame(&machine, &frame, "make live basic hires setup frame");
    expect_u32("basic hires setup foreground", 0xffffffffu,
               frame.pixels[51 * C64_FRAME_WIDTH + 31]);
    expect_u32("basic hires setup background", TEST_PALETTE_0,
               frame.pixels[51 * C64_FRAME_WIDTH + 32]);
}

static void test_multicolor_bitmap_mode(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  blue;

    blue = TEST_PALETTE_6;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen=$0400, bitmap=$2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = blue */

    machine.bus.ram[0x2000] = 0xE4; /* pairs: (11)(10)(01)(00) at sx=0,2,4,6 */
    machine.bus.ram[0x0400] = 0xAB; /* vm_byte: high=0xA→palette[10], low=0xB→palette[11] */
    machine.bus.color_ram[0] = 0x05; /* pair 11 → palette[5] = green */

    make_live_frame(&machine, &frame, "make live mcm bitmap frame");

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
    c64_bus_write(&machine.bus, 0xd018, 0x15); /* screen=$0400, char=$1000 (ROM) */
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, ECM=0, BMM=0, YSCROLL=3 */
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

    make_live_frame(&machine, &frame, "make live mcm text frame");

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

    make_live_frame(&machine, &frame, "make live invalid frame");

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

    make_live_frame(&machine, &frame, "make live mode5 frame");
    expect_u32("mode5 display black", black, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
}

static void test_live_raster_border_change_and_text(void) {
    static const uint8_t program[] = {
        0xa9, 0x05,       /* LDA #$05 */
        0x8d, 0x20, 0xd0, /* STA $D020 */
        0x4c, 0x05, 0xe0  /* JMP $E005 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_frame frame;

    build_roms(&roms);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine_with_roms(&machine, &roms);

    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* blue background */
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;

    step_until_frame_complete(&machine);
    expect_true("live frame snapshot", c64_make_frame_snapshot(&machine, &frame));

    expect_u32("live border before d020 write", TEST_PALETTE_6, frame.pixels[8]);
    expect_u32("live border after d020 write", TEST_PALETTE_5, frame.pixels[32]);
    expect_u32("live standard text foreground", TEST_PALETTE_5, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("live standard text background", TEST_PALETTE_6, frame.pixels[51 * C64_FRAME_WIDTH + 25]);
}

static void test_sprite_hires_appears_at_position(void) {
    c64_t     machine;
    c64_frame frame;
    int       i;
    /* palette[7] = yellow */
    uint32_t  sprite_color = 0xffedf171u;
    uint32_t  px;

    reset_machine(&machine);

    /* sprite data at $0340 (832): solid 24×21 block */
    for (i = 0; i < 63; i++) {
        machine.bus.ram[0x0340 + i] = 0xFFu;
    }
    /* sprite 0 pointer at $07F8: value 13 → data at 13×64=$0340 */
    machine.bus.ram[0x07F8] = 13;

    /* sprite 0: X=100, Y=99 displays its first visible row at raster 100. */
    c64_bus_write(&machine.bus, 0xD000, 100);
    c64_bus_write(&machine.bus, 0xD001, 99);
    c64_bus_write(&machine.bus, 0xD027, 7);
    c64_bus_write(&machine.bus, 0xD015, 1);

    make_live_frame(&machine, &frame, "sprite hires live frame");

    px = frame.pixels[100 * C64_FRAME_WIDTH + 100];
    if (px != sprite_color) {
        fprintf(stderr,
            "sprite at (100,100): got 0x%08x, expected 0x%08x\n",
            px, sprite_color);
        exit(1);
    }
    px = frame.pixels[100 * C64_FRAME_WIDTH + 123];
    if (px != sprite_color) {
        fprintf(stderr,
            "sprite at (123,100): got 0x%08x, expected 0x%08x\n",
            px, sprite_color);
        exit(1);
    }
}

static void test_sprite_y50_touches_top_border_fully_revealed(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 46, 50, 7);

    make_live_frame(&machine, &frame, "sprite y50 top border frame");

    expect_u32("sprite y50 does not draw in top border", TEST_PALETTE_2,
               frame.pixels[50 * C64_FRAME_WIDTH + 46]);
    expect_u32("sprite y50 first visible row touches display top", TEST_PALETTE_7,
               frame.pixels[51 * C64_FRAME_WIDTH + 46]);
    expect_u32("sprite y50 last row fully visible", TEST_PALETTE_7,
               frame.pixels[71 * C64_FRAME_WIDTH + 46]);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 46, 51, 7);

    make_live_frame(&machine, &frame, "sprite y51 one-line gap frame");
    expect_u32("sprite y51 leaves one display line above", TEST_PALETTE_6,
               frame.pixels[51 * C64_FRAME_WIDTH + 46]);
    expect_u32("sprite y51 starts on following line", TEST_PALETTE_7,
               frame.pixels[52 * C64_FRAME_WIDTH + 46]);
}

static void test_sprite_sprite_collision_priority_and_irq(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    setup_solid_sprite(&machine, 0, 0x0340, 100, 99, 7);
    setup_solid_sprite(&machine, 1, 0x0380, 100, 99, 10);
    c64_bus_write(&machine.bus, 0xd01a, 0x04); /* enable IMMC */

    make_live_frame(&machine, &frame, "sprite-sprite collision frame");

    expect_u32("sprite 0 wins visual priority", TEST_PALETTE_7,
               frame.pixels[100 * C64_FRAME_WIDTH + 100]);
    expect_u8("immc irq pending", 0xf5, vicii_read_register(&machine.vic, 0xd019));

    /* Clearing $D019 while $D01E remains latched must allow a later collision IRQ. */
    vicii_write_register(&machine.vic, 0xd019, 0x04);
    expect_u8("immc irq cleared", 0x71, vicii_read_register(&machine.vic, 0xd019));

    make_live_frame(&machine, &frame, "sprite-sprite collision retrigger frame");
    expect_u8("immc irq retriggered", 0xf5, vicii_read_register(&machine.vic, 0xd019));
    expect_u8("d01e participants", 0x03, vicii_read_register(&machine.vic, 0xd01e));
    expect_u8("d01e read clear after live collision", 0x00, vicii_read_register(&machine.vic, 0xd01e));
}

static void test_sprite_background_priority_and_collision(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    c64_bus_write(&machine.bus, 0xd01a, 0x02); /* enable IMBC */
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    setup_solid_sprite(&machine, 0, 0x0340, 24, 50, 7);

    make_live_frame(&machine, &frame, "front sprite over text frame");
    expect_u32("front sprite hides foreground text", TEST_PALETTE_7,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u8("imbc irq pending", 0xf3, vicii_read_register(&machine.vic, 0xd019));
    expect_u8("d01f front sprite collides with foreground", 0x01, vicii_read_register(&machine.vic, 0xd01f));

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    setup_solid_sprite(&machine, 0, 0x0340, 24, 50, 7);
    c64_bus_write(&machine.bus, 0xd01b, 0x01); /* sprite 0 behind foreground graphics */

    make_live_frame(&machine, &frame, "behind sprite over text frame");
    expect_u32("foreground text hides behind sprite", TEST_PALETTE_5,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u8("behind sprite still collides with foreground", 0x01, vicii_read_register(&machine.vic, 0xd01f));

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 100, 99, 7);
    c64_bus_write(&machine.bus, 0xd01b, 0x01);

    make_live_frame(&machine, &frame, "behind sprite over background frame");
    expect_u32("behind sprite visible over background", TEST_PALETTE_7,
               frame.pixels[100 * C64_FRAME_WIDTH + 100]);
    expect_u8("no sprite-background collision on background pixel", 0x00, vicii_read_register(&machine.vic, 0xd01f));
}

static void test_border_hides_sprites_but_collision_latches(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    setup_solid_sprite(&machine, 0, 0x0340, 10, 9, 7);
    setup_solid_sprite(&machine, 1, 0x0380, 10, 9, 10);

    make_live_frame(&machine, &frame, "border sprite collision frame");

    expect_u32("border hides sprite pixel", TEST_PALETTE_2,
               frame.pixels[10 * C64_FRAME_WIDTH + 10]);
    expect_u8("sprite collision latches under border", 0x03, vicii_read_register(&machine.vic, 0xd01e));
}

int main(void) {
    test_vicii_reset_state();
    test_raster_progression();
    test_irq_status_high_bit_reports_enabled_pending_irq();
    test_sprite_collision_registers_read_clear();
    test_bad_line_ba_asserts_at_cycle_12();
    test_frame_snapshot_geometry_and_regions();
    test_reset_screen_starts_clear();
    test_character_rendering_uses_screen_char_rom_and_color_ram();
    test_border_rsel_csel();
    test_xscroll_shifts_content();
    test_yscroll_shifts_content();
    test_default_yscroll_fills_display_rows();
    test_ecm_text_mode();
    test_standard_bitmap_mode();
    test_basic_hires_circle_setup_selects_bitmap_mode();
    test_multicolor_bitmap_mode();
    test_mcm_text_mode();
    test_invalid_mode_forces_black();
    test_live_raster_border_change_and_text();
    test_sprite_hires_appears_at_position();
    test_sprite_y50_touches_top_border_fully_revealed();
    test_sprite_sprite_collision_priority_and_irq();
    test_sprite_background_priority_and_collision();
    test_border_hides_sprites_but_collision_latches();
    return 0;
}
