#include "cia.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    CIA_REG_PORT_A = 0x00,
    CIA_REG_PORT_B = 0x01,
    CIA_REG_DDRA = 0x02,
    CIA_REG_DDRB = 0x03,
    CIA_REG_TIMER_A_LO = 0x04,
    CIA_REG_TIMER_A_HI = 0x05,
    CIA_REG_TIMER_B_LO = 0x06,
    CIA_REG_TIMER_B_HI = 0x07,
    CIA_REG_TOD_TENTHS = 0x08,
    CIA_REG_TOD_SECONDS = 0x09,
    CIA_REG_TOD_MINUTES = 0x0a,
    CIA_REG_TOD_HOURS = 0x0b,
    CIA_REG_SDR = 0x0c,
    CIA_REG_ICR = 0x0d,
    CIA_REG_CONTROL_A = 0x0e,
    CIA_REG_CONTROL_B = 0x0f,
    CIA_CONTROL_START = 0x01,
    CIA_CONTROL_PB_OUTPUT = 0x02,
    CIA_CONTROL_PB_TOGGLE = 0x04,
    CIA_CONTROL_ONESHOT = 0x08,
    CIA_CONTROL_FORCE_LOAD = 0x10,
    CIA_CONTROL_A_CNT_INPUT = 0x20,
    CIA_CONTROL_A_SERIAL_OUT = 0x40,
    CIA_CONTROL_TOD_50HZ = 0x80,
    CIA_INTERRUPT_TIMER_A = 0x01,
    CIA_INTERRUPT_TIMER_B = 0x02,
    CIA_INTERRUPT_TOD = 0x04,
    CIA_INTERRUPT_SERIAL = 0x08,
    CIA_INTERRUPT_FLAG = 0x10,
    CIA_INTERRUPT_SOURCE_MASK = 0x1f,
};

static void cia_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static uint8_t cia_read_port(uint8_t data, uint8_t direction) {
    return (uint8_t)((data & direction) | (uint8_t)~direction);
}

static cia_port_inputs cia_read_external_inputs(const cia *c, uint8_t port_a_pins, uint8_t port_b_pins) {
    cia_port_inputs inputs = {0, 0};

    if (c->port_input) {
        c->port_input(c->port_input_user, port_a_pins, port_b_pins, &inputs);
    }

    if (c->keyboard) {
        uint8_t selected_rows = (uint8_t)~port_a_pins;
        uint8_t selected_columns = (uint8_t)~port_b_pins;
        inputs.port_b_pull_down |= (uint8_t)~c64_keyboard_read_columns(c->keyboard, selected_rows);
        inputs.port_a_pull_down |= (uint8_t)~c64_keyboard_read_rows(c->keyboard, selected_columns);
    }

    return inputs;
}

static uint8_t cia_read_port_a_pins_internal(const cia *c) {
    uint8_t port_a = cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
    uint8_t port_b = cia_read_port(c->registers[CIA_REG_PORT_B], c->registers[CIA_REG_DDRB]);
    cia_port_inputs inputs = cia_read_external_inputs(c, port_a, port_b);

    return (uint8_t)(port_a & (uint8_t)~inputs.port_a_pull_down);
}

static void cia_reload_timer(cia_timer *timer) {
    timer->counter = timer->latch == 0 ? 0xffffu : timer->latch;
}

static void cia_reload_timer_from_run(cia_timer *timer) {
    timer->counter = timer->latch == 0 ? 0xffffu : timer->latch;
    /* Underflow/force-load while counting removes the next count clock. */
    timer->skip_tick = true;
}

static uint16_t cia_timer_reload_value(const cia_timer *timer) {
    return timer->latch == 0 ? 0xffffu : timer->latch;
}

static uint8_t cia_bcd_to_uint(uint8_t value) {
    return (uint8_t)(((value >> 4) & 0x0fu) * 10u + (value & 0x0fu));
}

static uint8_t cia_uint_to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static uint8_t cia_sanitize_bcd(uint8_t value, uint8_t max) {
    uint8_t decimal = cia_bcd_to_uint(value);

    if ((value & 0x0fu) > 9u || decimal > max) {
        decimal = 0;
    }

    return cia_uint_to_bcd(decimal);
}

