#include "c64.h"
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
    TEST_RETURN_ADDRESS = 0x1233,
    TEST_FILENAME_BUFFER = 0x0200
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

static void install_real_roms_ex(c64_t *machine, int media_1541) {
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
    config.media_1541 = media_1541;
    c64_set_config(machine, &config);
    expect_true("load 1541 rom", c1541_load_rom(&machine->drive8, C64M_SOURCE_DIR "/roms/1541.rom") != 0);
    expect_true("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void install_real_roms(c64_t *machine) {
    install_real_roms_ex(machine, 0);
}

static void mount_raw_d64(c64_t *machine, const char *name) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/%s", C64M_SOURCE_DIR, name);
    expect_true("read d64", read_file(path, &bytes, &size) != 0);
    expect_true(
        "mount d64",
        c64_mount_d64(machine, 8, bytes, size, NULL, 0, name, "", "", "", 0) == C64_DRIVE_STATUS_OK);
    free(bytes);
}

static void setup_load_call(c64_t *machine, const char *name) {
    size_t length = strlen(name);
    size_t i;

    machine->cpu.cpu.pc = 0xffd5u;
    machine->cpu.cpu.sp = 0x01fdu;
    machine->bus.ram[0x01fe] = (uint8_t)(TEST_RETURN_ADDRESS & 0xffu);
    machine->bus.ram[0x01ff] = (uint8_t)(TEST_RETURN_ADDRESS >> 8);
    machine->cpu.cpu.A = 0;
    machine->cpu.cpu.X = 0x01u;
    machine->cpu.cpu.Y = 0x08u;
    machine->cpu.cpu.flags |= 0x01u;

    machine->bus.ram[0xbau] = 8;
    machine->bus.ram[0xb9u] = 0;
    machine->bus.ram[0xb7u] = (uint8_t)length;
    machine->bus.ram[0xbbu] = (uint8_t)(TEST_FILENAME_BUFFER & 0xffu);
    machine->bus.ram[0xbcu] = (uint8_t)(TEST_FILENAME_BUFFER >> 8);
    for (i = 0; i < length; ++i) {
        machine->bus.ram[TEST_FILENAME_BUFFER + i] = (uint8_t)name[i];
    }
    machine->bus.ram[0x2bu] = 0x01u;
    machine->bus.ram[0x2cu] = 0x08u;
}

static void step_cycles(c64_t *machine, uint64_t limit) {
    char error[256];
    uint64_t i;

    for (i = 0; i < limit; ++i) {
        if (machine->cpu.cpu.pc == (uint16_t)(TEST_RETURN_ADDRESS + 1u)) {
            return;
        }
        expect_true("step cycle", c64_step_cycle(machine, error, sizeof(error)));
    }
}

static void test_real_1541_star_load_returns(void) {
    c64_t machine;
    uint16_t end;

    install_real_roms(&machine);
    mount_raw_d64(&machine, "GALENCIA.D64");
    step_cycles(&machine, 2500000u);

    setup_load_call(&machine, "*");
    step_cycles(&machine, 60000000u);

    if (machine.cpu.cpu.pc != (uint16_t)(TEST_RETURN_ADDRESS + 1u)) {
        fprintf(stderr,
            "LOAD did not return: pc=%04X a=%02X x=%02X y=%02X p=%02X cycle=%llu "
            "d8pc=%04X d8jobs=%02X,%02X,%02X,%02X,%02X d8talk=%02X d8listen=%02X\n",
            machine.cpu.cpu.pc,
            machine.cpu.cpu.A,
            machine.cpu.cpu.X,
            machine.cpu.cpu.Y,
            machine.cpu.cpu.flags,
            (unsigned long long)machine.clock.cycle,
            machine.drive8.cpu.cpu.pc,
            machine.drive8.ram[0], machine.drive8.ram[1], machine.drive8.ram[2],
            machine.drive8.ram[3], machine.drive8.ram[4],
            machine.drive8.ram[0x7A], machine.drive8.ram[0x79]);
        fail("real 1541 LOAD did not return");
    }
    if ((machine.cpu.cpu.flags & 0x01u) != 0) {
        fail("real 1541 LOAD returned carry set");
    }

    end = (uint16_t)machine.cpu.cpu.X | ((uint16_t)machine.cpu.cpu.Y << 8);
    if (end <= 0x0801u || machine.bus.ram[0x0801u] == 0) {
        fprintf(stderr,
            "LOAD returned but memory check failed: end=%04X eal=%02X%02X "
            "mem0801=%02X %02X %02X %02X %02X %02X %02X %02X p=%02X status=%02X\n",
            end,
            machine.bus.ram[0xafu],
            machine.bus.ram[0xaeu],
            machine.bus.ram[0x0801u],
            machine.bus.ram[0x0802u],
            machine.bus.ram[0x0803u],
            machine.bus.ram[0x0804u],
            machine.bus.ram[0x0805u],
            machine.bus.ram[0x0806u],
            machine.bus.ram[0x0807u],
            machine.bus.ram[0x0808u],
            machine.cpu.cpu.flags,
            machine.bus.ram[0x90u]);
        fail("real 1541 LOAD did not populate BASIC memory");
    }
}

