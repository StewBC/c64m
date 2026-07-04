#include "sid.h"
#include "audio_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* CPU clocks for the two video standards (see sid.c). Existing behavioural
   tests were written against PAL constants, so they init at the PAL clock. */
#define SID_CLK_PAL   985248u
#define SID_CLK_NTSC  1022727u

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

static void expect_ne_u8(const char *name, uint8_t not_expected, uint8_t actual) {
    if (not_expected == actual) {
        fprintf(stderr, "FAIL: %s: expected value != 0x%02X\n", name, not_expected);
        exit(1);
    }
}

static void expect_eq_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected 0x%04X, got 0x%04X\n", name, expected, actual);
        exit(1);
    }
}

static void expect_zero_float(const char *name, float actual) {
    if (actual != 0.0f) {
        fprintf(stderr, "FAIL: %s: expected 0.0f, got %f\n", name, (double)actual);
        exit(1);
    }
}

static void expect_nonzero_float(const char *name, float actual) {
    if (actual == 0.0f) {
        fprintf(stderr, "FAIL: %s: expected non-zero sample\n", name);
        exit(1);
    }
}

/* ------------------------------------------------------------------ */
/* Register tests                                                      */
/* ------------------------------------------------------------------ */

static void test_reset_deterministic(void) {
    sid a, b;

    sid_init(&a, SID_CLK_PAL);
    sid_init(&b, SID_CLK_PAL);

    /* Two freshly initialised SIDs must be identical */
    if (memcmp(&a, &b, sizeof(sid)) != 0) {
        fail("reset_deterministic: two fresh SIDs differ");
    }

    /* Write something, reset, then compare again */
    sid_write(&a, 0xD400, 0x55);
    sid_reset(&a);

    if (memcmp(&a, &b, sizeof(sid)) != 0) {
        fail("reset_deterministic: reset SID differs from fresh SID");
    }
}

static void test_writes_reach_sid(void) {
    sid s;
    sid_init(&s, SID_CLK_PAL);

    sid_write(&s, 0xD400, 0xAB);
    expect_eq_u8("writes_reach_sid: reg[0]", 0xAB, s.regs[0]);

    sid_write(&s, 0xD418, 0x0F);
    expect_eq_u8("writes_reach_sid: reg[0x18]", 0x0F, s.regs[0x18]);
}

static void test_frequency_decode(void) {
    sid s;
    sid_init(&s, SID_CLK_PAL);

    /* Voice 1 freq: write LO=0x34, HI=0x12 -> freq = 0x1234 */
    sid_write(&s, 0xD400, 0x34);
    sid_write(&s, 0xD401, 0x12);
    expect_eq_u16("freq_decode: voice1.freq", 0x1234, s.voices[0].freq);

    /* Voice 2 freq: 0x5678 */
    sid_write(&s, 0xD407, 0x78);
    sid_write(&s, 0xD408, 0x56);
    expect_eq_u16("freq_decode: voice2.freq", 0x5678, s.voices[1].freq);

    /* Voice 3 freq: 0x9ABC */
    sid_write(&s, 0xD40E, 0xBC);
    sid_write(&s, 0xD40F, 0x9A);
    expect_eq_u16("freq_decode: voice3.freq", 0x9ABC, s.voices[2].freq);
}

static void test_pulse_width_12bit(void) {
    sid s;
    sid_init(&s, SID_CLK_PAL);

    /* PW_LO=0xFF, PW_HI=0xFF -> effective 12-bit = 0xFFF */
    sid_write(&s, 0xD402, 0xFF);
    sid_write(&s, 0xD403, 0xFF);
    expect_eq_u16("pw_12bit: pulse_width", 0x0FFF, s.voices[0].pulse_width);

    /* High nibble of PW_HI must be masked off */
    sid_write(&s, 0xD402, 0x34);
    sid_write(&s, 0xD403, 0xA5);  /* upper nibble A should be ignored */
    expect_eq_u16("pw_12bit: masked pulse_width", 0x0534, s.voices[0].pulse_width);
}

static void test_gate_transitions(void) {
    sid s;
    sid_init(&s, SID_CLK_PAL);

    /* Gate rising edge: envelope state should become ATTACK */
    sid_write(&s, 0xD404, 0x01); /* gate on, triangle */
    if (s.voices[0].env_state != SID_ENV_ATTACK) {
        fail("gate_rising: expected ATTACK state");
    }

    /* Gate falling edge: should become RELEASE */
    sid_write(&s, 0xD404, 0x10); /* gate off, keep triangle */
    if (s.voices[0].env_state != SID_ENV_RELEASE) {
        fail("gate_falling: expected RELEASE state");
    }
}

static void test_voice3_osc_changes(void) {
    sid s;
    uint8_t first_osc;
    uint32_t i;

    sid_init(&s, SID_CLK_PAL);

    /* Set voice 3 to sawtooth at a mid-range frequency */
    sid_write(&s, 0xD40E, 0x00);
    sid_write(&s, 0xD40F, 0x10); /* freq = 0x1000 */
    sid_write(&s, 0xD412, 0x21); /* gate on, saw */
    sid_write(&s, 0xD418, 0x0F); /* volume 15 */

    first_osc = s.voice3_osc_read;

    /* Run enough cycles for the oscillator to advance meaningfully */
    for (i = 0; i < 256; i++) {
        sid_advance_cycles(&s, 1);
    }

    if (s.voice3_osc_read == first_osc) {
        fail("voice3_osc_changes: $D41B did not change after advancing cycles");
    }
}

static void test_voice3_env_changes(void) {
    sid s;
    uint8_t first_env;
    uint32_t i;

    sid_init(&s, SID_CLK_PAL);

    /* Voice 3 sawtooth, fast attack (attack nibble = 0), gate on */
    sid_write(&s, 0xD40E, 0x00);
    sid_write(&s, 0xD40F, 0x10);
    sid_write(&s, 0xD413, 0x00); /* ATTACK_DECAY: attack=0, decay=0 */
    sid_write(&s, 0xD414, 0xF0); /* SUSTAIN_RELEASE: sustain=F */
    sid_write(&s, 0xD412, 0x21); /* gate on, saw */
    sid_write(&s, 0xD418, 0x0F);

    first_env = s.voice3_env_read;

    for (i = 0; i < 2000; i++) {
        sid_advance_cycles(&s, 1);
    }

    if (s.voice3_env_read == first_env) {
        fail("voice3_env_changes: $D41C did not change after advancing cycles");
    }
}

static void test_disabled_sample_output_preserves_readback(void) {
    sid s;
    uint8_t first_osc;
    uint8_t first_env;
    uint32_t i;

    sid_init(&s, SID_CLK_PAL);
    sid_write(&s, 0xD40E, 0x00);
    sid_write(&s, 0xD40F, 0x10);
    sid_write(&s, 0xD413, 0x00);
    sid_write(&s, 0xD414, 0xF0);
    sid_write(&s, 0xD412, 0x21);
    sid_write(&s, 0xD418, 0x0F);

    sid_set_sample_output_enabled(&s, false);
    first_osc = s.voice3_osc_read;
    first_env = s.voice3_env_read;

    for (i = 0; i < 2000; i++) {
        sid_advance_cycles(&s, 1);
    }

    if (s.voice3_osc_read == first_osc) {
        fail("disabled_sample_output: $D41B did not change");
    }
    if (s.voice3_env_read == first_env) {
        fail("disabled_sample_output: $D41C did not change");
    }
    expect_zero_float("disabled_sample_output: sample remains silent", sid_sample(&s));
}

