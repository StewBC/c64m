// C64 6510 CPU helpers, adapted from the a2m cycle-accurate NMOS 6502 core.

#pragma once


#include <assert.h>

#define CYCLE(m)     do { (m)->cpu.cycles++; } while(0)

static inline uint8_t read_from_memory(C6510 *m, uint16_t address) {
    return m->read(m->user, address);
}

static inline void write_to_memory(C6510 *m, uint16_t address, uint8_t value) {
    m->write(m->user, address, value);
}

static inline uint8_t read_from_memory_debug(C6510 *m, uint16_t address) {
    return read_from_memory(m, address);
}

static inline uint16_t read_from_memory_debug_16(C6510 *m, uint16_t address) {
    uint8_t lo = read_from_memory_debug(m, address);
    uint8_t hi = read_from_memory_debug(m, (uint16_t)(address + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}
// Setters
static inline void set_register_to_value(C6510 *m, uint8_t *reg, uint8_t value) {
    *reg = value;
    m->cpu.N = *reg & 0x80 ? 1 : 0;
    m->cpu.Z = *reg ? 0 : 1;
}

// Helper Functions
static inline void add_value_to_accumulator(C6510 *m, uint8_t value) {
    uint8_t a = m->cpu.A;
    uint8_t c = m->cpu.C;
    m->cpu.scratch_16 = m->cpu.A + value + c;
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.scratch_lo = (a & 0x0F) + (value & 0x0F) + c;
    m->cpu.V = ((a ^ m->cpu.A) & ~(a ^ value) & 0x80) != 0 ? 1 : 0;
    m->cpu.C = m->cpu.scratch_hi;
    if(m->cpu.D) {
        m->cpu.scratch_hi = (a >> 4) + (value >> 4);
        if(m->cpu.scratch_lo > 9) {
            m->cpu.scratch_lo += 6;
            m->cpu.scratch_hi++;
        }
        {
            uint8_t intermediate = (m->cpu.scratch_hi << 4) | (m->cpu.scratch_lo & 0x0F);
            m->cpu.N = intermediate & 0x80 ? 1 : 0;
            m->cpu.V = ((a ^ intermediate) & ~(a ^ value) & 0x80) != 0 ? 1 : 0;
        }
        if(m->cpu.scratch_hi > 9) {
            m->cpu.scratch_hi += 6;
            m->cpu.C = 1;
        }
        m->cpu.A = (m->cpu.scratch_hi << 4) | (m->cpu.scratch_lo & 0x0F);
        if(m->cpu.class == CPU_65c02) {
            read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
            set_register_to_value(m, &m->cpu.A, m->cpu.A);
        }
    }
}

static inline void compare_bytes(C6510 *m, uint8_t lhs, uint8_t rhs) {
    m->cpu.Z = (lhs == rhs) ? 1 : 0;
    m->cpu.C = (lhs >= rhs) ? 1 : 0;
    m->cpu.N = ((lhs - rhs) & 0x80) ? 1 : 0;
}

static inline uint8_t pull(C6510 *m) {
    if(++m->cpu.sp >= 0x200) {
        m->cpu.sp = 0x100;
    }
    return read_from_memory(m, m->cpu.sp);
}

static inline void push(C6510 *m, uint8_t value) {
    write_to_memory(m, m->cpu.sp, value);
    if(--m->cpu.sp < 0x100) {
        m->cpu.sp += 0x100;
    }
}

static inline void subtract_value_from_accumulator(C6510 *m, uint8_t value) {
    uint8_t a = m->cpu.A;
    m->cpu.C ^= 1;
    m->cpu.scratch_16 = a - value - m->cpu.C;

    if(m->cpu.class == CPU_6502) {
        set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
        m->cpu.V = ((a ^ value) & (a ^ m->cpu.A) & 0x80) != 0 ? 1 : 0;
        if(m->cpu.D) {
            uint8_t lo = (a & 0x0F) - (value & 0x0F) - m->cpu.C;
            uint8_t hi = (a >> 4) - (value >> 4);
            if(lo & 0x10) {
                lo -= 6;
                hi--;
            }
            if(hi & 0xF0) {
                hi -= 6;
            }
            m->cpu.A = (hi << 4) | (lo & 0x0F);
        }
        m->cpu.C = m->cpu.scratch_16 < 0x100 ? 1 : 0;
    } else {
        m->cpu.A = m->cpu.scratch_lo;
        m->cpu.V = ((a ^ m->cpu.A) & (a ^ value) & 0x80) ? 1 : 0;
        if(m->cpu.D) {
            if((a & 0x0F) < ((value & 0x0F) + m->cpu.C)) {
                m->cpu.scratch_lo -= 0x06;
            }
            if(a < value + m->cpu.C) {
                m->cpu.scratch_lo -= 0x60;
            }
            read_from_memory(m, m->cpu.address_16);
            CYCLE(m);
        }
        m->cpu.C = m->cpu.scratch_hi ? 0 : 1;
        set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    }
}

// Stage Helpers
static inline void ah_from_stack(C6510 *m) {
    m->cpu.address_hi = pull(m);
    CYCLE(m);
}

static inline void ah_read_a16_sl2al(C6510 *m) {
    m->cpu.address_lo++;
    m->cpu.address_hi = read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo = m->cpu.scratch_lo;
    CYCLE(m);
}

static inline void ah_read_pc(C6510 *m) {
    m->cpu.address_hi = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void al_from_stack(C6510 *m) {
    m->cpu.address_lo = pull(m);
    CYCLE(m);
}

static inline void al_read_pc(C6510 *m) {
    m->cpu.address_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.address_hi = 0;
    m->cpu.pc++;
    CYCLE(m);
}

static inline void branch(C6510 *m) {
    read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.scratch_lo;
    if((lo + (int8_t)m->cpu.scratch_lo) & 0x100) {
        read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
    }
    m->cpu.pc += (int8_t)m->cpu.scratch_lo;
}

// static inline void brk_pc(C6510 *m) {
//     m->cpu.pc = 0xFFFE;
//     al_read_pc(m);
// }

static inline void p_from_stack(C6510 *m) {
    m->cpu.flags = (pull(m) & ~0b00010000) | 0b00100000;
    CYCLE(m);
}

static inline void pc_hi_to_stack(C6510 *m) {
    push(m, (m->cpu.pc >> 8) & 0xFF);
    CYCLE(m);
}

static inline void pc_lo_to_stack(C6510 *m) {
    push(m, m->cpu.pc & 0xFF);
    CYCLE(m);
}

static inline void read_a16_ind_x(C6510 *m) {
    read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo += m->cpu.X;
    CYCLE(m);
}

static inline void read_a16_ind_y(C6510 *m) {
    read_from_memory(m, m->cpu.address_16);
    m->cpu.address_lo += m->cpu.Y;
    CYCLE(m);
}

static inline void read_sp(C6510 *m) {
    read_from_memory(m, m->cpu.sp);
    CYCLE(m);
}

static inline void sl_read_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
}

static inline void sl_write_a16(C6510 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

// pipelines
static inline void a(C6510 *m) {
    al_read_pc(m);
    ah_read_pc(m);
}

static inline void ar(C6510 *m) {
    a(m);
    sl_read_a16(m);
}

static inline void arw(C6510 *m) {
    a(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void aix(C6510 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aipxr(C6510 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void aixrr(C6510 *m) {
    aix(m);
    sl_read_a16(m);
    sl_read_a16(m);
}

static inline void aipxrw(C6510 *m) {
    aipxr(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void aiy(C6510 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void aiyr(C6510 *m) {
    a(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void aiyr_und(C6510 *m) {
    a(m);
    m->cpu.scratch_hi = m->cpu.address_hi + 1;
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.page_fault = m->cpu.address_lo < lo;
    if(m->cpu.page_fault) {
        m->cpu.address_hi++;
    }
}

static inline void aipxr_und(C6510 *m) {
    a(m);
    m->cpu.scratch_hi = m->cpu.address_hi + 1;
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.X;
    read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.page_fault = m->cpu.address_lo < lo;
    if(m->cpu.page_fault) {
        m->cpu.address_hi++;
    }
}

static inline void mix(C6510 *m) {
    al_read_pc(m);
    read_a16_ind_x(m);
}

static inline void mixa(C6510 *m) {
    mix(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
}

static inline void mixrw(C6510 *m) {
    mix(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void miy(C6510 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.address_lo < lo) {
        if(m->cpu.class == CPU_6502) {
            read_from_memory(m, m->cpu.address_16);
        } else {
            read_from_memory(m, m->cpu.pc - 1);
        }
        m->cpu.address_hi++;
        CYCLE(m);
    }
}

static inline void miyr(C6510 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    if(m->cpu.class == CPU_6502) {
        read_from_memory(m, m->cpu.address_16);
    } else {
        read_from_memory(m, m->cpu.pc - 1);
    }
    CYCLE(m);
    if(m->cpu.address_lo < lo) {
        m->cpu.address_hi++;
    }
}

static inline void miyr_und(C6510 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
    m->cpu.scratch_hi = m->cpu.address_hi + 1;
    uint8_t lo = m->cpu.address_lo;
    m->cpu.address_lo += m->cpu.Y;
    read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.page_fault = m->cpu.address_lo < lo;
    if(m->cpu.page_fault) {
        m->cpu.address_hi++;
    }
}

static inline void miz(C6510 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    ah_read_a16_sl2al(m);
}

static inline void mizy(C6510 *m) {
    al_read_pc(m);
    read_a16_ind_y(m);
}

static inline void mrw(C6510 *m) {
    al_read_pc(m);
    sl_read_a16(m);
    if(m->cpu.class == CPU_6502) {
        sl_write_a16(m);
    } else {
        sl_read_a16(m);
    }
}

static inline void read_pc_1(C6510 *m) {
    read_from_memory(m, m->cpu.pc - 1);
    CYCLE(m);
}

static inline void read_pc(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    CYCLE(m);
}

static inline void unimplemented(C6510 *m) {
    m->cpu.cycles = -1;
}

static inline void unstable_store_a16(C6510 *m, uint8_t value) {
    if(m->cpu.page_fault) {
        m->cpu.address_hi &= value;
    }
    write_to_memory(m, m->cpu.address_16, value);
    CYCLE(m);
}

// Pipeline selectors
static inline void aixr_sel(C6510 *m) {
    if(m->cpu.class == CPU_6502) {
        aipxrw(m);
    } else {
        aixrr(m);
    }
}

// IRQ
static inline void c6510_irq(C6510 *m) {
    m->cpu.pc = 0xFFFE;
    a(m);
    m->cpu.pc = m->cpu.address_16;
    m->cpu.I = 1;
    if(m->cpu.class == CPU_65c02) {
        m->cpu.D = 0;
    }
}

static inline uint8_t c6510_irq_pending(C6510 *m) {
    return m->irq_pending ? m->irq_pending(m->user) : 0;
}

static inline uint8_t c6510_take_irq_if_pending(C6510 *m) {
    uint8_t pending = c6510_irq_pending(m);
    uint8_t irq_disable = m->cpu.I;
    if(m->cpu.irq_defer) {
        irq_disable = m->cpu.irq_defer_i;
    }
    if(!pending || irq_disable) {
        if(m->cpu.irq_defer) {
            m->cpu.irq_defer = 0;
        }
        return 0;
    }
    if(m->cpu.irq_defer) {
        m->cpu.irq_defer = 0;
    }

    m->cpu.opcode_pc = m->cpu.pc;
    read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    read_sp(m);
    pc_hi_to_stack(m);
    pc_lo_to_stack(m);
    push(m, (m->cpu.flags & (uint8_t)~0b00010000) | 0b00100000);
    CYCLE(m);
    c6510_irq(m);
    return 1;
}

// Instructions
static inline void adc_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void adc_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
    if(m->cpu.class == CPU_65c02 && m->cpu.D) {
        m->cpu.address_16 = 0x56;
    }
    add_value_to_accumulator(m, m->cpu.scratch_lo);
}

static inline void ahx_a16(C6510 *m) {
    unstable_store_a16(m, m->cpu.A & m->cpu.X & m->cpu.scratch_hi);
}

static inline void anc_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.C = m->cpu.N;
    CYCLE(m);
}

static inline void alr_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    m->cpu.A &= m->cpu.scratch_lo;
    m->cpu.C = m->cpu.A & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A >> 1);
    CYCLE(m);
}

static inline void and_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void and_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void arr_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    uint8_t value = m->cpu.A & m->cpu.scratch_lo;
    uint8_t result = (value >> 1) | (m->cpu.C << 7);
    set_register_to_value(m, &m->cpu.A, result);
    m->cpu.C = result & 0x40 ? 1 : 0;
    m->cpu.V = ((result >> 6) ^ (result >> 5)) & 0x01;
    if(m->cpu.D) {
        if(((value & 0x0F) + (value & 0x01)) > 5) {
            m->cpu.A = (m->cpu.A & 0xF0) | ((m->cpu.A + 0x06) & 0x0F);
        }
        if((value + (value & 0x10)) > 0x50) {
            m->cpu.A += 0x60;
            m->cpu.C = 1;
        } else {
            m->cpu.C = 0;
        }
    }
    CYCLE(m);
}

static inline void asl_a(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.C = m->cpu.A & 0x80 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A <<= 1);
    CYCLE(m);
}

static inline void asl_a16(C6510 *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x80 ? 1 : 0;
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo << 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void axs_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    uint8_t value = m->cpu.A & m->cpu.X;
    compare_bytes(m, value, m->cpu.scratch_lo);
    m->cpu.X = value - m->cpu.scratch_lo;
    CYCLE(m);
}

static inline void bcc(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.C) {
        branch(m);
    }
}

static inline void bcs(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.C) {
        branch(m);
    }
}

static inline void beq(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.Z) {
        branch(m);
    }
}

static inline void bit_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.flags &= 0b00111111;
    m->cpu.flags |= (m->cpu.scratch_lo & 0b11000000);
    CYCLE(m);
}

static inline void bit_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0 ? -1 : 0;
    m->cpu.pc++;
}

static inline void bmi(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.N) {
        branch(m);
    }
}

static inline void bne(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.Z) {
        branch(m);
    }
}

static inline void bpl(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.N) {
        branch(m);
    }
}

