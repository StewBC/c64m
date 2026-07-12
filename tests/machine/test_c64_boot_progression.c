#include "c64.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum {
    TEST_RESET_VECTOR = 0xe000,
    TEST_NMI_VECTOR = 0xe080,
    TEST_IRQ_VECTOR = 0xe100,
    TEST_COLOR_GREEN = 0xff56ac4du,
    TEST_COLOR_BLUE = 0xff2e2c9bu,
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

static void expect_u32(const char *name, uint32_t expected, uint32_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %08x, got %08x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64_gt(const char *name, uint64_t lhs, uint64_t rhs) {
    if (lhs <= rhs) {
        fprintf(stderr, "%s: expected %llu > %llu\n", name, (unsigned long long)lhs, (unsigned long long)rhs);
        exit(1);
    }
}

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

static const char *find_existing_path(const char *const *paths, size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (file_exists(paths[i])) {
            return paths[i];
        }
    }

    return paths[0];
}

static void build_roms(c64_rom_set *roms, uint16_t reset_vector) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffc] = (uint8_t)(reset_vector & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(reset_vector >> 8);
    roms->kernal[0x1ffa] = (uint8_t)(TEST_NMI_VECTOR & 0xff);
    roms->kernal[0x1ffb] = (uint8_t)(TEST_NMI_VECTOR >> 8);
    roms->kernal[0x1ffe] = (uint8_t)(TEST_IRQ_VECTOR & 0xff);
    roms->kernal[0x1fff] = (uint8_t)(TEST_IRQ_VECTOR >> 8);
    roms->kernal[TEST_NMI_VECTOR - 0xe000] = 0xea;
    roms->kernal[TEST_IRQ_VECTOR - 0xe000] = 0xea;
    roms->character[1 * 8] = 0x80;
    roms->character[1 * 8 + 3] = 0x10;
}

static void copy_to_kernal(c64_rom_set *roms, uint16_t address, const uint8_t *program, size_t size) {
    size_t offset = address - 0xe000;
    size_t i;

    if (address < 0xe000 || offset + size > C64_KERNAL_ROM_SIZE) {
        fail("test program does not fit in KERNAL ROM");
    }

    for (i = 0; i < size; i++) {
        roms->kernal[offset + i] = program[i];
    }
}

static void reset_machine(c64_t *machine, const c64_rom_set *roms) {
    c64_config cfg;
    char error[256];

    c64_init(machine);

    /* PAL is the canonical video standard for all tests: the 384×272 pixel
       buffer matches PAL dimensions and border compare values (top=51, left=24). */
    cfg.video_standard = C64_VIDEO_STANDARD_PAL;
    c64_set_config(machine, &cfg);

    expect_true("install ROMs", c64_install_roms(machine, roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void step_instructions(c64_t *machine, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; i++) {
        expect_true("step instruction", c64_step_instruction(machine, error, sizeof(error)));
    }
}

static void step_cycles(c64_t *machine, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; i++) {
        expect_true("step cycle", c64_step_cycle(machine, error, sizeof(error)));
    }
}

static void test_irq_entry_uses_machine_vectors_and_stack(void) {
    static const uint8_t program[] = {
        0xea,
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_snapshot cpu;
    c64_machine_snapshot machine_state;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);

    cia_write_register(&machine.cia1, 0x04, 0x01);
    cia_write_register(&machine.cia1, 0x05, 0x00);
    cia_write_register(&machine.cia1, 0x0d, 0x81);
    cia_write_register(&machine.cia1, 0x0e, 0x11);
    /* Advance CIA1 until the (delayed) interrupt pin actually asserts; the CPU
       samples cia_interrupt_line, which lags the latched flag by one cycle, so a
       fixed cycle count here rots whenever timer/pin timing is tuned. */
    {
        int guard = 0;
        while (!cia_interrupt_line(&machine.cia1) && guard++ < 32) {
            cia_step_cycle(&machine.cia1);
        }
    }
    machine.cpu.cpu.I = 0;

    step_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);
    c64_copy_machine_snapshot(&machine, &machine_state);

    expect_u16("irq vector entered", TEST_IRQ_VECTOR, cpu.pc);
    expect_u64_gt("irq entry counted", machine_state.irq_entries, 0);
    expect_u8("irq disables further irq", 1, (uint8_t)((cpu.p >> 2) & 1u));
    expect_u8("irq stack pc high", 0xe0, c64_bus_read(&machine.bus, 0x0100));
    expect_u8("irq stack pc low", 0x00, c64_bus_read(&machine.bus, 0x01ff));
}

static void test_restore_enters_nmi_vector(void) {
    static const uint8_t program[] = {
        0xea,
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_snapshot cpu;
    c64_machine_snapshot machine_state;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);

    c64_restore(&machine);
    step_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);
    c64_copy_machine_snapshot(&machine, &machine_state);

    expect_u16("restore nmi vector entered", TEST_NMI_VECTOR, cpu.pc);
    expect_u64_gt("nmi entry counted", machine_state.nmi_entries, 0);
    expect_u64_gt("restore request counted", machine_state.restore_requests, 0);
}

