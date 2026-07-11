/*
 * Robocop (Data East 1987) G64 multi-stage path regression.
 *
 * Shared media/IEC fixes (dual-bit, BA freeze, long post-sync gap align, etc.)
 * must keep this path past the stage-3 gap-table self-check and the former
 * BRK at $8072. Does not claim full playability — only load-to-game progress
 * that previously failed on the protection self-check.
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
    expect_true(
        "load system rom",
        c64_rom_load_combined_64c(
            &roms,
            C64M_SOURCE_DIR "/roms/system.rom",
            error,
            sizeof(error)));
    expect_true(
        "load character rom",
        c64_rom_load_character(
            &roms,
            C64M_SOURCE_DIR "/roms/character.rom",
            error,
            sizeof(error)));

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

    snprintf(
        path,
        sizeof(path),
        "%s/assets/disks/robocop[data_east_1987](ntsc)(alt)(!).g64",
        C64M_SOURCE_DIR);
    expect_true("read g64", read_file(path, &bytes, &size) != 0);
    expect_true(
        "mount g64",
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
        label,
        machine->cpu.cpu.pc,
        machine->drive8.cpu.cpu.pc);
    fail(label);
}

static int gap_table_entries_equal(const c64_t *machine) {
    int y;

    /* Drive code at $0349: compare $04C0..$04F7 with $04C8..$04FF (Y=$37..0). */
    for (y = 0x37; y >= 0; --y) {
        if (machine->drive8.ram[0x4C0 + y] != machine->drive8.ram[0x4C8 + y]) {
            return 0;
        }
    }
    return 1;
}

/*
 * FAST → install dual-bit → LOAD1 → stage-3 gap table must self-check equal,
 * and C64 must not hit BRK in low RAM / loader space (was $8072).
 */
static void test_robocop_stage3_gap_table_and_no_brk(void) {
    c64_t machine;
    char error[256];
    uint64_t t;
    int saw_table = 0;
    int table_ok = 0;
    int survived = 0;
    /* VICE-identical load1 decrypt stub prefix (first 12 bytes at $8000). */
    static const uint8_t load1_prefix[12] = {
        0xA0, 0x00, 0xA9, 0x1A, 0x59, 0x0C, 0x80, 0x99, 0x0C, 0x80, 0xC8, 0xD0
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

    if (memcmp(machine.bus.ram + 0x8000, load1_prefix, sizeof(load1_prefix)) != 0) {
        fprintf(stderr, "LOAD1 $8000 prefix mismatch: ");
        for (t = 0; t < 16; ++t) {
            fprintf(stderr, "%02X ", machine.bus.ram[0x8000 + (size_t)t]);
        }
        fprintf(stderr, "\n");
        fail("LOAD1 $8000 not VICE-identical decrypt stub");
    }

    machine.cpu.cpu.pc = 0x8000u;
    machine.cpu.cpu.sp = 0x01fdu;
    machine.bus.ram[0x01fe] = (uint8_t)(TEST_RETURN_ADDRESS & 0xffu);
    machine.bus.ram[0x01ff] = (uint8_t)(TEST_RETURN_ADDRESS >> 8);
    machine.cpu.cpu.flags |= 0x04u; /* SEI — match game entry path */

    for (t = 0; t < 40000000ull; ++t) {
        uint16_t dpc = machine.drive8.cpu.cpu.pc;
        uint16_t pc;
        uint8_t op;

        if (dpc == 0x0356u && !saw_table) {
            saw_table = 1;
            table_ok = gap_table_entries_equal(&machine);
            if (!table_ok) {
                int i;
                fprintf(stderr, "gap table $04C0:\n");
                for (i = 0; i < 64; ++i) {
                    if ((i % 16) == 0) {
                        fprintf(stderr, "%04X:", 0x4C0 + i);
                    }
                    fprintf(stderr, " %02X", machine.drive8.ram[0x4C0 + i]);
                    if ((i % 16) == 15) {
                        fprintf(stderr, "\n");
                    }
                }
                fail("stage-3 gap table self-check would FAIL");
            }
        }

        if (!machine.cpu.micro_active && !machine.pending_cpu_trace_active) {
            pc = machine.cpu.cpu.pc;
            op = c64_debug_read_cpu_map(&machine, pc);
            if (op == 0x00u && pc >= 0x0200u && pc < 0xA000u) {
                fprintf(stderr,
                    "BRK@$%04X after stage3 (table_seen=%d table_ok=%d t=%llu)\n",
                    pc,
                    saw_table,
                    table_ok,
                    (unsigned long long)t);
                fail("BRK in loader/game RAM (stage-3/protection path)");
            }
            /* After a successful table check, require a few more M cycles without BRK. */
            if (saw_table && table_ok && t > 2000000ull) {
                survived = 1;
                break;
            }
        }

        expect_true("step stage3", c64_step_cycle(&machine, error, sizeof(error)));
    }

    if (!saw_table) {
        fail("never reached drive stage-3 compare at $0356");
    }
    if (!table_ok) {
        fail("gap table not equal");
    }
    if (!survived) {
        fail("did not survive post-table window without BRK");
    }

    printf("PASS: test_robocop_stage3_gap_table_and_no_brk\n");
}

int main(void) {
    test_robocop_stage3_gap_table_and_no_brk();
    return 0;
}
