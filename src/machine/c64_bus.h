#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef C64M_VICII_TYPEDEF
#define C64M_VICII_TYPEDEF
typedef struct vicii vicii;
#endif

#ifndef C64M_CIA_TYPEDEF
#define C64M_CIA_TYPEDEF
typedef struct cia cia;
#endif

#ifndef C64M_SID_TYPEDEF
#define C64M_SID_TYPEDEF
typedef struct sid sid;
#endif

enum {
    C64_RAM_SIZE = 0x10000,
    C64_COLOR_RAM_SIZE = 0x0400,
    C64_BASIC_ROM_SIZE = 0x2000,
    C64_CHAR_ROM_SIZE = 0x1000,
    C64_KERNAL_ROM_SIZE = 0x2000,
};

#ifndef C64M_C64_BUS_TYPEDEF
#define C64M_C64_BUS_TYPEDEF
typedef struct c64_bus_t c64_bus_t;
#endif

struct c64_bus_t {
    uint8_t ram[C64_RAM_SIZE];
    uint8_t color_ram[C64_COLOR_RAM_SIZE];
    uint8_t basic_rom[C64_BASIC_ROM_SIZE];
    uint8_t char_rom[C64_CHAR_ROM_SIZE];
    uint8_t kernal_rom[C64_KERNAL_ROM_SIZE];
    vicii *vic;
    cia *cia1;
    cia *cia2;
    sid *sid;
    uint8_t cpu_port_direction;
    uint8_t cpu_port_data;
    uint64_t screen_ram_writes;
    uint64_t color_ram_writes;
    uint64_t vic_register_writes;
    uint64_t cia1_register_writes;
    uint64_t cia2_register_writes;
    uint64_t sid_register_writes;
    uint16_t vic_bank_base;
};

void c64_bus_init(c64_bus_t *bus);
void c64_bus_reset(c64_bus_t *bus);
void c64_bus_attach_vicii(c64_bus_t *bus, vicii *v);
void c64_bus_attach_cias(c64_bus_t *bus, cia *cia1, cia *cia2);
void c64_bus_attach_sid(c64_bus_t *bus, sid *s);
void c64_bus_refresh_vic_bank_base(c64_bus_t *bus);

uint8_t c64_bus_read(c64_bus_t *bus, uint16_t address);
void c64_bus_write(c64_bus_t *bus, uint16_t address, uint8_t value);
uint8_t c64_bus_vic_read_ram(const c64_bus_t *bus, uint16_t address);
uint8_t c64_bus_vic_read_screen(const c64_bus_t *bus, uint16_t offset);
uint8_t c64_bus_vic_read_color(const c64_bus_t *bus, uint16_t offset);
uint8_t c64_bus_vic_read_char_glyph(const c64_bus_t *bus, uint8_t character_code, uint8_t glyph_row);
uint8_t c64_bus_vic_read_char_glyph_at(
    const c64_bus_t *bus,
    uint16_t character_base,
    uint8_t character_code,
    uint8_t glyph_row);

bool c64_bus_set_basic_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_char_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_kernal_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_system_rom(c64_bus_t *bus, const uint8_t *data, size_t size);

uint16_t c64_bus_vic_bank_base(const c64_bus_t *bus);
