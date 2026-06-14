#include "c64.h"
#include "c64_rom.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static void write_bytes(const char *path, size_t size, uint8_t seed) {
    FILE *file = fopen(path, "wb");
    size_t i;

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    for (i = 0; i < size; i++) {
        fputc((int)(uint8_t)(seed + (i & 0xff)), file);
    }

    fclose(file);
}

static void test_combined_64c_split(void) {
    c64_rom_set roms;
    char error[256];

    write_bytes("test_64c.bin", C64_BASIC_ROM_SIZE + C64_KERNAL_ROM_SIZE, 0x10);
    c64_rom_set_init(&roms);

    expect_true(
        "combined load",
        c64_rom_load_combined_64c(&roms, "test_64c.bin", error, sizeof(error)));
    expect_true("has basic", roms.has_basic);
    expect_true("has kernal", roms.has_kernal);
    expect_u8("basic first byte", 0x10, roms.basic[0]);
    expect_u8("kernal first byte", 0x10, roms.kernal[0]);
    expect_u8("kernal second byte", 0x11, roms.kernal[1]);

    remove("test_64c.bin");
}

static void test_wrong_sizes_fail(void) {
    c64_rom_set roms;
    char error[256];

    write_bytes("test_wrong.bin", 17, 0x20);
    c64_rom_set_init(&roms);

    expect_false(
        "wrong combined size",
        c64_rom_load_combined_64c(&roms, "test_wrong.bin", error, sizeof(error)));
    expect_false(
        "wrong character size",
        c64_rom_load_character(&roms, "test_wrong.bin", error, sizeof(error)));

    remove("test_wrong.bin");
}

static void test_character_load_and_install(void) {
    c64_rom_set roms;
    c64_t machine;
    char error[256];

    c64_rom_set_init(&roms);
    write_bytes("test_basic.bin", C64_BASIC_ROM_SIZE, 0xa0);
    write_bytes("test_kernal.bin", C64_KERNAL_ROM_SIZE, 0xe0);
    write_bytes("test_character.bin", C64_CHAR_ROM_SIZE, 0xd0);

    expect_true("basic load", c64_rom_load_basic(&roms, "test_basic.bin", error, sizeof(error)));
    expect_true("kernal load", c64_rom_load_kernal(&roms, "test_kernal.bin", error, sizeof(error)));
    expect_true("character load", c64_rom_load_character(&roms, "test_character.bin", error, sizeof(error)));

    c64_init(&machine);
    expect_true("install roms", c64_install_roms(&machine, &roms, error, sizeof(error)));

    expect_u8("basic bus byte", 0xa0, c64_bus_read(&machine.bus, 0xa000));
    expect_u8("kernal bus byte", 0xe0, c64_bus_read(&machine.bus, 0xe000));
    c64_bus_write(&machine.bus, 0x0001, 0x33);
    expect_u8("character bus byte", 0xd0, c64_bus_read(&machine.bus, 0xd000));

    remove("test_basic.bin");
    remove("test_kernal.bin");
    remove("test_character.bin");
}

int main(void) {
    test_combined_64c_split();
    test_wrong_sizes_fail();
    test_character_load_and_install();
    return 0;
}
