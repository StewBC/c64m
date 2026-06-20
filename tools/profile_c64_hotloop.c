#include "c64.h"
#include "c64_rom.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_CYCLES 20000000ULL

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static bool load_default_roms(c64_rom_set *roms, char *error, size_t error_size) {
    c64_rom_set_init(roms);
    return c64_rom_load_combined_64c(roms, "roms/system.rom", error, error_size) &&
        c64_rom_load_character(roms, "roms/character.rom", error, error_size);
}

static uint64_t parse_cycles(int argc, char **argv) {
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        return DEFAULT_CYCLES;
    }
    return strtoull(argv[1], NULL, 10);
}

int main(int argc, char **argv) {
    c64_rom_set roms;
    c64_t machine;
    c64_config config = {
        .video_standard = C64_VIDEO_STANDARD_PAL,
    };
    char error[256];
    uint64_t cycles = parse_cycles(argc, argv);
    uint64_t i;
    double start;
    double elapsed;
    bool null_error = argc >= 3 && strcmp(argv[2], "null-error") == 0;

    if (!load_default_roms(&roms, error, sizeof(error))) {
        fprintf(stderr, "failed to load ROMs: %s\n", error);
        return 1;
    }

    c64_init(&machine);
    c64_set_config(&machine, &config);
    if (!c64_install_roms(&machine, &roms, error, sizeof(error)) ||
        !c64_reset(&machine, error, sizeof(error))) {
        fprintf(stderr, "failed to initialize C64: %s\n", error);
        return 1;
    }
    c64_set_audio_output_enabled(&machine, false);

    start = monotonic_seconds();
    for (i = 0; i < cycles; i++) {
        if (!c64_step_cycle(&machine, null_error ? NULL : error, null_error ? 0 : sizeof(error))) {
            fprintf(stderr, "step failed at %llu: %s\n", (unsigned long long)i, error);
            return 1;
        }
    }
    elapsed = monotonic_seconds() - start;

    printf(
        "cycles=%llu seconds=%.6f mhz=%.3f pc=%04x machine_cycle=%llu cpu_cycles=%llu\n",
        (unsigned long long)cycles,
        elapsed,
        elapsed > 0.0 ? (double)cycles / elapsed / 1000000.0 : 0.0,
        machine.cpu.cpu.pc,
        (unsigned long long)machine.clock.cycle,
        (unsigned long long)machine.clock.cpu_cycles);
    return 0;
}
