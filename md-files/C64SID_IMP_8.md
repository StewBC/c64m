# C64SID_IMP_8.md
# SID Improvement Phase 8 - Output-Stage High-Frequency Rolloff

## Component

C64MSID

## Status

Implementation planning guide. Implement after Phase 7A has confirmed the
correlation metric is meaningful and a new baseline score has been recorded.

## Purpose

After Phase 7, the c64m SID output is approximately +9.4 dB above the VICE
reference in the 16-22 kHz spectral band. The Phase 7 filter LUT caps the
Chamberlin SVF cutoff at 18,000 Hz, but this only benefits voices that are
routed through the filter. Waveform generators — especially sawtooth and pulse
— produce harmonics up to the Nyquist of the host sample rate (22,050 Hz at
44100 Hz output) regardless of whether they pass through the filter. Voices
with the filter routing bit clear bypass the SVF entirely.

The real MOS 6581 has a capacitive load on its output pin that causes a
gentle first-order rolloff starting in the 15-18 kHz range. This means that
all SID output — filtered or not — is naturally attenuated above that point.
The emulator's output stage does not model this rolloff, so it passes
ultrasonic content that the real chip never produced at the speaker.

This phase adds a single-pole IIR low-pass filter at approximately 16-18 kHz
in `sid_condition_output()`, after the DC blocker, to simulate the analog
output capacitor rolloff and reduce the excess high-frequency content.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md` through `C64SID_IMP_7A.md`
6. This guide

## Goal

Reduce the 16-22 kHz spectral excess from its Phase 7 value of +9.4 dB to
within ±3 dB of the VICE reference, without affecting frequencies below
10 kHz audibly and without reducing the overall RMS by more than 5%.

## In Scope

- Add a single-pole IIR low-pass in `sid_condition_output()` in `src/machine/sid.c`.
- Add a `hfroll_state` float field to the `sid` struct in `src/machine/sid.h`.
- Reset `hfroll_state` to 0.0f in `sid_init()` and `sid_reset()`.
- Add unit tests verifying rolloff behavior and reset.
- Run the comparison and record new metrics.
- Tune `SID_OUTPUT_GAIN_PERCENT` if RMS drifts more than 5% from VICE (0.0646).
- Update `STATUS.md`.

## Explicit Non-Goals

Do not implement these in this phase:

- Changes to the Chamberlin SVF or its cutoff LUT.
- ADSR changes.
- Waveform or phase accumulator changes.
- Multi-pole or shelving EQ.
- Any register-visible behavior change.
- A precise model of the 6581 analog output circuit.
- Changes to the SDL audio callback or the platform audio path.

## Architecture Contract

All changes stay in `machine/sid.c` and `machine/sid.h`. No runtime,
platform, or frontend component should know about this filter. The filter
state is part of `struct sid` and is initialized by `sid_init()` and reset
by `sid_reset()`.

Do not add `math.h` to `sid.c`. Compute the coefficient as a named constant.

## Implementation Strategy

### Filter form

Use a first-order IIR low-pass:

```c
y[n] = (1 - a) * x[n] + a * y[n-1]
```

The coefficient `a` controls the cutoff frequency. For a single-pole IIR
running at the SID clock rate (985,248 Hz PAL):

```text
a = 1 - 2π × fc / fs
```

Candidate values:

| fc (Hz) | a       |
|---------|---------|
| 16,000  | 0.898   |
| 17,000  | 0.892   |
| 18,000  | 0.885   |

Start with 16,500 Hz (a = 0.895) and tune by measuring the 16-22 kHz band
against the VICE reference.

Define the coefficient as a named constant at the top of the SID constants
block so tuning requires only one edit:

```c
static const float SID_HFROLL_COEFF = 0.895f;
```

### State field

Add one field to `struct sid` in `sid.h`:

```c
float      hfroll_state;    /* one-pole output LP filter state */
```

Reset it to 0.0f in both `sid_init()` and `sid_reset()`.

### Placement in `sid_condition_output()`

Apply the rolloff after the DC blocker and before the output gain clamp:

```c
static float sid_condition_output(sid *s, float raw) {
    float blocked;
    float rolled;

    /* existing DC blocker */
    blocked = raw - s->dc_block_prev_input
              + SID_DC_BLOCK_R * s->dc_block_prev_output;
    s->dc_block_prev_input  = raw;
    s->dc_block_prev_output = blocked;

    /* output-stage analog rolloff (~16-18 kHz) */
    s->hfroll_state = (1.0f - SID_HFROLL_COEFF) * blocked
                    +          SID_HFROLL_COEFF  * s->hfroll_state;

    return sid_clampf(s->hfroll_state
                      * ((float)SID_OUTPUT_GAIN_PERCENT / 100.0f));
}
```

### Gain retune

The one-pole low-pass reduces average signal power slightly. Measure the RMS
of the Phase 8 capture and compare to VICE (0.0646). If the RMS drops by more
than 5% (below ~0.0613), increase `SID_OUTPUT_GAIN_PERCENT` by one or two
points and re-run.

Do not over-correct. The goal is to stay within 5% of the VICE RMS; an exact
match is not required.

### Coefficient tuning procedure

Run the automated comparison with `SID_HFROLL_COEFF = 0.895f`. Inspect:

1. The 16-22 kHz band difference. Target: within ±3 dB of VICE.
2. The spectral MAE. Should improve from 2.2438 dB (Phase 7 baseline).
3. The RMS. Should stay within 5% of 0.0646.
4. The overall score and correlation. These should match or improve relative
   to the Phase 7A re-baselined value.

If the 16-22 kHz band is still more than 3 dB above VICE, lower the
coefficient by 0.005 (increase cutoff aggressiveness) and re-run.

If the output sounds audibly muffled on listened comparison, raise the
coefficient by 0.005 and re-run.

Stop when:
- The band is within ±3 dB of VICE, or
- The score stops improving, or
- Audible mid-range content is noticeably dulled.

## Suggested Tests

Add a Phase 8 section to `tests/machine/test_sid.c`:

- `test_hfroll_state_reset_to_zero` — call `sid_reset()` after feeding it
  several non-zero samples; verify `hfroll_state` is 0.0f.

- `test_hfroll_attenuates_high_frequency` — feed the SID output function
  a high-frequency alternating signal (alternating +1.0 and -1.0 at 20 kHz
  equivalent). Compare the output RMS with `SID_HFROLL_COEFF = 0.895f`
  against what a coefficient of 0.0 (full bypass) would produce. The 0.895
  case must produce lower RMS. Use a relative check, not an exact value.

- `test_hfroll_passes_low_frequency` — feed a slow-varying (low-frequency)
  constant-slope ramp through `sid_condition_output()`. Verify that the
  output remains above a minimum magnitude (the rolloff must not swallow
  low-frequency content).

- `test_hfroll_silence_remains_silence` — feed 0.0 through
  `sid_condition_output()` for 100 samples. Output must remain 0.0.

- `test_hfroll_output_bounded` — feed extreme values (+2.0 and -2.0)
  through `sid_condition_output()`. Output must remain within [-1.0, +1.0].

All tests must pass alongside the existing 55-test suite.

## Automated Audio Procedure

After tests pass, build:

```text
make
```

Capture:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase8.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

Compare:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase8.wav \
  --out ./build/sid-phase8-metrics.json
```

