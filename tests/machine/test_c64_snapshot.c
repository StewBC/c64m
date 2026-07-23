#include "c64_snapshot.h"
#include "c64_rom.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum {
    TEST_RESET_VECTOR = 0xe000
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

static void expect_false(const char *name, bool value) {
    if (value) {
        fprintf(stderr, "%s: expected false\n", name);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %04x, got %04x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(
            stderr,
            "%s: expected %llu, got %llu\n",
            name,
            (unsigned long long)expected,
            (unsigned long long)actual);
        exit(1);
    }
}

static void build_roms(c64_rom_set *roms) {
    size_t i;

    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    for (i = 0; i < sizeof(roms->basic); ++i) {
        roms->basic[i] = (uint8_t)(0xa0u ^ (uint8_t)i);
    }
    for (i = 0; i < sizeof(roms->kernal); ++i) {
        roms->kernal[i] = (uint8_t)(0xe0u ^ (uint8_t)(i * 3u));
    }
    for (i = 0; i < sizeof(roms->character); ++i) {
        roms->character[i] = (uint8_t)(0xd0u ^ (uint8_t)(i * 5u));
    }
    roms->kernal[0x1ffc] = (uint8_t)(TEST_RESET_VECTOR & 0xffu);
    roms->kernal[0x1ffd] = (uint8_t)(TEST_RESET_VECTOR >> 8);
    roms->kernal[TEST_RESET_VECTOR - 0xe000u] = 0xea;
}

