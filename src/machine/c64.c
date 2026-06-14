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

static uint32_t c64_argb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void c64_init(c64_t *machine) {
    assert(machine);

    memset(machine, 0, sizeof(*machine));
    c64_bus_init(&machine->bus);
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

    c6510_reset(&machine->cpu);
    memset(&machine->clock, 0, sizeof(machine->clock));
    memset(&machine->working_frame, 0, sizeof(machine->working_frame));
    machine->next_frame_number = 0;
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
    uint64_t frame_number;
    uint32_t x;
    uint32_t y;
    uint32_t marker_x;

    assert(machine);
    assert(out_frame);

    frame_number = machine->next_frame_number++;
    machine->working_frame.width = C64_FRAME_WIDTH;
    machine->working_frame.height = C64_FRAME_HEIGHT;
    machine->working_frame.stride_bytes = C64_FRAME_WIDTH * sizeof(machine->working_frame.pixels[0]);
    machine->working_frame.pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    machine->working_frame.frame_number = frame_number;
    machine->working_frame.machine_cycle = machine->clock.cycle;

    marker_x = (uint32_t)((frame_number * 5u) % (C64_FRAME_WIDTH - 48u));

    for (y = 0; y < C64_FRAME_HEIGHT; y++) {
        for (x = 0; x < C64_FRAME_WIDTH; x++) {
            uint32_t index = y * C64_FRAME_WIDTH + x;
            uint8_t r;
            uint8_t g;
            uint8_t b;

            if (x < 20 || x >= C64_FRAME_WIDTH - 20 || y < 18 || y >= C64_FRAME_HEIGHT - 18) {
                machine->working_frame.pixels[index] = c64_argb(88, 75, 164);
                continue;
            }

            r = (uint8_t)((x + frame_number * 3u) & 0xffu);
            g = (uint8_t)((y * 2u + frame_number * 7u) & 0xffu);
            b = (uint8_t)(((x ^ y) + frame_number * 11u) & 0xffu);
            if ((((x / 16u) + (y / 16u)) & 1u) == 0) {
                r = (uint8_t)(r / 2u);
                g = (uint8_t)(g / 2u);
                b = (uint8_t)(b / 2u);
            }
            machine->working_frame.pixels[index] = c64_argb(r, g, b);
        }
    }

    for (y = 34; y < 50; y++) {
        for (x = marker_x + 24u; x < marker_x + 48u; x++) {
            machine->working_frame.pixels[y * C64_FRAME_WIDTH + x] = c64_argb(245, 238, 118);
        }
    }

    memcpy(out_frame, &machine->working_frame, sizeof(*out_frame));
    return true;
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
