/*
 * Mid-transfer 1541 snapshot acceptance: Arkanoid V-MAX G64.
 *
 * Run the real custom loader, snapshot while drive-side code is resident and the
 * transfer is in flight, load into a fresh reset machine, continue to the $9400
 * handoff, and require the loaded region to match a never-snapshotted control run.
 */
#include "c64.h"
#include "c64_rom.h"
#include "c64_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

static void die(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void require(const char *name, int value) {
    if (!value) {
        die(name);
    }
}

static int read_file(const char *path, uint8_t **out_bytes, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;

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

static void install_media_pal(c64_t *machine) {
    c64_rom_set roms;
    c64_config config;
    char error[256];

    c64_rom_set_init(&roms);
    require("load system rom", c64_rom_load_combined_64c(
                                   &roms, C64M_SOURCE_DIR "/roms/system.rom", error, sizeof(error)));
    require("load character rom", c64_rom_load_character(
                                      &roms, C64M_SOURCE_DIR "/roms/character.rom", error, sizeof(error)));

    c64_init(machine);
    config = machine->config;
    config.emulate_1541 = 1;
    config.media_1541 = 1;
    config.video_standard = C64_VIDEO_STANDARD_PAL;
    c64_set_config(machine, &config);
    require("load 1541 rom 8", c1541_load_rom(&machine->drive8, C64M_SOURCE_DIR "/roms/1541.rom") != 0);
    require("load 1541 rom 9", c1541_load_rom(&machine->drive9, C64M_SOURCE_DIR "/roms/1541.rom") != 0);
    require("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    require("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void mount_arkanoid(c64_t *machine) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/arkanoid[imagine_1988](pal).g64", C64M_SOURCE_DIR);
    require("read g64", read_file(path, &bytes, &size));
    require("mount g64", c64_mount_g64(machine, 8, bytes, size, "arkanoid.g64") == C64_DRIVE_STATUS_OK);
    free(bytes);
}

static void queue_load_star(c64_t *machine) {
    static const uint8_t command[] = {
        'L', 'O', 'A', 'D', ' ', '"', '*', '"', ',', '8', ',', '1', 0x0d};
    memcpy(machine->bus.ram + 0x0277u, command, sizeof(command));
    machine->bus.ram[0x00c6u] = (uint8_t)sizeof(command);
}

static int at_instruction_boundary(const c64_t *m) {
    return !m->cpu.micro_active && !m->pending_cpu_trace_active;
}

static void step_to_9400(c64_t *machine, uint64_t limit, const char *label) {
    char error[256];
    uint64_t cycle;
    int saw = 0;

    for (cycle = 0; cycle < limit && !saw; ++cycle) {
        require("step", c64_step_cycle(machine, error, sizeof(error)));
        if (at_instruction_boundary(machine) && machine->cpu.cpu.pc == 0x9400u) {
            saw = 1;
        }
    }
    if (!saw) {
        fprintf(stderr, "%s: did not reach $9400 pc=%04X d8pc=%04X\n",
                label,
                machine->cpu.cpu.pc,
                machine->drive8.cpu.cpu.pc);
        die(label);
    }
}

/* VICE-matched regions from probe_c64_arkanoid_g64.c (primary image). */
static const uint8_t vice_9400[8] = {0xA9, 0x35, 0x85, 0x01, 0x80, 0xAD, 0x4C, 0x5A};
static const uint8_t vice_5f50[32] = {
    0x08, 0x20, 0x49, 0xA9, 0x8D, 0x0C, 0xDC, 0xA9, 0xD9, 0x8D, 0x0E, 0xDC, 0xAD, 0x0D, 0xDC, 0x8D,
    0x65, 0xBA, 0x29, 0x01, 0xF0, 0xF6, 0xAD, 0x65, 0xBA, 0x29, 0x08, 0xC9, 0x08, 0xD0, 0xE8, 0x8D};

static void assert_region(const c64_t *m, uint16_t base, const uint8_t *want, size_t n, const char *label) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (m->bus.ram[base + i] != want[i]) {
            fprintf(stderr, "%s mismatch at $%04X\n", label, (unsigned)(base + i));
            die(label);
        }
    }
}

static void run_control_to_handoff(c64_t *machine) {
    char error[256];
    uint64_t cycle;

    install_media_pal(machine);
    mount_arkanoid(machine);
    for (cycle = 0; cycle < 2500000ull; ++cycle) {
        require("boot", c64_step_cycle(machine, error, sizeof(error)));
    }
    queue_load_star(machine);
    step_to_9400(machine, 200000000ull, "control to $9400");
}

static int drive_has_resident_code(const c64_t *m) {
    /* Custom loaders land in drive RAM (job buffers / $0300+). */
    size_t i;
    int nonzero = 0;
    for (i = 0x0300u; i < 0x0700u; ++i) {
        if (m->drive8.ram[i] != 0) {
            nonzero++;
        }
    }
    return nonzero > 16 && m->drive8.cpu.cpu.pc < 0xC000u;
}

int main(void) {
    c64_t control;
    c64_t mid;
    c64_t restored;
    char error[256];
    uint64_t cycle;
    uint8_t *snapshot = NULL;
    size_t snapshot_size = 0;
    size_t written;
    int mid_ok = 0;
    size_t i;
    uint8_t control_ram[0x10000];

    /* 1) Never-snapshotted control run. */
    run_control_to_handoff(&control);
    assert_region(&control, 0x9400u, vice_9400, sizeof(vice_9400), "control $9400");
    assert_region(&control, 0x5f50u, vice_5f50, sizeof(vice_5f50), "control $5F50");
    memcpy(control_ram, control.bus.ram, sizeof(control_ram));

    /* 2) Mid-transfer snapshot. */
    install_media_pal(&mid);
    mount_arkanoid(&mid);
    for (cycle = 0; cycle < 2500000ull; ++cycle) {
        require("boot mid", c64_step_cycle(&mid, error, sizeof(error)));
    }
    queue_load_star(&mid);

    for (cycle = 0; cycle < 200000000ull; ++cycle) {
        require("step mid", c64_step_cycle(&mid, error, sizeof(error)));
        if (at_instruction_boundary(&mid) && mid.cpu.cpu.pc == 0x9400u) {
            die("reached $9400 before mid-transfer snapshot");
        }
        if (at_instruction_boundary(&mid) && drive_has_resident_code(&mid)) {
            /* Stay in the transfer for a while so we are not on the first upload edge. */
            if (cycle > 2000000ull) {
                mid_ok = 1;
                break;
            }
        }
    }
    require("found mid-transfer point with resident drive code", mid_ok);

    snapshot_size = c64_snapshot_size(&mid);
    require("snapshot size", snapshot_size > 0);
    snapshot = (uint8_t *)malloc(snapshot_size);
    require("alloc snapshot", snapshot != NULL);
    written = c64_snapshot_save(&mid, snapshot, snapshot_size);
    require("snapshot save", written == snapshot_size);

    /* 3) Load into a fresh reset machine and continue. */
    install_media_pal(&restored);
    /* Deliberately do not mount a disk before load — snapshot carries the slot. */
    require("load mid snapshot", c64_snapshot_load(&restored, snapshot, snapshot_size));
    require("restored drive has resident code", drive_has_resident_code(&restored));
    require("restored media tracks", restored.drive8.media.tracks_valid != 0);
    require("restored from_g64", restored.drive8.media.from_g64 != 0);

    {
        uint32_t head_before = restored.drive8.media.head_bit_pos;
        require("post-load media step", c64_step_cycle(&restored, error, sizeof(error)));
        /* One cycle may advance the head; a full rebuild would jump wildly.
           Require tracks still valid and from_g64 sticky. */
        require("tracks still valid after step", restored.drive8.media.tracks_valid != 0);
        require("from_g64 sticky", restored.drive8.media.from_g64 != 0);
        (void)head_before;
    }

    step_to_9400(&restored, 200000000ull, "restored to $9400");
    assert_region(&restored, 0x9400u, vice_9400, sizeof(vice_9400), "restored $9400");
    assert_region(&restored, 0x5f50u, vice_5f50, sizeof(vice_5f50), "restored $5F50");

    for (i = 0; i < sizeof(control_ram); ++i) {
        if (restored.bus.ram[i] != control_ram[i]) {
            fprintf(stderr, "RAM mismatch vs control at $%04X: got %02X want %02X\n",
                    (unsigned)i,
                    restored.bus.ram[i],
                    control_ram[i]);
            die("restored RAM != control RAM");
        }
    }

    free(snapshot);
    c64_unmount_all_drives(&control);
    c64_unmount_all_drives(&mid);
    c64_unmount_all_drives(&restored);
    c1541_destroy(&control.drive8);
    c1541_destroy(&control.drive9);
    c1541_destroy(&mid.drive8);
    c1541_destroy(&mid.drive9);
    c1541_destroy(&restored.drive8);
    c1541_destroy(&restored.drive9);

    printf("PASS: arkanoid mid-transfer snapshot matches control at $9400\n");
    return 0;
}
