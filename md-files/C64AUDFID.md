# Next Implementation Milestone: Audio and SID Fidelity

## Milestone Name

C64M-AUDIO-SID-1

## Goal

Add the first concrete audio milestone for the PAL/NTSC fidelity target.

The emulator must be able to generate recognizable SID audio and deliver it to the host audio device without violating the project architecture rules.

This milestone has two linked implementation guides:

1. C64MAUDIO implementation guide
2. C64MSID implementation guide

C64MAUDIO should be implemented first or in lockstep with C64MSID, because SID output is not complete until generated samples can reach the host audio device.

## Scope Boundary

In scope:

* SDL audio output infrastructure in `platform/`.
* Dependency-safe audio sample buffer in `util/`.
* Runtime-to-audio sample flow without `runtime/` depending on `platform/` or SDL.
* Initial buffering, underrun, overrun, sample format, and resampling policy.
* Minimal smoke path proving generated samples reach the host audio device.
* Machine-layer SID register map at `$D400-$D41F`.
* Three SID voices.
* Recognizable pulse, saw, triangle, and noise output.
* ADSR envelope behavior.
* Gate-controlled voice amplitude.
* Mixer and global volume.
* Voice 3 oscillator read at `$D41B`.
* Voice 3 envelope read at `$D41C`.
* Initial bounded functional filter approximation.

Explicitly out of scope:

* IEC serial bus protocol.
* 1541 CPU, ROM, firmware, or drive-side emulation.
* Fast loaders.
* D64 writes, SAVE to disk, directory modification, DOS command channel, or disk error channel.
* Cartridge support.
* CIA Phase I or Phase J work.
* VIC-II light pen.
* Last-byte-on-bus open-bus behavior.
* Bit-perfect SID analog behavior.
* Exact 6581 filter nonlinearity.
* Full 6581/8580 variant switching.
* ReSID-level compatibility.
* Sample-perfect digi playback.

---

# Guide 1: C64MAUDIO Implementation Guide

## Purpose

Create the host audio path used by SID playback.

The audio path must allow the runtime thread to write generated audio samples into a dependency-safe buffer and allow the SDL audio callback to consume those samples without touching runtime, machine, frontend, or C64-specific state.

## Ownership

`platform/` owns:

* SDL audio initialization.
* SDL audio device open/close.
* SDL audio callback.
* Host audio format negotiation.
* Host stream filling.

`util/` owns:

* Generic audio sample buffer type.
* Buffer read/write helpers.
* Underrun and overrun counters.
* Thread-safe single-producer/single-consumer behavior.

`runtime/` owns:

* Calling machine/SID sample generation.
* Writing generated samples to the shared util audio buffer.
* Handling pause/reset/turbo audio policy at the runtime level.

`machine/` owns:

* SID behavior and sample generation.

`frontend/` may later own:

* Optional user-facing audio enable, mute, volume, or latency controls.

Frontend audio UI is not required for this milestone.

## Dependency Rules

Allowed:

* `platform/` may include SDL headers.
* `platform/` may include `util/` audio buffer headers.
* `runtime/` may include `machine/` and `util/` headers.
* `machine/` may include `util/` headers if needed for plain data types.

Forbidden:

* `runtime/` must not include `platform/` headers.
* `runtime/` must not call SDL.
* `machine/` must not include `platform/`, `frontend/`, `runtime_client`, SDL, or Nuklear headers.
* The SDL audio callback must not call runtime or machine code.
* The SDL audio callback must not inspect SID registers or C64 timing state.

## Audio Buffer

Implement a single-producer/single-consumer audio sample buffer in `util/`.

Recommended file shape:

```text
src/util/audio_buffer.h
src/util/audio_buffer.c
```

Recommended initial API concepts:

```text
struct audio_buffer;

void audio_buffer_init(struct audio_buffer *buffer, ...);
void audio_buffer_reset(struct audio_buffer *buffer);

size_t audio_buffer_write(struct audio_buffer *buffer, const sample_type *samples, size_t count);
size_t audio_buffer_read(struct audio_buffer *buffer, sample_type *samples, size_t count);

uint64_t audio_buffer_underrun_count(const struct audio_buffer *buffer);
uint64_t audio_buffer_overrun_count(const struct audio_buffer *buffer);
size_t audio_buffer_available_read(const struct audio_buffer *buffer);
size_t audio_buffer_available_write(const struct audio_buffer *buffer);
```

