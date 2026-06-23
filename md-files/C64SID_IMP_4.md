# C64SID_IMP_4.md
# SID Improvement Phase 4 - Sync, Ring Modulation, And Combined Waveform Polish

## Component

C64MSID

## Status

Implementation planning guide. Split into smaller guides if the code review
shows this is too large for one safe pass.

## Purpose

Improve oscillator behavior used by normal SID music after output conditioning
and filter routing are measurable. This phase targets three important gaps:

- oscillator hard sync;
- ring modulation;
- better handling of selected combined waveforms.

These are grouped because they all affect waveform generation and control
register interpretation, but they may be implemented as separate commits or
sub-phases.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md`
6. `C64SID_IMP_2.md`
7. `C64SID_IMP_3.md`
8. This guide

## Goal

Move c64m closer to VICE for common musical oscillator behavior while preserving
the maintainable functional SID scope of the current milestone.

## In Scope

- Implement control bit 1 oscillator sync.
- Implement control bit 2 ring modulation for triangle output.
- Replace strict waveform priority with a documented combined-waveform
  approximation for common combinations.
- Add tests for control-bit behavior.
- Use the Phase 1 harness to compare against `./samples/x65sc.mp3`.

## Explicit Non-Goals

Do not implement these in this phase:

- Full reSID-level SID model.
- Chip revision switching.
- Exact nonlinear waveform DAC modeling.
- Exact 6581 combined-waveform analog decay/lockup behavior.
- Sample-perfect digi tricks.
- External audio input.
- New audio threading architecture.

## Architecture Contract

All oscillator behavior remains in `machine/sid.c` / `machine/sid.h`.

No runtime, platform, or frontend component should special-case sync, ring, or
combined waveform behavior.

## Implementation Order

Use this order unless code inspection gives a strong reason to split further:

1. Add oscillator neighbor access needed for sync/ring.
2. Implement hard sync.
3. Add focused sync tests.
4. Implement ring modulation.
5. Add focused ring tests.
6. Replace waveform priority with combined-waveform approximation.
7. Add focused combined-waveform tests.
8. Run automated audio comparison.

## Hard Sync Guidance

SID sync resets a voice oscillator when the previous voice oscillator wraps.

Voice relationships:

```text
voice 1 syncs to voice 3
voice 2 syncs to voice 1
voice 3 syncs to voice 2
```

When sync is enabled for a voice, detect the source oscillator wrap during the
current cycle and reset the destination phase in a deterministic way.

Be careful with TEST bit behavior. TEST should still silence/reset the affected
voice according to the existing policy.

## Ring Modulation Guidance

Ring modulation affects triangle waveform generation by using the previous
voice oscillator's high bit to invert or alter the triangle phase.

Voice relationships match sync source relationships:

```text
voice 1 rings with voice 3
voice 2 rings with voice 1
voice 3 rings with voice 2
```

Keep the first implementation functional and documented. Exact analog behavior
is not required for this milestone.

## Combined Waveform Guidance

The current implementation selects one waveform by priority:

```text
noise > pulse > saw > triangle
```

Replace that with an approximation that makes common combinations sound closer
to a SID than priority selection. Acceptable first approximations include:

- bitwise-style combination in an intermediate unsigned waveform domain;
- multiplicative/AND-like blending of normalized waveforms;
- small lookup-free formulas per common combination.

Document the chosen approximation in comments and in `STATUS.md`.

Noise combinations may remain conservative if exact behavior is unstable or
would require a much deeper model. If noise combinations are deferred, document
that explicitly.

## Suggested Tests

Add tests for:

- sync enabled causes destination phase reset when source wraps;
- sync disabled preserves normal phase accumulation;
- ring modulation changes triangle output when the source high bit changes;
- ring bit has no audible effect when triangle is not selected, unless the
  chosen approximation intentionally says otherwise;
- selected combined waveform output differs from each individual source
  waveform and remains bounded;
- TEST bit behavior remains deterministic.

Avoid hard-coding long exact sample sequences unless the implementation is
designed to guarantee them. Prefer small deterministic state tests and relational
audio assertions.

## Automated Audio Procedure

After each sub-step, run the Phase 1 comparison workflow:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase4.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

Then compare:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase4.wav \
  --out ./build/sid-phase4-metrics.json
```

If sync, ring, and combined waveform changes are committed separately, keep a
metrics JSON for each local run:

```text
./build/sid-phase4-sync-metrics.json
./build/sid-phase4-ring-metrics.json
./build/sid-phase4-combined-metrics.json
```

## Acceptance Criteria

- Existing tests pass.
- New oscillator behavior tests pass.
- Sync and ring bits have documented, tested behavior.
- Combined waveforms no longer use simple priority selection for all cases.
- Captured `el_cartero` output remains bounded and has no new large DC offset.
- Comparison metrics improve against Phase 3, or any regression is documented
  with a clear keep/revert decision.
- `STATUS.md` is updated only after implementation and tests support the claim.

## Human Verification

Listen to the VICE reference and c64m capture back to back. This phase should
make timbre and movement closer, especially where the tune relies on control
register effects rather than only simple pulse/saw/triangle voices.
