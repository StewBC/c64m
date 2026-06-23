# C64SID_IMP_1.md
# SID Improvement Phase 1 - Automated Capture And Comparison Harness

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

Create a repeatable automated way to run the emulator, capture the SID output
from the same program used for the VICE reference, and score c64m output against
`./samples/x65sc.mp3`.

This phase must not tune SID behavior. It creates the measuring stick for later
SID improvement phases.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MSID.md`
5. This guide

## Test Asset Contract

The reference assets live in:

```text
./samples/el_cartero.prg
./samples/x65sc.mp3
./samples/c64m.mp3
```

Running:

```text
./build/c64m -a -p ./samples/el_cartero.prg
```

starts the same tune after approximately 9.5 seconds.

The emulator does not quit by itself. Any automated run must kill it after:

```text
9.5 seconds warmup + desired music capture duration + small guard interval
```

For the existing reference files, use a default music capture duration of 4.0
seconds.

## Goal

Add an automated capture and comparison workflow that can be used after every
SID change to answer:

- Did c64m move closer to the VICE reference?
- Did c64m get louder or quieter than the accepted baseline?
- Did DC offset improve or regress?
- Did envelope alignment/correlation improve?
- Did spectral balance move toward or away from the reference?

## In Scope

- Add a deterministic audio capture path suitable for automated testing.
- Add a comparison script or small tool that scores captured c64m audio against
  `./samples/x65sc.mp3`.
- Add documentation for the exact run command and timeout behavior.
- Establish and record baseline metrics from current c64m output.
- Keep all new capture/analysis code dependency-safe with existing architecture.

## Explicit Non-Goals

Do not implement these in this phase:

- SID waveform changes.
- SID filter changes.
- ADSR changes.
- Runtime pacing changes unless required only to make capture deterministic.
- UI changes.
- Host audio loopback recording through macOS, SDL device capture, or external
  audio routing software.
- Any change that makes normal emulator audio dependent on test-only recording.

## Architecture Contract

SID still belongs to `machine/`.

The recommended capture point is in `runtime/`, immediately where runtime reads
`sid_sample()` and writes the resulting float sample to the shared audio buffer.

Allowed:

```text
runtime reads sid_sample()
runtime writes optional test WAV/PCM capture
runtime writes normal samples to util/audio_buffer
tools or scripts compare generated audio files
```

Forbidden:

```text
machine -> runtime
machine -> platform
runtime -> frontend
runtime -> platform audio APIs
SDL audio callback -> machine or runtime
```

Do not record from the SDL callback. That measures host callback scheduling and
underruns instead of SID output.

## Recommended Capture Interface

Add test-oriented command-line options equivalent to:

```text
--audio-record <wav-path>
--audio-record-start <seconds>
--audio-record-duration <seconds>
```

The default test run should be:

```text
./build/c64m -a -p ./samples/el_cartero.prg \
  --audio-record ./build/sid-el-cartero.wav \
  --audio-record-start 9.5 \
  --audio-record-duration 4.0
```

It is acceptable for the emulator process to keep running after recording
finishes. The automation wrapper must still terminate the process by timer.

If adding CLI options is too invasive, create an internal test mode or helper
binary that uses the same runtime path and documents the equivalent command.

## Timeout Wrapper

Use a portable wrapper rather than relying on GNU `timeout`, which may not exist
on macOS by default.

Suggested script behavior:

```text
start process
wait warmup + duration + guard seconds
terminate process
wait briefly
force-kill if still running
return nonzero if no recording was produced
```

Default timing:

```text
warmup: 9.5 seconds
music duration: 4.0 seconds
guard: 1.0 second
total kill time: 14.5 seconds
```

## Comparison Tool

Add a script or tool equivalent to:

```text
tools/compare_sid_audio.py \
  --reference ./samples/x65sc.mp3 \
  --candidate ./build/sid-el-cartero.wav \
  --out ./build/sid-el-cartero-metrics.json
```

The tool may use `ffmpeg`/`ffprobe` if already available in the local
environment. If not, document the requirement clearly. Avoid adding heavy C
dependencies for MP3 decoding.

## Required Metrics

Compute at least:

- duration after decode;
- sample rate after decode/resample;
- mono RMS;
- mono peak;
- mean/DC offset;
- crest factor;
- best gain from candidate to reference;
- envelope correlation after coarse alignment;
- estimated alignment offset in seconds;
- per-band relative power for these bands:

```text
20-100 Hz
100-250 Hz
250-500 Hz
500-1000 Hz
1000-2000 Hz
2000-4000 Hz
4000-8000 Hz
8000-16000 Hz
16000-22050 Hz
```

Also compute one scalar score for quick regression checks. The score does not
need to be perfect; it must be stable and documented. Prefer a weighted score
based on:

- envelope correlation, higher is better;
- RMS ratio error, lower is better;
- DC offset magnitude, lower is better;
- spectral band difference, lower is better.

## Baseline Requirement

Before changing SID behavior in later phases, run the tool against current c64m
and record the baseline metrics in a generated artifact such as:

```text
./build/sid-baseline-metrics.json
```

Do not commit generated audio or metrics unless the project explicitly accepts
large binary/test artifacts.

## Acceptance Criteria

- The project builds.
- The automated run launches:

```text
./build/c64m -a -p ./samples/el_cartero.prg
```

or the documented extended capture equivalent.
- The run is terminated by timer and does not hang indefinitely.
- A 4.0 second c64m audio capture is produced from approximately 9.5 seconds
  after emulator launch.
- The comparison tool decodes `./samples/x65sc.mp3` and the captured c64m audio.
- A JSON metrics file is produced.
- The tool reports enough metrics to decide whether a later SID change moved
  closer to or farther from the VICE reference.
- Existing tests continue to pass.
- `STATUS.md` is updated only if implementation actually lands.

## Human Verification

After implementation, listen to:

```text
./samples/x65sc.mp3
./build/sid-el-cartero.wav
```

Human listening is advisory. Phase success is the repeatable capture and metrics
pipeline, not audio fidelity improvement.
