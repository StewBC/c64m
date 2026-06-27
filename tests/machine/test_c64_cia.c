#include "c64.h"
#include "cia.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    CIA_INTERRUPT_TIMER_A = 0x01,
    CIA_INTERRUPT_TIMER_B = 0x02,
    CIA_INTERRUPT_TOD = 0x04,
    TEST_RESET_VECTOR = 0xe000,
    TEST_IRQ_VECTOR = 0xe010,
    TEST_NMI_VECTOR = 0xe020,
};

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_false(const char *name, bool value) {
    if (value) {
        fprintf(stderr, "%s: expected false\n", name);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %04x, got %04x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %llu, got %llu\n", name, (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

static void expect_u8_less_than(const char *name, uint8_t actual, uint8_t limit) {
    if (actual >= limit) {
        fprintf(stderr, "%s: expected %02x < %02x\n", name, actual, limit);
        exit(1);
    }
}

static void build_roms(c64_rom_set *roms) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffc] = (uint8_t)(TEST_RESET_VECTOR & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(TEST_RESET_VECTOR >> 8);
    roms->kernal[0x1ffe] = (uint8_t)(TEST_IRQ_VECTOR & 0xff);
    roms->kernal[0x1fff] = (uint8_t)(TEST_IRQ_VECTOR >> 8);
    roms->kernal[0x1ffa] = (uint8_t)(TEST_NMI_VECTOR & 0xff);
    roms->kernal[0x1ffb] = (uint8_t)(TEST_NMI_VECTOR >> 8);
    roms->kernal[TEST_RESET_VECTOR - 0xe000] = 0xea;
}

static void reset_machine(c64_t *machine) {
    c64_rom_set roms;
    char error[256];

    build_roms(&roms);
    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void reset_machine_with_kernal_code(c64_t *machine, const uint8_t *code, size_t code_size) {
    c64_rom_set roms;
    char error[256];
    size_t i;

    build_roms(&roms);
    for (i = 0; i < code_size; ++i) {
        roms.kernal[TEST_RESET_VECTOR - 0xe000u + i] = code[i];
    }

    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void step_cia_cycles(cia *c, size_t count) {
    size_t i;

    for (i = 0; i < count; ++i) {
        cia_step_cycle(c);
    }
}

static void step_machine_instructions(c64_t *machine, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; ++i) {
        expect_true("step instruction", c64_step_instruction(machine, error, sizeof(error)));
    }
}

static void test_cia_reset_and_no_key_ports(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_reset(&c);

    expect_u8("reset port a no keys", 0xff, cia_read_register(&c, CIA_REG_PORT_A));
    expect_u8("reset port b no keys", 0xff, cia_read_register(&c, CIA_REG_PORT_B));
    expect_u16("reset timer a latch", 0, c.timer_a.latch);
    expect_u16("reset timer b latch", 0, c.timer_b.latch);
    expect_false("reset irq", cia_irq_pending(&c));
}

static void test_cia_register_mirroring_and_ports(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_DDRA, 0x0f);
    cia_write_register(&c, CIA_REG_PORT_A, 0x05);

    expect_u8("port output input mix", 0xf5, cia_read_register(&c, CIA_REG_PORT_A));
    expect_u8("mirrored port read", 0xf5, cia_read_register(&c, 0x0010));
}

static void test_cia_timer_and_interrupts(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x02);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x81);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);

    expect_u8("timer loaded low", 0x02, cia_read_register(&c, CIA_REG_TIMER_A_LO));
    cia_step_cycle(&c);
    expect_u8("timer decremented", 0x01, cia_read_register(&c, CIA_REG_TIMER_A_LO));
    cia_step_cycle(&c);
    expect_u8("timer reaches zero", 0x00, cia_read_register(&c, CIA_REG_TIMER_A_LO));
    expect_false("irq not pending before underflow", cia_irq_pending(&c));
    cia_step_cycle(&c);

    expect_true("timer underflow recorded", c.timer_a.underflow);
    expect_true("timer irq pending", cia_irq_pending(&c));
    expect_u8("icr reports timer a irq", 0x81, cia_read_register(&c, CIA_REG_ICR));
    expect_false("icr read clears irq", cia_irq_pending(&c));
    expect_u64("icr read counted", 1, c.icr_reads);
    expect_u64("icr write counted", 1, c.icr_writes);
}

static void test_cia_timer_stopped_timers_do_not_decrement(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x04);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);

    step_cia_cycles(&c, 3);

    expect_u16("stopped timer a unchanged", 0x0004, c.timer_a.counter);
    expect_u16("stopped timer b unchanged", 0x0005, c.timer_b.counter);
}

