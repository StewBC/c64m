#include "via6522.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_eq_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected 0x%02X, got 0x%02X\n", name, expected, actual);
        exit(1);
    }
}

static void expect_eq_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected 0x%04X, got 0x%04X\n", name, expected, actual);
        exit(1);
    }
}

static void expect_zero_int(const char *name, int actual) {
    if (actual != 0) {
        fprintf(stderr, "FAIL: %s: expected 0, got %d\n", name, actual);
        exit(1);
    }
}

static void expect_nonzero_int(const char *name, int actual) {
    if (actual == 0) {
        fprintf(stderr, "FAIL: %s: expected non-zero\n", name);
        exit(1);
    }
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */

static void test_init_zero(void) {
    via6522 v;
    via6522_init(&v);

    expect_eq_u8("init: ora",         0x00, v.ora);
    expect_eq_u8("init: orb",         0x00, v.orb);
    expect_eq_u8("init: ddra",        0x00, v.ddra);
    expect_eq_u8("init: ddrb",        0x00, v.ddrb);
    expect_eq_u8("init: port_a_in",   0x00, v.port_a_in);
    expect_eq_u8("init: port_b_in",   0x00, v.port_b_in);
    expect_eq_u16("init: t1_counter", 0x0000, v.t1_counter);
    expect_eq_u16("init: t1_latch",   0x0000, v.t1_latch);
    expect_zero_int("init: t1_running",   v.t1_running);
    expect_zero_int("init: t1_pb7_state", v.t1_pb7_state);
    expect_eq_u16("init: t2_counter", 0x0000, v.t2_counter);
    expect_eq_u8("init: t2_latch_low", 0x00, v.t2_latch_low);
    expect_zero_int("init: t2_running", v.t2_running);
    expect_eq_u8("init: ifr",  0x00, v.ifr);
    expect_eq_u8("init: ier",  0x00, v.ier);
    expect_eq_u8("init: ca1_last", 0x00, v.ca1_last);
}

static void test_reset_zero(void) {
    via6522 v;

    via6522_init(&v);
    via6522_write(&v, 11, 0xFF);  /* dirty the struct */
    via6522_write(&v, 5, 0x00);   /* start T1 */
    via6522_reset(&v);

    expect_eq_u8("reset: ifr",        0x00, v.ifr);
    expect_eq_u8("reset: ier",        0x00, v.ier);
    expect_zero_int("reset: t1_running", v.t1_running);
    expect_zero_int("reset: t2_running", v.t2_running);
    expect_eq_u8("reset: acr",        0x00, v.acr);
    expect_eq_u8("reset: pcr",        0x00, v.pcr);
}

/* ------------------------------------------------------------------ */
/* Port reads                                                          */
/* ------------------------------------------------------------------ */

static void test_port_a_all_output(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 3, 0xFF);   /* DDRA = all output */
    via6522_write(&v, 1, 0xAB);   /* ORA = 0xAB */
    expect_eq_u8("port_a_all_output", 0xAB, via6522_read(&v, 1));
}

static void test_port_a_all_input(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 3, 0x00);   /* DDRA = all input */
    via6522_set_port_a_inputs(&v, 0xCD);
    expect_eq_u8("port_a_all_input", 0xCD, via6522_read(&v, 1));
}

static void test_port_a_mixed(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 3, 0xF0);   /* DDRA: upper nibble output, lower input */
    via6522_write(&v, 1, 0xAF);   /* ORA */
    via6522_set_port_a_inputs(&v, 0x5A);
    /* expected: (0xAF & 0xF0) | (0x5A & 0x0F) = 0xA0 | 0x0A = 0xAA */
    expect_eq_u8("port_a_mixed", 0xAA, via6522_read(&v, 1));
}

static void test_port_b_all_output(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 2, 0xFF);   /* DDRB = all output */
    via6522_write(&v, 0, 0x55);   /* ORB = 0x55 */
    expect_eq_u8("port_b_all_output", 0x55, via6522_read(&v, 0));
}

static void test_port_b_all_input(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 2, 0x00);   /* DDRB = all input */
    via6522_set_port_b_inputs(&v, 0xAA);
    expect_eq_u8("port_b_all_input", 0xAA, via6522_read(&v, 0));
}

