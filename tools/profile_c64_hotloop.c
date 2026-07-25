#include "c64.h"
#include "c64_rom.h"
#include "c1541.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define DEFAULT_CYCLES 20000000ULL

static double monotonic_seconds(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

static bool load_default_roms(c64_rom_set *roms, char *error, size_t error_size) {
    c64_rom_set_init(roms);
    return c64_rom_load_combined_64c(roms, "roms/system.rom", error, error_size) &&
        c64_rom_load_character(roms, "roms/character.rom", error, error_size);
}

/* Flags after the cycle count (any order):
 *   no-video     disable VIC live ARGB paint (warp-like)
 *   null-error   pass NULL error buffer to c64_step_cycle
 *   1541         load roms/1541.rom into drive8+drive9 (steps both every cycle)
 *   1541-one     load 1541 ROM into drive8 only
 *   media        with 1541: enable emulate_1541 + media_1541 (GCR path when image present)
 */
static uint64_t parse_cycles(int argc, char **argv) {
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        return DEFAULT_CYCLES;
    }
    return strtoull(argv[1], NULL, 10);
}

static bool has_flag(int argc, char **argv, const char *name) {
    int i;
    for (i = 2; i < argc; ++i) {
        if (argv[i] != NULL && strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv) {
    c64_rom_set roms;
    c64_t machine;
    c64_config config = {
        .video_standard = C64_VIDEO_STANDARD_PAL,
    };
    char error[256];
    uint64_t cycles = parse_cycles(argc, argv);
    uint64_t left;
    double start;
    double elapsed;
    bool null_error = has_flag(argc, argv, "null-error");
    bool no_video = has_flag(argc, argv, "no-video");
    bool load_1541 = has_flag(argc, argv, "1541") || has_flag(argc, argv, "1541-one");
    bool drive9_rom = has_flag(argc, argv, "1541");
    bool media = has_flag(argc, argv, "media");

    if (!load_default_roms(&roms, error, sizeof(error))) {
        fprintf(stderr, "failed to load ROMs: %s\n", error);
        return 1;
    }

    c64_init(&machine);
    if (load_1541 && media) {
        config.emulate_1541 = 1;
        config.media_1541 = 1;
    }
    c64_set_config(&machine, &config);
    if (!c64_install_roms(&machine, &roms, error, sizeof(error)) ||
        !c64_reset(&machine, error, sizeof(error))) {
        fprintf(stderr, "failed to initialize C64: %s\n", error);
        return 1;
    }
    if (load_1541) {
        if (c1541_load_rom(&machine.drive8, "roms/1541.rom") == 0) {
            fprintf(stderr, "failed to load roms/1541.rom into drive8\n");
            return 1;
        }
        if (drive9_rom) {
            if (c1541_load_rom(&machine.drive9, "roms/1541.rom") == 0) {
                fprintf(stderr, "failed to load roms/1541.rom into drive9\n");
                return 1;
            }
        }
        c1541_reset(&machine.drive8);
        if (drive9_rom) {
            c1541_reset(&machine.drive9);
        }
        /* Soft power: ROM load alone does not step drives; power units under test. */
        (void)c64_power_on_drive(&machine, 8);
        if (drive9_rom) {
            (void)c64_power_on_drive(&machine, 9);
        }
    }
    c64_set_audio_output_enabled(&machine, false);
    if (no_video) {
        c64_set_video_output_enabled(&machine, false);
    }

    start = monotonic_seconds();
    /* Batch Phi2 steps: mid-instruction uses the thinner micro hot path. */
    left = cycles;
    while (left > 0u) {
        uint32_t chunk = left > 1000000ull ? 1000000u : (uint32_t)left;
        if (!c64_step_cycles(&machine, chunk,
                             null_error ? NULL : error,
                             null_error ? 0 : sizeof(error))) {
            fprintf(stderr, "step failed: %s\n", error);
            return 1;
        }
        left -= chunk;
    }
    elapsed = monotonic_seconds() - start;

    printf(
        "cycles=%llu seconds=%.6f mhz=%.3f pc=%04x machine_cycle=%llu cpu_cycles=%llu "
        "video=%s drive8_rom=%d drive9_rom=%d emulate_1541=%d media_1541=%d\n",
        (unsigned long long)cycles,
        elapsed,
        elapsed > 0.0 ? (double)cycles / elapsed / 1000000.0 : 0.0,
        machine.cpu.cpu.pc,
        (unsigned long long)machine.clock.cycle,
        (unsigned long long)machine.clock.cpu_cycles,
        c64_video_output_enabled(&machine) ? "on" : "off",
        machine.drive8.rom_loaded,
        machine.drive9.rom_loaded,
        machine.config.emulate_1541,
        machine.config.media_1541);
    return 0;
}
