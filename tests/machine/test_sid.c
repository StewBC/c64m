#include "sid.h"
#include "audio_buffer.h"

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

    sid_init(&a);
    sid_init(&b);

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
    sid_init(&s);

    sid_write(&s, 0xD400, 0xAB);
    expect_eq_u8("writes_reach_sid: reg[0]", 0xAB, s.regs[0]);

    sid_write(&s, 0xD418, 0x0F);
    expect_eq_u8("writes_reach_sid: reg[0x18]", 0x0F, s.regs[0x18]);
}

static void test_frequency_decode(void) {
    sid s;
    sid_init(&s);

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
    sid_init(&s);

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
    sid_init(&s);

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

    sid_init(&s);

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

    sid_init(&s);

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

    sid_init(&s);
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
    sid_init(&s);

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
    float prev, curr;

    /* Count sawtooth wrap-arounds (large downward jumps) as the frequency
       indicator.  Low freq 0x0400 (period=16384 cy) yields ~2 wraps in 32768
       cycles; high freq 0x4000 (period=1024 cy) yields ~32. */

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x04); /* freq 0x0400 */
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21); /* gate+saw */
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 2048; i++) sid_advance_cycles(&s, 1); /* let attack finish */
    prev = sid_sample(&s);
    for (i = 0; i < 32768; i++) {
        sid_advance_cycles(&s, 1);
        curr = sid_sample(&s);
        if (curr < prev - 0.3f) wraps_lo++;
        prev = curr;
    }

    sid_reset(&s);
    sid_write(&s, 0xD400, 0x00); sid_write(&s, 0xD401, 0x40); /* freq 0x4000 */
    sid_write(&s, 0xD405, 0x00); sid_write(&s, 0xD406, 0xF0);
    sid_write(&s, 0xD404, 0x21);
    sid_write(&s, 0xD418, 0x0F);
    for (i = 0; i < 2048; i++) sid_advance_cycles(&s, 1);
    prev = sid_sample(&s);
    for (i = 0; i < 32768; i++) {
        sid_advance_cycles(&s, 1);
        curr = sid_sample(&s);
        if (curr < prev - 0.3f) wraps_hi++;
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
    sid_write(&s, 0xD417, 0x00); /* resonance 0 */
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
    sid_write(&s, 0xD417, 0x00);
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
    sid_write(&s, 0xD417, 0x00);
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
        sid_write(&s, 0xD417, 0x70); /* resonance 7 */
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
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
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

    /* Mixer and filter tests */
    test_volume_zero_mutes();
    test_volume_nonzero_scales();
    test_three_voices_no_clipping();
    test_filter_lp_bounded();
    test_filter_cutoff_affects_output();
    test_filter_modes_bounded();

    /* Audio flow smoke */
    test_audio_flow_smoke();

    printf("sid: all tests passed\n");
    return 0;
}