static void test_paddle_unused_reads(void) {
    sid s;
    sid_init(&s, SID_CLK_PAL);

    expect_eq_u8("POTX_0xFF", 0xFF, sid_read(&s, 0xD419));
    expect_eq_u8("POTY_0xFF", 0xFF, sid_read(&s, 0xD41A));
    expect_eq_u8("unused_0x1D", 0x00, sid_read(&s, 0xD41D));
    expect_eq_u8("unused_0x1E", 0x00, sid_read(&s, 0xD41E));
    expect_eq_u8("unused_0x1F", 0x00, sid_read(&s, 0xD41F));
}

/* ------------------------------------------------------------------ */
/* Voice tests                                                         */
/* ------------------------------------------------------------------ */

/* Configure one voice, run cycles, collect samples into buf, verify
   that not all samples are zero and that they change. */
static void run_voice(sid *s, uint8_t control, uint8_t freq_lo, uint8_t freq_hi,
                      float *buf, uint32_t count) {
    uint32_t i;
    sid_reset(s);
    sid_write(s, 0xD400, freq_lo);
    sid_write(s, 0xD401, freq_hi);
    sid_write(s, 0xD405, 0x00); /* ATTACK_DECAY fastest */
    sid_write(s, 0xD406, 0xF0); /* SUSTAIN=max */
    sid_write(s, 0xD404, control);
    sid_write(s, 0xD418, 0x0F); /* volume 15, no filter */
    for (i = 0; i < count; i++) {
        sid_advance_cycles(s, 1);
        buf[i] = sid_sample(s);
    }
}

static void test_triangle_voice(void) {
    sid s;
    float buf[1024];
    uint32_t i;
    int nonzero = 0, changed = 0;

    run_voice(&s, 0x11, 0x00, 0x10, buf, 1024);  /* gate+triangle, freq=0x1000 */

    for (i = 0; i < 1024; i++) {
        if (buf[i] != 0.0f) nonzero++;
        if (i > 0 && buf[i] != buf[i-1]) changed++;
    }
    if (!nonzero) fail("triangle: all samples are zero");
    if (!changed) fail("triangle: samples never change");
}

static void test_saw_voice(void) {
    sid s;
    float buf[1024];
    uint32_t i;
    int nonzero = 0, changed = 0;

    run_voice(&s, 0x21, 0x00, 0x10, buf, 1024);  /* gate+saw */

    for (i = 0; i < 1024; i++) {
        if (buf[i] != 0.0f) nonzero++;
        if (i > 0 && buf[i] != buf[i-1]) changed++;
    }
    if (!nonzero) fail("saw: all samples are zero");
    if (!changed) fail("saw: samples never change");
}

static void test_pulse_voice(void) {
    sid s;
    float buf[1024];
    uint32_t i;
    int nonzero = 0, changed = 0;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD402, 0x00);
    sid_write(&s, 0xD403, 0x08); /* PW = 0x800 (50% duty) */
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x41); /* gate+pulse */
    sid_write(&s, 0xD418, 0x0F);

    for (i = 0; i < 1024; i++) {
        sid_advance_cycles(&s, 1);
        buf[i] = sid_sample(&s);
    }
    for (i = 0; i < 1024; i++) {
        if (buf[i] != 0.0f) nonzero++;
        if (i > 0 && buf[i] != buf[i-1]) changed++;
    }
    if (!nonzero) fail("pulse: all samples are zero");
    if (!changed) fail("pulse: samples never change");
}

static void test_pulse_width_affects_duty(void) {
    sid s;
    float buf_narrow[2048], buf_wide[2048];
    uint32_t i;
    int high_narrow = 0, high_wide = 0;

    /* Narrow pulse: PW = 0x100 (~6% duty) */
    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD402, 0x00);
    sid_write(&s, 0xD403, 0x01); /* PW = 0x100 */
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x41);
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 2048; i++) {
        sid_advance_cycles(&s, 1);
        buf_narrow[i] = sid_sample(&s);
    }

    /* Wide pulse: PW = 0xE00 (~87% duty) */
    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD402, 0x00);
    sid_write(&s, 0xD403, 0x0E); /* PW = 0xE00 */
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x41);
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 2048; i++) {
        sid_advance_cycles(&s, 1);
        buf_wide[i] = sid_sample(&s);
    }

    for (i = 0; i < 2048; i++) {
        if (buf_narrow[i] > 0.0f) high_narrow++;
        if (buf_wide[i]   > 0.0f) high_wide++;
    }

    /* Wide pulse should have more +1 samples than narrow pulse */
    if (high_wide <= high_narrow) {
        fail("pulse_width_duty: wider PW did not produce more high samples");
    }
}

static void test_noise_voice(void) {
    sid s;
    float buf[1024];
    uint32_t i;
    int nonzero = 0;
    float first = 0.0f;
    int changed = 0;

    run_voice(&s, 0x81, 0x00, 0x10, buf, 1024);  /* gate+noise */

    for (i = 0; i < 1024; i++) {
        if (buf[i] != 0.0f) nonzero++;
        if (i == 0) first = buf[i];
        else if (buf[i] != first) changed = 1;
    }
    if (!nonzero) fail("noise: all samples are zero");
    if (!changed) fail("noise: samples are all identical");
}

static void test_frequency_affects_output(void) {
    sid s;
    uint32_t i;
    int wraps_lo = 0, wraps_hi = 0;
    uint32_t prev, curr;

    /* Count sawtooth wrap-arounds (large downward jumps) as the frequency
       indicator.  Low freq 0x0400 (period=16384 cy) yields ~2 wraps in 32768
       cycles; high freq 0x4000 (period=1024 cy) yields ~32. */

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x04); /* freq 0x0400 */
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21); /* gate+saw */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 2048; i++) sid_advance_cycles(&s, 1); /* let attack finish */
    prev = s.voices[0].phase;
    for (i = 0; i < 32768; i++) {
        sid_advance_cycles(&s, 1);
        curr = s.voices[0].phase;
        if (curr < prev) wraps_lo++;
        prev = curr;
    }

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x40); /* freq 0x4000 */
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 2048; i++) sid_advance_cycles(&s, 1);
    prev = s.voices[0].phase;
    for (i = 0; i < 32768; i++) {
        sid_advance_cycles(&s, 1);
        curr = s.voices[0].phase;
        if (curr < prev) wraps_hi++;
        prev = curr;
    }

    if (wraps_hi <= wraps_lo) {
        fprintf(stderr, "FAIL: freq_affects_output: wraps_lo=%d wraps_hi=%d\n",
            wraps_lo, wraps_hi);
        exit(1);
    }
}

static void test_test_bit_silences(void) {
    sid s;
    float sample;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x29); /* gate + saw + test bit */
    sid_write(&s, 0xD418, 0x0F);

    for (i = 0; i < 64; i++) {
        sid_advance_cycles(&s, 1);
    }
    sample = sid_sample(&s);
    expect_zero_float("test_bit_silences: sample with TEST bit set", sample);
}

/* ------------------------------------------------------------------ */
/* ADSR tests                                                          */
/* ------------------------------------------------------------------ */

static void test_gate_on_attack_rises(void) {
    sid s;
    uint32_t i;
    uint8_t env_before;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); /* fastest attack */
    sid_write(&s, 0xD406, 0xF0); /* sustain max */
    sid_write(&s, 0xD404, 0x11); /* gate on, triangle */
    sid_write(&s, 0xD418, 0x0F);

    env_before = s.voices[0].envelope;

    for (i = 0; i < 2000; i++) {
        sid_advance_cycles(&s, 1);
    }

    if (s.voices[0].envelope <= env_before) {
        fail("attack_rises: envelope did not increase after gate on");
    }
}

