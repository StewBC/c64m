# C64SID_IMP_5.md
# SID Improvement Phase 5 - Filter Calibration And Regression Lockdown

## Component

C64MSID

## Status

Implementation planning guide.

## Purpose

After the output path, routing, and oscillator control behavior are improved,
calibrate the functional filter approximation against the VICE reference and
lock down regression tests for future SID work.

This phase should tune the existing practical filter model. It should not turn
c64m into a full analog SID simulation.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md`
6. `C64SID_IMP_2.md`
7. `C64SID_IMP_3.md`
8. `C64SID_IMP_4.md`
9. This guide

## Goal

Use the automated comparison harness and focused tests to tune:

- cutoff mapping;
- resonance behavior;
- LP/BP/HP mode mixing;
- final filter gain balance;
- stability under extreme register values.

## In Scope

- Replace the linear cutoff factor with a better documented mapping.
- Adjust resonance damping if metrics and listening support it.
- Adjust combined LP/BP/HP output scaling.
- Add filter regression tests.
- Add an audio regression target using `el_cartero`.
- Update `STATUS.md` with the final accepted SID behavior and remaining
  deferrals.

## Explicit Non-Goals

Do not implement these in this phase:

- Full nonlinear 6581 filter.
- 8580 model.
- Runtime SID variant switching.
- Temperature/supply-voltage modeling.
- External input accuracy.
- Multi-SID support.
- Sample-perfect digi support.

## Architecture Contract

Filter behavior remains in `machine/sid.c`.

The automated comparison tool remains outside `machine/`.

Do not let comparison metrics or reference-file paths leak into emulator runtime
behavior.

## Implementation Strategy

1. Run Phase 1 comparison using the best output from Phase 4.
2. Inspect where filter-heavy sections differ from VICE by metrics and by
   listening.
3. Tune one parameter family at a time:
   - cutoff mapping;
   - resonance damping;
   - mode output scaling;
   - final gain compensation.
4. Keep a metrics file for each candidate.
5. Keep only changes that improve the overall score or produce a documented
   audible improvement without damaging stability.

## Cutoff Mapping Guidance

The current model uses a simple proportional mapping from the 11-bit cutoff
register to filter coefficient. Replace it only with a mapping that is:

- deterministic;
- cheap enough for per-cycle use;
- stable for all cutoff values;
- documented with the reason it was chosen.

Acceptable options:

- a small lookup table generated at reset or compile time;
- a simple curved formula;
- a clamped coefficient mapping based on measured comparison results.

Do not add `math.h` dependency in hot per-cycle code unless the project already
accepts it and performance is measured.

## Regression Lockdown

After tuning, create a stable regression check that can be run locally without
committing generated audio:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase5.wav \
  --out ./build/sid-phase5-metrics.json
```

The check should support a threshold mode equivalent to:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase5.wav \
  --max-score-regression <value>
```

Keep thresholds loose enough to avoid false failures from MP3 decode and host
timing differences. This is a local fidelity guard, not a bit-perfect audio
test.

## Suggested Tests

Add or extend SID tests for:

- cutoff minimum remains stable;
- cutoff maximum remains stable;
- resonance maximum remains stable;
- LP, BP, HP, and combined modes produce bounded output;
- routed and bypass voices remain mixed correctly after tuning;
- reset clears filter state;
- no filter mode bypasses the filter path as documented.

## Automated Audio Procedure

Run:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase5.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

Then:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase5.wav \
  --out ./build/sid-phase5-metrics.json
```

Compare against every earlier phase, especially Phase 4.

## Acceptance Criteria

- Existing tests pass.
- New filter regression tests pass.
- Filter output remains bounded for extreme cutoff/resonance/mode combinations.
- Automated comparison score improves against Phase 4, or a documented listening
  improvement is accepted despite a small score regression.
- `el_cartero` capture has no new large DC offset or clipping.
- `STATUS.md` records implemented SID behavior and still-deferred items.

## Human Verification

Listen to the final c64m capture against `./samples/x65sc.mp3`. The expected
result is not bit-perfect VICE output; it should be materially closer in level,
timbre, filter movement, and overall musical shape than the Phase 1 baseline.
