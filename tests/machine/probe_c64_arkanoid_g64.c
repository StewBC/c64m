/*
 * Arkanoid G64 Phase-1 checkpoint probe.
 *
 * This is intentionally not a ctest gate yet. It captures the current real
 * 1541/media execution at semantic milestones so it can be compared with the
 * corresponding VICE monitor captures described in C64mG64-fix.md.
 */
#include "c64.h"
#include "c64_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum { FILENAME_BUFFER = 0x0200 };

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

    if (file == NULL) return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) { fclose(file); return 0; }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) { fclose(file); return 0; }
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
    require("load 1541 rom", c1541_load_rom(
        &machine->drive8, C64M_SOURCE_DIR "/roms/1541.rom") != 0);
    require("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    require("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void mount_arkanoid(c64_t *machine) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/arkanoid[imagine_1988](pal).g64",
        C64M_SOURCE_DIR);
    require("read g64", read_file(path, &bytes, &size));
    require("mount g64", c64_mount_g64(machine, 8, bytes, size, "arkanoid.g64") ==
        C64_DRIVE_STATUS_OK);
    free(bytes);
}

static void setup_load_star(c64_t *machine) {
    machine->cpu.cpu.pc = 0xffd5u;
    machine->cpu.cpu.A = 0;
    machine->cpu.cpu.X = 0xffu;
    machine->cpu.cpu.Y = 0xffu;
    machine->cpu.cpu.flags |= 0x01u;
    machine->bus.ram[0xbau] = 8;
    machine->bus.ram[0xb9u] = 1;
    machine->bus.ram[0xb7u] = 1;
    machine->bus.ram[0xbbu] = (uint8_t)FILENAME_BUFFER;
    machine->bus.ram[0xbcu] = 0;
    machine->bus.ram[FILENAME_BUFFER] = '*';
}

static void print_checkpoint(const char *id, const c64_t *m) {
    const CPU *cpu = &m->cpu.cpu;
    const c1541 *d = &m->drive8;
    printf(
        "%s cyc=%llu c64=%04X A=%02X X=%02X Y=%02X SP=%02X P=%02X M01=%02X "
        "d8=%04X A=%02X X=%02X Y=%02X SP=%02X VIA2=%02X/%02X DDR=%02X/%02X "
        "ht=%d dens=%d bit=%u sync=%d byte=%02X\n",
        id, (unsigned long long)m->clock.cycle, cpu->pc, cpu->A, cpu->X, cpu->Y,
        cpu->sp, cpu->flags, m->bus.ram[1], d->cpu.cpu.pc, d->cpu.cpu.A,
        d->cpu.cpu.X, d->cpu.cpu.Y, d->cpu.cpu.sp, d->via2.ora, d->via2.orb,
        d->via2.ddra, d->via2.ddrb, d->media.half_track, d->media.density,
        d->media.head_bit_pos, d->media.in_sync, d->media.port_a_byte);
}

int main(void) {
    c64_t machine;
    char error[256];
    uint64_t cycle;
    int saw_a1 = 0;
    int saw_a2 = 0;

    printf("ARKANOID_G64_CHECKPOINT_PROBE start\n");
    fflush(stdout);
    install_media_pal(&machine);
    mount_arkanoid(&machine);

    for (cycle = 0; cycle < 2500000ull; ++cycle) {
        require("boot", c64_step_cycle(&machine, error, sizeof(error)));
    }
    setup_load_star(&machine);

    for (cycle = 0; cycle < 200000000ull; ++cycle) {
        uint16_t pc = machine.cpu.cpu.pc;

        if (!machine.cpu.micro_active && !machine.pending_cpu_trace_active) {
            if (!saw_a1 && pc == 0xffd5u && machine.bus.ram[0xb7u] == 1u &&
                machine.bus.ram[0x0395u] == 'F') {
                print_checkpoint("A1", &machine);
                saw_a1 = 1;
            }
            if (!saw_a2 && pc == 0x4000u) {
                print_checkpoint("A2", &machine);
                printf("A2_mem=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    machine.bus.ram[0x4000], machine.bus.ram[0x4001],
                    machine.bus.ram[0x4002], machine.bus.ram[0x4003],
                    machine.bus.ram[0x4004], machine.bus.ram[0x4005],
                    machine.bus.ram[0x4006], machine.bus.ram[0x4007]);
                saw_a2 = 1;
            }
            if (pc == 0x9400u) {
                print_checkpoint("A3", &machine);
                printf("A3_mem=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    machine.bus.ram[0x9400], machine.bus.ram[0x9401],
                    machine.bus.ram[0x9402], machine.bus.ram[0x9403],
                    machine.bus.ram[0x9404], machine.bus.ram[0x9405],
                    machine.bus.ram[0x9406], machine.bus.ram[0x9407]);
                return 0;
            }
        }
        require("step", c64_step_cycle(&machine, error, sizeof(error)));
    }

    die("did not reach Arkanoid A3 checkpoint");
    return 1;
}
