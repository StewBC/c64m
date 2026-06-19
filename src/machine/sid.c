#include "sid.h"

#include <string.h>

/*
 * Attack time at PAL 985248 Hz: cycles per envelope +1 step (255 steps, 0->255).
 * Times (ms): 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000
 */
static const uint32_t s_attack_cycles[16] = {
      8,   31,   62,   93,  147,  216,  263,  309,
    386,  966, 1932, 3091, 3864, 11592, 19320, 30911
};

/*
 * Decay/release at PAL 985248 Hz: cycles per envelope -1 step (255 steps, 255->0).
 * Times (ms): 6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000
 */
static const uint32_t s_decay_cycles[16] = {
     23,   93,  185,  278,  440,  649,  788,  927,
   1159, 2897, 5794, 9271, 11592, 34775, 57959, 92734
};

static float sid_clampf(float v) {
    if (v < -1.0f) return -1.0f;
    if (v >  1.0f) return  1.0f;
    return v;
}

static float sid_clampf_ext(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void sid_voice_init(sid_voice *v) {
    memset(v, 0, sizeof(*v));
    v->noise_lfsr = 0x7FFFFFu;   /* nonzero seed required */
    v->env_state  = SID_ENV_RELEASE;
}

void sid_init(sid *s) {
    int i;
    if (!s) return;
    memset(s, 0, sizeof(*s));
    for (i = 0; i < 3; i++) {
        sid_voice_init(&s->voices[i]);
    }
}

void sid_reset(sid *s) {
    sid_init(s);
}

/* ------------------------------------------------------------------ */
/* Register write                                                      */
/* ------------------------------------------------------------------ */

void sid_write(sid *s, uint16_t addr, uint8_t value) {
    uint8_t reg;
    uint8_t old_val;
    int vi, base, local;
    sid_voice *v;

    if (!s) return;
    reg     = (uint8_t)(addr & 0x1Fu);
    old_val = s->regs[reg];
    s->regs[reg] = value;

    if (reg <= 0x14u) {
        /* Voice registers 0x00-0x06 (v1), 0x07-0x0D (v2), 0x0E-0x14 (v3) */
        if      (reg <  7u) { vi = 0; base =  0; }
        else if (reg < 14u) { vi = 1; base =  7; }
        else                { vi = 2; base = 14; }

        local = (int)reg - base;
        v     = &s->voices[vi];

        switch (local) {
            case 0: /* FREQ_LO */
            case 1: /* FREQ_HI */
                v->freq = (uint16_t)((uint16_t)s->regs[base] |
                    ((uint16_t)s->regs[base + 1] << 8));
                break;

            case 2: /* PW_LO */
            case 3: /* PW_HI */
                v->pulse_width = (uint16_t)((uint16_t)s->regs[base + 2] |
                    (((uint16_t)(s->regs[base + 3] & 0x0Fu)) << 8));
                break;

            case 4: { /* CONTROL */
                uint8_t old_gate = old_val  & 0x01u;
                uint8_t new_gate = value    & 0x01u;
                v->control = value;
                if (value & 0x08u) {        /* TEST bit: silence and reset phase */
                    v->phase     = 0;
                    v->last_wave = 0.0f;
                }
                if (!old_gate && new_gate) {          /* gate rising: begin attack */
                    v->env_state   = SID_ENV_ATTACK;
                    v->env_counter = 0.0;
                } else if (old_gate && !new_gate) {   /* gate falling: begin release */
                    v->env_state = SID_ENV_RELEASE;
                }
                break;
            }

            case 5: v->attack_decay    = value; break;
            case 6: v->sustain_release = value; break;
            default: break;
        }
    } else if (reg == 0x15u || reg == 0x16u) { /* FC_LO, FC_HI */
        /* 11-bit cutoff: bits 2..0 from FC_LO, bits 10..3 from FC_HI */
        s->filter_cutoff = (uint16_t)(
            ((uint16_t)s->regs[0x16] << 3) |
            ((uint16_t)(s->regs[0x15] & 0x07u)));
    } else if (reg == 0x17u) {  /* RES_FILT */
        s->filter_res_route = value;
    } else if (reg == 0x18u) {  /* MODE_VOL */
        s->mode_volume = value;
    }
    /* 0x19-0x1F: stored in regs[] but no decoded mutable state */
}

/* ------------------------------------------------------------------ */
/* Register read                                                       */
/* ------------------------------------------------------------------ */

uint8_t sid_read(sid *s, uint16_t addr) {
    uint8_t reg;
    if (!s) return 0xFFu;
    reg = (uint8_t)(addr & 0x1Fu);
    switch (reg) {
        case 0x19u: return 0xFFu;               /* POTX: not connected */
        case 0x1Au: return 0xFFu;               /* POTY: not connected */
        case 0x1Bu: return s->voice3_osc_read;  /* OSC3 */
        case 0x1Cu: return s->voice3_env_read;  /* ENV3 */
        case 0x1Du:
        case 0x1Eu:
        case 0x1Fu: return 0u;                  /* unused */
        default:    return s->regs[reg];        /* last written value */
    }
}

/* ------------------------------------------------------------------ */
/* Oscillator waveform                                                 */
/* ------------------------------------------------------------------ */

/*
 * Compute one waveform sample for a voice.  Updates noise LFSR on
 * oscillator bit-19 low->high transition.  Returns a value in
 * approximately [-1.0, +1.0].
 *
 * Combined waveform policy: noise > pulse > saw > triangle.
 * Exact combined-waveform analog behaviour is deferred.
 */
static float sid_voice_waveform(sid_voice *v, uint32_t prev_phase) {
    uint8_t  ctrl  = v->control;
    uint32_t phase = v->phase;
    uint32_t t, thresh;
    uint8_t  noise_byte;

    if (ctrl & 0x08u) {           /* TEST bit: silence */
        return 0.0f;
    }
    if ((ctrl & 0xF0u) == 0u) {  /* no waveform selected */
        return 0.0f;
    }

    if (ctrl & 0x80u) {           /* NOISE */
        /* Clock 23-bit LFSR (taps 22, 17) on oscillator bit-19 0->1 edge */
        if ((prev_phase & 0x080000u) == 0u && (phase & 0x080000u) != 0u) {
            uint32_t lfsr = v->noise_lfsr;
            uint32_t fb   = ((lfsr >> 22) ^ (lfsr >> 17)) & 1u;
            lfsr = ((lfsr << 1) | fb) & 0x7FFFFFu;
            if (!lfsr) lfsr = 1u;   /* keep nonzero */
            v->noise_lfsr = lfsr;
        }
        /* Output byte from documented LFSR bit positions */
        {
            uint32_t lfsr = v->noise_lfsr;
            noise_byte = (uint8_t)(
                ((lfsr >> 13) & 0x80u) |   /* bit 20 -> out bit 7 */
                ((lfsr >> 12) & 0x40u) |   /* bit 18 -> out bit 6 */
                ((lfsr >>  9) & 0x20u) |   /* bit 14 -> out bit 5 */
                ((lfsr >>  7) & 0x10u) |   /* bit 11 -> out bit 4 */
                ((lfsr >>  6) & 0x08u) |   /* bit  9 -> out bit 3 */
                ((lfsr >>  3) & 0x04u) |   /* bit  5 -> out bit 2 */
                ((lfsr >>  1) & 0x02u) |   /* bit  2 -> out bit 1 */
                ( lfsr        & 0x01u));   /* bit  0 -> out bit 0 */
        }
        return (float)noise_byte / 127.5f - 1.0f;

    } else if (ctrl & 0x40u) {    /* PULSE */
        if (v->pulse_width == 0u) return -1.0f;
        if (v->pulse_width >= 0xFFFu) return 1.0f;
        thresh = (uint32_t)v->pulse_width << 12;
        return (phase < thresh) ? 1.0f : -1.0f;

    } else if (ctrl & 0x20u) {    /* SAW */
        return 2.0f * ((float)(phase & 0x00FFFFFFu) / (float)0x01000000u) - 1.0f;

    } else {                       /* TRIANGLE (ctrl & 0x10) */
        t = phase;
        if (t & 0x800000u) t = (~t) & 0x7FFFFFu;
        else                t =   t  & 0x7FFFFFu;
        return 2.0f * (float)t / (float)0x00800000u - 1.0f;
    }
}

/* ------------------------------------------------------------------ */
/* ADSR envelope                                                       */
/* ------------------------------------------------------------------ */

static void sid_voice_advance_env(sid_voice *v) {
    uint8_t attack_rate  = (v->attack_decay  >> 4) & 0x0Fu;
    uint8_t decay_rate   =  v->attack_decay         & 0x0Fu;
    uint8_t sustain_lvl  = (v->sustain_release >> 4) & 0x0Fu;
    uint8_t release_rate =  v->sustain_release        & 0x0Fu;
    uint8_t sustain_val  = (uint8_t)(sustain_lvl * 17u); /* 0..255 */

    switch (v->env_state) {
        case SID_ENV_ATTACK:
            v->env_counter += 1.0 / (double)s_attack_cycles[attack_rate];
            if (v->env_counter >= 1.0) {
                v->env_counter -= 1.0;
                if (v->envelope < 255u) v->envelope++;
                if (v->envelope >= 255u) {
                    v->envelope    = 255u;
                    v->env_state   = SID_ENV_DECAY;
                    v->env_counter = 0.0;
                }
            }
            break;

        case SID_ENV_DECAY:
            v->env_counter += 1.0 / (double)s_decay_cycles[decay_rate];
            if (v->env_counter >= 1.0) {
                v->env_counter -= 1.0;
                if (v->envelope > sustain_val) v->envelope--;
                if (v->envelope <= sustain_val) {
                    v->envelope  = sustain_val;
                    v->env_state = SID_ENV_SUSTAIN;
                }
            }
            break;

        case SID_ENV_SUSTAIN:
            v->envelope = sustain_val;
            break;

        case SID_ENV_RELEASE:
            v->env_counter += 1.0 / (double)s_decay_cycles[release_rate];
            if (v->env_counter >= 1.0) {
                v->env_counter -= 1.0;
                if (v->envelope > 0u) v->envelope--;
            }
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Cycle advance: oscillators + ADSR + mix + filter                   */
/* ------------------------------------------------------------------ */

void sid_advance_cycles(sid *s, uint32_t cycles) {
    uint32_t c, i;
    float    v1, v2, v3, mixed, vol_gain;
    float    f, q;
    uint8_t  res_nibble, mode;

    if (!s || cycles == 0u) return;

    for (c = 0u; c < cycles; c++) {
        /* --- Advance each voice --- */
        for (i = 0u; i < 3u; i++) {
            sid_voice *v    = &s->voices[i];
            uint32_t   prev = v->phase;

            if (!(v->control & 0x08u)) {   /* advance phase unless TEST bit */
                v->phase = (v->phase + (uint32_t)v->freq) & 0x00FFFFFFu;
            }
            v->last_wave = sid_voice_waveform(v, prev);
            sid_voice_advance_env(v);
        }

        /* --- Voice 3 read-back registers --- */
        s->voice3_osc_read = (uint8_t)(s->voices[2].phase >> 16);
        s->voice3_env_read =  s->voices[2].envelope;

        /* --- Mixer ---
         * Per-voice filter routing ($D417 bits 3..0) is deferred;
         * the full mixed signal is routed through the filter.
         * Voice 3 output is disconnected when $D418 bit 7 is set. */
        v1 = s->voices[0].last_wave * ((float)s->voices[0].envelope / 255.0f);
        v2 = s->voices[1].last_wave * ((float)s->voices[1].envelope / 255.0f);
        v3 = (s->mode_volume & 0x80u) ? 0.0f :
             s->voices[2].last_wave * ((float)s->voices[2].envelope / 255.0f);

        vol_gain = (float)(s->mode_volume & 0x0Fu) / 15.0f;
        mixed    = sid_clampf((v1 + v2 + v3) / 3.0f * vol_gain);

        /* --- State-variable filter ---
         * f: cutoff factor 0..0.5 (always stable).
         * q: resonance damping 0.1..1.0. */
        f = (float)(s->filter_cutoff + 1u) / 4096.0f;

        res_nibble = (s->filter_res_route >> 4) & 0x0Fu;
        q = 1.0f - (float)res_nibble / 20.0f;
        if (q < 0.1f) q = 0.1f;

        /* Chamberlin SVF (hp computed first for stability) */
        s->filter_hp  = mixed - s->filter_lp - q * s->filter_bp;
        s->filter_bp += f * s->filter_hp;
        s->filter_lp += f * s->filter_bp;

        /* Clamp intermediate states to prevent runaway */
        s->filter_lp = sid_clampf_ext(s->filter_lp, -2.0f, 2.0f);
        s->filter_bp = sid_clampf_ext(s->filter_bp, -2.0f, 2.0f);
        s->filter_hp = sid_clampf_ext(s->filter_hp, -2.0f, 2.0f);

        /* --- Select filter output ---
         * $D418 bits 4..6: LP=bit4, BP=bit5, HP=bit6.
         * When no mode bits are set, bypass the filter entirely. */
        mode = (s->mode_volume >> 4) & 0x07u;
        if (mode == 0u) {
            s->last_sample = mixed;
        } else {
            float out = 0.0f;
            if (mode & 0x01u) out += s->filter_lp;   /* LP */
            if (mode & 0x02u) out += s->filter_bp;   /* BP */
            if (mode & 0x04u) out += s->filter_hp;   /* HP */
            s->last_sample = sid_clampf(out);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sample read                                                         */
/* ------------------------------------------------------------------ */

float sid_sample(const sid *s) {
    if (!s) return 0.0f;
    return s->last_sample;
}