static void setup_save_call(c64_t *machine, const char *name, uint16_t start, uint16_t end) {
    size_t length = strlen(name);
    size_t i;

    machine->cpu.cpu.pc = 0xffd8u;
    machine->cpu.cpu.sp = 0x01fdu;
    machine->bus.ram[0x01fe] = (uint8_t)(TEST_RETURN_ADDRESS & 0xffu);
    machine->bus.ram[0x01ff] = (uint8_t)(TEST_RETURN_ADDRESS >> 8);
    machine->cpu.cpu.A = 0xc1u;
    machine->cpu.cpu.X = (uint8_t)(end & 0xffu);
    machine->cpu.cpu.Y = (uint8_t)(end >> 8);
    machine->cpu.cpu.flags |= 0x01u;

    machine->bus.ram[0xc1u] = (uint8_t)(start & 0xffu);
    machine->bus.ram[0xc2u] = (uint8_t)(start >> 8);
    machine->bus.ram[0xbau] = 8;
    machine->bus.ram[0xb9u] = 0;
    machine->bus.ram[0xb7u] = (uint8_t)length;
    machine->bus.ram[0xbbu] = (uint8_t)(TEST_FILENAME_BUFFER & 0xffu);
    machine->bus.ram[0xbcu] = (uint8_t)(TEST_FILENAME_BUFFER >> 8);
    for (i = 0; i < length; ++i) {
        machine->bus.ram[TEST_FILENAME_BUFFER + i] = (uint8_t)name[i];
    }
}

static void test_real_1541_media_star_load_returns(void) {
    c64_t machine;
    uint16_t end;

    install_real_roms_ex(&machine, 1);
    mount_raw_d64(&machine, "GALENCIA.D64");
    /* Allow media tracks to synthesise and the drive to finish reset. */
    step_cycles(&machine, 2500000u);

    expect_true("media tracks valid after mount spin", machine.drive8.media.tracks_valid != 0);

    setup_load_call(&machine, "*");
    /* Physical GCR reads are slower than job intercept; allow more cycles. */
    step_cycles(&machine, 120000000u);

    if (machine.cpu.cpu.pc != (uint16_t)(TEST_RETURN_ADDRESS + 1u)) {
        fprintf(stderr,
            "media LOAD did not return: pc=%04X a=%02X x=%02X y=%02X p=%02X cycle=%llu "
            "d8pc=%04X d8ht=%d d8mot=%d/%d d8sync=%d d8jobs=%02X,%02X,%02X,%02X,%02X\n",
            machine.cpu.cpu.pc,
            machine.cpu.cpu.A,
            machine.cpu.cpu.X,
            machine.cpu.cpu.Y,
            machine.cpu.cpu.flags,
            (unsigned long long)machine.clock.cycle,
            machine.drive8.cpu.cpu.pc,
            machine.drive8.media.half_track,
            machine.drive8.media.motor_on,
            machine.drive8.media.motor_ready,
            machine.drive8.media.in_sync,
            machine.drive8.ram[0], machine.drive8.ram[1], machine.drive8.ram[2],
            machine.drive8.ram[3], machine.drive8.ram[4]);
        fail("real 1541 media LOAD did not return");
    }
    if ((machine.cpu.cpu.flags & 0x01u) != 0) {
        fail("real 1541 media LOAD returned carry set");
    }

    end = (uint16_t)machine.cpu.cpu.X | ((uint16_t)machine.cpu.cpu.Y << 8);
    if (end <= 0x0801u || machine.bus.ram[0x0801u] == 0) {
        fail("real 1541 media LOAD did not populate BASIC memory");
    }
}

static void test_real_1541_media_save_small_prg(void) {
    c64_t machine;
    uint8_t expected[] = {0x01, 0x08, 0x11, 0x22, 0x33, 0x44};
    size_t i;

    install_real_roms_ex(&machine, 1);
    mount_raw_d64(&machine, "blank.d64");
    expect_true("writable blank", c64_set_drive_writable(&machine, 8, true));
    step_cycles(&machine, 2500000u);
    expect_true("media tracks valid", machine.drive8.media.tracks_valid != 0);

    for (i = 2; i < sizeof(expected); ++i) {
        machine.bus.ram[0x0801u + (uint16_t)(i - 2u)] = expected[i];
    }

    setup_save_call(&machine, "SAVED", 0x0801u, 0x0805u);
    step_cycles(&machine, 180000000u);

    if (machine.cpu.cpu.pc != (uint16_t)(TEST_RETURN_ADDRESS + 1u)) {
        fprintf(stderr,
            "media SAVE did not return: pc=%04X a=%02X p=%02X cycle=%llu "
            "d8pc=%04X d8ht=%d d8mot=%d/%d d8wr=%d d8jobs=%02X,%02X,%02X,%02X,%02X dirty=%d\n",
            machine.cpu.cpu.pc,
            machine.cpu.cpu.A,
            machine.cpu.cpu.flags,
            (unsigned long long)machine.clock.cycle,
            machine.drive8.cpu.cpu.pc,
            machine.drive8.media.half_track,
            machine.drive8.media.motor_on,
            machine.drive8.media.motor_ready,
            machine.drive8.media.writing,
            machine.drive8.ram[0], machine.drive8.ram[1], machine.drive8.ram[2],
            machine.drive8.ram[3], machine.drive8.ram[4],
            machine.drives[0].dirty ? 1 : 0);
        fail("real 1541 media SAVE did not return");
    }
    if ((machine.cpu.cpu.flags & 0x01u) != 0) {
        fail("real 1541 media SAVE returned carry set");
    }
    if (!machine.drives[0].dirty) {
        fail("media SAVE did not mark disk dirty");
    }
}

int main(void) {
    test_real_1541_star_load_returns();
    test_real_1541_media_star_load_returns();
    test_real_1541_media_save_small_prg();
    return 0;
}
