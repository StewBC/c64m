#include "c64.h"
#include "c64_bus.h"
#include "c64_rom.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void fill_cartridge_rom(uint8_t *rom, size_t size, uint8_t base) {
    size_t i;

    for (i = 0; i < size; ++i) {
        rom[i] = (uint8_t)(base + (i & 0x0fu));
    }
}

static void test_generic_8k_cartridge_mapping(void) {
    c64_t machine;
    uint8_t roml[C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];

    c64_init(&machine);
    fill_roms(&machine.bus);
    fill_cartridge_rom(roml, sizeof(roml), 0x80);

    c64_debug_write_ram(&machine, 0x8000, 0x12);
    c64_debug_write_ram(&machine, 0xa000, 0x34);

    expect_true(
        "attach 8k cartridge",
        c64_attach_generic_cartridge(
            &machine, roml, sizeof(roml), NULL, 0, 0, 1, error, sizeof(error)));
    expect_u8("8k roml visible", 0x80, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("8k roml debug visible", 0x80, c64_debug_read_cpu_map(&machine, 0x8000));
    expect_u8("8k no romh", 0xa0, c64_bus_read(&machine.bus, 0xa000));

    c64_bus_write(&machine.bus, 0x8000, 0x56);
    expect_u8("8k roml unchanged after write", 0x80, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("8k ram underneath written", 0x56, c64_debug_read_ram(&machine, 0x8000));

    c64_detach_cartridge(&machine);
    expect_u8("8k detach restores ram", 0x56, c64_bus_read(&machine.bus, 0x8000));
}

static void test_generic_16k_cartridge_mapping(void) {
    c64_t machine;
    uint8_t roml[C64_CARTRIDGE_ROM_BANK_SIZE];
    uint8_t romh[C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];

    c64_init(&machine);
    fill_roms(&machine.bus);
    fill_cartridge_rom(roml, sizeof(roml), 0x80);
    fill_cartridge_rom(romh, sizeof(romh), 0xa8);

    c64_debug_write_ram(&machine, 0xa000, 0x44);

    expect_true(
        "attach 16k cartridge",
        c64_attach_generic_cartridge(
            &machine, roml, sizeof(roml), romh, sizeof(romh), 0, 0, error, sizeof(error)));
    expect_u8("16k roml visible", 0x80, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("16k romh visible", 0xa8, c64_bus_read(&machine.bus, 0xa000));
    expect_u8("16k romh debug visible", 0xa8, c64_debug_read_cpu_map(&machine, 0xa000));

    c64_bus_write(&machine.bus, 0xa000, 0x66);
    expect_u8("16k romh unchanged after write", 0xa8, c64_bus_read(&machine.bus, 0xa000));
    expect_u8("16k ram underneath written", 0x66, c64_debug_read_ram(&machine, 0xa000));
    expect_true(
        "16k memory visibility rom",
        c64_memory_visibility_at(&machine, 0xa000) == C64_MEMORY_VISIBILITY_ROM);
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

static void test_cartridge_survives_reset(void) {
    c64_t machine;
    c64_rom_set roms;
    uint8_t roml[C64_CARTRIDGE_ROM_BANK_SIZE];
    uint8_t romh[C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    c64_init(&machine);
    c64_rom_set_init(&roms);

    for (i = 0; i < sizeof(roms.basic); i++) {
        roms.basic[i] = (uint8_t)(0xa0 + (i & 0x0f));
    }
    for (i = 0; i < sizeof(roms.character); i++) {
        roms.character[i] = (uint8_t)(0xd0 + (i & 0x0f));
    }
    for (i = 0; i < sizeof(roms.kernal); i++) {
        roms.kernal[i] = (uint8_t)(0xe0 + (i & 0x0f));
    }
    roms.kernal[0x1ffc] = 0x34;
    roms.kernal[0x1ffd] = 0xe1;
    roms.has_basic = true;
    roms.has_character = true;
    roms.has_kernal = true;

    fill_cartridge_rom(roml, sizeof(roml), 0x80);
    fill_cartridge_rom(romh, sizeof(romh), 0xa8);

    expect_true("install reset cartridge roms", c64_install_roms(&machine, &roms, error, sizeof(error)));
    expect_true(
        "attach reset cartridge",
        c64_attach_generic_cartridge(
            &machine, roml, sizeof(roml), romh, sizeof(romh), 0, 0, error, sizeof(error)));
    expect_true("reset with cartridge", c64_reset(&machine, error, sizeof(error)));
    expect_u8("reset keeps roml", 0x80, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("reset keeps romh", 0xa8, c64_bus_read(&machine.bus, 0xa000));
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

static void test_debugcart_d7ff(void) {
    c64_bus_t bus;

    c64_bus_init(&bus);
    /* CPU port default keeps I/O visible so $D7xx is open I/O. */
    bus.cpu_port_data = 0x37;
    expect_true("debugcart off by default", !bus.debugcart_hit);

    c64_bus_write(&bus, 0xd7ff, 0x00);
    expect_true("no hit when disabled", !bus.debugcart_hit);

    bus.debugcart_enabled = true;
    c64_bus_write(&bus, 0xd7ff, 0x00);
    expect_true("hit on pass write", bus.debugcart_hit);
    expect_u8("pass value", 0x00, bus.debugcart_value);

    bus.debugcart_hit = false;
    c64_bus_write(&bus, 0xd7ff, 0xff);
    expect_true("hit on fail write", bus.debugcart_hit);
    expect_u8("fail value", 0xff, bus.debugcart_value);
}

static void test_magic_desk_banking(void) {
    c64_t machine;
    uint8_t banks[4 * C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    c64_init(&machine);
    fill_roms(&machine.bus);

    /* Four banks with distinct first bytes; last bank index 3 → mask 0x03. */
    memset(banks, 0, sizeof(banks));
    for (i = 0; i < 4; ++i) {
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x10u + (uint8_t)i);
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE + 1] = (uint8_t)(0xa0u + (uint8_t)i);
    }

    expect_true(
        "attach magic desk",
        c64_attach_magic_desk_cartridge(
            &machine, banks, 4, 0, 1, error, sizeof(error)));
    expect_u8("bank0 at power-on", 0x10, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("bank0 second", 0xa0, c64_bus_read(&machine.bus, 0x8001));

    /* IO1 write selects bank (IO mapped with default CPU port). */
    c64_bus_write(&machine.bus, 0xde00, 0x02);
    expect_u8("bank2 after DE00", 0x12, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("bank2 second", 0xa2, c64_bus_read(&machine.bus, 0x8001));

    /* Bit 7 disables cart ROM → RAM at $8000. */
    c64_debug_write_ram(&machine, 0x8000, 0x55);
    c64_bus_write(&machine.bus, 0xde00, 0x80);
    expect_u8("disabled shows ram", 0x55, c64_bus_read(&machine.bus, 0x8000));

    /* Re-enable bank 1. */
    c64_bus_write(&machine.bus, 0xde00, 0x01);
    expect_u8("bank1 after re-enable", 0x11, c64_bus_read(&machine.bus, 0x8000));

    /* Bank mask: write bank 5 with mask 0x03 → bank 1. */
    c64_bus_write(&machine.bus, 0xde00, 0x05);
    expect_u8("masked bank 5->1", 0x11, c64_bus_read(&machine.bus, 0x8000));

    /* Reset restores bank 0 / enabled. */
    {
        c64_rom_set roms;
        size_t j;
        c64_rom_set_init(&roms);
        for (j = 0; j < sizeof(roms.basic); j++) {
            roms.basic[j] = 0xea;
        }
        for (j = 0; j < sizeof(roms.character); j++) {
            roms.character[j] = 0x00;
        }
        for (j = 0; j < sizeof(roms.kernal); j++) {
            roms.kernal[j] = 0xea;
        }
        roms.kernal[0x1ffc] = 0x00;
        roms.kernal[0x1ffd] = 0xe0;
        roms.has_basic = true;
        roms.has_character = true;
        roms.has_kernal = true;
        expect_true("roms", c64_install_roms(&machine, &roms, error, sizeof(error)));
        c64_bus_write(&machine.bus, 0xde00, 0x03);
        expect_u8("bank3 before reset", 0x13, c64_bus_read(&machine.bus, 0x8000));
        expect_true("reset", c64_reset(&machine, error, sizeof(error)));
    }
    expect_u8("reset bank0", 0x10, c64_bus_read(&machine.bus, 0x8000));

    c64_detach_cartridge(&machine);
}

static void test_ocean_banking(void) {
    c64_t machine;
    uint8_t banks[16 * C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    c64_init(&machine);
    fill_roms(&machine.bus);

    /* 16 banks (128 KiB): VICE uses 16K game, same bank at ROML and ROMH. */
    memset(banks, 0, sizeof(banks));
    for (i = 0; i < 16; ++i) {
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x40u + (uint8_t)i);
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE + 1] = (uint8_t)(0xc0u + (uint8_t)i);
    }

    /* GAME=0 => 16K mirror (Chase HQ II class). */
    expect_true(
        "attach ocean 16k",
        c64_attach_ocean_cartridge(&machine, banks, 16, 0, 0, error, sizeof(error)));
    expect_u8("ocean bank0 roml", 0x40, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("ocean bank0 romh mirror", 0x40, c64_bus_read(&machine.bus, 0xa000));

    c64_bus_write(&machine.bus, 0xde00, 0x05);
    expect_u8("ocean bank5 roml", 0x45, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("ocean bank5 romh", 0x45, c64_bus_read(&machine.bus, 0xa000));
    expect_u8("ocean bank5 second", 0xc5, c64_bus_read(&machine.bus, 0x8001));

    /* Bit 7 ignored (unlike Magic Desk). */
    c64_bus_write(&machine.bus, 0xde00, (uint8_t)(0x80u | 0x03u));
    expect_u8("ocean bit7 ignored bank3", 0x43, c64_bus_read(&machine.bus, 0x8000));

    /* Mask 0x0f for 16 banks: write 0x15 -> bank 5. */
    c64_bus_write(&machine.bus, 0xde00, 0x15);
    expect_u8("ocean masked bank", 0x45, c64_bus_read(&machine.bus, 0x8000));

    /* LORAM clear => $8000 is underlay RAM, not cart. */
    c64_debug_write_ram(&machine, 0x8000, 0xee);
    c64_bus_write(&machine.bus, 0x0001, 0x20); /* LORAM off */
    expect_u8("ocean loram off shows ram", 0xee, c64_bus_read(&machine.bus, 0x8000));
    c64_bus_write(&machine.bus, 0x0001, 0x37);
    expect_u8("ocean loram+hiram shows cart", 0x45, c64_bus_read(&machine.bus, 0x8000));

    /* $01=$25: LORAM on, HIRAM off => underlay at $8000 and $A000 (Pang IRQ path). */
    c64_debug_write_ram(&machine, 0x8000, 0x11);
    c64_debug_write_ram(&machine, 0xa000, 0x22);
    c64_bus_write(&machine.bus, 0x0001, 0x25);
    expect_u8("ocean $25 roml underlay", 0x11, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("ocean $25 romh underlay", 0x22, c64_bus_read(&machine.bus, 0xa000));
    c64_bus_write(&machine.bus, 0x0001, 0x37);
    expect_u8("ocean $37 roml cart", 0x45, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("ocean $37 romh cart", 0x45, c64_bus_read(&machine.bus, 0xa000));

    c64_bus_write(&machine.bus, 0xde00, 0x0a);
    {
        c64_rom_set roms;
        size_t j;
        c64_rom_set_init(&roms);
        for (j = 0; j < sizeof(roms.basic); j++) {
            roms.basic[j] = 0xea;
        }
        for (j = 0; j < sizeof(roms.character); j++) {
            roms.character[j] = 0x00;
        }
        for (j = 0; j < sizeof(roms.kernal); j++) {
            roms.kernal[j] = 0xea;
        }
        roms.kernal[0x1ffc] = 0x00;
        roms.kernal[0x1ffd] = 0xe0;
        roms.has_basic = true;
        roms.has_character = true;
        roms.has_kernal = true;
        expect_true("ocean roms", c64_install_roms(&machine, &roms, error, sizeof(error)));
        expect_true("ocean reset", c64_reset(&machine, error, sizeof(error)));
    }
    expect_u8("ocean reset bank0", 0x40, c64_bus_read(&machine.bus, 0x8000));

    /* CRT GAME is ignored: 128K stays 16K even when header GAME=1 (Pang). */
    expect_true(
        "attach ocean game1 still 16k",
        c64_attach_ocean_cartridge(&machine, banks, 16, 0, 1, error, sizeof(error)));
    expect_u8("ocean game1 roml", 0x40, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("ocean game1 romh mirror", 0x40, c64_bus_read(&machine.bus, 0xa000));

    /* 512K (64 banks): 8K game, no ROMH. */
    {
        uint8_t *banks64 = (uint8_t *)calloc(64u, C64_CARTRIDGE_ROM_BANK_SIZE);
        expect_true("ocean 512k alloc", banks64 != NULL);
        banks64[0] = 0x70;
        expect_true(
            "attach ocean 512k",
            c64_attach_ocean_cartridge(&machine, banks64, 64, 0, 0, error, sizeof(error)));
        free(banks64);
        expect_u8("ocean 512k roml", 0x70, c64_bus_read(&machine.bus, 0x8000));
        /* 8K: $A000 is BASIC ($EA from install above). */
        expect_u8("ocean 512k no romh", 0xea, c64_bus_read(&machine.bus, 0xa000));
    }

    c64_detach_cartridge(&machine);
}

static void test_c64gs_banking(void) {
    c64_t machine;
    uint8_t banks[8 * C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    c64_init(&machine);
    fill_roms(&machine.bus);

    memset(banks, 0, sizeof(banks));
    for (i = 0; i < 8; ++i) {
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x20u + (uint8_t)i);
    }

    expect_true(
        "attach c64gs",
        c64_attach_c64gs_cartridge(&machine, banks, 8, 0, 1, error, sizeof(error)));
    expect_u8("c64gs bank0 power-on", 0x20, c64_bus_read(&machine.bus, 0x8000));

    /* Bank comes from the ADDRESS (value ignored) on write. */
    c64_bus_write(&machine.bus, 0xde05, 0xff);
    expect_u8("c64gs write $de05 -> bank5", 0x25, c64_bus_read(&machine.bus, 0x8000));

    /* A READ of IO1 also latches the bank (VICE gs.c). */
    (void)c64_bus_read(&machine.bus, 0xde03);
    expect_u8("c64gs read $de03 -> bank3", 0x23, c64_bus_read(&machine.bus, 0x8000));
    (void)c64_bus_read(&machine.bus, 0xde00);
    expect_u8("c64gs read $de00 -> bank0", 0x20, c64_bus_read(&machine.bus, 0x8000));

    /* No disable line: $A000 stays BASIC ($EA/$A? from fill_roms), $8000 always cart. */
    c64_bus_write(&machine.bus, 0xde07, 0x00);
    expect_u8("c64gs bank7", 0x27, c64_bus_read(&machine.bus, 0x8000));

    c64_bus_cartridge_reset(&machine.bus);
    expect_u8("c64gs reset -> bank0", 0x20, c64_bus_read(&machine.bus, 0x8000));

    c64_detach_cartridge(&machine);
}

static void test_dinamic_banking(void) {
    c64_t machine;
    uint8_t banks[16 * C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    c64_init(&machine);
    fill_roms(&machine.bus);

    memset(banks, 0, sizeof(banks));
    for (i = 0; i < 16; ++i) {
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x30u + (uint8_t)i);
    }

    expect_true(
        "attach dinamic",
        c64_attach_dinamic_cartridge(&machine, banks, 16, 0, 1, error, sizeof(error)));
    expect_u8("dinamic bank0 power-on", 0x30, c64_bus_read(&machine.bus, 0x8000));

    /* Bank switches on READ of $de00-$de0f. */
    (void)c64_bus_read(&machine.bus, 0xde07);
    expect_u8("dinamic read $de07 -> bank7", 0x37, c64_bus_read(&machine.bus, 0x8000));

    /* Reads above $de0f do nothing (unknown mirrors). */
    (void)c64_bus_read(&machine.bus, 0xde10);
    expect_u8("dinamic read $de10 no change", 0x37, c64_bus_read(&machine.bus, 0x8000));

    /* Writes are ignored (bank switches on reads only). */
    c64_bus_write(&machine.bus, 0xde02, 0x02);
    expect_u8("dinamic write ignored", 0x37, c64_bus_read(&machine.bus, 0x8000));

    (void)c64_bus_read(&machine.bus, 0xde00);
    expect_u8("dinamic read $de00 -> bank0", 0x30, c64_bus_read(&machine.bus, 0x8000));

    c64_bus_cartridge_reset(&machine.bus);
    expect_u8("dinamic reset -> bank0", 0x30, c64_bus_read(&machine.bus, 0x8000));

    c64_detach_cartridge(&machine);
}

static void test_funplay_banking(void) {
    c64_t machine;
    uint8_t banks[16 * C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t i;

    c64_init(&machine);
    fill_roms(&machine.bus);

    memset(banks, 0, sizeof(banks));
    for (i = 0; i < 16; ++i) {
        banks[i * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x40u + (uint8_t)i);
    }

    /* Banks passed linearly (runtime de-scrambles CRT chip.bank before attach). */
    expect_true(
        "attach funplay",
        c64_attach_funplay_cartridge(&machine, banks, 16, 0, 0, error, sizeof(error)));
    expect_u8("funplay bank0 power-on", 0x40, c64_bus_read(&machine.bus, 0x8000));

    /* Register value is scrambled: $08 -> bank1, $10 -> bank2, $01 -> bank8. */
    c64_bus_write(&machine.bus, 0xde00, 0x08);
    expect_u8("funplay $08 -> bank1", 0x41, c64_bus_read(&machine.bus, 0x8000));
    c64_bus_write(&machine.bus, 0xde00, 0x10);
    expect_u8("funplay $10 -> bank2", 0x42, c64_bus_read(&machine.bus, 0x8000));
    c64_bus_write(&machine.bus, 0xde00, 0x01);
    expect_u8("funplay $01 -> bank8", 0x48, c64_bus_read(&machine.bus, 0x8000));

    /* Value $86 turns ROM off -> RAM at $8000. */
    c64_debug_write_ram(&machine, 0x8000, 0x55);
    c64_bus_write(&machine.bus, 0xde00, 0x86);
    expect_u8("funplay $86 ROM off shows ram", 0x55, c64_bus_read(&machine.bus, 0x8000));

    /* $00 re-enables 8K game at bank0. */
    c64_bus_write(&machine.bus, 0xde00, 0x00);
    expect_u8("funplay $00 -> bank0", 0x40, c64_bus_read(&machine.bus, 0x8000));

    c64_bus_cartridge_reset(&machine.bus);
    expect_u8("funplay reset -> bank0", 0x40, c64_bus_read(&machine.bus, 0x8000));

    c64_detach_cartridge(&machine);
}

static void test_super_games_banking(void) {
    c64_t machine;
    uint8_t slots[8 * C64_CARTRIDGE_ROM_BANK_SIZE];
    char error[256];
    size_t b;

    c64_init(&machine);
    fill_roms(&machine.bus);

    /* 4 banks × [ROML,ROMH] interleaved 8K slots. */
    memset(slots, 0, sizeof(slots));
    for (b = 0; b < 4; ++b) {
        slots[(b * 2u) * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x50u + (uint8_t)b);
        slots[(b * 2u + 1u) * C64_CARTRIDGE_ROM_BANK_SIZE] = (uint8_t)(0x60u + (uint8_t)b);
    }

    expect_true(
        "attach super games",
        c64_attach_super_games_cartridge(&machine, slots, 8, 0, 0, error, sizeof(error)));
    /* Power-on: bank0, 16K enabled (ROML at $8000, ROMH at $A000). */
    expect_u8("sg bank0 roml", 0x50, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("sg bank0 romh", 0x60, c64_bus_read(&machine.bus, 0xa000));

    /* Bank = value & 3, mode bit2=0 keeps 16K enabled. */
    c64_bus_write(&machine.bus, 0xdf00, 0x01);
    expect_u8("sg bank1 roml", 0x51, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("sg bank1 romh", 0x61, c64_bus_read(&machine.bus, 0xa000));

    /* Bit2 set disables the cart -> RAM at $8000. */
    c64_debug_write_ram(&machine, 0x8000, 0x55);
    c64_bus_write(&machine.bus, 0xdf00, 0x04);
    expect_u8("sg disabled shows ram", 0x55, c64_bus_read(&machine.bus, 0x8000));

    /* Re-enable bank2. */
    c64_bus_write(&machine.bus, 0xdf00, 0x02);
    expect_u8("sg bank2 roml", 0x52, c64_bus_read(&machine.bus, 0x8000));
    expect_u8("sg bank2 romh", 0x62, c64_bus_read(&machine.bus, 0xa000));

    /* Bit3 write-protect latch: select bank3 + latch, then further writes ignored. */
    c64_bus_write(&machine.bus, 0xdf00, 0x0b); /* bank3, enabled, latched */
    expect_u8("sg bank3 latched", 0x53, c64_bus_read(&machine.bus, 0x8000));
    c64_bus_write(&machine.bus, 0xdf00, 0x00); /* would be bank0, but latched */
    expect_u8("sg latch blocks write", 0x53, c64_bus_read(&machine.bus, 0x8000));

    /* Reset clears the latch and returns to bank0; writes work again. */
    c64_bus_cartridge_reset(&machine.bus);
    expect_u8("sg reset -> bank0", 0x50, c64_bus_read(&machine.bus, 0x8000));
    c64_bus_write(&machine.bus, 0xdf00, 0x01);
    expect_u8("sg post-reset write works", 0x51, c64_bus_read(&machine.bus, 0x8000));

    c64_detach_cartridge(&machine);
}

int main(void) {
    test_ram_roundtrip();
    test_rom_visibility();
    test_banking();
    test_generic_8k_cartridge_mapping();
    test_generic_16k_cartridge_mapping();
    test_vicii_io_mirroring_and_banking();
    test_color_ram_nibble_storage();
    test_debug_cpu_map_reads_sid();
    test_reset_vector();
    test_cartridge_survives_reset();
    test_combined_system_rom();
    test_debugcart_d7ff();
    test_magic_desk_banking();
    test_ocean_banking();
    test_c64gs_banking();
    test_dinamic_banking();
    test_funplay_banking();
    test_super_games_banking();
    return 0;
}
