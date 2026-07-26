#include "c64_bus.h"

#include "cia.h"
#include "sid.h"
#include "vicii.h"

#include <assert.h>
#include <stdlib.h>
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

/* PLA LORAM / HIRAM ($01 bits 0 and 1). For 8K and 16K game configs, VICE's
   c64meminit table maps ROML only when both LORAM and HIRAM are set; 16K ROMH
   needs HIRAM alone. $01=$25 (LORAM on, HIRAM off) therefore shows RAM underlay
   at $8000/$A000 — required by Ocean loaders (e.g. Pang IRQ JSR $A000). */
static bool c64_bus_loram_enabled(const c64_bus_t *bus) {
    return (bus->cpu_port_data & 0x01u) != 0;
}

static bool c64_bus_hiram_enabled(const c64_bus_t *bus) {
    return (bus->cpu_port_data & 0x02u) != 0;
}

/* VICE magicdesk.c: bankmask from highest bank index present in the image. */
static uint8_t c64_bus_magic_desk_bank_mask(size_t last_bank_index) {
    if (last_bank_index >= 64u) {
        return 0x7fu;
    }
    if (last_bank_index >= 32u) {
        return 0x3fu;
    }
    if (last_bank_index >= 16u) {
        return 0x1fu;
    }
    if (last_bank_index >= 8u) {
        return 0x0fu;
    }
    if (last_bank_index >= 4u) {
        return 0x07u;
    }
    return 0x03u;
}

static void c64_bus_magic_desk_apply_latch(c64_bus_t *bus) {
    uint8_t bank;
    size_t offset;

    assert(bus);
    assert(bus->cartridge_hardware_type == C64_CARTRIDGE_HW_MAGIC_DESK);

    bus->cartridge_io_latch =
        (uint8_t)(bus->cartridge_io_latch & (uint8_t)(0x80u | bus->cartridge_bank_mask));
    bank = (uint8_t)(bus->cartridge_io_latch & bus->cartridge_bank_mask);

    if (bus->cartridge_io_latch & 0x80u) {
        /* Bit 7 set: cart ROM disabled; $8000 is RAM. */
        bus->cartridge_mode = C64_CARTRIDGE_MODE_NONE;
        bus->cartridge_exrom = 1;
        bus->cartridge_game = 1;
    } else {
        bus->cartridge_exrom = 0;
        bus->cartridge_game = 1;
        bus->cartridge_mode = C64_CARTRIDGE_MODE_8K;
    }

    if (bus->cartridge_rom_banks != NULL && bus->cartridge_bank_count > 0) {
        if (bank >= bus->cartridge_bank_count) {
            bank = (uint8_t)(bank % (uint8_t)bus->cartridge_bank_count);
        }
        offset = (size_t)bank * C64_CARTRIDGE_ROM_BANK_SIZE;
        memcpy(
            bus->cartridge_roml,
            bus->cartridge_rom_banks + offset,
            C64_CARTRIDGE_ROM_BANK_SIZE);
        bus->cartridge_roml_present = true;
    }
}

/* VICE ocean.c: bank = value & io1_mask & 0x3f; bit 7 has no effect.
   512K (Terminator 2): 8K game. All other sizes: 16K game with the same bank
   mirrored to ROML and ROMH (CRT GAME line is ignored — see ocean_config_init). */
