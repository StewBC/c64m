# C64SID_IMP_3.md
# SID Improvement Phase 3 - Per-Voice Filter Routing

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

Implement SID filter routing controlled by `$D417` so that only selected voices
enter the filter path, while unfiltered voices bypass it and are mixed back into
the final output.

The current implementation routes the full mixed signal through the filter when
any filter mode bit is active. That is a major fidelity limitation for normal
SID music.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. `C64SID_IMP_1.md`
6. `C64SID_IMP_2.md`
7. This guide

## Goal

Make `$D417` voice routing affect the audible output:

- bit 0 routes voice 1 through the filter;
- bit 1 routes voice 2 through the filter;
- bit 2 routes voice 3 through the filter;
- unselected voices bypass the filter;
- `$D418` bit 7 still disconnects voice 3 from the audible output as currently
  documented.

## In Scope

- Decode and apply `$D417` voice routing bits.
- Split mixed voice output into filtered and unfiltered paths.
- Mix filtered and unfiltered paths after mode selection.
- Preserve current resonance nibble decoding unless a small adjustment is
  required by the split path.
- Add tests that prove routing affects output.
- Use the Phase 1 harness to score the result against `./samples/x65sc.mp3`.

## Explicit Non-Goals

Do not implement these in this phase:

- Exact 6581 nonlinear filter behavior.
- External input filter routing unless already represented cleanly.
- Oscillator sync.
- Ring modulation.
- Combined waveform blending.
- ADSR changes.
- SID chip variant switching.

## Architecture Contract

All SID behavior remains in `machine/sid.c` / `machine/sid.h`.

Runtime and platform must not know which voices are routed through the SID
filter.

Frontend may display copied snapshot state only; it must not influence routing.

## Implementation Strategy

The current mixer computes per-voice post-envelope values:

```text
v1
v2
v3
```

Change the mixing structure to:

```text
filtered_in = sum of routed audible voices
bypass_out = sum of unrouted audible voices
filter_out = apply selected LP/BP/HP modes to filtered_in
final = filtered contribution + bypass_out
apply volume/output conditioning
```

If no filter mode bits are set, all voices should effectively bypass filtering.

If no voices are routed into the filter but filter mode bits are set, the filter
input should be silence and only bypass voices should remain audible.

Keep scaling conservative. Do not let splitting the paths make three-voice music
significantly louder than Phase 2.

## Voice 3 Disconnect Rule

`$D418` bit 7 means voice 3 is disconnected from audio output. Preserve that
behavior across both filtered and bypass paths.

Voice 3 oscillator and envelope readback must continue to work even when voice 3
is disconnected from output.

## Suggested Tests

Add focused tests for:

- with LP mode enabled and only voice 1 routed, voice 1 output differs from
  bypass-only output;
- with LP mode enabled and voice 1 not routed, voice 1 remains audible through
  bypass;
- with all voice routing bits clear and filter mode enabled, voices remain
  audible through bypass;
- voice 3 disconnect removes voice 3 from output whether or not `$D417` routes
  it;
- resonance nibble is still decoded from `$D417` high nibble.

Tests should avoid asserting exact analog values. Prefer relational assertions
such as nonzero, differs from previous path, bounded, or lower high-frequency
energy for a routed low-pass case.

## Automated Audio Procedure

After tests pass, run:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-phase3.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

Then compare:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-phase3.wav \
  --out ./build/sid-phase3-metrics.json
```

Compare Phase 3 metrics against Phase 2, especially:

- envelope correlation;
- spectral band differences;
- RMS and peak behavior.

## Acceptance Criteria

- Existing tests pass.
- New routing tests pass.
- `$D417` low routing bits audibly affect output.
- Voice 3 disconnect remains correct.
- Captured `el_cartero` output is not louder or more DC-biased than Phase 2.
- Comparison metrics improve, or any regression is documented with a reason and
  a decision about whether to keep the change.
- `STATUS.md` is updated only after implementation and tests support the claim.

## Human Verification

Listen for obvious filter-related improvement: sections that should have a
filtered voice should no longer make every voice sound filtered together.
