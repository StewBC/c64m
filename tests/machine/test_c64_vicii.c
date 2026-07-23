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

static void reset_machine_with_roms_standard(c64_t *machine, const c64_rom_set *roms, c64_video_standard standard) {
    c64_config  cfg;
    char error[256];

    c64_init(machine);

    /* PAL is the canonical video standard for most tests: the 384×PAL-height pixel
       buffer matches PAL dimensions and border compare values (top=51, left=24).
       Targeted NTSC tests opt in through reset_machine_with_standard(). */
    memset(&cfg, 0, sizeof(cfg));
    cfg.video_standard = standard;
    c64_set_config(machine, &cfg);

    expect_true("install synthetic ROMs", c64_install_roms(machine, roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void reset_machine_with_roms(c64_t *machine, const c64_rom_set *roms) {
    reset_machine_with_roms_standard(machine, roms, C64_VIDEO_STANDARD_PAL);
}

static void reset_machine(c64_t *machine) {
    c64_rom_set roms;

    build_roms(&roms);
    reset_machine_with_roms(machine, &roms);
}

static void reset_machine_with_standard(c64_t *machine, c64_video_standard standard) {
    c64_rom_set roms;

    build_roms(&roms);
    reset_machine_with_roms_standard(machine, &roms, standard);
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

static void test_frame_boundary_carries_rc_vmli_display(void) {
    /* VICE vicii_cycle_start_of_frame resets only vc and vcbase; it deliberately
       carries rc, vmli and idle_state across the frame boundary. That carry is
       load-bearing for idle-region VSP/AGSP: a partial bad line induced above
       the first natural bad line advances VC by <40, and UpdateRc captures the
       shifted VCBASE only while rc==7 (the value the bottom border leaves). c64m
       used to force rc=0 (and vmli=0/display_state=false) here, which discarded
       the offset and left EoD's geometric object unable to scroll horizontally.
       Normal frames are unaffected: the first real bad line clears rc at UpdateVc
       before any display g-access. */
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    /* Park at the final cycle of the final line, with the bottom-border state a
       real demo leaves (rc at its max, display counters non-zero). */
    v.timing.raster_line = v.timing.lines_per_frame - 1u;
    v.timing.cycle_in_line = v.timing.cycles_per_line - 1u;
    v.rc = 7u;
    v.vmli = 25u;
    v.display_state = true;
    v.vc = 0x123u;
    v.vc_base = 0x123u;

    vicii_step_cycle(&v, NULL, 0u);

    expect_u32("frame wrapped to line 0", 0u, v.timing.raster_line);
    expect_u8("rc carries across frame boundary", 7u, v.rc);
    expect_u8("vmli carries across frame boundary", 25u, v.vmli);
    expect_true("display_state carries across frame boundary", v.display_state);
    expect_u32("vc resets at frame start", 0u, v.vc);
    expect_u32("vc_base resets at frame start", 0u, v.vc_base);
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

/* Writing $D012 (or $D011 RST8) so the 9-bit compare equals the current
   raster must raise the raster IRQ on that write. Hardware re-triggers mid-line;
   Galencia NTSC depends on this to chain its bottom-border IRQ slice. */
static void test_raster_compare_write_triggers_same_line_irq(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&v, 0xd01a, 0x01); /* enable raster IRQ */

    abs = 0;
    /* Reach line 50, cycle 20 (past the cycle-0 line-start check). */
    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE * 50u + 20u; i++) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    expect_u32("at raster 50", 50u, v.timing.raster_line);
    expect_true("past cycle 0", v.timing.cycle_in_line >= 20u);

    vicii_write_register(&v, 0xd019, 0x01); /* clear any pending */
    expect_u8("irq clear before same-line write", 0x70,
              (uint8_t)(vicii_read_register(&v, 0xd019) & 0xf1u));

    vicii_write_register(&v, 0xd012, 50); /* compare == current line */
    expect_u8("same-line d012 write raises raster irq", 0xf1,
              vicii_read_register(&v, 0xd019));

    /* Also RST8 path: line 200, set compare high bit via $D011 then low byte. */
    vicii_write_register(&v, 0xd019, 0x01);
    abs = 0;
    vicii_reset(&v);
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&v, 0xd01a, 0x01);
    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE * 200u + 10u; i++) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    vicii_write_register(&v, 0xd019, 0x01);
    vicii_write_register(&v, 0xd011, 0x1b); /* RST8=0 */
    vicii_write_register(&v, 0xd012, 200);
    expect_u8("same-line compare via d011/d012 raises irq", 0xf1,
              vicii_read_register(&v, 0xd019));
}

/* Arkanoid dual-zone: soft-scroll IRQ runs on the matching raster, acks $D019,
   then STA $D011 with a new YSCROLL (same RST8 / same 9-bit compare). That
   write must not re-assert the raster IRQ — otherwise the next chain handler
   runs immediately instead of at the programmed next D012 line (ECM clear at
   104 instead of 113). VICE only edge-triggers on non-match → match. */
static void test_d011_yscroll_write_does_not_retrigger_same_line_irq(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&v, 0xd01a, 0x01); /* enable raster IRQ */
    vicii_write_register(&v, 0xd012, 103);  /* compare = 103 (Arkanoid soft line) */
    vicii_write_register(&v, 0xd011, 0x1b); /* DEN|RSEL|Y=3, RST8=0 */

    abs = 0;
    for (i = 0; i < VICII_NTSC_CYCLES_PER_LINE * 103u + 20u; i++) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    expect_u32("at raster 103", 103u, v.timing.raster_line);
    /* Line-start already raised raster IRQ for compare 103. */
    expect_u8("line-start raster irq pending", 0xf1,
              vicii_read_register(&v, 0xd019));

    vicii_write_register(&v, 0xd019, 0x01); /* ack, as the IRQ handler does */
    expect_u8("irq acked before soft $D011", 0x70,
              (uint8_t)(vicii_read_register(&v, 0xd019) & 0xf1u));

    /* Soft-scroll style write: change YSCROLL only; 9-bit compare unchanged. */
    vicii_write_register(&v, 0xd011, 0x58u | 4u); /* ECM|DEN|RSEL|Y=4, RST8=0 */
    expect_u8("YSCROLL $D011 does not re-raise same-line raster irq", 0x70,
              (uint8_t)(vicii_read_register(&v, 0xd019) & 0xf1u));

    /* Control: writing a new D012 equal to the current line still triggers
       (Galencia mid-line chain). */
    vicii_write_register(&v, 0xd012, 50); /* leave match */
    vicii_write_register(&v, 0xd019, 0x01);
    vicii_write_register(&v, 0xd012, 103); /* back to current line */
    expect_u8("D012 change to current line still raises irq", 0xf1,
              vicii_read_register(&v, 0xd019));
}

static void test_sprite_collision_registers_read_clear(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));

    vicii_write_register(&v, 0xd01b, 0xa5);
    expect_u8("d01b priority readback", 0xa5, vicii_read_register(&v, 0xd01b));

    v.sprite_sprite_collision = 0x03;
    expect_u8("d01e first read", 0x03, vicii_read_register(&v, 0xd01e));
    /* VICE defers clear to end of the VIC draw cycle; a same-cycle re-read
       still sees the mask. Stepping one cycle commits the clear. */
    expect_u8("d01e back-to-back read keeps mask", 0x03, vicii_read_register(&v, 0xd01e));
    vicii_step_cycle(&v, NULL, 0u);
    expect_u8("d01e clears after read cycle", 0x00, vicii_read_register(&v, 0xd01e));
    vicii_write_register(&v, 0xd01e, 0xff);
    vicii_step_cycle(&v, NULL, 1u);
    expect_u8("d01e write ignored", 0x00, vicii_read_register(&v, 0xd01e));

    v.sprite_background_collision = 0x04;
    expect_u8("d01f first read", 0x04, vicii_read_register(&v, 0xd01f));
    expect_u8("d01f back-to-back read keeps mask", 0x04, vicii_read_register(&v, 0xd01f));
    vicii_step_cycle(&v, NULL, 2u);
    expect_u8("d01f clears after read cycle", 0x00, vicii_read_register(&v, 0xd01f));
    vicii_write_register(&v, 0xd01f, 0xff);
    vicii_step_cycle(&v, NULL, 3u);
    expect_u8("d01f write ignored", 0x00, vicii_read_register(&v, 0xd01f));
}