static void c64_bus_ocean_apply_latch(c64_bus_t *bus) {
    uint8_t bank;
    size_t offset;
    const uint8_t *src;
    c64_cartridge_mode mode;

    assert(bus);
    assert(bus->cartridge_hardware_type == C64_CARTRIDGE_HW_OCEAN);

    bank = (uint8_t)(bus->cartridge_io_latch & bus->cartridge_bank_mask & 0x3fu);
    if (bus->cartridge_rom_banks == NULL || bus->cartridge_bank_count == 0) {
        return;
    }
    if (bank >= bus->cartridge_bank_count) {
        bank = (uint8_t)(bank % (uint8_t)bus->cartridge_bank_count);
    }
    offset = (size_t)bank * C64_CARTRIDGE_ROM_BANK_SIZE;
    src = bus->cartridge_rom_banks + offset;
    memcpy(bus->cartridge_roml, src, C64_CARTRIDGE_ROM_BANK_SIZE);
    bus->cartridge_roml_present = true;

    mode = bus->cartridge_mode;
    if (mode == C64_CARTRIDGE_MODE_16K) {
        memcpy(bus->cartridge_romh, src, C64_CARTRIDGE_ROM_BANK_SIZE);
        bus->cartridge_romh_present = true;
        bus->cartridge_exrom = 0;
        bus->cartridge_game = 0;
    } else {
        memset(bus->cartridge_romh, 0, sizeof(bus->cartridge_romh));
        bus->cartridge_romh_present = false;
        bus->cartridge_exrom = 0;
        bus->cartridge_game = 1;
        bus->cartridge_mode = C64_CARTRIDGE_MODE_8K;
    }
}

/* Copy one 8 KiB ROML bank into the active window (banks wrap on overflow). */
static void c64_bus_copy_roml_bank(c64_bus_t *bus, uint8_t bank) {
    size_t offset;

    if (bus->cartridge_rom_banks == NULL || bus->cartridge_bank_count == 0) {
        return;
    }
    if (bank >= bus->cartridge_bank_count) {
        bank = (uint8_t)(bank % (uint8_t)bus->cartridge_bank_count);
    }
    offset = (size_t)bank * C64_CARTRIDGE_ROM_BANK_SIZE;
    memcpy(
        bus->cartridge_roml,
        bus->cartridge_rom_banks + offset,
        C64_CARTRIDGE_ROM_BANK_SIZE);
    bus->cartridge_roml_present = true;
}

/* Single-bank 8K-game mappers (C64GS, Dinamic): EXROM active, GAME inactive. */
static void c64_bus_apply_banked_8k_rom(c64_bus_t *bus, uint8_t bank) {
    bus->cartridge_exrom = 0;
    bus->cartridge_game = 1;
    bus->cartridge_mode = C64_CARTRIDGE_MODE_8K;
    c64_bus_copy_roml_bank(bus, bank);
}

/* VICE gs.c: bank = accessed IO1 address & $3f (latched on read or write). */
static void c64_bus_c64gs_apply_latch(c64_bus_t *bus) {
    assert(bus->cartridge_hardware_type == C64_CARTRIDGE_HW_C64GS);
    c64_bus_apply_banked_8k_rom(bus, (uint8_t)(bus->cartridge_io_latch & 0x3fu));
}

/* VICE dinamic.c: bank = accessed IO1 address & $0f (latched on read of $de0x). */
static void c64_bus_dinamic_apply_latch(c64_bus_t *bus) {
    assert(bus->cartridge_hardware_type == C64_CARTRIDGE_HW_DINAMIC);
    c64_bus_apply_banked_8k_rom(bus, (uint8_t)(bus->cartridge_io_latch & 0x0fu));
}

/* VICE funplay.c: bank = ((v>>3)&7)|((v&1)<<3); $86-masked value turns ROM off.
   The register value is stored in io_latch; the bank always switches, only the
   EXROM/GAME lines depend on the mode bits (unknown values leave lines as-is). */
static void c64_bus_funplay_apply_latch(c64_bus_t *bus) {
    uint8_t value;
    uint8_t bank;

    assert(bus->cartridge_hardware_type == C64_CARTRIDGE_HW_FUNPLAY);
    value = bus->cartridge_io_latch;
    bank = (uint8_t)(((value >> 3) & 7u) | ((value & 1u) << 3));
    c64_bus_copy_roml_bank(bus, bank);

    if ((value & 0xc6u) == 0x00u) {
        bus->cartridge_exrom = 0;
        bus->cartridge_game = 1;
        bus->cartridge_mode = C64_CARTRIDGE_MODE_8K;
    } else if ((value & 0xc6u) == 0x86u) {
        bus->cartridge_exrom = 1;
        bus->cartridge_game = 1;
        bus->cartridge_mode = C64_CARTRIDGE_MODE_NONE;
    }
    /* Other values: VICE warns and leaves the port config unchanged. */
}