static void test_attack_transitions_to_decay(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); /* fastest attack and decay */
    sid_write(&s, 0xD406, 0x00); /* sustain=0 */
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD418, 0x0F);

    /* Run long enough to complete attack (fastest = ~8 cycles * 255 = ~2040) */
    for (i = 0; i < 4000; i++) {
        sid_advance_cycles(&s, 1);
    }

    /* Should have transitioned out of ATTACK */
    if (s.voices[0].env_state == SID_ENV_ATTACK) {
        fail("attack_to_decay: still in ATTACK after expected time");
    }
}

static void test_decay_approaches_sustain(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); /* fastest attack/decay */
    sid_write(&s, 0xD406, 0x80); /* sustain=8, release=0 */
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD418, 0x0F);

    /* Run long enough for attack + decay */
    for (i = 0; i < 100000; i++) {
        sid_advance_cycles(&s, 1);
    }

    if (s.voices[0].env_state != SID_ENV_SUSTAIN) {
        fail("decay_to_sustain: did not reach SUSTAIN state");
    }
    /* Sustain level = 8 * 17 = 136 */
    if (s.voices[0].envelope != 136) {
        fprintf(stderr, "FAIL: decay_to_sustain: envelope=%u, expected 136\n",
            s.voices[0].envelope);
        exit(1);
    }
}

static void test_sustain_holds(void) {
    sid s;
    uint8_t env_a, env_b;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); /* fastest attack/decay */
    sid_write(&s, 0xD406, 0xF0); /* sustain=max */
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD418, 0x0F);

    /* Wait until sustain is reached */
    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].env_state == SID_ENV_SUSTAIN) break;
    }
    if (s.voices[0].env_state != SID_ENV_SUSTAIN) {
        fail("sustain_holds: never reached SUSTAIN");
    }

    env_a = s.voices[0].envelope;
    for (i = 0; i < 10000; i++) {
        sid_advance_cycles(&s, 1);
    }
    env_b = s.voices[0].envelope;

    if (env_a != env_b) {
        fail("sustain_holds: envelope changed during SUSTAIN while gate is on");
    }
}

static void test_gate_off_release(void) {
    sid s;
    uint8_t env_at_release;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); /* fastest attack/decay */
    sid_write(&s, 0xD406, 0xF0); /* sustain max, fastest release */
    sid_write(&s, 0xD404, 0x11); /* gate on */
    sid_write(&s, 0xD418, 0x0F);

    /* Let envelope reach sustain */
    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].env_state == SID_ENV_SUSTAIN) break;
    }

    /* Gate off: begin release */
    sid_write(&s, 0xD404, 0x10); /* gate off */
    env_at_release = s.voices[0].envelope;

    for (i = 0; i < 10000; i++) {
        sid_advance_cycles(&s, 1);
    }

    if (s.voices[0].envelope >= env_at_release) {
        fail("release_falls: envelope did not decrease after gate off");
    }
}

static void test_reset_clears_envelope(void) {
    sid s;
    sid_reset(&s);

    /* Start attack */
    sid_write(&s, 0xD404, 0x11);
    sid_advance_cycles(&s, 1000);

    /* Reset: envelope must return to zero */
    sid_reset(&s);
    if (s.voices[0].envelope != 0) {
        fail("reset_clears_envelope: envelope not zero after reset");
    }
    if (s.voices[0].env_state != SID_ENV_RELEASE) {
        fail("reset_clears_envelope: env_state not RELEASE after reset");
    }
}

/* ------------------------------------------------------------------ */
/* Exponential ADSR tests                                             */
/* ------------------------------------------------------------------ */

static void test_exp_decay_takes_longer_total(void) {
    /* Full decay from envelope=255 to 0 with fastest rate (23 cy/step).
     * Linear minimum = 255*23 = 5865 cycles.
     * Exponential total is ~17400 cycles due to slower steps at low levels.
     * We require at least 10000 cycles to confirm exponential behaviour. */
    sid s;
    uint32_t cycles;

    sid_reset(&s);
    sid_write(&s, 0xD405, 0x00); /* fastest attack=0, decay=0 */
    sid_write(&s, 0xD406, 0x00); /* sustain=0, fastest release=0 */
    sid_write(&s, 0xD404, 0x21); /* gate+saw (triggers ATTACK; overridden below) */
    sid_write(&s, 0xD418, 0x0F);
    s.voices[0].envelope    = 255u;
    s.voices[0].env_state   = SID_ENV_DECAY;
    s.voices[0].env_counter = 0.0;

    for (cycles = 0u; cycles < 40000u; cycles++) {
        if (s.voices[0].envelope == 0u) break;
        sid_advance_cycles(&s, 1);
    }

    if (s.voices[0].envelope != 0u) {
        fail("exp_decay_takes_longer: envelope never reached 0 in 40000 cycles");
    }
    if (cycles < 10000u) {
        fprintf(stderr,
            "FAIL: exp_decay_takes_longer: only %u cycles; expected >= 10000\n",
            cycles);
        exit(1);
    }
}

static void test_exp_decay_step_ratio_by_level(void) {
    /* At fastest decay rate (23 cy/step base):
     *   env=200: multiplier=1  -> ~23 cycles per step
     *   env=5:   multiplier=30 -> ~690 cycles per step
     * Expect the low-level step to take at least 20x longer. */
    sid s;
    uint32_t i;
    uint32_t cycles_high = 0u, cycles_low = 0u;

    sid_reset(&s);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0x00);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD418, 0x0F);
    s.voices[0].envelope    = 200u;
    s.voices[0].env_state   = SID_ENV_DECAY;
    s.voices[0].env_counter = 0.0;
    for (i = 1u; i <= 1000u; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].envelope < 200u) { cycles_high = i; break; }
    }
    if (!cycles_high) {
        fail("exp_decay_ratio: env=200 did not decrement in 1000 cycles");
    }

    sid_reset(&s);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0x00);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD418, 0x0F);
    s.voices[0].envelope    = 5u;
    s.voices[0].env_state   = SID_ENV_DECAY;
    s.voices[0].env_counter = 0.0;
    for (i = 1u; i <= 30000u; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].envelope < 5u) { cycles_low = i; break; }
    }
    if (!cycles_low) {
        fail("exp_decay_ratio: env=5 did not decrement in 30000 cycles");
    }

    if (cycles_low < cycles_high * 20u) {
        fprintf(stderr,
            "FAIL: exp_decay_ratio: cycles_high=%u cycles_low=%u; expected ratio>=20\n",
            cycles_high, cycles_low);
        exit(1);
    }
}

static void test_exp_release_slows_at_low_envelope(void) {
    /* Release uses the same exponential multiplier as decay.
     * env=200 (multiplier=1) should step much faster than env=5 (multiplier=30). */
    sid s;
    uint32_t i;
    uint32_t cycles_high = 0u, cycles_low = 0u;

    sid_reset(&s);
    sid_write(&s, 0xD406, 0x00); /* fastest release nibble=0 */
    s.voices[0].envelope    = 200u;
    s.voices[0].env_state   = SID_ENV_RELEASE;
    s.voices[0].env_counter = 0.0;
    for (i = 1u; i <= 1000u; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].envelope < 200u) { cycles_high = i; break; }
    }
    if (!cycles_high) {
        fail("exp_release_slows: env=200 did not decrement in 1000 cycles");
    }

    sid_reset(&s);
    sid_write(&s, 0xD406, 0x00);
    s.voices[0].envelope    = 5u;
    s.voices[0].env_state   = SID_ENV_RELEASE;
    s.voices[0].env_counter = 0.0;
    for (i = 1u; i <= 30000u; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].envelope < 5u) { cycles_low = i; break; }
    }
    if (!cycles_low) {
        fail("exp_release_slows: env=5 did not decrement in 30000 cycles");
    }

    if (cycles_low < cycles_high * 20u) {
        fprintf(stderr,
            "FAIL: exp_release_slows: cycles_high=%u cycles_low=%u; expected ratio>=20\n",
            cycles_high, cycles_low);
        exit(1);
    }
}