static inline void bra(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    branch(m);
}

static inline void bvc(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(!m->cpu.V) {
        branch(m);
    }
}

static inline void bvs(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    CYCLE(m);
    m->cpu.address_16 = ++m->cpu.pc;
    if(m->cpu.V) {
        branch(m);
    }
}

static inline void c6510_brk(C6510 *m) {
    m->cpu.pc = 0xFFFE;
    a(m);
    m->cpu.pc = m->cpu.address_16;
    if(m->cpu.class == CPU_6502) {
        // Interrupt flag on at break
        m->cpu.flags |= 0b00000100;
    } else {
        m->cpu.flags &= ~0b00001000;
        if(m->cpu.flags & 0b00100000) {
            // Interrupt flag on at break, if '-' flag is set
            m->cpu.flags |= 0b00000100;
        }
    }
}

static inline void clc(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.C = 0;
    CYCLE(m);
}

static inline void cld(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.D = 0;
    CYCLE(m);
}

static inline void cli(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.irq_defer = 1;
    m->cpu.irq_defer_i = 1;
    m->cpu.I = 0;
    CYCLE(m);
}

static inline void clv(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.V = 0;
    CYCLE(m);
}

static inline void cmp_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cmp_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void cpx_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cpx_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    compare_bytes(m, m->cpu.X, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void cpy_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    compare_bytes(m, m->cpu.Y, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void cpy_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    compare_bytes(m, m->cpu.Y, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void dcp_a16(C6510 *m) {
    m->cpu.scratch_lo--;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    compare_bytes(m, m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void dea(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A - 1);
    CYCLE(m);
}

static inline void dec_a16(C6510 *m) {
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo - 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void dex(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.X - 1);
    CYCLE(m);
}

static inline void dey(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.Y - 1);
    CYCLE(m);
}

static inline void eor_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void eor_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void inc_a16(C6510 *m) {
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo + 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void ina(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A + 1);
    CYCLE(m);
}

static inline void inx(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.X + 1);
    CYCLE(m);
}

static inline void iny(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.Y + 1);
    CYCLE(m);
}

static inline void isc_a16(C6510 *m) {
    m->cpu.scratch_lo++;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void jam(C6510 *m) {
    read_pc(m);
    read_from_memory(m, 0xFFFF);
    CYCLE(m);
    read_from_memory(m, 0xFFFE);
    CYCLE(m);
    read_from_memory(m, 0xFFFE);
    CYCLE(m);
    for(int i = 0; i < 6; i++) {
        read_from_memory(m, 0xFFFF);
        CYCLE(m);
    }
}

static inline void jmp_a16(C6510 *m) {
    m->cpu.pc = m->cpu.address_16;
}

static inline void jmp_ind(C6510 *m) {
    m->cpu.address_lo++;
    m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    if(m->cpu.class == CPU_65c02) {
        if(!m->cpu.address_lo) {
            m->cpu.address_hi++;
        }
        m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
        CYCLE(m);
    }
    m->cpu.pc = m->cpu.scratch_16;
}

static inline void jmp_ind_x(C6510 *m) {
    a(m);
    read_from_memory(m, m->cpu.pc - 2);
    m->cpu.address_16 += m->cpu.X;
    CYCLE(m);
    sl_read_a16(m);
    m->cpu.address_16++;
    m->cpu.scratch_hi = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    m->cpu.pc = m->cpu.scratch_16;
}

static inline void jsr_a16(C6510 *m) {
    ah_read_pc(m);
    m->cpu.pc = m->cpu.address_16;
}

static inline void las_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16) & (m->cpu.sp & 0xFF);
    m->cpu.A = m->cpu.scratch_lo;
    m->cpu.sp = 0x100 + m->cpu.scratch_lo;
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lax_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    m->cpu.A = m->cpu.scratch_lo;
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lax_imm_und(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    m->cpu.A = (m->cpu.A | 0xEE) & m->cpu.scratch_lo;
    set_register_to_value(m, &m->cpu.X, m->cpu.A);
    CYCLE(m);
}

static inline void lda_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void lda_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void ldx_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ldx_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.X, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void ldy_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.Y, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ldy_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.Y, m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void lsr_a(C6510 *m) {
    read_from_memory(m, m->cpu.pc);
    m->cpu.C = m->cpu.A & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.A, m->cpu.A >>= 1);
    CYCLE(m);
}

