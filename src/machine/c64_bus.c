#include "c64_bus.h"

#include "cia.h"
#include "sid.h"
#include "vicii.h"

#include <assert.h>
#include <string.h>

enum {
    C64_CPU_PORT_DIRECTION = 0x0000,
    C64_CPU_PORT_DATA = 0x0001,
    C64_DEFAULT_SCREEN_BASE = 0x0400,
    C64_SID_BASE = 0xd400,
    C64_SID_END  = 0xd41f,
    C64_COLOR_RAM_BASE = 0xd800,
    C64_COLOR_RAM_END = 0xdbff,
    C64_CIA1_BASE = 0xdc00,
    C64_CIA1_END = 0xdcff,
    C64_CIA2_BASE = 0xdd00,
    C64_CIA2_END = 0xddff,
    C64_CIA_REG_PORT_A = 0x00,
    C64_CIA_REG_DDRA = 0x02,
};

static uint16_t c64_bus_compute_vic_bank_base(const c64_bus_t *bus) {
    uint8_t pa;

    if (!bus->cia2) {
        return 0;
    }

    pa = cia_read_port_a_pins(bus->cia2);
    return (uint16_t)(((~pa) & 3u) * 0x4000u);
}

static uint8_t c64_bus_cpu_port_read(const c64_bus_t *bus, uint16_t address) {
    if (address == C64_CPU_PORT_DIRECTION) {
        return bus->cpu_port_direction;
    }

    return bus->cpu_port_data;
}

static uint8_t c64_bus_cpu_port_value(const c64_bus_t *bus) {
    return bus->cpu_port_data;
}

static bool c64_bus_basic_visible(const c64_bus_t *bus) {
    uint8_t port = c64_bus_cpu_port_value(bus);
    return (port & 0x03) == 0x03;
}

static bool c64_bus_kernal_visible(const c64_bus_t *bus) {
    uint8_t port = c64_bus_cpu_port_value(bus);
    return (port & 0x02) != 0;
}

static bool c64_bus_io_or_char_visible(const c64_bus_t *bus) {
    uint8_t port = c64_bus_cpu_port_value(bus);
    return (port & 0x03) != 0;
}

static bool c64_bus_io_visible(const c64_bus_t *bus) {
    uint8_t port = c64_bus_cpu_port_value(bus);
    return c64_bus_io_or_char_visible(bus) && (port & 0x04) != 0;
}

static bool c64_bus_char_visible(const c64_bus_t *bus) {
    uint8_t port = c64_bus_cpu_port_value(bus);
    return c64_bus_io_or_char_visible(bus) && (port & 0x04) == 0;
}

static c64_cartridge_mode c64_bus_cartridge_mode_from_lines(uint8_t exrom, uint8_t game) {
    if (exrom == 0 && game != 0) {
        return C64_CARTRIDGE_MODE_8K;
    }
    if (exrom == 0 && game == 0) {
        return C64_CARTRIDGE_MODE_16K;
    }
    if (exrom != 0 && game == 0) {
        return C64_CARTRIDGE_MODE_ULTIMAX;
    }
    return C64_CARTRIDGE_MODE_NONE;
}

static uint8_t c64_io_read(c64_bus_t *bus, uint16_t address) {
    if (address >= 0xd000 && address <= 0xd3ff && bus->vic) {
        return vicii_read_register(bus->vic, address);
    }

    if (address >= C64_SID_BASE && address <= C64_SID_END && bus->sid) {
        return sid_read(bus->sid, address);
    }

    if (address >= C64_COLOR_RAM_BASE && address <= C64_COLOR_RAM_END) {
        return (uint8_t)(bus->color_ram[address - C64_COLOR_RAM_BASE] & 0x0f);
    }

    if (address >= C64_CIA1_BASE && address <= C64_CIA1_END && bus->cia1) {
        return cia_read_register(bus->cia1, address);
    }

    if (address >= C64_CIA2_BASE && address <= C64_CIA2_END && bus->cia2) {
        return cia_read_register(bus->cia2, address);
    }

    return 0xff;
}