static uint8_t cia_sanitize_tod_hour(uint8_t value) {
    uint8_t pm = (uint8_t)(value & 0x80u);
    uint8_t hour = cia_bcd_to_uint((uint8_t)(value & 0x1fu));

    if (((value & 0x0fu) > 9u) || hour < 1u || hour > 12u) {
        hour = 12;
    }

    return (uint8_t)(pm | cia_uint_to_bcd(hour));
}

static bool cia_tod_equal(const cia_tod *a, const cia_tod *b) {
    return a->tenth == b->tenth &&
        a->seconds == b->seconds &&
        a->minutes == b->minutes &&
        a->hours == b->hours;
}

static void cia_increment_tod(cia *c) {
    uint8_t tenth = c->tod.tenth;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hour;
    uint8_t pm;

    tenth++;
    if (tenth < 10u) {
        c->tod.tenth = tenth;
        return;
    }

    c->tod.tenth = 0;
    seconds = (uint8_t)(cia_bcd_to_uint(c->tod.seconds) + 1u);
    if (seconds < 60u) {
        c->tod.seconds = cia_uint_to_bcd(seconds);
        return;
    }

    c->tod.seconds = 0;
    minutes = (uint8_t)(cia_bcd_to_uint(c->tod.minutes) + 1u);
    if (minutes < 60u) {
        c->tod.minutes = cia_uint_to_bcd(minutes);
        return;
    }

    c->tod.minutes = 0;
    pm = (uint8_t)(c->tod.hours & 0x80u);
    hour = (uint8_t)(cia_bcd_to_uint((uint8_t)(c->tod.hours & 0x1fu)) + 1u);
    if (hour == 12u) {
        pm ^= 0x80u;
    } else if (hour > 12u) {
        hour = 1;
    }
    c->tod.hours = (uint8_t)(pm | cia_uint_to_bcd(hour));
}

static uint64_t cia_tod_tick_cycles(const cia *c) {
    if ((c->registers[CIA_REG_CONTROL_A] & CIA_CONTROL_TOD_50HZ) != 0) {
        return c->tod_50hz_cycles;
    }

    return c->tod_60hz_cycles;
}

static void cia_step_tod(cia *c) {
    uint64_t tick_cycles = cia_tod_tick_cycles(c);

    if (tick_cycles == 0) {
        return;
    }

    c->tod_cycle_accum++;
    while (c->tod_cycle_accum >= tick_cycles) {
        c->tod_cycle_accum -= tick_cycles;
        cia_increment_tod(c);
        if (cia_tod_equal(&c->tod, &c->tod_alarm)) {
            cia_set_interrupt_source(c, CIA_INTERRUPT_TOD);
        }
    }
}

static uint8_t cia_read_tod_register(cia *c, uint8_t reg) {
    const cia_tod *tod = c->tod_latched ? &c->tod_latch : &c->tod;

    if (reg == CIA_REG_TOD_HOURS && !c->tod_latched) {
        c->tod_latch = c->tod;
        c->tod_latched = true;
        tod = &c->tod_latch;
    }

    switch (reg) {
        case CIA_REG_TOD_TENTHS:
            c->tod_latched = false;
            return tod->tenth;
        case CIA_REG_TOD_SECONDS:
            return tod->seconds;
        case CIA_REG_TOD_MINUTES:
            return tod->minutes;
        case CIA_REG_TOD_HOURS:
            return tod->hours;
        default:
            return 0xff;
    }
}

static uint8_t cia_debug_read_tod_register(const cia *c, uint8_t reg) {
    const cia_tod *tod = c->tod_latched ? &c->tod_latch : &c->tod;

    switch (reg) {
        case CIA_REG_TOD_TENTHS:
            return tod->tenth;
        case CIA_REG_TOD_SECONDS:
            return tod->seconds;
        case CIA_REG_TOD_MINUTES:
            return tod->minutes;
        case CIA_REG_TOD_HOURS:
            return tod->hours;
        default:
            return 0xff;
    }
}

