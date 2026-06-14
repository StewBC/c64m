#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    C64_RAM_SIZE = 0x10000,
    C64_BASIC_ROM_SIZE = 0x2000,
    C64_CHAR_ROM_SIZE = 0x1000,
    C64_KERNAL_ROM_SIZE = 0x2000,
};

typedef struct c64_bus_t {
    uint8_t ram[C64_RAM_SIZE];
    uint8_t basic_rom[C64_BASIC_ROM_SIZE];
    uint8_t char_rom[C64_CHAR_ROM_SIZE];
    uint8_t kernal_rom[C64_KERNAL_ROM_SIZE];
    uint8_t cpu_port_direction;
    uint8_t cpu_port_data;
} c64_bus_t;

void c64_bus_init(c64_bus_t *bus);
void c64_bus_reset(c64_bus_t *bus);

uint8_t c64_bus_read(c64_bus_t *bus, uint16_t address);
void c64_bus_write(c64_bus_t *bus, uint16_t address, uint8_t value);

bool c64_bus_set_basic_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_char_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_kernal_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_system_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