static void test_cia_timer_force_load_strobe_for_both_timers(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x34);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x12);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x78);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x56);

    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x11);

    expect_u16("force load timer a", 0x1234, c.timer_a.counter);
    expect_u16("force load timer b", 0x5678, c.timer_b.counter);
    expect_u8("cra force-load strobe clears", 0x01, cia_read_register(&c, CIA_REG_CONTROL_A));
    expect_u8("crb force-load strobe clears", 0x01, cia_read_register(&c, CIA_REG_CONTROL_B));
}

static void test_cia_timer_continuous_underflow_reloads_both_timers(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x11);

    cia_step_cycle(&c);
    expect_u16("continuous timer a reaches zero", 0x0000, c.timer_a.counter);
    expect_u16("continuous timer b reaches zero", 0x0000, c.timer_b.counter);
    expect_u8("no source flags before underflow", 0x00, c.interrupt_flags);

    cia_step_cycle(&c);
    expect_u16("continuous timer a reloads", 0x0001, c.timer_a.counter);
    expect_u16("continuous timer b reloads", 0x0001, c.timer_b.counter);
    expect_u8("continuous timer a keeps running", 0x01, cia_read_register(&c, CIA_REG_CONTROL_A));
    expect_u8("continuous timer b keeps running", 0x01, cia_read_register(&c, CIA_REG_CONTROL_B));
    expect_u8("timer source flags set", CIA_INTERRUPT_TIMER_A | CIA_INTERRUPT_TIMER_B, c.interrupt_flags);
}

static void test_cia_timer_oneshot_reloads_and_stops_both_timers(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x19);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x19);

    step_cia_cycles(&c, 2);

    expect_u16("oneshot timer a reloads", 0x0001, c.timer_a.counter);
    expect_u16("oneshot timer b reloads", 0x0001, c.timer_b.counter);
    expect_u8("oneshot timer a stops", 0x08, cia_read_register(&c, CIA_REG_CONTROL_A));
    expect_u8("oneshot timer b stops", 0x08, cia_read_register(&c, CIA_REG_CONTROL_B));
    expect_u8("oneshot source flags set", CIA_INTERRUPT_TIMER_A | CIA_INTERRUPT_TIMER_B, c.interrupt_flags);
}

static void test_cia_zero_latch_does_not_underflow_every_cycle(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x81);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);

    cia_step_cycle(&c);
    expect_false("zero latch does not immediately underflow", cia_irq_pending(&c));
    expect_u16("zero latch decrements", 0xfffe, c.timer_a.counter);
}

static void test_cia_zero_latch_stopped_write_loads_effective_counter(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);

    expect_u16("zero latch stopped write loads timer a effective counter", 0xffff, c.timer_a.counter);
    expect_u16("zero latch stopped write loads timer b effective counter", 0xffff, c.timer_b.counter);
}

static void test_cia_oneshot_stops_after_underflow(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x81);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x19);

    cia_step_cycle(&c);
    cia_step_cycle(&c);
    expect_true("oneshot irq", cia_irq_pending(&c));
    expect_u8("oneshot clears start", 0x08, cia_read_register(&c, CIA_REG_CONTROL_A));
}

static void test_cia_bus_mapping_and_ram_under_io(void) {
    c64_t machine;

    c64_init(&machine);
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc00, 0xa5);
    c64_bus_write(&machine.bus, 0xdd02, 0xff);
    c64_bus_write(&machine.bus, 0xdd00, 0x5a);

    expect_u8("cia1 bus read", 0xa5, c64_bus_read(&machine.bus, 0xdc00));
    expect_u8("cia1 mirror bus read", 0xa5, c64_bus_read(&machine.bus, 0xdc10));
    expect_u8("cia2 bus read", 0x5a, c64_bus_read(&machine.bus, 0xdd00));

    c64_bus_write(&machine.bus, 0x0001, 0x34);
    c64_bus_write(&machine.bus, 0xdc00, 0x11);
    c64_bus_write(&machine.bus, 0xdd00, 0x22);
    expect_u8("cia1 ram under io", 0x11, c64_bus_read(&machine.bus, 0xdc00));
    expect_u8("cia2 ram under io", 0x22, c64_bus_read(&machine.bus, 0xdd00));

    c64_bus_write(&machine.bus, 0x0001, 0x37);
    expect_u8("cia1 preserved while io hidden", 0xa5, c64_bus_read(&machine.bus, 0xdc00));
    expect_u8("cia2 preserved while io hidden", 0x5a, c64_bus_read(&machine.bus, 0xdd00));
}