static void cia_write_tod_register(cia *c, uint8_t reg, uint8_t value) {
    cia_tod *tod = (c->registers[CIA_REG_CONTROL_B] & 0x80u) != 0 ? &c->tod_alarm : &c->tod;

    switch (reg) {
        case CIA_REG_TOD_TENTHS:
            tod->tenth = value <= 9u ? value : 0;
            if (tod == &c->tod) {
                c->tod_cycle_accum = 0;
                c->tod_latched = false;
            }
            break;
        case CIA_REG_TOD_SECONDS:
            tod->seconds = cia_sanitize_bcd(value, 59);
            break;
        case CIA_REG_TOD_MINUTES:
            tod->minutes = cia_sanitize_bcd(value, 59);
            break;
        case CIA_REG_TOD_HOURS:
            tod->hours = cia_sanitize_tod_hour(value);
            break;
        default:
            break;
    }

    c->registers[reg] = value;
}

static void cia_reset_timer_output_pulses(cia *c) {
    if (c->timer_a.pulse_active) {
        c->timer_a.output_level = true;
    }
    if (c->timer_b.pulse_active) {
        c->timer_b.output_level = true;
    }
    c->timer_a.pulse_active = false;
    c->timer_b.pulse_active = false;
}

static void cia_update_interrupt_ff(cia *c) {
    if ((c->interrupt_flags & c->interrupt_mask & CIA_INTERRUPT_SOURCE_MASK) != 0) {
        c->interrupt_ff = true;
    }
}

void cia_set_interrupt_source(cia *c, uint8_t source_mask) {
    uint8_t flag;

    assert(c);

    flag = (uint8_t)(source_mask & CIA_INTERRUPT_SOURCE_MASK);
    if (flag == 0) {
        return;
    }

    if ((c->interrupt_flags & flag) == 0) {
        c->interrupt_assertions++;
    }
    c->interrupt_flags |= flag;
    cia_update_interrupt_ff(c);
}

static void cia_timer_update_oneshot_pipe(cia_timer *timer) {
    if (timer->oneshot_delay == 0u) {
        return;
    }
    timer->oneshot_delay--;
    if (timer->oneshot_delay == 0u) {
        timer->oneshot_effective = timer->oneshot_pending;
    }
}

static void cia_timer_write_oneshot(cia_timer *timer, bool oneshot) {
    if (oneshot == timer->oneshot_effective && timer->oneshot_delay == 0u) {
        timer->oneshot_pending = oneshot;
        return;
    }
    /* Lorenz FLIPOS: set oneshot takes effect in 1 cycle, clear in 2. */
    timer->oneshot_pending = oneshot;
    timer->oneshot_delay = oneshot ? 1u : 2u;
}

static bool cia_timer_a_should_count(const cia *c, uint8_t control) {
    if ((control & CIA_CONTROL_A_CNT_INPUT) != 0) {
        return c->cnt_pulse;
    }

    return true;
}

static bool cia_timer_b_should_count(const cia *c, uint8_t control) {
    uint8_t input_mode = (uint8_t)((control & 0x60u) >> 5);

    switch (input_mode) {
        case 0x00:
            return true;
        case 0x01:
            return c->cnt_pulse;
        case 0x02:
            return c->timer_a.underflow;
        case 0x03:
            return c->timer_a.underflow && c->cnt_pulse;
        default:
            return false;
    }
}

static void cia_timer_update_pb_output(cia_timer *timer, uint8_t control) {
    if ((control & CIA_CONTROL_PB_OUTPUT) == 0) {
        return;
    }

    if ((control & CIA_CONTROL_PB_TOGGLE) != 0) {
        timer->output_level = !timer->output_level;
    } else {
        timer->pulse_active = true;
        timer->output_level = false;
    }
}

/* Advances the force-load pipeline. Returns true while counting must be
 * suppressed: on the Phi2 the reload lands (the load occupies the clock, like
 * VICE's CIAT_LOAD clearing the count phase) and on the following Phi2 (the
 * reload discards the next count clock). Kept separate from skip_tick so a
 * cascade/CNT-gated timer, which may not count for many cycles, still clears the
 * suppression on schedule instead of eating its next real count event. */
static bool cia_timer_update_load_pipe(cia_timer *timer) {
    if (timer->load_delay > 0u) {
        timer->load_delay--;
        if (timer->load_delay == 0u) {
            timer->counter = cia_timer_reload_value(timer);
            timer->load_hold = 1u;
            return true;
        }
        return false;
    }
    if (timer->load_hold > 0u) {
        timer->load_hold--;
        return true;
    }
    return false;
}

