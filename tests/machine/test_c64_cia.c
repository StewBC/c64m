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
    CIA_REG_SDR = 0x0c,
    CIA_REG_ICR = 0x0d,
    CIA_REG_CONTROL_A = 0x0e,
    CIA_REG_CONTROL_B = 0x0f,
    CIA_INTERRUPT_TIMER_A = 0x01,
    CIA_INTERRUPT_TIMER_B = 0x02,
    CIA_INTERRUPT_TOD = 0x04,
    CIA_INTERRUPT_SERIAL = 0x08,
    CIA_INTERRUPT_FLAG = 0x10,
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
    /* Lorenz: two clocks after START before counting (start_delay). */
    cia_step_cycle(&c);
    expect_u8("still 2 during start delay 1", 0x02, cia_read_register(&c, CIA_REG_TIMER_A_LO));
    cia_step_cycle(&c);
    expect_u8("still 2 during start delay 2", 0x02, cia_read_register(&c, CIA_REG_TIMER_A_LO));
    cia_step_cycle(&c);
    expect_u8("timer decremented to 1", 0x01, cia_read_register(&c, CIA_REG_TIMER_A_LO));
    expect_false("irq not pending before underflow", cia_irq_pending(&c));
    /* Phi2: tick at 1 underflows and reloads (no visible 0). */
    cia_step_cycle(&c);

    expect_true("timer underflow recorded", c.timer_a.underflow);
    expect_true("timer irq pending", cia_irq_pending(&c));
    expect_u16("underflow reloads latch", 0x0002, c.timer_a.counter);
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

    /* start_delay 2 + count tick at 1 → underflow on third cycle. */
    cia_step_cycle(&c);
    cia_step_cycle(&c);
    expect_u8("no source flags during start delay", 0x00, c.interrupt_flags);
    expect_u16("still latch during delay", 0x0001, c.timer_a.counter);

    cia_step_cycle(&c);
    expect_u16("continuous timer a reloads on uf", 0x0001, c.timer_a.counter);
    expect_u16("continuous timer b reloads on uf", 0x0001, c.timer_b.counter);
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

    /* start_delay 2 + uf tick at counter 1. */
    step_cia_cycles(&c, 3);

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
    cia_step_cycle(&c);
    expect_false("zero latch does not underflow during start delay", cia_irq_pending(&c));
    expect_u16("zero latch still full during delay", 0xffff, c.timer_a.counter);
    cia_step_cycle(&c);
    expect_false("zero latch does not underflow on first tick", cia_irq_pending(&c));
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

    step_cia_cycles(&c, 3);
    expect_true("oneshot irq", cia_irq_pending(&c));
    expect_u8("oneshot clears start", 0x08, cia_read_register(&c, CIA_REG_CONTROL_A));
}

/* Lorenz oneshot.prg: CRA after start with latch 2 vs 3 around underflow. */
static void test_cia_oneshot_cra_at_underflow_window(void) {
    cia c;
    char error[256];
    int i;

    /* Latch=2, oneshot+start (no force load): by ~4 cycles after start delay,
     * CRA should show start cleared ($08). */
    expect_true("cia init latch2", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x02);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x09);
    for (i = 0; i < 8; ++i) {
        cia_step_cycle(&c);
    }
    expect_u8("latch2 oneshot cleared start", 0x08, cia_read_register(&c, CIA_REG_CONTROL_A));

    /* Latch=3: shortly after start delay, CRA can still show $09 (start set)
     * before the underflow tick. */
    expect_true("cia init latch3", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x03);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x09);
    step_cia_cycles(&c, 4); /* delay2 + 3→2 + 2→1, not yet uf */
    expect_u8("latch3 oneshot still running at t-1", 0x09, cia_read_register(&c, CIA_REG_CONTROL_A));
    cia_step_cycle(&c); /* uf */
    expect_u8("latch3 oneshot cleared at t", 0x08, cia_read_register(&c, CIA_REG_CONTROL_A));
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

    /* Both timers share start_delay (2). A: delay, delay, 3→2, 2→1, 1→uf. */
    cia_step_cycle(&c);
    expect_u16("tb cascade step1 unchanged", 5, c.timer_b.counter);
    cia_step_cycle(&c);
    expect_u16("tb cascade step2 unchanged", 5, c.timer_b.counter);
    cia_step_cycle(&c);
    expect_u16("tb cascade step3 ta dec", 2, c.timer_a.counter);
    expect_u16("tb cascade step3 b still 5", 5, c.timer_b.counter);
    cia_step_cycle(&c);
    expect_u16("tb cascade step4 ta at 1", 1, c.timer_a.counter);
    expect_u16("tb cascade step4 b still 5", 5, c.timer_b.counter);

    /* A underflows (tick at 1); B cascade decrements same cycle. */
    cia_step_cycle(&c);
    expect_u16("tb cascade after a uf", 4, c.timer_b.counter);
    expect_u16("ta reloaded after underflow", 3, c.timer_a.counter);

    /* A skip_tick then 3→2→1 then uf again. B starts after its own delay already
     * burned; cascade only on A uf. */
    cia_step_cycle(&c); /* A skip after reload */
    cia_step_cycle(&c); /* 3→2 */
    cia_step_cycle(&c); /* 2→1 */
    expect_u16("tb unchanged mid period", 4, c.timer_b.counter);
    cia_step_cycle(&c); /* A uf */
    expect_u16("tb cascade second a uf", 3, c.timer_b.counter);
}

