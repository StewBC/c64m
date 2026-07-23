#include "c64.h"
#include "c64_rom.h"
#include "c1541_media.h"

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

static void mount_raw_g64(c64_t *machine, const char *name) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/%s", C64M_SOURCE_DIR, name);
    expect_true("read g64", read_file(path, &bytes, &size) != 0);
    expect_true(
        "mount g64",
        c64_mount_g64(machine, 8, bytes, size, name) == C64_DRIVE_STATUS_OK);
    free(bytes);
}

/*
 * M7 baseline: commercial G64 first-file LOAD through media path.
 * Does NOT claim RUN / secondary custom-loader success (see matrix).
 */
static void test_real_1541_media_g64_star_load_returns(void) {
    c64_t machine;
    uint16_t end;

    install_real_roms_ex(&machine, 1);
    mount_raw_g64(&machine, "robocop[data_east_1987](ntsc)(alt)(!).g64");
    step_cycles(&machine, 2500000u);

    expect_true("g64 media tracks valid", machine.drive8.media.tracks_valid != 0);
    expect_true("from_g64", machine.drive8.media.from_g64 != 0);

    setup_load_call(&machine, "*");
    step_cycles(&machine, 200000000u);

    if (machine.cpu.cpu.pc != (uint16_t)(TEST_RETURN_ADDRESS + 1u)) {
        fprintf(stderr,
            "g64 media LOAD did not return: pc=%04X p=%02X cycle=%llu d8pc=%04X "
            "ht=%d mot=%d/%d from_g64=%d\n",
            machine.cpu.cpu.pc,
            machine.cpu.cpu.flags,
            (unsigned long long)machine.clock.cycle,
            machine.drive8.cpu.cpu.pc,
            machine.drive8.media.half_track,
            machine.drive8.media.motor_on,
            machine.drive8.media.motor_ready,
            machine.drive8.media.from_g64);
        fail("g64 media LOAD did not return");
    }
    if ((machine.cpu.cpu.flags & 0x01u) != 0) {
        fail("g64 media LOAD returned carry set");
    }

    end = (uint16_t)machine.cpu.cpu.X | ((uint16_t)machine.cpu.cpu.Y << 8);
    if (end <= 0x0801u || machine.bus.ram[0x0801u] == 0) {
        fail("g64 media LOAD did not populate BASIC memory");
    }
}

/*
 * DOS 2.6 data-block write ($F575..$F5D6 on this ROM):
 *   gate on → 5×SYNC + 69 + 256 GCR latches (330) → drain BVC → gate off.
 * Expected painted span if 1:1 with latches: 330 GCR bytes.
 * Standard zone-2 sector pitch is 371 bytes; data payload starts ~24 bytes
 * after the header, leaving ~17 bytes of gap before the next header.
 *
 * "SAVE returned OK" is intentionally NOT a pass criterion.
 */
enum {
    DOS_WRITE_LATCH_BYTES = 330,
    T18_SECTOR_PITCH = 371,
    T18_HEADER_COUNT = 19
};

static int count_gcr_headers(const uint8_t *tr, size_t len) {
    size_t i;
    int n = 0;
    if (tr == NULL || len < 6u) {
        return 0;
    }
    for (i = 0; i + 6u <= len; ++i) {
        if (tr[i] == 0xFFu && tr[i + 1u] == 0xFFu && tr[i + 2u] == 0xFFu &&
            tr[i + 3u] == 0xFFu && tr[i + 4u] == 0xFFu && tr[i + 5u] == 0x52u) {
            n++;
        }
    }
    return n;
}

/*
 * Regression: physical G64 DOS write must not overrun the data-block footprint
 * into the next sector header. Captures the write-gate / bit-clock stretch bug
 * where head motion while write_bits_left==0 smears a 330-byte ROM write across
 * ~400 track bytes and smashes the following FFFFFF…52 mark.
 *
 * Fixture: standard blank DOS G64 (35 tracks, zone-2 track 18, 19 headers at
 * 371-byte pitch). Placed under assets/disks/ like other machine-test images.
 */
