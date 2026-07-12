/*
 * Arkanoid G64 Phase-1 checkpoint probe.
 *
 * Exercises the real 1541/G64 path through Arkanoid's V-MAX loader without
 * title-specific drive behavior.
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
    BOOTSTRAP_ENTRY = 0x0363
};

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

static void mount_arkanoid(c64_t *machine, int alt) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/arkanoid[imagine_1988](pal)%s.g64",
        C64M_SOURCE_DIR, alt ? "(alt)" : "");
    require("read g64", read_file(path, &bytes, &size));
    require("mount g64", c64_mount_g64(machine, 8, bytes, size, "arkanoid.g64") ==
        C64_DRIVE_STATUS_OK);
    free(bytes);
}

static void queue_load_star(c64_t *machine) {
    static const uint8_t command[] = { 'L', 'O', 'A', 'D', ' ', '"', '*', '"', ',', '8', ',', '1', 0x0d };
    memcpy(machine->bus.ram + 0x0277u, command, sizeof(command));
    machine->bus.ram[0x00c6u] = (uint8_t)sizeof(command);
}

static void print_checkpoint(const char *id, const c64_t *m) {
    const CPU *cpu = &m->cpu.cpu;
    const c1541 *d = &m->drive8;
    printf(
        "%s cyc=%llu c64=%04X A=%02X X=%02X Y=%02X SP=%02X P=%02X M01=%02X "
        "st=%02X d8=%04X A=%02X X=%02X Y=%02X SP=%02X VIA2=%02X/%02X DDR=%02X/%02X "
        "ht=%d dens=%d bit=%u sync=%d byte=%02X\n",
        id, (unsigned long long)m->clock.cycle, cpu->pc, cpu->A, cpu->X, cpu->Y,
        cpu->sp, cpu->flags, m->bus.ram[1], m->bus.ram[0x90], d->cpu.cpu.pc, d->cpu.cpu.A,
        d->cpu.cpu.X, d->cpu.cpu.Y, d->cpu.cpu.sp, d->via2.ora, d->via2.orb,
        d->via2.ddra, d->via2.ddrb, d->media.half_track, d->media.density,
        d->media.head_bit_pos, d->media.in_sync, d->media.port_a_byte);
}

static uint8_t drive_opcode(const c1541 *d) {
    uint16_t pc = d->cpu.cpu.pc;
    if (pc < 0x1000u) return d->ram[pc & 0x07ffu];
    if (pc >= 0xc000u) return d->rom[pc - 0xc000u];
    return 0xeau;
}

int main(int argc, char **argv) {
    c64_t machine;
    char error[256];
    uint64_t cycle;
    int saw_a1 = 0;
    int saw_a2 = 0;
    int saw_a3 = 0;
    uint64_t a3_cycle = 0;
    int reported_5f_loop = 0;
    uint32_t deferred_undoc[256] = { 0 };
    uint32_t post_a3_deferred_undoc[256] = { 0 };
    uint32_t drive_deferred_undoc[256] = { 0 };

    int alt = argc > 1 && strcmp(argv[1], "alt") == 0;

    printf("ARKANOID_G64_CHECKPOINT_PROBE %s start\n", alt ? "alt" : "original");
    fflush(stdout);
    install_media_pal(&machine);
    mount_arkanoid(&machine, alt);

    for (cycle = 0; cycle < 2500000ull; ++cycle) {
        require("boot", c64_step_cycle(&machine, error, sizeof(error)));
    }
    queue_load_star(&machine);

    for (cycle = 0; cycle < 500000000ull; ++cycle) {
        uint16_t pc = machine.cpu.cpu.pc;

        if ((cycle % 10000000ull) == 0ull) {
            print_checkpoint("progress", &machine);
            fflush(stdout);
        }

        if (!machine.cpu.micro_active && !machine.pending_cpu_trace_active) {
            if (saw_a2 && !saw_a3 && !machine.drive8.cpu.micro_active) {
                uint8_t opcode = drive_opcode(&machine.drive8);
                if (!c6510_micro_can_begin(&machine.drive8.cpu, opcode)) {
                    drive_deferred_undoc[opcode]++;
                }
            }
            if (saw_a2 && !saw_a3) {
                uint8_t opcode = c64_debug_read_cpu_map(&machine, pc);
                if (!c6510_micro_can_begin(&machine.cpu, opcode)) {
                    deferred_undoc[opcode]++;
                }
            }
            if (saw_a3) {
                uint8_t opcode = c64_debug_read_cpu_map(&machine, pc);
                if (!c6510_micro_can_begin(&machine.cpu, opcode)) {
                    post_a3_deferred_undoc[opcode]++;
                }
            }
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
                {
                    FILE *dump = fopen(alt ? "/private/tmp/c64m-arkanoid-alt-a2.bin"
                                             : "/private/tmp/c64m-arkanoid-a2.bin", "wb");
                    if (dump != NULL) {
                        (void)fwrite(machine.bus.ram, 1, sizeof(machine.bus.ram), dump);
                        fclose(dump);
                    }
                }
                saw_a2 = 1;
            }
            if (!saw_a3 && pc == 0x9400u) {
                print_checkpoint("A3", &machine);
                printf("A3_mem=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    machine.bus.ram[0x9400], machine.bus.ram[0x9401],
                    machine.bus.ram[0x9402], machine.bus.ram[0x9403],
                    machine.bus.ram[0x9404], machine.bus.ram[0x9405],
                    machine.bus.ram[0x9406], machine.bus.ram[0x9407]);
                saw_a3 = 1;
                a3_cycle = cycle;
                {
                    FILE *dump = fopen(alt ? "/private/tmp/c64m-arkanoid-alt-a3.bin"
                                             : "/private/tmp/c64m-arkanoid-a3.bin", "wb");
                    if (dump != NULL) {
                        (void)fwrite(machine.bus.ram, 1, sizeof(machine.bus.ram), dump);
                        fclose(dump);
                    }
                }
                {
                    unsigned op;
                    printf("A2_deferred:");
                    for (op = 0; op < 256; ++op) {
                        if (deferred_undoc[op] != 0u) {
                            printf(" %02X=%u", op, deferred_undoc[op]);
                        }
                    }
                    printf("\n");
                    printf("drive_A2_deferred:");
                    for (op = 0; op < 256; ++op) {
                        if (drive_deferred_undoc[op] != 0u) {
                            printf(" %02X=%u", op, drive_deferred_undoc[op]);
                        }
                    }
                    printf("\n");
                }
            }
            if (saw_a3 && c64_debug_read_cpu_map(&machine, pc) == 0x00u &&
                pc >= 0x0200u && pc < 0xA000u) {
                fprintf(stderr, "BRK after A3 at %04X\n", pc);
                return 1;
            }
            /* The corrupt-load failure renders random character data while
               spinning at this address; surviving the handoff alone is not
               a valid Arkanoid acceptance condition. */
            if (saw_a3 && !reported_5f_loop && pc >= 0x5f50u && pc <= 0x5f8fu) {
                int j;
                fprintf(stderr, "corrupt Arkanoid execution loop at %04X\n", pc);
                for (j = 0; j < 32; ++j) {
                    if ((j % 16) == 0) fprintf(stderr, "%04X:", 0x5f50 + j);
                    fprintf(stderr, " %02X", machine.bus.ram[0x5f50 + j]);
                    if ((j % 16) == 15) fprintf(stderr, "\n");
                }
                fprintf(stderr, "screen writes: 0400=%08llX 1C00=%08llX 2000=%08llX 4000=%08llX\n",
                    (unsigned long long)c64_debug_read_write_history(&machine, 0x0400),
                    (unsigned long long)c64_debug_read_write_history(&machine, 0x1c00),
                    (unsigned long long)c64_debug_read_write_history(&machine, 0x2000),
                    (unsigned long long)c64_debug_read_write_history(&machine, 0x4000));
                fprintf(stderr, "post_A3_deferred:");
                for (j = 0; j < 256; ++j) {
                    if (post_a3_deferred_undoc[j] != 0u) {
                        fprintf(stderr, " %02X=%u", j, post_a3_deferred_undoc[j]);
                    }
                }
                fprintf(stderr, "\n");
                fprintf(stderr,
                    "VIC: D011=%02X D016=%02X D018=%02X D020=%02X D021=%02X bank=%04X raster=%u\n",
                    machine.vic.registers[0x11], machine.vic.registers[0x16],
                    machine.vic.registers[0x18], machine.vic.registers[0x20],
                    machine.vic.registers[0x21], machine.bus.vic_bank_base,
                    machine.vic.timing.raster_line);
                reported_5f_loop = 1;
            }
            if (saw_a3 && cycle - a3_cycle >= 100000000ull) {
                printf("POST_A3_100M c64=%04X d8=%04X\n", machine.cpu.cpu.pc,
                    machine.drive8.cpu.cpu.pc);
                return reported_5f_loop ? 1 : 0;
            }
        }
        require("step", c64_step_cycle(&machine, error, sizeof(error)));
    }

    die("did not reach Arkanoid A3 checkpoint");
    return 1;
}