static void cia_step_timer(cia *c, cia_timer *timer, uint8_t control_reg, uint8_t interrupt_flag) {
    uint8_t control = c->registers[control_reg];
    bool oneshot_for_uf;
    bool loaded_now;

    /* Apply a pending force-load before this cycle's count so the reloaded value
     * becomes visible one Phi2 after the CR write, not on the write cycle. */
    loaded_now = cia_timer_update_load_pipe(timer);

    timer->underflow = false;
    /*
     * Oneshot bit from a same-cycle CRA/CRB write is applied after this tick so
     * underflow sees the pre-write oneshot state (FLIPOS set-at-t does not stop).
     */
    oneshot_for_uf = timer->oneshot_effective;
    cia_timer_update_oneshot_pipe(timer);

    if ((control & CIA_CONTROL_START) == 0) {
        timer->start_delay = 0;
        goto apply_oneshot_write;
    }

    if (timer->start_delay > 0u) {
        timer->start_delay--;
        goto apply_oneshot_write;
    }

    if (control_reg == CIA_REG_CONTROL_A) {
        if (!cia_timer_a_should_count(c, control)) {
            goto apply_oneshot_write;
        }
    } else if (!cia_timer_b_should_count(c, control)) {
        goto apply_oneshot_write;
    }

    /* The cycle a force-load reload lands does not count; skip_tick (set by the
     * reload) still suppresses the following cycle. */
    if (loaded_now) {
        goto apply_oneshot_write;
    }

    if (timer->skip_tick) {
        timer->skip_tick = false;
        goto apply_oneshot_write;
    }

    if (timer->counter <= 1u) {
        timer->underflow = true;
        cia_set_interrupt_source(c, interrupt_flag);
        cia_timer_update_pb_output(timer, control);
        cia_reload_timer_from_run(timer);
        if (oneshot_for_uf) {
            c->registers[control_reg] =
                (uint8_t)(c->registers[control_reg] & (uint8_t)~CIA_CONTROL_START);
        }
        goto apply_oneshot_write;
    }

    timer->counter--;

apply_oneshot_write:
    if (timer->oneshot_write_pending) {
        cia_timer_write_oneshot(timer, timer->oneshot_write_value);
        timer->oneshot_write_pending = false;
    }
}

static uint8_t cia_read_port_b_pins(const cia *c) {
    uint8_t port_a = cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
    uint8_t value = cia_read_port(c->registers[CIA_REG_PORT_B], c->registers[CIA_REG_DDRB]);
    cia_port_inputs inputs = cia_read_external_inputs(c, port_a, value);

    value &= (uint8_t)~inputs.port_b_pull_down;

    if ((c->registers[CIA_REG_CONTROL_A] & CIA_CONTROL_PB_OUTPUT) != 0) {
        if (c->timer_a.pulse_active || !c->timer_a.output_level) {
            value &= (uint8_t)~0x40u;
        } else {
            value |= 0x40u;
        }
    }

    if ((c->registers[CIA_REG_CONTROL_B] & CIA_CONTROL_PB_OUTPUT) != 0) {
        if (c->timer_b.pulse_active || !c->timer_b.output_level) {
            value &= (uint8_t)~0x80u;
        } else {
            value |= 0x80u;
        }
    }

    return value;
}

bool cia_init(cia *c, char *error, size_t error_size) {
    if (!c) {
        cia_set_error(error, error_size, "CIA pointer is null");
        return false;
    }

    memset(c, 0, sizeof(*c));
    cia_reset(c);
    cia_set_error(error, error_size, "");
    return true;
}

void cia_reset(cia *c) {
    c64_keyboard *keyboard;
    cia_port_input_fn port_input;
    void *port_input_user;
    uint64_t tod_50hz_cycles;
    uint64_t tod_60hz_cycles;

    assert(c);

    keyboard = c->keyboard;
    port_input = c->port_input;
    port_input_user = c->port_input_user;
    tod_50hz_cycles = c->tod_50hz_cycles;
    tod_60hz_cycles = c->tod_60hz_cycles;
    memset(c, 0, sizeof(*c));
    c->keyboard = keyboard;
    c->port_input = port_input;
    c->port_input_user = port_input_user;
    c->timer_a.output_level = true;
    c->timer_b.output_level = true;
    c->flag_line = true;
    c->pc_line = true;
    c->tod.hours = 0x12;
    c->tod_alarm.hours = 0x12;
    c->tod_50hz_cycles = tod_50hz_cycles == 0 ? 100000u : tod_50hz_cycles;
    c->tod_60hz_cycles = tod_60hz_cycles == 0 ? 100000u : tod_60hz_cycles;
}