static void test_g64_dos_write_footprint_next_header_intact(void) {
    c64_t machine;
    uint8_t *g64 = NULL;
    size_t g64_size = 0;
    uint8_t expected[] = {0x01, 0x08, 0x11, 0x22, 0x33, 0x44};
    size_t i;
    c1541_track *t18;
    uint8_t *before = NULL;
    size_t before_len;
    int headers_before;
    int headers_after;
    size_t first_chg = (size_t)-1;
    size_t last_chg = 0;
    size_t changed = 0;
    int h;
    char path[512];

    install_real_roms_ex(&machine, 1);

    snprintf(path, sizeof(path), "%s/assets/disks/blank_dos.g64", C64M_SOURCE_DIR);
    expect_true(
        "read assets/disks/blank_dos.g64 (standard blank DOS G64 fixture)",
        read_file(path, &g64, &g64_size) != 0);
    expect_true(
        "mount g64 fixture",
        c64_mount_g64(&machine, 8, g64, g64_size, "blank_dos.g64") == C64_DRIVE_STATUS_OK);
    free(g64);
    expect_true("writable", c64_set_drive_writable(&machine, 8, true));

    step_cycles(&machine, 2500000u);
    expect_true("media tracks", machine.drive8.media.tracks_valid != 0);
    expect_true("from_g64", machine.drive8.media.from_g64 != 0);

    t18 = &machine.drive8.media.halves[34];
    expect_true("t18 present", t18->data != NULL && t18->length > 1000u);
    before_len = t18->length;
    before = (uint8_t *)malloc(before_len);
    expect_true("oom before", before != NULL);
    memcpy(before, t18->data, before_len);
    headers_before = count_gcr_headers(before, before_len);
    expect_true("t18 has 19 headers before write", headers_before == T18_HEADER_COUNT);

    for (i = 2; i < sizeof(expected); ++i) {
        machine.bus.ram[0x0801u + (uint16_t)(i - 2u)] = expected[i];
    }
    setup_save_call(&machine, "SAVED", 0x0801u, 0x0805u);
    step_cycles(&machine, 250000000u);

    /* Must not treat KERNAL success alone as pass — inspect the ring. */
    t18 = &machine.drive8.media.halves[34];
    expect_true("t18 still present", t18->data != NULL && t18->length == before_len);
    headers_after = count_gcr_headers(t18->data, t18->length);

    for (i = 0; i < before_len; ++i) {
        if (t18->data[i] != before[i]) {
            if (first_chg == (size_t)-1) {
                first_chg = i;
            }
            last_chg = i;
            changed++;
        }
    }

    /* Every pre-existing header mark must remain (no smash of next sector). */
    if (headers_after != headers_before) {
        fprintf(stderr,
            "g64 write footprint: header count %d -> %d (SAVE pc=%04X C=%d dirty=%d "
            "changed=%zu first=%zu last=%zu span=%zu)\n",
            headers_before,
            headers_after,
            machine.cpu.cpu.pc,
            machine.cpu.cpu.flags & 1,
            machine.drives[0].dirty ? 1 : 0,
            changed,
            first_chg,
            last_chg,
            (first_chg != (size_t)-1) ? (last_chg - first_chg + 1u) : 0u);
        free(before);
        fail("g64 DOS write smashed at least one GCR header on track 18");
    }
    for (h = 0; h < T18_HEADER_COUNT; ++h) {
        size_t off = (size_t)h * (size_t)T18_SECTOR_PITCH;
        if (off + 6u > before_len) {
            break;
        }
        if (memcmp(t18->data + off, before + off, 6) != 0) {
            fprintf(stderr, "header at byte %zu corrupted\n", off);
            free(before);
            fail("g64 DOS write corrupted a sector header footprint");
        }
    }

    /*
     * Any single write's dirty span must fit the ROM data-block budget.
     * SAVE issues several writes; require each changed run is not absurdly
     * larger than 330 bytes — if the whole track dirties past 330*writes,
     * something stretched. Primary gate is header integrity above.
     */
    if (changed > 0 && first_chg != (size_t)-1) {
        size_t span = last_chg - first_chg + 1u;
        /* Upper bound: a few DOS writes (BAM+dir+data) × 330 with gap slack. */
        if (span > (size_t)DOS_WRITE_LATCH_BYTES * 8u) {
            fprintf(stderr,
                "g64 write dirty span %zu exceeds %d (changed=%zu first=%zu last=%zu)\n",
                span,
                DOS_WRITE_LATCH_BYTES * 8,
                changed,
                first_chg,
                last_chg);
            free(before);
            fail("g64 DOS write dirty span unreasonably large");
        }
    }

    free(before);
    printf("PASS: test_g64_dos_write_footprint_next_header_intact\n");
}

int main(void) {
    test_real_1541_star_load_returns();
    test_real_1541_media_star_load_returns();
    test_real_1541_media_save_small_prg();
    test_real_1541_media_g64_star_load_returns();
    test_g64_dos_write_footprint_next_header_intact();
    return 0;
}