static void test_port_b_mixed(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 2, 0xF0);   /* DDRB: upper nibble output, lower input */
    via6522_write(&v, 0, 0x12);   /* ORB */
    via6522_set_port_b_inputs(&v, 0xCD);
    /* expected: (0x12 & 0xF0) | (0xCD & 0x0F) = 0x10 | 0x0D = 0x1D */
    expect_eq_u8("port_b_mixed", 0x1D, via6522_read(&v, 0));
}

/* ------------------------------------------------------------------ */
/* Timer 1 one-shot                                                    */
/* ------------------------------------------------------------------ */

static void test_t1_oneshot(void) {
    via6522 v;
    int i;
    uint8_t t1c_l;

    via6522_init(&v);

    /* latch = 3, one-shot (ACR bit 6 = 0, default) */
    via6522_write(&v, 4, 0x03);   /* T1L-L */
    via6522_write(&v, 5, 0x00);   /* T1C-H: load and start */

    /* Step N=3 times: counter goes 2, 1, 0 — no flag yet */
    for (i = 0; i < 3; i++) {
        via6522_step(&v);
    }
    if (v.ifr & 0x40u) {
        fail("t1_oneshot: IFR bit 6 set before underflow");
    }

    /* One more step: counter underflows to 0xFFFF, flag fires */
    via6522_step(&v);
    if (!(v.ifr & 0x40u)) {
        fail("t1_oneshot: IFR bit 6 not set after underflow");
    }

    /* One-shot: timer must have stopped */
    expect_zero_int("t1_oneshot: t1_running after underflow", v.t1_running);

    /* Counter must not advance further */
    expect_eq_u16("t1_oneshot: counter after underflow", 0xFFFF, v.t1_counter);
    via6522_step(&v);
    expect_eq_u16("t1_oneshot: counter stays at 0xFFFF", 0xFFFF, v.t1_counter);

    /* Reading T1C-L clears IFR bit 6 */
    t1c_l = via6522_read(&v, 4);
    (void)t1c_l;
    if (v.ifr & 0x40u) {
        fail("t1_oneshot: IFR bit 6 not cleared by T1C-L read");
    }
}

/* ------------------------------------------------------------------ */
/* Timer 1 free-run                                                    */
/* ------------------------------------------------------------------ */

static void test_t1_freerun(void) {
    via6522 v;
    int i;

    via6522_init(&v);

    via6522_write(&v, 11, 0x40u); /* ACR bit 6 = 1: free-run */
    via6522_write(&v, 4, 0x02);   /* T1L-L = 2 */
    via6522_write(&v, 5, 0x00);   /* T1C-H: load counter=2, start */

    /* Step 3: counter 1, 0, 0xFFFF → first underflow */
    for (i = 0; i < 3; i++) {
        via6522_step(&v);
    }
    if (!(v.ifr & 0x40u)) {
        fail("t1_freerun: IFR bit 6 not set on first underflow");
    }
    if (!v.t1_running) {
        fail("t1_freerun: t1_running cleared in free-run mode");
    }
    /* Counter must have reloaded from latch */
    expect_eq_u16("t1_freerun: counter reload after first underflow", 0x0002, v.t1_counter);

    /* Clear IFR bit 6 */
    via6522_write(&v, 13, 0x40u);

    /* Step 3 more: second underflow */
    for (i = 0; i < 3; i++) {
        via6522_step(&v);
    }
    if (!(v.ifr & 0x40u)) {
        fail("t1_freerun: IFR bit 6 not set on second underflow");
    }
    expect_eq_u16("t1_freerun: counter reload after second underflow", 0x0002, v.t1_counter);
}

/* ------------------------------------------------------------------ */
/* Timer 1 PB7 toggle                                                  */
/* ------------------------------------------------------------------ */

