#include "cpu.h"

#include <assert.h>
#include <string.h>

void c6510_init(C6510 *m, void *user, c6510_read_fn read, c6510_write_fn write) {
    assert(m);
    assert(read);
    assert(write);

    memset(m, 0, sizeof(*m));
    m->user = user;
    m->read = read;
    m->write = write;
    m->cpu.class = CPU_6502;
    m->cpu.sp = 0x100;
}

void c6510_set_irq_pending_callback(C6510 *m, c6510_irq_pending_fn irq_pending) {
    assert(m);
    m->irq_pending = irq_pending;
}

void c6510_set_trace_callback(C6510 *m, c6510_trace_fn trace) {
    assert(m);
    m->trace = trace;
}

void c6510_reset(C6510 *m) {
    assert(m);
    assert(m->read);

    m->cpu.pc = (uint16_t)(m->read(m->user, 0xfffc) | ((uint16_t)m->read(m->user, 0xfffd) << 8));
    m->cpu.sp = 0x100;
    m->cpu.I = 1;
    m->cpu.D = 0;
    m->cpu.B = 0;
    m->cpu.E = 1;
    m->cpu.irq_defer = 0;
    m->cpu.irq_defer_i = 0;
}