static void test_cia2_vic_bank_uses_port_pins(void) {
    c64_t machine;

    c64_init(&machine);
    c64_bus_write(&machine.bus, 0xdd00, 0x00);
    expect_u16("cia2 input bank bits default high select bank 0", 0x0000, c64_bus_vic_bank_base(&machine.bus));

    c64_bus_write(&machine.bus, 0xdd02, 0x03);

    c64_bus_write(&machine.bus, 0xdd00, 0x03);
    expect_u16("cia2 bank bits 11 select bank 0", 0x0000, c64_bus_vic_bank_base(&machine.bus));

    c64_bus_write(&machine.bus, 0xdd00, 0x02);
    expect_u16("cia2 bank bits 10 select bank 1", 0x4000, c64_bus_vic_bank_base(&machine.bus));

    c64_bus_write(&machine.bus, 0xdd00, 0x01);
    expect_u16("cia2 bank bits 01 select bank 2", 0x8000, c64_bus_vic_bank_base(&machine.bus));

    c64_bus_write(&machine.bus, 0xdd00, 0x00);
    expect_u16("cia2 bank bits 00 select bank 3", 0xc000, c64_bus_vic_bank_base(&machine.bus));
}

static void test_cia2_iec_lines_release_high(void) {
    c64_t machine;

    c64_init(&machine);
    cia_write_register(&machine.cia2, CIA_REG_DDRA, 0x38);
    cia_write_register(&machine.cia2, CIA_REG_PORT_A, 0x00);

    expect_u8("cia2 iec released lines read high", 0xc7, cia_read_register(&machine.cia2, CIA_REG_PORT_A));
    expect_u8("cia2 debug iec released lines read high", 0xc7, cia_debug_read_register(&machine.cia2, CIA_REG_PORT_A));
}

static void test_cia2_iec_cia_pull_low_lines(void) {
    c64_t machine;

    c64_init(&machine);
    cia_write_register(&machine.cia2, CIA_REG_DDRA, 0x38);

    cia_write_register(&machine.cia2, CIA_REG_PORT_A, 0x08);
    expect_u8("cia2 atn output leaves clk/data high", 0xcf, cia_read_register(&machine.cia2, CIA_REG_PORT_A));

    cia_write_register(&machine.cia2, CIA_REG_PORT_A, 0x10);
    expect_u8("cia2 clk output pulls clk low", 0x97, cia_read_register(&machine.cia2, CIA_REG_PORT_A));

    cia_write_register(&machine.cia2, CIA_REG_PORT_A, 0x20);
    expect_u8("cia2 data output pulls data low", 0x67, cia_read_register(&machine.cia2, CIA_REG_PORT_A));
}

static void test_cia2_iec_external_pull_survives_cia_release(void) {
    c64_t machine;

    c64_init(&machine);
    cia_write_register(&machine.cia2, CIA_REG_DDRA, 0x38);
    cia_write_register(&machine.cia2, CIA_REG_PORT_A, 0x00);
    c64_set_iec_external_pull(&machine, C64_IEC_DATA);

    expect_u8("external data pull reads data low", 0x47, cia_read_register(&machine.cia2, CIA_REG_PORT_A));

    c64_set_iec_external_pull(&machine, 0);
    expect_u8("external data release reads high", 0xc7, cia_read_register(&machine.cia2, CIA_REG_PORT_A));
}

static void test_cia_timer_b_cascade_mode(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));

    /* Timer A: latch=3, force-load + start ($11 = FORCE_LOAD|START) */
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x03);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);

    /* Timer B: latch=5, cascade (INMODE1=1 → bits 5-6 = 10), force-load + start
       CRB $51 = 0101_0001: bit0=START, bit4=FORCE_LOAD, bit6=INMODE1 → mode 10 */
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x51);

    /* Steps 1-3: Timer A counts down, Timer B stays at 5 (no underflow yet) */
    cia_step_cycle(&c);
    expect_u16("tb cascade step1 unchanged", 5, c.timer_b.counter);
    cia_step_cycle(&c);
    expect_u16("tb cascade step2 unchanged", 5, c.timer_b.counter);
    cia_step_cycle(&c);
    expect_u16("tb cascade step3 unchanged", 5, c.timer_b.counter);

    /* Step 4: Timer A underflows (counter was 0), Timer B decrements by 1 */
    cia_step_cycle(&c);
    expect_u16("tb cascade step4 decremented", 4, c.timer_b.counter);
    expect_u16("ta reloaded after underflow", 3, c.timer_a.counter);

    /* Steps 5-7: Timer A counts, Timer B stays at 4 */
    cia_step_cycle(&c);
    cia_step_cycle(&c);
    cia_step_cycle(&c);
    expect_u16("tb cascade step7 unchanged", 4, c.timer_b.counter);

    /* Step 8: Timer A underflows again, Timer B decrements */
    cia_step_cycle(&c);
    expect_u16("tb cascade step8 decremented", 3, c.timer_b.counter);
}

