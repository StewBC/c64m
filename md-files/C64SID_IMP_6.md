# C64SID_IMP_6.md
# SID Improvement Phase 6 - ADSR Pseudo-Exponential Decay

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

Replace the current linear decay and release envelope with the pseudo-exponential
approximation used by the real 6581. This is the dominant remaining source of
poor musical resemblance: the current uniform linear decrement makes notes fade
differently from a real SID, which causes the loudness envelope of c64m audio to
be nearly uncorrelated with VICE output.

The Phase 5 metrics confirm this. The score is 3.75 and 90% of it comes from an
envelope correlation of only 0.157. RMS and DC offset are already solved. The
next highest-return change is making note decay shapes match the real hardware.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md` through `C64SID_IMP_5.md`
6. This guide

## Goal

Change `sid_voice_advance_env()` so that during decay and release states the
envelope step rate slows down as the envelope value drops, producing the
exponential-shaped curve characteristic of the real 6581.

Attack is **not** changed. The real 6581 attack is linear.

## In Scope

- Add an exponential rate multiplier to decay and release states in
  `sid_voice_advance_env()`.
- Add tests that verify the exponential shape is produced.
- Run the Phase 1 automated comparison and record the score delta.
- Update `STATUS.md`.

## Explicit Non-Goals

Do not implement these in this phase:

- Filter changes of any kind.
- Waveform changes.
- Combined waveform improvements.
- NTSC rate tables.
- Any register-visible behavior change.
- Gate or ADSR timing changes beyond the decay/release rate multiplier.

## Architecture Contract

All changes remain in `machine/sid.c`.

No runtime, platform, or frontend component should know about ADSR shaping.

## How the Real 6581 Works

The real 6581 envelope generator does not decrement by one step every N cycles
uniformly during decay or release. Instead it maintains a secondary counter
(here called the exponential counter) that must reach a level-dependent period
before the primary envelope value decrements by one.

The period multiplier table, based on reverse engineering of the 6581 and
confirmed by reSID:

```text
envelope value >= 255: period multiplier = 1
envelope value >= 93:  period multiplier = 2
envelope value >= 54:  period multiplier = 4
envelope value >= 26:  period multiplier = 8
envelope value >= 14:  period multiplier = 16
envelope value >= 6:   period multiplier = 30
envelope value < 6:    period multiplier = 30
```

This means at full amplitude (255) the envelope decrements at the nominal rate.
As the value falls, each step takes progressively longer, producing a curve that
is fast at the top and slow near silence — the characteristic soft C64 note tail.

## Implementation Strategy

The existing decay and release branches use a fractional double counter:

```c
v->env_counter += 1.0 / (double)s_decay_cycles[rate];
if (v->env_counter >= 1.0) {
    v->env_counter -= 1.0;
    if (v->envelope > target) v->envelope--;
}
```

Add a helper that returns the multiplier for the current envelope value:

```c
static uint32_t sid_exp_period(uint8_t env) {
    if (env >= 93u) return 1u;
    if (env >= 54u) return 2u;
    if (env >= 26u) return 4u;
    if (env >= 14u) return 8u;
    if (env >= 6u)  return 16u;
    return 30u;
}
```

Then change the counter increment for decay and release to:

```c
v->env_counter += 1.0 / ((double)s_decay_cycles[rate] * (double)sid_exp_period(v->envelope));
```

The sustain and attack branches are unchanged.

One subtlety: evaluate `sid_exp_period` on the current `v->envelope` value
**before** the decrement, not after. The multiplier should reflect the level
the envelope is at when this step is being timed, not the level it will reach.

## Acceptance Criteria

- Existing tests pass.
- New tests verify that:
  - a voice with a fast decay rate and no sustain takes measurably longer to
    reach 0 with the exponential multiplier than without (verify by counting
    cycles at a controlled rate);
  - a voice at envelope 6 takes 30× longer per step than one at envelope 255
    with the same decay register setting;
  - attack is unaffected (linear);
  - release behaves exponentially, matching decay;
  - sustain hold is unaffected.
- The Phase 1 automated capture and comparison runs without error.
- The comparison score improves relative to Phase 5 (3.7503), or any regression
  is documented with a clear decision before proceeding.
- `STATUS.md` is updated only after implementation and tests support the claim.

## Automated Audio Procedure

After tests pass, run:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase6.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

Then compare:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase6.wav \
  --out ./build/sid-phase6-metrics.json
```

Focus on:

- envelope correlation (was 0.157 in Phase 5; this phase targets improvement);
- overall score change from 3.7503.

## Human Verification

Listen to the Phase 6 capture and compare to `./samples/x65sc.mp3`. The expected
audible change is that notes with any decay or release setting should have a
softer, more gradual fade-out rather than a uniform linear drop. Sustained notes
and attack shape should sound the same as before.
