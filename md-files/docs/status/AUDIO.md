# Audio runtime and platform status

## Scope

This file covers the audio transport and scheduling path. SID chip behavior belongs in `SID.md`.

## Current implementation

- Lock-free SPSC ring buffer delivers float mono samples from the runtime thread to the SDL audio callback.
- SDL audio device is managed by `platform/platform_audio`.
- Platform audio opens at 48 kHz stereo float (`AUDIO_F32SYS`) and accepts frequency/channel changes from SDL.
- Internal mono is expanded to actual output channels in the callback.
- Runtime audio scheduling advances after each completed C64 cycle.
- PAL and NTSC machine cycles are converted to host sample deadlines with a fractional cycle accumulator.
- The runtime frame pacer sleeps emulated time to wall-clock at the active standard's real frame rate (PAL ~50.12 fps, NTSC ~59.83 fps), derived from `c64_config_cycles_per_frame()` / `c64_config_clock_hz()` in `runtime_reset_pacer()`. This keeps sample production balanced against the fixed-rate output device on both standards.
- SID mode averages per-cycle `sid_sample()` values that fall within each host-sample interval.
- Recording uses the same emitted samples as playback.
- `--audio-smoke` emits a 440 Hz square wave to validate the transport before SID is involved.
- Turbo mode skips audio writes entirely to prevent buffer flooding while state advances normally.
- Overrun policy rejects excess samples and increments a counter once per write call.
- Underrun policy returns available samples, fills callback silence, and increments a counter once per read call.

## Important invariants

- `runtime/` and `util/` targets remain SDL-free.
- SDL dependency is confined to `platform/`.
- `audio_buffer.c` uses C11 `_Atomic` via a per-file CMake property.
- `audio_buffer` public header remains C99-compatible and opaque.
- Startup order is:
  1. `audio_buffer_create`
  2. `platform_audio_create`
  3. `runtime_create`
  4. `runtime_start`
  5. `platform_audio_start`

## Recent changes

- Fixed PAL-only live-audio distortion. The frame pacer (`runtime_reset_pacer()`)
  was hardcoded to 60 fps (`RUNTIME_TARGET_FPS`), so PAL — which runs ~50.12 fps —
  emulated ~20% faster than wall-clock and produced ~57k samples/sec into the
  48 kHz device. That saturated the SPSC ring buffer, so `audio_buffer_write`
  dropped samples (overrun counter) and the output crackled. NTSC (~59.83 fps)
  matched the fixed 60 and was unaffected. The pacer now derives per-frame wall
  duration from the active standard's real frame period via
  `c64_config_cycles_per_frame()` / `c64_config_clock_hz()`. Verified with a
  fixed-wall-interval production-rate probe: PAL 56.2k → 47.7k samples/sec, NTSC
  48.1k → 47.9k (device 48k). Side effect: PAL also no longer runs ~20% fast in
  wall-clock. Files: `src/runtime/runtime_thread.c`, `src/machine/c64.{c,h}`.
- C64SID_IMP_9 removed `runtime_audio_produce()` and `audio_last_cycle`.
- Runtime audio now advances from the cycle-stepping path through `runtime_audio_advance_cycle()`.
- Host samples are emitted at fractional PAL/NTSC sample deadlines.
- For SID mode, per-cycle SID values are averaged across the host-sample interval, typically 20 or 21 PAL cycles at 48 kHz.
- This eliminated batch-sized identical-sample runs from the former 1024-cycle batch path.

## Current measured result

After the Phase 9 scheduler fix, `el_cartero` improved against `x64sc-20s.mp3`:

- score: 1.3534, better than Phase 8 1.4277 and Phase 7A 1.4070
- correlation: 0.7430
- RMS: c64m 0.0643 vs VICE aligned 0.0670
- spectral-band MAE: 1.7828 dB
- 16-22 kHz excess improved but remained high: +9.92 dB

SID Phase 10 then further improved the audio baseline through SID output conditioning changes. See `SID.md`.

## Known limitations / deferred

- Cycle-perfect audio timing is not complete.
- Remaining high-frequency excess is no longer explained solely by the removed 1024-cycle zero-order-hold batch behavior.
- Further high-frequency work should be done as a measured SID/audio fidelity phase.

## Tests / smoke checks

- Runtime scheduler tests verify sample-count accounting.
- Runtime scheduler tests verify programmed SID output no longer forms batch-sized identical-sample runs.
- `test_c64_vicii` (`test_config_frame_timing`) locks the per-standard clock and cycles-per-frame constants that drive the pacer, so a regression to a fixed frame rate fails a test.
- `--audio-smoke` remains useful for transport validation independent of SID correctness.
- Recording paths should be checked when changing playback scheduling because they share emitted samples.

## Files likely involved

- `src/runtime/runtime_thread.c`
- `src/util/audio_buffer.*`
- `src/platform/platform_audio.*`
- `src/machine/sid.*`
- `tools/capture_sid_audio.py`
- `tools/compare_sid_audio.py`
