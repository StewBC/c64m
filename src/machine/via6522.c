#include "via6522.h"

#include <string.h>

void via6522_init(via6522 *v) {
    memset(v, 0, sizeof(*v));
}

void via6522_reset(via6522 *v) {
    memset(v, 0, sizeof(*v));
}

static uint8_t port_read(uint8_t output_reg, uint8_t ddr, uint8_t pin_in) {
    return (uint8_t)((output_reg & ddr) | (pin_in & (uint8_t)~ddr));
}

uint8_t via6522_read(via6522 *v, uint8_t reg) {
    switch (reg & 0x0Fu) {
        case 0:
            return port_read(v->orb, v->ddrb, v->port_b_in);
        case 1:
            v->ifr &= (uint8_t)~0x03u;
            return port_read(v->ora, v->ddra, v->port_a_in);
        case 2:
            return v->ddrb;
        case 3:
            return v->ddra;
        case 4:
            v->ifr &= (uint8_t)~0x40u;
            return (uint8_t)(v->t1_counter & 0xFFu);
        case 5:
            return (uint8_t)(v->t1_counter >> 8);
        case 6:
            return (uint8_t)(v->t1_latch & 0xFFu);
        case 7:
            return (uint8_t)(v->t1_latch >> 8);
        case 8:
            v->ifr &= (uint8_t)~0x20u;
            return (uint8_t)(v->t2_counter & 0xFFu);
        case 9:
            return (uint8_t)(v->t2_counter >> 8);
        case 10:
            return v->sr;
        case 11:
            return v->acr;
        case 12:
            return v->pcr;
        case 13:
            return (uint8_t)(v->ifr | (((v->ifr & v->ier & 0x7Fu) != 0u) ? 0x80u : 0x00u));
        case 14:
            return (uint8_t)(v->ier | 0x80u);
        case 15:
            return port_read(v->ora, v->ddra, v->port_a_in);
        default:
            return 0xFFu;
    }
}

void via6522_write(via6522 *v, uint8_t reg, uint8_t value) {
    switch (reg & 0x0Fu) {
        case 0:
            v->orb = value;
            break;
        case 1:
            v->ora = value;
            v->ifr &= (uint8_t)~0x03u;
            break;
        case 2:
            v->ddrb = value;
            break;
        case 3:
            v->ddra = value;
            break;
        case 4:
            v->t1_latch = (uint16_t)((v->t1_latch & 0xFF00u) | value);
            break;
        case 5:
            v->t1_latch = (uint16_t)((v->t1_latch & 0x00FFu) | ((uint16_t)value << 8));
            v->t1_counter = v->t1_latch;
            v->ifr &= (uint8_t)~0x40u;
            v->t1_running = 1;
            break;
        case 6:
            v->t1_latch = (uint16_t)((v->t1_latch & 0xFF00u) | value);
            break;
        case 7:
            v->t1_latch = (uint16_t)((v->t1_latch & 0x00FFu) | ((uint16_t)value << 8));
            break;
        case 8:
            v->t2_latch_low = value;
            break;
        case 9:
            v->t2_counter = (uint16_t)(((uint16_t)value << 8) | v->t2_latch_low);
            v->ifr &= (uint8_t)~0x20u;
            v->t2_running = 1;
            break;
        case 10:
            v->sr = value;
            break;
        case 11:
            v->acr = value;
            break;
        case 12:
            v->pcr = value;
            break;
        case 13:
            v->ifr &= (uint8_t)~(value & 0x7Fu);
            break;
        case 14:
            if (value & 0x80u) {
                v->ier |= (uint8_t)(value & 0x7Fu);
            } else {
                v->ier &= (uint8_t)~(value & 0x7Fu);
            }
            break;
        case 15:
            v->ora = value;
            break;
        default:
            break;
    }
}

void via6522_step(via6522 *v) {
    if (v->t1_running) {
        v->t1_counter--;
        if (v->t1_counter == 0xFFFFu) {
            v->ifr |= 0x40u;
            if (v->acr & 0x80u) {
                v->t1_pb7_state ^= 1;
            }
            if (v->acr & 0x40u) {
                v->t1_counter = v->t1_latch;
            } else {
                v->t1_running = 0;
            }
        }
    }
    if (v->t2_running) {
        v->t2_counter--;
        if (v->t2_counter == 0xFFFFu) {
            v->ifr |= 0x20u;
            v->t2_running = 0;
        }
    }
}

int via6522_irq_pending(via6522 *v) {
    return (v->ifr & v->ier & 0x7Fu) != 0;
}

void via6522_set_port_a_inputs(via6522 *v, uint8_t inputs) {
    v->port_a_in = inputs;
}

void via6522_set_port_b_inputs(via6522 *v, uint8_t inputs) {
    v->port_b_in = inputs;
}

void via6522_set_ca1(via6522 *v, uint8_t level) {
    uint8_t active_high = (uint8_t)(v->pcr & 0x01u);
    uint8_t edge_fired = active_high
        ? (uint8_t)(!v->ca1_last && level)
        : (uint8_t)(v->ca1_last && !level);
    v->ca1_last = level ? 1u : 0u;
    if (edge_fired) {
        v->ifr |= 0x02u;
    }
}
