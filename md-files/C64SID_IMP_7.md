# C64SID_IMP_7.md
# SID Improvement Phase 7 - Filter Cutoff Frequency Range

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

Fix the filter cutoff frequency range so that it approximates the real 6581
rather than mapping to an unusably high range.

The current linear formula `f = (cutoff + 1) / 4608` maps the 11-bit cutoff
register [0..2047] to Chamberlin SVF coefficients [0.000217..0.444]. Running at
the PAL clock of 985248 Hz, this corresponds to audio cutoff frequencies of
roughly 34 Hz to 70,000 Hz. The real 6581 range is approximately 200 Hz to
18,000 Hz with a non-linear curve.

The Phase 5 spectral metrics confirm the problem:

```text
16000-22050 Hz:  ref=-38.0 dB  cand=-27.9 dB  diff=+10.1 dB
  250-  500 Hz:  ref=-10.3 dB  cand= -7.4 dB  diff=+2.9  dB
  500- 1000 Hz:  ref=-12.3 dB  cand=-10.7 dB  diff=+1.6  dB
```

c64m has 10 dB too much energy in the 16-22 kHz range and too much in the
250-1000 Hz range. Both symptoms are consistent with a low-pass filter cutoff
being set much higher than intended by the music program, letting harmonics
through that the real hardware would attenuate.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md` through `C64SID_IMP_6.md`
6. This guide

## Goal

Replace `sid_filter_cutoff_factor()` with a mapping that:

- spans approximately 200 Hz to 18,000 Hz at the PAL clock of 985248 Hz;
- is non-linear (roughly exponential across the register range), matching the
  perceptual and approximate electrical behavior of the real 6581;
- remains stable and bounded for all register values 0..2047;
- is cheap enough for per-cycle use (no `math.h` in the hot path).

## In Scope

- Replace the body of `sid_filter_cutoff_factor()` with a LUT-based mapping.
- Tune the LUT entries against the spectral metrics and listening.
- Add filter regression tests for the new coefficient range.
- Run the Phase 1 automated comparison and record the score delta.
- Update `STATUS.md`.

## Explicit Non-Goals

Do not implement these in this phase:

- The full nonlinear 6581 filter model.
- 6581 vs 8580 variant switching.
- Resonance characteristic changes (may be a follow-on if needed).
- ADSR or waveform changes.
- Any register-visible behavior change.

## Architecture Contract

All changes remain in `machine/sid.c` / `machine/sid.h`.

The LUT may be declared as a file-scope `static const float` array.

No runtime, platform, or frontend component should know about the cutoff mapping.

## Why the Real 6581 Range Matters

The Chamberlin SVF coefficient `f` relates to audio cutoff frequency at sample
rate `fs` approximately as:

```text
fc_hz ≈ f × fs / (2π)    (valid for f << 1)
```

Target bounds for `f` at fs = 985248 Hz:

```text
fc_min = 200 Hz  →  f_min ≈ 2π × 200 / 985248 ≈ 0.001276
fc_max = 18000 Hz →  f_max ≈ 2π × 18000 / 985248 ≈ 0.1148
```

The current maximum `f = 0.444` corresponds to ~69,600 Hz — more than 3.8×
above the target maximum. At that coefficient the filter passes all audible
content and adds only phase distortion, making it indistinguishable from bypass.

The real 6581 cutoff is not linear in the register value. Equal register steps
produce roughly equal pitch steps to the ear, which implies an exponential
relationship between register value and cutoff frequency. Using a linear mapping
over the wrong range is a double error.

## Implementation Strategy

Replace `sid_filter_cutoff_factor()` with a small compile-time table.

### LUT Design

Use 32 evenly-spaced anchor points across the 11-bit register range [0..2047]
and linearly interpolate between them. This gives resolution of 2047/31 ≈ 66
register steps between anchors, which is sufficient.

The anchor cutoff frequencies should follow an exponential curve from 200 Hz to
18,000 Hz:

```text
fc(i) = 200 × (18000/200)^(i/31)   for i = 0..31
```

Precompute the corresponding `f` coefficients:

```text
f(i) = 2π × fc(i) / 985248
```

Store these 32 values in a file-scope `static const float` array.

At runtime, `sid_filter_cutoff_factor(cutoff)` maps the 11-bit register value to
an index and fraction, then interpolates:

```c
static float sid_filter_cutoff_factor(uint16_t cutoff) {
    /* map [0..2047] onto [0..31] with fractional part */
    float pos = (float)cutoff * (31.0f / 2047.0f);
    int   idx = (int)pos;
    float frac = pos - (float)idx;
    if (idx >= 31) return s_cutoff_lut[31];
    return s_cutoff_lut[idx] + frac * (s_cutoff_lut[idx + 1] - s_cutoff_lut[idx]);
}
```

### Computing the LUT

The 32 entries can be derived from the formula above. To avoid `math.h` in
production code, compute the values once offline (or in a small throwaway script)
and hard-code them as float literals.

Exponential spacing from 200 Hz to 18,000 Hz over 32 points (indices 0..31):

```text
ratio = (18000.0 / 200.0) ^ (1.0/31.0) = 90.0^(1/31) ≈ 1.1548
fc[i] = 200 × 1.1548^i
f[i]  = 2π × fc[i] / 985248
```

Approximate values (compute precisely before committing):

```text
i=0:  fc≈200     f≈0.001276
i=4:  fc≈325     f≈0.002073
i=8:  fc≈528     f≈0.003367
i=12: fc≈857     f≈0.005467
i=16: fc≈1392    f≈0.008877
i=20: fc≈2262    f≈0.014423
i=24: fc≈3673    f≈0.023424
i=28: fc≈5967    f≈0.038057
i=31: fc≈9000    f≈0.057380
```

Wait — the above stops at 9,000 Hz for i=31, not 18,000 Hz. Recheck:
fc[31] = 200 × (18000/200)^(31/31) = 18,000 Hz, so f[31] ≈ 0.1148.

Compute all 32 values precisely in a small Python snippet and embed them
as float literals. Do not approximate by hand; use the formula.

### Stability Check

After the change, verify that:

- `sid_filter_cutoff_factor(0)` returns approximately 0.00128 (≈200 Hz).
- `sid_filter_cutoff_factor(2047)` returns approximately 0.1148 (≈18 kHz).
- All returned values are in [0.001, 0.15] — no value approaches 0.5.

The existing Chamberlin SVF stability constraint (coefficient < 0.5) is
satisfied with wide margin at the new maximum.

## Suggested Tests

Extend the existing filter regression tests in `tests/machine/test_sid.c`:

- `sid_filter_cutoff_factor(0)` is within 10% of the target minimum coefficient;
- `sid_filter_cutoff_factor(2047)` is within 10% of the target maximum
  coefficient;
- `sid_filter_cutoff_factor(1023)` (mid-register) is between the min and max
  coefficients and is greater than the value at register 0;
- all returned values are strictly less than 0.5;
- filter output for an LP-routed voice at full cutoff is measurably louder in
  high frequencies than at mid-cutoff (relational, not exact);
- existing stability tests (extreme resonance/mode combinations, reset clears
  filter state) continue to pass.

## Automated Audio Procedure

After tests pass, run:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase7.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

Then compare:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase7.wav \
  --out ./build/sid-phase7-metrics.json
```