The detailed implementation may use atomics or another justified SPSC-safe design. It must not use a blocking lock inside the SDL audio callback.

## Sample Format

Initial policy:

```text
format: signed 16-bit PCM or float32, chosen once in the implementation guide
SID generation: mono
platform output: duplicate mono to stereo at the platform boundary if the host stream is stereo
preferred host rate: 48000 Hz
actual host rate: use the SDL-accepted rate after device open
```

The guide must pick one internal sample type and use it consistently.

Recommended choice:

```text
float32 mono inside util/runtime/machine
float32 or converted SDL format at platform boundary
```

This avoids early clipping during SID mixing and filter approximation.

## SDL Audio Device Setup

Add platform audio setup in `platform/`.

Recommended file shape:

```text
src/platform/platform_audio.h
src/platform/platform_audio.c
```

Responsibilities:

* Initialize the SDL audio subsystem if not already initialized.
* Request a practical default audio format.
* Open the audio device.
* Store the actual accepted rate, format, channels, and callback buffer size.
* Start and stop playback.
* Close the device on shutdown.
* Fill missing samples with silence.

The platform API must expose only host audio setup data and a pointer/reference to the util audio buffer. It must not expose SID or runtime types.

## Runtime-to-Audio Flow

Runtime receives or is given a pointer to the util audio buffer during startup/configuration.

Runtime writes generated SID samples into that buffer while stepping the machine.

Required property:

```text
runtime -> util audio buffer -> platform SDL callback
```

Forbidden property:

```text
runtime -> platform
runtime -> SDL
platform callback -> runtime
platform callback -> machine
```

## Resampling and Timing Policy

Initial policy:

* Generate host-rate samples using a fractional machine-cycle accumulator.
* Use the actual SDL device sample rate after device open.
* Support both PAL and NTSC machine clock rates.
* Keep SID oscillator phase advancement tied to emulated machine time.
* Emit one host sample whenever the accumulator crosses the host-sample boundary.
* Output silence while paused.
* Reset should clear the audio buffer to avoid stale samples.
* Turbo mode may either mute audio or generate only bounded best-effort audio, but the selected behavior must be documented.

The first guide should avoid high-quality resampling complexity. Linear interpolation is optional. Nearest/generated-at-host-rate output is acceptable for the first milestone if pitch is stable and artifacts are bounded.

## Buffering Policy

Initial target:

* Keep latency small but stable.
* Prefer sub-frame batches over whole-frame batches.
* Do not require perfect latency tuning.

Suggested first policy:

```text
runtime write batch: fixed small host-sample batch or raster-line-sized production
buffer capacity: enough for several callback periods
underrun: output silence and increment underrun counter
overrun: drop oldest samples or reject new samples, but choose one and increment overrun counter
```

Recommended overrun policy:

```text
reject new samples when full
```

Reason: it keeps the callback side simple and makes producer overload visible.

## Minimal Smoke Path

Before SID is complete, provide a host-audio smoke path that writes a simple generated tone or deterministic waveform into the util audio buffer.

The smoke path must prove:

* SDL audio device opens.
* Platform callback runs.
* Callback reads from util buffer.
* Missing samples produce silence.
* Runtime or test producer can write samples without including platform headers.
* Audible output reaches the host device.

The smoke path may be compiled as a test mode, debug command, or local diagnostic. It must not become a permanent music playback feature.

## C64MAUDIO Acceptance Criteria

C64MAUDIO is complete when:

* SDL audio device opens through `platform/`.
* Actual audio format/rate/channels are recorded.
* A util audio buffer exists and is used as the only runtime-to-callback sample path.
* Runtime can write samples without depending on `platform/` or SDL.
* SDL callback can read samples without touching runtime or machine.
* Underrun outputs silence and increments a counter.
* Overrun follows the documented policy and increments a counter.
* Pause and reset do not leave unsafe stale audio state.
* PAL and NTSC sample production remain stable.
* A minimal smoke path proves generated samples reach the host audio device.
* Existing boot, video, debugger, keyboard, joystick, PRG, D64, PAL, and NTSC tests still pass.

---

# Guide 2: C64MSID Implementation Guide

## Purpose

