# SID status

## Current implementation

- MOS 6581 SID emulation lives in `machine/sid.h` and `machine/sid.c`.
- SID is attached to the bus at `$D400-$D41F` through `c64_bus_attach_sid`.
- Register map:
  - Voice 1: `$D400-$D406`
  - Voice 2: `$D407-$D40D`
  - Voice 3: `$D40E-$D414`
  - Filter: `$D415-$D418`
  - Reads: `$D419-$D41F`
- Triangle, sawtooth, pulse, and noise waveforms are implemented.
- Functional combined-waveform approximation is implemented.
- Oscillator sync and ring modulation are implemented.
- ADSR envelope is implemented, including pseudo-exponential decay/release behavior.
- Chamberlin state-variable filter is implemented.
- Three-voice mixer is implemented.
- Per-voice filter routing is implemented.
- Voice 3 read-back is implemented.
- `$D400-$D41F` register map is implemented.

## Important invariants

- TEST bit freezes phase and silences output.
- Noise uses a 23-bit LFSR with taps 22/17, clocked on phase bit-19 low-to-high.
- 23-bit LFSR output maps documented bit positions 20, 18, 14, 11, 9, 5, 2, and 0.
- Combined waveforms use a deterministic bitwise/AND-style approximation in an unsigned 8-bit waveform domain.
- Exact analog 6581/8580 combined-waveform blending remains deferred.
- Sync resets a voice when its source voice wraps, using the relationship 1<-3, 2<-1, 3<-2.
- Ring modulation affects triangle output from the source voice high bit.
- ADSR uses a fractional double accumulator.
- Attack remains linear.
- Decay and release slow as envelope level drops through breakpoints 93, 54, 26, 14, and 6 with multipliers 1x, 2x, 4x, 8x, 16x, and 30x.
- Sustain level is nibble * 17.
- Mixer scales each voice by envelope/255, sums, divides by 3, multiplies by `$D418` volume, and clamps to [-1, +1].
- `$D418` bit 7 disconnects voice 3 from the mix.
- `$D417` bits 0..2 route voices 1..3 through the filter; unrouted voices bypass and mix back after mode selection.
- `$D417` high nibble is resonance.
- `$D418` bits 4..6 select LP/BP/HP. No mode bits means audible bypass.
- Filter state is clamped to [-2, +2].
- `$D41B` reads voice 3 phase bits 23..16.
- `$D41C` reads current voice 3 envelope byte.
- Paddle reads `$D419` and `$D41A` return 0xFF until connected input is emulated.
- `sid_sample()` is a const read of `last_sample`.

## Current fidelity baseline

SID Phase 10 is the current measured baseline:

- `SID_HFROLL_COEFF` tightened from 0.895 to 0.940.
- The model now represents combined 6581 chip-plus-board output-path rolloff rather than only the chip output pin.
- Output gain was retuned from 19% to 20%.
- No struct or API changes were made.
- `el_cartero` Phase 10 metrics against `x64sc-20s.mp3`:
  - score: 1.2838
  - correlation: 0.7372
  - RMS: c64m 0.0674 vs VICE aligned 0.0670
  - spectral-band MAE: 1.4512 dB
  - 16-22 kHz excess reduced from +9.92 dB to +6.34 dB
  - 4-8 kHz band became slightly darker: c64m -19.74 dB vs VICE -18.34 dB
- Results were stable across repeated runs with 0-block lag variation.
- Correlation slight regression from 0.7430 to 0.7372 was considered normal run-to-run/music-position alignment noise.

## Phase history summary

- C64AUDFID_2: functional SID audio implemented.
- C64SID_IMP_1: recording options and audio comparison tooling added.
- C64SID_IMP_2: deterministic DC blocker and calibrated output gain added.
- C64SID_IMP_3: per-voice filter routing added.
- C64SID_IMP_4: sync, ring modulation, and combined-waveform behavior improved.
- C64SID_IMP_5: filter coefficient mapping centralized and regression tests added.
- C64SID_IMP_6: pseudo-exponential decay/release envelope behavior added.
- C64SID_IMP_7: cutoff LUT added, but superseded by Phase 7A re-baseline.
- C64SID_IMP_7A: lag search widened, PAL capture fixed, 10 s reference baseline established.
- C64SID_IMP_8: one-pole IIR low-pass output rolloff added.
- C64SID_IMP_9: audio scheduling moved from 1024-cycle batch production to cycle-stepped sample deadlines.
- C64SID_IMP_10: output rolloff tightened and gain retuned.

## Known limitations / deferred

- Exact 6581/8580 analog waveform blending is deferred.
- Paddle/potentiometer behavior is not connected; current policy is 0xFF.
- NTSC SID rate tables are deferred; current ADSR tables are PAL 985248 Hz only.
- Remaining high-frequency excess likely comes from SID waveform harmonic/alias content and the intentionally gentle output rolloff.
- Further high-frequency work should be a new measured SID/audio fidelity phase.

## Tests / smoke checks

- `tests/machine/test_sid.c` currently contains 60 tests covering registers, voices, sync/ring/combined waveform behavior, ADSR, exponential ADSR shape, mixer/filter/routing, filter cutoff LUT range, filter regression, output conditioning, output HF rolloff, and audio-flow smoke.
- Keep measured audio changes tied to `tools/capture_sid_audio.py` and `tools/compare_sid_audio.py`.
- Avoid claiming fidelity improvement unless the metrics support it.

## Files likely involved

- `src/machine/sid.c`
- `src/machine/sid.h`
- `src/machine/c64*`
- `src/runtime/runtime_thread.c`
- `tests/machine/test_sid.c`
- `tools/capture_sid_audio.py`
- `tools/compare_sid_audio.py`