/* VICE supergames.c: bank=v&3; bit2=0 -> 16K game (EXROM+GAME active), bit2=1 ->
   cart disabled. Storage is 2 interleaved 8 KiB slots per bank: [ROML,ROMH]. */
static void c64_bus_super_games_apply_latch(c64_bus_t *bus) {
    uint8_t value;
    uint8_t bank;
    bool enabled;
    size_t roml_slot;
    size_t romh_slot;

    assert(bus->cartridge_hardware_type == C64_CARTRIDGE_HW_SUPER_GAMES);
    value = bus->cartridge_io_latch;
    bank = (uint8_t)(value & 0x03u);
    enabled = ((value >> 2) & 1u) == 0u;
    roml_slot = (size_t)bank * 2u;
    romh_slot = roml_slot + 1u;

    if (bus->cartridge_rom_banks != NULL &&
        (size_t)bus->cartridge_bank_count > romh_slot) {
        memcpy(
            bus->cartridge_roml,
            bus->cartridge_rom_banks + roml_slot * C64_CARTRIDGE_ROM_BANK_SIZE,
            C64_CARTRIDGE_ROM_BANK_SIZE);
        memcpy(
            bus->cartridge_romh,
            bus->cartridge_rom_banks + romh_slot * C64_CARTRIDGE_ROM_BANK_SIZE,
            C64_CARTRIDGE_ROM_BANK_SIZE);
        bus->cartridge_roml_present = true;
        bus->cartridge_romh_present = true;
    }

    if (enabled) {
        bus->cartridge_exrom = 0;
        bus->cartridge_game = 0;
        bus->cartridge_mode = C64_CARTRIDGE_MODE_16K;
    } else {
        bus->cartridge_exrom = 1;
        bus->cartridge_game = 1;
        bus->cartridge_mode = C64_CARTRIDGE_MODE_NONE;
    }
}

static void c64_bus_free_cartridge_banks(c64_bus_t *bus) {
    free(bus->cartridge_rom_banks);
    bus->cartridge_rom_banks = NULL;
    bus->cartridge_bank_count = 0;
    bus->cartridge_bank_mask = 0;
    bus->cartridge_io_latch = 0;
    bus->cartridge_hardware_type = C64_CARTRIDGE_HW_NORMAL;
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

    /* IO1 read side-effects: C64GS and Dinamic latch their bank from the
       accessed address on a read (VICE gs.c / dinamic.c). Read value is 0. */
    if (address >= 0xde00u && address <= 0xdeffu && bus->cartridge_mounted) {
        if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_C64GS) {
            bus->cartridge_io_latch = (uint8_t)(address & 0x3fu);
            c64_bus_c64gs_apply_latch(bus);
            return 0;
        }
        if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_DINAMIC) {
            uint8_t offset = (uint8_t)(address & 0xffu);
            if (offset <= 0x0fu) {
                bus->cartridge_io_latch = offset;
                c64_bus_dinamic_apply_latch(bus);
            }
            return 0;
        }
    }

    /* Magic Desk IO1 is write-only (VICE: read never valid). */
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
        return;
    }

    /* Bank registers at IO1 $DE00–$DEFF. C64GS latches the address (bits 0-5);
       Magic Desk / Ocean / Fun Play latch the written value. Dinamic ignores
       writes (its bank switches on reads only). */
    if (address >= 0xde00u && address <= 0xdeffu && bus->cartridge_mounted) {
        if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_MAGIC_DESK) {
            bus->cartridge_io_latch = value;
            c64_bus_magic_desk_apply_latch(bus);
        } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_OCEAN) {
            bus->cartridge_io_latch = value;
            c64_bus_ocean_apply_latch(bus);
        } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_C64GS) {
            bus->cartridge_io_latch = (uint8_t)(address & 0x3fu);
            c64_bus_c64gs_apply_latch(bus);
        } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_FUNPLAY) {
            bus->cartridge_io_latch = value;
            c64_bus_funplay_apply_latch(bus);
        }
        return;
    }

    /* Super Games: control register at IO2 $DF00–$DFFF. Bit 3 write-protects the
       register until the next hardware reset. */
    if (address >= 0xdf00u && address <= 0xdfffu && bus->cartridge_mounted) {
        if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_SUPER_GAMES &&
            (bus->cartridge_io_latch & 0x08u) == 0u) {
            bus->cartridge_io_latch = value;
            c64_bus_super_games_apply_latch(bus);
        }
        return;
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
    /* Cart ROM survives machine reset; Magic Desk re-selects bank 0 / enabled. */
    c64_bus_cartridge_reset(bus);
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
    uint8_t *banks;

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

    banks = (uint8_t *)malloc(C64_CARTRIDGE_ROM_BANK_SIZE);
    if (banks == NULL) {
        return false;
    }
    memcpy(banks, roml, C64_CARTRIDGE_ROM_BANK_SIZE);

    c64_bus_detach_cartridge(bus);

    bus->cartridge_rom_banks = banks;
    bus->cartridge_bank_count = 1;
    bus->cartridge_bank_mask = 0;
    bus->cartridge_io_latch = 0;
    bus->cartridge_hardware_type = C64_CARTRIDGE_HW_NORMAL;
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

