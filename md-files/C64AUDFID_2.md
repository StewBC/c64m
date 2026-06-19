# C64AUDFID_2.md
# SID Functional Audio Implementation Guide

## Component

C64MSID

## Status

Coding-agent-ready implementation guide.

## Purpose

Implement the first functional machine-layer SID for c64m. The result must
produce recognizable C64 audio, support ordinary software that reads SID voice 3
state, and deliver samples through the audio infrastructure from
`C64AUDFID_1.md`.

This guide is functional, not bit-perfect. It intentionally defers exact 6581 and
8580 analog modeling.

## Required Reading Before Coding

Read these in order before editing code:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MAUDIO.md`
5. `C64MSID.md`
6. `C64AUDFID_1.md`
7. This guide

If `C64AUDFID_1.md` is not implemented, implement enough of it first to provide a
working util audio buffer and host audio smoke path.

## Goal

Add a SID component in `machine/` with:

- CPU-visible register map at `$D400-$D41F`.
- Three voices.
- Basic triangle, saw, pulse, and noise output.
- Pulse width control.
- Frequency control.
- ADSR envelope behavior.
- Gate behavior.
- Mixer and global volume.
- Voice 3 oscillator read at `$D41B`.
- Voice 3 envelope read at `$D41C`.
- Initial bounded multimode filter approximation.
- Deterministic reset.
- Sample generation usable by runtime and the audio buffer.

## In Scope

- New SID state in `machine/`.
- SID reset, read, write, step, and sample APIs.
- C64 bus integration for `$D400-$D41F`.
- SID register mirroring according to existing I/O mapping policy.
- Functional three-voice oscillator output.
- Functional ADSR envelope.
- Voice 3 oscillator/envelope reads.
- Functional filter approximation.
- Runtime sample extraction and writing to C64AUDFID_1 audio buffer.
- Tests or diagnostics for register, voice, ADSR, readback, mixer, filter, and
  audio-flow behavior.
- STATUS.md update.

## Explicit Non-Goals

Do not implement these in this phase:

- Bit-perfect MOS 6581 analog behavior.
- MOS 8580 support.
- Runtime SID variant switching.
- Exact nonlinear filter modeling.
- Full combined-waveform analog behavior.
- Per-chip variation.
- Temperature or voltage behavior.
- Sample-perfect digi playback.
- Multi-SID.
- ReSID-level compatibility.
- External audio input accuracy.
- Music playback UI.
- IEC serial bus.
- 1541 emulation.
- Fast loaders.
- D64 writes.
- Cartridge support.
- CIA Phase I or Phase J work.
- VIC-II light pen.
- Open-bus behavior.

## Architecture Contract

SID belongs in `machine/`.

Allowed:

```text
runtime -> machine + util
machine -> util
```

Forbidden:

```text
machine -> runtime
machine -> platform
machine -> frontend
machine -> SDL
platform -> machine
SDL callback -> machine
```

The SDL audio callback must not call SID. Runtime steps the machine and writes
samples to the util audio buffer.

## Files To Add

Add these unless equivalent files already exist:

```text
src/machine/sid.h
src/machine/sid.c
```

## Files To Inspect And Possibly Modify

Inspect actual repo names before editing. Likely areas:

```text
src/machine/c64.h
src/machine/c64.c
src/machine/bus or memory-map files
src/runtime/* audio production integration
src/util/audio_buffer.h
CMakeLists.txt or src/*/CMakeLists.txt
STATUS.md
```

Do not change unrelated VIC, CIA, CPU, loader, or frontend behavior except as
needed to expose copied SID state through an existing snapshot path.

## SID State Model

Use one SID instance in the C64 machine state.

Conceptual structs:

```c
typedef struct sid_voice {
    uint16_t freq;
    uint16_t pulse_width;        /* 12-bit effective */
    uint8_t control;
    uint8_t attack_decay;
    uint8_t sustain_release;

    uint32_t phase;              /* 24-bit effective phase accumulator */
    uint32_t noise_lfsr;         /* deterministic nonzero reset value */

    uint8_t envelope;            /* 0..255 */
    uint8_t envelope_state;      /* attack, decay, sustain, release */
    double envelope_counter;

    float last_wave;
} sid_voice;