static void test_cia_timer_a_cnt_mode(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x05);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x31);

    /* Burn start_delay on Phi2 while started; still no CNT → no count. */
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

    /* A latch=1: delay 2 then uf. B needs A.underflow + CNT same cycle. */
    step_cia_cycles(&c, 2);
    expect_u16("combined mode ignores during start delay", 5, c.timer_b.counter);

    cia_pulse_cnt(&c);
    cia_step_cycle(&c); /* A underflows; B has CNT + A uf → dec */
    expect_u16("combined mode counts timer a underflow with cnt", 4, c.timer_b.counter);
}

static void test_cia_pb6_pb7_pulse_outputs(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x13);
    step_cia_cycles(&c, 3); /* delay 2 + uf */
    expect_u8("pb6 pulse low", 0xbf, cia_read_register(&c, CIA_REG_PORT_B));
    cia_step_cycle(&c);
    expect_u8("pb6 pulse restored high", 0xff, cia_read_register(&c, CIA_REG_PORT_B));

    expect_true("cia reset", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x13);
    step_cia_cycles(&c, 3);
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
    step_cia_cycles(&c, 3); /* delay 2 + uf */
    expect_u8("pb6 toggle low", 0xbf, cia_read_register(&c, CIA_REG_PORT_B));
    step_cia_cycles(&c, 2); /* skip after reload + uf */
    expect_u8("pb6 toggle high", 0xff, cia_read_register(&c, CIA_REG_PORT_B));

    expect_true("cia reset", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&c, CIA_REG_CONTROL_B, 0x17);
    step_cia_cycles(&c, 3);
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

    /* start_delay 2 + 3 decrements: 0x0210 → 0x020d, 0x0320 → 0x031d */
    step_cia_cycles(&c, 5);

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

    step_cia_cycles(&c, 3);
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

    /* Alarm sets ICR on a TOD tick; delayed pin asserts on the following cycle. */
    expect_true("tod nmi cycle (latch)", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("tod pending latched", cia_irq_pending(&machine.cia2));
    if (!cia_interrupt_line(&machine.cia2)) {
        expect_true("tod nmi cycle (pin delay)", c64_step_cycle(&machine, error, sizeof(error)));
    }
    expect_true("tod pin asserted", cia_interrupt_line(&machine.cia2));
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
    /* start_delay 2 + uf at counter 1 */
    expect_true("machine cycle steps cia", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("machine cycle steps cia again", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("machine cycle steps cia uf", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("cia1 irq foundation", cia_irq_pending(&machine.cia1));

    cia_write_register(&machine.cia2, CIA_REG_TIMER_B_LO, 0x01);
    cia_write_register(&machine.cia2, CIA_REG_TIMER_B_HI, 0x00);
    cia_write_register(&machine.cia2, CIA_REG_ICR, 0x82);
    cia_write_register(&machine.cia2, CIA_REG_CONTROL_B, 0x11);
    expect_true("machine cycle steps cia2", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("machine cycle steps cia2 again", c64_step_cycle(&machine, error, sizeof(error)));
    expect_true("machine cycle steps cia2 uf", c64_step_cycle(&machine, error, sizeof(error)));
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
    /* start_delay 2 + uf → pending; one more cycle → delayed pin. */
    cia_step_cycle(&machine.cia1);
    cia_step_cycle(&machine.cia1);
    cia_step_cycle(&machine.cia1);
    expect_true("pending after underflow", cia_irq_pending(&machine.cia1));
    expect_false("pin still low on underflow cycle", cia_interrupt_line(&machine.cia1));
    cia_step_cycle(&machine.cia1);
    expect_true("pin asserts one cycle later", cia_interrupt_line(&machine.cia1));
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
    cia_step_cycle(&machine.cia2);
    expect_false("nmi pin low on underflow cycle", cia_interrupt_line(&machine.cia2));
    cia_step_cycle(&machine.cia2);
    expect_true("nmi pin asserts one cycle later", cia_interrupt_line(&machine.cia2));

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

static void test_cia_interrupt_line_delays_one_cycle_behind_pending(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x81);       /* enable Timer A */
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x19); /* start + force-load + one-shot */

    cia_step_cycle(&c); /* start delay */
    cia_step_cycle(&c); /* start delay */
    expect_false("no pending before underflow", cia_irq_pending(&c));
    expect_false("pin low before underflow", cia_interrupt_line(&c));

    cia_step_cycle(&c); /* underflow: flag latches immediately, pin delayed */
    expect_true("pending immediately after underflow", cia_irq_pending(&c));
    expect_false("pin still low the cycle of underflow", cia_interrupt_line(&c));

    cia_step_cycle(&c); /* pin catches up one cycle later */
    expect_true("pin asserts one cycle after pending", cia_interrupt_line(&c));
    expect_true("pending remains latched", cia_irq_pending(&c));
}

static void test_cia_interrupt_line_deasserts_one_cycle_after_icr_read(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x81);
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x19); /* one-shot */

    cia_step_cycle(&c); /* start delay */
    cia_step_cycle(&c); /* start delay */
    cia_step_cycle(&c); /* underflow */
    cia_step_cycle(&c); /* pin asserts */
    expect_true("pin asserted", cia_interrupt_line(&c));
    expect_true("pending set", cia_irq_pending(&c));

    /* Reading ICR clears the latched flag immediately; the pin lingers a cycle. */
    (void)cia_read_register(&c, CIA_REG_ICR);
    expect_false("pending cleared immediately by icr read", cia_irq_pending(&c));
    expect_true("pin still asserted same cycle as read", cia_interrupt_line(&c));

    cia_step_cycle(&c);
    expect_true("pin lingers one cycle after read", cia_interrupt_line(&c));

    cia_step_cycle(&c);
    expect_false("pin deasserts one cycle after read", cia_interrupt_line(&c));
}

static void test_cia_pc_pulses_on_prb_access(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    expect_true("pc high after reset", cia_pc_line(&c));

    /* CPU-visible PRB write drives PC low for exactly one cycle. */
    cia_write_register(&c, CIA_REG_PORT_B, 0x00);
    cia_step_cycle(&c);
    expect_false("pc low cycle after prb write", cia_pc_line(&c));
    cia_step_cycle(&c);
    expect_true("pc returns high after prb write pulse", cia_pc_line(&c));

    /* CPU-visible PRB read also drives PC low for one cycle. */
    (void)cia_read_register(&c, CIA_REG_PORT_B);
    cia_step_cycle(&c);
    expect_false("pc low cycle after prb read", cia_pc_line(&c));
    cia_step_cycle(&c);
    expect_true("pc returns high after prb read pulse", cia_pc_line(&c));

    /* Debug-safe PRB peeks must not pulse PC. */
    (void)cia_debug_read_register(&c, CIA_REG_PORT_B);
    cia_step_cycle(&c);
    expect_true("debug prb peek does not pulse pc", cia_pc_line(&c));

    /* PRA access must not pulse PC. */
    (void)cia_read_register(&c, CIA_REG_PORT_A);
    cia_step_cycle(&c);
    expect_true("pra access does not pulse pc", cia_pc_line(&c));
}

static void test_cia_serial_output_shifts_eight_bits(void) {
    cia c;
    char error[256];
    uint8_t received = 0;
    int i;

    expect_true("cia init", cia_init(&c, error, sizeof(error)));

    /* Timer A continuous, small latch so it underflows every two cycles. */
    cia_write_register(&c, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&c, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&c, CIA_REG_ICR, 0x88); /* enable serial source */
    /* CRA: start (0x01) + serial output (0x40). */
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x41);

    cia_write_register(&c, CIA_REG_SDR, 0xb5);
    expect_u8("serial output armed 8 bits", 8, c.serial_out_bits);

    /* Debug-safe reads must not advance shift state. */
    (void)cia_debug_read_register(&c, CIA_REG_SDR);
    expect_u8("debug read does not advance serial", 8, c.serial_out_bits);

    /* Burn start_delay; Timer A latch=1 underflows every 2 cycles thereafter
     * (uf + skip). One serial bit every two underflows → 4 cycles/bit. */
    step_cia_cycles(&c, 2);
    for (i = 0; i < 8; ++i) {
        step_cia_cycles(&c, 4);
        received = (uint8_t)((received << 1) | (c.sp_output ? 1u : 0u));
    }

    expect_u8("serial output shifted msb-first byte", 0xb5, received);
    expect_u8("serial complete after eight bits", 0, c.serial_out_bits);
    expect_true("enabled serial source asserts line", cia_irq_pending(&c));
    /* Timer A also latches its own flag while running; check the serial + summary
     * bits without asserting the exact combined flag byte. */
    expect_u8("serial icr flag reported",
        (uint8_t)(CIA_INTERRUPT_SERIAL | 0x80u),
        (uint8_t)(cia_debug_read_register(&c, CIA_REG_ICR) & (CIA_INTERRUPT_SERIAL | 0x80u)));
}