bool c64_bus_attach_magic_desk_cartridge(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    uint8_t exrom,
    uint8_t game)
{
    uint8_t *storage;
    size_t bytes;
    size_t last_bank;

    assert(bus);

    if (banks == NULL || bank_count == 0 || bank_count > C64_CARTRIDGE_MAX_BANKS) {
        return false;
    }
    /* Magic Desk is 8K game: EXROM low, GAME high in the CRT header. */
    if (exrom != 0 || game == 0) {
        return false;
    }

    bytes = bank_count * C64_CARTRIDGE_ROM_BANK_SIZE;
    storage = (uint8_t *)malloc(bytes);
    if (storage == NULL) {
        return false;
    }
    memcpy(storage, banks, bytes);

    c64_bus_detach_cartridge(bus);

    last_bank = bank_count - 1u;
    bus->cartridge_rom_banks = storage;
    bus->cartridge_bank_count = (uint16_t)bank_count;
    bus->cartridge_bank_mask = c64_bus_magic_desk_bank_mask(last_bank);
    bus->cartridge_hardware_type = C64_CARTRIDGE_HW_MAGIC_DESK;
    bus->cartridge_romh_present = false;
    memset(bus->cartridge_romh, 0, sizeof(bus->cartridge_romh));
    bus->cartridge_mounted = true;
    /* Power-on: bank 0, cart enabled (VICE magicdesk_config_init). */
    bus->cartridge_io_latch = 0;
    c64_bus_magic_desk_apply_latch(bus);
    return true;
}

bool c64_bus_attach_ocean_cartridge(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    uint8_t exrom,
    uint8_t game)
{
    uint8_t *storage;
    size_t bytes;
    size_t rom_size;
    bool mode_16k;

    assert(bus);

    if (banks == NULL || bank_count == 0 || bank_count > C64_CARTRIDGE_OCEAN_MAX_BANKS) {
        return false;
    }
    if (exrom != 0) {
        return false;
    }

    bytes = bank_count * C64_CARTRIDGE_ROM_BANK_SIZE;
    storage = (uint8_t *)malloc(bytes);
    if (storage == NULL) {
        return false;
    }
    memcpy(storage, banks, bytes);

    c64_bus_detach_cartridge(bus);

    rom_size = bytes;
    /*
     * Mode selection (VICE ocean_config_init / ocean.c comments):
     * - 512 KiB (Terminator 2): 8K game.
     * - Otherwise always 16K game with the same bank at ROML and ROMH.
     * CRT header GAME is ignored for Ocean type 1 (Pang dumps often say GAME=1).
     */
    (void)game;
    mode_16k = (rom_size != 0x80000u);

    bus->cartridge_rom_banks = storage;
    bus->cartridge_bank_count = (uint16_t)bank_count;
    /* VICE: io1_mask = (rom_size >> 13) - 1. */
    bus->cartridge_bank_mask = (uint8_t)((rom_size >> 13) - 1u);
    bus->cartridge_hardware_type = C64_CARTRIDGE_HW_OCEAN;
    bus->cartridge_mode = mode_16k ? C64_CARTRIDGE_MODE_16K : C64_CARTRIDGE_MODE_8K;
    bus->cartridge_mounted = true;
    bus->cartridge_io_latch = 0;
    c64_bus_ocean_apply_latch(bus);
    return true;
}

