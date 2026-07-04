#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum sid_env_state {
    SID_ENV_RELEASE = 0,
    SID_ENV_ATTACK,
    SID_ENV_DECAY,
    SID_ENV_SUSTAIN
} sid_env_state;

typedef struct sid_voice {
    uint16_t      freq;             /* decoded 16-bit frequency */
    uint16_t      pulse_width;      /* decoded 12-bit pulse width */
    uint8_t       control;          /* control register */
    uint8_t       attack_decay;     /* ATTACK_DECAY register */
    uint8_t       sustain_release;  /* SUSTAIN_RELEASE register */

    uint32_t      phase;            /* 24-bit phase accumulator */
    uint32_t      noise_lfsr;       /* 23-bit LFSR; always nonzero */

    uint8_t       envelope;         /* current envelope level 0..255 */
    sid_env_state env_state;        /* current ADSR state */
    double        env_counter;      /* fractional step accumulator */

    float         last_wave;        /* last waveform output (pre-envelope) */
} sid_voice;

typedef struct sid {
    uint8_t    regs[0x20];          /* raw register mirror $D400-$D41F */
    sid_voice  voices[3];

    uint16_t   filter_cutoff;       /* 11-bit decoded from $D415/$D416 */
    uint8_t    filter_res_route;    /* $D417: high nibble=resonance, low=routing */
    uint8_t    mode_volume;         /* $D418: bits 6-4=HP/BP/LP, bits 3-0=volume */

    float      filter_lp;           /* state-variable filter state */
    float      filter_bp;
    float      filter_hp;

    float      dc_block_prev_input;  /* final output DC blocker state */
    float      dc_block_prev_output;
    float      hfroll_state;         /* one-pole output LP filter state */

    float      last_sample;         /* last mixed+filtered output */
    bool       sample_output_enabled;

    uint8_t    voice3_osc_read;     /* $D41B shadow (top byte of voice 3 phase) */
    uint8_t    voice3_env_read;     /* $D41C shadow (voice 3 envelope) */

    /* Active CPU (Ø2) clock and the per-standard rate tables/coefficients it
       selects. Derived from the clock at init; not serialized (they follow the
       host machine's video standard, not a save-state). PAL selects the tables
       that keep output bit-identical to the pre-clock-parameterization baseline. */
    uint32_t         cpu_clock_hz;
    const uint32_t  *attack_cycles;   /* [16] cycles per envelope +1 step */
    const uint32_t  *decay_cycles;    /* [16] cycles per envelope -1 step */
    const float     *cutoff_lut;      /* [32] Chamberlin SVF cutoff anchors */
    float            hfroll_coeff;    /* output HF-rolloff one-pole coefficient */
} sid;

/* Initialise SID to power-on state for the given CPU clock (PAL 985248 Hz /
   NTSC 1022727 Hz). The clock selects the envelope/filter rate tables. */
void    sid_init(sid *s, uint32_t cpu_clock_hz);

/* Reset SID to power-on state, preserving the currently selected CPU clock. */
void    sid_reset(sid *s);

/* CPU write: addr is the raw C64 address ($D400-$D41F); masked internally. */
void    sid_write(sid *s, uint16_t addr, uint8_t value);
void    sid_set_sample_output_enabled(sid *s, bool enabled);

/* CPU read: addr is the raw C64 address.
   $D41B returns voice 3 oscillator byte; $D41C returns voice 3 envelope.
   $D419/$D41A return 0xFF (no paddle connected). Other registers return
   the last written value. Reads are non-destructive. */
uint8_t sid_read(sid *s, uint16_t addr);
uint8_t sid_debug_read(const sid *s, uint16_t addr);

/* Advance oscillators, envelopes, and filter by |cycles| machine cycles.
   Called once per machine cycle from the c64 step path. */
void    sid_advance_cycles(sid *s, uint32_t cycles);

/* Return the last mixed and filtered float sample [-1.0, +1.0].
   Non-blocking; safe to call at host audio rate. */
float   sid_sample(const sid *s);

/* Map the 11-bit cutoff register value [0..2047] to the Chamberlin SVF
   coefficient.  Exposed for unit testing; not part of the emulator API. */
float   sid_filter_cutoff_factor(uint16_t cutoff);
