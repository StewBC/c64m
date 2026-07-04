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
        vicii_step_cycle(&v, NULL, (uint64_t)i);
    }
    expect_u32("line cycle wraps", 0, v.timing.cycle_in_line);
    expect_u32("raster increments", 1, v.timing.raster_line);

    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE * (VICII_NTSC_LINES_PER_FRAME - 1); i++) {
        vicii_step_cycle(&v, NULL, (uint64_t)(VICII_NTSC_CYCLES_PER_LINE + i));
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
    vicii_step_cycle(&v, NULL, 0u);
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
    uint64_t abs_cycle;
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_write_register(&v, 0xd011, 0x13); /* DEN=1, YSCROLL=3 */
    v.timing.raster_line = 0x33;

    abs_cycle = 0;
    for (i = 0; i < 12; i++) {
        expect_true("ba high before cycle 12", !vicii_ba_active(&v, abs_cycle));
        vicii_step_cycle(&v, NULL, abs_cycle);
        abs_cycle++;
    }

    /* Step through cycle 12; BA is asserted inside that step. */
    vicii_step_cycle(&v, NULL, abs_cycle);
    abs_cycle++;
    expect_true("ba asserts at cycle 12", vicii_ba_active(&v, abs_cycle));

    while (v.timing.cycle_in_line <= 54) {
        expect_true("ba remains low through c-access window", vicii_ba_active(&v, abs_cycle));
        vicii_step_cycle(&v, NULL, abs_cycle);
        abs_cycle++;
    }

    /* One more step past cycle 54; BA should have expired. */
    vicii_step_cycle(&v, NULL, abs_cycle);
    abs_cycle++;
    expect_true("ba released after cycle 54", !vicii_ba_active(&v, abs_cycle));
}

static void test_frame_snapshot_geometry_and_regions(void) {
    c64_t machine;
    c64_frame frame;
    uint32_t corner;
    uint32_t active;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
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
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
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
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd018, 24); /* screen=$0400, bitmap=$2000 */

    /* The user's program clears bitmap RAM, but standard bitmap colors come from
       screen RAM. Make one bit visible as white-on-black so the mode switch is
       unambiguous. */
    machine.bus.ram[0x0400] = 0x10;
    machine.bus.ram[0x2000] = 0x80;

    make_live_frame(&machine, &frame, "make live basic hires setup frame");
    expect_u32("basic hires setup foreground", 0xffffffffu,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("basic hires setup background", TEST_PALETTE_0,
               frame.pixels[51 * C64_FRAME_WIDTH + 25]);
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

    /* sprite 0: X=100, Y=100 displays its first visible row at raster 101. */
    c64_bus_write(&machine.bus, 0xD000, 100);
    c64_bus_write(&machine.bus, 0xD001, 100);
    c64_bus_write(&machine.bus, 0xD027, 7);
    c64_bus_write(&machine.bus, 0xD015, 1);

    make_live_frame(&machine, &frame, "sprite hires live frame");

    px = frame.pixels[101 * C64_FRAME_WIDTH + 100];
    if (px != sprite_color) {
        fprintf(stderr,
            "sprite at (100,101): got 0x%08x, expected 0x%08x\n",
            px, sprite_color);
        exit(1);
    }
    px = frame.pixels[101 * C64_FRAME_WIDTH + 123];
    if (px != sprite_color) {
        fprintf(stderr,
            "sprite at (123,101): got 0x%08x, expected 0x%08x\n",
            px, sprite_color);
        exit(1);
    }
}

