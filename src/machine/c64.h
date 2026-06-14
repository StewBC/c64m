#pragma once

#include "c64_bus.h"
#include "c64_rom.h"
#include "cpu.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct c64_cpu_snapshot {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint64_t cycles;
} c64_cpu_snapshot;

typedef struct c64_t {
    c64_bus_t bus;
    C6510 cpu;
    bool has_basic_rom;
    bool has_kernal_rom;
    bool has_character_rom;
    bool ready;
} c64_t;

void c64_init(c64_t *machine);
bool c64_install_roms(c64_t *machine, const c64_rom_set *roms, char *error, size_t error_size);
bool c64_reset(c64_t *machine, char *error, size_t error_size);
bool c64_step_instruction(c64_t *machine, char *error, size_t error_size);
void c64_copy_cpu_snapshot(const c64_t *machine, c64_cpu_snapshot *out);