static void test_cia_timer_a_cnt_mode(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x31);

    step_cia_cycles(&c, 3);
    expect_u16("timer a cnt mode ignores phi2", 5, c.timer_a.counter);

    cia_pulse_cnt(&c);
    cia_step_cycle(&c);
    expect_u16("timer a cnt pulse decrements", 4, c.timer_a.counter);
}

static void test_cia_timer_b_cnt_mode(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));

    /* Timer B: latch=5, CNT mode (INMODE0=1 → bits 5-6 = 01), force-load + start
       CRB $31 = 0011_0001: bit0=START, bit4=FORCE_LOAD, bit5=INMODE0 → mode 01 */
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x31);

    step_cia_cycles(&c, 3);
    expect_u16("tb cnt mode ignores phi2", 5, c.timer_b.counter);

    cia_pulse_cnt(&c);
    cia_step_cycle(&c);
    expect_u16("tb cnt pulse decrements", 4, c.timer_b.counter);
}

static void test_cia_timer_b_combined_cascade_and_cnt_mode(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x71);

    step_cia_cycles(&c, 2);
    expect_u16("combined mode ignores timer a underflow without cnt", 5, c.timer_b.counter);

    cia_pulse_cnt(&c);
    cia_step_cycle(&c);
    expect_u16("combined mode ignores cnt without timer a underflow", 5, c.timer_b.counter);

    cia_pulse_cnt(&c);
    cia_step_cycle(&c);
    expect_u16("combined mode counts timer a underflow with cnt", 4, c.timer_b.counter);
}

static void test_cia_pb6_pb7_pulse_outputs(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x13);
    step_cia_cycles(&c, 2);
    expect_u8("pb6 pulse low", 0xbf, cia_read_register(&c, CIA_REG_PORT_B));
    cia_step_cycle(&c);
    expect_u8("pb6 pulse restored high", 0xff, cia_read_register(&c, CIA_REG_PORT_B));

    expect_true("cia reset", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x13);
    step_cia_cycles(&c, 2);
    expect_u8("pb7 pulse low", 0x7f, cia_read_register(&c, CIA_REG_PORT_B));
    cia_step_cycle(&c);
    expect_u8("pb7 pulse restored high", 0xff, cia_read_register(&c, CIA_REG_PORT_B));
}

static void test_cia_pb6_pb7_toggle_outputs(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x17);
    step_cia_cycles(&c, 2);
    expect_u8("pb6 toggle low", 0xbf, cia_read_register(&c, CIA_REG_PORT_B));
    step_cia_cycles(&c, 2);
    expect_u8("pb6 toggle high", 0xff, cia_read_register(&c, CIA_REG_PORT_B));

    expect_true("cia reset", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x17);
    step_cia_cycles(&c, 2);
    expect_u8("pb7 toggle low", 0x7f, cia_read_register(&c, CIA_REG_PORT_B));
    step_cia_cycles(&c, 2);
    expect_u8("pb7 toggle high", 0xff, cia_read_register(&c, CIA_REG_PORT_B));
}

static void test_cia_port_b_ordinary_when_timer_output_disabled(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_DDRB, 0xff);
    cia_write_register(&c, CIA_REG_PORT_B, 0x00);
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x11);
    step_cia_cycles(&c, 2);

    expect_u8("port b ordinary output remains when timer output disabled", 0x00, cia_read_register(&c, CIA_REG_PORT_B));
}

static void test_cia_debug_read_timer_counters(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x10);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x02);
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x20);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x03);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x01);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x01);

    cia_step_cycle(&c);
    cia_step_cycle(&c);
    cia_step_cycle(&c);

    expect_u8("debug timer a lo is live counter", 0x0d, cia_debug_read_register(&c, CIA_REG_TIMER_A_LO));
    expect_u8("debug timer a hi is live counter", 0x02, cia_debug_read_register(&c, CIA_REG_TIMER_A_HI));
    expect_u8("debug timer b lo is live counter", 0x1d, cia_debug_read_register(&c, CIA_REG_TIMER_B_LO));
    expect_u8("debug timer b hi is live counter", 0x03, cia_debug_read_register(&c, CIA_REG_TIMER_B_HI));

    /* raw registers still hold the latch, not the live counter */
    expect_u8("registers[4] holds latch not counter", 0x10, c.registers[CIA_REG_TIMER_A_LO]);
    expect_u8("registers[6] holds latch not counter", 0x20, c.registers[CIA_REG_TIMER_B_LO]);
}

