#include "c64.h"
#include "c64_bus.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void fill_roms(c64_bus_t *bus) {
    uint8_t basic[C64_BASIC_ROM_SIZE];
    uint8_t chars[C64_CHAR_ROM_SIZE];
    uint8_t kernal[C64_KERNAL_ROM_SIZE];
    size_t i;

    for (i = 0; i < sizeof(basic); i++) {
        basic[i] = (uint8_t)(0xa0 + (i & 0x0f));
    }

    for (i = 0; i < sizeof(chars); i++) {
        chars[i] = (uint8_t)(0xd0 + (i & 0x0f));
    }

    for (i = 0; i < sizeof(kernal); i++) {
        kernal[i] = (uint8_t)(0xe0 + (i & 0x0f));
    }

    kernal[0x1ffc] = 0x34;
    kernal[0x1ffd] = 0xe1;

    expect_true("basic rom load", c64_bus_set_basic_rom(bus, basic, sizeof(basic)));
    expect_true("char rom load", c64_bus_set_char_rom(bus, chars, sizeof(chars)));
    expect_true("kernal rom load", c64_bus_set_kernal_rom(bus, kernal, sizeof(kernal)));
}

static void test_ram_roundtrip(void) {
    c64_bus_t bus;

    c64_bus_init(&bus);
    c64_bus_write(&bus, 0x1234, 0x56);
    expect_u8("ram roundtrip", 0x56, c64_bus_read(&bus, 0x1234));
}

static void test_rom_visibility(void) {
    c64_bus_t bus;

    c64_bus_init(&bus);
    fill_roms(&bus);

    c64_bus_write(&bus, 0xe000, 0x42);
    expect_u8("kernal visible", 0xe0, c64_bus_read(&bus, 0xe000));

    c64_bus_write(&bus, 0x0001, 0x00);
    expect_u8("kernal hidden", 0x42, c64_bus_read(&bus, 0xe000));
}

static void test_banking(void) {
    c64_bus_t bus;

    c64_bus_init(&bus);
    fill_roms(&bus);

    c64_bus_write(&bus, 0xa000, 0x11);
    c64_bus_write(&bus, 0xd000, 0x22);
    c64_bus_write(&bus, 0xe000, 0x33);

    c64_bus_write(&bus, 0x0001, 0x37);
    expect_u8("basic visible", 0xa0, c64_bus_read(&bus, 0xa000));
    expect_u8("io visible", 0xff, c64_bus_read(&bus, 0xd000));
    expect_u8("kernal visible after banking", 0xe0, c64_bus_read(&bus, 0xe000));

    c64_bus_write(&bus, 0x0001, 0x33);
    expect_u8("char visible", 0xd0, c64_bus_read(&bus, 0xd000));

    c64_bus_write(&bus, 0x0001, 0x34);
    expect_u8("basic hidden by loram", 0x11, c64_bus_read(&bus, 0xa000));
    expect_u8("kernal hidden by hiram", 0x33, c64_bus_read(&bus, 0xe000));
}

static void test_vicii_io_mirroring_and_banking(void) {
    c64_t machine;

    c64_init(&machine);

    c64_bus_write(&machine.bus, 0x0001, 0x34);
    c64_bus_write(&machine.bus, 0xd020, 0x42);
    expect_u8("ram under io initialized", 0x42, c64_bus_read(&machine.bus, 0xd020));

    c64_bus_write(&machine.bus, 0x0001, 0x37);
    c64_bus_write(&machine.bus, 0xd020, 0x05);
    expect_u8("vic border visible", 0xF5, c64_bus_read(&machine.bus, 0xd020));
    expect_u8("vic border mirror", 0xF5, c64_bus_read(&machine.bus, 0xd060));
    expect_u8("visible vic write does not touch ram", 0x42,
              c64_bus_vic_read_ram(&machine.bus, 0xd020));

    c64_bus_write(&machine.bus, 0x0001, 0x34);
    c64_bus_write(&machine.bus, 0xd020, 0x09);
    expect_u8("ram under io visible", 0x09, c64_bus_read(&machine.bus, 0xd020));

    c64_bus_write(&machine.bus, 0x0001, 0x37);
    expect_u8("vic register preserved while ram banked", 0xF5, c64_bus_read(&machine.bus, 0xd020));
}

