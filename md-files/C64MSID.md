# C64MSID.md

# SID Implementation Plan for c64m

## Purpose

This document defines the high-level plan for adding SID audio emulation to
c64m. It is intentionally not a coding-agent-ready phase document. It should be
refined into smaller implementation guides before coding begins.

## Goal

Implement a functional MOS 6581-style SID sufficient for the current PAL/NTSC
fidelity milestone.

The goal is recognizable C64 audio and ordinary software compatibility, not
bit-perfect analog SID reproduction.

Required for the current milestone:

```text
- $D400-$D41F SID register map and mirroring behavior as selected by the bus.
- Three voices.
- Basic triangle, sawtooth, pulse, and noise waveforms.
- Pulse width control.
- ADSR envelope generation.
- Gate behavior.
- Voice frequency control.
- Voice 3 oscillator read at $D41B.
- Voice 3 envelope read at $D41C.
- Global volume.
- Functional filter approximation.
- Deterministic reset behavior.
- Debugger-safe SID snapshot state if exposed.
```

## Scope Boundary

In scope:

```text
- One default SID model, initially 6581-like.
- Functional sound for games, simple demos, BASIC programs, and music routines.
- Enough readable SID behavior for ordinary RNG and detection use.
- Audio sample generation usable by C64MAUDIO.md.
```

Out of scope for this milestone:

```text
- Bit-perfect SID analog behavior.
- Full 6581 vs 8580 variant switching.
- Exact nonlinear filter modeling.
- External audio input accuracy.
- Digi playback perfection.
- Multi-SID configurations.
- ReSID-level compatibility as a first milestone target.
```

## Architecture

SID belongs in `machine/`.

Suggested shape:

```text
machine/sid.h
machine/sid.c
```

SID must not depend on runtime, platform, frontend, SDL, or Nuklear.

Runtime may step the machine and retrieve generated samples, but the SID module
owns SID behavior.

Frontend may display copied SID state only if runtime publishes a snapshot.

## Register Map

The detailed implementation guide must define all visible registers:

```text
$D400-$D406 voice 1
$D407-$D40D voice 2
$D40E-$D414 voice 3
$D415-$D418 filter and volume
$D419-$D41A paddle reads / unused or delegated behavior
$D41B voice 3 oscillator read
$D41C voice 3 envelope read
$D41D-$D41F unused behavior
```

The first implementation may return deterministic fixed values for unsupported
paddle or unused registers if that behavior is documented in STATUS.md.

## Suggested Phase Sequence

### Phase A - Register Shell and Silent Safety

Goal:

```text
Add a SID component with reset, register read/write, debugger-safe snapshot, and
bus integration. It may produce silence.
```

Acceptance direction:

```text
- CPU reads and writes reach SID registers.
- Writes do not crash or corrupt unrelated machine state.
- Reset initializes SID deterministically.
- Existing boot, VIC, CIA, debugger, PRG, and D64 tests still pass.
```

### Phase B - Oscillators and Basic Waveforms

Goal:

```text
Implement three frequency-controlled oscillators with triangle, sawtooth, pulse,
and noise output.
```

Acceptance direction:

```text
- Each voice can produce audible output through a test harness.
- Frequency register changes affect pitch.
- Pulse width affects pulse waveform.
- Multiple voices mix together.
```

### Phase C - ADSR and Gate Behavior

Goal:

```text
Implement ADSR envelope state machines and gate-controlled voice amplitude.
```

Acceptance direction:

```text
- Attack, decay, sustain, and release are audible and testable.
- Gate on/off changes envelope state.
- Voice 3 envelope read returns useful changing values.
```

### Phase D - Readable Voice 3 and Compatibility Reads

Goal:

```text
Make $D41B and $D41C useful for ordinary software that reads SID for randomness
or detection.
```

Acceptance direction:

```text
- $D41B changes with voice 3 oscillator state.
- $D41C reflects voice 3 envelope state.
- Reads are deterministic enough for tests but not fixed constants.
```

### Phase E - Sync, Ring Modulation, and Control Polish

Goal:

```text
Implement hard sync and ring modulation well enough for normal SID music.
```

Acceptance direction:

```text
- Control register bits have documented behavior.
- Common tunes using sync or ring modulation sound recognizably correct.
```

### Phase F - Functional Filter

Goal:

```text
Add a bounded, functional multimode filter approximation.
```

Acceptance direction:

```text
- Cutoff and resonance affect output.
- Low-pass, band-pass, and high-pass modes are present.
- Combined modes follow a documented approximation.
- Filter does not destabilize or clip catastrophically.
```

### Phase G - Validation and Debug Visibility

Goal:

```text
Make SID behavior maintainable.
```

Acceptance direction:

```text
- Local audio diagnostics exist.
- A small set of known tunes or programs are used as smoke tests where licensing
  permits.
- SID snapshot state, if exposed, follows the snapshot rule.
- STATUS.md records implemented and deferred SID behavior.
```

## Audio Output Dependency

SID implementation depends on C64MAUDIO.md for host playback. SID can be tested
without SDL through offline sample generation, but the milestone requires both
SID emulation and host audio output.

## Fidelity Notes

The first SID should be useful, not perfect.

Known rabbit holes to avoid in the first milestone:

```text
- exact 6581 filter nonlinearity;
- exact waveform DAC imperfections;
- full combined waveform analog behavior;
- chip revision differences;
- sample-perfect digi tricks;
- temperature or supply-voltage modeling.
```

If a program needs one of these for compatibility, document it as a later
compatibility target rather than expanding the current phase silently.

## Suggested Detailed Specs To Write Later

```text
1. SID register map and bus integration guide.
2. SID oscillator and waveform guide.
3. SID ADSR guide.
4. SID readable voice 3 guide.
5. SID sync/ring modulation guide.
6. SID functional filter guide.
7. SID validation and snapshot guide.
```
