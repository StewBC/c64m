# SID and audio handoff

## SID source of truth

Implementation: `src/machine/sid.{c,h}`; bus attachment is in `c64_bus.c`.
Tests: `tests/machine/test_sid.c`.

The model is a functional MOS 6581-style SID with three voices, triangle/saw/pulse/
noise, deterministic combined-wave approximation, sync, ring modulation, ADSR,
filter routing, state-variable filter, voice mixer, voice-3 phase/envelope readback,
and `$D400-$D41F` register mapping. Paddle reads return `0xFF` until input is
implemented. SID rate tables, cutoff LUT, and HF rolloff are selected from the PAL/
NTSC CPU clock passed to `sid_init()`.

Current measured baseline is SID Phase 10. The model is not bit-perfect analog 6581,
does not support runtime 8580 switching, and leaves exact combined-waveform blending
and paddles deferred. Audio changes must be measured with the capture/compare tools,
not judged only by listening.

## Runtime/platform audio

- `runtime/` advances SID/audio scheduling after each completed C64 cycle.
- Fractional cycle deadlines convert PAL/NTSC clocks to host samples; SID values in
  each host interval are averaged.
- `util/audio_buffer` is an SPSC float mono ring buffer. `platform/platform_audio`
  owns the SDL device and expands mono to the obtained output channels.
- Recording consumes the emitted runtime samples. `--audio-smoke` emits a 440 Hz
  square wave independent of SID. Turbo suppresses audio writes but continues
  machine state advancement.
- The SDL callback only reads the buffer. Runtime/util remain SDL-free.

## Limits and checks

Audio is not cycle-perfect and residual high-frequency error remains. Preserve
sample-count accounting, PAL/NTSC pacing, non-batched SID output, recording, and
audio-buffer tests when changing this area.
