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
    TEST_RESET_VECTOR = 0xe000,
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

static void build_roms(c64_rom_set *roms) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffc] = (uint8_t)(TEST_RESET_VECTOR & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(TEST_RESET_VECTOR >> 8);
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

static void test_cia_timer_b_cnt_mode(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));

    /* Timer B: latch=5, CNT mode (INMODE0=1 → bits 5-6 = 01), force-load + start
       CRB $31 = 0011_0001: bit0=START, bit4=FORCE_LOAD, bit5=INMODE0 → mode 01 */
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x31);

    /* CNT pin not emulated — timer must never count regardless of steps */
    cia_step_cycle(&c);
    cia_step_cycle(&c);
    cia_step_cycle(&c);

    expect_u16("tb cnt mode never counts", 5, c.timer_b.counter);
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

int main(void) {
    test_cia_reset_and_no_key_ports();
    test_cia_register_mirroring_and_ports();
    test_cia_timer_and_interrupts();
    test_cia_zero_latch_does_not_underflow_every_cycle();
    test_cia_oneshot_stops_after_underflow();
    test_cia_bus_mapping_and_ram_under_io();
    test_cia_timer_b_cascade_mode();
    test_cia_timer_b_cnt_mode();
    test_machine_cia_step_and_irq_foundations();
    test_cia_debug_read_timer_counters();
    test_cia_debug_read_icr_no_side_effects();
    test_cia_debug_read_port_formula();
    return 0;
}