Implement the first functional machine-layer SID sufficient for recognizable C64 audio and ordinary-software compatibility.

The first SID must be useful, deterministic, and bounded. It is not a bit-perfect 6581/8580 model.

## Ownership

SID belongs in `machine/`.

Recommended file shape:

```text
src/machine/sid.h
src/machine/sid.c
```

SID must own:

* SID register state.
* Voice oscillator state.
* Noise LFSR state.
* ADSR envelope state.
* Mixer behavior.
* Filter approximation state.
* Deterministic reset behavior.
* CPU-visible SID reads and writes.

SID must not own:

* SDL audio.
* Host audio device state.
* Runtime threading.
* Frontend UI.
* Audio buffer ownership.

## Register Map

Implement CPU-visible SID behavior for `$D400-$D41F`.

Required map:

```text
$D400-$D406 voice 1
$D407-$D40D voice 2
$D40E-$D414 voice 3
$D415-$D417 filter cutoff/resonance/routing
$D418 mode/volume
$D419-$D41A paddle reads or documented fixed/delegated behavior
$D41B voice 3 oscillator read
$D41C voice 3 envelope read
$D41D-$D41F unused/documented behavior
```

The implementation guide must define mirroring behavior according to the existing bus mapping policy.

Unsupported paddle and unused reads may return deterministic documented values for this milestone.

## Voice Model

Implement three voices with the same basic structure:

* 24-bit phase accumulator or equivalent fixed-width oscillator phase.
* 16-bit frequency register.
* 12-bit pulse width register.
* Control register.
* ADSR register pair.
* Envelope generator.
* Current waveform output.
* Noise LFSR state.

Required waveforms:

* Triangle.
* Sawtooth.
* Pulse with variable pulse width.
* Noise.

For this milestone, combined waveform analog quirks are deferred. If multiple waveform bits are set, choose and document a deterministic approximation.

## Control Register Behavior

Implement at minimum:

* Gate.
* Triangle select.
* Saw select.
* Pulse select.
* Noise select.
* Test bit behavior sufficient to silence/reset oscillator state in a documented way.

Sync and ring modulation may be deferred unless the guide chooses to include them in the first implementation. If deferred, the control bits must still have documented safe behavior.

## ADSR Envelope

Implement ADSR envelope behavior for each voice.

Required states:

* Attack.
* Decay.
* Sustain.
* Release.

Required behavior:

* Gate on starts attack.
* Attack rises toward maximum.
* Decay falls toward sustain level.
* Sustain holds while gate remains on.
* Gate off enters release.
* Release falls toward zero.
* Envelope value modulates voice amplitude.
* Reset initializes envelope state deterministically.

The first implementation may use a bounded approximation for SID rate curves. It must be audible, deterministic, and testable.

## Mixer and Global Volume

Mix all enabled voice outputs into a mono sample.

Required behavior:

* Per-voice waveform output is multiplied by envelope amplitude.
* Three voices are summed.
* `$D418` low nibble controls global volume.
* Output is normalized or clipped safely into the selected internal audio sample range.
* Mixer must avoid catastrophic clipping.

External audio input is deferred.

## Voice 3 Reads

Implement compatibility reads:

```text
$D41B: voice 3 oscillator output
$D41C: voice 3 envelope output
```

Required behavior:

* `$D41B` changes as voice 3 oscillator state changes.
* `$D41C` reflects voice 3 envelope state.
* Values are deterministic enough for tests.
* Values are not fixed constants during active voice 3 operation.
* Behavior is useful for ordinary software that reads SID for randomness or detection.

## Filter Approximation

Implement a bounded functional filter approximation.

Required behavior:

* Cutoff register changes affect output.
* Resonance changes affect output.
* Low-pass mode exists.
* Band-pass mode exists.
* High-pass mode exists.
* Combined modes follow a documented approximation.
* Filter routing bits affect which voices enter the filter path if implemented.
* Filter must not become numerically unstable.
* Filter must not clip catastrophically.

Explicitly deferred:

* Exact nonlinear 6581 filter behavior.
* 8580 filter differences.
* Per-chip analog variation.
* Temperature or supply-voltage behavior.

## Sample Generation API

SID should expose a machine-layer API that runtime can drive without platform knowledge.

Recommended API concepts:

```text
void sid_reset(struct sid *sid);
void sid_write(struct sid *sid, uint16_t addr, uint8_t value);
uint8_t sid_read(struct sid *sid, uint16_t addr);
void sid_advance_cycles(struct sid *sid, uint32_t cycles);
float sid_sample(const struct sid *sid);
```

The final API can differ if it fits the existing machine stepping model better. The important rule is that SID behavior remains in `machine/`, while runtime only drives stepping and sample extraction.

## Bus Integration

Connect the existing C64 memory map so CPU reads and writes to `$D400-$D41F` reach SID.

Required behavior:

* Writes update SID registers.
* Reads from `$D41B` and `$D41C` return voice 3 values.
* Reads from unsupported SID registers return documented deterministic behavior.
* Debugger-safe SID state, if exposed, must use copied snapshot data.

## Minimal SID Smoke Tests

Add local tests or diagnostics proving:

* Writes to `$D400-$D41F` reach SID.
* Reset initializes SID deterministically.
* Each voice can produce a non-silent sample stream.
* Frequency register changes affect pitch or phase delta.
* Pulse width changes pulse output.
* ADSR gate on/off changes envelope state.
* `$D41B` changes with voice 3 oscillator state.
* `$D41C` changes with voice 3 envelope state.
* Global volume affects mixed output.
* Filter settings alter output without instability.
* Generated SID samples can be written to the C64MAUDIO buffer and heard through the host audio smoke path.

## C64MSID Acceptance Criteria

C64MSID is complete when:

* `$D400-$D41F` are mapped and documented.
* Three voices exist.
* Triangle, saw, pulse, and noise are recognizable.
* Frequency and pulse width registers affect output.
* ADSR envelope behavior is implemented and testable.
* Gate behavior controls envelope state.
* Voices mix into mono output.
* `$D418` global volume affects output.
* `$D41B` returns useful changing voice 3 oscillator values.
* `$D41C` returns useful changing voice 3 envelope values.
* A bounded functional filter approximation exists.
* Unsupported SID analog details remain explicitly deferred.
* SID samples can flow through the C64MAUDIO host audio path.
* Existing boot, video, debugger, keyboard, joystick, PRG, D64, PAL, and NTSC tests still pass.
* STATUS.md records implemented and deferred SID behavior.

---

# Follow-On Fidelity Scopes After Audio/SID

After C64MAUDIO and C64MSID are scoped and either implemented or ready for implementation, the next small fidelity scopes are:

## 3. C64MCPU_NEW Audit

Goal:

Verify practical 6510 undocumented opcode coverage.

Scope:

* Audit current CPU opcode implementation.
* Audit current CPU tests.
* Identify whether commonly used undocumented opcodes are implemented and tested.
* Do not write an implementation guide if coverage is already sufficient.
* Write a focused implementation guide only if gaps are found.

Out of scope:

* Rewriting the CPU core.
* Exact sub-cycle RDY/AEC behavior.
* Open-bus behavior.
* CPU changes unrelated to practical undocumented opcode coverage.

## 4. C64MVICII_NEW Guide

Goal:

Add NTSC sprite BA timing parity with the existing PAL path.

Scope:

* Identify the existing PAL sprite BA timing table.
* Add the NTSC timing table using the same representation.
* Select PAL or NTSC table through machine video-standard configuration.
* Preserve existing PAL behavior.
* Add tests or diagnostics that distinguish PAL and NTSC sprite BA stalls.

Out of scope:

* VIC-II light pen.
* Last-byte-on-bus behavior.
* NTSC color generation differences.
* Full demo-scene cycle-perfect validation.
* Unrelated sprite rendering rewrites.

## 5. C64MCIA_NEW Verification

Goal:

Reconcile CIA #2 NMI behavior between code, tests, STATUS.md, and older CIA planning documents.

Scope:

* Inspect current CIA #2 interrupt/NMI wiring.
* Inspect relevant tests.
* Compare code behavior against STATUS.md and older CIA plans.
* If implementation is correct and docs are stale, update docs.
* If implementation is wrong or untested, add the minimal fix and tests needed.
* Write an implementation guide only if the verification finds a real behavior gap.

Out of scope:

* CIA Phase I handshake/FLAG work.
* CIA Phase J cycle-level/sub-Phi2 timing.
* Full CIA rewrite.
* IEC serial bus implementation.