static void test_color_ram_nibble_storage(void) {
    c64_t machine;

    c64_init(&machine);

    c64_bus_write(&machine.bus, 0xd800, 0x1f);
    c64_bus_write(&machine.bus, 0xdbff, 0x26);
    expect_u8("color ram low nibble read", 0x0f, c64_bus_read(&machine.bus, 0xd800));
    expect_u8("color ram high address read", 0x06, c64_bus_read(&machine.bus, 0xdbff));
    expect_u8("vic color low nibble", 0x0f, c64_bus_vic_read_color(&machine.bus, 0));
    expect_u8("vic color high address", 0x06, c64_bus_vic_read_color(&machine.bus, C64_COLOR_RAM_SIZE - 1));
}

static void test_debug_cpu_map_reads_sid(void) {
    c64_t machine;

    c64_init(&machine);

    machine.sid.voice3_osc_read = 0x2a;
    machine.sid.voice3_env_read = 0x5c;

    expect_u8("sid osc live bus read", 0x2a, c64_bus_read(&machine.bus, 0xd41b));
    expect_u8("sid osc debug map read", 0x2a, c64_debug_read_cpu_map(&machine, 0xd41b));
    expect_u8("sid env debug map read", 0x5c, c64_debug_read_cpu_map(&machine, 0xd41c));
}

static void test_reset_vector(void) {
    c64_t machine;
    c64_rom_set roms;
    uint16_t vector;
    char error[256];

    c64_init(&machine);
    c64_rom_set_init(&roms);

    for (size_t i = 0; i < sizeof(roms.basic); i++) {
        roms.basic[i] = (uint8_t)(0xa0 + (i & 0x0f));
    }
    for (size_t i = 0; i < sizeof(roms.character); i++) {
        roms.character[i] = (uint8_t)(0xd0 + (i & 0x0f));
    }
    for (size_t i = 0; i < sizeof(roms.kernal); i++) {
        roms.kernal[i] = (uint8_t)(0xe0 + (i & 0x0f));
    }
    roms.kernal[0x1ffc] = 0x34;
    roms.kernal[0x1ffd] = 0xe1;
    roms.has_basic = true;
    roms.has_character = true;
    roms.has_kernal = true;

    expect_true("install roms", c64_install_roms(&machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(&machine, error, sizeof(error)));

    vector = (uint16_t)c64_bus_read(&machine.bus, 0xfffc) |
        ((uint16_t)c64_bus_read(&machine.bus, 0xfffd) << 8);
    expect_u16("reset vector from bus", 0xe134, vector);

    expect_u16("cpu reset pc", 0xe134, machine.cpu.cpu.pc);
}

static void test_combined_system_rom(void) {
    c64_bus_t bus;
    uint8_t system_rom[C64_BASIC_ROM_SIZE + C64_KERNAL_ROM_SIZE];
    size_t i;

    c64_bus_init(&bus);

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        system_rom[i] = 0xba;
    }
    for (i = C64_BASIC_ROM_SIZE; i < sizeof(system_rom); i++) {
        system_rom[i] = 0xea;
    }

    expect_true(
        "system rom load",
        c64_bus_set_system_rom(&bus, system_rom, sizeof(system_rom)));

    expect_u8("system basic visible", 0xba, c64_bus_read(&bus, 0xa000));
    expect_u8("system kernal visible", 0xea, c64_bus_read(&bus, 0xe000));
}

int main(void) {
    test_ram_roundtrip();
    test_rom_visibility();
    test_banking();
    test_vicii_io_mirroring_and_banking();
    test_color_ram_nibble_storage();
    test_debug_cpu_map_reads_sid();
    test_reset_vector();
    test_combined_system_rom();
    return 0;
}