static void init_ready_machine(c64_t *machine) {
    c64_rom_set roms;
    c64_config config;
    char error[256];

    build_roms(&roms);
    c64_init(machine);
    memset(&config, 0, sizeof(config));
    config.video_standard = C64_VIDEO_STANDARD_PAL;
    config.emulate_1541 = 0;
    c64_set_config(machine, &config);
    expect_true("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void fill_pattern(uint8_t *data, size_t size, uint8_t seed) {
    size_t i;

    for (i = 0; i < size; ++i) {
        data[i] = (uint8_t)(seed + (uint8_t)(i * 17u));
    }
}

static void install_drive(c64_t *machine) {
    uint8_t image[C64_DRIVE_D64_STANDARD_SIZE];
    c64_drive_directory_entry entry;

    fill_pattern(image, sizeof(image), 0x31);
    memset(&entry, 0, sizeof(entry));
    entry.raw_type = 0x82;
    entry.type = C64_DRIVE_FILE_PRG;
    entry.first_track = 18;
    entry.first_sector = 1;
    memcpy(entry.filename, "SNAPTEST", 8);
    entry.filename_length = 8;
    entry.block_count = 12;

    expect_true(
        "mount d64",
        c64_mount_d64(
            machine,
            8,
            image,
            sizeof(image),
            &entry,
            1,
            "test.d64",
            "SNAP DISK",
            "ID",
            "2A",
            664) == C64_DRIVE_STATUS_OK);
}

static void prepare_interesting_state(c64_t *machine) {
    uint8_t roml[C64_CARTRIDGE_ROM_BANK_SIZE];
    uint8_t romh[C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    fill_pattern(machine->bus.ram, sizeof(machine->bus.ram), 0x10);
    fill_pattern(machine->bus.color_ram, sizeof(machine->bus.color_ram), 0x02);
    machine->bus.cpu_port_direction = 0x3f;
    machine->bus.cpu_port_data = 0x35;
    machine->bus.screen_ram_writes = 11;
    machine->bus.color_ram_writes = 12;
    machine->bus.vic_register_writes = 13;
    machine->bus.cia1_register_writes = 14;
    machine->bus.cia2_register_writes = 15;
    machine->bus.sid_register_writes = 16;
    machine->bus.vic_bank_base = 0x8000;

    machine->cpu.cpu.pc = 0xc123;
    machine->cpu.cpu.opcode_pc = 0xc120;
    machine->cpu.cpu.sp = 0x01e7;
    machine->cpu.cpu.A = 0x44;
    machine->cpu.cpu.X = 0x55;
    machine->cpu.cpu.Y = 0x66;
    machine->cpu.cpu.flags = 0xa5;
    machine->cpu.cpu.address_16 = 0x2345;
    machine->cpu.cpu.scratch_16 = 0x4567;
    machine->cpu.cpu.page_fault = 1;
    machine->cpu.cpu.irq_defer = 2;
    machine->cpu.cpu.irq_defer_i = 3;
    machine->cpu.cpu.opcode_active = 1;
    machine->cpu.cpu.class = 0x76543210u;
    machine->cpu.cpu.cycles = 123456;
    machine->cpu.cpu.irq_entries = 7;
    machine->cpu.cpu.nmi_entries = 8;

    machine->vic.registers[0x11] = 0x9b;
    machine->vic.timing.cycles_per_line = VICII_PAL_CYCLES_PER_LINE;
    machine->vic.timing.lines_per_frame = VICII_PAL_LINES_PER_FRAME;
    machine->vic.timing.cycle_in_line = 23;
    machine->vic.timing.raster_line = 101;
    machine->vic.timing.frame_number = 4;
    machine->vic.timing.frame_complete = true;
    machine->vic.timing.standard = VICII_VIDEO_STANDARD_PAL;
    machine->vic.timing.raster_compare = 250;
    machine->vic.timing.ba_low_until_abs = 1111;
    machine->vic.timing.sprite_ba_low_until_abs = 2222;
    machine->vic.timing.prefetch_cycles = 2;
    machine->vic.completed_frame_ready = true;
    machine->vic.vc = 0x0123;
    machine->vic.vc_base = 0x0234;
    machine->vic.vmli = 17;
    machine->vic.rc = 5;
    machine->vic.display_state = true;
    machine->vic.bad_line = true;
    fill_pattern(machine->vic.video_matrix, sizeof(machine->vic.video_matrix), 0x40);
    fill_pattern(machine->vic.color_line, sizeof(machine->vic.color_line), 0x05);
    fill_pattern(machine->vic.g_line, sizeof(machine->vic.g_line), 0x80);
    machine->vic.reg11_delay = 0x5b;
    machine->vic.irq_status = 0x81;
    machine->vic.irq_enable = 0x0f;
    machine->vic.sprite_mc[3] = 9;
    machine->vic.sprite_active[3] = true;
    machine->vic.sprite_visible[3] = true;
    machine->vic.sprite_y_exp_ff[3] = true;
    machine->vic.sprite_data[3][1] = 0xaa;
    machine->vic.sprite_line_enabled[3] = true;
    machine->vic.sprite_line_x[3] = 322;
    machine->vic.sprite_line_x_expand[3] = true;
    machine->vic.sprite_line_multicolor[3] = true;
    machine->vic.sprite_line_color[3] = 7;
    machine->vic.sprite_line_mm0 = 8;
    machine->vic.sprite_line_mm1 = 9;
    machine->vic.sprite_priority = 0x12;
    machine->vic.sprite_sprite_collision = 0x34;
    machine->vic.sprite_background_collision = 0x56;
    machine->vic.vertical_border_active = false;
    /* Set the full geometry, not just width/height: vicii_prepare_frame always
       writes all four fields together, so a frame carrying a width but a zero
       stride/format is a shape no real machine holds - and the snapshot reader
       now rejects it. */
    machine->vic.working_frame.width = C64_FRAME_PAL_WIDTH;
    machine->vic.working_frame.height = C64_FRAME_HEIGHT;
    machine->vic.working_frame.stride_bytes =
        C64_FRAME_WIDTH * sizeof(machine->vic.working_frame.pixels[0]);
    machine->vic.working_frame.pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    machine->vic.working_frame.pixels[17] = 0xff112233u;
    machine->vic.completed_frame.width = C64_FRAME_PAL_WIDTH;
    machine->vic.completed_frame.height = C64_FRAME_HEIGHT;
    machine->vic.completed_frame.stride_bytes =
        C64_FRAME_WIDTH * sizeof(machine->vic.completed_frame.pixels[0]);
    machine->vic.completed_frame.pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    machine->vic.completed_frame.pixels[19] = 0xff445566u;

    machine->cia1.registers[0] = 0x12;
    machine->cia1.registers[1] = 0x34;
    machine->cia1.timer_a.latch = 0x1001;
    machine->cia1.timer_a.counter = 0x1002;
    machine->cia1.timer_a.underflow = true;
    machine->cia1.timer_a.output_level = false;
    machine->cia1.timer_a.pulse_active = true;
    machine->cia1.timer_b.latch = 0x2001;
    machine->cia1.timer_b.counter = 0x2002;
    machine->cia1.tod.tenth = 1;
    machine->cia1.tod.seconds = 0x22;
    machine->cia1.tod.minutes = 0x33;
    machine->cia1.tod.hours = 0x84;
    machine->cia1.tod_alarm.hours = 0x85;
    machine->cia1.tod_latch.minutes = 0x44;
    machine->cia1.tod_cycle_accum = 123;
    machine->cia1.interrupt_flags = 0x05;
    machine->cia1.interrupt_mask = 0x07;
    machine->cia1.icr_reads = 9;
    machine->cia1.icr_writes = 10;
    machine->cia1.interrupt_assertions = 11;
    machine->cia1.tod_latched = true;
    machine->cia1.cnt_pulse = true;
    machine->cia2 = machine->cia1;
    machine->cia2.port_input = machine->cia1.port_input;
    machine->cia2.port_input_user = machine;
    machine->cia2.registers[0] = 0x9a;

    machine->sid.regs[0] = 0x77;
    for (i = 0; i < 3; ++i) {
        machine->sid.voices[i].freq = (uint16_t)(0x1000u + i);
        machine->sid.voices[i].pulse_width = (uint16_t)(0x0800u + i);
        machine->sid.voices[i].control = (uint8_t)(0x11u + i);
        machine->sid.voices[i].attack_decay = (uint8_t)(0x22u + i);
        machine->sid.voices[i].sustain_release = (uint8_t)(0x33u + i);
        machine->sid.voices[i].phase = 0x00123456u + (uint32_t)i;
        machine->sid.voices[i].noise_lfsr = 0x00700000u + (uint32_t)i;
        machine->sid.voices[i].envelope = (uint8_t)(0x40u + i);
        machine->sid.voices[i].env_state = SID_ENV_DECAY;
        machine->sid.voices[i].env_counter = 0.25 + (double)i;
        machine->sid.voices[i].last_wave = 0.5f;
    }
    machine->sid.filter_cutoff = 0x456;
    machine->sid.filter_res_route = 0x97;
    machine->sid.mode_volume = 0x1f;
    machine->sid.filter_lp = 0.1f;
    machine->sid.filter_bp = 0.2f;
    machine->sid.filter_hp = 0.3f;
    machine->sid.dc_block_prev_input = 0.4f;
    machine->sid.dc_block_prev_output = 0.5f;
    machine->sid.hfroll_state = 0.6f;
    machine->sid.last_sample = 0.7f;
    machine->sid.sample_output_enabled = false;
    machine->sid.voice3_osc_read = 0x88;
    machine->sid.voice3_env_read = 0x99;

    machine->keyboard.rows[2] = 0xef;
    machine->joystick1 = C64_JOYSTICK_UP | C64_JOYSTICK_FIRE;
    machine->joystick2 = C64_JOYSTICK_LEFT;
    machine->iec_external_pull = C64_IEC_CLK;
    machine->iec_external_pull_other = C64_IEC_DATA;
    machine->iec_external_pull_drive8 = C64_IEC_ATN;
    machine->clock.cycle = 9000;
    machine->clock.cpu_cycles = 8000;
    machine->clock.vic_cycles = 7000;
    machine->clock.cia_cycles = 6000;
    machine->keyboard_events = 5;
    machine->restore_requests = 6;
    machine->restore_pending = true;
    machine->cia2_nmi_line = true;
    machine->cpu_cycles_remaining = 3;
    machine->instruction_complete = true;

    fill_pattern(roml, sizeof(roml), 0x80);
    fill_pattern(romh, sizeof(romh), 0xa0);
    expect_true(
        "attach cartridge",
        c64_attach_generic_cartridge(
            machine, roml, sizeof(roml), romh, sizeof(romh), 0, 0, error, sizeof(error)));
    install_drive(machine);
}

static uint8_t *save_snapshot(const c64_t *machine, size_t *out_size) {
    uint8_t *bytes;
    size_t size = c64_snapshot_size(machine);
    size_t written;

    if (size == 0) {
        fail("snapshot size was zero");
    }
    bytes = (uint8_t *)malloc(size);
    if (bytes == NULL) {
        fail("alloc snapshot");
    }
    written = c64_snapshot_save(machine, bytes, size);
    if (written != size) {
        fail("snapshot save size mismatch");
    }
    *out_size = size;
    return bytes;
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)(value >> 24);
}

static void mutate_machine(c64_t *machine) {
    char error[256];

    c64_debug_write_ram(machine, 0x1234, 0xfe);
    machine->cpu.cpu.pc = 0x1111;
    machine->vic.registers[0x11] = 0x22;
    machine->cia1.timer_a.counter = 0x3333;
    machine->sid.voices[2].phase = 0x00444444u;
    c64_detach_cartridge(machine);
    c64_unmount_drive(machine, 8);
    c64_set_joystick(machine, 1, 0);
    expect_true("reset after mutation", c64_reset(machine, error, sizeof(error)));
}

static void assert_restored_state(const c64_t *machine) {
    expect_u8("restored ram", 0x84, machine->bus.ram[0x1234]);
    expect_u8("restored color ram", (uint8_t)(0x02u + (uint8_t)(17u * 7u)), machine->bus.color_ram[7]);
    expect_u16("restored pc", 0xc123, machine->cpu.cpu.pc);
    expect_u8("restored vic reg", 0x9b, machine->vic.registers[0x11]);
    expect_u8("restored vic vmli", 17, machine->vic.vmli);
    expect_u8("restored vic graphics latch", 0x80, machine->vic.g_line[0]);
    expect_u8("restored vic reg11 delay", 0x5b, machine->vic.reg11_delay);
    expect_u8("restored vic BA prefetch", 2, machine->vic.timing.prefetch_cycles);
    expect_true("restored vic frame ready", machine->vic.completed_frame_ready);
    expect_true("restored vic active sprite", machine->vic.sprite_active[3]);
    expect_u16("restored cia timer", 0x1002, machine->cia1.timer_a.counter);
    expect_u8("restored cia2 reg", 0x9a, machine->cia2.registers[0]);
    expect_u8("restored sid reg", 0x77, machine->sid.regs[0]);
    expect_u8("restored sid osc", 0x88, machine->sid.voice3_osc_read);
    expect_true("restored cartridge attached", c64_cartridge_attached(machine));
    expect_u8("restored cartridge roml", 0x80, c64_debug_read_cpu_map(machine, 0x8000));
    expect_true("restored drive mounted", machine->drives[0].mounted);
    expect_u64("restored drive image size", C64_DRIVE_D64_STANDARD_SIZE, machine->drives[0].image_size);
    expect_u8("restored drive byte", 0x31, machine->drives[0].image_bytes[0]);
    expect_u8("restored keyboard row", 0xef, machine->keyboard.rows[2]);
    expect_u8("restored joystick", C64_JOYSTICK_UP | C64_JOYSTICK_FIRE, machine->joystick1);
    expect_u64("restored clock", 9000, machine->clock.cycle);
    expect_true("restored restore pending", machine->restore_pending);
}

static void test_round_trip(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    uint8_t *again;
    size_t snapshot_size;
    size_t again_size;

    init_ready_machine(&source);
    init_ready_machine(&target);
    prepare_interesting_state(&source);
    snapshot = save_snapshot(&source, &snapshot_size);

    mutate_machine(&target);
    expect_true("load snapshot", c64_snapshot_load(&target, snapshot, snapshot_size));
    assert_restored_state(&target);

    again = save_snapshot(&target, &again_size);
    if (again_size != snapshot_size || memcmp(again, snapshot, snapshot_size) != 0) {
        fail("snapshot did not round-trip byte-identically");
    }

    free(snapshot);
    free(again);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

static void test_ignores_unknown_chunk(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    uint8_t *extended;
    size_t snapshot_size;
    const uint8_t payload[4] = {1, 2, 3, 4};

    init_ready_machine(&source);
    init_ready_machine(&target);
    prepare_interesting_state(&source);
    snapshot = save_snapshot(&source, &snapshot_size);

    extended = (uint8_t *)malloc(snapshot_size + 12);
    if (extended == NULL) {
        fail("alloc extended snapshot");
    }
    memcpy(extended, snapshot, snapshot_size);
    write_le32(extended + snapshot_size, 0x54534554u); /* TEST */
    write_le32(extended + snapshot_size + 4, sizeof(payload));
    memcpy(extended + snapshot_size + 8, payload, sizeof(payload));

    mutate_machine(&target);
    expect_true("load snapshot with unknown chunk",
                c64_snapshot_load(&target, extended, snapshot_size + 12));
    assert_restored_state(&target);

    free(snapshot);
    free(extended);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

static void test_reject_and_leave_unchanged(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    uint8_t *before;
    uint8_t *after;
    size_t snapshot_size;
    size_t before_size;
    size_t after_size;

    init_ready_machine(&source);
    init_ready_machine(&target);
    prepare_interesting_state(&source);
    prepare_interesting_state(&target);
    snapshot = save_snapshot(&source, &snapshot_size);
    before = save_snapshot(&target, &before_size);

    snapshot[0] ^= 0xffu;
    expect_false("reject bad magic", c64_snapshot_load(&target, snapshot, snapshot_size));
    after = save_snapshot(&target, &after_size);
    if (before_size != after_size || memcmp(before, after, before_size) != 0) {
        fail("failed load changed target machine");
    }

    free(snapshot);
    free(before);
    free(after);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

/* Locate a stored frame header (width, height, stride, format) in a saved
   snapshot. The VIC chunk writes two of them; the first is enough. Returns 0 if
   not found - the pattern is far too specific to appear in RAM by chance. */
static size_t find_frame_header(const uint8_t *snapshot, size_t size) {
    size_t offset;

    if (size < 16) {
        return 0;
    }
    for (offset = 0; offset + 16 <= size; ++offset) {
        uint32_t width = read_le32(snapshot + offset);
        uint32_t height = read_le32(snapshot + offset + 4);
        uint32_t stride = read_le32(snapshot + offset + 8);
        uint32_t format = read_le32(snapshot + offset + 12);

        if ((width == (uint32_t)C64_FRAME_PAL_WIDTH ||
             width == (uint32_t)C64_FRAME_NTSC_WIDTH) &&
            height > 0u && height <= (uint32_t)C64_FRAME_HEIGHT &&
            stride == (uint32_t)C64_FRAME_WIDTH * sizeof(uint32_t) &&
            format == (uint32_t)C64_FRAME_PIXEL_FORMAT_ARGB8888) {
            return offset;
        }
    }
    return 0;
}

/* A frame header that does not match this build's framebuffer must fail the
   load outright. Before the reader validated it, the fixed-size pixel loop ran
   past the mismatched header, silently misaligning every following chunk. */
static void test_reject_frame_geometry_mismatch(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    size_t snapshot_size;
    size_t header;
    uint32_t original_width;
    uint32_t original_height;
    uint32_t original_stride;

    init_ready_machine(&source);
    init_ready_machine(&target);
    prepare_interesting_state(&source);
    snapshot = save_snapshot(&source, &snapshot_size);

    header = find_frame_header(snapshot, snapshot_size);
    if (header == 0) {
        fail("frame header not found in snapshot");
    }
    original_width = read_le32(snapshot + header);
    original_height = read_le32(snapshot + header + 4);
    original_stride = read_le32(snapshot + header + 8);

    write_le32(snapshot + header, original_width + 8u);
    expect_false("reject frame width mismatch",
                 c64_snapshot_load(&target, snapshot, snapshot_size));

    write_le32(snapshot + header, original_width);
    write_le32(snapshot + header + 8, original_stride + 32u);
    expect_false("reject frame stride mismatch",
                 c64_snapshot_load(&target, snapshot, snapshot_size));

    write_le32(snapshot + header + 8, original_stride);
    write_le32(snapshot + header + 4, (uint32_t)C64_FRAME_HEIGHT + 1u);
    expect_false("reject frame height overflow",
                 c64_snapshot_load(&target, snapshot, snapshot_size));

    /* Restoring every field makes it load again, proving the rejections came
       from the geometry check and not from collateral damage to the buffer. */
    write_le32(snapshot + header + 4, original_height);
    expect_true("accept repaired frame header",
                c64_snapshot_load(&target, snapshot, snapshot_size));
    assert_restored_state(&target);

    free(snapshot);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

static void test_reject_rom_hash_mismatch(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    size_t snapshot_size;

    init_ready_machine(&source);
    init_ready_machine(&target);
    prepare_interesting_state(&source);
    snapshot = save_snapshot(&source, &snapshot_size);

    target.bus.kernal_rom[0] ^= 0xffu;
    expect_false("reject rom mismatch", c64_snapshot_load(&target, snapshot, snapshot_size));

    free(snapshot);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

static void test_reject_mid_instruction_state(void) {
    c64_t machine;

    init_ready_machine(&machine);
    machine.pending_cpu_trace_active = true;
    expect_u64("mid-instruction snapshot size", 0, c64_snapshot_size(&machine));

    machine.pending_cpu_trace_active = false;
    machine.cpu.micro_active = true;
    expect_u64("mid-microcycle snapshot size", 0, c64_snapshot_size(&machine));
}

static void scribble_via(via6522 *v, uint8_t seed) {
    v->ora = (uint8_t)(0x10u + seed);
    v->orb = (uint8_t)(0x20u + seed);
    v->ddra = (uint8_t)(0x30u + seed);
    v->ddrb = (uint8_t)(0x40u + seed);
    v->port_a_in = (uint8_t)(0x50u + seed);
    v->port_b_in = (uint8_t)(0x60u + seed);
    v->t1_counter = (uint16_t)(0x1000u + seed);
    v->t1_latch = (uint16_t)(0x2000u + seed);
    v->t1_running = 1;
    v->t1_pb7_state = (int)(seed & 1);
    v->t2_counter = (uint16_t)(0x3000u + seed);
    v->t2_latch_low = (uint8_t)(0x70u + seed);
    v->t2_running = 1;
    v->sr = (uint8_t)(0x80u + seed);
    v->acr = (uint8_t)(0x90u + seed);
    v->pcr = (uint8_t)(0xa0u + seed);
    v->ifr = (uint8_t)(0x0fu & seed);
    v->ier = (uint8_t)(0x0eu);
    v->ca1_last = (uint8_t)(seed & 1u);
}

static void scribble_drive_core(c1541 *drive, uint8_t seed) {
    size_t i;
    uint8_t *track;

    fill_pattern(drive->ram, sizeof(drive->ram), seed);
    drive->rom_loaded = 1;
    drive->device_number = seed == 0x40u ? 8 : 9;
    drive->cpu_cycles_remaining = 3u + seed;
    drive->via2_t1_pb7_last = 1;

    drive->cpu.cpu.pc = (uint16_t)(0x0500u + seed);
    drive->cpu.cpu.opcode_pc = (uint16_t)(0x0501u + seed);
    drive->cpu.cpu.sp = 0x01f0u;
    drive->cpu.cpu.A = (uint8_t)(0x11u + seed);
    drive->cpu.cpu.X = (uint8_t)(0x22u + seed);
    drive->cpu.cpu.Y = (uint8_t)(0x33u + seed);
    drive->cpu.cpu.flags = 0xa5;
    drive->cpu.cpu.address_16 = 0x1234;
    drive->cpu.cpu.scratch_16 = 0x5678;
    drive->cpu.cpu.page_fault = 1;
    drive->cpu.cpu.irq_defer = 2;
    drive->cpu.cpu.irq_defer_i = 1;
    drive->cpu.cpu.opcode_active = 1;
    drive->cpu.cpu.class = 0x11223344u;
    drive->cpu.cpu.cycles = 999u + seed;
    drive->cpu.cpu.irq_entries = 4;
    drive->cpu.cpu.nmi_entries = 5;
    drive->cpu.bus_access_kind = C6510_BUS_ACCESS_OPCODE_FETCH;
    drive->cpu.micro_active = 1;
    drive->cpu.micro_opcode = 0xa9;
    drive->cpu.micro_phase = 2;
    drive->cpu.micro_branch_taken = 1;
    drive->cpu.micro_target = 0x0600;
    drive->cpu.micro_interrupt_vector = 0xfffe;
    drive->cpu.micro_is_interrupt = 0;

    scribble_via(&drive->via1, (uint8_t)(seed + 1u));
    scribble_via(&drive->via2, (uint8_t)(seed + 2u));

    drive->media.enabled = 1;
    drive->media.motor_on = 1;
    drive->media.motor_ready = 1;
    drive->media.motor_spin_left = 1234;
    drive->media.half_track = 18;
    drive->media.stepper_phase = 2;
    drive->media.density = 1;
    drive->media.bit_acc = 0x55aa;
    drive->media.ref_cycle = 100;
    drive->media.bit_event_ref = 101;
    drive->media.ref_tick_accum = 102;
    drive->media.ref_advance = 3;
    drive->media.req_ref_cycles = 4;
    drive->media.flux_acc = 5;
    drive->media.filter_counter = 6;
    drive->media.filter_state = 1;
    drive->media.filter_last_state = 0;
    drive->media.no_flux_cycles = 7;
    drive->media.flux_rand = 0xdeadbeefu;
    drive->media.ue7_counter = 8;
    drive->media.uf4_counter = 9;
    drive->media.head_bit_pos = 0x4000u + seed;
    drive->media.shift10 = 0x2aa;
    drive->media.in_sync = 1;
    drive->media.bits_in_byte = 3;
    drive->media.shifting_byte = 0xab;
    drive->media.port_a_byte = 0xcd;
    drive->media.byte_ready = 1;
    drive->media.so_pulse = 0;
    drive->media.so_delay = 12;
    drive->media.writing = 0;
    drive->media.write_bits_left = 0;
    drive->media.write_shift = 0;
    drive->media.last_write_bit = 1;
    drive->media.tracks_valid = 1;
    drive->media.from_g64 = seed != 0x40u ? 1 : 0;
    drive->media.built_from = (const uint8_t *)(uintptr_t)1;
    drive->media.built_size = 100;
    drive->media.built_from_seq = 0;
    drive->media.attach_left = 50;
    drive->media.attach_pending = 0;

    track = (uint8_t *)malloc(64);
    if (track == NULL) {
        fail("alloc scribble track");
    }
    for (i = 0; i < 64; ++i) {
        track[i] = (uint8_t)(seed + i);
    }
    drive->media.halves[2].data = track;
    drive->media.halves[2].length = 64;
    drive->media.halves[2].density = 2;
    drive->media.halves[2].dirty = 1;
}

static void expect_drive_core(const char *label, const c1541 *expected, const c1541 *actual) {
    char name[128];

    snprintf(name, sizeof(name), "%s ram", label);
    if (memcmp(expected->ram, actual->ram, sizeof(expected->ram)) != 0) {
        fail(name);
    }
    snprintf(name, sizeof(name), "%s rom_loaded", label);
    expect_u8(name, (uint8_t)expected->rom_loaded, (uint8_t)actual->rom_loaded);
    snprintf(name, sizeof(name), "%s device", label);
    expect_u8(name, (uint8_t)expected->device_number, (uint8_t)actual->device_number);
    snprintf(name, sizeof(name), "%s pc", label);
    expect_u16(name, expected->cpu.cpu.pc, actual->cpu.cpu.pc);
    snprintf(name, sizeof(name), "%s micro_active", label);
    expect_u8(name, expected->cpu.micro_active, actual->cpu.micro_active);
    snprintf(name, sizeof(name), "%s micro_opcode", label);
    expect_u8(name, expected->cpu.micro_opcode, actual->cpu.micro_opcode);
    snprintf(name, sizeof(name), "%s micro_phase", label);
    expect_u8(name, expected->cpu.micro_phase, actual->cpu.micro_phase);
    snprintf(name, sizeof(name), "%s via1.ora", label);
    expect_u8(name, expected->via1.ora, actual->via1.ora);
    snprintf(name, sizeof(name), "%s via2.t1", label);
    expect_u16(name, expected->via2.t1_counter, actual->via2.t1_counter);
    snprintf(name, sizeof(name), "%s head_bit_pos", label);
    if (expected->media.head_bit_pos != actual->media.head_bit_pos) {
        fail(name);
    }
    snprintf(name, sizeof(name), "%s track length", label);
    if (expected->media.halves[2].length != actual->media.halves[2].length) {
        fail(name);
    }
    snprintf(name, sizeof(name), "%s track data", label);
    if (actual->media.halves[2].data == NULL ||
        memcmp(expected->media.halves[2].data, actual->media.halves[2].data, 64) != 0) {
        fail(name);
    }
    snprintf(name, sizeof(name), "%s callbacks rewired", label);
    expect_true(name, actual->cpu.read != NULL && actual->cpu.write != NULL &&
                          actual->cpu.user == (void *)actual && actual->c64 != NULL);
}

static void mount_pattern_d64(c64_t *machine, uint8_t device, uint8_t seed, const char *name) {
    uint8_t image[C64_DRIVE_D64_STANDARD_SIZE];
    c64_drive_directory_entry entry;

    fill_pattern(image, sizeof(image), seed);
    memset(&entry, 0, sizeof(entry));
    entry.raw_type = 0x82;
    entry.type = C64_DRIVE_FILE_PRG;
    entry.first_track = 17;
    entry.first_sector = 0;
    memcpy(entry.filename, "BOTHDRV", 7);
    entry.filename_length = 7;
    entry.block_count = 1;
    expect_true(
        name,
        c64_mount_d64(
            machine,
            device,
            image,
            sizeof(image),
            &entry,
            1,
            name,
            "BOTH",
            "ID",
            "2A",
            664) == C64_DRIVE_STATUS_OK);
}

static void install_fake_1541_rom(c1541 *drive) {
    size_t i;

    for (i = 0; i < C1541_ROM_SIZE; ++i) {
        drive->rom[i] = (uint8_t)(0xc0u ^ (uint8_t)i);
    }
    drive->rom_loaded = 1;
}

static void test_1541_core_round_trip(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    uint8_t *again;
    size_t snapshot_size;
    size_t again_size;
    uint8_t host_rom8[C1541_ROM_SIZE];
    uint8_t host_rom9[C1541_ROM_SIZE];
    size_t i;

    init_ready_machine(&source);
    init_ready_machine(&target);
    source.config.emulate_1541 = 1;
    source.config.media_1541 = 1;
    target.config.emulate_1541 = 1;
    target.config.media_1541 = 1;

    install_fake_1541_rom(&source.drive8);
    install_fake_1541_rom(&source.drive9);
    install_fake_1541_rom(&target.drive8);
    install_fake_1541_rom(&target.drive9);
    /* Distinct host ROM on target to prove host ROM is preserved. */
    for (i = 0; i < C1541_ROM_SIZE; ++i) {
        target.drive8.rom[i] = (uint8_t)(0x55u + (uint8_t)i);
        target.drive9.rom[i] = (uint8_t)(0x66u + (uint8_t)i);
    }
    memcpy(host_rom8, target.drive8.rom, sizeof(host_rom8));
    memcpy(host_rom9, target.drive9.rom, sizeof(host_rom9));

    mount_pattern_d64(&source, 8, 0x41, "d64-8");
    /* Device 9 gets a second D64 (G64 path covered by mid-transfer acceptance). */
    mount_pattern_d64(&source, 9, 0x42, "d64-9");

    scribble_drive_core(&source.drive8, 0x40u);
    scribble_drive_core(&source.drive9, 0x50u);
    source.drive8.media.built_from = source.drives[0].image_bytes;
    source.drive8.media.built_size = source.drives[0].image_size;
    source.drive8.media.built_from_seq = source.drives[0].image_content_seq;
    source.drive9.media.built_from = source.drives[1].image_bytes;
    source.drive9.media.built_size = source.drives[1].image_size;
    source.drive9.media.built_from_seq = source.drives[1].image_content_seq;
    source.clock.drive_accum = 0x12345678ull;
    source.clock.drive_synced_cycle = 0xabcdefull;

    /* C64 CPU must not be mid-micro (save guard); drive micro is allowed. */
    source.cpu.micro_active = 0;
    source.pending_cpu_trace_active = false;

    snapshot = save_snapshot(&source, &snapshot_size);
    expect_true("1541 included flag",
                (snapshot[16] & (uint8_t)C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED) != 0 ||
                    ((snapshot[16] | (snapshot[17] << 8) | (snapshot[18] << 16) |
                      (snapshot[19] << 24)) &
                     C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED) != 0);

    mutate_machine(&target);
    target.config.emulate_1541 = 1;
    target.config.media_1541 = 1;
    install_fake_1541_rom(&target.drive8);
    install_fake_1541_rom(&target.drive9);
    for (i = 0; i < C1541_ROM_SIZE; ++i) {
        target.drive8.rom[i] = host_rom8[i];
        target.drive9.rom[i] = host_rom9[i];
    }

    expect_true("load 1541 core snapshot", c64_snapshot_load(&target, snapshot, snapshot_size));
    expect_drive_core("drive8", &source.drive8, &target.drive8);
    expect_drive_core("drive9", &source.drive9, &target.drive9);
    if (memcmp(target.drive8.rom, host_rom8, sizeof(host_rom8)) != 0 ||
        memcmp(target.drive9.rom, host_rom9, sizeof(host_rom9)) != 0) {
        fail("host 1541 ROM was not preserved");
    }
    expect_u64("drive_accum", source.clock.drive_accum, target.clock.drive_accum);
    expect_u64("drive_synced_cycle", source.clock.drive_synced_cycle, target.clock.drive_synced_cycle);
    expect_u8("media_1541", 1, (uint8_t)target.config.media_1541);
    expect_true("both slots mounted", target.drives[0].mounted && target.drives[1].mounted);

    again = save_snapshot(&target, &again_size);
    if (again_size != snapshot_size || memcmp(again, snapshot, snapshot_size) != 0) {
        fail("1541 core snapshot did not round-trip byte-identically");
    }

    free(snapshot);
    free(again);
    c1541_media_free_tracks(&source.drive8.media);
    c1541_media_free_tracks(&source.drive9.media);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

static uint8_t *read_entire_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;

    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)size;
    return bytes;
}

static void init_real_rom_machine(c64_t *machine, int emulate_1541) {
    c64_rom_set roms;
    c64_config config;
    char error[256];

    c64_rom_set_init(&roms);
    expect_true(
        "load system rom",
        c64_rom_load_combined_64c(&roms, C64M_SOURCE_DIR "/roms/system.rom", error, sizeof(error)));
    expect_true(
        "load character rom",
        c64_rom_load_character(&roms, C64M_SOURCE_DIR "/roms/character.rom", error, sizeof(error)));
    c64_init(machine);
    memset(&config, 0, sizeof(config));
    config.video_standard = C64_VIDEO_STANDARD_PAL;
    config.emulate_1541 = emulate_1541;
    c64_set_config(machine, &config);
    expect_true("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

/* v10 widened the framebuffer, so pre-v10 files carry a frame that cannot be
   reconstructed into the current buffer. They are sunset, not migrated: the
   header version check must reject them and leave the machine untouched. The v8
   fixture is kept as the evidence that the rejection is real rather than a
   constant nobody exercises. */
static void test_legacy_versions_rejected(void) {
    c64_t machine;
    uint8_t *fixture;
    uint8_t *before;
    uint8_t *after;
    size_t fixture_size;
    size_t before_size;
    size_t after_size;
    char path[512];
    uint32_t version;

    snprintf(path, sizeof(path), "%s/tests/fixtures/legacy_v8.c64state", C64M_SOURCE_DIR);
    fixture = read_entire_file(path, &fixture_size);
    expect_true("read legacy v8 fixture", fixture != NULL);
    version = (uint32_t)fixture[4] | ((uint32_t)fixture[5] << 8) | ((uint32_t)fixture[6] << 16) |
              ((uint32_t)fixture[7] << 24);
    expect_u64("fixture version 8", 8, version);
    expect_true("fixture predates the minimum", version < C64_SNAPSHOT_VERSION_MIN);

    init_real_rom_machine(&machine, 0);
    before = save_snapshot(&machine, &before_size);
    expect_false("reject legacy v8 fixture",
                 c64_snapshot_load(&machine, fixture, fixture_size));
    after = save_snapshot(&machine, &after_size);
    if (before_size != after_size || memcmp(before, after, before_size) != 0) {
        fail("rejected legacy load changed the machine");
    }

    free(fixture);
    free(before);
    free(after);
    c64_unmount_all_drives(&machine);
}

static void test_synthetic_v9_deferred_resets_drives(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    size_t snapshot_size;
    uint32_t flags;
    size_t i;
    int found_dr8c = 0;

    init_ready_machine(&source);
    init_ready_machine(&target);
    source.config.emulate_1541 = 1;
    install_fake_1541_rom(&source.drive8);
    install_fake_1541_rom(&source.drive9);
    install_fake_1541_rom(&target.drive8);
    install_fake_1541_rom(&target.drive9);

    fill_pattern(source.drive8.ram, sizeof(source.drive8.ram), 0x77);
    source.drive8.cpu.cpu.pc = 0x0600;
    source.drive8.cpu.micro_active = 0;
    source.cpu.micro_active = 0;

    snapshot = save_snapshot(&source, &snapshot_size);
    flags = (uint32_t)snapshot[16] | ((uint32_t)snapshot[17] << 8) | ((uint32_t)snapshot[18] << 16) |
            ((uint32_t)snapshot[19] << 24);
    expect_true("source has included flag", (flags & C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED) != 0);

    /* Clear INCLUDED so the loader takes the legacy hard-reset path. Core chunks
       remain; the loader ignores them when the flag is off. */
    flags = (flags & ~C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED) | C64_SNAPSHOT_FLAG_1541_STATE_DEFERRED;
    snapshot[16] = (uint8_t)(flags & 0xffu);
    snapshot[17] = (uint8_t)((flags >> 8) & 0xffu);
    snapshot[18] = (uint8_t)((flags >> 16) & 0xffu);
    snapshot[19] = (uint8_t)((flags >> 24) & 0xffu);
    /* META flags must match header flags. */
    for (i = 32; i + 8 < snapshot_size; i++) {
        uint32_t tag = (uint32_t)snapshot[i] | ((uint32_t)snapshot[i + 1] << 8) |
                       ((uint32_t)snapshot[i + 2] << 16) | ((uint32_t)snapshot[i + 3] << 24);
        if (tag == 0x4154454du /* META little-endian 'META' */) {
            uint32_t meta_flags_off = i + 8;
            snapshot[meta_flags_off + 0] = snapshot[16];
            snapshot[meta_flags_off + 1] = snapshot[17];
            snapshot[meta_flags_off + 2] = snapshot[18];
            snapshot[meta_flags_off + 3] = snapshot[19];
            break;
        }
    }

    /* Confirm DR8C is still present (flag clear is what selects reset). */
    for (i = 32; i + 4 < snapshot_size; i++) {
        if (snapshot[i] == 'D' && snapshot[i + 1] == 'R' && snapshot[i + 2] == '8' &&
            snapshot[i + 3] == 'C') {
            found_dr8c = 1;
            break;
        }
    }
    expect_true("DR8C still present", found_dr8c);

    fill_pattern(target.drive8.ram, sizeof(target.drive8.ram), 0x11);
    target.drive8.cpu.cpu.pc = 0x0999;
    target.config.emulate_1541 = 1;

    expect_true("load synthetic deferred", c64_snapshot_load(&target, snapshot, snapshot_size));
    /* Hard-reset clears RAM and reloads reset vector from ROM. */
    expect_true("drive ram cleared by legacy reset", target.drive8.ram[0] == 0 &&
                                                           target.drive8.ram[1] == 0);
    expect_true("drive not left at scribbled pc", target.drive8.cpu.cpu.pc != 0x0600);

    free(snapshot);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

static void test_load_clears_host_micro_state(void) {
    c64_t source;
    c64_t target;
    uint8_t *snapshot;
    size_t snapshot_size;
    char error[256];
    uint64_t i;
    uint16_t pc_after_load;

    init_ready_machine(&source);
    init_ready_machine(&target);
    prepare_interesting_state(&source);
    source.cpu.micro_active = 0;
    source.cpu.cpu.pc = 0xe000; /* NOP at test reset vector area */
    source.bus.ram[0xe000] = 0xea; /* if banking shows RAM; ROMs may map here */
    source.cpu.cpu.opcode_active = 0;
    snapshot = save_snapshot(&source, &snapshot_size);

    /* Target is free-running mid-instruction with foreign micro state — the
       real UI load-while-running case that left the title bar on BRK. */
    mutate_machine(&target);
    target.cpu.micro_active = 1;
    target.cpu.micro_opcode = 0x00; /* BRK micro residue */
    target.cpu.micro_phase = 3;
    target.cpu.micro_branch_taken = 1;
    target.cpu.micro_target = 0x1234;
    target.cpu.micro_interrupt_vector = 0xfffe;
    target.cpu.micro_is_interrupt = 1;
    target.cpu.cpu.pc = 0x0000;
    target.cpu_prev_between_stall = true;
    target.cpu_deferred_interrupt = C6510_INTERRUPT_IRQ;

    expect_true("load over mid-micro host", c64_snapshot_load(&target, snapshot, snapshot_size));
    expect_u8("micro_active cleared", 0, target.cpu.micro_active);
    expect_u8("micro_opcode cleared", 0, target.cpu.micro_opcode);
    expect_u8("micro_phase cleared", 0, target.cpu.micro_phase);
    expect_u8("micro_is_interrupt cleared", 0, target.cpu.micro_is_interrupt);
    expect_false("between-stall cleared", target.cpu_prev_between_stall);
    expect_true(
        "deferred irq cleared",
        target.cpu_deferred_interrupt == C6510_INTERRUPT_NONE);
    expect_u16("restored pc", 0xe000, target.cpu.cpu.pc);

    pc_after_load = target.cpu.cpu.pc;
    for (i = 0; i < 200ull; ++i) {
        expect_true("step after load", c64_step_cycle(&target, error, sizeof(error)));
        /* Runtime BRK auto-pause keys off opcode 0x00 at PC on a boundary. A
           corrupted micro resume can leave PC at 0 with RAM[0]==0. */
        if (!target.cpu.micro_active && !target.pending_cpu_trace_active &&
            target.cpu.cpu.pc == 0x0000 &&
            c64_debug_read_cpu_map(&target, 0x0000) == 0x00) {
            fail("wedged on BRK at $0000 after load over mid-micro host");
        }
    }
    expect_u8("still not mid-micro after steps", 0, target.cpu.micro_active);
    expect_true("cpu advanced cycles", target.cpu.cpu.cycles > source.cpu.cpu.cycles);
    (void)pc_after_load;

    free(snapshot);
    c64_unmount_all_drives(&source);
    c64_unmount_all_drives(&target);
}

int main(void) {
    test_round_trip();
    test_ignores_unknown_chunk();
    test_reject_and_leave_unchanged();
    test_reject_frame_geometry_mismatch();
    test_reject_rom_hash_mismatch();
    test_reject_mid_instruction_state();
    test_1541_core_round_trip();
    test_legacy_versions_rejected();
    test_synthetic_v9_deferred_resets_drives();
    test_load_clears_host_micro_state();
    return 0;
}