/* ------------------------------------------------------------------ */
/* Mixer and filter tests                                              */
/* ------------------------------------------------------------------ */

static void test_volume_zero_mutes(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11); /* gate+triangle */
    sid_write(&s, 0xD418, 0x00); /* volume=0 */

    for (i = 0; i < 10000; i++) {
        sid_advance_cycles(&s, 1);
    }
    expect_zero_float("volume_zero_mutes", sid_sample(&s));
}

static void test_volume_nonzero_scales(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD418, 0x0F); /* volume=15 */

    /* Run until attack completes (envelope > 0) */
    for (i = 0; i < 10000; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[0].envelope > 0) break;
    }
    /* Run a few more to get a non-zero waveform sample */
    for (i = 0; i < 200; i++) {
        sid_advance_cycles(&s, 1);
    }
    expect_nonzero_float("volume_nonzero_scales", sid_sample(&s));
}

static void test_three_voices_no_clipping(void) {
    sid s;
    float sample;
    uint32_t i;

    sid_reset(&s);

    /* Voice 1: triangle at freq 0x1000 */
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11);

    /* Voice 2: saw at freq 0x2000 */
    sid_write(&s, 0xD407, 0x00); sid_write(&s, 0xD408, 0x20);
    sid_write(&s, 0xD40C, 0x00); sid_write(&s, 0xD40D, 0xF0);
    sid_write(&s, 0xD40B, 0x21);

    /* Voice 3: pulse at freq 0x0800 */
    sid_write(&s, 0xD40E, 0x00); sid_write(&s, 0xD40F, 0x08);
    sid_write(&s, 0xD410, 0x00); sid_write(&s, 0xD411, 0x08);
    sid_write(&s, 0xD413, 0x00); sid_write(&s, 0xD414, 0xF0);
    sid_write(&s, 0xD412, 0x41);

    sid_write(&s, 0xD418, 0x0F); /* volume max */

    /* Run 50000 cycles, verify sample stays in [-1, +1] */
    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        sample = sid_sample(&s);
        if (sample < -1.0f || sample > 1.0f) {
            fprintf(stderr, "FAIL: three_voices_no_clipping: sample %f out of range\n",
                (double)sample);
            exit(1);
        }
    }
}

static void test_filter_lp_bounded(void) {
    sid s;
    float sample;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD415, 0x00); sid_write(&s, 0xD416, 0x40); /* cutoff mid */
    sid_write(&s, 0xD417, 0x01); /* resonance 0, route voice 1 */
    sid_write(&s, 0xD418, 0x1F); /* LP mode, volume 15 */

    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        sample = sid_sample(&s);
        if (sample < -2.0f || sample > 2.0f) {
            fprintf(stderr, "FAIL: filter_lp_bounded: sample %f wildly out of range\n",
                (double)sample);
            exit(1);
        }
    }
}

static void test_filter_cutoff_affects_output(void) {
    sid s;
    uint32_t i;
    float sum_lo = 0.0f, sum_hi = 0.0f;
    float v;

    /* Signal: triangle at freq=0x8000 (period=512 SID cycles, f_signal≈0.002).
     * Low cutoff: FC=0 → f_filter≈0.000244 → LP heavily attenuates the signal.
     * High cutoff: FC=max → f_filter=0.5 → LP passes the signal fully.
     * Warmup 20000 cycles ensures ADSR and filter both reach steady state. */

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x80);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD415, 0x00); sid_write(&s, 0xD416, 0x00); /* minimum cutoff */
    sid_write(&s, 0xD417, 0x01);
    sid_write(&s, 0xD418, 0x1F); /* LP mode, vol max */
    for (i = 0; i < 20000; i++) {
        sid_advance_cycles(&s, 1);
    }
    for (i = 0; i < 4096; i++) {
        sid_advance_cycles(&s, 1);
        v = sid_sample(&s);
        sum_lo += v > 0.0f ? v : -v;
    }

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x80);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11);
    sid_write(&s, 0xD415, 0xFF); sid_write(&s, 0xD416, 0xFF); /* maximum cutoff */
    sid_write(&s, 0xD417, 0x01);
    sid_write(&s, 0xD418, 0x1F);
    for (i = 0; i < 20000; i++) {
        sid_advance_cycles(&s, 1);
    }
    for (i = 0; i < 4096; i++) {
        sid_advance_cycles(&s, 1);
        v = sid_sample(&s);
        sum_hi += v > 0.0f ? v : -v;
    }

    if (sum_hi <= sum_lo) {
        fprintf(stderr, "FAIL: filter_cutoff_affects: sum_lo=%f sum_hi=%f\n",
            (double)sum_lo, (double)sum_hi);
        exit(1);
    }
}

static void test_filter_modes_bounded(void) {
    sid s;
    float sample;
    uint32_t i;
    uint8_t mode;

    for (mode = 1; mode <= 7; mode++) {
        sid_reset(&s);
        sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x10);
        sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
        sid_write(&s, 0xD404, 0x11);
        sid_write(&s, 0xD415, 0x00); sid_write(&s, 0xD416, 0x40);
        sid_write(&s, 0xD417, 0x71); /* resonance 7, route voice 1 */
        sid_write(&s, 0xD418, (uint8_t)(((uint8_t)(mode << 4)) | 0x0Fu));

        for (i = 0; i < 20000; i++) {
            sid_advance_cycles(&s, 1);
            sample = sid_sample(&s);
            if (sample < -2.0f || sample > 2.0f) {
                fprintf(stderr,
                    "FAIL: filter_modes_bounded: mode=0x%X sample=%f\n",
                    mode, (double)sample);
                exit(1);
            }
        }
    }
}

static float sid_abs_average_after_warmup(sid *s, uint32_t warmup, uint32_t samples);

static void test_filter_extreme_cutoff_resonance_bounded(void) {
    sid s;
    uint32_t i;
    uint16_t cutoff;
    uint8_t mode;

    for (cutoff = 0; cutoff <= 0x7FFu; cutoff += 0x7FFu) {
        for (mode = 1; mode <= 7; mode++) {
            sid_reset(&s);
            sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x80);
            sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
            sid_write(&s, 0xD404, 0x21);
            sid_write(&s, 0xD415, (uint8_t)(cutoff & 0x07u));
            sid_write(&s, 0xD416, (uint8_t)(cutoff >> 3));
            sid_write(&s, 0xD417, 0xF1); /* max resonance, route voice 1 */
            sid_write(&s, 0xD418, (uint8_t)((mode << 4) | 0x0Fu));

            for (i = 0; i < 50000; i++) {
                float sample;
                sid_advance_cycles(&s, 1);
                sample = sid_sample(&s);
                if (sample < -1.0f || sample > 1.0f) {
                    fprintf(stderr,
                        "FAIL: filter_extreme_bounded: cutoff=0x%03X mode=0x%X sample=%f\n",
                        (unsigned)cutoff, (unsigned)mode, (double)sample);
                    exit(1);
                }
            }
        }
    }
}