typedef struct sid {
    uint8_t regs[0x20];
    sid_voice voices[3];

    uint16_t filter_cutoff;
    uint8_t filter_resonance_route;
    uint8_t mode_volume;

    float filter_lp;
    float filter_bp;
    float filter_hp;

    uint8_t voice3_osc_read;
    uint8_t voice3_env_read;
} sid;
```

The exact fields may differ, but the implementation must preserve the behaviors
specified below.

## Required SID API

Implement a small machine-layer API equivalent to:

```c
void sid_init(sid *s);
void sid_reset(sid *s);
void sid_write(sid *s, uint16_t addr, uint8_t value);
uint8_t sid_read(sid *s, uint16_t addr);
void sid_advance_cycles(sid *s, uint32_t cycles);
float sid_sample(const sid *s);
```

If the existing machine step model prefers `sid_step_one_cycle`, that is allowed,
but runtime must still be able to obtain a mono float sample after stepping.

No SID API may expose SDL types.

## Register Map

Map `$D400-$D41F` as follows:

```text
Voice 1:
$D400 FREQ_LO
$D401 FREQ_HI
$D402 PW_LO
$D403 PW_HI
$D404 CONTROL
$D405 ATTACK_DECAY
$D406 SUSTAIN_RELEASE

Voice 2:
$D407 FREQ_LO
$D408 FREQ_HI
$D409 PW_LO
$D40A PW_HI
$D40B CONTROL
$D40C ATTACK_DECAY
$D40D SUSTAIN_RELEASE

Voice 3:
$D40E FREQ_LO
$D40F FREQ_HI
$D410 PW_LO
$D411 PW_HI
$D412 CONTROL
$D413 ATTACK_DECAY
$D414 SUSTAIN_RELEASE

Filter and volume:
$D415 FC_LO
$D416 FC_HI
$D417 RES_FILT
$D418 MODE_VOL

Reads:
$D419 POTX or deterministic fixed/delegated value
$D41A POTY or deterministic fixed/delegated value
$D41B OSC3
$D41C ENV3
$D41D unused documented value
$D41E unused documented value
$D41F unused documented value
```

Use `addr & 0x1F` after the C64 bus has selected the SID I/O range.

## Register Write Behavior

Writes to voice registers update both `regs[]` and decoded voice fields.

Frequency:

```text
freq = FREQ_LO | (FREQ_HI << 8)
```

Pulse width:

```text
pulse_width = (PW_LO | ((PW_HI & 0x0F) << 8)) & 0x0FFF
```

Control bits:

```text
bit 0: gate
bit 1: sync, optional functional approximation or documented no-op in this phase
bit 2: ring mod, optional functional approximation or documented no-op in this phase
bit 3: test
bit 4: triangle
bit 5: saw
bit 6: pulse
bit 7: noise
```

ADSR:

```text
attack  = high nibble of ATTACK_DECAY
 decay  = low nibble of ATTACK_DECAY