static void test_cia_debug_read_icr_no_side_effects(void) {
    cia c;
    char error[256];
    uint8_t icr_val;

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x81);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x19);

    cia_step_cycle(&c);
    cia_step_cycle(&c);
    expect_true("irq pending before debug read", cia_irq_pending(&c));

    icr_val = cia_debug_read_register(&c, CIA_REG_ICR);
    expect_u8("debug icr reports timer a and bit 7", 0x81, icr_val);
    expect_true("irq still pending after debug read", cia_irq_pending(&c));
    expect_u64("icr_reads not incremented by debug read", 0, c.icr_reads);

    /* normal read does clear */
    cia_read_register(&c, CIA_REG_ICR);
    expect_false("normal icr read clears flags", cia_irq_pending(&c));
}

static void test_cia_icr_masked_flags_and_mask_writes(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_set_interrupt_source(&c, CIA_INTERRUPT_TIMER_A);
    expect_u8("masked timer a flag without summary", CIA_INTERRUPT_TIMER_A, cia_read_register(&c, CIA_REG_ICR));
    expect_false("masked timer a does not assert", cia_irq_pending(&c));

    cia_set_interrupt_source(&c, CIA_INTERRUPT_TIMER_A | CIA_INTERRUPT_TIMER_B);
    cia_write_register(&c, CIA_REG_ICR, 0x81);
    expect_true("mask write can assert pending flag", cia_irq_pending(&c));
    expect_u8("enabled timer a summary with timer b masked", 0x83, cia_debug_read_register(&c, CIA_REG_ICR));

    cia_write_register(&c, CIA_REG_ICR, 0x82);
    expect_u8("set mask preserves existing timer a", 0x03, c.interrupt_mask);
    cia_write_register(&c, CIA_REG_ICR, 0x01);
    expect_u8("clear mask only clears selected timer a", 0x02, c.interrupt_mask);
    expect_true("timer b still asserts after timer a mask clear", cia_irq_pending(&c));

    expect_u8("normal read reports and clears flags", 0x83, cia_read_register(&c, CIA_REG_ICR));
    expect_false("icr read deasserts when flags clear", cia_irq_pending(&c));
    expect_u8("source flags cleared by read", 0x00, c.interrupt_flags);
}

static void test_cia_icr_reserved_sources_have_mask_semantics(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_set_interrupt_source(&c, 0x1c);
    expect_u8("reserved sources set flags without summary", 0x1c, cia_debug_read_register(&c, CIA_REG_ICR));
    cia_write_register(&c, CIA_REG_ICR, 0x9c);
    expect_u8("reserved masks set", 0x1c, c.interrupt_mask);
    expect_true("reserved enabled sources assert", cia_irq_pending(&c));
    cia_write_register(&c, CIA_REG_ICR, 0x08);
    expect_u8("reserved mask clear preserves others", 0x14, c.interrupt_mask);
}

static void test_cia_debug_read_port_formula(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));

    /* low nibble output, high nibble input */
    cia_write_register(&c, CIA_REG_DDRA, 0x0f);
    cia_write_register(&c, CIA_REG_PORT_A, 0x05);

    /* output bits: data & dir = 0x05; input bits: ~dir = 0xf0 */
    expect_u8("debug port a formula", 0xf5, cia_debug_read_register(&c, CIA_REG_PORT_A));
}

static void write_tod(cia *c, uint8_t hours, uint8_t minutes, uint8_t seconds, uint8_t tenths) {
    cia_write_register(c, 0x0b, hours);
    cia_write_register(c, 0x0a, minutes);
    cia_write_register(c, 0x09, seconds);
    cia_write_register(c, 0x08, tenths);
}

static void test_cia_tod_advances_at_selected_cadence(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_set_tod_cycles(&c, 5, 3);

    step_cia_cycles(&c, 2);
    expect_u8("tod clear cra uses 60hz source before tick", 0x00, cia_debug_read_register(&c, 0x08));
    cia_step_cycle(&c);
    expect_u8("tod clear cra uses 60hz source tick", 0x01, cia_debug_read_register(&c, 0x08));

    cia_write_register(&c, CIA_REG_CONTROL_A, 0x80);
    step_cia_cycles(&c, 4);
    expect_u8("tod set cra uses 50hz source before tick", 0x01, cia_debug_read_register(&c, 0x08));
    cia_step_cycle(&c);
    expect_u8("tod set cra uses 50hz source tick", 0x02, cia_debug_read_register(&c, 0x08));
}

static void test_cia_tod_machine_cadence_policy(void) {
    c64_t machine;

    c64_init(&machine);

    expect_u64("cia tod 50hz pal tenth cadence",
        (uint64_t)63u * 312u * 5u,
        machine.cia1.tod_50hz_cycles);
    expect_u64("cia tod 60hz ntsc tenth cadence",
        (uint64_t)65u * 263u * 6u,
        machine.cia1.tod_60hz_cycles);
}

