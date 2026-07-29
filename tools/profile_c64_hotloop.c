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
#define BENCH_OBSERVER_RING_RECORDS 8192u

typedef enum bench_observer_mode {
    BENCH_OBSERVER_ABSENT = 0,
    BENCH_OBSERVER_STOPPED,
    BENCH_OBSERVER_INSTRUCTION,
    BENCH_OBSERVER_FULL
} bench_observer_mode;

typedef struct bench_observer_record {
    uint64_t cycle;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint8_t kind;
} bench_observer_record;

typedef struct bench_observer_state {
    bench_observer_mode mode;
    uint64_t records;
    uint64_t accesses;
    uint64_t nonfetch_accesses;
    uint64_t wraps;
    uint64_t checksum;
    bench_observer_record ring[BENCH_OBSERVER_RING_RECORDS];
} bench_observer_state;

static bool has_flag(int argc, char **argv, const char *name);

static void bench_observer_begin(
    void *user,
    const c64_cpu_observer_begin *begin) {
    bench_observer_state *state = (bench_observer_state *)user;
    bench_observer_record *record;
    size_t slot;

    if (state->mode < BENCH_OBSERVER_INSTRUCTION) {
        return;
    }
    slot = (size_t)(state->records % BENCH_OBSERVER_RING_RECORDS);
    if (slot == 0u && state->records != 0u) {
        state->wraps++;
    }
    record = &state->ring[slot];
    record->cycle = begin->machine_cycle;
    record->pc = begin->pc;
    record->a = begin->a;
    record->x = begin->x;
    record->y = begin->y;
    record->sp = begin->sp;
    record->p = begin->p;
    record->kind = (uint8_t)begin->kind;
    state->records++;
}

static void bench_observer_access(
    void *user,
    uint64_t machine_cycle,
    uint16_t address,
    uint8_t value,
    c6510_bus_access_kind kind) {
    bench_observer_state *state = (bench_observer_state *)user;

    if (state->mode != BENCH_OBSERVER_FULL) {
        return;
    }
    state->accesses++;
    if (kind != C6510_BUS_ACCESS_OPCODE_FETCH &&
        kind != C6510_BUS_ACCESS_OPERAND_READ) {
        state->nonfetch_accesses++;
    }
    state->checksum +=
        machine_cycle + address + value + kind;
}

static void bench_observer_complete(void *user) {
    bench_observer_state *state = (bench_observer_state *)user;
    if (state->mode >= BENCH_OBSERVER_INSTRUCTION) {
        state->checksum += state->records;
    }
}

static const char *bench_observer_mode_name(bench_observer_mode mode) {
    switch (mode) {
    case BENCH_OBSERVER_STOPPED: return "stopped";
    case BENCH_OBSERVER_INSTRUCTION: return "instruction";
    case BENCH_OBSERVER_FULL: return "full";
    case BENCH_OBSERVER_ABSENT:
    default: return "absent";
    }
}

static bench_observer_mode parse_observer_mode(int argc, char **argv) {
    if (has_flag(argc, argv, "observer-stopped")) {
        return BENCH_OBSERVER_STOPPED;
    }
    if (has_flag(argc, argv, "observer-instruction")) {
        return BENCH_OBSERVER_INSTRUCTION;
    }
    if (has_flag(argc, argv, "observer-full")) {
        return BENCH_OBSERVER_FULL;
    }
    return BENCH_OBSERVER_ABSENT;
}

static void install_access_heavy_workload(c64_t *machine) {
    static const uint8_t program[] = {
        0xadu, 0x12u, 0xd0u, /* LDA $D012 */
        0xeeu, 0x00u, 0x04u, /* INC $0400 */
        0x8du, 0x20u, 0xd0u, /* STA $D020 */
        0x4cu, 0x00u, 0xc0u, /* JMP $C000 */
    };
    size_t i;

    for (i = 0u; i < sizeof(program); ++i) {
        c64_debug_write_ram(machine, (uint16_t)(0xc000u + i), program[i]);
    }
    machine->cpu.cpu.pc = 0xc000u;
}

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
 *   no-video     disable VIC live pixel paint (warp-like)
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
    bool access_heavy = has_flag(argc, argv, "access-heavy");
    bench_observer_state observer_state = {
        .mode = parse_observer_mode(argc, argv),
    };
    c64_cpu_observer observer = {
        .begin = bench_observer_begin,
        .access = bench_observer_access,
        .complete = bench_observer_complete,
    };

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
    if (access_heavy) {
        install_access_heavy_workload(&machine);
    }
    if (observer_state.mode != BENCH_OBSERVER_ABSENT) {
        c64_set_cpu_observer(&machine, &observer, &observer_state);
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
        "video=%s workload=%s observer=%s records=%llu records_per_second=%.0f "
        "accesses=%llu accesses_per_second=%.0f nonfetch_accesses=%llu "
        "bytes_per_execution=%.3f wraps=%llu checksum=%llu "
        "drive8_rom=%d drive9_rom=%d emulate_1541=%d media_1541=%d\n",
        (unsigned long long)cycles,
        elapsed,
        elapsed > 0.0 ? (double)cycles / elapsed / 1000000.0 : 0.0,
        machine.cpu.cpu.pc,
        (unsigned long long)machine.clock.cycle,
        (unsigned long long)machine.clock.cpu_cycles,
        c64_video_output_enabled(&machine) ? "on" : "off",
        access_heavy ? "access-heavy" : "idle-basic",
        bench_observer_mode_name(observer_state.mode),
        (unsigned long long)observer_state.records,
        elapsed > 0.0 ? (double)observer_state.records / elapsed : 0.0,
        (unsigned long long)observer_state.accesses,
        elapsed > 0.0 ? (double)observer_state.accesses / elapsed : 0.0,
        (unsigned long long)observer_state.nonfetch_accesses,
        observer_state.records > 0u ?
            (22.0 * (double)observer_state.records +
             6.0 * (double)observer_state.nonfetch_accesses) /
                (double)observer_state.records :
            0.0,
        (unsigned long long)observer_state.wraps,
        (unsigned long long)observer_state.checksum,
        machine.drive8.rom_loaded,
        machine.drive9.rom_loaded,
        machine.config.emulate_1541,
        machine.config.media_1541);
    return 0;
}
