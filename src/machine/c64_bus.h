#pragma once

#include <assert.h>
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
    C64_CARTRIDGE_ROM_BANK_SIZE = 0x2000,
    /* VICE Magic Desk max: 128 × 8 KiB (1 MiB homebrew / DDI variants). */
    C64_CARTRIDGE_MAX_BANKS = 128,
    C64_CARTRIDGE_HW_NORMAL = 0,
    C64_CARTRIDGE_HW_MAGIC_DESK = 19
};

typedef enum c64_cartridge_mode {
    C64_CARTRIDGE_MODE_NONE = 0,
    C64_CARTRIDGE_MODE_8K,
    C64_CARTRIDGE_MODE_16K,
    C64_CARTRIDGE_MODE_ULTIMAX
} c64_cartridge_mode;

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
    /* Active ROML window cache (bank 0 for type 0; selected bank for Magic Desk). */
    uint8_t cartridge_roml[C64_CARTRIDGE_ROM_BANK_SIZE];
    uint8_t cartridge_romh[C64_CARTRIDGE_ROM_BANK_SIZE];
    /* Multi-bank ROML storage (heap). bank_count × 8 KiB; NULL when detached. */
    uint8_t *cartridge_rom_banks;
    uint16_t cartridge_bank_count;
    uint8_t cartridge_bank_mask;
    uint8_t cartridge_io_latch;
    uint16_t cartridge_hardware_type;
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
    bool cartridge_mounted;
    bool cartridge_roml_present;
    bool cartridge_romh_present;
    uint8_t cartridge_exrom;
    uint8_t cartridge_game;
    c64_cartridge_mode cartridge_mode;
    /* VICE-compatible debugcart ($D7FF write exits test with value). Opt-in. */
    bool debugcart_enabled;
    bool debugcart_hit;
    uint8_t debugcart_value;

    /* Host 6510 PC snapshot for VIC-II open-bus cbuf during BA lead-in.
       VICE samples ram_base_phi2[reg_pc] & 0x0f when AEC is still high
       (vicii_fetch_matrix). Updated by the machine before each VIC cycle. */
    uint16_t cpu_open_bus_pc;
};

void c64_bus_init(c64_bus_t *bus);
void c64_bus_reset(c64_bus_t *bus);
void c64_bus_attach_vicii(c64_bus_t *bus, vicii *v);
void c64_bus_attach_cias(c64_bus_t *bus, cia *cia1, cia *cia2);
void c64_bus_attach_sid(c64_bus_t *bus, sid *s);
void c64_bus_refresh_vic_bank_base(c64_bus_t *bus);

uint8_t c64_bus_read(c64_bus_t *bus, uint16_t address);
void c64_bus_write(c64_bus_t *bus, uint16_t address, uint8_t value);

/* Hot VIC-II accessors: keep these static inline so the pixel path can fold
   bank/ram/color/glyph peeks without a call across the translation unit. */
static inline uint8_t c64_bus_vic_read_ram(const c64_bus_t *bus, uint16_t address) {
    assert(bus);
    return bus->ram[address];
}

static inline uint8_t c64_bus_vic_read_screen(const c64_bus_t *bus, uint16_t offset) {
    assert(bus);
    return bus->ram[(uint16_t)(0x0400u + (offset % 1000u))];
}

static inline uint8_t c64_bus_vic_read_color(const c64_bus_t *bus, uint16_t offset) {
    assert(bus);
    return (uint8_t)(bus->color_ram[offset % C64_COLOR_RAM_SIZE] & 0x0f);
}

static inline uint8_t c64_bus_vic_read_char_glyph(
    const c64_bus_t *bus,
    uint8_t character_code,
    uint8_t glyph_row)
{
    assert(bus);
    return bus->char_rom[((uint16_t)character_code * 8u) + (glyph_row & 0x07u)];
}

static inline uint8_t c64_bus_vic_read_char_glyph_at(
    const c64_bus_t *bus,
    uint16_t character_base,
    uint8_t character_code,
    uint8_t glyph_row)
{
    uint16_t full_addr;

    assert(bus);
    /* character_base is already a full absolute VIC address (vic_bank + offset). */
    full_addr = (uint16_t)(character_base + (uint16_t)character_code * 8u + (glyph_row & 0x07u));
    /* Char ROM appears at $1000-$1FFF (bank 0) and $9000-$9FFF (bank 2). */
    if ((full_addr & 0xF000u) == 0x1000u || (full_addr & 0xF000u) == 0x9000u) {
        return bus->char_rom[full_addr & 0x0FFFu];
    }
    return bus->ram[full_addr];
}

static inline uint16_t c64_bus_vic_bank_base(const c64_bus_t *bus) {
    assert(bus);
    return bus->vic_bank_base;
}

bool c64_bus_set_basic_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_char_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_kernal_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_set_system_rom(c64_bus_t *bus, const uint8_t *data, size_t size);
bool c64_bus_attach_generic_cartridge(
    c64_bus_t *bus,
    const uint8_t *roml,
    size_t roml_size,
    const uint8_t *romh,
    size_t romh_size,
    uint8_t exrom,
    uint8_t game);
/* Magic Desk (CRT type 19): banks is bank_count contiguous 8 KiB ROML images. */
bool c64_bus_attach_magic_desk_cartridge(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    uint8_t exrom,
    uint8_t game);
void c64_bus_detach_cartridge(c64_bus_t *bus);
/* Re-apply mapper power-on state (Magic Desk: bank 0, cart enabled). */
void c64_bus_cartridge_reset(c64_bus_t *bus);
/* Apply current Magic Desk IO latch to mode + ROML window (no-op for other mappers). */
void c64_bus_cartridge_apply_banking(c64_bus_t *bus);
bool c64_bus_cartridge_read(const c64_bus_t *bus, uint16_t address, uint8_t *out_value);