static inline void lsr_a16(C6510 *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x01 ? 1 : 0;
    set_register_to_value(m, &m->cpu.scratch_hi, m->cpu.scratch_lo >> 1);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_hi);
    CYCLE(m);
}

static inline void ora_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void ora_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    m->cpu.pc++;
    CYCLE(m);
}

static inline void phx(C6510 *m) {
    push(m, m->cpu.X);
    CYCLE(m);
}

static inline void phy(C6510 *m) {
    push(m, m->cpu.Y);
    CYCLE(m);
}

static inline void pla(C6510 *m) {
    set_register_to_value(m, &m->cpu.A, pull(m));
    CYCLE(m);
}

static inline void plp(C6510 *m) {
    m->cpu.flags = (pull(m) & ~0b00010000) | 0b00100000;            // Break flag off, but - flag on
    CYCLE(m);
}

static inline void plx(C6510 *m) {
    set_register_to_value(m, &m->cpu.X, pull(m));
    CYCLE(m);
}

static inline void ply(C6510 *m) {
    set_register_to_value(m, &m->cpu.Y, pull(m));
    CYCLE(m);
}

static inline void pha(C6510 *m) {
    push(m, m->cpu.A);
    CYCLE(m);
}

static inline void php(C6510 *m) {
    // Break flag on flags push
    push(m, m->cpu.flags | 0b00010000);
    CYCLE(m);
}

