#include "c64.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    C64_VICII_REG_MEMORY_POINTER = 0x18,
};

static void c64_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static uint8_t c64_cpu_read(void *user, uint16_t address) {
    c64_t *machine = user;
    uint8_t value = c64_bus_read(&machine->bus, address);

    if (machine->memory_access) {
        machine->memory_access(machine->memory_access_user, C64_MEMORY_ACCESS_READ, address, value);
    }

    return value;
}

static void c64_cpu_write(void *user, uint16_t address, uint8_t value) {
    c64_t *machine = user;
    c64_bus_write(&machine->bus, address, value);
    if (machine->memory_access) {
        machine->memory_access(machine->memory_access_user, C64_MEMORY_ACCESS_WRITE, address, value);
    }
}

static uint8_t c64_cpu_irq_pending(void *user) {
    c64_t *machine = user;
    return cia_irq_pending(&machine->cia1) ? 1u : 0u;
}

static uint8_t c64_cpu_nmi_pending(void *user) {
    c64_t *machine = user;
    bool pending = machine->restore_pending;
    machine->restore_pending = false;
    return pending ? 1u : 0u;
}

static void c64_step_vic(c64_t *machine) {
    vicii_step_cycle(&machine->vic);
    machine->clock.vic_cycles++;
}

static void c64_step_cia1(c64_t *machine) {
    cia_step_cycle(&machine->cia1);
}

static void c64_step_cia2(c64_t *machine) {
    cia_step_cycle(&machine->cia2);
    machine->clock.cia_cycles++;
}

static void c64_step_sid(c64_t *machine) {
    (void)machine;
}

void c64_init(c64_t *machine) {
    char error[256];

    assert(machine);

    memset(machine, 0, sizeof(*machine));
    c64_bus_init(&machine->bus);
    (void)vicii_init(&machine->vic, error, sizeof(error));
    (void)cia_init(&machine->cia1, error, sizeof(error));
    (void)cia_init(&machine->cia2, error, sizeof(error));
    c64_keyboard_reset(&machine->keyboard);
    cia_attach_keyboard(&machine->cia1, &machine->keyboard);
    c64_bus_attach_vicii(&machine->bus, &machine->vic);
    c64_bus_attach_cias(&machine->bus, &machine->cia1, &machine->cia2);
    c6510_init(&machine->cpu, machine, c64_cpu_read, c64_cpu_write);
    c6510_set_irq_pending_callback(&machine->cpu, c64_cpu_irq_pending);
    c6510_set_nmi_pending_callback(&machine->cpu, c64_cpu_nmi_pending);
}

bool c64_install_roms(c64_t *machine, const c64_rom_set *roms, char *error, size_t error_size) {
    assert(machine);
    assert(roms);

    if (!roms->has_basic) {
        c64_set_error(error, error_size, "BASIC ROM missing");
        machine->ready = false;
        return false;
    }

    if (!roms->has_kernal) {
        c64_set_error(error, error_size, "KERNAL ROM missing");
        machine->ready = false;
        return false;
    }

    if (!roms->has_character) {
        c64_set_error(error, error_size, "character ROM missing");
        machine->ready = false;
        return false;
    }

    if (!c64_bus_set_basic_rom(&machine->bus, roms->basic, sizeof(roms->basic)) ||
        !c64_bus_set_kernal_rom(&machine->bus, roms->kernal, sizeof(roms->kernal)) ||
        !c64_bus_set_char_rom(&machine->bus, roms->character, sizeof(roms->character))) {
        c64_set_error(error, error_size, "failed to install ROM data");
        machine->ready = false;
        return false;
    }

    machine->has_basic_rom = true;
    machine->has_kernal_rom = true;
    machine->has_character_rom = true;
    machine->ready = true;
    c64_set_error(error, error_size, "");
    return true;
}

bool c64_reset(c64_t *machine, char *error, size_t error_size) {
    uint16_t vector;

    assert(machine);

    if (!machine->ready ||
        !machine->has_basic_rom ||
        !machine->has_kernal_rom ||
        !machine->has_character_rom) {
        c64_set_error(error, error_size, "machine cannot reset without BASIC, KERNAL, and character ROMs");
        return false;
    }

    c64_bus_reset(&machine->bus);
    vicii_reset(&machine->vic);
    vicii_write_register(&machine->vic, C64_VICII_REG_MEMORY_POINTER, 0x10);
    cia_reset(&machine->cia1);
    cia_reset(&machine->cia2);
    c64_keyboard_reset(&machine->keyboard);

    c6510_reset(&machine->cpu);
    memset(&machine->clock, 0, sizeof(machine->clock));
    memset(&machine->working_frame, 0, sizeof(machine->working_frame));
    machine->keyboard_events = 0;
    machine->restore_requests = 0;
    machine->restore_pending = false;
    machine->cpu_cycles_remaining = 0;
    vector = (uint16_t)c64_bus_read(&machine->bus, 0xfffc) |
        ((uint16_t)c64_bus_read(&machine->bus, 0xfffd) << 8);

    if (machine->cpu.cpu.pc != vector) {
        c64_set_error(error, error_size, "CPU reset vector mismatch");
        return false;
    }

    c64_set_error(error, error_size, "");
    return true;
}