/* Shared setup for the linear 8K-bank ROML mappers (C64GS, Fun Play, Dinamic).
   The mapper drives its own 8K game config, so header EXROM/GAME are ignored. */
static bool c64_bus_attach_banked_8k(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    size_t max_banks,
    uint16_t hardware_type,
    uint8_t bank_mask)
{
    uint8_t *storage;
    size_t bytes;

    assert(bus);

    if (banks == NULL || bank_count == 0 || bank_count > max_banks) {
        return false;
    }

    bytes = bank_count * C64_CARTRIDGE_ROM_BANK_SIZE;
    storage = (uint8_t *)malloc(bytes);
    if (storage == NULL) {
        return false;
    }
    memcpy(storage, banks, bytes);

    c64_bus_detach_cartridge(bus);

    bus->cartridge_rom_banks = storage;
    bus->cartridge_bank_count = (uint16_t)bank_count;
    bus->cartridge_bank_mask = bank_mask;
    bus->cartridge_hardware_type = hardware_type;
    bus->cartridge_romh_present = false;
    memset(bus->cartridge_romh, 0, sizeof(bus->cartridge_romh));
    bus->cartridge_mounted = true;
    bus->cartridge_io_latch = 0;
    return true;
}

bool c64_bus_attach_c64gs_cartridge(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    uint8_t exrom,
    uint8_t game)
{
    (void)exrom;
    (void)game;
    if (!c64_bus_attach_banked_8k(
            bus, banks, bank_count, C64_CARTRIDGE_OCEAN_MAX_BANKS,
            C64_CARTRIDGE_HW_C64GS, 0x3fu)) {
        return false;
    }
    c64_bus_c64gs_apply_latch(bus);
    return true;
}

bool c64_bus_attach_funplay_cartridge(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    uint8_t exrom,
    uint8_t game)
{
    (void)exrom;
    (void)game;
    if (!c64_bus_attach_banked_8k(
            bus, banks, bank_count, 16u,
            C64_CARTRIDGE_HW_FUNPLAY, 0x0fu)) {
        return false;
    }
    c64_bus_funplay_apply_latch(bus);
    return true;
}

bool c64_bus_attach_dinamic_cartridge(
    c64_bus_t *bus,
    const uint8_t *banks,
    size_t bank_count,
    uint8_t exrom,
    uint8_t game)
{
    (void)exrom;
    (void)game;
    if (!c64_bus_attach_banked_8k(
            bus, banks, bank_count, 16u,
            C64_CARTRIDGE_HW_DINAMIC, 0x0fu)) {
        return false;
    }
    c64_bus_dinamic_apply_latch(bus);
    return true;
}