void cia_attach_keyboard(cia *c, c64_keyboard *keyboard) {
    assert(c);

    c->keyboard = keyboard;
}

void cia_attach_port_input(cia *c, cia_port_input_fn input, void *user) {
    assert(c);

    c->port_input = input;
    c->port_input_user = user;
}

void cia_set_tod_cycles(cia *c, uint64_t cycles_50hz, uint64_t cycles_60hz) {
    assert(c);

    c->tod_50hz_cycles = cycles_50hz;
    c->tod_60hz_cycles = cycles_60hz;
}

static void cia_step_serial(cia *c) {
    uint8_t control = c->registers[CIA_REG_CONTROL_A];

    if ((control & CIA_CONTROL_A_SERIAL_OUT) != 0) {
        /* Output mode: shifting is clocked by Timer A underflows. CNT toggles on
         * each underflow, so one bit leaves SP (MSB first) every two underflows. */
        if (c->serial_out_bits == 0 || !c->timer_a.underflow) {
            return;
        }

        c->serial_cnt_toggle = !c->serial_cnt_toggle;
        if (c->serial_cnt_toggle) {
            return;
        }

        c->sp_output = (c->serial_shift & 0x80u) != 0;
        c->serial_shift = (uint8_t)(c->serial_shift << 1);
        c->serial_out_bits--;
        if (c->serial_out_bits == 0) {
            cia_set_interrupt_source(c, CIA_INTERRUPT_SERIAL);
            if (c->serial_pending) {
                c->serial_shift = c->serial_pending_data;
                c->serial_out_bits = 8;
                c->serial_pending = false;
            }
        }
        return;
    }

    /* Input mode: shifting is clocked by external CNT edges. Each edge samples SP
     * (MSB first). After eight bits the byte latches into SDR and sets ICR. */
    if (!c->cnt_pulse) {
        return;
    }

    c->serial_shift = (uint8_t)((c->serial_shift << 1) | (c->sp_input ? 1u : 0u));
    c->serial_in_count++;
    if (c->serial_in_count >= 8u) {
        c->registers[CIA_REG_SDR] = c->serial_shift;
        c->serial_in_count = 0;
        cia_set_interrupt_source(c, CIA_INTERRUPT_SERIAL);
    }
}

void cia_step_cycle(cia *c) {
    bool pending_now;

    assert(c);

    cia_reset_timer_output_pulses(c);
    cia_step_timer(c, &c->timer_a, CIA_REG_CONTROL_A, CIA_INTERRUPT_TIMER_A);
    cia_step_timer(c, &c->timer_b, CIA_REG_CONTROL_B, CIA_INTERRUPT_TIMER_B);
    cia_step_serial(c);
    cia_step_tod(c);
    /* IR flip-flop (set when flags&mask) drives the interrupt pin one cycle
     * later. Clearing the mask does not clear the flip-flop — only ICR read. */
    cia_update_interrupt_ff(c);
    pending_now = c->interrupt_ff;
    c->interrupt_line = c->interrupt_pending_latched;
    c->interrupt_pending_latched = pending_now;
    /* PC pulses low for the one cycle following a CPU-visible PRB access, then
     * returns high. */
    c->pc_line = !c->pc_pulse_request;
    c->pc_pulse_request = false;
    c->cnt_pulse = false;
}

void cia_pulse_cnt(cia *c) {
    assert(c);

    c->cnt_pulse = true;
}

void cia_set_flag_line(cia *c, bool level) {
    assert(c);

    if (c->flag_line && !level) {
        cia_set_interrupt_source(c, CIA_INTERRUPT_FLAG);
    }
    c->flag_line = level;
}

void cia_set_sp_line(cia *c, bool level) {
    assert(c);

    c->sp_input = level;
}

bool cia_pc_line(const cia *c) {
    assert(c);

    return c->pc_line;
}

bool cia_interrupt_line(const cia *c) {
    assert(c);

    return c->interrupt_line;
}

