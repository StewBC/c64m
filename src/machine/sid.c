#include "sid.h"

#include <string.h>

/* Envelope rate tables are cycles-per-step counts derived from the SID's real
 * (absolute-time) step periods, so they scale with the CPU (Ø2) clock. The PAL
 * tables are the committed baseline and must stay verbatim (PAL is bit-identical
 * to the pre-clock-parameterization output). The NTSC tables are the PAL values
 * scaled by the clock ratio 1022727/985248 (round-to-nearest). sid_init selects
 * one pair by the active clock. */
#define SID_CPU_CLOCK_PAL   985248u
#define SID_CPU_CLOCK_NTSC  1022727u

/*
 * Attack time at PAL 985248 Hz: cycles per envelope +1 step (255 steps, 0->255).
 * Times (ms): 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000
 */
static const uint32_t s_attack_cycles_pal[16] = {
      8,   31,   62,   93,  147,  216,  263,  309,
    386,  966, 1932, 3091, 3864, 11592, 19320, 30911
};
/* NTSC 1022727 Hz: round(pal * 1022727/985248). */
static const uint32_t s_attack_cycles_ntsc[16] = {
      8,   32,   64,   97,  153,  224,  273,  321,
    401, 1003, 2005, 3209, 4011, 12033, 20055, 32087
};

/*
 * Decay/release at PAL 985248 Hz: cycles per envelope -1 step (255 steps, 255->0).
 * Times (ms): 6, 24, 48, 72, 114, 168, 204, 240, 300, 750, 1500, 2400, 3000, 9000, 15000, 24000
 */
static const uint32_t s_decay_cycles_pal[16] = {
     23,   93,  185,  278,  440,  649,  788,  927,
   1159, 2897, 5794, 9271, 11592, 34775, 57959, 92734
};
/* NTSC 1022727 Hz: round(pal * 1022727/985248). */
static const uint32_t s_decay_cycles_ntsc[16] = {
     24,   97,  192,  289,  457,  674,  818,  962,
   1203, 3007, 6014, 9624, 12033, 36098, 60164, 96262
};

enum {
    SID_OUTPUT_GAIN_PERCENT = 20
};

static const float SID_DC_BLOCK_R   = 0.99987f;
/* One-pole IIR LP: models 6581 output-path rolloff (~9.4 kHz combined chip+board).
 * a = 1 - 2π × 9400 / clock. Applied after DC blocker, before gain.
 * PAL (985248) ≈ 0.940; NTSC (1022727) ≈ 0.942. Selected by the active clock. */
#define SID_HFROLL_COEFF_PAL   0.940f
#define SID_HFROLL_COEFF_NTSC  0.942f

/* Exponential cutoff LUT: maps 11-bit register [0..2047] to Chamberlin SVF
 * coefficient f = 2*sin(pi*fc/985248), where fc spans 200 Hz to 18000 Hz.
 * 32 anchor points with linear interpolation gives ~66-register resolution.
 * fc[i] = 200 * (90)^(i/31); real 6581 range is approximately 200-18000 Hz. */
