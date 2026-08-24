# SID and audio

## SID

`src/machine/sid.{c,h}`; bus attach in `c64_bus.c`. Tests:
`tests/machine/test_sid.c`.

Functional MOS 6581-style model: three voices, triangle/saw/pulse/noise,
deterministic combined-wave approximation, sync, ring modulation, ADSR,
filter routing, state-variable filter, voice mixer, voice-3 phase/envelope
readback, `$D400-$D41F`. Rate tables, cutoff LUT, and HF rolloff come from
the PAL/NTSC CPU clock passed to `sid_init()`.

Not bit-perfect analog 6581. No runtime 8580 switch. Paddle reads
(`$D419/$D41A`) return `$FF`. Audio changes must be measured with
`tools/capture_sid_audio.py` / `tools/compare_sid_audio.py`, not judged
only by listening.

## Host path

Runtime advances SID after each completed C64 cycle. Fractional deadlines
convert PAL/NTSC clocks to host samples; SID values in each host interval
are averaged.

`util/audio_buffer` is an SPSC float mono ring. `platform/platform_audio`
owns the SDL device and expands mono to the obtained output channels. The
SDL callback only reads the buffer.

Recording consumes emitted runtime samples. `--audio-smoke` emits a 440 Hz
square wave independent of SID. Max/warp suppress host audio writes unless
`--audio-record`; machine SID state still advances. Inspector sealed replay
suppresses host audio.

Preserve sample-count accounting, PAL/NTSC pacing, non-batched SID output,
recording, and `audio_buffer` tests. PAL pace uses
`cycles_per_frame / clock_hz` (a fixed 60 fps clock ran PAL about 20% fast).