sustain = high nibble of SUSTAIN_RELEASE
release = low nibble of SUSTAIN_RELEASE
```

Filter:

```text
cutoff = ((FC_HI << 3) | (FC_LO & 0x07))   /* 11-bit effective */
resonance = high nibble of RES_FILT
filter routing = low bits of RES_FILT
volume = low nibble of MODE_VOL
filter modes = high bits of MODE_VOL
```

## Register Read Behavior

Reads:

- `$D41B` returns voice 3 oscillator output byte.
- `$D41C` returns voice 3 envelope output byte.
- `$D419` and `$D41A` may return `0xFF` or a project-standard fixed value unless
  paddle input is already modeled elsewhere. Document this in `STATUS.md`.
- Other readable SID registers may return the last written value or a documented
  deterministic value according to existing bus policy.
- Do not implement open-bus last-byte behavior in this phase.

## Oscillator Policy

Use a 24-bit phase accumulator per voice.

On each SID cycle or on batched cycle advancement:

```text
phase += freq * cycles
phase &= 0x00FFFFFF
```

This is a practical functional approximation. If the existing timing model has a
more exact SID phase increment convention, use it and document it.

## Waveform Output

Each voice produces a normalized float in approximately `[-1.0f, +1.0f]` before
envelope multiplication.

Triangle:

```text
phase top bit folds ramp into triangle
output range: -1.0 to +1.0
```

Saw:

```text
phase ramp converted to -1.0 to +1.0
```

Pulse:

```text
compare top 12 bits of phase against 12-bit pulse_width
below threshold -> +1.0
above threshold -> -1.0
```

Clamp pulse width at the edges to avoid permanently stuck invalid math. Hardware
edge quirks are deferred.

Noise:

- Use a deterministic LFSR per voice.
- Reset to a nonzero seed.
- Advance the LFSR from oscillator phase transitions or a documented practical
  approximation tied to phase advancement.
- Output selected high bits normalized to `[-1.0f, +1.0f]`.

Multiple waveform bits:

- For this phase, use deterministic priority or simple averaging.
- Recommended priority: noise, pulse, saw, triangle.
- Document the choice in comments and `STATUS.md`.
- Exact combined waveform analog behavior is deferred.

No waveform selected:

```text
output = 0.0f
```

Test bit:

- When set, force oscillator output to silence and reset or hold phase in a
  documented deterministic way.
- Recommended: set `phase = 0`, set waveform output to `0.0f`, leave envelope
  controlled by gate state.

Sync and ring modulation:

- They are allowed but not required for this phase unless existing docs already
  require them.
- If not implemented, control bits must be stored and documented as functional
  no-ops for now.

## ADSR Envelope

Each voice has envelope states:

```text
ATTACK
DECAY
SUSTAIN
RELEASE
```

Gate behavior:

- Gate rising edge enters ATTACK.
- ATTACK increases envelope toward 255.
- At 255, enter DECAY.
- DECAY decreases envelope toward sustain level.
- SUSTAIN holds while gate remains set.
- Gate falling edge enters RELEASE.
- RELEASE decreases envelope toward 0.

Sustain level:

```text
sustain_level = sustain_nibble * 17
```

Rate policy:

- Use a deterministic table of rates for attack, decay, and release.
- The table may be approximate, but it must make all 16 rate values distinct or
  at least monotonic.
- Do not implement exact SID exponential quirks unless already available.

Envelope sample value:

```text
envelope_gain = envelope / 255.0f
voice_output = waveform * envelope_gain
```

Voice 3 envelope read:

```text
$D41C = voice[2].envelope
```

## Mixer

Mix three voice outputs into mono.

Policy:

```text
mixed = (v1 + v2 + v3) / 3.0f
mixed *= (volume_nibble / 15.0f)
```

If volume is zero, output silence.

Clamp final sample to `[-1.0f, +1.0f]` after filter and volume.

External input is deferred and should behave as zero.

## Filter Approximation

Implement a bounded functional multimode filter.

Minimum behavior:

- `$D415/$D416` cutoff changes affect sound.
- `$D417` resonance changes affect sound.
- `$D418` low-pass bit enables low-pass output.
- `$D418` band-pass bit enables band-pass output.
- `$D418` high-pass bit enables high-pass output.
- Combined modes sum the selected filtered outputs and clamp safely.
- Filter never becomes numerically unstable.

Recommended implementation:

Use a simple state-variable filter with cutoff and resonance clamped to safe
ranges.

Pseudo policy:

```text
normalized_cutoff = clamp(cutoff / 2047.0, min, max)
resonance_factor = safe monotonic value from resonance nibble
lp += f * bp
hp = input - lp - q * bp
bp += f * hp
```

Clamp intermediate states if necessary.

Filter routing:

- If easy, honor low bits of `$D417` so selected voices enter the filtered path.
- If not, route the full mixed signal through the filter and document that per
  voice routing is deferred.
- The guide acceptance requires routing bits only if implemented. Do not silently
  pretend they work.

## Voice 3 Reads

`$D41B` must not be a fixed constant during active voice 3 operation.

Recommended:

```text
$D41B = top 8 bits of voice 3 current waveform or phase-derived oscillator output
```

Good enough options:

- top 8 bits of voice 3 phase;
- current raw waveform converted to 0..255;
- noise output byte when noise is selected.

`$D41C`:

```text
$D41C = voice 3 envelope value 0..255
```

Both reads must be deterministic enough for tests and useful for ordinary
software RNG/detection.

## Bus Integration

Connect CPU-visible reads/writes in the existing C64 memory map:

- `$D400-$D41F` selects SID.
- Mirroring follows existing I/O decode rules.
- Writes call `sid_write`.
- Reads call `sid_read`.
- Debugger-safe memory peeks must not mutate SID state.

If the project already has safe-peek paths, add SID support there. If not,
document the limitation in `STATUS.md` and do not use normal side-effecting reads
for debugger views.

## Runtime Audio Integration

After machine stepping, runtime should emit host-rate samples using the accumulator
from C64AUDFID_1.

Sample source:

```text
float sample = sid_sample(&c64->sid);
audio_buffer_write(audio_out, &sample, 1);
```

Runtime must still not include platform or SDL headers.

If SID stepping occurs every machine cycle, `sid_sample` may simply read current
mixed output. If SID stepping is batched, `sid_advance_cycles` must advance phases
and envelopes enough to keep pitch stable.

## Tests And Diagnostics

Add tests where the project harness supports them.

### Register Tests

Required:

1. Reset initializes registers and decoded fields deterministically.
2. Writes to `$D400-$D41F` reach SID.
3. Voice frequency writes decode correctly.
4. Pulse width writes use only 12 effective bits.
5. Control register gate rising/falling edges change envelope state.
6. `$D41B` changes when voice 3 oscillator advances.
7. `$D41C` changes when voice 3 envelope advances.
8. Unsupported paddle/unused reads return documented values.

### Voice Tests

Required:

1. Triangle voice produces non-silent changing samples.
2. Saw voice produces non-silent changing samples.
3. Pulse voice produces non-silent changing samples.
4. Pulse width affects pulse duty behavior.
5. Noise voice produces deterministic non-constant samples.
6. Frequency changes affect phase delta or observed sample pattern.
7. Test bit silences or resets output according to documented policy.

### ADSR Tests

Required:

1. Gate on causes attack to rise.
2. Attack reaches max and transitions to decay.
3. Decay approaches sustain level.
4. Sustain holds while gate remains on.
5. Gate off causes release to fall toward zero.
6. Reset clears envelope state deterministically.

### Mixer And Filter Tests

Required:

1. Global volume zero mutes output.
2. Global volume nonzero scales output.
3. Multiple voices mix without catastrophic clipping.
4. Cutoff changes alter filtered output.
5. Resonance changes alter filtered output.
6. Low-pass, band-pass, and high-pass modes produce bounded output.

### Audio Flow Smoke

Required:

1. A simple SID register program or diagnostic configures one voice.
2. Runtime writes SID samples to the C64AUDFID_1 util buffer.
3. SDL callback consumes samples.
4. Audible output reaches host device in manual smoke mode.

## STATUS.md Update

After implementation and tests, update `STATUS.md` with:

- SID register map implemented.
- Three voices implemented.
- Waveforms implemented and combined-waveform policy.
- ADSR implemented and rate approximation policy.
- Voice 3 reads implemented.
- Mixer/global volume implemented.
- Filter approximation implemented and limitations.
- Paddle/unused SID read policy.
- Sync/ring-mod status.
- Deferred exact 6581/8580 behavior.
- Audio flow through C64AUDFID_1 verified.

## Acceptance Checklist

This guide is complete only when all items below are true:

- `machine/` owns a SID component.
- SID has no SDL, platform, frontend, or runtime dependency.
- `$D400-$D41F` are mapped through the C64 bus.
- SID reset is deterministic.
- Three voices exist.
- Triangle, saw, pulse, and noise produce recognizable output.
- Frequency registers affect output.
- Pulse width affects pulse output.
- ADSR gate and envelope behavior is implemented and tested.
- Mixer sums voices into mono float output.
- `$D418` global volume affects output.
- `$D41B` returns useful changing voice 3 oscillator data.
- `$D41C` returns useful voice 3 envelope data.
- A bounded functional filter approximation exists.
- Unsupported analog and chip-variant behavior remains explicitly deferred.
- Runtime can write SID samples into the audio buffer without platform dependency.
- Host audio smoke proves SID samples can reach the SDL device.
- Existing boot, video, debugger, keyboard, joystick, PRG, D64, PAL, and NTSC
  tests still pass.
- `STATUS.md` reflects implemented and deferred SID behavior.