static void test_cia_serial_input_shifts_eight_bits(void) {
    cia c;
    char error[256];
    const uint8_t pattern = 0x4e;
    int i;

    expect_true("cia init", cia_init(&c, error, sizeof(error)));
    cia_write_register(&c, CIA_REG_ICR, 0x88); /* enable serial source */
    /* CRA serial-output bit clear selects input mode. */
    cia_write_register(&c, CIA_REG_CONTROL_A, 0x00);

    /* Feed eight bits MSB first, each clocked by a CNT edge. */
    for (i = 7; i >= 0; --i) {
        cia_set_sp_line(&c, ((pattern >> i) & 1u) != 0);
        cia_pulse_cnt(&c);
        cia_step_cycle(&c);
    }

    expect_u8("serial input latched byte", pattern, cia_read_register(&c, CIA_REG_SDR));
    expect_true("enabled serial input asserts line", cia_irq_pending(&c));
}

static void test_cia2_serial_input_can_raise_nmi(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;
    const uint8_t pattern = 0xa3;
    int i;

    reset_machine(&machine);
    cia_write_register(&machine.cia2, CIA_REG_ICR, 0x88);
    cia_write_register(&machine.cia2, CIA_REG_CONTROL_A, 0x00);

    for (i = 7; i >= 0; --i) {
        cia_set_sp_line(&machine.cia2, ((pattern >> i) & 1u) != 0);
        cia_pulse_cnt(&machine.cia2);
        cia_step_cycle(&machine.cia2);
    }
    expect_true("serial complete pending", cia_irq_pending(&machine.cia2));
    if (!cia_interrupt_line(&machine.cia2)) {
        cia_step_cycle(&machine.cia2);
    }
    expect_true("serial pin asserted after delay", cia_interrupt_line(&machine.cia2));

    step_machine_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);

    expect_u16("cia2 serial nmi vector entered", TEST_NMI_VECTOR, cpu.pc);
    expect_u64("cia2 serial nmi entry counted", 1, machine.cpu.cpu.nmi_entries);
}