static void test_rom_driven_screen_and_device_writes_update_frame(void) {
    static const uint8_t program[] = {
        0xa9, 0x01,       /* LDA #$01 */
        0x8d, 0x00, 0x04, /* STA $0400 */
        0xa9, 0x05,       /* LDA #$05 */
        0x8d, 0x00, 0xd8, /* STA $D800 */
        0xa9, 0x06,       /* LDA #$06 */
        0x8d, 0x21, 0xd0, /* STA $D021 */
        0xa9, 0x1b,       /* LDA #$1B */
        0x8d, 0x11, 0xd0, /* STA $D011: DEN=1, RSEL=1, YSCROLL=3 */
        0xa9, 0x08,       /* LDA #$08 */
        0x8d, 0x16, 0xd0, /* STA $D016: CSEL=1, XSCROLL=0 */
        0xa9, 0x15,       /* LDA #$15 */
        0x8d, 0x18, 0xd0, /* STA $D018 */
        0xa9, 0x34,       /* LDA #$34 */
        0x8d, 0x04, 0xdc, /* STA $DC04 */
        0xea              /* NOP */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_machine_snapshot snapshot;
    c64_frame frame;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_instructions(&machine, 15);
    c64_copy_machine_snapshot(&machine, &snapshot);

    expect_u64_gt("screen write checkpoint", snapshot.screen_ram_writes, 0);
    expect_u64_gt("color write checkpoint", snapshot.color_ram_writes, 0);
    expect_u64_gt("vic write checkpoint", snapshot.vic_register_writes, 0);
    expect_u64_gt("cia write checkpoint", snapshot.cia1_register_writes, 0);
    expect_u8("screen memory changed", 0x01, c64_bus_vic_read_screen(&machine.bus, 0));
    expect_u8("color memory changed", 0x05, c64_bus_vic_read_color(&machine.bus, 0));

    expect_true("make frame", c64_make_frame_snapshot(&machine, &frame));
    /* $D011=$1B → DEN=1, RSEL=1 (top=51), YSCROLL=3, so the top visible line
       samples glyph row 0. $D016=$08 → CSEL=1 (left=24). Character 1 glyph row
       0=0x80 has bit 7 set, so the foreground pixel is at x=24. */
    expect_u32("foreground from color ram", TEST_COLOR_GREEN, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
    expect_u32("background from d021", TEST_COLOR_BLUE, frame.pixels[51 * C64_FRAME_WIDTH + 25]);
}

static void test_d018_selects_screen_memory(void) {
    c64_rom_set roms;
    c64_t machine;
    c64_frame frame;

    build_roms(&roms, TEST_RESET_VECTOR);
    reset_machine(&machine, &roms);

    machine.bus.ram[0x0400] = 0x20;
    machine.bus.ram[0x0800] = 0x01;
    machine.bus.color_ram[0] = 0x05;
    c64_bus_write(&machine.bus, 0xd011, 0x1b); /* DEN=1, RSEL=1, YSCROLL=3 */
    c64_bus_write(&machine.bus, 0xd016, 0x08); /* CSEL=1, XSCROLL=0 */
    c64_bus_write(&machine.bus, 0xd021, 0x06);
    c64_bus_write(&machine.bus, 0xd018, 0x24); /* screen=$0800, char=$1000 (ROM) */

    expect_true("make d018 frame", c64_make_frame_snapshot(&machine, &frame));
    /* $D011=$1B → DEN=1, RSEL=1 (top=51), YSCROLL=3; $D016=$08 → CSEL=1 (left=24).
       The top visible line samples glyph row 0, whose bit 7 lands at x=24. */
    expect_u32("d018 screen base foreground", TEST_COLOR_GREEN, frame.pixels[51 * C64_FRAME_WIDTH + 24]);
}

static void test_real_rom_progresses_to_device_checkpoints(void) {
    static const char system_rom_source_path[] = C64M_SOURCE_DIR "/roms/system.rom";
    static const char character_rom_source_path[] = C64M_SOURCE_DIR "/roms/character.rom";
    static const char *const system_rom_paths[] = {
        system_rom_source_path,
        "roms/system.rom",
        "../roms/system.rom",
    };
    static const char *const character_rom_paths[] = {
        character_rom_source_path,
        "roms/character.rom",
        "../roms/character.rom",
    };
    c64_rom_set roms;
    c64_t machine;
    c64_machine_snapshot snapshot;
    char error[256];
    const char *system_rom_path = find_existing_path(
        system_rom_paths,
        sizeof(system_rom_paths) / sizeof(system_rom_paths[0]));
    const char *character_rom_path = find_existing_path(
        character_rom_paths,
        sizeof(character_rom_paths) / sizeof(character_rom_paths[0]));

    c64_rom_set_init(&roms);
    expect_true("load system rom", c64_rom_load_combined_64c(&roms, system_rom_path, error, sizeof(error)));
    expect_true("load character rom", c64_rom_load_character(&roms, character_rom_path, error, sizeof(error)));
    reset_machine(&machine, &roms);

    step_cycles(&machine, 50000);
    c64_copy_machine_snapshot(&machine, &snapshot);

    expect_u64_gt("real rom advanced cycles", snapshot.cycle, 0);
    expect_u64_gt("real rom touched vic", snapshot.vic_register_writes, 0);
    expect_u64_gt("real rom touched cia1", snapshot.cia1_register_writes, 0);
    expect_u64_gt("real rom asserted cia1 interrupts", snapshot.cia1_interrupt_assertions, 0);
}

int main(void) {
    test_irq_entry_uses_machine_vectors_and_stack();
    test_restore_enters_nmi_vector();
    test_rom_driven_screen_and_device_writes_update_frame();
    test_d018_selects_screen_memory();
    test_real_rom_progresses_to_device_checkpoints();
    return 0;
}