static void test_cia_tod_bcd_rollover_and_ampm(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_set_tod_cycles(&c, 1, 1);
    write_tod(&c, 0x11, 0x59, 0x59, 0x09);

    cia_step_cycle(&c);
    expect_u8("tod tenths rolls to zero", 0x00, cia_debug_read_register(&c, 0x08));
    expect_u8("tod seconds rolls to zero", 0x00, cia_debug_read_register(&c, 0x09));
    expect_u8("tod minutes rolls to zero", 0x00, cia_debug_read_register(&c, 0x0a));
    expect_u8("tod 11am rolls to 12pm", 0x92, cia_debug_read_register(&c, 0x0b));

    write_tod(&c, 0x92, 0x59, 0x59, 0x09);
    cia_step_cycle(&c);
    expect_u8("tod 12pm rolls to 1pm", 0x81, cia_debug_read_register(&c, 0x0b));
}

static void test_cia_tod_read_latch_and_debug_peek(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_set_tod_cycles(&c, 1, 1);
    write_tod(&c, 0x01, 0x59, 0x59, 0x09);

    expect_u8("tod hours read latches", 0x01, cia_read_register(&c, 0x0b));
    cia_step_cycle(&c);
    expect_u8("debug peek sees latched tenths without releasing", 0x09, cia_debug_read_register(&c, 0x08));
    expect_u8("tod latched minutes remain coherent", 0x59, cia_read_register(&c, 0x0a));
    expect_u8("tod latched seconds remain coherent", 0x59, cia_read_register(&c, 0x09));
    expect_u8("tod tenths read releases latch", 0x09, cia_read_register(&c, 0x08));
    expect_u8("tod live minutes visible after release", 0x00, cia_read_register(&c, 0x0a));
}

static void test_cia_tod_writes_alarm_and_sets_interrupt(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_set_tod_cycles(&c, 1, 1);
    write_tod(&c, 0x12, 0x00, 0x00, 0x00);

    cia_write_register(&c, CIA_REG_CONTROL_B, 0x80);
    write_tod(&c, 0x12, 0x00, 0x00, 0x01);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x84);

    cia_step_cycle(&c);
    expect_u8("tod alarm flag set", CIA_INTERRUPT_TOD, c.interrupt_flags);
    expect_true("tod alarm irq pending", cia_irq_pending(&c));
    expect_u8("tod alarm icr read", 0x84, cia_read_register(&c, CIA_REG_ICR));
}

static void test_cia2_tod_alarm_can_raise_nmi(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;
    char error[256];

    reset_machine(&machine);
    cia_set_tod_cycles(&machine.cia2, 1, 1);
    write_tod(&machine.cia2, 0x12, 0x00, 0x00, 0x00);
    cia_write_register(&machine.cia2, CIA_REG_CONTROL_B, 0x80);
    write_tod(&machine.cia2, 0x12, 0x00, 0x00, 0x01);
    cia_write_register(&machine.cia2, CIA_REG_CONTROL_B, 0x00);
    cia_write_register(&machine.cia2, CIA_REG_ICR, 0x84);

    expect_true("tod nmi cycle", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("tod nmi step instruction", c64_step_instruction(&machine, error, sizeof(error)));
    c64_copy_cpu_snapshot(&machine, &cpu);
    expect_u16("tod cia2 nmi vector entered", TEST_NMI_VECTOR, cpu.pc);
}

static void test_machine_cia_step_and_irq_foundations(void) {
    c64_t machine;
    char error[256];

    reset_machine(&machine);

    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_ICR, 0x81);
    cia_write_register(&machine.cia1, CIA_REG_CONTROL_A, 0x11);
    expect_true("machine cycle steps cia", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("machine cycle steps cia again", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("cia1 irq foundation", cia_irq_pending(&machine.cia1));

    cia_write_register(&machine.cia2, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&machine.cia2, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&machine.cia2, CIA_REG_ICR, 0x82);
    cia_write_register(&machine.cia2, CIA_REG_CONTROL_B, 0x11);
    expect_true("machine cycle steps cia2", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("machine cycle steps cia2 again", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("cia2 nmi foundation", cia_irq_pending(&machine.cia2));
}

static void test_cia1_timer_irq_reaches_cpu_irq_vector(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;

    reset_machine(&machine);
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_ICR, 0x81);
    cia_write_register(&machine.cia1, CIA_REG_CONTROL_A, 0x11);
    cia_step_cycle(&machine.cia1);
    cia_step_cycle(&machine.cia1);
    machine.cpu.cpu.I = 0;

    step_machine_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);

    expect_u16("cia1 irq vector entered", TEST_IRQ_VECTOR, cpu.pc);
    expect_u64("cia1 irq entry counted", 1, machine.cpu.cpu.irq_entries);
}

static void test_cia2_timer_nmi_reaches_cpu_nmi_vector(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;

    reset_machine(&machine);
    cia_write_register(&machine.cia2, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&machine.cia2, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&machine.cia2, CIA_REG_ICR, 0x82);
    cia_write_register(&machine.cia2, CIA_REG_CONTROL_B, 0x11);
    cia_step_cycle(&machine.cia2);
    cia_step_cycle(&machine.cia2);

    step_machine_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);

    expect_u16("cia2 nmi vector entered", TEST_NMI_VECTOR, cpu.pc);
    expect_u64("cia2 nmi entry counted", 1, machine.cpu.cpu.nmi_entries);

    step_machine_instructions(&machine, 1);
    expect_u64("cia2 nmi is edge-triggered", 1, machine.cpu.cpu.nmi_entries);
}

static void test_restore_nmi_still_reaches_cpu_nmi_vector(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;

    reset_machine(&machine);
    c64_restore(&machine);

    step_machine_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);

    expect_u16("restore nmi vector entered", TEST_NMI_VECTOR, cpu.pc);
    expect_u64("restore nmi entry counted", 1, machine.cpu.cpu.nmi_entries);
    expect_u64("restore request counted", 1, machine.restore_requests);
}