static void c64_io_write(c64_bus_t *bus, uint16_t address, uint8_t value) {
    /* Debugcart (VICE testbench): write to $D7FF records exit code ($00 pass, $FF fail). */
    if (address == 0xd7ffu && bus->debugcart_enabled) {
        bus->debugcart_hit = true;
        bus->debugcart_value = value;
        return;
    }

    if (address >= 0xd000 && address <= 0xd3ff && bus->vic) {
        vicii_write_register(bus->vic, address, value);
        bus->vic_register_writes++;
        return;
    }

    if (address >= C64_SID_BASE && address <= C64_SID_END && bus->sid) {
        sid_write(bus->sid, address, value);
        bus->sid_register_writes++;
        return;
    }

    if (address >= C64_COLOR_RAM_BASE && address <= C64_COLOR_RAM_END) {
        bus->color_ram[address - C64_COLOR_RAM_BASE] = (uint8_t)(value & 0x0f);
        bus->color_ram_writes++;
        return;
    }

    if (address >= C64_CIA1_BASE && address <= C64_CIA1_END && bus->cia1) {
        cia_write_register(bus->cia1, address, value);
        bus->cia1_register_writes++;
        return;
    }

    if (address >= C64_CIA2_BASE && address <= C64_CIA2_END && bus->cia2) {
        uint8_t reg = (uint8_t)(address & 0x0fu);
        cia_write_register(bus->cia2, address, value);
        if (reg == C64_CIA_REG_PORT_A || reg == C64_CIA_REG_DDRA) {
            c64_bus_refresh_vic_bank_base(bus);
        }
        bus->cia2_register_writes++;
    }
}

void c64_bus_init(c64_bus_t *bus) {
    assert(bus);

    memset(bus, 0, sizeof(*bus));
    c64_bus_reset(bus);
}

void c64_bus_reset(c64_bus_t *bus) {
    vicii *vic;
    cia *cia1;
    cia *cia2;
    sid *s;

    assert(bus);

    vic  = bus->vic;
    cia1 = bus->cia1;
    cia2 = bus->cia2;
    s    = bus->sid;
    memset(bus->ram, 0, sizeof(bus->ram));
    memset(bus->color_ram, 0, sizeof(bus->color_ram));
    bus->vic  = vic;
    bus->cia1 = cia1;
    bus->cia2 = cia2;
    bus->sid  = s;
    bus->cpu_port_direction = 0x2f;
    bus->cpu_port_data = 0x37;
    bus->screen_ram_writes = 0;
    bus->color_ram_writes = 0;
    bus->vic_register_writes = 0;
    bus->cia1_register_writes = 0;
    bus->cia2_register_writes = 0;
    bus->sid_register_writes = 0;
    c64_bus_refresh_vic_bank_base(bus);
}

void c64_bus_attach_vicii(c64_bus_t *bus, vicii *v) {
    assert(bus);

    bus->vic = v;
}

void c64_bus_attach_cias(c64_bus_t *bus, cia *cia1, cia *cia2) {
    assert(bus);

    bus->cia1 = cia1;
    bus->cia2 = cia2;
    c64_bus_refresh_vic_bank_base(bus);
}

void c64_bus_attach_sid(c64_bus_t *bus, sid *s) {
    assert(bus);

    bus->sid = s;
}

void c64_bus_refresh_vic_bank_base(c64_bus_t *bus) {
    assert(bus);

    bus->vic_bank_base = c64_bus_compute_vic_bank_base(bus);
}

uint8_t c64_bus_read(c64_bus_t *bus, uint16_t address) {
    uint8_t cartridge_value;

    assert(bus);

    if (address <= C64_CPU_PORT_DATA) {
        return c64_bus_cpu_port_read(bus, address);
    }

    if (c64_bus_cartridge_read(bus, address, &cartridge_value)) {
        return cartridge_value;
    }

    if (address >= 0xa000 && address <= 0xbfff && c64_bus_basic_visible(bus)) {
        return bus->basic_rom[address - 0xa000];
    }

    if (address >= 0xd000 && address <= 0xdfff) {
        if (c64_bus_io_visible(bus)) {
            return c64_io_read(bus, address);
        }

        if (c64_bus_char_visible(bus)) {
            return bus->char_rom[address - 0xd000];
        }
    }

    if (address >= 0xe000 && c64_bus_kernal_visible(bus)) {
        return bus->kernal_rom[address - 0xe000];
    }

    return bus->ram[address];
}

void c64_bus_write(c64_bus_t *bus, uint16_t address, uint8_t value) {
    assert(bus);

    if (address == C64_CPU_PORT_DIRECTION) {
        bus->cpu_port_direction = value;
        return;
    }

    if (address == C64_CPU_PORT_DATA) {
        bus->cpu_port_data = value;
        return;
    }

    if (address >= 0xd000 && address <= 0xdfff && c64_bus_io_visible(bus)) {
        c64_io_write(bus, address, value);
        return;
    }

    bus->ram[address] = value;
    if (address >= C64_DEFAULT_SCREEN_BASE && address < C64_DEFAULT_SCREEN_BASE + 1000u) {
        bus->screen_ram_writes++;
    }
}

bool c64_bus_set_basic_rom(c64_bus_t *bus, const uint8_t *data, size_t size) {
    assert(bus);
    if (!data || size != sizeof(bus->basic_rom)) {
        return false;
    }

    memcpy(bus->basic_rom, data, sizeof(bus->basic_rom));
    return true;
}