Focus on:

- spectral band error (was 2.2646 dB in Phase 5; this phase targets improvement);
- the 16-22 kHz band specifically (was +10.1 dB above reference);
- overall score change from the Phase 6 baseline.

If the spectral error improves but the score regresses due to correlation or RMS
changes, investigate whether the filter change is altering the overall level of
filtered voices. Retune the output gain constant if needed.

## Acceptance Criteria

- Existing tests pass.
- New cutoff range tests pass.
- `sid_filter_cutoff_factor(0)` returns a coefficient corresponding to
  approximately 200 Hz at the PAL clock.
- `sid_filter_cutoff_factor(2047)` returns a coefficient corresponding to
  approximately 18,000 Hz at the PAL clock.
- Spectral band mean absolute error improves against the Phase 5 baseline
  (2.2646 dB), or any regression is documented with a clear decision.
- The 16-22 kHz excess (+10.1 dB in Phase 5) is reduced.
- Output remains bounded; no new DC offset or clipping.
- `STATUS.md` is updated only after implementation and tests support the claim.

## Human Verification

Listen to the Phase 7 capture and compare to `./samples/x65sc.mp3`. The
expected audible change is that LP filter settings sound darker and more
contained — filter sweeps should have the characteristic C64 movement rather
than opening to full brightness too early. HP and BP modes should also sweep
through the audible range rather than into inaudible frequencies.