static void test_filter_reset_clears_state(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x80);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD415, 0xFF); sid_write(&s, 0xD416, 0xFF);
    sid_write(&s, 0xD417, 0xF1);
    sid_write(&s, 0xD418, 0x7F);
    for (i = 0; i < 20000; i++) {
        sid_advance_cycles(&s, 1);
    }
    if (s.filter_lp == 0.0f && s.filter_bp == 0.0f && s.filter_hp == 0.0f) {
        fail("filter_reset_clears_state: filter never moved before reset");
    }

    sid_reset(&s);
    expect_zero_float("filter_reset_clears_state: lp", s.filter_lp);
    expect_zero_float("filter_reset_clears_state: bp", s.filter_bp);
    expect_zero_float("filter_reset_clears_state: hp", s.filter_hp);
}

static void test_filter_mode_zero_audible_bypasses_filter(void) {
    sid s;
    float mode_zero;
    float lowpass;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x80);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD415, 0x00); sid_write(&s, 0xD416, 0x00);
    sid_write(&s, 0xD417, 0x01);
    sid_write(&s, 0xD418, 0x0F); /* mode 0 */
    mode_zero = sid_abs_average_after_warmup(&s, 30000, 4096);

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x80);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD415, 0x00); sid_write(&s, 0xD416, 0x00);
    sid_write(&s, 0xD417, 0x01);
    sid_write(&s, 0xD418, 0x1F); /* LP */
    lowpass = sid_abs_average_after_warmup(&s, 30000, 4096);

    if (mode_zero <= lowpass * 1.25f) {
        fprintf(stderr,
            "FAIL: filter_mode_zero_bypass: mode_zero=%f lowpass=%f\n",
            (double)mode_zero, (double)lowpass);
        exit(1);
    }
}

static float sid_abs_average_after_warmup(sid *s, uint32_t warmup, uint32_t samples) {
    uint32_t i;
    float sum = 0.0f;

    for (i = 0; i < warmup; i++) {
        sid_advance_cycles(s, 1);
    }
    for (i = 0; i < samples; i++) {
        float v;
        sid_advance_cycles(s, 1);
        v = sid_sample(s);
        sum += v >= 0.0f ? v : -v;
    }
    return sum / (float)samples;
}

static void sid_configure_voice1_filter_probe(sid *s, uint8_t route) {
    sid_reset(s);
    sid_write(s, 0xD400, 0x00);
    sid_write(s, 0xD401, 0x80); /* high saw frequency for visible LP effect */
    sid_write(s, 0xD405, 0x00);
    sid_write(s, 0xD406, 0xF0);
    sid_write(s, 0xD404, 0x21); /* gate+saw */
    sid_write(s, 0xD415, 0x00);
    sid_write(s, 0xD416, 0x00); /* minimum cutoff */
    sid_write(s, 0xD417, route);
    sid_write(s, 0xD418, 0x1F); /* LP mode, volume 15 */
}

static void test_filter_routing_voice1_lp_affects_output(void) {
    sid s;
    float routed;
    float bypassed;

    sid_configure_voice1_filter_probe(&s, 0x01);
    routed = sid_abs_average_after_warmup(&s, 30000, 4096);

    sid_configure_voice1_filter_probe(&s, 0x00);
    bypassed = sid_abs_average_after_warmup(&s, 30000, 4096);

    if (routed >= bypassed * 0.75f) {
        fprintf(stderr,
            "FAIL: filter_routing_voice1_lp_affects: routed=%f bypassed=%f\n",
            (double)routed, (double)bypassed);
        exit(1);
    }
}

static void test_filter_routing_unrouted_voice_bypasses_filter(void) {
    sid s;
    float bypassed;

    sid_configure_voice1_filter_probe(&s, 0x00);
    bypassed = sid_abs_average_after_warmup(&s, 30000, 4096);

    if (bypassed <= 0.001f) {
        fprintf(stderr,
            "FAIL: filter_routing_unrouted_voice_bypasses: bypassed=%f\n",
            (double)bypassed);
        exit(1);
    }
}

static void test_filter_routing_resonance_nibble_preserved(void) {
    sid s;

    sid_reset(&s);
    sid_write(&s, 0xD417, 0xA5);
    expect_eq_u8("filter_routing_resonance_nibble: reg", 0xA5, s.filter_res_route);
    if ((s.filter_res_route & 0xF0u) != 0xA0u) {
        fail("filter_routing_resonance_nibble: high nibble not preserved");
    }
    if ((s.filter_res_route & 0x07u) != 0x05u) {
        fail("filter_routing_resonance_nibble: route bits not preserved");
    }
}

static void sid_configure_voice3_only(sid *s, uint8_t route, uint8_t mode_volume) {
    sid_reset(s);
    sid_write(s, 0xD40E, 0x00);
    sid_write(s, 0xD40F, 0x40);
    sid_write(s, 0xD413, 0x00);
    sid_write(s, 0xD414, 0xF0);
    sid_write(s, 0xD412, 0x21); /* gate+saw */
    sid_write(s, 0xD415, 0xFF);
    sid_write(s, 0xD416, 0xFF);
    sid_write(s, 0xD417, route);
    sid_write(s, 0xD418, mode_volume);
}

static void test_filter_routing_voice3_disconnect_applies_to_routed_voice(void) {
    sid s;
    float connected;
    float disconnected;

    sid_configure_voice3_only(&s, 0x04, 0x1F);
    connected = sid_abs_average_after_warmup(&s, 30000, 4096);

    sid_configure_voice3_only(&s, 0x04, 0x9F); /* disconnect voice 3 */
    disconnected = sid_abs_average_after_warmup(&s, 30000, 4096);

    if (connected <= 0.001f || disconnected >= connected * 0.10f) {
        fprintf(stderr,
            "FAIL: voice3_disconnect_routed: connected=%f disconnected=%f\n",
            (double)connected, (double)disconnected);
        exit(1);
    }
}

static void test_filter_routing_voice3_disconnect_applies_to_bypass_voice(void) {
    sid s;
    float connected;
    float disconnected;

    sid_configure_voice3_only(&s, 0x00, 0x1F);
    connected = sid_abs_average_after_warmup(&s, 30000, 4096);

    sid_configure_voice3_only(&s, 0x00, 0x9F); /* disconnect voice 3 */
    disconnected = sid_abs_average_after_warmup(&s, 30000, 4096);

    if (connected <= 0.001f || disconnected >= connected * 0.10f) {
        fprintf(stderr,
            "FAIL: voice3_disconnect_bypass: connected=%f disconnected=%f\n",
            (double)connected, (double)disconnected);
        exit(1);
    }
}

static void test_sync_resets_destination_on_source_wrap(void) {
    sid s;
    uint32_t i;
    uint32_t prev_source;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x01);
    sid_write(&s, 0xD401, 0x00);
    sid_write(&s, 0xD404, 0x23); /* voice 1: gate+saw+sync to voice 3 */
    sid_write(&s, 0xD40E, 0x00);
    sid_write(&s, 0xD40F, 0x80); /* voice 3 wraps quickly */
    sid_write(&s, 0xD412, 0x21);
    sid_write(&s, 0xD418, 0x0F);

    prev_source = s.voices[2].phase;
    for (i = 0; i < 1024; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[2].phase < prev_source) {
            if (s.voices[0].phase != 0u) {
                fprintf(stderr,
                    "FAIL: sync_resets_destination: phase=%u\n",
                    (unsigned)s.voices[0].phase);
                exit(1);
            }
            return;
        }
        prev_source = s.voices[2].phase;
    }
    fail("sync_resets_destination: source did not wrap");
}