static const float s_cutoff_lut_pal[32] = {
    /* i= 0  fc=   200.0 Hz */  0.00127545f,
    /* i= 1  fc=   231.2 Hz */  0.00147470f,
    /* i= 2  fc=   267.4 Hz */  0.00170508f,
    /* i= 3  fc=   309.1 Hz */  0.00197144f,
    /* i= 4  fc=   357.4 Hz */  0.00227942f,
    /* i= 5  fc=   413.3 Hz */  0.00263551f,
    /* i= 6  fc=   477.8 Hz */  0.00304723f,
    /* i= 7  fc=   552.5 Hz */  0.00352326f,
    /* i= 8  fc=   638.8 Hz */  0.00407366f,
    /* i= 9  fc=   738.6 Hz */  0.00471004f,
    /* i=10  fc=   853.9 Hz */  0.00544584f,
    /* i=11  fc=   987.3 Hz */  0.00629658f,
    /* i=12  fc=  1141.6 Hz */  0.00728022f,
    /* i=13  fc=  1319.9 Hz */  0.00841752f,
    /* i=14  fc=  1526.1 Hz */  0.00973248f,
    /* i=15  fc=  1764.5 Hz */  0.01125287f,
    /* i=16  fc=  2040.2 Hz */  0.01301076f,
    /* i=17  fc=  2358.9 Hz */  0.01504325f,
    /* i=18  fc=  2727.4 Hz */  0.01739323f,
    /* i=19  fc=  3153.5 Hz */  0.02011030f,
    /* i=20  fc=  3646.1 Hz */  0.02325178f,
    /* i=21  fc=  4215.7 Hz */  0.02688394f,
    /* i=22  fc=  4874.3 Hz */  0.03108341f,
    /* i=23  fc=  5635.8 Hz */  0.03593874f,
    /* i=24  fc=  6516.2 Hz */  0.04155229f,
    /* i=25  fc=  7534.1 Hz */  0.04804238f,
    /* i=26  fc=  8711.1 Hz */  0.05554571f,
    /* i=27  fc= 10071.9 Hz */  0.06422023f,
    /* i=28  fc= 11645.3 Hz */  0.07424834f,
    /* i=29  fc= 13464.6 Hz */  0.08584069f,
    /* i=30  fc= 15568.0 Hz */  0.09924036f,
    /* i=31  fc= 18000.0 Hz */  0.11472771f
};

/* NTSC 1022727 Hz: same anchors fc[i]=200*90^(i/31), coeff = 2*sin(pi*fc/clock).
 * Generated from the identical closed form as PAL (which reproduces the PAL
 * values above exactly at 985248), only the clock denominator differs. */