bool c64_step_instruction(c64_t *machine, char *error, size_t error_size) {
    assert(machine);

    if (!machine->ready) {
        c64_set_error(error, error_size, "machine is not ready");
        return false;
    }

    c6510_step(&machine->cpu);
    machine->clock.cpu_cycles = machine->cpu.cpu.cycles;
    machine->cpu_cycles_remaining = 0;
    c64_set_error(error, error_size, "");
    return true;
}

bool c64_step_cycle(c64_t *machine, char *error, size_t error_size) {
    assert(machine);

    if (!machine->ready) {
        c64_set_error(error, error_size, "machine is not ready");
        return false;
    }

    if (machine->cpu_cycles_remaining == 0) {
        machine->cpu_cycles_remaining = c6510_step(&machine->cpu);
        if (machine->cpu_cycles_remaining == 0) {
            machine->cpu_cycles_remaining = 1;
        }
    }

    machine->cpu_cycles_remaining--;
    machine->clock.cpu_cycles++;
    c64_step_vic(machine);
    c64_step_cia1(machine);
    c64_step_cia2(machine);
    c64_step_sid(machine);
    machine->clock.cycle++;

    c64_set_error(error, error_size, "");
    return true;
}

bool c64_generate_test_frame(c64_t *machine, c64_frame *out_frame) {
    return c64_make_frame_snapshot(machine, out_frame);
}

bool c64_make_frame_snapshot(c64_t *machine, c64_frame *out_frame) {
    assert(machine);
    assert(out_frame);

    return vicii_make_frame_snapshot(&machine->vic, &machine->bus, out_frame, machine->clock.cycle);
}

bool c64_consume_frame_complete(c64_t *machine) {
    assert(machine);

    return vicii_consume_frame_complete(&machine->vic);
}

void c64_set_key(c64_t *machine, c64_key key, bool pressed) {
    assert(machine);

    c64_keyboard_set_key(&machine->keyboard, key, pressed);
    machine->keyboard_events++;
}

void c64_restore(c64_t *machine) {
    assert(machine);

    machine->restore_requests++;
    machine->restore_pending = true;
}

void c64_set_memory_access_callback(c64_t *machine, c64_memory_access_fn callback, void *user) {
    assert(machine);

    machine->memory_access = callback;
    machine->memory_access_user = user;
}

void c64_copy_cpu_snapshot(const c64_t *machine, c64_cpu_snapshot *out) {
    assert(machine);
    assert(out);

    out->pc = machine->cpu.cpu.pc;
    out->a = machine->cpu.cpu.A;
    out->x = machine->cpu.cpu.X;
    out->y = machine->cpu.cpu.Y;
    out->sp = (uint8_t)(machine->cpu.cpu.sp & 0xff);
    out->p = machine->cpu.cpu.flags;
    out->cycles = machine->cpu.cpu.cycles;
}

void c64_copy_machine_snapshot(const c64_t *machine, c64_machine_snapshot *out) {
    assert(machine);
    assert(out);

    out->cycle = machine->clock.cycle;
    out->cpu_cycles = machine->clock.cpu_cycles;
    out->vic_cycles = machine->clock.vic_cycles;
    out->cia_cycles = machine->clock.cia_cycles;
    out->pc = machine->cpu.cpu.pc;
    out->a = machine->cpu.cpu.A;
    out->x = machine->cpu.cpu.X;
    out->y = machine->cpu.cpu.Y;
    out->sp = (uint8_t)(machine->cpu.cpu.sp & 0xff);
    out->p = machine->cpu.cpu.flags;
    out->ready = machine->ready;
    out->screen_ram_writes = machine->bus.screen_ram_writes;
    out->color_ram_writes = machine->bus.color_ram_writes;
    out->vic_register_writes = machine->bus.vic_register_writes;
    out->cia1_register_writes = machine->bus.cia1_register_writes;
    out->cia2_register_writes = machine->bus.cia2_register_writes;
    out->keyboard_events = machine->keyboard_events;
    out->irq_entries = machine->cpu.cpu.irq_entries;
    out->cia1_icr_reads = machine->cia1.icr_reads;
    out->cia1_icr_writes = machine->cia1.icr_writes;
    out->cia1_interrupt_assertions = machine->cia1.interrupt_assertions;
    out->nmi_entries = machine->cpu.cpu.nmi_entries;
    out->restore_requests = machine->restore_requests;
    out->cia1_irq_pending = cia_irq_pending(&machine->cia1);
    out->cia2_nmi_pending = cia_irq_pending(&machine->cia2);
}