uint8_t cia_read_register(cia *c, uint16_t addr) {
    uint8_t reg;

    assert(c);

    reg = (uint8_t)(addr & 0x0fu);
    switch (reg) {
        case CIA_REG_PORT_A:
            return cia_read_port_a_pins_internal(c);
        case CIA_REG_PORT_B:
            c->pc_pulse_request = true;
            return cia_read_port_b_pins(c);
        case CIA_REG_TIMER_A_LO:
            return (uint8_t)(c->timer_a.counter & 0xffu);
        case CIA_REG_TIMER_A_HI:
            return (uint8_t)(c->timer_a.counter >> 8);
        case CIA_REG_TIMER_B_LO:
            return (uint8_t)(c->timer_b.counter & 0xffu);
        case CIA_REG_TIMER_B_HI:
            return (uint8_t)(c->timer_b.counter >> 8);
        case CIA_REG_TOD_TENTHS:
        case CIA_REG_TOD_SECONDS:
        case CIA_REG_TOD_MINUTES:
        case CIA_REG_TOD_HOURS:
            return cia_read_tod_register(c, reg);
        case CIA_REG_ICR: {
            uint8_t flags = (uint8_t)(c->interrupt_flags & CIA_INTERRUPT_SOURCE_MASK);
            uint8_t value = flags;
            c->icr_reads++;
            /* Bit 7 follows the IR flip-flop (and live flags&mask). */
            if (c->interrupt_ff || (flags & c->interrupt_mask) != 0) {
                value |= 0x80u;
            }
            c->interrupt_flags &= (uint8_t)~flags;
            c->interrupt_ff = false;
            c->timer_a.underflow = false;
            c->timer_b.underflow = false;
            return value;
        }
        default:
            return c->registers[reg];
    }
}

void cia_write_register(cia *c, uint16_t addr, uint8_t value) {
    uint8_t reg;

    assert(c);

    reg = (uint8_t)(addr & 0x0fu);
    switch (reg) {
        case CIA_REG_PORT_B:
            c->registers[reg] = value;
            c->pc_pulse_request = true;
            return;
        case CIA_REG_TIMER_A_LO:
            c->registers[reg] = value;
            /* Low-byte write updates the latch only. On the 6526 only a high-byte
             * write (or force-load/underflow) loads a stopped counter. */
            c->timer_a.latch = (uint16_t)((c->timer_a.latch & 0xff00u) | value);
            return;
        case CIA_REG_TIMER_A_HI:
            c->registers[reg] = value;
            c->timer_a.latch = (uint16_t)((c->timer_a.latch & 0x00ffu) | ((uint16_t)value << 8));
            if ((c->registers[CIA_REG_CONTROL_A] & CIA_CONTROL_START) == 0) {
                cia_reload_timer(&c->timer_a);
            }
            return;
        case CIA_REG_TIMER_B_LO:
            c->registers[reg] = value;
            /* Low-byte write updates the latch only (see Timer A low above). */
            c->timer_b.latch = (uint16_t)((c->timer_b.latch & 0xff00u) | value);
            return;
        case CIA_REG_TIMER_B_HI:
            c->registers[reg] = value;
            c->timer_b.latch = (uint16_t)((c->timer_b.latch & 0x00ffu) | ((uint16_t)value << 8));
            if ((c->registers[CIA_REG_CONTROL_B] & CIA_CONTROL_START) == 0) {
                cia_reload_timer(&c->timer_b);
            }
            return;
        case CIA_REG_TOD_TENTHS:
        case CIA_REG_TOD_SECONDS:
        case CIA_REG_TOD_MINUTES:
        case CIA_REG_TOD_HOURS:
            cia_write_tod_register(c, reg, value);
            return;
        case CIA_REG_SDR:
            c->registers[reg] = value;
            if ((c->registers[CIA_REG_CONTROL_A] & CIA_CONTROL_A_SERIAL_OUT) != 0) {
                if (c->serial_out_bits == 0) {
                    c->serial_shift = value;
                    c->serial_out_bits = 8;
                    c->serial_cnt_toggle = false;
                } else {
                    c->serial_pending_data = value;
                    c->serial_pending = true;
                }
            }
            return;
        case CIA_REG_ICR:
            c->icr_writes++;
            if ((value & 0x80u) != 0) {
                c->interrupt_mask |= (uint8_t)(value & CIA_INTERRUPT_SOURCE_MASK);
            } else {
                c->interrupt_mask &= (uint8_t)~(value & CIA_INTERRUPT_SOURCE_MASK);
            }
            c->registers[reg] = c->interrupt_mask;
            /* Enabling a mask for an already-latched flag sets IR; clear does not. */
            cia_update_interrupt_ff(c);
            return;
        case CIA_REG_CONTROL_A: {
            uint8_t old = c->registers[reg];
            bool was_running = (old & CIA_CONTROL_START) != 0;
            c->registers[reg] = (uint8_t)(value & (uint8_t)~CIA_CONTROL_FORCE_LOAD);
            if ((value & CIA_CONTROL_FORCE_LOAD) != 0) {
                /* Deferred: reload becomes visible on the second Phi2 after the
                 * write (see cia_timer_update_load_pipe). */
                c->timer_a.load_delay = 2;
            }
            if (!was_running && ((value & CIA_CONTROL_START) != 0)) {
                c->timer_a.start_delay = 2;
                c->timer_a.skip_tick = false;
            }
            if ((value & CIA_CONTROL_START) == 0) {
                c->timer_a.start_delay = 0;
            }
            /* Defer oneshot pipeline update until after this cycle's timer tick. */
            c->timer_a.oneshot_write_value = (value & CIA_CONTROL_ONESHOT) != 0;
            c->timer_a.oneshot_write_pending = true;
            return;
        }
        case CIA_REG_CONTROL_B: {
            uint8_t old = c->registers[reg];
            bool was_running = (old & CIA_CONTROL_START) != 0;
            c->registers[reg] = (uint8_t)(value & (uint8_t)~CIA_CONTROL_FORCE_LOAD);
            if ((value & CIA_CONTROL_FORCE_LOAD) != 0) {
                /* Deferred: see Timer A force-load above. */
                c->timer_b.load_delay = 2;
            }
            if (!was_running && ((value & CIA_CONTROL_START) != 0)) {
                c->timer_b.start_delay = 2;
                c->timer_b.skip_tick = false;
            }
            if ((value & CIA_CONTROL_START) == 0) {
                c->timer_b.start_delay = 0;
            }
            c->timer_b.oneshot_write_value = (value & CIA_CONTROL_ONESHOT) != 0;
            c->timer_b.oneshot_write_pending = true;
            return;
        }
        default:
            c->registers[reg] = value;
            return;
    }
}