static void test_t1_pb7(void) {
    via6522 v;
    int i;

    via6522_init(&v);

    /* ACR bit 7 (PB7 output) + bit 6 (free-run) */
    via6522_write(&v, 11, 0xC0u);
    via6522_write(&v, 4, 0x01);   /* latch = 1 */
    via6522_write(&v, 5, 0x00);

    /* Step 2: counter 0, then 0xFFFF → first underflow, pb7 toggles 0→1 */
    for (i = 0; i < 2; i++) {
        via6522_step(&v);
    }
    expect_nonzero_int("t1_pb7: pb7_state after first underflow", v.t1_pb7_state);

    /* Step 2 more: second underflow, pb7 toggles 1→0 */
    for (i = 0; i < 2; i++) {
        via6522_step(&v);
    }
    expect_zero_int("t1_pb7: pb7_state after second underflow", v.t1_pb7_state);
}

/* ------------------------------------------------------------------ */
/* Timer 2                                                             */
/* ------------------------------------------------------------------ */

static void test_t2(void) {
    via6522 v;
    int i;
    uint8_t t2c_l;

    via6522_init(&v);

    via6522_write(&v, 8, 0x03);   /* T2C-L latch = 3 */
    via6522_write(&v, 9, 0x00);   /* T2C-H: load counter=3, start */

    /* Step 3: counter 2, 1, 0 — no flag yet */
    for (i = 0; i < 3; i++) {
        via6522_step(&v);
    }
    if (v.ifr & 0x20u) {
        fail("t2: IFR bit 5 set before underflow");
    }

    /* One more step: underflow */
    via6522_step(&v);
    if (!(v.ifr & 0x20u)) {
        fail("t2: IFR bit 5 not set after underflow");
    }

    /* T2 does not reload */
    expect_zero_int("t2: t2_running after underflow", v.t2_running);

    /* Reading T2C-L clears IFR bit 5 */
    t2c_l = via6522_read(&v, 8);
    (void)t2c_l;
    if (v.ifr & 0x20u) {
        fail("t2: IFR bit 5 not cleared by T2C-L read");
    }
}

/* ------------------------------------------------------------------ */
/* CA1 edge detection                                                  */
/* ------------------------------------------------------------------ */

static void test_ca1_negative_edge_fires(void) {
    via6522 v;
    via6522_init(&v);

    /* PCR bit 0 = 0: active on negative (high→low) edge */
    via6522_write(&v, 12, 0x00);

    via6522_set_ca1(&v, 1);  /* high: no fire */
    if (v.ifr & 0x02u) {
        fail("ca1_neg: IFR bit 1 set on rising edge");
    }

    via6522_set_ca1(&v, 0);  /* high→low: fire */
    if (!(v.ifr & 0x02u)) {
        fail("ca1_neg: IFR bit 1 not set on falling edge");
    }
}

static void test_ca1_negative_edge_no_fire_on_rising(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 12, 0x00);

    via6522_set_ca1(&v, 0);  /* low (from low): no edge */
    via6522_set_ca1(&v, 1);  /* low→high: not the active edge */
    if (v.ifr & 0x02u) {
        fail("ca1_neg: IFR bit 1 set on rising edge (should not fire)");
    }
}

static void test_ca1_positive_edge_fires(void) {
    via6522 v;
    via6522_init(&v);

    /* PCR bit 0 = 1: active on positive (low→high) edge */
    via6522_write(&v, 12, 0x01);

    via6522_set_ca1(&v, 0);  /* low (ca1_last was 0): no edge change */
    via6522_set_ca1(&v, 1);  /* low→high: fire */
    if (!(v.ifr & 0x02u)) {
        fail("ca1_pos: IFR bit 1 not set on rising edge");
    }
}

static void test_ca1_positive_edge_no_fire_on_falling(void) {
    via6522 v;
    via6522_init(&v);

    via6522_write(&v, 12, 0x01);

    /* Drive high first, clear any flag from the initial 0→1 transition */
    via6522_set_ca1(&v, 1);
    via6522_write(&v, 13, 0x02u);  /* clear IFR bit 1 */

    via6522_set_ca1(&v, 0);  /* high→low: not the active edge */
    if (v.ifr & 0x02u) {
        fail("ca1_pos: IFR bit 1 set on falling edge (should not fire)");
    }
}

/* ------------------------------------------------------------------ */
/* IFR / IER                                                           */
/* ------------------------------------------------------------------ */

