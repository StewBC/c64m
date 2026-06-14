#include "c64.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void c64_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static uint8_t c64_cpu_read(void *user, uint16_t address) {
    c64_t *machine = user;
    return c64_bus_read(&machine->bus, address);
}

static void c64_cpu_write(void *user, uint16_t address, uint8_t value) {
    c64_t *machine = user;
    c64_bus_write(&machine->bus, address, value);
}

static void c64_step_vic(c64_t *machine) {
    vicii_step_cycle(&machine->vic);
    machine->clock.vic_cycles++;
}

static void c64_step_cia1(c64_t *machine) {
    (void)machine;
}

static void c64_step_cia2(c64_t *machine) {
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
    c64_bus_attach_vicii(&machine->bus, &machine->vic);
    c6510_init(&machine->cpu, machine, c64_cpu_read, c64_cpu_write);
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

    c6510_reset(&machine->cpu);
    memset(&machine->clock, 0, sizeof(machine->clock));
    memset(&machine->working_frame, 0, sizeof(machine->working_frame));
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

    return vicii_make_frame_snapshot(&machine->vic, out_frame, machine->clock.cycle);
}

bool c64_consume_frame_complete(c64_t *machine) {
    assert(machine);

    return vicii_consume_frame_complete(&machine->vic);
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
}

void c64_copy_vicii_snapshot(const c64_t *machine, c64_vicii_snapshot *out) {
    assert(machine);
    assert(out);

    vicii_copy_snapshot(&machine->vic, out);
}