static void test_cia_flag_edge_sets_icr_and_respects_mask(void) {
    cia c;
    char error[256];

    expect_true("cia init", cia_init(&c, error, sizeof(error)));

    /* Masked FLAG: edge sets the flag but does not assert the interrupt line. */
    cia_set_flag_line(&c, false);
    expect_u8("masked flag sets icr bit 4", CIA_INTERRUPT_FLAG,
        cia_debug_read_register(&c, CIA_REG_ICR));
    expect_false("masked flag does not assert line", cia_irq_pending(&c));

    /* Clear the pending flag and re-arm the line high. */
    (void)cia_read_register(&c, CIA_REG_ICR);
    cia_set_flag_line(&c, true);

    /* Enable FLAG and drive a high->low edge. */
    cia_write_register(&c, CIA_REG_ICR, (uint8_t)(0x80u | CIA_INTERRUPT_FLAG));
    cia_set_flag_line(&c, false);
    expect_true("enabled flag edge asserts line", cia_irq_pending(&c));

    /* Holding FLAG low must not re-raise without a new edge. */
    (void)cia_read_register(&c, CIA_REG_ICR);
    expect_false("icr read clears flag", cia_irq_pending(&c));
    cia_set_flag_line(&c, false);
    expect_false("held-low flag does not re-raise", cia_irq_pending(&c));

    /* A fresh high->low edge raises it again. */
    cia_set_flag_line(&c, true);
    cia_set_flag_line(&c, false);
    expect_true("new flag edge asserts line again", cia_irq_pending(&c));
}

