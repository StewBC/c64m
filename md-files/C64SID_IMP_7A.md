# C64SID_IMP_7A.md
# SID Improvement Phase 7A - Comparison Lag Search And Capture Tuning

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

The automated comparison score has been stuck near 3.75 across Phases 5, 6, and 7
despite measurable improvements to ADSR shape, filter routing, and filter cutoff
range. The dominant score term — envelope correlation — has remained at
approximately 0.157 throughout all three phases, including Phase 6 which directly
changed how note envelopes decay. A correlation value that does not respond to
ADSR changes is not measuring ADSR quality. It is measuring something structural
in the comparison setup.

The most likely cause is that the ±1-second lag search in `tools/compare_sid_audio.py`
is too narrow to find the true alignment between the VICE reference and the c64m
capture. If c64m reaches the music start point more than one second later than
VICE did when the reference was captured, the search returns the least-bad
misalignment rather than the real one. This inflates the correlation error term,
which contributes 90% of the scalar score, and masks the effect of every
subsequent SID improvement.

This phase fixes the measurement before further SID work continues, so that
later phases can actually see whether their changes help.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md` through `C64SID_IMP_7.md`
6. This guide

## Goal

Widen the lag search and, if needed, tune the capture start time so that the
best-lag correlation reflects genuine musical alignment rather than a forced
best-of-misaligned-captures.

After this phase:

- The comparison tool finds the correct lag when the two captures are offset
  by up to ±4 seconds.
- The correlation value responds visibly to SID changes that affect note shape.
- The scalar score reflects actual fidelity distance, not measurement noise.

## In Scope

- Increase `DEFAULT_MAX_LAG_BLOCKS` in `tools/compare_sid_audio.py` from 100
  to a value that covers ±3–4 seconds of lag.
- Optionally tune the `--audio-record-start` warmup time in
  `tools/capture_sid_audio.py` if measurement shows c64m reaches the music
  start point at a measurably different time than the reference assumed.
- Re-run the comparison with the widened search and record new baseline metrics.
- Update `STATUS.md` with the new correlation level and revised score.

## Explicit Non-Goals

Do not implement these in this phase:

- SID waveform or filter changes.
- ADSR changes.
- Output-stage changes.
- Any new audio path code.
- Multi-song comparison support.

## Diagnosis Procedure

Before changing anything, determine whether the low correlation is caused by
a capture timing offset or by genuine musical difference.

### Step 1 — Widen the lag search temporarily

Edit `DEFAULT_MAX_LAG_BLOCKS` in `tools/compare_sid_audio.py` from 100 to 400
(covers ±4 seconds at 441 samples/block at 44100 Hz).

Re-run:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-el-cartero.wav \
  --out ./build/sid-phase7a-wide-metrics.json
```

Observe:

- If the best-lag offset jumps well beyond ±1 second, a capture timing
  mismatch is confirmed. The correlation should also rise noticeably.
- If the best-lag offset stays within ±1 second and correlation remains near
  0.157, the low correlation reflects a real musical difference, not a
  measurement artifact. Document this result and investigate CIA/IRQ timing
  before continuing SID fidelity work.

### Step 2 — Tune the capture start time (if warranted)

If the lag search reveals that the music starts N seconds later in c64m than
the reference assumed, adjust `--audio-record-start` in
`tools/capture_sid_audio.py` so the two captures land at the same musical
position. Then re-run the full capture and comparison workflow.

If the lag is consistently positive (c64m music starts later), increase the
warmup by the measured offset. If the lag varies between runs, the two captures
are landing at different positions in a repeating loop; in that case prefer
increasing capture duration (see below) rather than chasing the phase.

### Step 3 — Increase capture duration (if loop-phase variation persists)

If the music loops with a period shorter than the lag search window, the
correlation will vary between runs depending on which part of the loop each
4-second window happens to start on. The mitigation is to capture a longer
window (8–10 seconds) so the loop-phase effect averages out.

Increase `--audio-record-duration` in `tools/capture_sid_audio.py` and adjust
the kill timeout accordingly. The comparison tool handles different durations
automatically; it aligns and overlaps whatever duration is available.

## Implementation

Apply whichever of the following steps the diagnosis warrants:

1. Set `DEFAULT_MAX_LAG_BLOCKS` to cover ±4 seconds:
   - At 44100 Hz with block size 441 samples: 1 second = 100 blocks.
   - ±4 seconds = 400 blocks. Set `DEFAULT_MAX_LAG_BLOCKS = 400`.

2. Adjust the default warmup in `tools/capture_sid_audio.py` if the measured
   lag indicates c64m reaches the music start point at a different wall-clock
   time than the reference assumed.

3. Increase `music_duration` default in `tools/capture_sid_audio.py` if loop
   averaging is needed. Update the kill timeout proportionally.

Keep changes minimal. Do not refactor the comparison tool beyond what is
required to fix the alignment.

## Acceptance Criteria

- Existing tests pass without modification (the comparison tool is a script,
  not part of the test suite).
- The widened lag search runs to completion without error on the current
  captures.
- The comparison produces a best-lag offset and correlation value that are
  stable across two or more consecutive capture-and-compare runs (within
  ±2 blocks variation).
- The new correlation value is documented in `STATUS.md`.
- If the correlation rises to ≥ 0.4 after lag correction, the score is
  re-baselined and the old 3.75 entry is marked superseded.
- If the correlation remains near 0.157 after widening and tuning, that result
  is documented as a genuine musical-difference finding and the CIA/IRQ timing
  path is flagged for investigation before further SID improvement phases.

## Automated Audio Procedure

After any change to the comparison tool or capture script, run:

```text
python3 tools/capture_sid_audio.py
```

Then:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-el-cartero.wav \
  --out ./build/sid-phase7a-metrics.json
```

Compare the new lag offset and correlation against the Phase 7 baseline
(`./build/sid-phase7-metrics.json`).

## Human Verification

Listen to the Phase 7A capture alongside `./samples/x65sc.mp3`. If the musical
phrasing matches in timing — notes starting and stopping at the same moments —
the lag correction is working. If the musical phrasing differs structurally
(different note patterns, different tempo), the low correlation is a real
fidelity problem that requires CPU or CIA timing investigation.
