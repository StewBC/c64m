# C64MENH Phase 4: Measured SID / Audio Fidelity Improvement

## Purpose

Run a measured SID/audio fidelity phase that identifies one dominant remaining audio error, makes at most one focused improvement, and validates the result with metrics and tests.

This is a measured-improvement task, not an open-ended SID rewrite.

## Background

The emulator already has functional SID audio and host audio output:

- SDL audio output infrastructure exists.
- SID voices, ADSR, mixer, filter, voice 3 read-back, and register map are implemented.
- Audio production now samples SID output from the cycle-stepping path and averages per-cycle samples into host samples.
- Previous measured SID improvement phases used capture and comparison tooling.

Remaining deferred SID/audio areas include exact 6581/8580 analog waveform behavior, paddle/pot reads, NTSC rate tables, and further measured high-frequency/audio fidelity work.

The current milestone requires recognizable SID audio and voice 3 oscillator/envelope reads good enough for ordinary software. It does not require bit-perfect SID analog behavior.

## Required Reading

Read in this order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MAUDIO.md`, if present
5. `C64MSID.md`, if present
6. Any current SID/audio phase or implementation guide, if present
7. Current SID, runtime audio, platform audio, and audio tooling code

## Suggested Branch

Use a dedicated branch:

```sh
git checkout -b enhancement/measured-sid-fidelity
```

## Phase Goal

Answer these questions with evidence:

1. What is the current measured SID/audio baseline?
2. Which remaining error appears dominant and milestone-relevant?
3. Can one small change improve that error without regressions?
4. Are current metrics stable enough to justify the change?
5. Should the phase proceed, or should SID/audio remain as-is for this milestone?

## Files And Areas To Inspect

At minimum, inspect:

```text
src/machine/sid.c
src/machine/sid.h
src/runtime/runtime_thread.c
src/util/audio_buffer.c
src/util/audio_buffer.h
src/platform/platform_audio.c
src/platform/platform_audio.h
tests/machine/test_sid.c
tests/runtime/test_runtime_scheduler.c
tools/capture_sid_audio.py
tools/compare_sid_audio.py
md-files/STATUS.md
md-files/C64MAUDIO.md, if present
md-files/C64MSID.md, if present
```

Search terms:

```text
SID
audio
sample
filter
waveform
ADSR
envelope
oscillator
voice3
capture
compare
RMS
spectral
correlation
NTSC
PAL
```

## Candidate Improvement Areas

Consider only one focused area per run.

Possible candidates:

1. NTSC SID rate tables or PAL/NTSC ADSR timing confirmation.
2. Further high-frequency excess investigation after the Phase 9 scheduler fix.
3. Combined waveform approximation improvement, if measurable and bounded.
4. Filter coefficient or resonance refinement, if a specific test case exercises it.
5. Audio comparison tooling stability or baseline management.
6. Voice 3 oscillator/envelope read-back tests for ordinary-software randomness/detection use.

Avoid open-ended analog modeling.

## Analysis Commands

Suggested inspection commands:

```sh
git status --short --branch
rg -n "SID|sid_|audio|sample|filter|waveform|ADSR|envelope|voice3|capture|compare|RMS|spectral|correlation|NTSC|PAL" src tests tools md-files
nl -ba src/machine/sid.c | sed -n '1,260p'
nl -ba src/machine/sid.c | sed -n '260,620p'
nl -ba src/machine/sid.c | sed -n '620,980p'
nl -ba src/runtime/runtime_thread.c | sed -n '240,340p'
nl -ba src/runtime/runtime_thread.c | sed -n '2840,2940p'
nl -ba tests/machine/test_sid.c | sed -n '1,260p'
nl -ba tests/runtime/test_runtime_scheduler.c | sed -n '420,560p'
```

Adjust line ranges as needed.

## Measurement Guidance

Before changing code, establish a baseline.

Use existing tools if reference files are available:

```sh
python3 tools/capture_sid_audio.py --help
python3 tools/compare_sid_audio.py --help
```

Then run the repo's established SID capture/compare process. Record:

- exact command lines;
- reference file used;
- PAL or NTSC mode;
- warmup and capture duration;
- score;
- correlation;
- RMS;
- DC offset;
- peak;
- spectral-band errors;
- whether results are stable across repeated runs.

If reference files are unavailable, do not invent metrics. In that case, limit the phase to code/test inspection or tooling improvements.

## Stop / Continue Gate

After baseline measurement and analysis, stop and write a decision note before implementing.

Continue only if:

- a specific dominant error is identified;
- the error is milestone-relevant or clearly improves ordinary-software SID output;
- a small implementation change can target it;
- metrics or focused tests can validate the result.

Stop without implementation if:

- current SID/audio is sufficient for the milestone;
- no stable reference measurement is available;
- likely improvements are speculative analog modeling;
- the change would broaden into 8580 support, runtime SID variant switching, or bit-perfect filtering.

## Implementation Guidance If Continuing

If implementation is needed:

1. Make one focused change only.
2. Add or update focused SID/audio tests.
3. Re-run baseline comparison with the same reference and parameters.
4. Compare before/after metrics.
5. Reject or revert the change if it worsens the target metric without a documented reason.
6. Keep runtime/platform dependency rules intact.

Respect architecture rules:

- SID behavior lives in `machine/`.
- Runtime may write samples to the shared audio buffer.
- SDL audio device and callback stay in `platform/`.
- Runtime must not include SDL or platform headers.
- SDL audio callback must not call runtime or machine code.

## Acceptance Criteria

This phase is complete when:

- baseline metrics are recorded, or measurement unavailability is explicitly documented;
- one focused improvement is accepted or the phase is stopped with evidence;
- SID tests pass;
- runtime audio scheduler tests pass;
- full test suite passes;
- `STATUS.md` reflects reality if a durable status claim changed;
- deferred SID behavior remains explicitly deferred.

## Required Commands Before Final Hand-Off

At minimum:

```sh
cmake --build build
ctest --test-dir build
```

If measurement is possible, also run the exact capture/compare commands used for the baseline and candidate.

Do not run `./build/c64m` without a timeout.

## Hand-Off Report

End with a concise hand-off report containing:

- branch name;
- commit hash or note that changes are uncommitted;
- exact commands run;
- files inspected;
- files changed;
- tests run and results;
- baseline audio metrics;
- candidate metrics, if changed;
- whether the change was accepted, rejected, or deferred;
- whether SID/audio is sufficient for the current milestone;
- known limitations;
- recommended next step.