static void test_cia1_keyboard_matrix_bidirectional_scan(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_set_key(&machine, C64_KEY_A, true);

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_A, 0xfd);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0x00);
    expect_u8("A pulls PRB column 2 low", 0xfb, cia_read_register(&machine.cia1, CIA_REG_PORT_B));
    expect_u8("debug A pulls PRB column 2 low", 0xfb, cia_debug_read_register(&machine.cia1, CIA_REG_PORT_B));

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_B, 0xfb);
    expect_u8("A pulls PRA row 1 low", 0xfd, cia_read_register(&machine.cia1, CIA_REG_PORT_A));
    expect_u8("debug A pulls PRA row 1 low", 0xfd, cia_debug_read_register(&machine.cia1, CIA_REG_PORT_A));
}

static void test_cia1_keyboard_multiple_keys_combine(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_set_key(&machine, C64_KEY_A, true);
    c64_set_key(&machine, C64_KEY_S, true);

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_A, 0xfd);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0x00);
    expect_u8("A and S pull both columns low", 0xdb, cia_read_register(&machine.cia1, CIA_REG_PORT_B));
}

static void test_cia1_joystick_port_1_bits(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_set_joystick(&machine, 1, C64_JOYSTICK_UP | C64_JOYSTICK_FIRE);

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_A, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0x00);
    expect_u8("joystick 1 pulls PRB up/fire low", 0xee, cia_read_register(&machine.cia1, CIA_REG_PORT_B));
}

static void test_cia1_joystick_port_2_bits(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_set_joystick(&machine, 2, C64_JOYSTICK_DOWN | C64_JOYSTICK_LEFT | C64_JOYSTICK_FIRE);

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_B, 0xff);
    expect_u8("joystick 2 pulls PRA down/left/fire low", 0xe9, cia_read_register(&machine.cia1, CIA_REG_PORT_A));
}

static void test_cia1_keyboard_and_joystick_shared_lines_combine(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_set_key(&machine, C64_KEY_A, true);
    c64_set_joystick(&machine, 1, C64_JOYSTICK_FIRE);

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_A, 0xfd);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0x00);
    expect_u8("keyboard A and joystick fire both pull PRB", 0xeb, cia_read_register(&machine.cia1, CIA_REG_PORT_B));
}

static void test_restore_does_not_enter_keyboard_matrix(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_restore(&machine);

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_A, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0x00);
    expect_u8("restore does not pull keyboard columns", 0xff, cia_read_register(&machine.cia1, CIA_REG_PORT_B));

    cia_write_register(&machine.cia1, CIA_REG_DDRA, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_DDRB, 0xff);
    cia_write_register(&machine.cia1, CIA_REG_PORT_B, 0x00);
    expect_u8("restore does not pull keyboard rows", 0xff, cia_read_register(&machine.cia1, CIA_REG_PORT_A));
    expect_u64("restore still counted separately", 1, machine.restore_requests);
}