Key metrics to record (compare against Phase 7A baseline):

- `spectral.band_16_22_khz_db` — target within ±3 dB of VICE.
- `spectral.mae_db` — should improve from 2.2438 dB.
- `rms` — should stay within 5% of VICE (0.0646).
- `alignment.correlation` — should match Phase 7A baseline (this phase does
  not change envelope behavior).
- `score` — overall direction should be improvement.

## Acceptance Criteria

- All existing tests pass.
- All five new Phase 8 tests pass.
- `hfroll_state` is confirmed to be 0.0f after `sid_reset()`.
- The 16-22 kHz spectral excess improves from the Phase 7 value of +9.4 dB.
- Spectral band MAE improves from the Phase 7 value of 2.2438 dB, or any
  regression is documented and justified before `STATUS.md` is updated.
- RMS stays within 5% of VICE (0.0646) after any gain retune.
- Output remains bounded within [-1.0, +1.0] for all inputs.
- `STATUS.md` is updated only after implementation and tests support the claim.

## Human Verification

Listen to the Phase 8 capture alongside `./samples/x65sc.mp3`. The expected
change is subtle: very high-frequency content (above 15 kHz) should be
slightly attenuated, reducing any harshness on noise-based sounds or fast
arpeggios. Mid-range and bass content should be indistinguishable from Phase
7A by ear.

If the output sounds noticeably duller or darker overall, the rolloff
coefficient is too high; reduce `SID_HFROLL_COEFF` and re-run. If the
16-22 kHz band still measures well above VICE after three coefficient
adjustments, note that the remaining excess may come from the SID noise
generator rather than the output stage, and defer further reduction to a
later phase targeting the noise waveform.
