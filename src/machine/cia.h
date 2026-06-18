#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard.h"

enum {
    CIA_REGISTER_COUNT = 0x10,
};

#ifndef C64M_CIA_TYPEDEF
#define C64M_CIA_TYPEDEF
typedef struct cia cia;
#endif

typedef struct cia_timer {
    uint16_t latch;
    uint16_t counter;
    bool underflow;
    bool output_level;
    bool pulse_active;
} cia_timer;

struct cia {
    uint8_t registers[CIA_REGISTER_COUNT];
    cia_timer timer_a;
    cia_timer timer_b;
    uint8_t interrupt_flags;
    uint8_t interrupt_mask;
    c64_keyboard *keyboard;
    uint64_t icr_reads;
    uint64_t icr_writes;
    uint64_t interrupt_assertions;
    bool cnt_pulse;
};

bool cia_init(cia *c, char *error, size_t error_size);
void cia_reset(cia *c);
void cia_attach_keyboard(cia *c, c64_keyboard *keyboard);
void cia_step_cycle(cia *c);
void cia_pulse_cnt(cia *c);
void cia_set_interrupt_source(cia *c, uint8_t source_mask);
uint8_t cia_read_register(cia *c, uint16_t addr);
uint8_t cia_debug_read_register(const cia *c, uint16_t addr);
uint8_t cia_read_port_a_pins(const cia *c);
void cia_write_register(cia *c, uint16_t addr, uint8_t value);
bool cia_irq_pending(const cia *c);