static void test_cycle_step_replays_cia_icr_read_at_bus_cycle(void) {
    static const uint8_t code[] = {
        0xad, 0x0d, 0xdc, /* LDA $DC0D */
        0xea              /* NOP */
    };
    c64_t machine;
    char error[256];

    reset_machine_with_kernal_code(&machine, code, sizeof(code));
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_ICR, 0x81);
    cia_write_register(&machine.cia1, CIA_REG_CONTROL_A, 0x19);
    cia_step_cycle(&machine.cia1);
    cia_step_cycle(&machine.cia1);

    expect_true("irq pending before lda icr", cia_irq_pending(&machine.cia1));

    expect_true("lda icr cycle 1", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("icr still pending after opcode fetch", cia_irq_pending(&machine.cia1));
    expect_u64("icr read not early after opcode fetch", 0, machine.cia1.icr_reads);

    expect_true("lda icr cycle 2", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("icr still pending after address low fetch", cia_irq_pending(&machine.cia1));
    expect_u64("icr read not early after address low fetch", 0, machine.cia1.icr_reads);

    expect_true("lda icr cycle 3", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("icr still pending after address high fetch", cia_irq_pending(&machine.cia1));
    expect_u64("icr read not early after address high fetch", 0, machine.cia1.icr_reads);

    expect_true("lda icr cycle 4", c64_step_cycle(&machine, error, sizeof(error)));
    expect_false("icr read clears at data bus cycle", cia_irq_pending(&machine.cia1));
    expect_u64("icr read counted at data bus cycle", 1, machine.cia1.icr_reads);
}

static void test_cpu_visible_timer_b_counts_down(void) {
    static const uint8_t code[] = {
        0xa9, 0x40,       /* LDA #$40 */
        0x8d, 0x06, 0xdc, /* STA $DC06 */
        0xa9, 0x00,       /* LDA #$00 */
        0x8d, 0x07, 0xdc, /* STA $DC07 */
        0xa9, 0x01,       /* LDA #$01 */
        0x8d, 0x0f, 0xdc, /* STA $DC0F */
        0xea,             /* NOP */
        0xea,             /* NOP */
        0xea,             /* NOP */
        0xad, 0x06, 0xdc  /* LDA $DC06 */
    };
    c64_t machine;
    char error[256];
    size_t i;

    reset_machine_with_kernal_code(&machine, code, sizeof(code));

    for (i = 0; i < 10; ++i) {
        expect_true("step timer b cpu diagnostic", c64_step_instruction(&machine, error, sizeof(error)));
    }

    expect_u8_less_than("cpu-visible timer b low decremented", machine.cpu.cpu.A, 0x40);
}

int main(void) {
    test_cia_reset_and_no_key_ports();
    test_cia_register_mirroring_and_ports();
    test_cia_timer_and_interrupts();
    test_cia_timer_stopped_timers_do_not_decrement();
    test_cia_timer_force_load_strobe_for_both_timers();
    test_cia_timer_continuous_underflow_reloads_both_timers();
    test_cia_timer_oneshot_reloads_and_stops_both_timers();
    test_cia_zero_latch_does_not_underflow_every_cycle();
    test_cia_zero_latch_stopped_write_loads_effective_counter();
    test_cia_oneshot_stops_after_underflow();
    test_cia_bus_mapping_and_ram_under_io();
    test_cia2_vic_bank_uses_port_pins();
    test_cia2_iec_lines_release_high();
    test_cia2_iec_cia_pull_low_lines();
    test_cia2_iec_external_pull_survives_cia_release();
    test_cia_timer_b_cascade_mode();
    test_cia_timer_a_cnt_mode();
    test_cia_timer_b_cnt_mode();
    test_cia_timer_b_combined_cascade_and_cnt_mode();
    test_cia_pb6_pb7_pulse_outputs();
    test_cia_pb6_pb7_toggle_outputs();
    test_cia_port_b_ordinary_when_timer_output_disabled();
    test_machine_cia_step_and_irq_foundations();
    test_cia1_timer_irq_reaches_cpu_irq_vector();
    test_cia2_timer_nmi_reaches_cpu_nmi_vector();
    test_restore_nmi_still_reaches_cpu_nmi_vector();
    test_cia1_keyboard_matrix_bidirectional_scan();
    test_cia1_keyboard_multiple_keys_combine();
    test_cia1_joystick_port_1_bits();
    test_cia1_joystick_port_2_bits();
    test_cia1_keyboard_and_joystick_shared_lines_combine();
    test_restore_does_not_enter_keyboard_matrix();
    test_cycle_step_replays_cia_icr_read_at_bus_cycle();
    test_cpu_visible_timer_b_counts_down();
    test_cia_debug_read_timer_counters();
    test_cia_debug_read_icr_no_side_effects();
    test_cia_icr_masked_flags_and_mask_writes();
    test_cia_icr_reserved_sources_have_mask_semantics();
    test_cia_debug_read_port_formula();
    test_cia_tod_advances_at_selected_cadence();
    test_cia_tod_machine_cadence_policy();
    test_cia_tod_bcd_rollover_and_ampm();
    test_cia_tod_read_latch_and_debug_peek();
    test_cia_tod_writes_alarm_and_sets_interrupt();
    test_cia2_tod_alarm_can_raise_nmi();
    return 0;
}
