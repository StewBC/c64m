#include "c64_snapshot.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    machine->vic.working_frame.width = C64_FRAME_WIDTH;
    machine->vic.working_frame.height = C64_FRAME_HEIGHT;
    machine->vic.working_frame.pixels[17] = 0xff112233u;
    machine->vic.completed_frame.width = C64_FRAME_WIDTH;
    machine->vic.completed_frame.height = C64_FRAME_HEIGHT;
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

int main(void) {
    test_round_trip();
    test_ignores_unknown_chunk();
    test_reject_and_leave_unchanged();
    test_reject_rom_hash_mismatch();
    test_reject_mid_instruction_state();
    return 0;
}