static void test_sync_disabled_preserves_destination_phase(void) {
    sid s;
    uint32_t i;
    uint32_t prev_source;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x01);
    sid_write(&s, 0xD401, 0x00);
    sid_write(&s, 0xD404, 0x21); /* voice 1: gate+saw, sync disabled */
    sid_write(&s, 0xD40E, 0x00);
    sid_write(&s, 0xD40F, 0x80);
    sid_write(&s, 0xD412, 0x21);
    sid_write(&s, 0xD418, 0x0F);

    prev_source = s.voices[2].phase;
    for (i = 0; i < 1024; i++) {
        sid_advance_cycles(&s, 1);
        if (s.voices[2].phase < prev_source) {
            if (s.voices[0].phase == 0u) {
                fail("sync_disabled_preserves_phase: destination was reset");
            }
            return;
        }
        prev_source = s.voices[2].phase;
    }
    fail("sync_disabled_preserves_phase: source did not wrap");
}

static float sid_last_wave_for_ring_probe(uint8_t control, uint32_t source_phase) {
    sid s;

    sid_reset(&s);
    sid_write(&s, 0xD404, control);
    sid_write(&s, 0xD418, 0x0F);
    s.voices[0].phase = 0x200000u;
    s.voices[2].phase = source_phase;
    sid_advance_cycles(&s, 1);
    return s.voices[0].last_wave;
}

static void test_ring_modulation_changes_triangle_output(void) {
    float low_source;
    float high_source;

    low_source = sid_last_wave_for_ring_probe(0x15, 0x000000u);  /* gate+ring+triangle */
    high_source = sid_last_wave_for_ring_probe(0x15, 0x800000u);

    if (low_source == high_source) {
        fail("ring_triangle_changes_output: samples matched");
    }
}

static void test_ring_modulation_ignored_without_triangle(void) {
    float low_source;
    float high_source;

    low_source = sid_last_wave_for_ring_probe(0x25, 0x000000u);  /* gate+ring+saw */
    high_source = sid_last_wave_for_ring_probe(0x25, 0x800000u);

    if (low_source != high_source) {
        fail("ring_without_triangle: samples differed");
    }
}

static float sid_last_wave_for_combined_probe(uint8_t control) {
    sid s;

    sid_reset(&s);
    sid_write(&s, 0xD404, control);
    sid_write(&s, 0xD418, 0x0F);
    s.voices[0].phase = 0x600000u;
    sid_advance_cycles(&s, 1);
    return s.voices[0].last_wave;
}

static void test_combined_waveform_differs_from_source_waveforms(void) {
    float triangle;
    float saw;
    float combined;

    triangle = sid_last_wave_for_combined_probe(0x11);
    saw = sid_last_wave_for_combined_probe(0x21);
    combined = sid_last_wave_for_combined_probe(0x31);

    if (combined == triangle || combined == saw) {
        fprintf(stderr,
            "FAIL: combined_waveform_differs: tri=%f saw=%f combined=%f\n",
            (double)triangle, (double)saw, (double)combined);
        exit(1);
    }
}

static void test_combined_waveform_bounded(void) {
    sid s;
    uint32_t i;
    float sample;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x40);
    sid_write(&s, 0xD402, 0x00);
    sid_write(&s, 0xD403, 0x08);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x71); /* gate+triangle+saw+pulse */
    sid_write(&s, 0xD418, 0x0F);

    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        sample = sid_sample(&s);
        if (sample < -1.0f || sample > 1.0f) {
            fprintf(stderr,
                "FAIL: combined_waveform_bounded: sample=%f\n",
                (double)sample);
            exit(1);
        }
    }
}

static void test_output_conditioning_reset_clears_state(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21); /* gate+saw */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 20000; i++) {
        sid_advance_cycles(&s, 1);
    }
    expect_nonzero_float("conditioning_reset: pre-reset output", sid_sample(&s));

    sid_reset(&s);
    expect_zero_float("conditioning_reset: sample", sid_sample(&s));
    expect_zero_float("conditioning_reset: prev_input", s.dc_block_prev_input);
    expect_zero_float("conditioning_reset: prev_output", s.dc_block_prev_output);
}

static void test_output_conditioning_silence_stays_silent(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD418, 0x0F); /* volume with no waveforms */
    for (i = 0; i < 20000; i++) {
        sid_advance_cycles(&s, 1);
    }
    expect_zero_float("conditioning_silence: sample", sid_sample(&s));
    expect_zero_float("conditioning_silence: prev_input", s.dc_block_prev_input);
    expect_zero_float("conditioning_silence: prev_output", s.dc_block_prev_output);
}

static void test_output_conditioning_constant_input_decays(void) {
    sid s;
    float early;
    float late;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00);
    sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD402, 0xFF);
    sid_write(&s, 0xD403, 0x0F); /* PW = 0xFFF => constant positive pulse */
    sid_write(&s, 0xD405, 0x00);
    sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x41); /* gate+pulse */
    sid_write(&s, 0xD418, 0x0F);

    for (i = 0; i < 3000; i++) {
        sid_advance_cycles(&s, 1);
    }
    early = sid_sample(&s);
    if (early < 0.0f) {
        early = -early;
    }

    for (i = 0; i < 80000; i++) {
        sid_advance_cycles(&s, 1);
    }
    late = sid_sample(&s);
    if (late < 0.0f) {
        late = -late;
    }

    if (late >= early * 0.25f) {
        fprintf(stderr,
            "FAIL: conditioning_constant_decays: early=%f late=%f\n",
            (double)early, (double)late);
        exit(1);
    }
}

static void test_output_conditioning_high_volume_bounded(void) {
    sid s;
    float sample;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x20);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);

    sid_write(&s, 0xD407, 0x00); sid_write(&s, 0xD408, 0x30);
    sid_write(&s, 0xD40C, 0x00); sid_write(&s, 0xD40D, 0xF0);
    sid_write(&s, 0xD40B, 0x21);

    sid_write(&s, 0xD40E, 0x00); sid_write(&s, 0xD40F, 0x40);
    sid_write(&s, 0xD413, 0x00); sid_write(&s, 0xD414, 0xF0);
    sid_write(&s, 0xD412, 0x21);
    sid_write(&s, 0xD418, 0x0F);

    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        sample = sid_sample(&s);
        if (sample < -1.0f || sample > 1.0f) {
            fprintf(stderr,
                "FAIL: conditioning_high_volume_bounded: sample=%f\n",
                (double)sample);
            exit(1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HF rolloff tests (Phase 8)                                         */
/* ------------------------------------------------------------------ */

static void test_hfroll_state_reset_to_zero(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD404, 0x81); /* gate + noise */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
    }
    expect_nonzero_float("hfroll_reset: pre-reset hfroll_state", s.hfroll_state);

    sid_reset(&s);
    expect_zero_float("hfroll_reset: hfroll_state after reset", s.hfroll_state);
}

static void test_hfroll_attenuates_high_frequency(void) {
    /* Noise waveform has wideband spectrum including HF content above the
     * output-path rolloff.  Over many samples the HF rolloff filter produces
     * a smoothed (attenuated) signal; its mean-absolute value must be
     * strictly less than the raw DC-blocked input's mean-absolute value. */
    sid s;
    uint32_t i;
    float sum_hf = 0.0f, sum_dc = 0.0f;
    float h, d;

    sid_reset(&s);
    sid_write(&s, 0xD404, 0x81); /* gate + noise */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 100000; i++) {
        sid_advance_cycles(&s, 1);
        h = s.hfroll_state;         if (h < 0.0f) h = -h;
        d = s.dc_block_prev_output; if (d < 0.0f) d = -d;
        sum_hf += h;
        sum_dc += d;
    }
    if (sum_hf >= sum_dc) {
        fprintf(stderr,
            "FAIL: hfroll_attenuates: hfroll mean-abs %f >= dc_blocked mean-abs %f\n",
            (double)sum_hf, (double)sum_dc);
        exit(1);
    }
}