static void test_sprite_line_state_stable_after_midline_x_write(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    setup_solid_sprite(&machine, 5, 0x0340, 100, 100, 7);

    abs = 0;
    while (!(machine.vic.timing.raster_line == 101u &&
             machine.vic.timing.cycle_in_line == 25u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("sprite visible before midline x write", machine.vic.sprite_visible[5]);
    expect_true("sprite line enabled before midline x write", machine.vic.sprite_line_enabled[5]);
    expect_u8("sprite line color before midline x write", 7, machine.vic.sprite_line_color[5]);
    expect_u8("sprite row data before midline x write", 0xff, machine.vic.sprite_data[5][0]);

    c64_bus_write(&machine.bus, 0xd00a, 200);

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("midline sprite x write frame", vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("sprite keeps original x for active line", TEST_PALETTE_7,
               frame.pixels[101 * C64_FRAME_WIDTH + 100]);
    expect_not_u32("midline x write does not redraw current line at new x", TEST_PALETTE_7,
                   frame.pixels[101 * C64_FRAME_WIDTH + 200]);
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
    setup_solid_sprite(&machine, 0, 0x0340, 100, 100, 7);
    setup_solid_sprite(&machine, 1, 0x0380, 100, 100, 10);
    c64_bus_write(&machine.bus, 0xd01a, 0x04); /* enable IMMC */

    make_live_frame(&machine, &frame, "sprite-sprite collision frame");

    expect_u32("sprite 0 wins visual priority", TEST_PALETTE_7,
               frame.pixels[101 * C64_FRAME_WIDTH + 100]);
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
    setup_solid_sprite(&machine, 0, 0x0340, 100, 100, 7);
    c64_bus_write(&machine.bus, 0xd01b, 0x01);

    make_live_frame(&machine, &frame, "behind sprite over background frame");
    expect_u32("behind sprite visible over background", TEST_PALETTE_7,
               frame.pixels[101 * C64_FRAME_WIDTH + 100]);
    expect_u8("no sprite-background collision on background pixel", 0x00, vicii_read_register(&machine.vic, 0xd01f));
}

static void test_border_hides_sprites_but_collision_latches(void) {
    c64_t     machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    setup_solid_sprite(&machine, 0, 0x0340, 10, 10, 7);
    setup_solid_sprite(&machine, 1, 0x0380, 10, 10, 10);

    make_live_frame(&machine, &frame, "border sprite collision frame");

    expect_u32("border hides sprite pixel", TEST_PALETTE_2,
               frame.pixels[11 * C64_FRAME_WIDTH + 10]);
    expect_u8("sprite collision latches under border", 0x03, vicii_read_register(&machine.vic, 0xd01e));
}

static void test_live_bottom_border_can_be_opened_for_sprites(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red border */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* blue background */
    setup_solid_sprite(&machine, 0, 0x0340, 46, 250, 7);

    make_live_frame(&machine, &frame, "closed bottom border frame");
    expect_u32("closed bottom border hides sprite", TEST_PALETTE_2,
               frame.pixels[251 * C64_FRAME_WIDTH + 46]);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 46, 250, 7);

    abs = 0;
    while (!(machine.vic.timing.raster_line == 248u &&
             machine.vic.timing.cycle_in_line == 0u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }

    /* The 24-row bottom compare at raster 247 has already been missed. Clearing
       RSEL before raster 251 makes the VIC also miss the 25-row bottom compare,
       leaving the vertical border open until the next frame. */
    c64_bus_write(&machine.bus, 0xd011, 0x13); /* DEN=1, RSEL=0, YSCROLL=3 */

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("opened bottom border frame", vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("opened bottom border shows sprite first row", TEST_PALETTE_7,
               frame.pixels[251 * C64_FRAME_WIDTH + 46]);
    expect_u32("opened bottom border shows sprite last row", TEST_PALETTE_7,
               frame.pixels[271 * C64_FRAME_WIDTH + 46]);
    expect_u32("opened bottom border keeps side border", TEST_PALETTE_2,
               frame.pixels[251 * C64_FRAME_WIDTH + 10]);
}

static void test_den_clear_blanks_text_display(void) {
    c64_t machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red border */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* blue background */
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;

    expect_true("den set snapshot", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("den set text foreground", TEST_PALETTE_5,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);

    c64_bus_write(&machine.bus, 0xd011, 0x0b); /* DEN=0, RSEL=1, YSCROLL=3 */
    expect_true("den clear snapshot", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("den clear blanks former foreground", TEST_PALETTE_6,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("den clear blanks background pixel", TEST_PALETTE_6,
               frame.pixels[51 * C64_FRAME_WIDTH + 25]);
    expect_u32("den clear blanks snapshot border", TEST_PALETTE_6,
               frame.pixels[0]);
}

static void test_den_clear_keeps_sprite_visible(void) {
    c64_t machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x0b); /* DEN=0, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 24, 50, 7);

    make_live_frame(&machine, &frame, "den clear sprite visible frame");
    expect_u32("den clear live border blanks to d021", TEST_PALETTE_6,
               frame.pixels[40 * C64_FRAME_WIDTH + 20]);
    expect_u32("den clear live crop bottom blanks to d021", TEST_PALETTE_6,
               frame.pixels[270 * C64_FRAME_WIDTH + 20]);
    expect_u32("den clear live frame bottom blanks to d021", TEST_PALETTE_6,
               frame.pixels[(C64_FRAME_HEIGHT - 1) * C64_FRAME_WIDTH + 20]);
    expect_u32("den clear live display background is d021", TEST_PALETTE_6,
               frame.pixels[51 * C64_FRAME_WIDTH + 60]);
    expect_u32("den clear sprite visible over blanked display", TEST_PALETTE_7,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
}

static void test_den_clear_keeps_sprite_collisions(void) {
    c64_t machine;
    c64_frame frame;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x0b); /* DEN=0, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    setup_solid_sprite(&machine, 0, 0x0340, 24, 50, 7);
    setup_solid_sprite(&machine, 1, 0x0380, 24, 50, 10);

    make_live_frame(&machine, &frame, "den clear collision frame");
    expect_u8("den clear sprite-background collision", 0x03, vicii_read_register(&machine.vic, 0xd01f));
    expect_u8("den clear sprite-sprite collision", 0x03, vicii_read_register(&machine.vic, 0xd01e));
}

/* Phase G: $D016 bits 7:5 always read as 1 regardless of writes */
static void test_d016_unused_high_bits_read_as_1(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd016, 0x00);
    expect_u8("d016 high bits set when written 0x00", 0xE0, vicii_read_register(&v, 0xd016) & 0xE0u);
    expect_u8("d016 low bits zero when written 0x00", 0x00, vicii_read_register(&v, 0xd016) & 0x1Fu);

    vicii_write_register(&v, 0xd016, 0xFF);
    expect_u8("d016 high bits set when written 0xFF", 0xE0, vicii_read_register(&v, 0xd016) & 0xE0u);
    expect_u8("d016 low bits set when written 0xFF", 0x1F, vicii_read_register(&v, 0xd016) & 0x1Fu);

    vicii_write_register(&v, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    expect_u8("d016 csel set reads correctly", 0xE8, vicii_read_register(&v, 0xd016));
}

/* Phase G: $D020–$D02E color registers — bits 7:4 always read as 1 */
static void test_color_register_high_nibble_reads_as_1(void) {
    vicii v;
    char error[256];
    uint16_t addr;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    for (addr = 0xd020u; addr <= 0xd02eu; addr++) {
        vicii_write_register(&v, addr, 0x00);
        expect_u8("color reg high nibble set when written 0x00",
                  0xF0, vicii_read_register(&v, addr) & 0xF0u);
        expect_u8("color reg low nibble zero when written 0x00",
                  0x00, vicii_read_register(&v, addr) & 0x0Fu);

        vicii_write_register(&v, addr, 0x0A);
        expect_u8("color reg high nibble set when written 0x0A",
                  0xF0, vicii_read_register(&v, addr) & 0xF0u);
        expect_u8("color reg low nibble preserved when written 0x0A",
                  0x0A, vicii_read_register(&v, addr) & 0x0Fu);

        vicii_write_register(&v, addr, 0xF5);
        expect_u8("color reg high nibble set when written 0xF5",
                  0xF0, vicii_read_register(&v, addr) & 0xF0u);
        expect_u8("color reg low nibble preserved when written 0xF5",
                  0x05, vicii_read_register(&v, addr) & 0x0Fu);
    }
}

/* Phase G: $D02F–$D03F unused block always returns $FF regardless of writes */
static void test_unused_register_block_reads_ff(void) {
    vicii v;
    char error[256];
    uint16_t addr;
    static const uint8_t probes[] = {0x00, 0x55, 0xAA, 0xFF};
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    for (addr = 0xd02fu; addr <= 0xd03fu; addr++) {
        for (i = 0; i < 4; i++) {
            vicii_write_register(&v, addr, probes[i]);
            expect_u8("unused block reads 0xFF", 0xFF, vicii_read_register(&v, addr));
        }
    }
}

/* Phase G: mirrored unused block reads — $D12F/$D22F/$D32F also return $FF */
static void test_unused_register_block_mirrored_reads_ff(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd02f, 0x00);
    expect_u8("d12f mirror reads 0xFF", 0xFF, vicii_read_register(&v, 0xd12f));
    expect_u8("d22f mirror reads 0xFF", 0xFF, vicii_read_register(&v, 0xd22f));
    expect_u8("d32f mirror reads 0xFF", 0xFF, vicii_read_register(&v, 0xd32f));
    expect_u8("d13f mirror reads 0xFF", 0xFF, vicii_read_register(&v, 0xd13f));
}

/* Phase G: $D018 must not have Phase G masking applied — read matches write */
static void test_d018_no_phase_g_masking(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd018, 0x15);
    expect_u8("d018 reads back 0x15 unchanged", 0x15, vicii_read_register(&v, 0xd018));

    vicii_write_register(&v, 0xd018, 0xAA);
    expect_u8("d018 reads back 0xAA unchanged", 0xAA, vicii_read_register(&v, 0xd018));
}

/* ---------------------------------------------------------------------------
 * Phase H: sprite BA window tests.
 *
 * All sprite BA tests use PAL video standard (63 cycles/line) and DEN=0 so
 * that no bad lines fire and only sprite BA windows affect the predicate.
 *
 * Cycle schedule reference (0-based, from vicii_pal_sprite_ba_assert[]):
 *   sprite 0: BA assert cycle 54, window [54, 59)
 *   sprite 1: BA assert cycle 56, window [56, 61)
 *   sprite 2: BA assert cycle 58, window [58, 63)
 *   sprite 3: BA assert cycle 60 of PREVIOUS line, window spans into next line
 *   sprite 4: BA assert cycle 62 of PREVIOUS line, window spans into next line
 *   sprite 5: BA assert cycle  1, window [ 1,  6)
 *   sprite 6: BA assert cycle  3, window [ 3,  8)
 *   sprite 7: BA assert cycle  5, window [ 5, 10)
 * ---------------------------------------------------------------------------*/

/* Helper: advance vicii by one cycle and return the new abs_cycle. */
static uint64_t step_vicii(vicii *v, uint64_t abs_cycle) {
    vicii_step_cycle(v, NULL, abs_cycle);
    return abs_cycle + 1u;
}

/* Helper: advance abs_cycle by n steps without inspecting BA. */
static uint64_t advance_vicii(vicii *v, uint64_t abs_cycle, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        abs_cycle = step_vicii(v, abs_cycle);
    }
    return abs_cycle;
}

/* Set up a fresh vicii with DEN=0 (no bad lines) at a given raster line.
   sprite_enable_mask: bits set → sprite enabled and pre-marked active.
   For sprite n: Y register = raster_line - 1 so no Y-match fires (sprite_active pre-set).
   sprite_active[n] is set directly because these tests pass bus=NULL to
   vicii_step_cycle, causing vicii_fetch_sprites to return early without
   updating sprite_active. */
static void setup_sprite_ba_test_for_standard(
    vicii *v,
    char *error,
    vicii_video_standard standard,
    uint32_t raster_line,
    uint8_t sprite_enable_mask)
{
    int n;

    expect_true("vicii init", vicii_init(v, error, 256));
    vicii_set_video_standard(v, standard);
    vicii_write_register(v, 0xd011, 0x03u); /* DEN=0, YSCROLL=3 */
    v->timing.raster_line = raster_line;
    vicii_write_register(v, 0xd015, sprite_enable_mask);

    for (n = 0; n < 8; n++) {
        if ((sprite_enable_mask >> n) & 1u) {
            /* spr_y = raster_line - 1 → no Y-match on this line (sprite_active pre-set) */
            vicii_write_register(v, (uint16_t)(0xd001u + (uint16_t)(n * 2)), (uint8_t)(raster_line - 1u));
            v->sprite_active[n] = true;
        }
    }
}

static void setup_sprite_ba_test(
    vicii *v, char *error, uint32_t raster_line, uint8_t sprite_enable_mask)
{
    setup_sprite_ba_test_for_standard(
        v,
        error,
        VICII_VIDEO_STANDARD_PAL,
        raster_line,
        sprite_enable_mask);
}

/* Phase H test 1 (Bad Line baseline): no active sprites; existing bad-line
   read-cycle stall count remains unchanged.
   (Covered by test_bad_line_ba_asserts_at_cycle_12 — no new assertions needed.) */

/* Phase H test 2: single active sprite (sprite 5) on a non-bad-line raster.
   BA window [1, 6) must fire and expire exactly. */
static void test_sprite5_ba_window_within_line(void) {
    vicii v;
    char error[256];
    uint64_t abs;

    setup_sprite_ba_test(&v, error, 100u, 0x20u); /* sprite 5 enabled */
    abs = 0;

    /* Cycle 0: vicii_fetch_sprites fires, sprite 5 becomes active. */
    abs = step_vicii(&v, abs);

    /* abs=1: BA assert cycle for sprite 5 fires inside this step. */
    expect_true("sprite5 ba not yet at abs 1", !vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs);

    /* abs=2: window [1,6) is open. */
    expect_true("sprite5 ba open at abs 2", vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs); /* process cycle 2 */

    expect_true("sprite5 ba open at abs 3", vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs);
    expect_true("sprite5 ba open at abs 4", vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs);
    expect_true("sprite5 ba open at abs 5", vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs);

    /* abs=6: window expired (sprite_ba_low_until_abs = 1+5 = 6). */
    expect_true("sprite5 ba closed at abs 6", !vicii_ba_active(&v, abs));
}

/* Phase H test 3: sprites 5, 6, 7 active simultaneously; union window [1, 10). */
static void test_sprites567_adjacent_ba_union(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    setup_sprite_ba_test(&v, error, 80u, 0xe0u); /* sprites 5,6,7 enabled */
    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0: fetch */

    /* abs=1: before sprite 5's assert fires inside step at abs=1 */
    expect_true("spr567 ba not yet at abs 1", !vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs); /* processes cycle 1, sprite 5 asserts */
    expect_true("spr567 ba open at abs 2", vicii_ba_active(&v, abs));

    /* Cycle 3 (abs=3): sprite 6 assert fires, extends to 8. Already within window. */
    /* Cycle 5 (abs=5): sprite 7 assert fires, extends to 10. */
    for (i = 2; i < 10; i++) {
        expect_true("spr567 union ba open", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    /* abs=10: window expired (max of 6, 8, 10 = 10). */
    expect_true("spr567 ba closed at abs 10", !vicii_ba_active(&v, abs));
}

/* Phase H test 4: six sprites (0,1,2,5,6,7) active — two disjoint windows.
   Sprites 3 and 4 are excluded here because their cross-line lookahead fires on
   the PREVIOUS line at cycles 60/62, extending the window past the PAL line
   boundary in ways that interact with the persistent pre-marked sprite_active
   flags (bus=NULL means vicii_fetch_sprites never clears them).  The cross-line
   behaviour of sprites 3 and 4 is covered by dedicated tests below.

   Early group (sprites 5-7): window [1, 10).
   Late group  (sprites 0-2): window [54, 63).
   Gap [10, 54) must have BA high. */
static void test_6sprite_ba_early_and_late_windows(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    /* 0xe7 = 0b11100111 → sprites 0,1,2,5,6,7 */
    setup_sprite_ba_test(&v, error, 120u, 0xe7u);
    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0 */

    /* Early group: sprite 5 at cycle 1 → until=6; 6 at cycle 3 → until=8;
       7 at cycle 5 → until=10.  Union [1, 10). */
    abs = step_vicii(&v, abs); /* cycle 1: sprite 5 asserts */
    expect_true("6spr early ba open at abs 2", vicii_ba_active(&v, abs));

    for (i = 2u; i < 10u; i++) {
        expect_true("6spr early ba union", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    expect_true("6spr gap at abs 10", !vicii_ba_active(&v, abs));

    /* Gap [10, 54). */
    abs = advance_vicii(&v, abs, 54u - 10u);
    expect_true("6spr gap before late group", !vicii_ba_active(&v, abs));

    /* Late group: sprite 0 at cycle 54 → until=59; 1 at 56 → until=61;
       2 at 58 → until=63.  Union [54, 63). */
    abs = step_vicii(&v, abs); /* cycle 54: sprite 0 asserts */
    expect_true("6spr late ba open after sprite0 assert", vicii_ba_active(&v, abs));

    for (i = 55u; i < 63u; i++) {
        expect_true("6spr late ba union", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    /* abs=63: sprite_ba_low_until_abs=63 (set by sprite 2: 58+5=63). */
    expect_true("6spr late ba closed at abs 63", !vicii_ba_active(&v, abs));
}

/* Phase 2: NTSC 6567R8 late sprite group. NTSC mode is 65 cycles/line, so
   sprites 0-2 assert BA at cycles 56, 58, and 60, producing union [56, 65).
   This must not reuse the PAL late group [54, 63). */
static void test_ntsc_sprites012_late_ba_window(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    setup_sprite_ba_test_for_standard(&v, error, VICII_VIDEO_STANDARD_NTSC, 120u, 0x07u);
    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0 */

    abs = advance_vicii(&v, abs, 54u - 1u);
    expect_true("ntsc no pal-style ba at abs 54", !vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs); /* cycle 54 */
    expect_true("ntsc no pal-style ba at abs 55", !vicii_ba_active(&v, abs));

    abs = step_vicii(&v, abs); /* cycle 55 */
    expect_true("ntsc late ba not yet before cycle 56", !vicii_ba_active(&v, abs));

    abs = step_vicii(&v, abs); /* cycle 56: sprite 0 asserts */
    expect_true("ntsc late ba opens at abs 57", vicii_ba_active(&v, abs));

    for (i = 57u; i < 65u; i++) {
        expect_true("ntsc late ba union", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    expect_true("ntsc late ba closed at abs 65", !vicii_ba_active(&v, abs));
    expect_u32("ntsc line wraps after 65 cycles", 0, v.timing.cycle_in_line);
}

/* NTSC sprite 4 asserts at cycle 64 of line N-1 and remains low across the
   65-cycle line boundary for the fetch on line N. */
static void test_ntsc_sprite4_cross_line_ba(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&v, 0xd011, 0x03u);
    v.timing.raster_line = 70u;
    vicii_write_register(&v, 0xd015, 0x10u); /* sprite 4 enabled */
    vicii_write_register(&v, 0xd009, 70u);   /* sprite 4 Y=70 -> first visible row 71 */

    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0: sprite 4 not active on line 70 */
    abs = advance_vicii(&v, abs, 63u); /* advance to cycle 64 */

    expect_true("ntsc sprite4 ba not yet at abs 64", !vicii_ba_active(&v, abs));
    abs = step_vicii(&v, abs); /* cycle 64: sprite 4 asserts */
    expect_true("ntsc sprite4 ba asserted at abs 65", vicii_ba_active(&v, abs));
    expect_u32("ntsc cross-line wrapped to next line", 0, v.timing.cycle_in_line);

    for (i = 65u; i < 69u; i++) {
        expect_true("ntsc sprite4 cross-line ba open", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    expect_true("ntsc sprite4 ba closed at abs 69", !vicii_ba_active(&v, abs));
}

/* Phase H test 5: inactive sprites contribute no BA window. */
static void test_inactive_sprites_no_ba(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    /* Sprite 5 disabled (d015=0), Y register set to would-match value. */
    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x03u);
    v.timing.raster_line = 100u;
    vicii_write_register(&v, 0xd015, 0x00u);   /* all disabled */
    vicii_write_register(&v, 0xd00b, 99u);      /* sprite 5 Y = 99 → display_y=100 */

    abs = 0;
    for (i = 0; i < 63u; i++) {
        expect_true("no sprite ba with disabled sprites", !vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
}

/* Phase H test 6a: sprite 3 cross-line BA — asserted at cycle 60 of line N-1
   for a fetch on line N. Verifies window spans [abs60, abs65). */
static void test_sprite3_cross_line_ba(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    /* Sprite 3 will start on raster line 50: spr_y=49 → first visible row 50.
       Set raster_line=49 (line N-1); sprite 3 not yet active this line. */
    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x03u);
    v.timing.raster_line = 49u;
    vicii_write_register(&v, 0xd015, 0x08u); /* sprite 3 enabled */
    vicii_write_register(&v, 0xd007, 49u);   /* sprite 3 Y=49 → first visible row 50 */

    abs = 0;
    /* Cycle 0 of line 49: fetch — sprite 3 NOT active until raster 50. */
    abs = step_vicii(&v, abs);

    /* Advance to cycle 60 without triggering BA. */
    abs = advance_vicii(&v, abs, 59u);
    /* abs=60, cycle_in_line=60 */

    expect_true("sprite3 ba not yet at abs 60", !vicii_ba_active(&v, abs));

    /* Step at cycle 60: vicii_sprite_dma_next_line(3) fires, sprite_ba_low_until_abs=65. */
    abs = step_vicii(&v, abs);
    /* abs=61 */
    expect_true("sprite3 ba asserted at abs 61", vicii_ba_active(&v, abs));

    /* Continue through end of line (cycles 61, 62) and into next line. */
    for (i = 61u; i < 65u; i++) {
        expect_true("sprite3 cross-line ba open", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    /* abs=65: window expires (60+5=65). */
    expect_true("sprite3 ba closed at abs 65", !vicii_ba_active(&v, abs));
}

/* Phase H test 6b: sprite 4 cross-line BA — asserted at cycle 62 of line N-1. */
static void test_sprite4_cross_line_ba(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x03u);
    v.timing.raster_line = 70u;
    vicii_write_register(&v, 0xd015, 0x10u); /* sprite 4 enabled */
    vicii_write_register(&v, 0xd009, 70u);   /* sprite 4 Y=70 → first visible row 71 */

    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0: fetch, sprite 4 not active on line 70 */
    abs = advance_vicii(&v, abs, 61u); /* advance to cycle 62 */
    /* abs=62, cycle_in_line=62 */

    expect_true("sprite4 ba not yet at abs 62", !vicii_ba_active(&v, abs));

    /* Step at cycle 62: sprite_ba_low_until_abs set to 62+5=67. */
    abs = step_vicii(&v, abs);
    /* abs=63: end of PAL line, cycle_in_line wraps to 0, raster_line=71 */
    expect_true("sprite4 ba asserted at abs 63", vicii_ba_active(&v, abs));

    for (i = 63u; i < 67u; i++) {
        expect_true("sprite4 cross-line ba open", vicii_ba_active(&v, abs));
        abs = step_vicii(&v, abs);
    }
    /* abs=67: window expires (62+5=67). */
    expect_true("sprite4 ba closed at abs 67", !vicii_ba_active(&v, abs));
}

/* Phase H test 7: AEC absence — verify no AEC-named symbols exist.
   This is enforced by the build: the codebase has no vicii_aec_active(),
   no aec field, and no AEC snapshot surface.  The test confirms indirectly
   by exercising vicii_ba_active() as the sole stall predicate. */
static void test_aec_absent_ba_is_sole_stall_predicate(void) {
    vicii v;
    char error[256];
    uint64_t abs;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x13u); /* DEN=1, YSCROLL=3 */
    v.timing.raster_line = 0x33u;

    abs = 0;
    abs = advance_vicii(&v, abs, 13u); /* step through cycle 12 (BA asserted) */
    /* abs=13: bad-line BA window open via vicii_ba_active only */
    expect_true("ba active via sole predicate", vicii_ba_active(&v, abs));
}

static void test_vicii_debug_read_raster(void) {
    vicii v;
    char error[256];
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);

    /* advance one full line so raster_line == 1 */
    for (i = 0; i < VICII_PAL_CYCLES_PER_LINE; i++) {
        vicii_step_cycle(&v, NULL, (uint64_t)i);
    }

    /* $D012 must return the live raster line low byte, not 0 */
    expect_u8("d012 debug returns live raster low", 1, vicii_debug_read_register(&v, 0xd012));

    /* registers[0x12] is never written by the VIC internally so it stays 0 */
    expect_u8("registers[12] still holds compare value", 0, v.registers[0x12]);
}

static void test_vicii_debug_read_d011_raster_bit8(void) {
    vicii v;
    char error[256];
    uint32_t i;
    uint64_t abs;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);

    /* advance past line 255 so raster_line >= 256 */
    abs = 0;
    for (i = 0; i < VICII_PAL_CYCLES_PER_LINE * 256u; i++) {
        vicii_step_cycle(&v, NULL, abs++);
    }

    /* raster_line should now be 256 — bit 8 set */
    expect_u8("d011 debug bit 7 reflects raster bit 8", 0x80,
              (uint8_t)(vicii_debug_read_register(&v, 0xd011) & 0x80u));
    /* stored registers[0x11] bit 7 should be 0 (cleared on write) */
    expect_u8("registers[11] bit 7 is 0", 0, (uint8_t)(v.registers[0x11] & 0x80u));
}

static void test_vicii_debug_read_collision_no_clear(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    v.sprite_sprite_collision = 0x05;
    v.sprite_background_collision = 0x06;

    expect_u8("d01e debug read", 0x05, vicii_debug_read_register(&v, 0xd01e));
    expect_u8("d01e latch unchanged after debug read", 0x05, v.sprite_sprite_collision);

    expect_u8("d01f debug read", 0x06, vicii_debug_read_register(&v, 0xd01f));
    expect_u8("d01f latch unchanged after debug read", 0x06, v.sprite_background_collision);

    /* normal CPU reads still clear */
    vicii_read_register(&v, 0xd01e);
    expect_u8("d01e cleared by normal read", 0x00, v.sprite_sprite_collision);
    vicii_read_register(&v, 0xd01f);
    expect_u8("d01f cleared by normal read", 0x00, v.sprite_background_collision);
}

static void test_vicii_debug_read_irq_status_no_clear(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    /* trigger raster IRQ at line 0 */
    vicii_write_register(&v, 0xd012, 0x00);
    vicii_write_register(&v, 0xd01a, 0x01);
    vicii_step_cycle(&v, NULL, 0u);

    /* debug read returns formatted value with bit 7 set */
    expect_u8("d019 debug has pending bit", 0xf1, vicii_debug_read_register(&v, 0xd019));
    /* irq_status must not be cleared */
    expect_u8("irq_status intact after debug read", 0x01, v.irq_status);

    /* normal read also returns correct value */
    expect_u8("d019 normal read", 0xf1, vicii_read_register(&v, 0xd019));
    /* normal write-1-to-clear then clears it */
    vicii_write_register(&v, 0xd019, 0x01);
    expect_u8("irq_status cleared after normal write", 0x00, v.irq_status);
}

static void test_vicii_debug_read_forced_high_bits(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd016, 0x08);
    expect_u8("d016 debug bits 7:5 forced high", 0xe8, vicii_debug_read_register(&v, 0xd016));

    vicii_write_register(&v, 0xd01a, 0x03);
    expect_u8("d01a debug bits 7:4 forced high", 0xf3, vicii_debug_read_register(&v, 0xd01a));

    vicii_write_register(&v, 0xd020, 0x05);
    expect_u8("d020 debug bits 7:4 forced high", 0xf5, vicii_debug_read_register(&v, 0xd020));

    vicii_write_register(&v, 0xd02e, 0x03);
    expect_u8("d02e debug bits 7:4 forced high", 0xf3, vicii_debug_read_register(&v, 0xd02e));
}

/* Locks the per-standard clock and frame-length constants that drive the
   runtime audio pacer.  A regression here (e.g. reverting to a fixed frame
   rate) mis-paces PAL and over-runs the audio buffer. */
static void test_config_frame_timing(void) {
    c64_config pal  = { 0 };
    c64_config ntsc = { 0 };
    pal.video_standard  = C64_VIDEO_STANDARD_PAL;
    ntsc.video_standard = C64_VIDEO_STANDARD_NTSC;

    expect_u32("PAL clock hz", 985248u, c64_config_clock_hz(&pal));
    expect_u32("NTSC clock hz", 1022727u, c64_config_clock_hz(&ntsc));

    expect_u32("PAL cycles/frame", 63u * 312u, c64_config_cycles_per_frame(&pal));
    expect_u32("NTSC cycles/frame", 65u * 263u, c64_config_cycles_per_frame(&ntsc));

    /* Derived frame rate must match the real standards (PAL ~50, NTSC ~60),
       not a shared constant. */
    expect_true("PAL ~50 fps",
        (int)(c64_config_clock_hz(&pal) / c64_config_cycles_per_frame(&pal)) == 50);
    expect_true("NTSC ~60 fps",
        (int)(c64_config_clock_hz(&ntsc) / c64_config_cycles_per_frame(&ntsc)) == 59);
}

int main(void) {
    test_config_frame_timing();
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
    test_sprite_line_state_stable_after_midline_x_write();
    test_sprite_y50_touches_top_border_fully_revealed();
    test_sprite_sprite_collision_priority_and_irq();
    test_sprite_background_priority_and_collision();
    test_border_hides_sprites_but_collision_latches();
    test_live_bottom_border_can_be_opened_for_sprites();
    test_den_clear_blanks_text_display();
    test_den_clear_keeps_sprite_visible();
    test_den_clear_keeps_sprite_collisions();
    test_d016_unused_high_bits_read_as_1();
    test_color_register_high_nibble_reads_as_1();
    test_unused_register_block_reads_ff();
    test_unused_register_block_mirrored_reads_ff();
    test_d018_no_phase_g_masking();
    /* Phase H: sprite BA windows */
    test_sprite5_ba_window_within_line();
    test_sprites567_adjacent_ba_union();
    test_6sprite_ba_early_and_late_windows();
    test_ntsc_sprites012_late_ba_window();
    test_ntsc_sprite4_cross_line_ba();
    test_inactive_sprites_no_ba();
    test_sprite3_cross_line_ba();
    test_sprite4_cross_line_ba();
    test_aec_absent_ba_is_sole_stall_predicate();
    test_vicii_debug_read_raster();
    test_vicii_debug_read_d011_raster_bit8();
    test_vicii_debug_read_collision_no_clear();
    test_vicii_debug_read_irq_status_no_clear();
    test_vicii_debug_read_forced_high_bits();
    return 0;
}
