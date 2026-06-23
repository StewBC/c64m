# C64SID_IMP_2.md
# SID Improvement Phase 2 - Output Level, DC Offset, And Baseline Hygiene

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

Fix the easiest whole-output mismatches before changing deeper SID behavior:
excessive output level, DC offset, and unstable comparison hygiene.

The initial MP3 comparison showed c64m much louder than the VICE reference and
with significant DC bias. This phase should make c64m easier to compare and
less fatiguing to listen to, without pretending to solve SID waveform fidelity.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md`
6. This guide

## Goal

Use the Phase 1 capture/comparison harness to tune only the final SID output
conditioning so that:

- mean/DC offset is near zero;
- c64m RMS is in the same broad range as `./samples/x65sc.mp3`;
- peaks remain inside the float audio range expected by SDL;
- ordinary SID output is still recognizable;
- no register-visible SID behavior changes.

## In Scope

- Add or tune a final SID output gain.
- Add a simple DC blocker or very low-cut high-pass filter.
- Add tests for bounded output and DC blocker stability.
- Use the Phase 1 harness to compare against `./samples/x65sc.mp3`.
- Record before/after metrics in local generated artifacts.

## Explicit Non-Goals

Do not implement these in this phase:

- Per-voice filter routing.
- Oscillator sync.
- Ring modulation.
- Combined waveform blending.
- ADSR behavior changes.
- SID register read behavior changes.
- Runtime or platform audio architecture changes beyond using the existing
  capture harness.

## Architecture Contract

The conditioning should live in `machine/sid.c` as part of SID sample generation
unless the implementation clearly shows it is a host-output-only concern.

Do not put SID-specific level policy in `platform/`.

The SDL callback must remain a plain buffer consumer.

## Implementation Notes

Prefer a small final stage after mix/filter selection:

```text
raw SID mixed/filter output
DC blocker / low-cut
output gain
clamp or soft limit if needed
last_sample
```

The DC blocker must be deterministic and reset by `sid_reset()`.

Use a stable one-pole form such as:

```text
y[n] = x[n] - x[n-1] + R * y[n-1]
```

where `R` is close to 1.0. The exact value should be chosen by measurement and
listening, not guessed permanently without comparison.

Keep gain as a named constant or small field so later phases can retune it
without hunting through formulas.

## Automated Test Procedure

1. Build the project.
2. Run existing tests.
3. Capture current output using the Phase 1 workflow:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase2.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

4. Compare:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase2.wav \
  --out ./build/sid-phase2-metrics.json
```

5. Compare Phase 2 metrics to the Phase 1 baseline.

## Acceptance Criteria

- Existing tests pass.
- SID register tests still pass without altered visible register semantics.
- Captured c64m output has substantially lower DC offset than the Phase 1
  baseline.
- Captured c64m RMS is closer to the VICE reference than the Phase 1 baseline.
- No sustained samples exceed the expected `[-1.0, +1.0]` range after final
  conditioning.
- The scalar comparison score from Phase 1 improves or, if it does not, the
  reason is documented before proceeding.
- `STATUS.md` is updated only after implementation and tests support the claim.

## Suggested Tests

Add focused tests in `tests/machine/test_sid.c` or a nearby SID test file:

- reset clears DC blocker state;
- silence remains silence;
- constant input into the final stage decays toward zero output;
- normal generated waveform remains nonzero;
- output is bounded for high-volume three-voice cases.

## Human Verification

Listen to the Phase 2 capture and verify that it is not obviously clipped,
offset, or explosively louder than the VICE reference.

Human listening does not replace the metrics.