static void test_ifr_ier_masking(void) {
    via6522 v;
    int i;
    uint8_t ifr_val;

    via6522_init(&v);

    /* Enable T1 interrupt (IER bit 6) */
    via6522_write(&v, 14, 0xC0u);  /* bit7=1: set bit6 */

    /* Fire T1 underflow (latch=1, step 2) */
    via6522_write(&v, 4, 0x01);
    via6522_write(&v, 5, 0x00);
    for (i = 0; i < 2; i++) {
        via6522_step(&v);
    }

    if (!(v.ifr & 0x40u)) {
        fail("ifr_ier: T1 underflow did not set IFR bit 6");
    }
    expect_nonzero_int("ifr_ier: irq_pending with IER bit 6 set", via6522_irq_pending(&v));

    /* Disable T1 interrupt (clear IER bit 6) */
    via6522_write(&v, 14, 0x40u);  /* bit7=0: clear bit6 */
    expect_zero_int("ifr_ier: irq_pending after clearing IER bit 6", via6522_irq_pending(&v));

    /* IFR bit 6 is still set even though interrupt is masked */
    if (!(v.ifr & 0x40u)) {
        fail("ifr_ier: IFR bit 6 was incorrectly cleared when IER was cleared");
    }

    /* Clearing IFR bit 6 via write */
    via6522_write(&v, 13, 0x40u);
    if (v.ifr & 0x40u) {
        fail("ifr_ier: IFR bit 6 not cleared by IFR write");
    }

    /* IFR bit 7 on read reflects masking */
    via6522_write(&v, 14, 0xC0u);   /* re-enable T1 */
    via6522_write(&v, 4, 0x01);
    via6522_write(&v, 5, 0x00);
    for (i = 0; i < 2; i++) {
        via6522_step(&v);
    }
    ifr_val = via6522_read(&v, 13);
    if (!(ifr_val & 0x80u)) {
        fail("ifr_ier: IFR bit 7 not set when IFR & IER & 0x7F != 0");
    }
    if (!(ifr_val & 0x40u)) {
        fail("ifr_ier: IFR bit 6 not visible in IFR read");
    }

    /* Clear IER, IFR bit 7 must drop */
    via6522_write(&v, 14, 0x40u);
    ifr_val = via6522_read(&v, 13);
    if (ifr_val & 0x80u) {
        fail("ifr_ier: IFR bit 7 still set after masking IER");
    }
}

/* ------------------------------------------------------------------ */
/* ORA no-handshake (reg 15) does not clear CA1 flag                  */
/* ------------------------------------------------------------------ */

static void test_ora_no_handshake_does_not_clear_ca1(void) {
    via6522 v;
    via6522_init(&v);

    /* Fire CA1 */
    via6522_set_ca1(&v, 1);
    via6522_set_ca1(&v, 0);
    if (!(v.ifr & 0x02u)) {
        fail("ora_nohs: CA1 flag not set (precondition)");
    }

    /* Read ORA without handshake (reg 15): must not clear CA1 flag */
    (void)via6522_read(&v, 15);
    if (!(v.ifr & 0x02u)) {
        fail("ora_nohs: reg 15 read cleared CA1 flag");
    }

    /* Read ORA with handshake (reg 1): must clear CA1 flag */
    (void)via6522_read(&v, 1);
    if (v.ifr & 0x02u) {
        fail("ora_nohs: reg 1 read did not clear CA1 flag");
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    test_init_zero();
    test_reset_zero();

    test_port_a_all_output();
    test_port_a_all_input();
    test_port_a_mixed();
    test_port_b_all_output();
    test_port_b_all_input();
    test_port_b_mixed();

    test_t1_oneshot();
    test_t1_freerun();
    test_t1_pb7();

    test_t2();

    test_ca1_negative_edge_fires();
    test_ca1_negative_edge_no_fire_on_rising();
    test_ca1_positive_edge_fires();
    test_ca1_positive_edge_no_fire_on_falling();

    test_ifr_ier_masking();
    test_ora_no_handshake_does_not_clear_ca1();

    printf("via6522: all tests passed\n");
    return 0;
}