static void test_hfroll_passes_low_frequency(void) {
    /* A sawtooth at ~100 Hz is well below the output-path rolloff.  After
     * collecting many samples the hfroll output mean-absolute value must
     * remain at least 80% of the DC-blocked input mean-absolute value. */
    sid s;
    uint32_t i;
    float sum_hf = 0.0f, sum_dc = 0.0f;
    float h, d;

    sid_reset(&s);
    /* Fn = round(100 × 16777216 / 985248) = 1703 = 0x06A7 */
    sid_write(&s, 0xD400, 0xA7); /* freq lo */
    sid_write(&s, 0xD401, 0x06); /* freq hi */
    sid_write(&s, 0xD405, 0x00); /* attack=0, decay=0 */
    sid_write(&s, 0xD406, 0xF0); /* sustain=F, release=0 */
    sid_write(&s, 0xD404, 0x21); /* gate + sawtooth */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 5000; i++) { sid_advance_cycles(&s, 1); } /* settle */
    for (i = 0; i < 100000; i++) {
        sid_advance_cycles(&s, 1);
        h = s.hfroll_state;         if (h < 0.0f) h = -h;
        d = s.dc_block_prev_output; if (d < 0.0f) d = -d;
        sum_hf += h;
        sum_dc += d;
    }
    if (sum_dc > 0.0f && sum_hf < 0.8f * sum_dc) {
        fprintf(stderr,
            "FAIL: hfroll_passes_low_frequency: hfroll mean-abs %f < 0.8 * dc_blocked %f\n",
            (double)sum_hf, (double)(0.8f * sum_dc));
        exit(1);
    }
}

static void test_hfroll_silence_remains_silence(void) {
    sid s;
    uint32_t i;

    sid_reset(&s);
    sid_write(&s, 0xD418, 0x0F); /* volume, no waveforms */
    for (i = 0; i < 100; i++) {
        sid_advance_cycles(&s, 1);
        expect_zero_float("hfroll_silence: sample", sid_sample(&s));
    }
    expect_zero_float("hfroll_silence: hfroll_state", s.hfroll_state);
}

static void test_hfroll_output_bounded(void) {
    sid s;
    uint32_t i;
    float sample;

    sid_reset(&s);
    sid_write(&s, 0xD404, 0x81); /* gate + noise */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 50000; i++) {
        sid_advance_cycles(&s, 1);
        sample = sid_sample(&s);
        if (sample < -1.0f || sample > 1.0f) {
            fprintf(stderr,
                "FAIL: hfroll_bounded: sample %f outside [-1,+1] at cycle %u\n",
                (double)sample, i);
            exit(1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Filter cutoff LUT tests (Phase 7)                                  */
/* ------------------------------------------------------------------ */

static void test_filter_lut_min_coefficient(void) {
    /* FC=0 must map to approximately 200 Hz: f ≈ 0.001276.
     * Accept within 10% either side. */
    float f = sid_filter_cutoff_factor(0);
    float target = 0.001276f;
    if (f < target * 0.90f || f > target * 1.10f) {
        fprintf(stderr,
            "FAIL: lut_min_coefficient: f=%f, expected ~%f (±10%%)\n",
            (double)f, (double)target);
        exit(1);
    }
}

static void test_filter_lut_max_coefficient(void) {
    /* FC=2047 must map to approximately 18000 Hz: f ≈ 0.11475.
     * Accept within 10% either side. */
    float f = sid_filter_cutoff_factor(2047);
    float target = 0.11475f;
    if (f < target * 0.90f || f > target * 1.10f) {
        fprintf(stderr,
            "FAIL: lut_max_coefficient: f=%f, expected ~%f (±10%%)\n",
            (double)f, (double)target);
        exit(1);
    }
}

static void test_filter_lut_mid_between_bounds(void) {
    /* FC=1023 (mid-register) must be between min and max, and above min. */
    float f_min = sid_filter_cutoff_factor(0);
    float f_mid = sid_filter_cutoff_factor(1023);
    float f_max = sid_filter_cutoff_factor(2047);
    if (f_mid <= f_min || f_mid >= f_max) {
        fprintf(stderr,
            "FAIL: lut_mid_between_bounds: f_min=%f f_mid=%f f_max=%f\n",
            (double)f_min, (double)f_mid, (double)f_max);
        exit(1);
    }
}

static void test_filter_lut_all_below_half(void) {
    /* All cutoff values must produce a coefficient below 0.5 for stability. */
    uint16_t cutoff;
    for (cutoff = 0; cutoff <= 2047u; cutoff++) {
        float f = sid_filter_cutoff_factor(cutoff);
        if (f >= 0.5f) {
            fprintf(stderr,
                "FAIL: lut_all_below_half: cutoff=%u f=%f >= 0.5\n",
                (unsigned)cutoff, (double)f);
            exit(1);
        }
    }
}

static void test_filter_lut_max_cutoff_passes_more_than_mid(void) {
    /* At mid-register (FC=1023, ~1900 Hz LP), a 2886 Hz saw is above cutoff
     * and is attenuated.  At max register (FC=2047, ~18000 Hz LP) the same
     * signal passes freely.  Max output must be at least 1.5× mid output. */
    sid s;
    float level_mid, level_max;
    uint8_t fc_lo, fc_hi;

    /* Mid cutoff: FC = 1023 → lo bits = 1023 & 0x07 = 7, hi = 1023 >> 3 = 127 */
    fc_lo = (uint8_t)(1023u & 0x07u);
    fc_hi = (uint8_t)(1023u >> 3);
    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0xC0); /* freq=0xC000 ~2886 Hz */
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD415, fc_lo); sid_write(&s, 0xD416, fc_hi);
    sid_write(&s, 0xD417, 0x01);
    sid_write(&s, 0xD418, 0x1F); /* LP mode, vol=15 */
    level_mid = sid_abs_average_after_warmup(&s, 30000, 4096);

    /* Max cutoff: FC = 2047 */
    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0xC0);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD415, 0xFF); sid_write(&s, 0xD416, 0xFF); /* FC=2047 */
    sid_write(&s, 0xD417, 0x01);
    sid_write(&s, 0xD418, 0x1F);
    level_max = sid_abs_average_after_warmup(&s, 30000, 4096);

    if (level_max <= level_mid * 1.5f) {
        fprintf(stderr,
            "FAIL: lut_max_passes_more_than_mid: level_mid=%f level_max=%f\n",
            (double)level_mid, (double)level_max);
        exit(1);
    }
}

/* ------------------------------------------------------------------ */
/* Audio flow smoke test (no SDL needed)                               */
/* ------------------------------------------------------------------ */

static void test_audio_flow_smoke(void) {
    sid s;
    audio_buffer *buf;
    float out[64];
    size_t written, read;
    uint32_t i;
    int nonzero = 0;

    buf = audio_buffer_create(4096);
    if (!buf) fail("audio_flow_smoke: audio_buffer_create returned NULL");

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x10);
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x11); /* gate+triangle */
    sid_write(&s, 0xD418, 0x0F); /* volume max */

    /* Advance until attack builds up, then collect samples */
    for (i = 0; i < 20000; i++) {
        sid_advance_cycles(&s, 1);
    }

    /* Simulate runtime: write SID samples into audio buffer */
    for (i = 0; i < 64; i++) {
        float sample = sid_sample(&s);
        written = audio_buffer_write(buf, &sample, 1);
        if (written != 1) fail("audio_flow_smoke: audio_buffer_write failed");
        sid_advance_cycles(&s, 20); /* ~one host sample at 48kHz from PAL */
    }

    /* Simulate SDL callback: read from buffer */
    read = audio_buffer_read(buf, out, 64);
    if (read != 64) fail("audio_flow_smoke: audio_buffer_read returned fewer than 64 samples");

    for (i = 0; i < 64; i++) {
        if (out[i] != 0.0f) nonzero++;
    }
    if (!nonzero) fail("audio_flow_smoke: all read samples are zero");

    if (audio_buffer_underrun_count(buf) != 0) {
        fail("audio_flow_smoke: unexpected underrun");
    }

    audio_buffer_destroy(buf);
}

