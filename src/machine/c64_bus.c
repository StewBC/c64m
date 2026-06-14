#include "c64_bus.h"

#include <assert.h>
#include <string.h>

enum {
    C64_CPU_PORT_DIRECTION = 0x0000,
    C64_CPU_PORT_DATA = 0x0001,
};

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

static uint8_t c64_io_read(c64_bus_t *bus, uint16_t address) {
    (void)bus;
    (void)address;
    return 0xff;
}

static void c64_io_write(c64_bus_t *bus, uint16_t address, uint8_t value) {
    (void)bus;
    (void)address;
    (void)value;
}

void c64_bus_init(c64_bus_t *bus) {
    assert(bus);

    memset(bus, 0, sizeof(*bus));
    c64_bus_reset(bus);
}

void c64_bus_reset(c64_bus_t *bus) {
    assert(bus);

    memset(bus->ram, 0, sizeof(bus->ram));
    bus->cpu_port_direction = 0x2f;
    bus->cpu_port_data = 0x37;
}

uint8_t c64_bus_read(c64_bus_t *bus, uint16_t address) {
    assert(bus);

    if (address <= C64_CPU_PORT_DATA) {
        return c64_bus_cpu_port_read(bus, address);
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

    bus->ram[address] = value;

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