static void test_cia2_flag_line_can_raise_nmi(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;

    reset_machine(&machine);
    cia_write_register(&machine.cia2, CIA_REG_ICR, (uint8_t)(0x80u | CIA_INTERRUPT_FLAG));
    cia_set_flag_line(&machine.cia2, false);
    expect_true("flag edge latches pending", cia_irq_pending(&machine.cia2));
    expect_false("flag pin delayed until pipeline steps", cia_interrupt_line(&machine.cia2));
    /* FLAG is set outside step_cycle, so the delay pipeline needs one step to
     * sample latched pending and a second to drive the pin (same net delay as
     * a timer underflow that sets the flag mid-step). */
    cia_step_cycle(&machine.cia2);
    expect_false("flag pin still low after first pipeline step", cia_interrupt_line(&machine.cia2));
    cia_step_cycle(&machine.cia2);
    expect_true("flag pin asserts after second pipeline step", cia_interrupt_line(&machine.cia2));

    step_machine_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);

    expect_u16("cia2 flag nmi vector entered", TEST_NMI_VECTOR, cpu.pc);
    expect_u64("cia2 flag nmi entry counted", 1, machine.cpu.cpu.nmi_entries);
}

/* Option-2: CPU must not sample IRQ on the underflow cycle itself. */
static void test_cia1_cpu_irq_waits_one_cycle_after_underflow(void) {
    c64_t machine;
    c64_cpu_snapshot cpu;

    reset_machine(&machine);
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_LO, 0x01);
    cia_write_register(&machine.cia1, CIA_REG_TIMER_A_HI, 0x00);
    cia_write_register(&machine.cia1, CIA_REG_ICR, 0x81);
    cia_write_register(&machine.cia1, CIA_REG_CONTROL_A, 0x11);
    machine.cpu.cpu.I = 0;

    /* Isolate CIA steps so the CPU is still between instructions. */
    cia_step_cycle(&machine.cia1); /* start delay */
    cia_step_cycle(&machine.cia1); /* start delay */
    cia_step_cycle(&machine.cia1); /* underflow: pending, pin low */
    expect_true("pending on underflow", cia_irq_pending(&machine.cia1));
    expect_false("pin still low on underflow", cia_interrupt_line(&machine.cia1));

    /* Instruction boundary samples pin low → no IRQ entry; micro-steps will
     * advance CIA and may assert the pin during this instruction. */
    step_machine_instructions(&machine, 1);
    expect_u64("no irq entry while pin was low at poll", 0, machine.cpu.cpu.irq_entries);

    /* Next boundary: delayed pin is high → IRQ. */
    step_machine_instructions(&machine, 1);
    c64_copy_cpu_snapshot(&machine, &cpu);
    expect_u16("irq vector after delay", TEST_IRQ_VECTOR, cpu.pc);
    expect_u64("irq entry after delay", 1, machine.cpu.cpu.irq_entries);
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
    test_cia_oneshot_cra_at_underflow_window();
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
    test_cia1_cpu_irq_waits_one_cycle_after_underflow();
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
    test_cia_interrupt_line_delays_one_cycle_behind_pending();
    test_cia_interrupt_line_deasserts_one_cycle_after_icr_read();
    test_cia_pc_pulses_on_prb_access();
    test_cia_serial_output_shifts_eight_bits();
    test_cia_serial_input_shifts_eight_bits();
    test_cia2_serial_input_can_raise_nmi();
    test_cia_flag_edge_sets_icr_and_respects_mask();
    test_cia2_flag_line_can_raise_nmi();
    return 0;
}
