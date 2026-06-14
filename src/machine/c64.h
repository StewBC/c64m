#pragma once

#include "c64_bus.h"
#include "c64_frame.h"
#include "c64_rom.h"
#include "c6510.h"
#include "cia.h"
#include "keyboard.h"
#include "vicii.h"

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

typedef struct c64_clock {
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
} c64_clock;

typedef struct c64_machine_snapshot {
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    bool ready;
    uint64_t screen_ram_writes;
    uint64_t color_ram_writes;
    uint64_t vic_register_writes;
    uint64_t cia1_register_writes;
    uint64_t cia2_register_writes;
    uint64_t keyboard_events;
    uint64_t irq_entries;
    uint64_t cia1_icr_reads;
    uint64_t cia1_icr_writes;
    uint64_t cia1_interrupt_assertions;
    uint64_t nmi_entries;
    uint64_t restore_requests;
    bool cia1_irq_pending;
    bool cia2_nmi_pending;
} c64_machine_snapshot;

typedef struct c64_t {
    c64_bus_t bus;
    C6510 cpu;
    vicii vic;
    cia cia1;
    cia cia2;
    c64_keyboard keyboard;
    c64_clock clock;
    c64_frame working_frame;
    uint64_t keyboard_events;
    uint64_t restore_requests;
    bool restore_pending;
    size_t cpu_cycles_remaining;
    bool has_basic_rom;
    bool has_kernal_rom;
    bool has_character_rom;
    bool ready;
} c64_t;

void c64_init(c64_t *machine);
bool c64_install_roms(c64_t *machine, const c64_rom_set *roms, char *error, size_t error_size);
bool c64_reset(c64_t *machine, char *error, size_t error_size);
bool c64_step_instruction(c64_t *machine, char *error, size_t error_size);
bool c64_step_cycle(c64_t *machine, char *error, size_t error_size);
bool c64_generate_test_frame(c64_t *machine, c64_frame *out_frame);
bool c64_make_frame_snapshot(c64_t *machine, c64_frame *out_frame);
bool c64_consume_frame_complete(c64_t *machine);
void c64_set_key(c64_t *machine, c64_key key, bool pressed);
void c64_restore(c64_t *machine);
void c64_copy_cpu_snapshot(const c64_t *machine, c64_cpu_snapshot *out);
void c64_copy_machine_snapshot(const c64_t *machine, c64_machine_snapshot *out);
void c64_copy_vicii_snapshot(const c64_t *machine, c64_vicii_snapshot *out);
