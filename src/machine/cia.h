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
    /* Lorenz CIA model: two Phi2 clocks after start before counting begins. */
    uint8_t start_delay;
    /* After force-load or underflow reload, the next count clock is discarded. */
    bool skip_tick;
    /* Force-load strobe reloads the counter from the latch on the 6526, but the
     * new value is not visible until the second Phi2 after the CR write, and the
     * reload suppresses counting on its Phi2 and the following one. */
    uint8_t load_delay;
    uint8_t load_hold;
    /* A CPU write that clears START while running takes effect on counting one
     * Phi2 late: the timer counts once more on the write cycle, then stops. */
    bool stop_pending;
    /* Effective oneshot for underflow (delayed vs CRA bit 3 / FLIPOS). */
    bool oneshot_effective;
    bool oneshot_pending;
    uint8_t oneshot_delay; /* cycles until pending applies: set=1, clear=2 */
    /* Oneshot bit from CRA write applied after this cycle's timer clock. */
    bool oneshot_write_pending;
    bool oneshot_write_value;
} cia_timer;

typedef struct cia_tod {
    uint8_t tenth;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
} cia_tod;

typedef struct cia_port_inputs {
    uint8_t port_a_pull_down;
    uint8_t port_b_pull_down;
} cia_port_inputs;

typedef void (*cia_port_input_fn)(
    void *user,
    uint8_t port_a_pins,
    uint8_t port_b_pins,
    cia_port_inputs *out);

struct cia {
    uint8_t registers[CIA_REGISTER_COUNT];
    cia_timer timer_a;
    cia_timer timer_b;
    cia_tod tod;
    cia_tod tod_alarm;
    cia_tod tod_latch;
    uint64_t tod_cycle_accum;
    uint64_t tod_50hz_cycles;
    uint64_t tod_60hz_cycles;
    uint8_t interrupt_flags;
    uint8_t interrupt_mask;
    c64_keyboard *keyboard;
    uint64_t icr_reads;
    uint64_t icr_writes;
    uint64_t interrupt_assertions;
    cia_port_input_fn port_input;
    void *port_input_user;
    bool tod_latched;
    bool cnt_pulse;
    bool flag_line;
    uint8_t serial_shift;
    uint8_t serial_out_bits;
    uint8_t serial_in_count;
    uint8_t serial_pending_data;
    bool serial_pending;
    bool serial_cnt_toggle;
    bool sp_output;
    bool sp_input;
    bool pc_line;
    bool pc_pulse_request;
    bool interrupt_line;
    bool interrupt_pending_latched;
    /* Latched IR flip-flop: set when flags&mask, cleared only by ICR read. */
    bool interrupt_ff;
};

bool cia_init(cia *c, char *error, size_t error_size);
void cia_reset(cia *c);
void cia_attach_keyboard(cia *c, c64_keyboard *keyboard);
void cia_attach_port_input(cia *c, cia_port_input_fn input, void *user);
void cia_set_tod_cycles(cia *c, uint64_t cycles_50hz, uint64_t cycles_60hz);
void cia_step_cycle(cia *c);
void cia_pulse_cnt(cia *c);
void cia_set_flag_line(cia *c, bool level);
void cia_set_sp_line(cia *c, bool level);
bool cia_pc_line(const cia *c);
bool cia_interrupt_line(const cia *c);
void cia_set_interrupt_source(cia *c, uint8_t source_mask);
uint8_t cia_read_register(cia *c, uint16_t addr);
uint8_t cia_debug_read_register(const cia *c, uint16_t addr);
uint8_t cia_read_port_a_pins(const cia *c);
void cia_write_register(cia *c, uint16_t addr, uint8_t value);
bool cia_irq_pending(const cia *c);
uint8_t cia_peek_port_a_output(const cia *c);