bool c64_bus_attach_super_games_cartridge(
    c64_bus_t *bus,
    const uint8_t *slots,
    size_t slot_count,
    uint8_t exrom,
    uint8_t game)
{
    uint8_t *storage;
    size_t bytes;

    assert(bus);

    (void)exrom;
    (void)game;
    /* slot_count = 2 × logical 16K banks (max 4): [ROML,ROMH] per bank. */
    if (slots == NULL || slot_count == 0 || slot_count > 8u ||
        (slot_count & 1u) != 0u) {
        return false;
    }

    bytes = slot_count * C64_CARTRIDGE_ROM_BANK_SIZE;
    storage = (uint8_t *)malloc(bytes);
    if (storage == NULL) {
        return false;
    }
    memcpy(storage, slots, bytes);

    c64_bus_detach_cartridge(bus);

    bus->cartridge_rom_banks = storage;
    bus->cartridge_bank_count = (uint16_t)slot_count;
    bus->cartridge_bank_mask = 0x03u;
    bus->cartridge_hardware_type = C64_CARTRIDGE_HW_SUPER_GAMES;
    bus->cartridge_mounted = true;
    bus->cartridge_io_latch = 0;
    c64_bus_super_games_apply_latch(bus);
    return true;
}

void c64_bus_detach_cartridge(c64_bus_t *bus) {
    assert(bus);

    c64_bus_free_cartridge_banks(bus);
    memset(bus->cartridge_roml, 0, sizeof(bus->cartridge_roml));
    memset(bus->cartridge_romh, 0, sizeof(bus->cartridge_romh));
    bus->cartridge_mounted = false;
    bus->cartridge_roml_present = false;
    bus->cartridge_romh_present = false;
    bus->cartridge_exrom = 1;
    bus->cartridge_game = 1;
    bus->cartridge_mode = C64_CARTRIDGE_MODE_NONE;
}

void c64_bus_cartridge_reset(c64_bus_t *bus) {
    assert(bus);

    if (!bus->cartridge_mounted) {
        return;
    }
    if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_MAGIC_DESK) {
        bus->cartridge_io_latch = 0;
        c64_bus_magic_desk_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_OCEAN) {
        bus->cartridge_io_latch = 0;
        c64_bus_ocean_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_C64GS) {
        bus->cartridge_io_latch = 0;
        c64_bus_c64gs_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_FUNPLAY) {
        bus->cartridge_io_latch = 0;
        c64_bus_funplay_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_DINAMIC) {
        bus->cartridge_io_latch = 0;
        c64_bus_dinamic_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_SUPER_GAMES) {
        /* Clears the write-protect latch (bit 3) on hardware reset. */
        bus->cartridge_io_latch = 0;
        c64_bus_super_games_apply_latch(bus);
    }
}

void c64_bus_cartridge_apply_banking(c64_bus_t *bus) {
    assert(bus);

    if (!bus->cartridge_mounted) {
        return;
    }
    if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_MAGIC_DESK) {
        c64_bus_magic_desk_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_OCEAN) {
        c64_bus_ocean_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_C64GS) {
        c64_bus_c64gs_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_FUNPLAY) {
        c64_bus_funplay_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_DINAMIC) {
        c64_bus_dinamic_apply_latch(bus);
    } else if (bus->cartridge_hardware_type == C64_CARTRIDGE_HW_SUPER_GAMES) {
        c64_bus_super_games_apply_latch(bus);
    } else if (
        bus->cartridge_rom_banks != NULL &&
        bus->cartridge_bank_count > 0 &&
        bus->cartridge_roml_present) {
        memcpy(
            bus->cartridge_roml,
            bus->cartridge_rom_banks,
            C64_CARTRIDGE_ROM_BANK_SIZE);
    }
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
        /* 8K/16K game: ROML only when LORAM and HIRAM are both set (VICE PLA). */
        if (bus->cartridge_mode != C64_CARTRIDGE_MODE_ULTIMAX &&
            !(c64_bus_loram_enabled(bus) && c64_bus_hiram_enabled(bus))) {
            return false;
        }
        *out_value = bus->cartridge_roml[address - 0x8000u];
        return true;
    }

    if (address >= 0xa000u && address <= 0xbfffu &&
        bus->cartridge_romh_present &&
        bus->cartridge_mode == C64_CARTRIDGE_MODE_16K) {
        /* 16K ROMH needs HIRAM; with HIRAM clear, $A000 is underlay RAM. */
        if (!c64_bus_hiram_enabled(bus)) {
            return false;
        }
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