void c64_copy_vicii_snapshot(const c64_t *machine, c64_vicii_snapshot *out) {
    assert(machine);
    assert(out);

    vicii_copy_snapshot(&machine->vic, out);
}

static bool c64_debug_basic_visible(const c64_t *machine) {
    return (machine->bus.cpu_port_data & 0x03u) == 0x03u;
}

static bool c64_debug_kernal_visible(const c64_t *machine) {
    return (machine->bus.cpu_port_data & 0x02u) != 0;
}

static bool c64_debug_io_or_char_visible(const c64_t *machine) {
    return (machine->bus.cpu_port_data & 0x03u) != 0;
}

static bool c64_debug_io_visible(const c64_t *machine) {
    return c64_debug_io_or_char_visible(machine) && (machine->bus.cpu_port_data & 0x04u) != 0;
}

static bool c64_debug_char_visible(const c64_t *machine) {
    return c64_debug_io_or_char_visible(machine) && (machine->bus.cpu_port_data & 0x04u) == 0;
}

static uint8_t c64_debug_peek_io(const c64_t *machine, uint16_t address) {
    if (address >= 0xd000 && address <= 0xd3ff) {
        return machine->vic.registers[address & 0x3fu];
    }

    if (address >= 0xd800 && address <= 0xdbff) {
        return (uint8_t)(machine->bus.color_ram[address - 0xd800u] & 0x0fu);
    }

    if (address >= 0xdc00 && address <= 0xdcff) {
        return machine->cia1.registers[address & 0x0fu];
    }

    if (address >= 0xdd00 && address <= 0xddff) {
        return machine->cia2.registers[address & 0x0fu];
    }

    return 0xff;
}

c64_memory_visibility c64_memory_visibility_at(const c64_t *machine, uint16_t address) {
    assert(machine);

    if (address >= 0xa000 && address <= 0xbfff && c64_debug_basic_visible(machine)) {
        return C64_MEMORY_VISIBILITY_ROM;
    }

    if (address >= 0xd000 && address <= 0xdfff) {
        if (c64_debug_io_visible(machine)) {
            return C64_MEMORY_VISIBILITY_IO;
        }

        if (c64_debug_char_visible(machine)) {
            return C64_MEMORY_VISIBILITY_ROM;
        }
    }

    if (address >= 0xe000 && c64_debug_kernal_visible(machine)) {
        return C64_MEMORY_VISIBILITY_ROM;
    }

    return C64_MEMORY_VISIBILITY_RAM;
}

uint8_t c64_debug_read_cpu_map(const c64_t *machine, uint16_t address) {
    assert(machine);

    if (address == 0x0000u) {
        return machine->bus.cpu_port_direction;
    }

    if (address == 0x0001u) {
        return machine->bus.cpu_port_data;
    }

    if (address >= 0xa000 && address <= 0xbfff && c64_debug_basic_visible(machine)) {
        return machine->bus.basic_rom[address - 0xa000u];
    }

    if (address >= 0xd000 && address <= 0xdfff) {
        if (c64_debug_io_visible(machine)) {
            return c64_debug_peek_io(machine, address);
        }

        if (c64_debug_char_visible(machine)) {
            return machine->bus.char_rom[address - 0xd000u];
        }
    }

    if (address >= 0xe000 && c64_debug_kernal_visible(machine)) {
        return machine->bus.kernal_rom[address - 0xe000u];
    }

    return machine->bus.ram[address];
}

uint8_t c64_debug_read_ram(const c64_t *machine, uint16_t address) {
    assert(machine);

    return machine->bus.ram[address];
}

void c64_debug_write_cpu_map(c64_t *machine, uint16_t address, uint8_t value) {
    assert(machine);

    c64_bus_write(&machine->bus, address, value);
}

void c64_debug_write_ram(c64_t *machine, uint16_t address, uint8_t value) {
    assert(machine);

    machine->bus.ram[address] = value;
    if (address >= 0x0400u && address < 0x0400u + 1000u) {
        machine->bus.screen_ram_writes++;
    }
}