bool c64_bus_set_char_rom(c64_bus_t *bus, const uint8_t *data, size_t size) {
    assert(bus);
    if (!data || size != sizeof(bus->char_rom)) {
        return false;
    }

    memcpy(bus->char_rom, data, sizeof(bus->char_rom));
    return true;
}

bool c64_bus_set_kernal_rom(c64_bus_t *bus, const uint8_t *data, size_t size) {
    assert(bus);
    if (!data || size != sizeof(bus->kernal_rom)) {
        return false;
    }

    memcpy(bus->kernal_rom, data, sizeof(bus->kernal_rom));
    return true;
}

bool c64_bus_set_system_rom(c64_bus_t *bus, const uint8_t *data, size_t size) {
    assert(bus);
    if (!data || size != sizeof(bus->basic_rom) + sizeof(bus->kernal_rom)) {
        return false;
    }

    memcpy(bus->basic_rom, data, sizeof(bus->basic_rom));
    memcpy(bus->kernal_rom, data + sizeof(bus->basic_rom), sizeof(bus->kernal_rom));
    return true;
}

bool c64_bus_attach_generic_cartridge(
    c64_bus_t *bus,
    const uint8_t *roml,
    size_t roml_size,
    const uint8_t *romh,
    size_t romh_size,
    uint8_t exrom,
    uint8_t game)
{
    c64_cartridge_mode mode;

    assert(bus);

    mode = c64_bus_cartridge_mode_from_lines(exrom, game);
    if (mode == C64_CARTRIDGE_MODE_NONE || mode == C64_CARTRIDGE_MODE_ULTIMAX) {
        return false;
    }
    if (roml == NULL || roml_size != C64_CARTRIDGE_ROM_BANK_SIZE) {
        return false;
    }
    if (mode == C64_CARTRIDGE_MODE_16K &&
        (romh == NULL || romh_size != C64_CARTRIDGE_ROM_BANK_SIZE)) {
        return false;
    }
    if (mode == C64_CARTRIDGE_MODE_8K && romh_size != 0) {
        return false;
    }

    memcpy(bus->cartridge_roml, roml, C64_CARTRIDGE_ROM_BANK_SIZE);
    bus->cartridge_roml_present = true;
    if (mode == C64_CARTRIDGE_MODE_16K) {
        memcpy(bus->cartridge_romh, romh, C64_CARTRIDGE_ROM_BANK_SIZE);
        bus->cartridge_romh_present = true;
    } else {
        memset(bus->cartridge_romh, 0, sizeof(bus->cartridge_romh));
        bus->cartridge_romh_present = false;
    }
    bus->cartridge_exrom = exrom;
    bus->cartridge_game = game;
    bus->cartridge_mode = mode;
    bus->cartridge_mounted = true;
    return true;
}

void c64_bus_detach_cartridge(c64_bus_t *bus) {
    assert(bus);

    memset(bus->cartridge_roml, 0, sizeof(bus->cartridge_roml));
    memset(bus->cartridge_romh, 0, sizeof(bus->cartridge_romh));
    bus->cartridge_mounted = false;
    bus->cartridge_roml_present = false;
    bus->cartridge_romh_present = false;
    bus->cartridge_exrom = 1;
    bus->cartridge_game = 1;
    bus->cartridge_mode = C64_CARTRIDGE_MODE_NONE;
}

bool c64_bus_cartridge_read(const c64_bus_t *bus, uint16_t address, uint8_t *out_value) {
    assert(bus);

    if (out_value == NULL || !bus->cartridge_mounted) {
        return false;
    }

    if (address >= 0x8000u && address <= 0x9fffu &&
        bus->cartridge_roml_present &&
        (bus->cartridge_mode == C64_CARTRIDGE_MODE_8K ||
         bus->cartridge_mode == C64_CARTRIDGE_MODE_16K ||
         bus->cartridge_mode == C64_CARTRIDGE_MODE_ULTIMAX)) {
        *out_value = bus->cartridge_roml[address - 0x8000u];
        return true;
    }

    if (address >= 0xa000u && address <= 0xbfffu &&
        bus->cartridge_romh_present &&
        bus->cartridge_mode == C64_CARTRIDGE_MODE_16K) {
        *out_value = bus->cartridge_romh[address - 0xa000u];
        return true;
    }

    if (address >= 0xe000u &&
        bus->cartridge_romh_present &&
        bus->cartridge_mode == C64_CARTRIDGE_MODE_ULTIMAX) {
        *out_value = bus->cartridge_romh[address - 0xe000u];
        return true;
    }

    return false;
}