static inline void rla_a16(C6510 *m) {
    uint8_t c = m->cpu.scratch_lo & 0x80;
    m->cpu.scratch_lo = (m->cpu.scratch_lo << 1) | m->cpu.C;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    set_register_to_value(m, &m->cpu.A, m->cpu.A & m->cpu.scratch_lo);
    m->cpu.C = c ? 1 : 0;
    CYCLE(m);
}

static inline void rol_a(C6510 *m) {
    uint8_t c = m->cpu.A & 0x80;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A << 1) | m->cpu.C);
    m->cpu.C = c ? 1 : 0;
}

static inline void rol_a16(C6510 *m) {
    uint8_t c = m->cpu.scratch_lo & 0x80;
    set_register_to_value(m, &m->cpu.scratch_lo, (m->cpu.scratch_lo << 1) | m->cpu.C);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c ? 1 : 0;
    CYCLE(m);
}

static inline void ror_a(C6510 *m) {
    uint8_t c = m->cpu.A & 0x01;
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, (m->cpu.A >> 1) | (m->cpu.C << 7));
    m->cpu.C = c;
}

static inline void ror_a16(C6510 *m) {
    uint8_t c = m->cpu.scratch_lo & 0x01;
    set_register_to_value(m, &m->cpu.scratch_lo, (m->cpu.scratch_lo >> 1) | (m->cpu.C << 7));
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c;
    CYCLE(m);
}