/* ------------------------------------------------------------------ */
/* NTSC rate tables (feature: clock-parameterized SID)                 */
/* ------------------------------------------------------------------ */

/* Committed PAL baseline. These must never change without a deliberate,
   measured PAL re-tune; the test below locks them. */
static const uint32_t k_pal_attack[16] = {
      8,   31,   62,   93,  147,  216,  263,  309,
    386,  966, 1932, 3091, 3864, 11592, 19320, 30911 };
static const uint32_t k_pal_decay[16] = {
     23,   93,  185,  278,  440,  649,  788,  927,
   1159, 2897, 5794, 9271, 11592, 34775, 57959, 92734 };

/* PAL selection must be bit-identical to the committed baseline. */
static void test_pal_rate_tables_bit_identical(void) {
    sid s;
    int i;
    sid_init(&s, SID_CLK_PAL);
    if (s.cpu_clock_hz != SID_CLK_PAL) fail("pal invariant: clock not stored");
    for (i = 0; i < 16; i++) {
        if (s.attack_cycles[i] != k_pal_attack[i]) fail("pal invariant: attack table drifted");
        if (s.decay_cycles[i]  != k_pal_decay[i])  fail("pal invariant: decay table drifted");
    }
    if (s.hfroll_coeff != 0.940f)       fail("pal invariant: hfroll coeff drifted");
    if (s.cutoff_lut[0]  != 0.00127545f) fail("pal invariant: cutoff[0] drifted");
    if (s.cutoff_lut[31] != 0.11472771f) fail("pal invariant: cutoff[31] drifted");
    /* The public helper stays PAL-backed and matches the PAL LUT. */
    if (sid_filter_cutoff_factor(0) != s.cutoff_lut[0]) fail("pal invariant: helper != pal lut");
}

/* NTSC selection must be the clock-scaled / regenerated variant. */
static void test_ntsc_rate_tables_scaled(void) {
    sid p, n;
    int i;
    double ratio = (double)SID_CLK_NTSC / (double)SID_CLK_PAL;
    sid_init(&p, SID_CLK_PAL);
    sid_init(&n, SID_CLK_NTSC);
    if (n.cpu_clock_hz != SID_CLK_NTSC) fail("ntsc: clock not stored");
    for (i = 0; i < 16; i++) {
        uint32_t exp_a = (uint32_t)floor((double)k_pal_attack[i] * ratio + 0.5);
        uint32_t exp_d = (uint32_t)floor((double)k_pal_decay[i]  * ratio + 0.5);
        if (n.attack_cycles[i] != exp_a) fail("ntsc: attack != round(pal*ratio)");
        if (n.decay_cycles[i]  != exp_d) fail("ntsc: decay != round(pal*ratio)");
    }
    if (n.hfroll_coeff != 0.942f) fail("ntsc: hfroll coeff wrong");
    /* Higher clock -> smaller SVF cutoff coefficient at every anchor. */
    for (i = 1; i < 32; i++) {
        if (!(n.cutoff_lut[i] < p.cutoff_lut[i])) fail("ntsc: cutoff not below pal");
    }
}

/* The point of the feature: the same musical envelope time in absolute seconds
   on both standards, differing only by integer-cycle rounding (<= 1 CPU cycle). */
static void test_envelope_absolute_time_preserved(void) {
    sid p, n;
    int i;
    sid_init(&p, SID_CLK_PAL);
    sid_init(&n, SID_CLK_NTSC);
    for (i = 0; i < 16; i++) {
        double pal_t   = (double)p.attack_cycles[i] / (double)SID_CLK_PAL;
        double ntsc_t  = (double)n.attack_cycles[i] / (double)SID_CLK_NTSC;
        double one_cyc = 1.0 / (double)SID_CLK_NTSC;
        if (fabs(pal_t - ntsc_t) > one_cyc) fail("envelope: PAL/NTSC absolute time off by >1 cycle");
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    test_pal_rate_tables_bit_identical();
    test_ntsc_rate_tables_scaled();
    test_envelope_absolute_time_preserved();

    /* Register tests */
    test_reset_deterministic();
    test_writes_reach_sid();
    test_frequency_decode();
    test_pulse_width_12bit();
    test_gate_transitions();
    test_voice3_osc_changes();
    test_voice3_env_changes();
    test_disabled_sample_output_preserves_readback();
    test_paddle_unused_reads();

    /* Voice tests */
    test_triangle_voice();
    test_saw_voice();
    test_pulse_voice();
    test_pulse_width_affects_duty();
    test_noise_voice();
    test_frequency_affects_output();
    test_test_bit_silences();

    /* ADSR tests */
    test_gate_on_attack_rises();
    test_attack_transitions_to_decay();
    test_decay_approaches_sustain();
    test_sustain_holds();
    test_gate_off_release();
    test_reset_clears_envelope();

    /* Exponential ADSR tests */
    test_exp_decay_takes_longer_total();
    test_exp_decay_step_ratio_by_level();
    test_exp_release_slows_at_low_envelope();

    /* Mixer and filter tests */
    test_volume_zero_mutes();
    test_volume_nonzero_scales();
    test_three_voices_no_clipping();
    test_filter_lp_bounded();
    test_filter_cutoff_affects_output();
    test_filter_modes_bounded();
    test_filter_extreme_cutoff_resonance_bounded();
    test_filter_reset_clears_state();
    test_filter_mode_zero_audible_bypasses_filter();
    test_filter_routing_voice1_lp_affects_output();
    test_filter_routing_unrouted_voice_bypasses_filter();
    test_filter_routing_resonance_nibble_preserved();
    test_filter_routing_voice3_disconnect_applies_to_routed_voice();
    test_filter_routing_voice3_disconnect_applies_to_bypass_voice();
    test_sync_resets_destination_on_source_wrap();
    test_sync_disabled_preserves_destination_phase();
    test_ring_modulation_changes_triangle_output();
    test_ring_modulation_ignored_without_triangle();
    test_combined_waveform_differs_from_source_waveforms();
    test_combined_waveform_bounded();
    test_output_conditioning_reset_clears_state();
    test_output_conditioning_silence_stays_silent();
    test_output_conditioning_constant_input_decays();
    test_output_conditioning_high_volume_bounded();

    /* HF rolloff tests (Phase 8) */
    test_hfroll_state_reset_to_zero();
    test_hfroll_attenuates_high_frequency();
    test_hfroll_passes_low_frequency();
    test_hfroll_silence_remains_silence();
    test_hfroll_output_bounded();

    /* Filter cutoff LUT tests */
    test_filter_lut_min_coefficient();
    test_filter_lut_max_coefficient();
    test_filter_lut_mid_between_bounds();
    test_filter_lut_all_below_half();
    test_filter_lut_max_cutoff_passes_more_than_mid();

    /* Audio flow smoke */
    test_audio_flow_smoke();

    printf("sid: all tests passed\n");
    return 0;
}
