# C64AUDFID_1.md
# Audio Output Infrastructure Implementation Guide

## Component

C64MAUDIO

## Status

Coding-agent-ready implementation guide.

## Purpose

Add the first host audio output path for c64m without violating the existing
architecture. This guide creates the dependency-safe path that later SID work
uses to get generated samples from the runtime thread to the host SDL audio
device.

This guide does not implement SID. It provides a deterministic smoke producer so
that the audio path can be proven before SID is present.

## Required Reading Before Coding

Read these in order before editing code:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MAUDIO.md`
5. This guide

If any document disagrees with code or tests, do not guess. Treat the mismatch
as a reconciliation task and document the result in `STATUS.md` only after code
and tests support it.

## Goal

Implement a host audio path with this flow:

```text
runtime thread -> util audio buffer -> SDL audio callback -> host audio device
```

The runtime must be able to write generated mono audio samples into a shared
buffer. The SDL callback must be able to consume those samples and fill the host
stream. The callback must not call runtime, machine, frontend, or C64-specific
logic.

## In Scope

- A generic audio sample buffer in `util/`.
- SDL audio device setup in `platform/`.
- SDL audio callback that consumes from the util buffer.
- Runtime-side sample production hook using a deterministic test tone.
- Runtime-to-buffer sample flow without runtime depending on platform or SDL.
- Buffer underrun and overrun handling.
- Sample format choice and conversion policy.
- PAL/NTSC-safe sample accumulator policy for future SID sample generation.
- Minimal smoke path proving generated samples reach the host audio device.
- Tests or diagnostics for buffer behavior and callback-safe failure modes.
- `STATUS.md` update after implementation.

## Explicit Non-Goals

Do not implement these in this phase:

- SID register map or waveform emulation.
- Music playback UI.
- WAV recording.
- MIDI.
- Audio effects.
- Perfect latency tuning.
- CoreAudio, WASAPI, ALSA, or other non-SDL backends.
- IEC serial bus.
- 1541 CPU, ROM, firmware, or drive-side emulation.
- Fast loaders.
- D64 writes or SAVE to disk.
- Cartridge support.
- CIA Phase I or Phase J behavior.
- VIC-II light pen.
- Open-bus behavior.

## Architecture Contract

Allowed dependency direction:

```text
runtime  -> machine + util
platform -> util + SDL2
machine  -> util
frontend -> runtime_client + platform + tools + util
```

Forbidden:

```text
runtime  -> platform
runtime  -> SDL
platform -> runtime
platform -> machine
machine  -> platform
machine  -> runtime
SDL callback -> runtime
SDL callback -> machine
```

Thread ownership:

```text
Runtime thread:
    runtime
    live machine
    writes audio samples

SDL audio callback thread:
    platform audio callback only
    reads util audio buffer
    writes host stream

UI thread:
    SDL event/render/frontend
    no live machine access
