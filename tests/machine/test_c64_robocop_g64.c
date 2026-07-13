/*
 * Robocop (Data East 1987) G64 multi-stage load regression.
 *
 * NTSC C64, true 1541 + GCR media, real KERNAL LOADs (no title-specific drive
 * behaviour). Robocop chains LOAD "FAST" (its dual-bit fast loader, installed at
 * $7000) then LOAD "LOAD1", whose transfer decrypts into $8000.
 *
 * $8000 is asserted against a VICE 3.10 true-drive capture. The first byte read
 * $A8 (bit 3 wrong) instead of $A0 before the 1541's fractional clock and IEC
 * catch-up timing matched VICE; the whole decrypt stub then cascaded.
 */
#include "c64.h"
#include "c64_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum {
    TEST_RETURN_ADDRESS = 0x1233,
    TEST_FILENAME_BUFFER = 0x0200
};

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void expect_true(const char *name, int value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static int read_file(const char *path, uint8_t **out_bytes, size_t *out_size) {
    FILE *file;
    long size;
    uint8_t *bytes;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    *out_bytes = bytes;
    *out_size = (size_t)size;
    return 1;
}

static void install_media_ntsc(c64_t *machine) {
    c64_rom_set roms;
    c64_config config;
    char error[256];

    c64_rom_set_init(&roms);
    expect_true("load system rom", c64_rom_load_combined_64c(
        &roms, C64M_SOURCE_DIR "/roms/system.rom", error, sizeof(error)));
    expect_true("load character rom", c64_rom_load_character(
        &roms, C64M_SOURCE_DIR "/roms/character.rom", error, sizeof(error)));

    c64_init(machine);
    config = machine->config;
    config.emulate_1541 = 1;
    config.media_1541 = 1;
    config.video_standard = C64_VIDEO_STANDARD_NTSC;
    c64_set_config(machine, &config);
    expect_true("load 1541 rom", c1541_load_rom(&machine->drive8, C64M_SOURCE_DIR "/roms/1541.rom") != 0);
    expect_true("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void mount_robocop_g64(c64_t *machine) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/robocop[data_east_1987](ntsc)(alt)(!).g64",
        C64M_SOURCE_DIR);
    expect_true("read g64", read_file(path, &bytes, &size) != 0);
    expect_true("mount g64",
        c64_mount_g64(machine, 8, bytes, size, "robocop.g64") == C64_DRIVE_STATUS_OK);
    free(bytes);
}

static void setup_load(c64_t *machine, const char *name, uint8_t secondary) {
    size_t length = strlen(name);
    size_t i;

    machine->cpu.cpu.pc = 0xffd5u;
    machine->cpu.cpu.sp = 0x01fdu;
    machine->bus.ram[0x01fe] = (uint8_t)(TEST_RETURN_ADDRESS & 0xffu);
    machine->bus.ram[0x01ff] = (uint8_t)(TEST_RETURN_ADDRESS >> 8);
    machine->cpu.cpu.A = 0;
    machine->cpu.cpu.X = 0xffu;
    machine->cpu.cpu.Y = 0xffu;
    machine->cpu.cpu.flags |= 0x01u;

    machine->bus.ram[0xbau] = 8;
    machine->bus.ram[0xb9u] = secondary;
    machine->bus.ram[0xb7u] = (uint8_t)length;
    machine->bus.ram[0xbbu] = (uint8_t)(TEST_FILENAME_BUFFER & 0xffu);
    machine->bus.ram[0xbcu] = (uint8_t)(TEST_FILENAME_BUFFER >> 8);
    for (i = 0; i < length; ++i) {
        machine->bus.ram[TEST_FILENAME_BUFFER + i] = (uint8_t)name[i];
    }
}

static void step_until_ret(c64_t *machine, uint64_t limit, const char *label) {
    char error[256];
    uint64_t i;

    for (i = 0; i < limit; ++i) {
        if (machine->cpu.cpu.pc == (uint16_t)(TEST_RETURN_ADDRESS + 1u)) {
            return;
        }
        expect_true("step", c64_step_cycle(machine, error, sizeof(error)));
    }
    fprintf(stderr, "%s: did not return pc=%04X d8pc=%04X\n",
        label, machine->cpu.cpu.pc, machine->drive8.cpu.cpu.pc);
    fail(label);
}

/*
 * FAST installs the dual-bit fast loader; LOAD1's timed dual-bit transfer must
 * decrypt into a VICE-identical $8000 image.
 */
static void test_robocop_load1_matches_vice(void) {
    c64_t machine;
    char error[256];
    uint64_t t;
    size_t i;

    /* VICE 3.10 true-drive capture of $8000..$803F after LOAD1. */
    static const uint8_t vice_8000[64] = {
        0xA0, 0x00, 0xA9, 0x1A, 0x59, 0x0C, 0x80, 0x99, 0x0C, 0x80, 0xC8, 0xD0, 0xED, 0x57, 0x19, 0x00,
        0x89, 0xB0, 0x19, 0x66, 0xF5, 0x82, 0x58, 0x27, 0x52, 0xA4, 0x49, 0x40, 0xA1, 0x8F, 0xA3, 0x84,
        0xA1, 0x8F, 0x0F, 0x16, 0x3D, 0xAB, 0xA7, 0x27, 0x16, 0x3D, 0xAB, 0xA0, 0xA2, 0x22, 0xCC, 0x4C,
        0x0B, 0x8F, 0x3F, 0x3D, 0x3A, 0x1C, 0xA0, 0x20, 0x16, 0x3D, 0xA2, 0xA1, 0x28, 0x94, 0x4B, 0x56
    };

    install_media_ntsc(&machine);
    mount_robocop_g64(&machine);

    for (t = 0; t < 2500000ull; ++t) {
        expect_true("boot", c64_step_cycle(&machine, error, sizeof(error)));
    }
    expect_true("tracks valid", machine.drive8.media.tracks_valid != 0);
    expect_true("from_g64", machine.drive8.media.from_g64 != 0);

    setup_load(&machine, "FAST", 1);
    step_until_ret(&machine, 80000000ull, "LOAD FAST");

    machine.cpu.cpu.pc = 0x7000u;
    machine.cpu.cpu.sp = 0x01fdu;
    machine.bus.ram[0x01fe] = (uint8_t)(TEST_RETURN_ADDRESS & 0xffu);
    machine.bus.ram[0x01ff] = (uint8_t)(TEST_RETURN_ADDRESS >> 8);
    step_until_ret(&machine, 5000000ull, "JSR $7000");

    setup_load(&machine, "LOAD1", 1);
    step_until_ret(&machine, 500000000ull, "LOAD LOAD1");

    for (i = 0; i < sizeof(vice_8000); ++i) {
        if (machine.bus.ram[0x8000 + i] != vice_8000[i]) {
            fprintf(stderr, "LOAD1 $8000 mismatch at $%04X\ngot     :", (unsigned)(0x8000 + i));
            for (i = 0; i < sizeof(vice_8000); ++i) {
                fprintf(stderr, " %02X", machine.bus.ram[0x8000 + i]);
            }
            fprintf(stderr, "\nexpected:");
            for (i = 0; i < sizeof(vice_8000); ++i) {
                fprintf(stderr, " %02X", vice_8000[i]);
            }
            fprintf(stderr, "\n");
            fail("LOAD1 $8000 not VICE-identical");
        }
    }

    printf("PASS: robocop g64 LOAD1 $8000 matches VICE\n");
}

int main(void) {
    test_robocop_load1_matches_vice();
    return 0;
}