static inline void rti(C6510 *m) {
    ah_from_stack(m);
    m->cpu.pc = m->cpu.address_16;
}

static inline void rts(C6510 *m) {
    m->cpu.pc = m->cpu.address_16;
    al_read_pc(m);
}

static inline void sbc_a16(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.address_16);
    CYCLE(m);
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);
}

static inline void sbc_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    CYCLE(m);
    if(m->cpu.class == CPU_65c02 && m->cpu.D) {
        m->cpu.address_16 = 0;
    }
    subtract_value_from_accumulator(m, m->cpu.scratch_lo);

}

static inline void sec(C6510 *m) {
    read_pc(m);
    m->cpu.C = 1;
}

static inline void sed(C6510 *m) {
    read_pc(m);
    m->cpu.D = 1;
}

static inline void sei(C6510 *m) {
    read_pc(m);
    m->cpu.irq_defer = 1;
    m->cpu.irq_defer_i = 0;
    m->cpu.I = 1;
}

static inline void sta_a16(C6510 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.A);
    CYCLE(m);
}

static inline void sax_a16(C6510 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.A & m->cpu.X);
    CYCLE(m);
}

static inline void slo_a16(C6510 *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x80 ? 1 : 0;
    m->cpu.scratch_lo <<= 1;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    set_register_to_value(m, &m->cpu.A, m->cpu.A | m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void sre_a16(C6510 *m) {
    m->cpu.C = m->cpu.scratch_lo & 0x01 ? 1 : 0;
    m->cpu.scratch_lo >>= 1;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    set_register_to_value(m, &m->cpu.A, m->cpu.A ^ m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void rra_a16(C6510 *m) {
    uint8_t c = m->cpu.scratch_lo & 0x01;
    m->cpu.scratch_lo = (m->cpu.scratch_lo >> 1) | (m->cpu.C << 7);
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    m->cpu.C = c;
    add_value_to_accumulator(m, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void stx_a16(C6510 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.X);
    CYCLE(m);
}

static inline void sty_a16(C6510 *m) {
    write_to_memory(m, m->cpu.address_16, m->cpu.Y);
    CYCLE(m);
}

static inline void stz_a16(C6510 *m, uint8_t value) {
    write_to_memory(m, m->cpu.address_16, value);
    CYCLE(m);
}

static inline void shs_a16(C6510 *m) {
    m->cpu.sp = 0x100 + (m->cpu.A & m->cpu.X);
    unstable_store_a16(m, m->cpu.sp & m->cpu.scratch_hi);
}

static inline void shy_a16(C6510 *m) {
    unstable_store_a16(m, m->cpu.Y & m->cpu.scratch_hi);
}

static inline void shx_a16(C6510 *m) {
    unstable_store_a16(m, m->cpu.X & m->cpu.scratch_hi);
}

static inline void tax(C6510 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.A);
}

static inline void tay(C6510 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.Y, m->cpu.A);
}

static inline void trb(C6510 *m) {
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0;
    m->cpu.scratch_lo = (m->cpu.A ^ 0xff) & m->cpu.scratch_lo;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void tsb(C6510 *m) {
    m->cpu.Z = (m->cpu.A & m->cpu.scratch_lo) == 0;
    m->cpu.scratch_lo |= m->cpu.A;
    write_to_memory(m, m->cpu.address_16, m->cpu.scratch_lo);
    CYCLE(m);
}

static inline void tsx(C6510 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.X, m->cpu.sp - 0x100);
}

static inline void txa(C6510 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.X);
}

static inline void txs(C6510 *m) {
    read_pc(m);
    m->cpu.sp = 0x100 + m->cpu.X;
}

static inline void tya(C6510 *m) {
    read_pc(m);
    set_register_to_value(m, &m->cpu.A, m->cpu.Y);
}

static inline void xaa_imm(C6510 *m) {
    m->cpu.scratch_lo = read_from_memory(m, m->cpu.pc);
    m->cpu.pc++;
    set_register_to_value(m, &m->cpu.A, (m->cpu.A | 0xEE) & m->cpu.X & m->cpu.scratch_lo);
    CYCLE(m);
}