uint8_t cia_debug_read_register(const cia *c, uint16_t addr) {
    uint8_t reg;
    uint8_t flags;

    assert(c);

    reg = (uint8_t)(addr & 0x0fu);
    switch (reg) {
        case CIA_REG_PORT_A:
            return cia_read_port_a_pins_internal(c);
        case CIA_REG_PORT_B:
            return cia_read_port_b_pins(c);
        case CIA_REG_TIMER_A_LO:
            return (uint8_t)(c->timer_a.counter & 0xffu);
        case CIA_REG_TIMER_A_HI:
            return (uint8_t)(c->timer_a.counter >> 8);
        case CIA_REG_TIMER_B_LO:
            return (uint8_t)(c->timer_b.counter & 0xffu);
        case CIA_REG_TIMER_B_HI:
            return (uint8_t)(c->timer_b.counter >> 8);
        case CIA_REG_TOD_TENTHS:
        case CIA_REG_TOD_SECONDS:
        case CIA_REG_TOD_MINUTES:
        case CIA_REG_TOD_HOURS:
            return cia_debug_read_tod_register(c, reg);
        case CIA_REG_ICR:
            flags = (uint8_t)(c->interrupt_flags & CIA_INTERRUPT_SOURCE_MASK);
            if (c->interrupt_ff || (flags & c->interrupt_mask) != 0) {
                return (uint8_t)(flags | 0x80u);
            }
            return flags;
        default:
            return c->registers[reg];
    }
}

uint8_t cia_read_port_a_pins(const cia *c) {
    assert(c);

    return cia_read_port_a_pins_internal(c);
}

bool cia_irq_pending(const cia *c) {
    assert(c);

    /* True while the IR flip-flop is set (or live flags&mask before step). */
    return c->interrupt_ff ||
        (c->interrupt_flags & c->interrupt_mask & CIA_INTERRUPT_SOURCE_MASK) != 0;
}

uint8_t cia_peek_port_a_output(const cia *c) {
    assert(c);
    return cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
}