static const float s_cutoff_lut_ntsc[32] = {
    /* i= 0  fc=   200.0 Hz */  0.00122871f,
    /* i= 1  fc=   231.2 Hz */  0.00142066f,
    /* i= 2  fc=   267.4 Hz */  0.00164259f,
    /* i= 3  fc=   309.1 Hz */  0.00189920f,
    /* i= 4  fc=   357.4 Hz */  0.00219589f,
    /* i= 5  fc=   413.3 Hz */  0.00253893f,
    /* i= 6  fc=   477.8 Hz */  0.00293556f,
    /* i= 7  fc=   552.5 Hz */  0.00339415f,
    /* i= 8  fc=   638.8 Hz */  0.00392438f,
    /* i= 9  fc=   738.6 Hz */  0.00453744f,
    /* i=10  fc=   853.9 Hz */  0.00524627f,
    /* i=11  fc=   987.3 Hz */  0.00606583f,
    /* i=12  fc=  1141.6 Hz */  0.00701343f,
    /* i=13  fc=  1319.9 Hz */  0.00810905f,
    /* i=14  fc=  1526.1 Hz */  0.00937583f,
    /* i=15  fc=  1764.5 Hz */  0.01084050f,
    /* i=16  fc=  2040.2 Hz */  0.01253397f,
    /* i=17  fc=  2358.9 Hz */  0.01449198f,
    /* i=18  fc=  2727.4 Hz */  0.01675585f,
    /* i=19  fc=  3153.5 Hz */  0.01937336f,
    /* i=20  fc=  3646.1 Hz */  0.02239972f,
    /* i=21  fc=  4215.7 Hz */  0.02589880f,
    /* i=22  fc=  4874.3 Hz */  0.02994441f,
    /* i=23  fc=  5635.8 Hz */  0.03462185f,
    /* i=24  fc=  6516.2 Hz */  0.04002977f,
    /* i=25  fc=  7534.1 Hz */  0.04628214f,
    /* i=26  fc=  8711.1 Hz */  0.05351067f,
    /* i=27  fc= 10071.9 Hz */  0.06186757f,
    /* i=28  fc= 11645.3 Hz */  0.07152861f,
    /* i=29  fc= 13464.6 Hz */  0.08269679f,
    /* i=30  fc= 15568.0 Hz */  0.09560641f,
    /* i=31  fc= 18000.0 Hz */  0.11052775f
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

/* Select the per-standard rate tables/coefficients for the given clock. Any
   clock other than NTSC selects the PAL baseline (bit-identical output). */
static void sid_select_clock(sid *s, uint32_t cpu_clock_hz) {
    s->cpu_clock_hz = cpu_clock_hz;
    if (cpu_clock_hz == SID_CPU_CLOCK_NTSC) {
        s->attack_cycles = s_attack_cycles_ntsc;
        s->decay_cycles  = s_decay_cycles_ntsc;
        s->cutoff_lut    = s_cutoff_lut_ntsc;
        s->hfroll_coeff  = SID_HFROLL_COEFF_NTSC;
    } else {
        s->attack_cycles = s_attack_cycles_pal;
        s->decay_cycles  = s_decay_cycles_pal;
        s->cutoff_lut    = s_cutoff_lut_pal;
        s->hfroll_coeff  = SID_HFROLL_COEFF_PAL;
    }
}

void sid_init(sid *s, uint32_t cpu_clock_hz) {
    int i;
    if (!s) return;
    memset(s, 0, sizeof(*s));
    sid_select_clock(s, cpu_clock_hz);
    s->sample_output_enabled = true;
    for (i = 0; i < 3; i++) {
        sid_voice_init(&s->voices[i]);
    }
}

void sid_reset(sid *s) {
    if (!s) return;
    sid_init(s, s->cpu_clock_hz ? s->cpu_clock_hz : SID_CPU_CLOCK_PAL);
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

void sid_set_sample_output_enabled(sid *s, bool enabled) {
    if (!s) return;

    s->sample_output_enabled = enabled;
}

/* ------------------------------------------------------------------ */
/* Register read                                                       */
/* ------------------------------------------------------------------ */

uint8_t sid_read(sid *s, uint16_t addr) {
    return sid_debug_read(s, addr);
}

uint8_t sid_debug_read(const sid *s, uint16_t addr) {
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

static uint8_t sid_triangle_byte(const sid_voice *v, uint32_t source_phase) {
    uint32_t t;

    t = v->phase;
    if ((v->control & 0x04u) != 0u && (source_phase & 0x800000u) != 0u) {
        t ^= 0x800000u;
    }
    if (t & 0x800000u) {
        t = (~t) & 0x7FFFFFu;
    } else {
        t &= 0x7FFFFFu;
    }
    return (uint8_t)(t >> 15);
}

static uint8_t sid_saw_byte(const sid_voice *v) {
    return (uint8_t)(v->phase >> 16);
}

static uint8_t sid_pulse_byte(const sid_voice *v) {
    uint32_t thresh;

    if (v->pulse_width == 0u) {
        return 0u;
    }
    if (v->pulse_width >= 0xFFFu) {
        return 255u;
    }
    thresh = (uint32_t)v->pulse_width << 12;
    return (v->phase < thresh) ? 255u : 0u;
}

static uint8_t sid_noise_byte(sid_voice *v, uint32_t prev_phase) {
    uint8_t noise_byte;

    if ((prev_phase & 0x080000u) == 0u && (v->phase & 0x080000u) != 0u) {
        uint32_t lfsr = v->noise_lfsr;
        uint32_t fb   = ((lfsr >> 22) ^ (lfsr >> 17)) & 1u;
        lfsr = ((lfsr << 1) | fb) & 0x7FFFFFu;
        if (!lfsr) lfsr = 1u;   /* keep nonzero */
        v->noise_lfsr = lfsr;
    }

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
    return noise_byte;
}

/*
 * Compute one waveform sample for a voice.  Updates noise LFSR on oscillator
 * bit-19 low->high transition.  Returns approximately [-1.0, +1.0].
 *
 * Ring modulation affects triangle generation using the previous voice's phase
 * high bit.  Combined waveforms use a deterministic bitwise/AND-style blend in
 * an unsigned 8-bit waveform domain.  This is a functional approximation, not
 * exact analog 6581 combined-waveform behavior.
 */
static float sid_voice_waveform(sid_voice *v, uint32_t prev_phase, uint32_t source_phase) {
    uint8_t  ctrl  = v->control;
    uint8_t  wave_mask;
    uint8_t  combined = 0xFFu;

    if (ctrl & 0x08u) {           /* TEST bit: silence */
        return 0.0f;
    }
    wave_mask = ctrl & 0xF0u;
    if (wave_mask == 0u) {  /* no waveform selected */
        return 0.0f;
    }

    if (wave_mask & 0x10u) combined &= sid_triangle_byte(v, source_phase);
    if (wave_mask & 0x20u) combined &= sid_saw_byte(v);
    if (wave_mask & 0x40u) combined &= sid_pulse_byte(v);
    if (wave_mask & 0x80u) combined &= sid_noise_byte(v, prev_phase);

    return (float)combined / 127.5f - 1.0f;
}

/* ------------------------------------------------------------------ */
/* ADSR envelope                                                       */
/* ------------------------------------------------------------------ */

/* Exponential period multiplier for decay/release: the real 6581 slows the
 * step rate as the envelope value drops, producing a natural decay curve.
 * Attack is linear and does not use this multiplier. */
static uint32_t sid_exp_period(uint8_t env) {
    if (env >= 93u) return  1u;
    if (env >= 54u) return  2u;
    if (env >= 26u) return  4u;
    if (env >= 14u) return  8u;
    if (env >=  6u) return 16u;
    return 30u;
}

static void sid_voice_advance_env(sid_voice *v,
                                  const uint32_t *attack_cycles,
                                  const uint32_t *decay_cycles) {
    uint8_t attack_rate  = (v->attack_decay  >> 4) & 0x0Fu;
    uint8_t decay_rate   =  v->attack_decay         & 0x0Fu;
    uint8_t sustain_lvl  = (v->sustain_release >> 4) & 0x0Fu;
    uint8_t release_rate =  v->sustain_release        & 0x0Fu;
    uint8_t sustain_val  = (uint8_t)(sustain_lvl * 17u); /* 0..255 */

    switch (v->env_state) {
        case SID_ENV_ATTACK:
            v->env_counter += 1.0 / (double)attack_cycles[attack_rate];
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
            v->env_counter += 1.0 / ((double)decay_cycles[decay_rate] *
                                     (double)sid_exp_period(v->envelope));
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
            v->env_counter += 1.0 / ((double)decay_cycles[release_rate] *
                                     (double)sid_exp_period(v->envelope));
            if (v->env_counter >= 1.0) {
                v->env_counter -= 1.0;
                if (v->envelope > 0u) v->envelope--;
            }
            break;
    }
}

static float sid_condition_output(sid *s, float raw) {
    float blocked;

    blocked = raw - s->dc_block_prev_input + SID_DC_BLOCK_R * s->dc_block_prev_output;
    s->dc_block_prev_input  = raw;
    s->dc_block_prev_output = blocked;

    s->hfroll_state = (1.0f - s->hfroll_coeff) * blocked
                    +          s->hfroll_coeff  * s->hfroll_state;

    return sid_clampf(s->hfroll_state * ((float)SID_OUTPUT_GAIN_PERCENT / 100.0f));
}

/* Interpolate the 11-bit cutoff register against a 32-anchor LUT. */
static float sid_cutoff_from_lut(const float *lut, uint16_t cutoff) {
    float pos  = (float)cutoff * (31.0f / 2047.0f);
    int   idx  = (int)pos;
    float frac;
    if (idx >= 31) return lut[31];
    frac = pos - (float)idx;
    return lut[idx] + frac * (lut[idx + 1] - lut[idx]);
}

/* Public test/PAL helper: kept clockless and PAL-backed for legacy unit tests.
   The emulator hot path uses the per-instance s->cutoff_lut instead. */
float sid_filter_cutoff_factor(uint16_t cutoff) {
    return sid_cutoff_from_lut(s_cutoff_lut_pal, cutoff);
}

/* ------------------------------------------------------------------ */
/* Cycle advance: oscillators + ADSR + mix + filter                   */
/* ------------------------------------------------------------------ */

void sid_advance_cycles(sid *s, uint32_t cycles) {
    uint32_t c, i;
    float    v1, v2, v3, voice_sum, filtered_in, bypass_out, vol_gain;
    float    f, q;
    float    raw_output;
    uint8_t  res_nibble, route, mode;
    uint32_t prev_phase[3];
    bool     wrapped[3];
    static const uint8_t source_voice[3] = { 2u, 0u, 1u };

    if (!s || cycles == 0u) return;

    for (c = 0u; c < cycles; c++) {
        /* --- Advance each voice --- */
        for (i = 0u; i < 3u; i++) {
            sid_voice *v    = &s->voices[i];
            prev_phase[i] = v->phase;
            wrapped[i] = false;

            if (!(v->control & 0x08u)) {   /* advance phase unless TEST bit */
                v->phase = (v->phase + (uint32_t)v->freq) & 0x00FFFFFFu;
                wrapped[i] = v->phase < prev_phase[i];
            }
        }

        /* Hard sync: a voice resets when its previous/source voice wraps.
         * Voice 1<-3, voice 2<-1, voice 3<-2. */
        for (i = 0u; i < 3u; i++) {
            sid_voice *v = &s->voices[i];
            if ((v->control & 0x02u) != 0u && wrapped[source_voice[i]]) {
                v->phase = 0;
            }
        }

        for (i = 0u; i < 3u; i++) {
            sid_voice *v = &s->voices[i];
            if (s->sample_output_enabled) {
                v->last_wave = sid_voice_waveform(
                    v,
                    prev_phase[i],
                    s->voices[source_voice[i]].phase);
            }
            sid_voice_advance_env(v, s->attack_cycles, s->decay_cycles);
        }

        /* --- Voice 3 read-back registers --- */
        s->voice3_osc_read = (uint8_t)(s->voices[2].phase >> 16);
        s->voice3_env_read =  s->voices[2].envelope;

        if (!s->sample_output_enabled) {
            continue;
        }

        /* --- Mixer ---
         * $D417 bits 0..2 route voices 1..3 through the filter. Unrouted
         * voices bypass the filter and are mixed back after mode selection.
         * Voice 3 output is disconnected when $D418 bit 7 is set. */
        v1 = s->voices[0].last_wave * ((float)s->voices[0].envelope / 255.0f);
        v2 = s->voices[1].last_wave * ((float)s->voices[1].envelope / 255.0f);
        v3 = (s->mode_volume & 0x80u) ? 0.0f :
             s->voices[2].last_wave * ((float)s->voices[2].envelope / 255.0f);

        vol_gain = (float)(s->mode_volume & 0x0Fu) / 15.0f;
        route = s->filter_res_route & 0x07u;
        voice_sum = v1 + v2 + v3;
        filtered_in = 0.0f;
        bypass_out = 0.0f;

        if (route & 0x01u) filtered_in += v1; else bypass_out += v1;
        if (route & 0x02u) filtered_in += v2; else bypass_out += v2;
        if (route & 0x04u) filtered_in += v3; else bypass_out += v3;

        filtered_in = sid_clampf(filtered_in / 3.0f * vol_gain);
        bypass_out  = sid_clampf(bypass_out  / 3.0f * vol_gain);

        /* --- State-variable filter ---
         * f: cutoff factor, capped below 0.5 for stability and to avoid the
         * overly bright top-end produced by the earlier linear 0..0.5 mapping.
         * q: resonance damping 0.1..1.0. */
        f = sid_cutoff_from_lut(s->cutoff_lut, s->filter_cutoff);

        res_nibble = (s->filter_res_route >> 4) & 0x0Fu;
        q = 1.0f - (float)res_nibble / 20.0f;
        if (q < 0.1f) q = 0.1f;

        /* Chamberlin SVF (hp computed first for stability) */
        s->filter_hp  = filtered_in - s->filter_lp - q * s->filter_bp;
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
            raw_output = sid_clampf(voice_sum / 3.0f * vol_gain);
        } else {
            float out = 0.0f;
            if (mode & 0x01u) out += s->filter_lp;   /* LP */
            if (mode & 0x02u) out += s->filter_bp;   /* BP */
            if (mode & 0x04u) out += s->filter_hp;   /* HP */
            raw_output = sid_clampf(out + bypass_out);
        }
        s->last_sample = sid_condition_output(s, raw_output);
    }
}

/* ------------------------------------------------------------------ */
/* Sample read                                                         */
/* ------------------------------------------------------------------ */

float sid_sample(const sid *s) {
    if (!s) return 0.0f;
    return s->last_sample;
}
