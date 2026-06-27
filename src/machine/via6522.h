#pragma once

#include <stdint.h>

typedef struct via6522 {
    uint8_t  ora;
    uint8_t  orb;
    uint8_t  ddra;
    uint8_t  ddrb;
    uint8_t  port_a_in;
    uint8_t  port_b_in;
    uint16_t t1_counter;
    uint16_t t1_latch;
    int      t1_running;
    int      t1_pb7_state;
    uint16_t t2_counter;
    uint8_t  t2_latch_low;
    int      t2_running;
    uint8_t  sr;
    uint8_t  acr;
    uint8_t  pcr;
    uint8_t  ifr;
    uint8_t  ier;
    uint8_t  ca1_last;
} via6522;

void    via6522_init(via6522 *v);
void    via6522_reset(via6522 *v);
uint8_t via6522_read(via6522 *v, uint8_t reg);
void    via6522_write(via6522 *v, uint8_t reg, uint8_t value);
void    via6522_step(via6522 *v);
int     via6522_irq_pending(via6522 *v);
void    via6522_set_port_a_inputs(via6522 *v, uint8_t inputs);
void    via6522_set_port_b_inputs(via6522 *v, uint8_t inputs);
void    via6522_set_ca1(via6522 *v, uint8_t level);