static void test_bad_line_ba_asserts_at_cycle_11(void) {
    vicii v;
    char error[256];
    uint64_t abs_cycle;
    uint32_t i;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_write_register(&v, 0xd011, 0x13); /* DEN=1, YSCROLL=3 */
    /* Keep the delayed mode latch coherent after a setup write that did not go
       through a stepped cycle. Bad-line YSCROLL itself is live. */
    v.reg11_delay = v.registers[0x11];
    v.timing.raster_line = 0x33;
    /* Tests teleport past raster $30; arm the frame latch as if DEN was set there. */
    v.allow_bad_lines = true;

    abs_cycle = 0;
    for (i = 0; i <= 54u; ++i) {
        expect_u32("badline fixture cycle", i, v.timing.cycle_in_line);
        vicii_step_cycle(&v, NULL, abs_cycle++);

        /* RDY is the pin sampled for the cycle just executed. The absolute BA
           endpoint is intentionally only one live cycle long now because BA is
           recomputed from the current bad-line condition every cycle. */
        if (i < 11u || i > 53u) {
            expect_true("rdy high outside badline BA window", vicii_rdy_active(&v, abs_cycle));
        } else {
            expect_true("rdy low in badline BA window", !vicii_rdy_active(&v, abs_cycle));
        }

        if (i >= 14u && i <= 53u) {
            expect_true("aec blocks badline c-access", !vicii_aec_active(&v));
        }
    }
    expect_true("aec releases after final c-access", vicii_aec_active(&v));
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

static void test_ntsc_character_rendering_uses_ntsc_top_border(void) {
    c64_t machine;
    c64_frame frame;

    reset_machine_with_standard(&machine, C64_VIDEO_STANDARD_NTSC);

    c64_bus_write(&machine.bus, 0xd021, 0x06);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;

    /* NTSC uses a shorter (263-line) frame but the display window still opens at
       raster line 51 (RSEL=1), exactly as PAL: the vertical border compares are
       identical across standards. This keeps the background aligned with sprites,
       which are placed by absolute raster line. */
    expect_true("make ntsc glyph frame", c64_make_frame_snapshot(&machine, &frame));
    expect_u32("ntsc frame height", C64_FRAME_NTSC_HEIGHT, frame.height);
    expect_u32("ntsc glyph foreground at display top", TEST_COLOR_GREEN,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("ntsc glyph background at display top", TEST_COLOR_BLUE,
               frame.pixels[51 * C64_FRAME_WIDTH + 25]);
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

    /* The program's single boot-time STA $D020=green completes before cycle 12,
       and under the dot-anchored paint mapping (C64MVICII_SIDEBORDER.md §2.2) the
       earliest visible column of raster 0 is painted at cycle 12. So the whole
       first line's border is already green -- there is no pre-write visible dot,
       unlike the old scaled mapping that painted x=0..7 at cycle ~0. Both border
       samples are therefore green; the assertion still exercises that the live
       renderer paints a uniform border line at the written color. */
    expect_u32("live border left is written green", TEST_PALETTE_5, frame.pixels[8]);
    expect_u32("live border mid is written green", TEST_PALETTE_5, frame.pixels[32]);
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
    /* DEN=1 so the vertical border can clear at the top compare and main can
       open over the display window; otherwise the whole frame stays main-border. */
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);

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

static void test_sprite_midline_x_write_affects_remaining_dots(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    setup_solid_sprite(&machine, 5, 0x0340, 100, 100, 7);

    abs = 0;
    /* Stop just before cycle 25 runs: cycles 12..24 have already painted
       x=0..103, including the sprite at X=100. */
    while (!(machine.vic.timing.raster_line == 101u &&
             machine.vic.timing.cycle_in_line == 25u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("sprite visible before midline x write", machine.vic.sprite_visible[5]);
    expect_true("sprite line enabled before midline x write", machine.vic.sprite_line_enabled[5]);
    expect_u8("sprite line color before midline x write", 7, machine.vic.sprite_line_color[5]);
    expect_u8("sprite row data before midline x write", 0xff, machine.vic.sprite_data[5][0]);

    /* VICE pipes sprite X by one cycle — mid-line $D00A takes effect for
       subsequent dots of the same raster (not a whole-line latch). */
    c64_bus_write(&machine.bus, 0xd00a, 200);

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("midline sprite x write frame", vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("dots already painted keep original x", TEST_PALETTE_7,
               frame.pixels[101 * C64_FRAME_WIDTH + 100]);
    expect_u32("midline x write places sprite at new x for later dots", TEST_PALETTE_7,
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
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    setup_solid_sprite(&machine, 0, 0x0340, 100, 100, 7);
    setup_solid_sprite(&machine, 1, 0x0380, 100, 100, 10);
    c64_bus_write(&machine.bus, 0xd01a, 0x04); /* enable IMMC */

    make_live_frame(&machine, &frame, "sprite-sprite collision frame");

    expect_u32("sprite 0 wins visual priority", TEST_PALETTE_7,
               frame.pixels[101 * C64_FRAME_WIDTH + 100]);
    expect_u8("immc irq pending", 0xf5, vicii_read_register(&machine.vic, 0xd019));

    /* Bauer/VICE: IRQ only on 0→nonzero latch. Ack $D019 while $D01E stays set
       must NOT re-assert on further overlap; read $D01E first, then collide. */
    vicii_write_register(&machine.vic, 0xd019, 0x04);
    expect_u8("immc irq cleared", 0x71, vicii_read_register(&machine.vic, 0xd019));

    make_live_frame(&machine, &frame, "sprite-sprite collision no-retrigger frame");
    expect_u8("immc stays clear while d01e latched", 0x71, vicii_read_register(&machine.vic, 0xd019));
    expect_u8("d01e participants", 0x03, vicii_read_register(&machine.vic, 0xd01e));
    {
        uint64_t abs = 0;
        vicii_step_cycle(&machine.vic, &machine.bus, abs);
    }
    expect_u8("d01e read clear after live collision", 0x00, vicii_read_register(&machine.vic, 0xd01e));

    make_live_frame(&machine, &frame, "sprite-sprite collision after d01e clear");
    expect_u8("immc irq after 0-to-nonzero edge", 0xf5, vicii_read_register(&machine.vic, 0xd019));
    expect_u8("d01e after new collision", 0x03, vicii_read_register(&machine.vic, 0xd01e));
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

/* Run the real lft-nine detect at $9900 (bytes captured from depacked demo)
   via the CPU bus. Expect C=1 so LDX #$34 is kept (6569R3 / VICE default). */
static void test_lft_nine_detect_sets_carry(void) {
    c64_t     machine;
    c64_rom_set roms;
    char      error[256];
    uint64_t  abs;
    int       i;
    /* $9900..$99BE inclusive from live depack (ends with RTS). */
    static const uint8_t detect_9900[] = {
        0xa2, 0x00, 0xa9, 0x06, 0x9d, 0x00, 0xdb, 0xa9, 0xff, 0x9d, 0x00, 0x07,
        0xe8, 0xd0, 0xf3, 0x2c, 0x11, 0xd0, 0x10, 0xfb, 0x2c, 0x11, 0xd0, 0x30,
        0xfb, 0xa9, 0x32, 0x8d, 0x01, 0xd0, 0x8d, 0x03, 0xd0, 0x8d, 0x00, 0xd0,
        0xa9, 0x18, 0x8d, 0x02, 0xd0, 0xa9, 0x00, 0x8d, 0x10, 0xd0, 0x8d, 0x17,
        0xd0, 0x8d, 0x1b, 0xd0, 0x8d, 0x1c, 0xd0, 0xa9, 0x02, 0x8d, 0x1d, 0xd0,
        0xa9, 0x1c, 0x8d, 0xf8, 0x07, 0x8d, 0xf9, 0x07, 0xa9, 0x06, 0x8d, 0x27,
        0xd0, 0x8d, 0x28, 0xd0, 0xa9, 0x03, 0x8d, 0x15, 0xd0, 0xac, 0xff, 0x39,
        0xa2, 0x00, 0x8e, 0xff, 0x3f, 0xca, 0x8e, 0xff, 0x39, 0xa9, 0x08, 0x8d,
        0x16, 0xd0, 0xa9, 0x5c, 0x8d, 0x11, 0xd0, 0xa9, 0x32, 0xcd, 0x12, 0xd0,
        0xd0, 0xfb, 0xa9, 0x2d, 0x38, 0xed, 0x06, 0xdc, 0x8d, 0x78, 0x99, 0x10,
        0x06, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9,
        0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xa9, 0xad, 0xa5, 0xea,
        0x24, 0x00, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xad,
        0x1f, 0xd0, 0xa9, 0x1c, 0x8d, 0x11, 0xd0, 0xad, 0x1f, 0xd0, 0x4a, 0x8c,
        0xff, 0x39, 0xad, 0x12, 0xd0, 0x49, 0x33, 0xf0, 0xf9, 0xce, 0x11, 0xd0,
        0xa9, 0x00, 0x8d, 0x15, 0xd0, 0x8d, 0x1d, 0xd0, 0x60
    };

    build_roms(&roms);
    {
        static const uint8_t harness[] = {
            0x78,             /* SEI */
            0x20, 0x00, 0x99, /* JSR $9900 */
            0x00,             /* BRK */
        };
        copy_to_kernal(&roms, TEST_RESET_VECTOR, harness, sizeof(harness));
    }
    reset_machine_with_roms(&machine, &roms);

    for (i = 0; i < (int)sizeof(detect_9900); i++) {
        machine.bus.ram[0x9900 + i] = detect_9900[i];
    }
    /* Demo Timer B setup: latch $xx3E, CRB=$11 (load+start Phi2). */
    c64_bus_write(&machine.bus, 0xdc0f, 0x00);
    c64_bus_write(&machine.bus, 0xdc06, 0x3e);
    c64_bus_write(&machine.bus, 0xdc07, 0x00);
    c64_bus_write(&machine.bus, 0xdc0e, 0x11); /* Timer A free-run too */
    c64_bus_write(&machine.bus, 0xdc0f, 0x11);

    for (abs = 0; abs < 500000u; abs++) {
        if (!c64_step_instruction(&machine, error, sizeof(error))) {
            fail(error);
        }
        if (machine.cpu.cpu.pc == (uint16_t)(TEST_RESET_VECTOR + 4)) {
            break; /* returned to BRK */
        }
    }
    expect_true("detect returned to BRK",
        machine.cpu.cpu.pc == (uint16_t)(TEST_RESET_VECTOR + 4));
    /* LSR of second $D01F: C = sprite 0 bg collision. Needed so the demo's
       BCS after JSR $9900 skips the LDX #$38 patch path when the ghost+sprite
       probe hits. */
    expect_true("lft-nine detect sets carry (sprite0-bg on 2nd $D01F)",
        machine.cpu.cpu.C != 0);
}

/* lft-nine VIC-type detect ($9900): solid sprite over idle ghost in the top
   border must set $D01F. Hardware collides against the graphics sequencer
   even when the main/vertical border covers the pixel. The full detect
   routine is covered by test_lft_nine_detect_sets_carry(); this checks the
   pure latch at R51 mid-line. */
static void test_sprite_bg_collision_in_top_border_idle(void) {
    c64_t    machine;
    uint64_t abs = 0;
    int      i;

    reset_machine(&machine);
    /* Mirror lft-nine $9900 probe as closely as practical. */
    for (i = 0; i < 256; i++) {
        machine.bus.ram[0x0700 + i] = 0xffu;
        machine.bus.color_ram[i] = 0x06;
    }
    machine.bus.ram[0x07f8] = 0x1cu;
    machine.bus.ram[0x07f9] = 0x1cu;
    machine.bus.ram[0x39ff] = 0xffu;
    machine.bus.ram[0x3fff] = 0x00u;
    c64_bus_write(&machine.bus, 0xd000, 0x32);
    c64_bus_write(&machine.bus, 0xd001, 0x32);
    c64_bus_write(&machine.bus, 0xd002, 0x18);
    c64_bus_write(&machine.bus, 0xd003, 0x32);
    c64_bus_write(&machine.bus, 0xd010, 0x00);
    c64_bus_write(&machine.bus, 0xd017, 0x00);
    c64_bus_write(&machine.bus, 0xd01b, 0x00);
    c64_bus_write(&machine.bus, 0xd01c, 0x00);
    c64_bus_write(&machine.bus, 0xd01d, 0x02);
    c64_bus_write(&machine.bus, 0xd027, 0x06);
    c64_bus_write(&machine.bus, 0xd028, 0x06);
    c64_bus_write(&machine.bus, 0xd015, 0x03);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd011, 0x5c);

    while (!(machine.vic.timing.raster_line == 51u &&
             machine.vic.timing.cycle_in_line == 40u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("detect-like sprite-bg or sprite-sprite",
        machine.vic.sprite_background_collision != 0 ||
        machine.vic.sprite_sprite_collision != 0);
    expect_u8("detect-like sprite-bg preferred", 0x03,
        machine.vic.sprite_background_collision | machine.vic.sprite_sprite_collision);
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

static void test_ntsc_live_bottom_border_can_be_opened_for_sprites(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine_with_standard(&machine, C64_VIDEO_STANDARD_NTSC);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red border */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* blue background */
    setup_solid_sprite(&machine, 0, 0x0340, 46, 250, 7);

    make_live_frame(&machine, &frame, "ntsc closed bottom border frame");
    expect_u32("ntsc closed bottom border hides sprite", TEST_PALETTE_2,
               frame.pixels[251 * C64_FRAME_WIDTH + 46]);

    reset_machine_with_standard(&machine, C64_VIDEO_STANDARD_NTSC);
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

    /* NTSC shares PAL's bottom compares (247 for 24-row, 251 for 25-row): the
       vertical border compare values are raster-line numbers identical across
       standards. Clearing RSEL after the 24-row compare and before the 25-row
       compare leaves the vertical border open through the bottom of the frame.
       The last-row check stays within the shorter 263-line NTSC frame. */
    c64_bus_write(&machine.bus, 0xd011, 0x13); /* DEN=1, RSEL=0, YSCROLL=3 */

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("ntsc opened bottom border frame", vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("ntsc opened bottom border shows sprite first row", TEST_PALETTE_7,
               frame.pixels[251 * C64_FRAME_WIDTH + 46]);
    expect_u32("ntsc opened bottom border shows sprite last row", TEST_PALETTE_7,
               frame.pixels[261 * C64_FRAME_WIDTH + 46]);
    expect_u32("ntsc opened bottom border keeps side border", TEST_PALETTE_2,
               frame.pixels[251 * C64_FRAME_WIDTH + 10]);
}

/* Galencia-class HUD: sprite Y=254 paints lines 255..275. PAL paint height is
   the full 312-line raster so no border effect is clipped by the buffer. */
static void test_live_deep_bottom_border_sprite_is_painted(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 46, 254, 7);

    abs = 0;
    while (!(machine.vic.timing.raster_line == 248u &&
             machine.vic.timing.cycle_in_line == 0u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd011, 0x13); /* open vertical border */

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("deep bottom border frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("PAL paint height is full raster", 312u, frame.height);
    expect_u32("PAL paint height matches frame constant", C64_FRAME_PAL_HEIGHT, frame.height);
    expect_u32("PAL paint height matches lines_per_frame",
               machine.vic.timing.lines_per_frame, frame.height);
    expect_u32("deep sprite first row", TEST_PALETTE_7,
               frame.pixels[255 * C64_FRAME_WIDTH + 46]);
    expect_u32("deep sprite last row", TEST_PALETTE_7,
               frame.pixels[275 * C64_FRAME_WIDTH + 46]);
}

/* VICE sprite_display_bits is sticky once set while DMA is on: clearing $D015
   only blocks a new DMA start, not the remainder of an active sprite's rows.
   lft-nine clears $D015 at R251 while bottom digits still have DMA rows left;
   re-gating paint on latched enable clipped those digits under the bottom bar. */
static void test_d015_clear_keeps_active_sprite_display(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    /* Y=250 → first display row 251, last row 271 (21 lines, non-expanded). */
    setup_solid_sprite(&machine, 0, 0x0340, 46, 250, 7);

    abs = 0;
    while (!(machine.vic.timing.raster_line == 248u &&
             machine.vic.timing.cycle_in_line == 0u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd011, 0x13); /* open vertical border */

    /* After DMA has started and several rows have painted, clear enable the
       same way lft-nine does at R251. Display must continue while DMA is on. */
    while (!(machine.vic.timing.raster_line == 251u &&
             machine.vic.timing.cycle_in_line == 12u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("sprite dma active before d015 clear", machine.vic.sprite_active[0]);
    expect_true("sprite visible before d015 clear", machine.vic.sprite_visible[0]);
    c64_bus_write(&machine.bus, 0xd015, 0x00);

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("d015 clear active sprite frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("sprite still paints first open-border row", TEST_PALETTE_7,
               frame.pixels[251 * C64_FRAME_WIDTH + 46]);
    expect_u32("sprite still paints after d015 clear", TEST_PALETTE_7,
               frame.pixels[260 * C64_FRAME_WIDTH + 46]);
    expect_u32("sprite still paints last dma row after d015 clear", TEST_PALETTE_7,
               frame.pixels[271 * C64_FRAME_WIDTH + 46]);
    expect_u32("side border still covers sprites", TEST_PALETTE_2,
               frame.pixels[260 * C64_FRAME_WIDTH + 10]);
}

/* Step B/C main-border flip-flop: a timed $D016 CSEL 1->0 write at cycle 56 (the
   VICE viciisc check_hborder open window) leaves the flip-flop clear, opening the
   right side border for the rest of the line. The right-border check sets the flip-
   flop at cycle 57 for CSEL=1 and cycle 56 for CSEL=0, sampling the CSEL latched at
   the end of the previous cycle; a write landing at cycle 56 is 1 through cycle 55
   (so cycle 56's check, seeing CSEL=1, expects the compare at 57) and 0 by cycle 56
   (so cycle 57's check, seeing CSEL=0, expects the compare at 56) -- neither fires,
   and the border stays open. c64m applies this decision through a two-cycle border
   pipeline so normal edges stay at x=24/344. */
static void test_live_right_side_border_opens(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    /* Closed baseline: 40-column display, red border, blue background. The right
       side border (x >= 344) shows the border colour on a display line. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red border */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* blue background */
    make_live_frame(&machine, &frame, "closed right side border frame");
    expect_u32("closed right side border is border colour", TEST_PALETTE_2,
               frame.pixels[100 * C64_FRAME_WIDTH + 350]);

    /* Opened: flip CSEL 1->0 at cycle 56 of raster 100. Neither the CSEL=1 right
       compare (cycle 57) nor the CSEL=0 right compare (cycle 56) fires for this
       write phase, so the flip-flop stays clear and x >= 344 shows background
       instead of border. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    abs = 0;
    while (!(machine.vic.timing.raster_line == 100u &&
             machine.vic.timing.cycle_in_line == 56u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd016, 0x00); /* CSEL=0 in the open window */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("opened right side border frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("opened right side border shows background", TEST_PALETTE_6,
               frame.pixels[100 * C64_FRAME_WIDTH + 350]);
    /* A line the write did not touch keeps its closed right border. */
    expect_u32("untouched line keeps closed right border", TEST_PALETTE_2,
               frame.pixels[99 * C64_FRAME_WIDTH + 350]);
}

/* Writing CSEL too early (well before the cycle-56 open window) makes the VIC
   match the CSEL=0 right compare (cycle 56) with CSEL already 0, setting the flip-
   flop and closing the border -- the side border does NOT open. */
static void test_live_side_border_wrong_cycle_stays_closed(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1 */
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    abs = 0;
    while (!(machine.vic.timing.raster_line == 100u &&
             machine.vic.timing.cycle_in_line == 52u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd016, 0x00); /* CSEL=0 too early (before cycle 53) */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("wrong-cycle side border frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("wrong-cycle write keeps right border closed", TEST_PALETTE_2,
               frame.pixels[100 * C64_FRAME_WIDTH + 350]);
}

/* The flip-flop persists across the line boundary: opening the right border on
   raster 100 leaves it clear entering raster 101, so raster 101's left region is
   also open (nothing sets it before the left compare). */
static void test_live_side_border_flip_flop_persists_left(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    abs = 0;
    while (!(machine.vic.timing.raster_line == 100u &&
             machine.vic.timing.cycle_in_line == 55u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    /* lft-nine's CIA-synchronised loop projects VICE's cycle-56 store one c64m
       cycle earlier than EoD.  It must still dodge the right compare and keep
       main_border_ff clear through line wrap, revealing the next line's left
       edge before the normal left compare. */
    c64_bus_write(&machine.bus, 0xd016, 0x00);
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("persist-left side border frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("left region open on next line (flip-flop stayed clear)",
               TEST_PALETTE_6, frame.pixels[101 * C64_FRAME_WIDTH + 10]);
}

/* A sprite positioned in the right side border is hidden while the border is
   closed and revealed once the border is opened by the CSEL trick. */
static void test_live_side_border_reveals_sprite(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    /* Closed baseline: sprite at X=350, Y=100 -> first visible row raster 101,
       hidden by the closed right border. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 350, 100, 7);
    make_live_frame(&machine, &frame, "closed side border sprite frame");
    expect_u32("closed side border hides sprite", TEST_PALETTE_2,
               frame.pixels[101 * C64_FRAME_WIDTH + 352]);

    /* Opened on raster 101: the sprite becomes visible in the side border. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    setup_solid_sprite(&machine, 0, 0x0340, 350, 100, 7);
    abs = 0;
    while (!(machine.vic.timing.raster_line == 101u &&
             machine.vic.timing.cycle_in_line == 56u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd016, 0x00);
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("opened side border sprite frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("opened side border reveals sprite", TEST_PALETTE_7,
               frame.pixels[101 * C64_FRAME_WIDTH + 352]);
}

/* Main-border strips always use $D020 (even when DEN=0). Leave the vertical
   FF clear via open-bottom, then begin a DEN=0 frame: the inactive sequencer's
   idle output is B0C here, while the closed side strips remain $D020. */
static void test_den_clear_main_border_keeps_d020_full_height(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;
    uint32_t  y;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);

    abs = 0;
    while (!(machine.vic.timing.raster_line == 248u &&
             machine.vic.timing.cycle_in_line == 0u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd011, 0x13); /* open bottom → vertical stays clear */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }

    c64_bus_write(&machine.bus, 0xd011, 0x0b); /* DEN=0, vertical still clear */
    make_live_frame(&machine, &frame, "den0 main border full height");

    for (y = 60; y < 200u; y++) {
        expect_u32("left main-border strip is d020 when DEN=0", TEST_PALETTE_0,
                   frame.pixels[y * C64_FRAME_WIDTH + 10]);
        expect_u32("right main-border strip is d020 when DEN=0", TEST_PALETTE_0,
                   frame.pixels[y * C64_FRAME_WIDTH + 360]);
    }
    expect_u32("den0 interior is d021", TEST_PALETTE_6,
               frame.pixels[100 * C64_FRAME_WIDTH + 100]);
}

/* Step D: an opened side border shows *zero* graphics data, not the $3FFF ghost
   byte. No g-access loads the sequencer outside cycles 15..54, so VICE viciisc
   forces the shift register to zero there (vicii-draw-cycle.c: `gbuf_pipe0_reg =
   0` when the cycle is not visible); vicii_fetch_idle() reads $3FFF for the bus
   but never assigns gbuf. Every pair is therefore 00 → B0C in text modes, and a
   patterned ghost byte must NOT show through. Emitting the ghost byte instead
   painted its set bits in colour 0, which put pure-black blocks under the
   multicolor sprites in Edge of Disgrace's opened side border. */
static void test_live_side_border_shows_zero_graphics(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3, ECM=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red border */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* blue background */
    /* A patterned ghost byte that must stay invisible: graphics data in the
       over-border region is zero, so the revealed border is flat B0C. */
    machine.bus.ram[0x3fffu] = 0xf0u;

    abs = 0;
    while (!(machine.vic.timing.raster_line == 100u &&
             machine.vic.timing.cycle_in_line == 56u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd016, 0x00); /* open right border */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("zero-graphics side border frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("opened border is B0C where the ghost byte had set bits",
               TEST_PALETTE_6, frame.pixels[100 * C64_FRAME_WIDTH + 344]);
    expect_u32("opened border is B0C where the ghost byte had clear bits",
               TEST_PALETTE_6, frame.pixels[100 * C64_FRAME_WIDTH + 348]);
}

/* XSCROLL phases the graphics shift register, and that phasing carries the last
   g-access column into the opened side border. VICE's draw_graphics loads the
   shift register at `i == xscroll_pipe` (vicii-draw-cycle.c), so column 39 is
   emitted at x = 336+XSCROLL .. 343+XSCROLL and gbuf only falls to zero
   (→ over-border pair-0 colour) at x = 344+XSCROLL. The over-border right edge is
   therefore XSCROLL-delayed, not the fixed x=344. Getting this wrong painted a
   solid B0C vertical line at x=344 on every second frame of Deus Ex Machina's
   water scene, whose ±1-dot shimmer toggles XSCROLL 0<->1 under an opened side
   border (agents/demo/deusexmachina/dem-handoff.md).

   Multicolor bitmap mode (as in the DEM water scene) gives a directly-controlled
   g-byte whose over-border pair-0 colour is B0C. Column 39's last pixel pair
   (sx=6,7) carries a foreground colour (pair 11 → colour RAM); at XSCROLL=0 it
   lands at x=342..343 with over-border B0C at x=344, and at XSCROLL=1 the whole
   column shifts one dot right so that foreground pair reaches x=343..344 (real
   graphics, not B0C) with over-border B0C pushed to x=345. */
static void test_live_open_border_right_edge_xscroll_delayed(void) {
    c64_t     machine;
    c64_frame frame0;
    c64_frame frame1;
    uint64_t  abs;
    /* Multicolor bitmap, D018=$18: screen $0400, bitmap base $2000. At raster 100
       (YSCROLL=3) the sequencer is on character row 6 (VC=240), RC=1, so column 39
       fetches bitmap byte $2000 + (279<<3) + 1 = $28B9 and its pair-11 colour from
       colour RAM cell 279. g-byte $03 = pairs (00)(00)(00)(11): only the last pair
       (sx=6,7) is foreground = colour RAM (palette 10); the rest is B0C. */
    const uint16_t col39_gbyte = 0x28B9u;
    const uint16_t col39_cell = 279u;
    const uint32_t row = 100u;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* border colour */
    c64_bus_write(&machine.bus, 0xd021, 0x06); /* B0C = palette 6 */
    machine.bus.ram[col39_gbyte]     = 0x03u;  /* last pair (sx6,7) = 11 → cbuf */
    machine.bus.color_ram[col39_cell] = 0x0Au; /* pair 11 → palette 10 */

    abs = 0;
    while (!(machine.vic.timing.raster_line == row &&
             machine.vic.timing.cycle_in_line == 56u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd016, 0x10); /* CSEL=0 opens the right border, XSCROLL=0 */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("open border xscroll0 frame",
                vicii_copy_completed_frame(&machine.vic, &frame0, abs));
    /* XSCROLL=0: foreground pair at x=343 (validates the column-39 addressing),
       over-border B0C begins at x=344. */
    expect_u32("xscroll0 col39 foreground at x=343", TEST_PALETTE_10,
               frame0.pixels[row * C64_FRAME_WIDTH + 343]);
    expect_u32("xscroll0 over-border B0C at x=344", TEST_PALETTE_6,
               frame0.pixels[row * C64_FRAME_WIDTH + 344]);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3b);
    c64_bus_write(&machine.bus, 0xd016, 0x19); /* CSEL=1, MCM=1, XSCROLL=1 */
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[col39_gbyte]     = 0x03u;
    machine.bus.color_ram[col39_cell] = 0x0Au;

    abs = 0;
    while (!(machine.vic.timing.raster_line == row &&
             machine.vic.timing.cycle_in_line == 56u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd016, 0x11); /* CSEL=0 opens the right border, XSCROLL=1 */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("open border xscroll1 frame",
                vicii_copy_completed_frame(&machine.vic, &frame1, abs));
    /* XSCROLL=1: the whole display shifts one dot right, so column 39's foreground
       pair now reaches x=344 as real graphics (the DEM fix) and over-border B0C is
       pushed out to x=345. A fixed x>=344 over-border cutoff would paint B0C here
       instead — the water-scene line. */
    expect_u32("xscroll1 col39 foreground carried to x=344", TEST_PALETTE_10,
               frame1.pixels[row * C64_FRAME_WIDTH + 344]);
    expect_u32("xscroll1 over-border B0C at x=345", TEST_PALETTE_6,
               frame1.pixels[row * C64_FRAME_WIDTH + 345]);
}

/* A same-cycle Phi2 $D016 MCM write must reach the display column painted on
   that same cycle. c64m paints a cycle's 8-dot span in begin_cycle, before the
   CPU's Phi2 store, so without the finish_cycle MCM resolution the first display
   column (x=24, drawn at cycle 15) is left one paint behind: it keeps the
   pre-write mode while columns 1+ get the new one. Deus Ex Machina toggles MCM
   on at cycle 15 every line, which painted column 0 in hires (colour 8) over a
   VICE-black centre. VICE resamples the MCM bit mid-cycle (viciisc
   vmode16_pipe), so the write reaches column 0. Standard bitmap (mode 2, hires)
   vs multicolor bitmap (mode 3): bitmap byte $80, VM cell $2A → x=24 is the VM
   high nibble (palette 2) in hires, the VM low nibble (palette 10) in MC. */
static void test_live_mcm_toggle_reaches_column0_same_cycle(void) {
    c64_t    machine;
    c64_frame frame;
    uint64_t abs;
    /* Row 100, YSCROLL=3: column 0 maps to cell 240, RC=1 (see the xscroll test
       above), so its bitmap byte is $2000 + 240*8 + 1 = $2781. */
    const uint16_t col0_gbyte = 0x2781u;
    const uint16_t col0_cell  = 240u;
    const uint32_t row = 100u;

    /* Control: MCM stays 0 for the whole row → x=24 is hires bitmap fg (VM
       high nibble = palette 2). */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen $0400, bitmap $2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, MCM=0, XSCROLL=0 */
    machine.bus.ram[col0_gbyte]        = 0x80u; /* bit7 set; pair(7,6)=10 */
    machine.bus.ram[0x0400u + col0_cell] = 0x2Au; /* VM high=2, low=A */
    abs = 0;
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("control frame", vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("MCM=0 col0 x=24 is hires fg (VM high, palette 2)",
               TEST_PALETTE_2, frame.pixels[row * C64_FRAME_WIDTH + 24]);

    /* Fix: MCM=0 through begin_cycle(15) (column 0 paints hires), then the CPU
       sets MCM=1 during that same cycle's Phi2, before finish_cycle(15). Column
       0 must be resolved to MC bitmap (VM low nibble = palette 10), matching
       columns 1+ and VICE -- not left as hires palette 2. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3b);
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* start MCM=0 */
    machine.bus.ram[col0_gbyte]        = 0x80u;
    machine.bus.ram[0x0400u + col0_cell] = 0x2Au;
    abs = 0;
    while (!(machine.vic.timing.raster_line == row &&
             machine.vic.timing.cycle_in_line == 15u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    /* Split cycle 15: begin paints column 0 with MCM=0, the Phi2 store flips
       MCM=1, finish resolves the span. */
    vicii_begin_cycle(&machine.vic, &machine.bus, abs);
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    vicii_finish_cycle(&machine.vic);
    abs++;
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("mcm-toggle frame", vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("same-cycle MCM write reaches col0 x=24 (MC, VM low, palette 10)",
               TEST_PALETTE_10, frame.pixels[row * C64_FRAME_WIDTH + 24]);
}

/* VICE draw_graphics: MCM text only uses 2-bit pairs when cbuf bit 3 is set.
   Idle forces cbuf=0, so MCM=1 + BMM=0 idle is still hires (ghost $F0 → four
   solid fg dots, four bg). Decoding idle as multicolor pairs broke open-border
   outlines (Edge of Disgrace bottom frame stipple). */
/* Bauer/VICE: DEN is sampled on raster $30 to arm allow_bad_lines for the
   rest of $30–$F7. Clearing DEN afterward must not kill bad lines. */
static void test_allow_bad_lines_latched_at_line_30(void) {
    vicii v;
    char error[256];
    uint64_t abs = 0;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    /* DEN=1, YSCROLL=0 so line $30 itself is a bad line once armed. */
    vicii_write_register(&v, 0xd011, 0x10u);

    while (!(v.timing.raster_line == 0x30u && v.timing.cycle_in_line == 20u)) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    expect_true("allow_bad_lines armed on $30 with DEN", v.allow_bad_lines);
    expect_true("bad line on $30 with YSCROLL=0", v.bad_line);

    /* Clear DEN mid-window; bad lines must continue. */
    vicii_write_register(&v, 0xd011, 0x00u); /* DEN=0, YSCROLL=0 */
    while (v.timing.raster_line != 0x38u) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    /* Step into line $38 (YSCROLL=0 still matches). */
    while (v.timing.cycle_in_line != 20u) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    expect_true("allow_bad_lines still armed after DEN clear", v.allow_bad_lines);
    expect_true("bad line still fires with DEN=0 after arming", v.bad_line);
}

/* VICE set_vborder: bottom only sets the latch; RSEL open after missing the
   24-row bottom leaves vertical open (already covered by sprite border tests).
   Also: once latched closed, only top+DEN clears it. */
static void test_set_vborder_latch_sticky_until_top(void) {
    vicii v;
    char error[256];
    uint64_t abs = 0;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x1bu); /* DEN=1 RSEL=1 Y=3 */

    /* Run through a bottom compare so set_vborder latches. */
    while (!(v.timing.raster_line == 251u && v.timing.cycle_in_line == 20u)) {
        vicii_step_cycle(&v, NULL, abs++);
    }
    expect_true("set_vborder latched at bottom", v.set_vborder);
    expect_true("vertical border active at bottom", v.vertical_border_active);

    /* Mid-frame DEN clear must not alone reopen the vertical border. */
    vicii_write_register(&v, 0xd011, 0x0bu); /* DEN=0 RSEL=1 */
    vicii_step_cycle(&v, NULL, abs++);
    expect_true("vertical stays closed with DEN=0 mid-frame", v.vertical_border_active);
}

/* The idle ghost byte is visible *inside* the 40-column window when the
   sequencer leaves display state, not in the over-border region (which has no
   g-access and therefore zero graphics data -- see
   test_live_side_border_shows_zero_graphics). YSCROLL=7 puts the first bad line
   at raster 55, so rasters 51..54 are already past the vertical-border top open
   while the sequencer is still idle -- the ghost byte is on screen there. */
static void test_live_mcm_idle_ghost_is_hires(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1f); /* DEN=1, RSEL=1, YSCROLL=7 */
    c64_bus_write(&machine.bus, 0xd016, 0x18); /* CSEL=1, MCM=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x3fffu] = 0xf0u; /* 11110000 hires */

    abs = 0;
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("mcm idle ghost frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    /* Inside the window the 8-dot ghost group starts at x=24. $F0 as hires:
       x=96..99 foreground (colour 0), x=100..103 background (B0C) -- four solid
       dots each, not multicolor pairs. */
    expect_u32("mcm idle hires fg at x=96", TEST_PALETTE_0,
               frame.pixels[52 * C64_FRAME_WIDTH + 96]);
    expect_u32("mcm idle hires fg at x=99", TEST_PALETTE_0,
               frame.pixels[52 * C64_FRAME_WIDTH + 99]);
    expect_u32("mcm idle hires bg at x=100", TEST_PALETTE_6,
               frame.pixels[52 * C64_FRAME_WIDTH + 100]);
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
    /* DEN=0 prevents the top vertical-FF clear, so main never opens and the
       whole frame is $D020 (including former display pixels). */
    expect_u32("den clear keeps d020 on former foreground", TEST_PALETTE_2,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("den clear keeps d020 on background pixel", TEST_PALETTE_2,
               frame.pixels[51 * C64_FRAME_WIDTH + 25]);
    expect_u32("den clear snapshot border keeps d020", TEST_PALETTE_2,
               frame.pixels[0]);
}

/* DEN is not a live graphics blanking input. Once the top border and display
   sequencer have opened with DEN=1, clearing DEN mid-display must leave the
   running graphics pipeline visible. Edge of Disgrace does this on every line
   while changing $D018 to form its background checker. */
static void test_den_clear_mid_display_keeps_graphics_pipeline(void) {
    c64_t machine;
    c64_frame frame;
    uint64_t abs;
    uint32_t i;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* arm bad lines and open top */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen $0400, charset $2000 */
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    for (i = 0; i < 1000u; i++) {
        machine.bus.ram[0x0400u + i] = 1u;
        machine.bus.color_ram[i] = 5u;
    }
    for (i = 0; i < 8u; i++) {
        machine.bus.ram[0x2008u + i] = 0xffu;
    }

    abs = 0;
    while (!(machine.vic.timing.raster_line == 100u &&
             machine.vic.timing.cycle_in_line == 0u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd011, 0x0b); /* DEN=0, keep live VC/RC */
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("den clear mid-display frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));
    expect_u32("den clear mid-display preserves foreground graphics",
               TEST_PALETTE_5,
               frame.pixels[100 * C64_FRAME_WIDTH + 24]);
}

static void test_den_clear_keeps_sprite_visible(void) {
    c64_t machine;
    c64_frame frame;
    uint64_t abs;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x02);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    /* Open vertical so main can clear; the following DEN=0 frame has no active
       display sequence, so its idle-zero interior is B0C. */
    abs = 0;
    while (!(machine.vic.timing.raster_line == 248u &&
             machine.vic.timing.cycle_in_line == 0u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    c64_bus_write(&machine.bus, 0xd011, 0x13);
    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }

    c64_bus_write(&machine.bus, 0xd011, 0x0b); /* DEN=0, vertical still clear */
    setup_solid_sprite(&machine, 0, 0x0340, 24, 50, 7);

    make_live_frame(&machine, &frame, "den clear sprite visible frame");
    expect_u32("den clear live border keeps d020", TEST_PALETTE_2,
               frame.pixels[40 * C64_FRAME_WIDTH + 20]);
    expect_u32("den clear live crop bottom keeps d020", TEST_PALETTE_2,
               frame.pixels[270 * C64_FRAME_WIDTH + 20]);
    expect_u32("den clear live frame bottom keeps d020", TEST_PALETTE_2,
               frame.pixels[(C64_FRAME_HEIGHT - 1) * C64_FRAME_WIDTH + 20]);
    expect_u32("den clear live display background is d021", TEST_PALETTE_6,
               frame.pixels[51 * C64_FRAME_WIDTH + 60]);
    expect_u32("den clear sprite visible over blanked display", TEST_PALETTE_7,
               frame.pixels[51 * C64_FRAME_WIDTH + 24]);
}

static void test_den_clear_idle_has_no_sprite_background_collision(void) {
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
    /* With DEN low from the start there is no live display sequence. The idle
       pixel is background, so the overlapping sprites collide with each other
       but not with raster-derived foreground graphics. */
    expect_u8("den clear idle has no sprite-background collision", 0x00,
              vicii_read_register(&machine.vic, 0xd01f));
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
 * Phase H: sprite BA mask tests.
 *
 * All sprite BA tests use PAL video standard (63 cycles/line) and DEN=0 so
 * that no bad lines fire and only sprite BA windows affect the predicate.
 *
 * VICE models BA as a live per-cycle mask, not a persistent six-cycle window.
 * Adjacent sprite masks overlap to give the VIC the required three-cycle lead
 * before each Phi2 data access.  These tests inspect the RDY pin latched by the
 * just-completed VIC cycle.
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
   (Covered by test_bad_line_ba_asserts_at_cycle_11 — no new assertions needed.) */

/* Phase H test 2: single active sprite (sprite 5) on a non-bad-line raster.
   VICE's PAL cycle table holds BA low on zero-based cycles 1..5. */
static void test_sprite5_ba_window_within_line(void) {
    vicii v;
    char error[256];
    uint64_t abs;

    setup_sprite_ba_test(&v, error, 100u, 0x20u); /* sprite 5 enabled */
    abs = 0;

    /* Cycle 0 leaves RDY high; cycles 1..5 have sprite-5 in the VICE BA mask. */
    abs = step_vicii(&v, abs);
    expect_true("sprite5 rdy high at cycle 0", vicii_rdy_active(&v, abs));
    for (uint32_t cycle = 1u; cycle <= 5u; ++cycle) {
        abs = step_vicii(&v, abs);
        expect_true("sprite5 rdy low in VICE BA window", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs);
    expect_true("sprite5 rdy high after VICE BA window", vicii_rdy_active(&v, abs));
}

static void test_vicii_bus_schedule_reports_c_and_sprite_accesses(void) {
    vicii v;
    vicii ntsc;
    char error[256];
    uint64_t abs;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_step_cycle(&v, NULL, 0u);
    (void)advance_vicii(&v, 1u, 10u);
    expect_u8("idle g-access schedule", VICII_BUS_ACCESS_IDLE,
        (uint8_t)vicii_bus_access_phi1(&v));
    vicii_reset(&v);
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x13u);
    v.timing.raster_line = 0x33u;
    v.allow_bad_lines = true;
    abs = advance_vicii(&v, 0, 16u); /* process cycles 0 through 15 */
    expect_u8("badline c-access schedule", VICII_BUS_ACCESS_C,
        (uint8_t)vicii_bus_access(&v));
    expect_u8("badline g-access schedule", VICII_BUS_ACCESS_G,
        (uint8_t)vicii_bus_access_phi1(&v));
    abs = advance_vicii(&v, abs, 40u); /* process cycles 16 through 55 */
    expect_u8("post-badline c-access schedule", VICII_BUS_ACCESS_NONE,
        (uint8_t)vicii_bus_access(&v));

    setup_sprite_ba_test(&v, error, 100u, 0x01u);
    (void)advance_vicii(&v, 0, 58u); /* process PAL sprite-0 fetch cycle 57 */
    expect_u8("sprite fetch schedule", VICII_BUS_ACCESS_SPRITE,
        (uint8_t)vicii_bus_access(&v));
    expect_u8("sprite pointer schedule", VICII_BUS_ACCESS_SPRITE_POINTER,
        (uint8_t)vicii_bus_access_phi1(&v));
    expect_u64("sprite BA records current live-cycle endpoint", 58u,
        v.timing.sprite_ba_low_until_abs);
    (void)advance_vicii(&v, 58u, 1u); /* process PAL sprite-0 second data cycle */
    expect_u8("sprite data Phi1 schedule", VICII_BUS_ACCESS_SPRITE_DATA,
        (uint8_t)vicii_bus_access_phi1(&v));
    expect_u8("sprite data Phi2 schedule", VICII_BUS_ACCESS_SPRITE_DATA,
        (uint8_t)vicii_bus_access(&v));

    expect_true("ntsc vicii init", vicii_init(&ntsc, error, sizeof(error)));
    vicii_set_video_standard(&ntsc, VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&ntsc, 0xd011, 0x13u);
    ntsc.timing.raster_line = 0x33u;
    ntsc.allow_bad_lines = true;
    abs = advance_vicii(&ntsc, 0, 16u); /* process cycles 0 through 15 */
    expect_u8("ntsc badline c-access schedule", VICII_BUS_ACCESS_C,
        (uint8_t)vicii_bus_access(&ntsc));

    setup_sprite_ba_test_for_standard(
        &ntsc, error, VICII_VIDEO_STANDARD_NTSC, 100u, 0x01u);
    (void)advance_vicii(&ntsc, 0, 59u); /* process NTSC sprite-0 fetch cycle 58 */
    expect_u8("ntsc sprite fetch schedule", VICII_BUS_ACCESS_SPRITE,
        (uint8_t)vicii_bus_access(&ntsc));
    expect_u8("ntsc sprite pointer schedule", VICII_BUS_ACCESS_SPRITE_POINTER,
        (uint8_t)vicii_bus_access_phi1(&ntsc));
}

/* The renderer latches a complete sprite row at cycle 0, but that latch must
   not extend bus DMA past the MCBASE==63 shutdown (Bauer cycle 16 / index 15).
   Sprite 0's slots are late in the line, so this exercises the exact
   stale-latch seam. */
static void test_sprite_dma_off_stops_late_phi2_slots(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_write_register(&v, 0xd011, 0x03u); /* DEN=0 */
    v.timing.raster_line = 100u;
    vicii_write_register(&v, 0xd015, 0x01u);
    vicii_write_register(&v, 0xd017, 0x01u);
    vicii_write_register(&v, 0xd001, 99u); /* no new Y-match on this line */
    v.sprite_active[0] = true;
    v.sprite_y_exp_ff[0] = true;
    v.sprite_mc[0] = 60u;
    v.sprite_mcbase[0] = 60u;

    /* Cycle 0 advances MC to 63; MCBASE update at index 15 drops DMA.
       Processing through cycle 57 reaches sprite 0's late pointer/data slot. */
    (void)advance_vicii(&v, 0u, 58u);
    expect_true("sprite dma off at MCBASE 63", !v.sprite_active[0]);
    expect_u8("dma-off sprite0 has no Phi2 slot", VICII_BUS_ACCESS_NONE,
        (uint8_t)vicii_bus_access(&v));
}

/* lft-nine-style sprite crunch: a mid-line $D017 clear on VICE's crunch
   cycle (raster_cycle 14 == VICII_PAL_CYCLE(15)) while the expansion
   flip-flop is clear corrupts MC so DMA never reaches MCBASE==63.
   At cycle 14, prepare_sprite_line has already advanced MC by +3 for the
   line while MCBASE still holds the previous latch — the bit-magic mixes
   those two values into a residue class that +3 never takes to 63. */
static void test_sprite_crunch_keeps_dma_past_21_rows(void) {
    vicii v;
    char error[256];
    uint32_t line;
    uint64_t abs = 0u;
    uint8_t mc_after;
    const uint8_t mcbase = 9u;
    const uint8_t mc = 12u; /* mcbase + 3 after this line's fetch advance */

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_write_register(&v, 0xd011, 0x03u); /* DEN=0: no bad lines */
    v.timing.raster_line = 12u;
    vicii_write_register(&v, 0xd015, 0x01u); /* sprite 0 */
    vicii_write_register(&v, 0xd001, 9u);    /* Y no longer matches */
    vicii_write_register(&v, 0xd017, 0x01u); /* expand currently on */

    /* Mid-display state after a few normal rows, with exp_flop clear as after
       the expand-toggle cycle — the condition the hardware crunch requires. */
    v.sprite_active[0] = true;
    v.sprite_y_exp_ff[0] = false;
    v.sprite_mcbase[0] = mcbase;
    v.sprite_mc[0] = mc;
    v.timing.cycle_in_line = 14u;

    vicii_write_register(&v, 0xd017, 0x00u); /* clear expand on crunch cycle */
    mc_after = v.sprite_mc[0];

    expect_true("exp flop forced set by crunch clear", v.sprite_y_exp_ff[0]);
    expect_u8("crunch MC formula",
        (uint8_t)((0x2au & (mcbase & mc)) | (0x15u & (mcbase | mc))),
        mc_after);
    expect_true("crunched MC not congruent 0 mod 3", (mc_after % 3u) != 0u);

    /* Ordinary +3 counting from MCBASE=9 hits 63 in ~18 rows. After crunch
       the counter walks a longer path (and only hits 63 after a 6-bit wrap);
       require DMA still active past the normal lifetime. */
    abs = 14u;
    for (line = 0u; line < 25u; ++line) {
        abs = advance_vicii(&v, abs, 63u);
    }
    expect_true("crunch keeps dma past 21 rows", v.sprite_active[0]);
    expect_true("mcbase never settled at 63", v.sprite_mcbase[0] != 63u);
}

/* Control: without the cycle-14 bit-magic, the same mid-display clear only
   forces the expand flip-flop and MCBASE still reaches 63. */
static void test_sprite_d017_clear_off_crunch_cycle_ends_normally(void) {
    vicii v;
    char error[256];
    uint32_t line;
    uint64_t abs;

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_write_register(&v, 0xd011, 0x03u);
    v.timing.raster_line = 12u;
    vicii_write_register(&v, 0xd015, 0x01u);
    vicii_write_register(&v, 0xd001, 9u);
    vicii_write_register(&v, 0xd017, 0x01u);

    v.sprite_active[0] = true;
    v.sprite_y_exp_ff[0] = false;
    v.sprite_mcbase[0] = 9u;
    v.sprite_mc[0] = 12u;
    v.timing.cycle_in_line = 20u; /* not the crunch cycle */

    vicii_write_register(&v, 0xd017, 0x00u);
    expect_true("exp flop forced even off crunch cycle", v.sprite_y_exp_ff[0]);
    expect_u8("MC unchanged off crunch cycle", 12u, v.sprite_mc[0]);

    abs = 20u;
    for (line = 0u; line < 25u; ++line) {
        abs = advance_vicii(&v, abs, 63u);
    }
    expect_true("dma ends without bit-magic", !v.sprite_active[0]);
}

/* Phase H test 3: sprites 5, 6, 7 keep BA low on PAL cycles 1..9. */
static void test_sprites567_adjacent_ba_union(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    setup_sprite_ba_test(&v, error, 80u, 0xe0u); /* sprites 5,6,7 enabled */
    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0 */
    expect_true("spr567 rdy high at cycle 0", vicii_rdy_active(&v, abs));

    for (i = 1u; i <= 9u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("spr567 rdy low in union", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs); /* cycle 10 */
    expect_true("spr567 rdy high after union", vicii_rdy_active(&v, abs));
}

/* Phase H test 4: sprites 0,1,2,5,6,7 form two disjoint PAL BA groups.
   The early group is cycles 1..9 and the late group is cycles 54..62. */
static void test_6sprite_ba_early_and_late_windows(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    /* 0xe7 = 0b11100111 → sprites 0,1,2,5,6,7 */
    setup_sprite_ba_test(&v, error, 120u, 0xe7u);
    abs = 0;
    abs = step_vicii(&v, abs); /* cycle 0 */
    expect_true("6spr rdy high at cycle 0", vicii_rdy_active(&v, abs));
    for (i = 1u; i <= 9u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("6spr early rdy low", !vicii_rdy_active(&v, abs));
    }
    for (i = 10u; i < 54u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("6spr rdy high between groups", vicii_rdy_active(&v, abs));
    }
    for (i = 54u; i <= 62u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("6spr late rdy low", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs); /* next line cycle 0 */
    expect_true("6spr rdy high after late group", vicii_rdy_active(&v, abs));
}

/* Phase 2: NTSC 6567R8 sprites 0..2 hold BA low on cycles 55..63. */
static void test_ntsc_sprites012_late_ba_window(void) {
    vicii v;
    char error[256];
    uint64_t abs;
    uint32_t i;

    setup_sprite_ba_test_for_standard(&v, error, VICII_VIDEO_STANDARD_NTSC, 120u, 0x07u);
    abs = 0;
    for (i = 0u; i < 55u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("ntsc rdy high before late group", vicii_rdy_active(&v, abs));
    }
    for (i = 55u; i <= 63u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("ntsc late rdy low", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs); /* cycle 64 */
    expect_true("ntsc late rdy released", vicii_rdy_active(&v, abs));
    expect_u32("ntsc at final cycle", 0, v.timing.cycle_in_line);
}

/* NTSC sprite 4 holds BA low on cycles 63..64 of line N-1 and cycles 0..2
   of line N for its data fetch. */
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

    abs = advance_vicii(&v, 0u, 63u); /* process cycles 0..62 */
    expect_true("ntsc sprite4 rdy high before cycle 63", vicii_rdy_active(&v, abs));
    abs = step_vicii(&v, abs); /* cycle 63 */
    expect_true("ntsc sprite4 rdy low at cycle 63", !vicii_rdy_active(&v, abs));
    abs = step_vicii(&v, abs); /* cycle 64 */
    expect_true("ntsc sprite4 rdy low at cycle 64", !vicii_rdy_active(&v, abs));
    expect_u32("ntsc cross-line wrapped to next line", 0, v.timing.cycle_in_line);

    for (i = 0u; i <= 2u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("ntsc sprite4 cross-line rdy low", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs); /* next line cycle 3 */
    expect_true("ntsc sprite4 rdy released", vicii_rdy_active(&v, abs));
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
        abs = step_vicii(&v, abs);
        expect_true("no sprite rdy stall with disabled sprites", vicii_rdy_active(&v, abs));
    }
}

/* Phase H test 6a: PAL sprite 3 spans cycles 60..62 and next-line 0..1. */
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

    abs = advance_vicii(&v, abs, 59u); /* through cycle 59 */
    expect_true("sprite3 rdy high before cycle 60", vicii_rdy_active(&v, abs));
    for (i = 0u; i < 5u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("sprite3 cross-line rdy low", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs); /* next line cycle 2 */
    expect_true("sprite3 rdy released", vicii_rdy_active(&v, abs));
}

/* Phase H test 6b: PAL sprite 4 spans cycle 62 and next-line cycles 0..3. */
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
    abs = advance_vicii(&v, 0u, 62u); /* through cycle 61 */
    expect_true("sprite4 rdy high before cycle 62", vicii_rdy_active(&v, abs));
    for (i = 0u; i < 5u; ++i) {
        abs = step_vicii(&v, abs);
        expect_true("sprite4 cross-line rdy low", !vicii_rdy_active(&v, abs));
    }
    abs = step_vicii(&v, abs); /* next line cycle 4 */
    expect_true("sprite4 rdy released", vicii_rdy_active(&v, abs));
}

static void test_aec_rdy_pin_transitions_follow_schedule(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x13u); /* DEN=1, YSCROLL=3 */
    v.reg11_delay = v.registers[0x11];
    v.timing.raster_line = 0x33u;
    v.allow_bad_lines = true;

    /* BA/RDY goes low three cycles before the first c-access, while AEC
       remains high until the actual cycle-14 Phi2 bus takeover. */
    v.timing.cycle_in_line = 11u;
    vicii_begin_cycle(&v, NULL, 11u);
    expect_true("badline RDY low during BA lead", !vicii_rdy_active(&v, 11u));
    expect_true("badline AEC high during BA lead", vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    vicii_begin_cycle(&v, NULL, 12u);
    expect_true("badline AEC high on second BA lead", vicii_aec_active(&v));
    vicii_finish_cycle(&v);
    vicii_begin_cycle(&v, NULL, 13u);
    expect_true("badline AEC high on third BA lead", vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    v.timing.cycle_in_line = 14u;
    vicii_begin_cycle(&v, NULL, 14u);
    expect_true("badline RDY low during c-access", !vicii_rdy_active(&v, 14u));
    expect_true("badline AEC low during c-access", !vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    v.timing.cycle_in_line = 53u;
    vicii_begin_cycle(&v, NULL, 53u);
    expect_true("badline RDY remains low on final c-access", !vicii_rdy_active(&v, 53u));
    expect_true("badline AEC remains low for final c-access", !vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    v.timing.cycle_in_line = 54u;
    vicii_begin_cycle(&v, NULL, 54u);
    expect_true("badline RDY remains released", vicii_rdy_active(&v, 54u));
    expect_true("badline AEC releases after c-access", vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    vicii_reset(&v);
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x03u); /* DEN=0: sprite DMA only. */
    v.reg11_delay = v.registers[0x11];
    vicii_write_register(&v, 0xd015, 0x01u);
    v.timing.raster_line = 100u;
    v.sprite_active[0] = true;
    v.sprite_visible[0] = true;

    v.timing.cycle_in_line = 54u;
    vicii_begin_cycle(&v, NULL, 54u);
    expect_true("sprite RDY low during BA lead", !vicii_rdy_active(&v, 54u));
    expect_true("sprite AEC high during BA lead", vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    vicii_begin_cycle(&v, NULL, 55u);
    expect_true("sprite AEC high on second BA lead", vicii_aec_active(&v));
    vicii_finish_cycle(&v);
    vicii_begin_cycle(&v, NULL, 56u);
    expect_true("sprite AEC high on third BA lead", vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    v.timing.cycle_in_line = 57u;
    vicii_begin_cycle(&v, NULL, 57u);
    expect_true("sprite RDY low during data", !vicii_rdy_active(&v, 57u));
    expect_true("sprite AEC low during data", !vicii_aec_active(&v));
    vicii_finish_cycle(&v);

    v.timing.cycle_in_line = 60u;
    vicii_begin_cycle(&v, NULL, 60u);
    expect_true("sprite RDY releases", vicii_rdy_active(&v, 60u));
    expect_true("sprite AEC releases", vicii_aec_active(&v));
    vicii_finish_cycle(&v);
}

static void test_updatevc_observes_phi1_before_same_cycle_cpu_write(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    v.timing.raster_line = 0x33u;
    v.timing.cycle_in_line = 13u;
    v.allow_bad_lines = true;
    v.display_state = true;
    v.vc_base = 0x120u;
    v.vc = 0x155u;
    v.rc = 4u;
    vicii_write_register(&v, 0xd011, 0x12u); /* YSCROLL=2: not a badline. */

    vicii_begin_cycle(&v, NULL, 13u);
    expect_u8("same-cycle pre-write UpdateVc keeps RC", 4u, v.rc);
    expect_u32("UpdateVc restores VCBASE", 0x120u, v.vc);
    expect_u8("UpdateVc resets VMLI", 0u, v.vmli);

    /* This is the CPU's Phi2 store in the same raster cycle. It is too late
       for UpdateVc above, but is visible to subsequent VIC cycles. */
    vicii_write_register(&v, 0xd011, 0x13u);
    vicii_finish_cycle(&v);
    expect_u8("same-cycle write remains too late for RC clear", 4u, v.rc);

    vicii_reset(&v);
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    v.timing.raster_line = 0x33u;
    v.timing.cycle_in_line = 13u;
    v.allow_bad_lines = true;
    v.display_state = true;
    v.vc_base = 0x120u;
    v.vc = 0x155u;
    v.rc = 4u;
    vicii_write_register(&v, 0xd011, 0x13u);
    vicii_begin_cycle(&v, NULL, 13u);
    expect_u8("earlier write clears RC at UpdateVc", 0u, v.rc);
    vicii_finish_cycle(&v);
}

static void test_reg11_delay_latches_before_same_cycle_cpu_write(void) {
    vicii v;
    char error[256];

    expect_true("vicii init", vicii_init(&v, error, sizeof(error)));
    vicii_set_video_standard(&v, VICII_VIDEO_STANDARD_PAL);
    vicii_write_register(&v, 0xd011, 0x1bu);
    v.reg11_delay = 0x1bu;
    v.timing.cycle_in_line = 20u;

    /* VIC Phi2 captures the old value before the CPU owns its half-cycle. */
    vicii_begin_cycle(&v, NULL, 20u);
    expect_u8("VIC Phi2 latches pre-store D011", 0x1bu, v.reg11_delay);
    vicii_write_register(&v, 0xd011, 0x3bu);
    vicii_finish_cycle(&v);
    expect_u8("CPU store does not rewrite VIC delay latch", 0x1bu, v.reg11_delay);

    /* The next Phi1 still consumes 0x1b. Its following Phi2 captures 0x3b for
       the subsequent Phi1, exactly matching VICE's reg11_delay pipeline. */
    vicii_begin_cycle(&v, NULL, 21u);
    expect_u8("following VIC Phi2 advances D011 delay", 0x3bu, v.reg11_delay);
    vicii_finish_cycle(&v);
}

static void test_late_badline_observes_three_cycle_ba_takeover(void) {
    c64_t machine;
    vicii *v;

    reset_machine(&machine);
    v = &machine.vic;
    vicii_write_register(v, 0xd011, 0x13u); /* line $32 does not match YSCROLL 3 */
    vicii_write_register(v, 0xd018, 0x14u);
    v->timing.raster_line = 0x32u;
    v->timing.cycle_in_line = 20u;
    v->allow_bad_lines = true;
    v->timing.prefetch_cycles = 4u;
    machine.bus.ram[0x0403u] = 0x42u;
    machine.bus.color_ram[3] = 0x06u;
    /* VICE open-bus cbuf: ram[PC] & 0x0f during BA lead. Point PC at a blue
       nibble so dummy columns are not stuck at the old hardcoded $0f. */
    machine.bus.cpu_open_bus_pc = 0x1000u;
    machine.bus.ram[0x1000u] = 0xA6u;

    vicii_begin_cycle(v, &machine.bus, 20u);
    expect_true("pre-write line has no badline BA", vicii_rdy_active(v, 20u));
    vicii_write_register(v, 0xd011, 0x12u); /* CPU Phi2 creates badline */
    vicii_finish_cycle(v);

    vicii_begin_cycle(v, &machine.bus, 21u);
    expect_true("late badline asserts BA immediately", !vicii_rdy_active(v, 21u));
    expect_true("late badline first BA cycle keeps AEC high", vicii_aec_active(v));
    expect_u8("late badline first c-access is dummy", 0xffu, v->video_matrix[0]);
    expect_u8("late badline first cbuf is open-bus nibble", 0x06u, v->color_line[0]);
    vicii_finish_cycle(v);

    vicii_begin_cycle(v, &machine.bus, 22u);
    expect_true("late badline second BA cycle keeps AEC high", vicii_aec_active(v));
    expect_u8("late badline second c-access is dummy", 0xffu, v->video_matrix[1]);
    expect_u8("late badline second cbuf is open-bus nibble", 0x06u, v->color_line[1]);
    vicii_finish_cycle(v);

    vicii_begin_cycle(v, &machine.bus, 23u);
    expect_true("late badline third BA cycle keeps AEC high", vicii_aec_active(v));
    expect_u8("late badline third c-access is dummy", 0xffu, v->video_matrix[2]);
    expect_u8("late badline third cbuf is open-bus nibble", 0x06u, v->color_line[2]);
    vicii_finish_cycle(v);

    vicii_begin_cycle(v, &machine.bus, 24u);
    expect_true("late badline fourth BA cycle lowers AEC", !vicii_aec_active(v));
    expect_u8("late badline acquired c-access reads screen", 0x42u, v->video_matrix[3]);
    expect_u8("late badline acquired c-access reads color", 0x06u, v->color_line[3]);
    vicii_finish_cycle(v);
}

static void test_vc_vmli_advance_between_c_accesses(void) {
    c64_t machine;
    vicii *v;
    uint16_t bank;
    uint16_t screen_base;

    reset_machine(&machine);
    v = &machine.vic;
    vicii_write_register(v, 0xd011, 0x13u);
    vicii_write_register(v, 0xd018, 0x14u); /* screen $0400, charset $1000 */
    v->timing.raster_line = 0x33u;
    v->timing.cycle_in_line = 13u;
    v->allow_bad_lines = true;
    v->display_state = true;
    v->vc_base = 0x020u;
    v->vc = 0x099u;
    v->rc = 4u;
    /* This focused test starts at cycle 13; BA cycles 11 and 12 already
       elapsed, leaving one prefetch countdown step before cycle-14 c-data. */
    v->timing.prefetch_cycles = 1u;

    bank = c64_bus_vic_bank_base(&machine.bus);
    screen_base = (uint16_t)(bank + 0x0400u);
    machine.bus.ram[(uint16_t)(screen_base + 0x020u)] = 0x11u;
    machine.bus.ram[(uint16_t)(screen_base + 0x021u)] = 0x22u;
    machine.bus.color_ram[0x020u] = 0x05u;
    machine.bus.color_ram[0x021u] = 0x06u;

    vicii_begin_cycle(v, &machine.bus, 13u);
    expect_u32("UpdateVc starts c/g row at VCBASE", 0x020u, v->vc);
    expect_u8("UpdateVc starts c/g row at VMLI zero", 0u, v->vmli);
    vicii_finish_cycle(v);

    vicii_begin_cycle(v, &machine.bus, 14u);
    expect_u8("cycle14 c-access fills vbuf zero", 0x11u, v->video_matrix[0]);
    expect_u8("cycle14 c-access fills cbuf zero", 0x05u, v->color_line[0]);
    expect_u32("cycle14 does not advance VC", 0x020u, v->vc);
    expect_u8("cycle14 does not advance VMLI", 0u, v->vmli);
    vicii_finish_cycle(v);

    vicii_begin_cycle(v, &machine.bus, 15u);
    expect_u32("cycle15 g-access advances VC", 0x021u, v->vc);
    expect_u8("cycle15 g-access advances VMLI", 1u, v->vmli);
    expect_u8("cycle15 c-access fills next vbuf", 0x22u, v->video_matrix[1]);
    expect_u8("cycle15 c-access fills next cbuf", 0x06u, v->color_line[1]);
    vicii_finish_cycle(v);
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

    /* normal CPU reads defer clear to end of VIC cycle */
    vicii_read_register(&v, 0xd01e);
    expect_u8("d01e still set until cycle ends", 0x05, v.sprite_sprite_collision);
    vicii_step_cycle(&v, NULL, 0u);
    expect_u8("d01e cleared after read cycle", 0x00, v.sprite_sprite_collision);
    v.sprite_background_collision = 0x06;
    vicii_read_register(&v, 0xd01f);
    expect_u8("d01f still set until cycle ends", 0x06, v.sprite_background_collision);
    vicii_step_cycle(&v, NULL, 1u);
    expect_u8("d01f cleared after read cycle", 0x00, v.sprite_background_collision);
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

/* ------------------------------------------------------------------------
 * Phase 1 (C64MVICIIEXPHASES): per-scanline measurement harness.
 *
 * These helpers drive the VIC-II directly, cycle by cycle, over one full
 * frame against a real machine bus, and apply register writes at exact
 * (raster_line, cycle_in_line) points. This mirrors how c64.c applies CPU
 * bus events immediately BEFORE the VIC steps that cycle, so an injected
 * $D011/$D020/... write lands at the same pixel it would on hardware.
 *
 * This is the foundation for the FLI/badline work: it lets later phases
 * assert per-scanline behavior deterministically without running a CPU
 * program, and it provides the reveal-progression metric (lit-row count).
 * ------------------------------------------------------------------------ */

typedef struct expose_injection {
    uint32_t raster_line;
    uint32_t cycle_in_line;
    uint16_t reg;    /* VIC register I/O address (e.g. 0xd011), or RAM address if is_ram */
    uint8_t  value;
    bool     is_ram; /* true: write machine RAM at 'reg'; false: write a VIC register */
} expose_injection;

/* Render exactly one VIC-II frame, applying each injection at its scheduled
   (raster_line, cycle_in_line) BEFORE the VIC steps that cycle. Assumes the
   VIC is at a frame boundary (raster 0, cycle 0), as after reset. */
static void run_vic_frame_with_injections(
    c64_t *machine,
    const expose_injection *injs,
    size_t inj_count,
    c64_frame *out_frame) {
    vicii   *v = &machine->vic;
    uint64_t abs = 0;
    uint32_t guard;
    uint32_t max_cycles =
        v->timing.cycles_per_line * v->timing.lines_per_frame +
        v->timing.cycles_per_line + 16u;

    (void)vicii_consume_frame_complete(v);

    for (guard = 0; guard < max_cycles; guard++) {
        size_t k;
        for (k = 0; k < inj_count; k++) {
            if (injs[k].raster_line == v->timing.raster_line &&
                injs[k].cycle_in_line == v->timing.cycle_in_line) {
                if (injs[k].is_ram) {
                    machine->bus.ram[injs[k].reg] = injs[k].value;
                } else {
                    vicii_write_register(v, injs[k].reg, injs[k].value);
                }
            }
        }
        vicii_step_cycle(v, &machine->bus, abs++);
        if (vicii_consume_frame_complete(v)) {
            expect_true("injected frame copied",
                vicii_copy_completed_frame(v, out_frame, abs));
            return;
        }
    }
    fail("run_vic_frame_with_injections: frame did not complete");
}

/* Live-path YSCROLL soft-scroll. The geometric snapshot path still uses (y, YSCROLL)
   math, so a completed live VIC frame (CPU idle — this injection harness) is required
   to exercise VC/RC addressing. YSCROLL=3 vs 4 must shift glyph row 0 by one raster. */
static void test_live_yscroll_shifts_content(void) {
    c64_t machine;
    c64_frame frame3, frame4;
    uint32_t green = TEST_PALETTE_5;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    run_vic_frame_with_injections(&machine, NULL, 0, &frame3);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1c); /* YSCROLL=4 */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    run_vic_frame_with_injections(&machine, NULL, 0, &frame4);

    expect_u32("live yscroll3 fg at y=51", green, frame3.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_not_u32("live yscroll3 no fg at y=52", green, frame3.pixels[52 * C64_FRAME_WIDTH + 24]);
    expect_not_u32("live yscroll4 no fg at y=51", green, frame4.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("live yscroll4 fg at y=52", green, frame4.pixels[52 * C64_FRAME_WIDTH + 24]);
}

/* Fort Apocalypse: DEN=1, RSEL=0 (24 rows), $D011 = $10 | YSCROLL. Soft scroll
   must still advance content by one raster when YSCROLL goes 3 -> 4. */
static void test_live_yscroll_rsel0_fort_style(void) {
    c64_t machine;
    c64_frame frame3, frame4;
    uint32_t green = TEST_PALETTE_5;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x13); /* DEN=1, RSEL=0, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    run_vic_frame_with_injections(&machine, NULL, 0, &frame3);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x14); /* YSCROLL=4, RSEL=0 */
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    machine.bus.ram[0x0400] = 1;
    machine.bus.color_ram[0] = 5;
    run_vic_frame_with_injections(&machine, NULL, 0, &frame4);

    /* Synthetic char 1 has row 7 = 0x80. YSCROLL=3: RC=7 at y=58; YSCROLL=4: at y=59. */
    expect_u32("rsel0 yscroll3 row7 at y=58", green, frame3.pixels[58 * C64_FRAME_WIDTH + 24]);
    expect_u32("rsel0 yscroll4 row7 at y=59", green, frame4.pixels[59 * C64_FRAME_WIDTH + 24]);
    expect_not_u32("rsel0 yscroll4 y=58 not row7", green, frame4.pixels[58 * C64_FRAME_WIDTH + 24]);
}


/* Fort Apocalypse dual-raster-IRQ pattern (matches VICE + c64m breakpoints):
   - D011=$1F (YSCROLL=7,RSEL=1) from the $F9 IRQ through the top of the frame
   - soft-scroll STA $D011 at $AE2D lands at raster 119 ~cycle 9 (before Bauer
     cycle-14 RC clear). Soft Y 3->4 must shift lower-half content by one raster. */
static void test_fort_dual_zone_yscroll(void) {
    c64_t machine;
    c64_frame f3, f4;
    uint32_t green = TEST_PALETTE_5;
    uint32_t y, found3 = 0xffffffffu, found4 = 0xffffffffu;
    const expose_injection inj3[] = {
        { 0u, 0u, 0xd011u, 0x1fu, false },
        { 119u, 9u, 0xd011u, 0x13u, false }, /* Fort soft $10|3 before cycle 14 */
    };
    const expose_injection inj4[] = {
        { 0u, 0u, 0xd011u, 0x1fu, false },
        { 119u, 9u, 0xd011u, 0x14u, false }, /* Fort soft $10|4 before cycle 14 */
    };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    /* Put unique marker char in several rows so we can see lower-half motion.
       Fill all cells with char 1 so any visible RC0/RC7 is green. */
    {
        uint32_t i;
        for (i = 0; i < 1000u; i++) {
            machine.bus.ram[0x0400u + i] = 1;
            machine.bus.color_ram[i] = 5;
        }
    }
    run_vic_frame_with_injections(&machine, inj3, 2u, &f3);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    {
        uint32_t i;
        for (i = 0; i < 1000u; i++) {
            machine.bus.ram[0x0400u + i] = 1;
            machine.bus.color_ram[i] = 5;
        }
    }
    run_vic_frame_with_injections(&machine, inj4, 2u, &f4);

    /* Find first green in lower half (y>=130) column 24 for each frame */
    for (y = 130u; y < 250u; y++) {
        if (found3 == 0xffffffffu && f3.pixels[y * C64_FRAME_WIDTH + 24] == green) {
            found3 = y;
        }
        if (found4 == 0xffffffffu && f4.pixels[y * C64_FRAME_WIDTH + 24] == green) {
            found4 = y;
        }
    }

    /* Soft scroll of lower half should shift first-green by +1 when yscroll 3->4 */
    if (found3 == 0xffffffffu || found4 == 0xffffffffu) {
        fail("fort dual-zone: no green found in lower half");
    }
    expect_u32("fort dual-zone soft-scroll lower half +1", found3 + 1u, found4);
}



/* Fort soft-scroll timing (YSCROLL=7 until rast 119 ~cycle 9, then soft Y):
   bitmap mode with only one lit bitmap row per 8-line cell group so each lit
   raster is unique. Soft Y 3 vs 4 must shift every lit line at y>=130 by +1. */
static void test_fort_soft_scroll_unique_rows(void) {
    c64_t machine;
    c64_frame f3, f4;
    uint32_t white = 0xffffffffu;
    uint32_t y, i;
    uint32_t lit3[40];
    uint32_t lit4[40];
    uint32_t n3 = 0, n4 = 0;
    /* Cycle 9: Fort soft STA lands before Bauer cycle-14 RC clear, so the
       upper-zone Y=7 badline condition does not commit RC on line 119. */
    const expose_injection inj3[] = {
        { 0u, 0u, 0xd011u, 0x3fu, false }, /* BMM|DEN|RSEL|Y=7 */
        { 119u, 9u, 0xd011u, 0x33u, false }, /* soft Y=3 before cycle 14 */
    };
    const expose_injection inj4[] = {
        { 0u, 0u, 0xd011u, 0x3fu, false },
        { 119u, 9u, 0xd011u, 0x34u, false }, /* soft Y=4 before cycle 14 */
    };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen $0400, bitmap $2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x3f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x00);
    /* Each 8x8 cell: only internal row (cell_index % 8) lit with 0x80.
       Cell index = char_row * 40 + col; use col 0 only meaningful for x=24. */
    for (i = 0; i < 1000u; i++) {
        uint32_t row_in = i % 8u;
        uint32_t b;
        for (b = 0; b < 8u; b++) {
            machine.bus.ram[0x2000u + i * 8u + b] = (b == row_in) ? 0x80u : 0x00u;
        }
        machine.bus.ram[0x0400u + i] = 0x10u; /* white on black */
    }
    run_vic_frame_with_injections(&machine, inj3, 2u, &f3);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x00);
    for (i = 0; i < 1000u; i++) {
        uint32_t row_in = i % 8u;
        uint32_t b;
        for (b = 0; b < 8u; b++) {
            machine.bus.ram[0x2000u + i * 8u + b] = (b == row_in) ? 0x80u : 0x00u;
        }
        machine.bus.ram[0x0400u + i] = 0x10u;
    }
    run_vic_frame_with_injections(&machine, inj4, 2u, &f4);

    for (y = 51u; y < 250u; y++) {
        if (f3.pixels[y * C64_FRAME_WIDTH + 24] == white && n3 < 40u) {
            lit3[n3++] = y;
        }
        if (f4.pixels[y * C64_FRAME_WIDTH + 24] == white && n4 < 40u) {
            lit4[n4++] = y;
        }
    }

    fprintf(stderr, "bitmap fort soft3 lit(%u):", (unsigned)n3);
    for (i = 0; i < n3 && i < 20u; i++) fprintf(stderr, " %u", (unsigned)lit3[i]);
    fprintf(stderr, "\nbitmap fort soft4 lit(%u):", (unsigned)n4);
    for (i = 0; i < n4 && i < 20u; i++) fprintf(stderr, " %u", (unsigned)lit4[i]);
    fprintf(stderr, "\n");

    {
        uint32_t matched = 0, considered = 0;
        for (i = 0; i < n3; i++) {
            uint32_t j;
            if (lit3[i] < 130u) {
                continue;
            }
            considered++;
            for (j = 0; j < n4; j++) {
                if (lit4[j] == lit3[i] + 1u) {
                    matched++;
                    break;
                }
            }
        }
        fprintf(stderr, "bitmap post-130 match %u/%u\n", (unsigned)matched, (unsigned)considered);
        expect_true("fort soft at 119: post-130 lit rows shift +1",
                    considered >= 3u && matched == considered);
    }
}



/* Fort soft Y=7 vs Y=6 at rast 119 cycle 9 (real soft-STA phase): both must
   soft-scroll by about -1 on first post-130 lit row. A +7 FLD snap was the
   pre-fix signature when RC was cleared at cycle 0 while YSCROLL was still 7. */
static void test_fort_soft_y7_to_y6_transition(void) {
    c64_t machine;
    c64_frame f7, f6;
    uint32_t white = 0xffffffffu;
    uint32_t y, i, n7 = 0, n6 = 0;
    uint32_t lit7[40], lit6[40];
    const expose_injection inj7[] = {
        { 0u, 0u, 0xd011u, 0x3fu, false }, /* BMM DEN RSEL Y=7 */
        { 119u, 9u, 0xd011u, 0x37u, false }, /* soft Y=7 before cycle 14 */
    };
    const expose_injection inj6[] = {
        { 0u, 0u, 0xd011u, 0x3fu, false },
        { 119u, 9u, 0xd011u, 0x36u, false }, /* soft Y=6 before cycle 14 */
    };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x00);
    for (i = 0; i < 1000u; i++) {
        uint32_t b;
        for (b = 0; b < 8u; b++) {
            machine.bus.ram[0x2000u + i * 8u + b] = (b == (i % 8u)) ? 0x80u : 0x00u;
        }
        machine.bus.ram[0x0400u + i] = 0x10u;
    }
    run_vic_frame_with_injections(&machine, inj7, 2u, &f7);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x00);
    for (i = 0; i < 1000u; i++) {
        uint32_t b;
        for (b = 0; b < 8u; b++) {
            machine.bus.ram[0x2000u + i * 8u + b] = (b == (i % 8u)) ? 0x80u : 0x00u;
        }
        machine.bus.ram[0x0400u + i] = 0x10u;
    }
    run_vic_frame_with_injections(&machine, inj6, 2u, &f6);

    for (y = 130u; y < 250u; y++) {
        if (f7.pixels[y * C64_FRAME_WIDTH + 24] == white && n7 < 40u) {
            lit7[n7++] = y;
        }
        if (f6.pixels[y * C64_FRAME_WIDTH + 24] == white && n6 < 40u) {
            lit6[n6++] = y;
        }
    }

    fprintf(stderr, "Y7 lit post-130(%u):", (unsigned)n7);
    for (i = 0; i < n7 && i < 12u; i++) fprintf(stderr, " %u", (unsigned)lit7[i]);
    fprintf(stderr, "\nY6 lit post-130(%u):", (unsigned)n6);
    for (i = 0; i < n6 && i < 12u; i++) fprintf(stderr, " %u", (unsigned)lit6[i]);
    fprintf(stderr, "\n");

    if (n7 > 0u && n6 > 0u) {
        int32_t delta = (int32_t)lit6[0] - (int32_t)lit7[0];
        fprintf(stderr, "first-lit delta Y7->Y6: %d (expect soft -1, FLD snap ~+7)\n", (int)delta);
        /* With cycle-14 RC clear, Fort soft-STA phase yields smooth soft scroll. */
        expect_true("fort Y7->Y6 first lit delta is soft-scroll-ish (not +7 FLD snap)",
                    delta >= -3 && delta <= 1);
    } else {
        fail("fort Y7/Y6: no lit rows");
    }
}

/* Soft Y 1->2 was the dual-zone FLD discontinuity when RC cleared at cycle 0
   while YSCROLL was still 7 (first-lit jumped by ~-7). With Fort timing (soft
   write at cycle 9) and cycle-14 RC clear, every soft step must be ~+1. */
static void test_fort_soft_y1_to_y2_smooth(void) {
    c64_t machine;
    c64_frame f1, f2;
    uint32_t white = 0xffffffffu;
    uint32_t y, i, n1 = 0, n2 = 0;
    uint32_t lit1[40], lit2[40];
    const expose_injection inj1[] = {
        { 0u, 0u, 0xd011u, 0x3fu, false },
        { 119u, 9u, 0xd011u, 0x31u, false }, /* soft Y=1 */
    };
    const expose_injection inj2[] = {
        { 0u, 0u, 0xd011u, 0x3fu, false },
        { 119u, 9u, 0xd011u, 0x32u, false }, /* soft Y=2 */
    };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x00);
    for (i = 0; i < 1000u; i++) {
        uint32_t b;
        for (b = 0; b < 8u; b++) {
            machine.bus.ram[0x2000u + i * 8u + b] = (b == (i % 8u)) ? 0x80u : 0x00u;
        }
        machine.bus.ram[0x0400u + i] = 0x10u;
    }
    run_vic_frame_with_injections(&machine, inj1, 2u, &f1);

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3f);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x00);
    for (i = 0; i < 1000u; i++) {
        uint32_t b;
        for (b = 0; b < 8u; b++) {
            machine.bus.ram[0x2000u + i * 8u + b] = (b == (i % 8u)) ? 0x80u : 0x00u;
        }
        machine.bus.ram[0x0400u + i] = 0x10u;
    }
    run_vic_frame_with_injections(&machine, inj2, 2u, &f2);

    for (y = 130u; y < 250u; y++) {
        if (f1.pixels[y * C64_FRAME_WIDTH + 24] == white && n1 < 40u) {
            lit1[n1++] = y;
        }
        if (f2.pixels[y * C64_FRAME_WIDTH + 24] == white && n2 < 40u) {
            lit2[n2++] = y;
        }
    }

    fprintf(stderr, "Y1 lit post-130(%u):", (unsigned)n1);
    for (i = 0; i < n1 && i < 12u; i++) fprintf(stderr, " %u", (unsigned)lit1[i]);
    fprintf(stderr, "\nY2 lit post-130(%u):", (unsigned)n2);
    for (i = 0; i < n2 && i < 12u; i++) fprintf(stderr, " %u", (unsigned)lit2[i]);
    fprintf(stderr, "\n");

    /* Match each Y1 lit row at y>=130 to y+1 in Y2. First-lit-only is wrong at
       the 130 boundary (a new row can enter the window and fake a -7 delta). */
    {
        uint32_t matched = 0, considered = 0;
        for (i = 0; i < n1; i++) {
            uint32_t j;
            if (lit1[i] < 130u) {
                continue;
            }
            considered++;
            for (j = 0; j < n2; j++) {
                if (lit2[j] == lit1[i] + 1u) {
                    matched++;
                    break;
                }
            }
        }
        fprintf(stderr, "Y1->Y2 post-130 match %u/%u\n",
                (unsigned)matched, (unsigned)considered);
        expect_true("fort Y1->Y2 post-130 lit rows shift +1 (not FLD -7)",
                    considered >= 3u && matched == considered);
    }
}

/* Reveal-progression metric: number of raster rows containing at least one
   pixel that differs from bg within x in [x0, x1). This is the signal the
   VICII_EXPOSE_REVEAL doc uses to describe the reveal (it grows, plateaus for
   ~27 frames on hardware, then grows again). */
static uint32_t count_lit_rows(const c64_frame *f, uint32_t x0, uint32_t x1, uint32_t bg) {
    uint32_t y, x, n = 0;

    for (y = 0; y < f->height; y++) {
        for (x = x0; x < x1; x++) {
            if (f->pixels[y * C64_FRAME_WIDTH + x] != bg) {
                n++;
                break;
            }
        }
    }
    return n;
}

/* Fill a full-screen multicolor-free hires bitmap so every pixel in the
   display window is foreground (white), against a black background/border.
   Used to validate both the harness and the lit-row metric. */
static void setup_full_white_bitmap(c64_t *machine) {
    uint32_t i;

    /* $D018 = $18: screen matrix at $0400, bitmap at $2000. */
    c64_bus_write(&machine->bus, 0xd018, 0x18);
    /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 -> $3B. */
    c64_bus_write(&machine->bus, 0xd011, 0x3b);
    /* CSEL=1, XSCROLL=0. */
    c64_bus_write(&machine->bus, 0xd016, 0x08);
    /* Black border. */
    c64_bus_write(&machine->bus, 0xd020, 0x00);

    for (i = 0x2000u; i < 0x4000u; i++) {
        machine->bus.ram[i] = 0xffu;         /* every bitmap bit set */
    }
    for (i = 0x0400u; i < 0x0800u; i++) {
        machine->bus.ram[i] = 0x10u;          /* fg nibble 1 (white), bg nibble 0 (black) */
    }
}

static void test_expose_harness_renders_bitmap_and_metric(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  white = 0xffffffffu; /* palette[1] */
    uint32_t  black = TEST_PALETTE_0;
    uint32_t  in_window, above_window;
    uint32_t  lit;

    reset_machine(&machine);
    setup_full_white_bitmap(&machine);

    run_vic_frame_with_injections(&machine, NULL, 0, &frame);

    /* Inside the display window (row 100, a solid column) -> white foreground. */
    in_window = frame.pixels[100 * C64_FRAME_WIDTH + 160];
    expect_u32("harness bitmap in-window is white", white, in_window);

    /* Above the display window (row 10) -> border/idle black. */
    above_window = frame.pixels[10 * C64_FRAME_WIDTH + 160];
    expect_u32("harness bitmap above-window is black", black, above_window);

    /* Display window is 200 lines (raster 51..250); all lit, nothing else. */
    lit = count_lit_rows(&frame, 24u, 344u, black);
    expect_u32("lit-row metric counts the 200 display rows", 200u, lit);
}

static void test_expose_harness_midline_injection_hits_exact_column(void) {
    c64_t     machine;
    c64_frame frame;
    /* Change border color from red to green partway across raster line 10. */
    const expose_injection injs[] = {
        { 10u, 20u, 0xd020u, 0x05u }, /* at cycle 20 of line 10: border -> green */
    };

    reset_machine(&machine);
    /* DEN=1 so the border region renders the border color (not background). */
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* start red */

    run_vic_frame_with_injections(&machine, injs, 1u, &frame);

    /* Line 10 is entirely top border. Under the dot-anchored paint mapping
       (C64MVICII_SIDEBORDER.md §2.2) column X is painted at cycle 15+(X-24)/8,
       so a write at cycle 20 lands on column 64. VICE 6569 color_latency delays
       $D020 by one pixel: x=63 (cycle 19) and x=64 (first pixel of the write
       cycle) stay red; x=65 is the first green. */
    expect_u32("border last dot before mid-line write is red",
        TEST_PALETTE_2, frame.pixels[10 * C64_FRAME_WIDTH + 63]);
    expect_u32("border write-cycle first pixel still red (6569 1px latency)",
        TEST_PALETTE_2, frame.pixels[10 * C64_FRAME_WIDTH + 64]);
    expect_u32("border second pixel of write cycle is green",
        TEST_PALETTE_5, frame.pixels[10 * C64_FRAME_WIDTH + 65]);

    /* The next line (11) is fully past the write -> entirely green. */
    expect_u32("subsequent line fully takes new border color",
        TEST_PALETTE_5, frame.pixels[11 * C64_FRAME_WIDTH + 90]);
}

/* The machine's real phase order is VIC begin -> CPU Phi2 -> VIC finish.  VICE
   draw_colors8 resolves buffered colour tokens after that CPU store.  c64m's
   horizontal-border compensation keeps two spans pending: only the oldest dot
   stays red; the rest, including the newer span, must use the new $D020. */
static void test_color_latency_resolves_same_cycle_phi2_d020_write(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs = 0;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red */

    while (!(machine.vic.timing.raster_line == 10u &&
             machine.vic.timing.cycle_in_line == 20u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }

    /* This is the production c64.c ordering for a CPU-owned Phi2 write. */
    vicii_begin_cycle(&machine.vic, &machine.bus, abs++);
    c64_bus_write(&machine.bus, 0xd020, 0x05); /* green */
    vicii_finish_cycle(&machine.vic);

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("same-cycle Phi2 D020 frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("same-cycle Phi2 D020 keeps oldest pending dot old",
        TEST_PALETTE_2, frame.pixels[10 * C64_FRAME_WIDTH + 56]);
    expect_u32("same-cycle Phi2 D020 changes next pending dot",
        TEST_PALETTE_5, frame.pixels[10 * C64_FRAME_WIDTH + 57]);
    expect_u32("same-cycle Phi2 D020 changes newer pending span",
        TEST_PALETTE_5, frame.pixels[10 * C64_FRAME_WIDTH + 64]);
}

/* $D021 uses the same unresolved-colour-token ring as $D020 in VICE.  A
   CPU-owned Phi2 store therefore leaves only the oldest pending dot on the old
   background colour; the remaining buffered background dots resolve through
   the new register value.  Nine relies on this exact one-pixel delay for six
   colour splits on every Device raster line. */
static void test_color_latency_resolves_same_cycle_phi2_d021_write(void) {
    c64_t     machine;
    c64_frame frame;
    uint64_t  abs = 0;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen $0400, RAM charset $2000 */
    c64_bus_write(&machine.bus, 0xd021, 0x02); /* red background */
    /* A red sprite begins at x=65 on the sampled row.  It deliberately shares
       the old B0C palette value but is not a D021 token and must stay red. */
    setup_solid_sprite(&machine, 0, 0x0340, 65, 99, 2);

    while (!(machine.vic.timing.raster_line == 100u &&
             machine.vic.timing.cycle_in_line == 20u)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }

    /* Production order: VIC draw tokens, CPU Phi2 store, token resolution. */
    vicii_begin_cycle(&machine.vic, &machine.bus, abs++);
    c64_bus_write(&machine.bus, 0xd021, 0x05); /* green background */
    vicii_finish_cycle(&machine.vic);

    while (!vicii_consume_frame_complete(&machine.vic)) {
        vicii_step_cycle(&machine.vic, &machine.bus, abs++);
    }
    expect_true("same-cycle Phi2 D021 frame",
                vicii_copy_completed_frame(&machine.vic, &frame, abs));

    expect_u32("same-cycle Phi2 D021 keeps oldest pending dot old",
        TEST_PALETTE_2, frame.pixels[100 * C64_FRAME_WIDTH + 56]);
    expect_u32("same-cycle Phi2 D021 changes next pending dot",
        TEST_PALETTE_5, frame.pixels[100 * C64_FRAME_WIDTH + 57]);
    expect_u32("same-cycle Phi2 D021 changes newer pending span",
        TEST_PALETTE_5, frame.pixels[100 * C64_FRAME_WIDTH + 64]);
    expect_u32("same-cycle Phi2 D021 does not recolour matching sprite pixel",
        TEST_PALETTE_2, frame.pixels[100 * C64_FRAME_WIDTH + 65]);
}

/* VICE draw_colors runs on every cycle, including HBLANK. A $D020 write at
   cycle 0 of a border line must drain its 1px color_latency before the first
   painted column (x=0 at cycle 12). Without HBLANK advances, x=0 kept the
   previous colour — the EoD top/bottom black-bar stub. */
static void test_color_latency_drains_during_hblank(void) {
    c64_t     machine;
    c64_frame frame;
    const expose_injection injs[] = {
        { 10u, 0u, 0xd020u, 0x05u }, /* cycle 0 of line 10: border -> green */
    };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd011, 0x1b);
    c64_bus_write(&machine.bus, 0xd020, 0x02); /* red */

    run_vic_frame_with_injections(&machine, injs, 1u, &frame);

    expect_u32("HBLANK $D020 write: first visible pixel is new colour",
        TEST_PALETTE_5, frame.pixels[10 * C64_FRAME_WIDTH + 0]);
    expect_u32("HBLANK $D020 write: rest of line is new colour",
        TEST_PALETTE_5, frame.pixels[10 * C64_FRAME_WIDTH + 90]);
    /* Prior line was still red at its last painted columns. */
    expect_u32("prior line still old border colour",
        TEST_PALETTE_2, frame.pixels[9 * C64_FRAME_WIDTH + 90]);
}

/* Phase 2 (C64MVICIIEXPHASES): address generation is counter-driven. A bad line
   forced mid-character-row resets RC, which shifts the row-in-cell phase for the
   REST of that character row. The distinguishing property vs the old positional
   renderer: after restoring YSCROLL, the affected line has the SAME YSCROLL as
   the un-forced run, so a renderer keyed only to (y, current YSCROLL) would show
   identical output; the counter model instead remembers the RC reset. */
static void test_expose_forced_badline_resets_row_counter(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  white = 0xffffffffu; /* palette[1] */
    uint32_t  black = TEST_PALETTE_0;
    /* Force a bad line at raster 53 (YSCROLL=5, since 53&7==5), then restore
       YSCROLL=3 at raster 54 so line 54 is identical to the un-forced run except
       for the remembered RC reset. */
    const expose_injection injs[] = {
        { 53u, 0u, 0xd011u, 0x3du }, /* BMM|DEN|RSEL|YSCROLL=5 -> bad line at 53 */
        { 54u, 0u, 0xd011u, 0x3bu }, /* BMM|DEN|RSEL|YSCROLL=3 restored */
    };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen $0400, bitmap $2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x00); /* black border */

    /* Cell 0 bitmap: only row-in-cell 1 is lit; row-in-cell 3 is blank. */
    machine.bus.ram[0x2001] = 0xffu; /* rc==1 -> foreground */
    machine.bus.ram[0x2003] = 0x00u; /* rc==3 -> background */
    machine.bus.ram[0x0400] = 0x10u; /* fg nibble 1 (white), bg nibble 0 (black) */

    /* Baseline: no forcing. With YSCROLL=3, raster 54 is RC=3 -> blank cell row. */
    run_vic_frame_with_injections(&machine, NULL, 0, &frame);
    expect_u32("baseline raster 52 (RC=1) is lit",
        white, frame.pixels[52 * C64_FRAME_WIDTH + 24]);
    expect_u32("baseline raster 54 (RC=3) is blank",
        black, frame.pixels[54 * C64_FRAME_WIDTH + 24]);

    /* Forced: the extra bad line at 53 resets RC, so raster 54 is now RC=1 and
       lit -- even though YSCROLL is back to 3, exactly as in the baseline. */
    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18);
    c64_bus_write(&machine.bus, 0xd011, 0x3b);
    c64_bus_write(&machine.bus, 0xd016, 0x08);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    machine.bus.ram[0x2001] = 0xffu;
    machine.bus.ram[0x2003] = 0x00u;
    machine.bus.ram[0x0400] = 0x10u;

    run_vic_frame_with_injections(&machine, injs, 2u, &frame);
    expect_u32("forced bad line shifts raster 54 to RC=1 (now lit)",
        white, frame.pixels[54 * C64_FRAME_WIDTH + 24]);
}

/* Phase 3 (C64MVICIIEXPHASES): the display reads character/colour data from the
   line latch captured at the last bad line, not live RAM. Proof without relying
   on removed code: the SAME screen-RAM write produces different output depending
   on whether it lands BEFORE or AFTER the character row's bad line at raster 51.
   Bitmap mode with all-zero bitmap data shows the background colour, which is the
   low nibble of the latched video-matrix byte. */
static void setup_latch_bitmap(c64_t *machine) {
    reset_machine(machine);
    c64_bus_write(&machine->bus, 0xd018, 0x18); /* screen $0400, bitmap $2000 */
    c64_bus_write(&machine->bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine->bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine->bus, 0xd020, 0x00); /* black border */
    machine->bus.ram[0x2000] = 0x00u;           /* cell 0 rows 0..2: no fg -> show bg colour */
    machine->bus.ram[0x2001] = 0x00u;
    machine->bus.ram[0x2002] = 0x00u;
    machine->bus.ram[0x0400] = 0x12u;           /* vm byte: fg=1 (white), bg=2 (red) */
}

static void test_expose_video_matrix_latched_at_badline(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  red   = TEST_PALETTE_2; /* bg from vm byte low nibble 2 */
    uint32_t  green = TEST_PALETTE_5; /* bg from vm byte low nibble 5 */
    /* Write screen RAM cell 0 -> 0x15 (bg green) BEFORE the bad line at 51. */
    const expose_injection pre[]  = { { 50u, 0u, 0x0400u, 0x15u, true } };
    /* Same write, but AFTER the bad line at 51. */
    const expose_injection post[] = { { 53u, 0u, 0x0400u, 0x15u, true } };

    /* Before the bad line: latch captures the new value -> raster 53 is green. */
    setup_latch_bitmap(&machine);
    run_vic_frame_with_injections(&machine, pre, 1u, &frame);
    expect_u32("pre-badline RAM write is latched (green)",
        green, frame.pixels[53 * C64_FRAME_WIDTH + 24]);

    /* After the bad line: latch keeps the old value for the rest of the row ->
       raster 53 stays red even though screen RAM now reads 0x15. */
    setup_latch_bitmap(&machine);
    run_vic_frame_with_injections(&machine, post, 1u, &frame);
    expect_u32("post-badline RAM write is NOT seen this row (red)",
        red, frame.pixels[53 * C64_FRAME_WIDTH + 24]);
}

/* Phase 4 (C64MVICIIEXPHASES): idle vs display is selected by the sequencer's
   display state, not the fixed 51..251 window. Suppress the bad line at the start
   of a character row (raster 59) by changing YSCROLL so 59 is no longer a bad
   line; display state then stays OFF for that line even though it is inside the
   window, so it must render idle-state graphics (black for bitmap idle) -- not the
   bitmap, and not the B0C background the pre-Phase-4 in-window blank produced. */
static void test_expose_idle_state_shows_idle_graphics_in_window(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  white = 0xffffffffu;  /* bitmap foreground (vm high nibble 1) */
    uint32_t  red   = TEST_PALETTE_2; /* B0C, to prove idle != background blank */
    uint32_t  black = TEST_PALETTE_0; /* bitmap idle output */
    uint32_t  i;
    /* At raster 59 set YSCROLL=0 (59&7==3, so YSCROLL=0 means no bad line). */
    const expose_injection injs[] = { { 59u, 0u, 0xd011u, 0x38u, false } };

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xd018, 0x18); /* screen $0400, bitmap $2000 */
    c64_bus_write(&machine.bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd020, 0x00); /* black border */
    c64_bus_write(&machine.bus, 0xd021, 0x02); /* B0C = red (must NOT appear) */
    for (i = 0x2000u; i < 0x4000u; i++) {
        machine.bus.ram[i] = 0xffu;             /* solid bitmap -> foreground */
    }
    for (i = 0x0400u; i < 0x0800u; i++) {
        machine.bus.ram[i] = 0x10u;             /* fg nibble 1 (white) */
    }

    run_vic_frame_with_injections(&machine, injs, 1u, &frame);

    /* Raster 57 (display state on, RC=6) shows the bitmap foreground. */
    expect_u32("in-display line shows bitmap (white)",
        white, frame.pixels[57 * C64_FRAME_WIDTH + 24]);

    /* Raster 59 (bad line suppressed -> display state off) shows idle graphics
       (black), NOT the bitmap and NOT the red B0C background. */
    expect_u32("suppressed-badline line shows idle graphics (black)",
        black, frame.pixels[59 * C64_FRAME_WIDTH + 24]);
    expect_not_u32("suppressed-badline line is not the B0C background",
        red, frame.pixels[59 * C64_FRAME_WIDTH + 24]);
    expect_not_u32("suppressed-badline line is not the bitmap",
        white, frame.pixels[59 * C64_FRAME_WIDTH + 24]);
}

/* Phase 5 (C64MVICIIEXPHASES): Bad Line Condition is evaluated every cycle, so a
   $D011 write after cycle 0 can still force a bad line on that line (FLI/expose).
   Bauer 3.7.2 rule 2: RC is cleared only at cycle 14 if the condition still
   holds then -- a write after cycle 14 does not restart the row. Force at
   cycle 12 of raster 53 (after cycle 0, before cycle 14); restore YSCROLL at
   raster 54. Line 54 shows RC=1 only because the mid-line force restarted the
   row -- impossible with cycle-0-only evaluation. */
static void setup_rc_probe_bitmap(c64_t *machine) {
    uint32_t i;
    reset_machine(machine);
    c64_bus_write(&machine->bus, 0xd018, 0x18); /* screen $0400, bitmap $2000 */
    c64_bus_write(&machine->bus, 0xd011, 0x3b); /* BMM=1, DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine->bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine->bus, 0xd020, 0x00); /* black border */
    for (i = 0x2000u; i < 0x2008u; i++) {
        machine->bus.ram[i] = 0x00u;            /* cell 0: all rows blank... */
    }
    machine->bus.ram[0x2001] = 0xffu;           /* ...except RC==1 -> foreground */
    machine->bus.ram[0x0400] = 0x10u;           /* fg nibble 1 (white), bg nibble 0 (black) */
}

static void test_expose_midline_d011_forces_badline(void) {
    c64_t     machine;
    c64_frame frame;
    uint32_t  white = 0xffffffffu;
    uint32_t  black = TEST_PALETTE_0;
    /* Force a bad line at cycle 12 of raster 53 (YSCROLL=5, 53&7==5) so the
       condition is present at the cycle-14 RC clear; restore YSCROLL at 54. */
    const expose_injection injs[] = {
        { 53u, 12u, 0xd011u, 0x3du, false }, /* mid-line before cycle 14: YSCROLL=5 */
        { 54u,  0u, 0xd011u, 0x3bu, false }, /* restore YSCROLL=3 */
    };
    /* Control: same force after cycle 14 must NOT restart the row (Bauer). */
    const expose_injection late[] = {
        { 53u, 20u, 0xd011u, 0x3du, false },
        { 54u,  0u, 0xd011u, 0x3bu, false },
    };

    /* Baseline: no force. Raster 52 is RC=1 (lit); raster 54 is RC=3 (blank). */
    setup_rc_probe_bitmap(&machine);
    run_vic_frame_with_injections(&machine, NULL, 0, &frame);
    expect_u32("baseline raster 52 is RC=1 (lit)",
        white, frame.pixels[52 * C64_FRAME_WIDTH + 24]);
    expect_u32("baseline raster 54 is RC=3 (blank)",
        black, frame.pixels[54 * C64_FRAME_WIDTH + 24]);

    /* Mid-line force at cycle 12 of raster 53 restarts the row, so raster 54 is
       now RC=1 and lit. Only possible if the bad line is evaluated after cycle 0. */
    setup_rc_probe_bitmap(&machine);
    run_vic_frame_with_injections(&machine, injs, 2u, &frame);
    expect_true("mid-line $D011 force makes raster 54 RC=1 (lit)",
        frame.pixels[54 * C64_FRAME_WIDTH + 24] != black);

    /* Write after cycle 14: condition can open display/c-accesses, but RC is not
       cleared, so line 54 stays at RC=3 (blank) like the baseline. */
    setup_rc_probe_bitmap(&machine);
    run_vic_frame_with_injections(&machine, late, 2u, &frame);
    expect_u32("post-cycle-14 $D011 force does not reset RC (raster 54 blank)",
        black, frame.pixels[54 * C64_FRAME_WIDTH + 24]);
}


/* EoD open-border checker join: AA sprite in the open left border must phase-
   match AA matrix at x=24. Covers plain CSEL=0 and the EoD dodge $D016=$62
   (CSEL=0, XSCROLL=2) restored to $E8 at cycle 14. */
static void test_open_border_sprite_matrix_checker_joins(void) {
    c64_t     machine;
    c64_frame frame;
    c64_rom_set roms;
    uint64_t  abs;
    int       i, y, pass;
    uint32_t  yellow = TEST_PALETTE_7;
    uint32_t  blue = TEST_PALETTE_6;

    build_roms(&roms);
    for (i = 0; i < 8; i++) {
        roms.character[1 * 8 + i] = 0xAAu;
    }
    reset_machine_with_roms(&machine, &roms);

    for (i = 0; i < 1000; i++) {
        machine.bus.ram[0x0400 + i] = 1;
        machine.bus.color_ram[i] = 7;
    }
    for (i = 0; i < 63; i++) {
        machine.bus.ram[0x0340 + i] = 0xAAu;
    }
    machine.bus.ram[0x07f8] = 13;
    c64_bus_write(&machine.bus, 0xd000, 0);
    c64_bus_write(&machine.bus, 0xd001, 55);
    c64_bus_write(&machine.bus, 0xd027, 7);
    c64_bus_write(&machine.bus, 0xd015, 1);
    c64_bus_write(&machine.bus, 0xd020, 0x00);
    c64_bus_write(&machine.bus, 0xd021, 0x06);

    for (pass = 0; pass < 2; pass++) {
        unsigned seams = 0, rows = 0, doubles = 0, yellow_px = 0;
        const char *label;
        uint8_t dodge, restore;

        if (pass == 0) {
            label = "CSEL0 XSCROLL0";
            dodge = 0x00; restore = 0x08;
        } else {
            label = "CSEL0 XSCROLL2 (EoD $62)";
            dodge = 0x62; restore = 0xe8;
        }

        c64_bus_write(&machine.bus, 0xd011, 0x1b);
        c64_bus_write(&machine.bus, 0xd016, restore);

        abs = 0;
        while (!vicii_consume_frame_complete(&machine.vic)) {
            uint32_t line = machine.vic.timing.raster_line;
            uint32_t cyc = machine.vic.timing.cycle_in_line;
            if (line >= 50u && line <= 250u && cyc == 56u) {
                c64_bus_write(&machine.bus, 0xd016, dodge);
            }
            if (line >= 50u && line <= 250u && cyc == 14u) {
                c64_bus_write(&machine.bus, 0xd016, restore);
            }
            vicii_step_cycle(&machine.vic, &machine.bus, abs++);
        }
        expect_true(label, vicii_copy_completed_frame(&machine.vic, &frame, abs));

        for (y = 60; y <= 70; y++) {
            int x;
            for (x = 0; x < 24; x++) {
                if (frame.pixels[y * C64_FRAME_WIDTH + x] == yellow) {
                    yellow_px++;
                }
            }
            {
                uint32_t a = frame.pixels[y * C64_FRAME_WIDTH + 23];
                uint32_t b = frame.pixels[y * C64_FRAME_WIDTH + 24];
                if ((a == yellow || a == blue) && (b == yellow || b == blue)) {
                    rows++;
                    if (a == b) seams++;
                }
                if (frame.pixels[y * C64_FRAME_WIDTH + 16] ==
                    frame.pixels[y * C64_FRAME_WIDTH + 17]) {
                    doubles++;
                }
            }
        }
        fprintf(stderr, "%s: rows=%u seams=%u doubles=%u yellow_border=%u\n",
                label, rows, seams, doubles, yellow_px);
        {
            int x;
            fprintf(stderr, "  y=65:");
            for (x = 0; x < 32; x++) {
                uint32_t p = frame.pixels[65 * C64_FRAME_WIDTH + x];
                fprintf(stderr, "%c", p == yellow ? 'Y' : (p == blue ? '.' : '?'));
            }
            fprintf(stderr, "\n");
        }
        if (yellow_px < 20u) {
            fail("sprite not visible in left border");
        }
        if (rows < 8u) {
            fail("not enough sample rows");
        }
        if (seams * 5u > rows) {
            fprintf(stderr, "FAIL %s seam %u/%u\n", label, seams, rows);
            fail("sprite/matrix seam");
        }
        if (doubles * 5u > rows) {
            fprintf(stderr, "FAIL %s doubles %u/%u\n", label, doubles, rows);
            fail("x=16/17 doubles");
        }
    }
}

int main(void) {
    test_config_frame_timing();
    test_vicii_reset_state();
    test_raster_progression();
    test_frame_boundary_carries_rc_vmli_display();
    test_irq_status_high_bit_reports_enabled_pending_irq();
    test_raster_compare_write_triggers_same_line_irq();
    test_d011_yscroll_write_does_not_retrigger_same_line_irq();
    test_sprite_collision_registers_read_clear();
    test_bad_line_ba_asserts_at_cycle_11();
    test_frame_snapshot_geometry_and_regions();
    test_reset_screen_starts_clear();
    test_character_rendering_uses_screen_char_rom_and_color_ram();
    test_ntsc_character_rendering_uses_ntsc_top_border();
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
    test_sprite_midline_x_write_affects_remaining_dots();
    test_sprite_y50_touches_top_border_fully_revealed();
    test_sprite_sprite_collision_priority_and_irq();
    test_sprite_background_priority_and_collision();
    test_border_hides_sprites_but_collision_latches();
    test_sprite_bg_collision_in_top_border_idle();
    test_lft_nine_detect_sets_carry();
    test_live_bottom_border_can_be_opened_for_sprites();
    test_ntsc_live_bottom_border_can_be_opened_for_sprites();
    test_live_deep_bottom_border_sprite_is_painted();
    test_d015_clear_keeps_active_sprite_display();
    test_live_right_side_border_opens();
    test_live_side_border_wrong_cycle_stays_closed();
    test_live_side_border_flip_flop_persists_left();
    test_live_side_border_reveals_sprite();
    test_open_border_sprite_matrix_checker_joins();
    test_den_clear_main_border_keeps_d020_full_height();
    test_live_side_border_shows_zero_graphics();
    test_live_open_border_right_edge_xscroll_delayed();
    test_live_mcm_toggle_reaches_column0_same_cycle();
    test_live_mcm_idle_ghost_is_hires();
    test_allow_bad_lines_latched_at_line_30();
    test_set_vborder_latch_sticky_until_top();
    test_den_clear_blanks_text_display();
    test_den_clear_mid_display_keeps_graphics_pipeline();
    test_den_clear_keeps_sprite_visible();
    test_den_clear_idle_has_no_sprite_background_collision();
    test_d016_unused_high_bits_read_as_1();
    test_color_register_high_nibble_reads_as_1();
    test_unused_register_block_reads_ff();
    test_unused_register_block_mirrored_reads_ff();
    test_d018_no_phase_g_masking();
    /* Phase H: sprite BA windows */
    test_sprite5_ba_window_within_line();
    test_vicii_bus_schedule_reports_c_and_sprite_accesses();
    test_sprite_dma_off_stops_late_phi2_slots();
    test_sprite_crunch_keeps_dma_past_21_rows();
    test_sprite_d017_clear_off_crunch_cycle_ends_normally();
    test_sprites567_adjacent_ba_union();
    test_6sprite_ba_early_and_late_windows();
    test_ntsc_sprites012_late_ba_window();
    test_ntsc_sprite4_cross_line_ba();
    test_inactive_sprites_no_ba();
    test_sprite3_cross_line_ba();
    test_sprite4_cross_line_ba();
    test_aec_rdy_pin_transitions_follow_schedule();
    test_updatevc_observes_phi1_before_same_cycle_cpu_write();
    test_reg11_delay_latches_before_same_cycle_cpu_write();
    test_late_badline_observes_three_cycle_ba_takeover();
    test_vc_vmli_advance_between_c_accesses();
    test_vicii_debug_read_raster();
    test_vicii_debug_read_d011_raster_bit8();
    test_vicii_debug_read_collision_no_clear();
    test_vicii_debug_read_irq_status_no_clear();
    test_vicii_debug_read_forced_high_bits();
    test_expose_harness_renders_bitmap_and_metric();
    test_expose_harness_midline_injection_hits_exact_column();
    test_color_latency_resolves_same_cycle_phi2_d020_write();
    test_color_latency_resolves_same_cycle_phi2_d021_write();
    test_color_latency_drains_during_hblank();
    test_expose_forced_badline_resets_row_counter();
    test_live_yscroll_shifts_content();
    test_live_yscroll_rsel0_fort_style();
    test_fort_dual_zone_yscroll();
    test_fort_soft_scroll_unique_rows();
    test_fort_soft_y7_to_y6_transition();
    test_fort_soft_y1_to_y2_smooth();
    test_expose_video_matrix_latched_at_badline();
    test_expose_idle_state_shows_idle_graphics_in_window();
    test_expose_midline_d011_forces_badline();
    return 0;
}
