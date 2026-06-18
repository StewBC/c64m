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
    CIA_REG_ICR = 0x0d,
    CIA_REG_CONTROL_A = 0x0e,
    CIA_REG_CONTROL_B = 0x0f,
    CIA_CONTROL_START = 0x01,
    CIA_CONTROL_PB_OUTPUT = 0x02,
    CIA_CONTROL_PB_TOGGLE = 0x04,
    CIA_CONTROL_ONESHOT = 0x08,
    CIA_CONTROL_FORCE_LOAD = 0x10,
    CIA_CONTROL_A_CNT_INPUT = 0x20,
    CIA_INTERRUPT_TIMER_A = 0x01,
    CIA_INTERRUPT_TIMER_B = 0x02,
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

static uint8_t cia_keyboard_selected_rows(const cia *c) {
    return (uint8_t)~cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
}

static void cia_reload_timer(cia_timer *timer) {
    timer->counter = timer->latch == 0 ? 0xffffu : timer->latch;
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

static void cia_step_timer(cia *c, cia_timer *timer, uint8_t control_reg, uint8_t interrupt_flag) {
    uint8_t control = c->registers[control_reg];

    timer->underflow = false;
    if ((control & CIA_CONTROL_START) == 0) {
        return;
    }

    if (control_reg == CIA_REG_CONTROL_A) {
        if (!cia_timer_a_should_count(c, control)) {
            return;
        }
    } else if (!cia_timer_b_should_count(c, control)) {
        return;
    }

    if (timer->counter == 0) {
        timer->underflow = true;
        cia_set_interrupt_source(c, interrupt_flag);
        cia_timer_update_pb_output(timer, control);
        cia_reload_timer(timer);
        if ((control & CIA_CONTROL_ONESHOT) != 0) {
            c->registers[control_reg] = (uint8_t)(control & (uint8_t)~CIA_CONTROL_START);
        }
        return;
    }

    timer->counter--;
}

static uint8_t cia_read_port_b_pins(const cia *c) {
    uint8_t value = cia_read_port(c->registers[CIA_REG_PORT_B], c->registers[CIA_REG_DDRB]);

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

    assert(c);

    keyboard = c->keyboard;
    memset(c, 0, sizeof(*c));
    c->keyboard = keyboard;
    c->timer_a.output_level = true;
    c->timer_b.output_level = true;
}

void cia_attach_keyboard(cia *c, c64_keyboard *keyboard) {
    assert(c);

    c->keyboard = keyboard;
}

void cia_step_cycle(cia *c) {
    assert(c);

    cia_reset_timer_output_pulses(c);
    cia_step_timer(c, &c->timer_a, CIA_REG_CONTROL_A, CIA_INTERRUPT_TIMER_A);
    cia_step_timer(c, &c->timer_b, CIA_REG_CONTROL_B, CIA_INTERRUPT_TIMER_B);
    c->cnt_pulse = false;
}

void cia_pulse_cnt(cia *c) {
    assert(c);

    c->cnt_pulse = true;
}

uint8_t cia_read_register(cia *c, uint16_t addr) {
    uint8_t reg;

    assert(c);

    reg = (uint8_t)(addr & 0x0fu);
    switch (reg) {
        case CIA_REG_PORT_A:
            return cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
        case CIA_REG_PORT_B:
            if (c->keyboard) {
                return (uint8_t)(cia_read_port_b_pins(c) &
                    c64_keyboard_read_columns(c->keyboard, cia_keyboard_selected_rows(c)));
            }
            return cia_read_port_b_pins(c);
        case CIA_REG_TIMER_A_LO:
            return (uint8_t)(c->timer_a.counter & 0xffu);
        case CIA_REG_TIMER_A_HI:
            return (uint8_t)(c->timer_a.counter >> 8);
        case CIA_REG_TIMER_B_LO:
            return (uint8_t)(c->timer_b.counter & 0xffu);
        case CIA_REG_TIMER_B_HI:
            return (uint8_t)(c->timer_b.counter >> 8);
        case CIA_REG_ICR: {
            uint8_t flags = (uint8_t)(c->interrupt_flags & CIA_INTERRUPT_SOURCE_MASK);
            uint8_t value = flags;
            c->icr_reads++;
            if ((flags & c->interrupt_mask) != 0) {
                value |= 0x80u;
            }
            c->interrupt_flags &= (uint8_t)~flags;
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
        case CIA_REG_TIMER_A_LO:
            c->registers[reg] = value;
            c->timer_a.latch = (uint16_t)((c->timer_a.latch & 0xff00u) | value);
            if ((c->registers[CIA_REG_CONTROL_A] & CIA_CONTROL_START) == 0) {
                cia_reload_timer(&c->timer_a);
            }
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
            c->timer_b.latch = (uint16_t)((c->timer_b.latch & 0xff00u) | value);
            if ((c->registers[CIA_REG_CONTROL_B] & CIA_CONTROL_START) == 0) {
                cia_reload_timer(&c->timer_b);
            }
            return;
        case CIA_REG_TIMER_B_HI:
            c->registers[reg] = value;
            c->timer_b.latch = (uint16_t)((c->timer_b.latch & 0x00ffu) | ((uint16_t)value << 8));
            if ((c->registers[CIA_REG_CONTROL_B] & CIA_CONTROL_START) == 0) {
                cia_reload_timer(&c->timer_b);
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
            return;
        case CIA_REG_CONTROL_A:
            c->registers[reg] = (uint8_t)(value & (uint8_t)~CIA_CONTROL_FORCE_LOAD);
            if ((value & CIA_CONTROL_FORCE_LOAD) != 0) {
                cia_reload_timer(&c->timer_a);
            }
            return;
        case CIA_REG_CONTROL_B:
            c->registers[reg] = (uint8_t)(value & (uint8_t)~CIA_CONTROL_FORCE_LOAD);
            if ((value & CIA_CONTROL_FORCE_LOAD) != 0) {
                cia_reload_timer(&c->timer_b);
            }
            return;
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
            return cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
        case CIA_REG_PORT_B:
            if (c->keyboard) {
                return (uint8_t)(cia_read_port_b_pins(c) &
                    c64_keyboard_read_columns(c->keyboard, cia_keyboard_selected_rows(c)));
            }
            return cia_read_port_b_pins(c);
        case CIA_REG_TIMER_A_LO:
            return (uint8_t)(c->timer_a.counter & 0xffu);
        case CIA_REG_TIMER_A_HI:
            return (uint8_t)(c->timer_a.counter >> 8);
        case CIA_REG_TIMER_B_LO:
            return (uint8_t)(c->timer_b.counter & 0xffu);
        case CIA_REG_TIMER_B_HI:
            return (uint8_t)(c->timer_b.counter >> 8);
        case CIA_REG_ICR:
            flags = (uint8_t)(c->interrupt_flags & CIA_INTERRUPT_SOURCE_MASK);
            if ((flags & c->interrupt_mask) != 0) {
                return (uint8_t)(flags | 0x80u);
            }
            return flags;
        default:
            return c->registers[reg];
    }
}

uint8_t cia_read_port_a_pins(const cia *c) {
    assert(c);

    return cia_read_port(c->registers[CIA_REG_PORT_A], c->registers[CIA_REG_DDRA]);
}

bool cia_irq_pending(const cia *c) {
    assert(c);

    return (c->interrupt_flags & c->interrupt_mask & CIA_INTERRUPT_SOURCE_MASK) != 0;
}