```

No live machine pointer may cross into the callback.

## Files To Add

Add these files unless the repo already has equivalent files with compatible
names. If equivalent files exist, extend them instead of duplicating concepts.

```text
src/util/audio_buffer.h
src/util/audio_buffer.c
src/platform/platform_audio.h
src/platform/platform_audio.c
```

## Files To Inspect And Possibly Modify

Inspect actual repo names before editing. Likely integration points include:

```text
CMakeLists.txt or src/*/CMakeLists.txt
src/runtime/*
src/platform/*
src/frontend/* or app startup/shutdown files
STATUS.md
```

Do not invent a second runtime or platform initialization path. Use the existing
application startup/shutdown structure.

## Internal Sample Format

Use this format inside util/runtime/machine-facing code:

```text
sample type: float
channels: mono
range: -1.0f to +1.0f
nominal host rate: 48000 Hz
actual host rate: value accepted by SDL device open
```

Rationale:

- Float avoids early clipping during later SID mixing and filter approximation.
- Mono is correct for one SID chip.
- Stereo duplication belongs at the platform boundary, not in machine or runtime.

## Platform Output Format

Request SDL audio with:

```text
format: AUDIO_F32SYS
channels: 2
frequency: 48000
samples: 512 or the nearest existing project default
callback: platform-owned audio callback
userdata: platform audio state containing only util buffer and format data
```

Accept SDL changes to `freq`, `format`, `channels`, and `samples` if SDL returns
a valid opened device. Record the actual accepted values.

If SDL accepts `AUDIO_F32SYS`, the callback should duplicate mono float samples
to all output channels. If the existing platform policy requires another format,
perform conversion in `platform_audio.c` only. Do not leak SDL format decisions
into runtime or machine.

## Util Audio Buffer Design

Implement a single-producer/single-consumer ring buffer.

Producer:

```text
runtime thread
```

Consumer:

```text
SDL audio callback thread
```

Required properties:

- Stores mono `float` samples.
- Fixed capacity chosen at initialization.
- Capacity is expressed in samples, not bytes.
- Non-blocking read and write.
- No blocking mutex in the SDL callback.
- Safe when producer and consumer run concurrently.
- Counts underruns.
- Counts overruns.
- Can be reset safely during startup/shutdown/reset when audio is stopped or under
  the existing lifecycle lock/ordering.

Preferred implementation:

- Use a power-of-two capacity.
- Use C11 atomics if the project already permits C11.
- If the project is strictly C99, use the existing project atomic abstraction if
  present. If no atomic abstraction exists, add the smallest util-local atomic
  wrapper that the project accepts, or document that the build must move this
  file to C11. Do not use volatile as a substitute for thread safety.
- Maintain separate read and write indices.
- Leave one slot empty to distinguish full from empty, or track count with
  atomics. Pick one simple policy and document it in comments.

Recommended struct visibility:

- Prefer an opaque `audio_buffer` type if the project style supports allocation
  helpers.
- If stack allocation is common in the repo, expose a small struct but keep field
  comments clear and do not let callers mutate internals directly.

## Required Util API

Implement this conceptual API. Names may be adjusted to match project naming,
but keep the same functionality.

```c
typedef struct audio_buffer audio_buffer;

int audio_buffer_init(audio_buffer *buffer, size_t capacity_samples);
void audio_buffer_shutdown(audio_buffer *buffer);
void audio_buffer_reset(audio_buffer *buffer);

size_t audio_buffer_write(audio_buffer *buffer, const float *samples, size_t count);
size_t audio_buffer_read(audio_buffer *buffer, float *samples, size_t count);

size_t audio_buffer_available_read(const audio_buffer *buffer);
size_t audio_buffer_available_write(const audio_buffer *buffer);
size_t audio_buffer_capacity(const audio_buffer *buffer);

uint64_t audio_buffer_underrun_count(const audio_buffer *buffer);
uint64_t audio_buffer_overrun_count(const audio_buffer *buffer);
```

Behavior:

- `audio_buffer_write` writes as many samples as fit.
- If not all requested samples fit, reject the excess new samples and increment
  the overrun counter once for that write call.
- `audio_buffer_read` reads as many samples as available and returns the count.
- The caller fills silence for any missing samples.
- The SDL callback must not allocate memory.
- The SDL callback must not log every underrun.

## Underrun Policy

If the callback asks for N samples and fewer than N are available:

1. Read the available samples.
2. Fill the rest of the output stream with silence.
3. Increment underrun count once for the callback or read call that observed the
   shortage.

Silence value:

```text
0.0f
```

Do not output stale samples.

## Overrun Policy

Use this policy:

```text
reject new samples that do not fit
```

When a write cannot fit all requested samples:

1. Write the prefix that fits.
2. Drop the unwritten suffix.
3. Increment overrun count once for that write call.
4. Return the number of samples actually written.

Do not drop old samples in this milestone. Rejecting new samples makes producer
overload visible and keeps callback behavior simple.

## Platform Audio State

Implement a platform-owned state object with at least:

```c
typedef struct platform_audio platform_audio;
```

It should contain:

- SDL audio device id.
- Actual sample rate.
- Actual channel count.
- Actual SDL format.
- Callback sample count.
- Pointer to the util `audio_buffer`.
- Enabled/open/paused state.

It must not contain:

- `c64 *`.
- `runtime *`.
- `sid *`.
- frontend UI pointers.
- C64 timing state.

## Required Platform API

Implement this conceptual API. Names may be adjusted to match project style.

```c
typedef struct platform_audio platform_audio;

typedef struct platform_audio_desc {
    int requested_rate;
    int requested_channels;
    int requested_callback_samples;
    audio_buffer *buffer;
} platform_audio_desc;

int platform_audio_init(platform_audio *audio, const platform_audio_desc *desc);
void platform_audio_shutdown(platform_audio *audio);
void platform_audio_start(platform_audio *audio);
void platform_audio_stop(platform_audio *audio);

int platform_audio_actual_rate(const platform_audio *audio);
int platform_audio_actual_channels(const platform_audio *audio);
int platform_audio_is_open(const platform_audio *audio);
```

The platform callback reads mono float samples from the buffer and writes the host
stream:

- Mono output: write one float per host sample.
- Stereo output: duplicate mono to left and right.
- More than two channels: duplicate mono to every channel or fill unsupported
  extra channels with silence, but document the chosen behavior.

## Startup Integration

At application startup:

1. Allocate or initialize the `audio_buffer` in a dependency-safe owner.
2. Open the platform audio device with a pointer to the buffer.
3. Read the actual accepted SDL sample rate.
4. Pass the buffer pointer and actual sample rate into runtime configuration using
   an existing dependency-safe setup path.
5. Start the audio device only after the buffer exists and runtime is ready to
   produce samples or silence.

Allowed allocation ownership options:

- A top-level application object owns both platform and runtime setup objects.
- Platform owns the SDL device but not the generic buffer.
- The buffer is not hidden behind `platform_audio.h` from runtime.

Forbidden:

- Runtime asks platform for the buffer.
- Runtime includes `platform_audio.h`.
- Runtime calls SDL.

## Runtime Integration

Add runtime fields equivalent to:

```c
audio_buffer *audio_out;
int audio_sample_rate;
double audio_cycle_accumulator;
int audio_enabled;
int audio_muted_or_paused;
```

The exact names should match project conventions.

For this guide, runtime should produce a deterministic smoke tone when audio
smoke mode is enabled. The smoke tone exists only to validate the audio path.
Later SID code will replace it with SID samples.

Runtime audio production must be isolated behind a small helper, for example:

```c
static void runtime_audio_produce(runtime *rt, uint32_t elapsed_machine_cycles);
```

This helper may write a small batch of mono float samples to the util buffer.

## Sample Accumulator Policy

Use a fractional accumulator that converts machine cycles to host samples.

Inputs:

```text
machine_clock_hz: PAL or NTSC C64 clock from machine configuration
host_sample_rate: actual SDL accepted rate
elapsed_machine_cycles: cycles advanced by runtime
```

Formula:

```text
samples_to_emit += elapsed_machine_cycles * host_sample_rate / machine_clock_hz
```

Implementation direction:

```c
accumulator += (double)elapsed_cycles * (double)host_rate;
while(accumulator >= machine_clock_hz) {
    emit_one_sample();
    accumulator -= machine_clock_hz;
}
```

The machine clock value must come from the same PAL/NTSC configuration used by
machine timing. Do not hard-code PAL only.

This phase may use the smoke-tone phase as the sample source. C64AUDFID_2 will
use the same cadence to sample SID output.

## Runtime Batch Policy

Do not batch a full frame of audio at once unless the existing runtime structure
forces it. Prefer one of these:

1. Emit samples after each small runtime cycle batch.
2. Emit samples once per raster line.
3. Emit samples at fixed host-sample batches of 64 or 128 samples.

Pick the smallest integration change that fits the current runtime. Document the
chosen batch point in comments and `STATUS.md`.

## Pause, Reset, Turbo, And Shutdown

Pause:

- Output silence while paused.
- Do not let the callback read stale tone or stale future SID samples.
- It is acceptable for runtime to stop producing samples while callback fills
  silence on underrun.

Reset:

- Reset the audio accumulator.
- Clear the audio buffer.
- Later SID reset must also reset SID state, but that is not part of this guide.

Turbo:

- Initial policy: mute audio in turbo mode unless the existing runtime has a
  stable real-time throttle.
- If turbo mode stays audio-enabled, sample production must remain bounded and
  must not flood the buffer. Document the chosen behavior.

Shutdown:

- Stop or pause the SDL audio device before destroying the buffer.
- Close the SDL device before freeing platform audio state.
- Do not leave callback userdata pointing to freed memory.

## Smoke Tone

Add a diagnostic smoke producer with these properties:

```text
waveform: sine or simple square
frequency: 440 Hz or 880 Hz
amplitude: 0.15f to 0.25f
channels: mono inside runtime/util, duplicated by platform
activation: debug flag, command-line option, compile-time diagnostic, or temporary local test mode
```

Keep the smoke producer separate from SID. It should be easy to remove or leave
as a diagnostic without becoming a music feature.

If the project does not have command-line options, add the smallest local
diagnostic function or test binary that opens audio and writes tone samples into
the buffer.

## Tests And Diagnostics

Add tests where the current project test harness supports them.

### Required Unit Tests For util Audio Buffer

Test cases:

1. Init/shutdown succeeds for a valid capacity.
2. Empty buffer read returns zero samples.
3. Write then read returns samples in order.
4. Wraparound preserves order.
5. Read shortage returns available samples and allows caller to fill silence.
6. Overrun rejects excess new samples and increments overrun counter.
7. Reset clears readable samples and counters if the API defines counter reset;
   otherwise document counter persistence.
8. Capacity and available read/write values are consistent.

### Required Platform Smoke Diagnostic

Create one minimal smoke path that proves:

1. SDL audio device opens.
2. Actual rate/channels/format are recorded.
3. Callback runs.
4. Callback reads from util buffer.
5. Underrun produces silence instead of crash.
6. Smoke tone reaches host audio when enabled.

This may be manual if CI cannot open an audio device. If manual, add clear run
instructions in a comment, test README, or `STATUS.md`.

### Required Runtime Smoke Diagnostic

Prove runtime can write to the util buffer without platform dependency:

1. Build runtime without including platform headers.
2. Inject or configure an `audio_buffer *` and sample rate.
3. Run the smoke producer for a fixed number of cycles.
4. Verify samples appear in the buffer.

## CMake / Build Requirements

- Add new source files to the correct targets.
- `util/audio_buffer.c` must not link SDL.
- `runtime` target must not link SDL because of this phase.
- `platform_audio.c` may link SDL2.
- Existing targets must continue to build.

## STATUS.md Update

After implementation and tests, update `STATUS.md` with:

- Audio buffer implemented in util.
- SDL audio device setup implemented in platform.
- Runtime-to-audio-buffer smoke path implemented.
- Sample format: float mono internal.
- Host output: SDL float, mono duplicated to accepted channels.
- Underrun policy: silence and count.
- Overrun policy: reject new samples and count.
- Turbo audio policy selected.
- Any remaining limitations.

Do not claim SID audio exists after this guide unless C64AUDFID_2 is also
implemented.

## Acceptance Checklist

This guide is complete only when all items below are true:

- `util/` contains a dependency-safe audio buffer.
- The buffer stores mono float samples.
- Buffer read/write is safe for one producer and one consumer.
- Buffer operations used by the callback do not block.
- Underrun outputs silence and increments a counter.
- Overrun rejects excess new samples and increments a counter.
- `platform/` owns SDL audio device creation and shutdown.
- `platform/` owns the SDL audio callback.
- The callback reads only from the util buffer and local platform state.
- The callback does not call runtime, machine, frontend, or tools.
- Runtime can write samples without including platform or SDL headers.
- Runtime uses the actual host sample rate accepted by SDL.
- PAL and NTSC sample production use the machine clock selected by config.
- Pause and reset do not leave stale unsafe audio.
- Turbo behavior is documented and bounded.
- A smoke tone or equivalent diagnostic proves generated samples reach the host
  audio device.
- Existing boot, video, debugger, keyboard, joystick, PRG, D64, PAL, and NTSC
  tests still pass.
- `STATUS.md` reflects the implemented behavior and deferred SID behavior.
