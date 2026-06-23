# STATUS_CLEAN.md

## Current status

The emulator is complete through:

- Core C64 runtime: 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction step, frame handoff.
- VIC-II through Phase J except skipped light pen: live raster timing, timed bus-visible writes, PAL/NTSC frame sizes, text/bitmap/multicolor/ECM/invalid modes, sprites, sprite priority/collisions, open/unused register reads, PAL/NTSC sprite BA stealing, DEN-off blanking.
- CIA through Phase G: CIA #1/#2 routing, timers, ICR/IRQ/NMI behavior, keyboard/joystick/RESTORE, CIA #2 VIC bank and IEC port pins, TOD/alarm.
- Debugger UI through Phase 13: CPU/registers, memory, disassembly, misc/debugger tabs, execute/read/write breakpoints/watchpoints, counters/actions, INI persistence. Call stack view implemented in Misc|Debugger tab.
- Memory/disassembly view source mode: Map (CPU-visible address space), ROM (physical ROM bytes regardless of mapping), RAM (raw RAM regardless of mapping). Mode is per-view and independent. Right-click contextual popup selects mode; Opt+M keyboard shortcut cycles source mode from the active memory/disassembly view; Opt+Tab cycles active view C64→Disassembly→Misc→Memory and Shift+Opt+Tab reverses it. ROM mode shows an amber border inside the content area; RAM mode shows a blue border; Map has no source-mode color; the active C64, disassembly, misc, or memory view shows a neutral border when no modal dialog is open. The Memory view bottom status row shows active edit field, cursor address, and editable/read-only state.
- Configuration UI through Phase 14: Configure dialog, PAL/NTSC setting, display/turbo/symbol/INI options, runtime config apply and reboot on video-standard change.
- D64 disk support through Phase G: read-only tools parser, runtime mount/unmount for devices 8 and 9, KERNAL LOAD traps for PRG loads, LOAD "$" directory loads, exact/wildcard filename matching, Machine-tab disk UI/status.
- PRG loader polish: reset-before-load, pending injection after BASIC warm-start at $E38B, keyboard-buffer autostart PRGs supported.
- Command-line startup load: `--disk`/`-d <drive>=<image>` now correctly mounts D64 images at startup (was parsed but never applied to runtime); `--prg`/`-p <file>` loads any file as PRG on startup (resets, boots to BASIC, injects at embedded load address, resumes running automatically); `--basic`/`-B <file>` loads any file as a BASIC program on startup (resets, boots, writes at embedded load address, updates TXTTAB/VARTAB at `$2B–$2E`); file extension is irrelevant for both, format is determined by the flag. `--autorun`/`-a` may be combined with any of these three: with `--prg` or `--basic` it buffer-injects `RUN\r` into the KERNAL keyboard buffer immediately after bytes land at `$E38B`; with `--disk 8=…` it uses a two-phase `$E38B` trap — first BASIC READY injects `LOAD"*",8\r`, second READY injects `RUN\r`; both paths use `use_buffer=true` paste so PETSCII is written directly to `$0277–$00C6`.
- Command-line video-standard override: `--video PAL|NTSC`, `-P`/`--pal`, and `-N`/`--ntsc` apply after INI loading and override `[Video] standard` for the current launch; invalid `--video` values fail startup with a clear error.
- Assembler UI integration: Assembler tab, file picker, address/run address, auto-run, reset/run-to-BASIC assembly flow, assembler error event/dialog, symbol snapshot handoff to disassembler.
- Host file load/save UI: unified Load and Save buttons on Machine tab; Load dialog has From File address, Reset, and Basic Program checkboxes; Save dialog has Basic Program checkbox (reads $2B–$2E, forces header), Write address header, and Start/End range fields.
- Help UI Phase 1: build-time `manual/manual.md` to compiled help data, Nuklear help overlay, OPTION+H/ESC toggle, runtime-client pause/resume, fixed heading/footer with scrollable section content.
- Help UI Phase 2: added per-section help scroll memory and keyboard navigation for PageUp/PageDown/Home/End plus Left/Right section switching, while preserving accepted Phase 1 rendering.
- Help UI Phase 3: added compile-time C64-inspired help theme colors while preserving existing help rendering and navigation behavior.
- Help UI Phase 4: added safe long-line wrapping for help text and a generator `--level N` option for selecting which Markdown heading level becomes the bottom-row help section list, defaulting to level 2.
- Help UI Phase 5: embedded C64 Pro Mono TrueType font (compiled-in byte array, no runtime file load) used exclusively for help view text at 10 px via Nuklear font push/pop; extended `help_wrap_text` with character-level hard-wrap fallback so tokens with no spaces never overflow the panel; routed `HELP_SPAN_CODE_BLOCK` through `help_inline_wrap_if_needed` so code lines wrap identically to body text.
- Audio output infrastructure (C64AUDFID_1): lock-free SPSC ring buffer, SDL audio device, PAL/NTSC cycle-to-sample conversion, 440 Hz smoke tone, turbo mute, overrun/underrun counters.
- SID functional audio (C64AUDFID_2): triangle/saw/pulse/noise waveforms, functional combined-waveform approximation, oscillator sync, ring modulation, ADSR envelope, Chamberlin SVF filter, 3-voice mixer, per-voice filter routing, voice 3 read-back, $D400–$D41F register map; deferred: exact analog combined-waveform blending and NTSC tables.
- SID improvement Phase 1 (C64SID_IMP_1): runtime audio recording options (`--audio-record`, `--audio-record-start`, `--audio-record-duration`) capture mono WAV output from the same runtime `sid_sample()` path used for playback; `tools/capture_sid_audio.py` launches `./build/c64m -a -p ./samples/el_cartero.prg` with a 9.5 s default warmup, 4.0 s capture, and timed termination; `tools/compare_sid_audio.py` decodes reference/candidate audio through ffmpeg and emits JSON metrics for RMS, peak, DC offset, crest factor, coarse envelope alignment/correlation, best gain, spectral bands, and a scalar lower-is-better score. Current generated artifacts are local build outputs only, not committed baselines.
- SID improvement Phase 2 (C64SID_IMP_2): final SID output conditioning now applies a deterministic one-pole DC blocker and calibrated output gain in `machine/sid.c` after mix/filter selection. This does not change SID register-visible behavior. `el_cartero` automated capture improved from Phase 1 score 6.99 to Phase 2 score 3.64; candidate RMS moved from 0.3337 to 0.0645 against VICE 0.0646, DC offset from -0.0439 to -0.000066, and peak from 1.2455 to 0.2655 against VICE 0.2676.
- SID improvement Phase 3 (C64SID_IMP_3): `$D417` bits 0..2 now route voices 1..3 through the filter while unrouted voices bypass and mix back after mode selection; `$D417` high-nibble resonance decoding is preserved; `$D418` bit 7 still disconnects voice 3 from both routed and bypass output. Output gain was retuned conservatively to 22% after routing so the `el_cartero` RMS remains aligned with VICE. Phase 3 capture score improved slightly from 3.6395 to 3.6332; RMS 0.0643 vs VICE 0.0646, DC offset 0.000027, peak 0.2798, and spectral-band mean absolute error improved from 2.2807 dB to 2.2076 dB.
- SID improvement Phase 4 (C64SID_IMP_4): oscillator sync (`$D404/$D40B/$D412` bit 1) resets a voice when its source voice wraps using the SID voice relationship 1<-3, 2<-1, 3<-2; ring modulation (bit 2) affects triangle generation from the source voice high bit; combined waveforms now use a deterministic bitwise/AND-style approximation in an unsigned 8-bit waveform domain instead of simple priority selection. The `el_cartero` capture remains bounded and level-centered but the scalar score regressed from 3.6332 to 3.7503; this is accepted because the phase replaces documented placeholder control-bit behavior. Phase 4 metrics: RMS 0.0630 vs VICE 0.0646, DC offset -0.000123, peak 0.2732, spectral-band mean absolute error 2.2646 dB.
- SID improvement Phase 5 (C64SID_IMP_5): filter coefficient mapping is centralized in `sid_filter_cutoff_factor()` with a named denominator of 4608, capping the Chamberlin SVF factor below 0.5 for stability and top-end restraint; added filter regression tests for extreme cutoff/resonance/mode combinations, reset-cleared filter state, and mode-0 audible bypass behavior. `tools/compare_sid_audio.py` now supports `--baseline-metrics` with `--max-score-regression` for local regression lockdown. The `el_cartero` score is unchanged from Phase 4 at 3.7503 because the current 4 s reference window does not measurably exercise the tuned filter path; this is recorded as neutral rather than claimed as fidelity improvement.
- SID improvement Phase 6 (C64SID_IMP_6): decay and release envelope states now apply a pseudo-exponential period multiplier matching the real 6581 behavior — the counter step rate slows as the envelope level drops through breakpoints at 93, 54, 26, 14, and 6 (multipliers 1×, 2×, 4×, 8×, 16×, 30×). Attack remains linear. `sid_exp_period()` is a static helper in `machine/sid.c`; 3 new tests verify the exponential shape and per-level step-rate ratio. The `el_cartero` automated score is 3.757 (flat vs 3.750 baseline) because the dominant score term — envelope correlation 0.157 — reflects a music-position alignment offset between the two captures rather than ADSR shape; this is accepted as the correct 6581 behavior and the change is kept.
- SID improvement Phase 7 (C64SID_IMP_7): `sid_filter_cutoff_factor()` is replaced by a 32-entry compile-time LUT with linear interpolation, mapping the 11-bit cutoff register [0..2047] to Chamberlin SVF coefficients spanning approximately 200 Hz to 18 000 Hz exponentially (previously the range was ~34 Hz to ~70 000 Hz). The function is now non-static and declared in `sid.h` for unit testing. Output gain retuned from 22% to 19% to restore RMS alignment with VICE after the filter-range change. 5 new tests verify coefficient range bounds, monotonicity, and stability. `el_cartero` score improved to 3.7492 (from 3.7503 Phase 5 baseline); RMS 0.0661 vs VICE 0.0646, spectral-band MAE 2.2438 dB (was 2.2646), 16–22 kHz excess reduced from +10.1 dB to +9.4 dB. **Superseded by Phase 7A re-baseline; see below.**
- SID improvement Phase 7A (C64SID_IMP_7A): Lag search widened from ±1 s (100 blocks) to ±4 s (400 blocks) in `tools/compare_sid_audio.py` (`DEFAULT_MAX_LAG_BLOCKS` 100 → 400). `tools/capture_sid_audio.py` updated: warmup 9.5 → 9.31 s, duration 4.0 → 10.0 s, explicit `--pal` flag passed to c64m so the capture uses PAL timing (985248 Hz) matching the VICE x64sc reference. Root cause of the stuck 0.157 correlation across Phases 5–7 was that c64m was running at NTSC timing by default; switching to PAL corrected all note-length and tempo timing. Reference changed from `x65sc.mp3` (4 s) to `x64sc-20s.mp3` (20 s). New baseline: **correlation 0.7324, score 1.4070**, lag_blocks −26 (−0.26 s, MP3 encoder silence), 1000 blocks (10 s) overlap, RMS c64m 0.0653 vs VICE 0.0668, spectral-band MAE 1.96 dB. Lag and correlation stable across consecutive runs (0-block variation, within ±2 tolerance). Old score baseline of 3.75 is superseded.
- SID improvement Phase 8 (C64SID_IMP_8): Added one-pole IIR low-pass filter in `sid_condition_output()` to model the MOS 6581 output-pin capacitive rolloff (~16.5 kHz, a = 0.895, applied after DC blocker before gain). `float hfroll_state` added to `struct sid`; zeroed by existing `memset` in `sid_init/sid_reset`. 5 new unit tests (60 total): reset clears state, HF attenuated vs wideband signal, low-frequency content passes ≥ 80%, silence in → silence out, output bounded ±1.0. Score 1.4277 vs Phase 7A 1.4070 (slight regression accepted — see note). HF band excess: 16–22 kHz c64m −28.00 dB vs VICE −39.09 dB, +11.09 dB excess unchanged despite coefficient tuning because the excess originates from zero-order-hold upsampling in the batch audio loop (`runtime_audio_produce` emits ~49 identical copies of the last `sid_sample()` per 1024-cycle batch, effective SID sampling rate ≈ 962 Hz), not from SID waveform content above 16 kHz. The IIR runs before the ZOH and therefore cannot attenuate its spectral images. HF excess fix deferred to a future phase that improves the audio production resampling path.
- SID improvement Phase 9 (C64SID_IMP_9): Runtime audio production now advances from the cycle-stepping path instead of once per 1024-cycle run batch. `runtime_audio_produce()` and `audio_last_cycle` were removed; `runtime_audio_advance_cycle()` emits host samples at fractional PAL/NTSC sample deadlines and, for SID mode, averages the per-cycle `sid_sample()` values accumulated during each host-sample interval (typically 20 or 21 PAL cycles at 48 kHz). `--audio-smoke` still emits at host sample deadlines, turbo mute discards pending audio timing/averages, and recording uses the same emitted samples as playback. 2 runtime scheduler tests verify sample-count accounting and that programmed SID output no longer forms batch-sized identical-sample runs; all 27 tests pass. `el_cartero` Phase 9 metrics against `x64sc-20s.mp3`: score **1.3534** (better than Phase 8 1.4277 and Phase 7A 1.4070), correlation 0.7430, RMS 0.0643 vs VICE aligned 0.0670, spectral-band MAE 1.7828 dB. The 16–22 kHz band improved but remains high: c64m −29.17 dB vs VICE −39.09 dB, +9.92 dB excess (was +11.09 dB in Phase 8). Remaining HF excess is therefore no longer explained solely by the 1024-cycle ZOH; likely contributors include SID waveform harmonic/alias content and the deliberately gentle one-pole output rolloff. Further HF work should be a new measured SID/audio fidelity phase, not another batch-scheduler fix.
- Memory view virtual views: the memory panel supports up to 16 independent virtual views stacked vertically. Opt+V splits the active view at the cursor (Shift+Opt+V row-aligned); Opt+J dissolves the active view (no-op on the last view); Opt+Up/Down navigate between views. Each view has its own cursor, scroll, source mode (Map/ROM/RAM), and edit state. Row height is distributed proportionally; each view gets at least one row. A 16-slot color palette assigns a unique background per view; slots are freed on dissolve and reused. Click activates a view; mouse wheel scrolls the hovered view. ROM/RAM borders are inset per view; the active-panel selection border spans the whole panel. The scrollbar tracks the active view.
- C64MENH Phase 1 CIA #2 NMI reconciliation: current code and tests confirm CIA #1 interrupt output routes to CPU IRQ, CIA #2 enabled-pending interrupt output routes to the CPU NMI callback through an edge latch, RESTORE remains a separate one-shot NMI source, CPU NMI sampling occurs at instruction entry before IRQ, normal CIA ICR reads clear reported flags, and debugger-safe CIA peeks do not clear ICR/TOD state. The older `C64MCIA.md` current-state text was updated to remove the stale claim that CIA #2 NMI was not wired.
- C64MENH Phase 2 NTSC sprite BA timing: VIC-II sprite BA now selects a PAL 6569 or NTSC 6567R8 BA-assert table from the machine video standard. PAL sprite BA tests still cover existing single, adjacent, split-window, cross-line, inactive, and unified BA-predicate behavior; NTSC tests now cover the 65-cycle late sprite window and sprite 4 cross-line window. AEC remains intentionally unmodeled; CPU stalling still consumes the unified BA predicate.
- C64MENH Phase 3 6510 undocumented opcode audit: `src/machine/c6510.c` has explicit dispatch for all 256 opcode slots, adapted from the a2m cycle-accurate NMOS 6502 core. Practical undocumented execution families are implemented in `c6510_inln.h`: SLO, RLA, SRE, RRA, SAX, LAX, DCP, ISC/ISB, unofficial NOP variants, alternate `SBC #$EB`, ANC, ALR, ARR, AXS/SBX, LAS, AHX/SHA, SHX, SHY, TAS/SHS, XAA/ANE, unstable `LAX #imm`, and JAM/KIL. The C64 wrapper runs all CPU reads/writes through the machine bus and BA stalls use traced read/write events, so undocumented RMW/store opcodes follow the same integration path as official opcodes. No local Harte corpus or harness is present in this repo; local tests cover documented CPU execution, bus integration, trace timing, IRQ/NMI entry, banking, and BA read/write stalling rather than per-opcode undocumented semantics. Practical undocumented opcode coverage is sufficient for the current milestone; no CPU behavior change was made.

## Optimizations

- Accepted: `ca16212` removed successful per-cycle error formatting from `c64_step_cycle`; hot-loop +26.9%, tests passed.
- Accepted: `12ac7b7` moved runtime completed-frame publish buffer off stack; fixed optimized-build bus error, tests passed.
- Accepted: `b0a6bc9` skipped VIC sprite composition when `$D015 == 0`; hot-loop +41.6%, tests passed.
- Accepted: `e05c2dc` cached VIC bank base from CIA2 port state; hot-loop +13.4%, tests passed.
- Accepted: `72ad283` skipped SID mixing/filter/sample output only when audio is explicitly disabled; SID still runs during normal audio playback and turbo multipliers; hot-loop +5.1%, tests passed.
- Rejected: VIC background lazy color/base computation; measured speedup was within noise, reverted.
- Accepted: `8efc9f5` gated CPU debug trace copies while preserving pending bus-event timing; hot-loop +10.4%, tests passed.

## Important implemented details

### VIC-II

- Machine owns monotonic master cycle; VIC/CIA/SID hooks advance to timestamped CPU bus events before visible side effects.
- Live frame publication uses completed live VIC-II frame buffers; snapshot renderer remains only as fallback/debug before a live frame exists.
- Bad Line BA and sprite-fetch BA both stall CPU reads using CPU event read/write classification; writes continue where allowed. Sprite-fetch BA uses per-standard PAL 6569 and NTSC 6567R8 tables selected through machine video configuration.
- AEC is intentionally not modeled as emulator state; BA is the stall predicate.
- Sprite system supports 8 sprites, X/Y position, X/Y expansion, multicolor, bank-aware sprite pointer/data fetch, priority, collisions, and IRQs.
- VIC memory reads are bank-aware via CIA #2 port A; char ROM is visible only in VIC banks 0 and 2 at the normal ranges.
- `$D011` DEN=0 blanks visible display/border color to `$D021` while preserving sprite visibility and collision behavior.

### CIA

- CPU-visible CIA reads have side effects; debugger-safe reads avoid side effects.
- Timer A/B use project-level cycle countdown semantics, separate latch/live counters, force-load strobe, one-shot/continuous modes, CNT and cascade sources, PB6/PB7 output behavior.
- ICR masks and flags are separate; normal reads clear reported flags; debugger peeks do not.
- CIA #1 drives IRQ; CIA #2 drives NMI edge latch.
- CIA #1 handles bidirectional keyboard matrix, joystick ports, and RESTORE isolation.
- CIA #2 handles VIC bank selection and IEC ATN/CLK/DATA open-collector line modeling.
- TOD uses BCD tenths/seconds/minutes/hours, 12-hour AM/PM, 50/60 Hz source policy, coherent read latch, alarm ICR source.

### D64

- Parser supports standard 35-track D64s and common appended error-info bytes.
- Parses BAM metadata, directory chain, raw PETSCII names, ASCII debug names, PRG file chains, and PRG load address.
- Devices 8 and 9 can mount independent read-only images; runtime/frontend exchange copied status only.
- LOAD supports device 8/9 PRG exact names, `*`, prefix wildcards, `?`, and LOAD "$" directory synthesis.
- Failure paths preserve unrelated memory for no disk, missing file, unsupported type/mode, malformed chains, loops, out-of-range sectors, and target overflow.

### Dialog modal input exclusivity

- When any dialog (Configure, Breakpoint Editor, Load, Save, Assembly Errors) is open, all `SDL_MOUSEBUTTONDOWN` events whose coordinates fall outside every open dialog's current bounds are swallowed before reaching Nuklear, preventing base views (Commodore Display, CPU, Disassembly, Memory, Misc) from being brought forward or gaining input focus.
- Mouse motion and button-up events always pass through so dialog title-bar dragging continues to work.
- Custom input code (layout splitter drag, scrollbar detection, view active-flag, mouse-wheel scroll) is additionally gated by `frontend_any_dialog_open` so none of it fires while a dialog is open.
- Dialog bounds are queried each frame via `nk_window_find` using the window name strings registered at `nk_begin`.

### Debugger / UI / config

- Runtime owns machine state, breakpoints, watchpoints, stop reason, counters, and actions.
- Frontend renders copied snapshots only and sends intents/commands to runtime.
- Register and memory edits apply only while paused; running edits are ignored.
- Debugger input focus is explicit: C64 display vs debugger views.
- Symbol table is tools/frontend/debug-session-owned, separate from emulator machine and assembler internals.
- INI supports config and breakpoint persistence; invalid breakpoint entries are skipped while valid entries load.
- Call stack view (Misc|Debugger tab): runtime walks the 6510 stack each frame, verifies JSR opcode at each candidate return address via the CPU memory map, and publishes up to 16 entries as a `runtime_call_stack_snapshot`. Displays `XXXX | JSR label/YYYY` rows; clicking either column centers the disassembly view on that address.
- Hardware view (Misc|Hardware tab): collapsible `NK_TREE_TAB` sections render copied runtime snapshots for Memory/Banks, VIC-II, CIA #1/#2, SID, and counters. Memory/Banks shows CPU port banking, CPU-visible regions, VIC bank, and `$D018`-derived bases; VIC-II shows raster/IRQ/register/color/BA/sprite state; CIA shows ports, timers, ICR, TOD, and alarm; SID shows voice, filter, read-back, and sample state. The shared hardware rows use a wider static layout so the tab can scroll horizontally for long diagnostics.
- Memory/disassembly view source mode: three independent per-view modes — Map (CPU address space via `c64_debug_read_cpu_map`), ROM (physical ROM bytes via `c64_debug_read_rom`, regardless of current mapping), RAM (raw RAM via `c64_debug_read_ram`, regardless of ROM overlay). Mode is tracked per view (disassembly and memory view are independent, future sub-views will also be independent). `RUNTIME_MEMORY_MODE_ROM` added to the runtime enum; both `RUNTIME_EVENT_MEMORY_RESPONSE` and `RUNTIME_EVENT_MEMORY_VIEW_RESPONSE` paths dispatch accordingly. UI: right-click contextual popup with active-mode dot indicator; Opt+M shortcut cycles source mode in the focused memory/disassembly view; Opt+Tab cycles active view C64→Disassembly→Misc→Memory and Shift+Opt+Tab reverses it. Visual indicators are drawn inside the content region via `nk_stroke_rect`: amber (`rgb(200,130,40)`) in ROM mode, blue (`rgb(60,120,200)`) in RAM mode, no source-mode color for Map, and neutral gray (`rgb(188,198,190)`) for the active C64, disassembly, misc, or memory view when no modal dialog is open. Memory view has a visible bottom status row for active edit field, cursor address, and editability.

### Audio output infrastructure (C64AUDFID_1)

- Lock-free SPSC ring buffer (`util/audio_buffer`) delivers float mono samples from the runtime thread to the SDL audio callback without blocking either side.
- SDL audio device managed by `platform/platform_audio`: opens at 48 kHz stereo float (`AUDIO_F32SYS`), accepts frequency/channel changes from SDL; expands internal mono to actual output channels in the callback.
- Runtime thread advances audio scheduling after each completed C64 cycle: a fractional cycle accumulator converts PAL (985248 Hz) or NTSC (1022727 Hz) machine cycles to host sample rate; SID mode averages the per-cycle `sid_sample()` values that fall in each host-sample interval before writing one float sample to the audio buffer/recorder.
- Overrun policy: reject excess samples, increment counter once per write call.
- Underrun policy: return available samples, callback fills silence, increment counter once per read call.
- Turbo (RUNTIME_SPEED_MODE_FAST): audio writes are skipped entirely to prevent buffer flooding; state advances normally.
- `--audio-smoke` CLI flag emits a 440 Hz square wave (±0.2f, phase accumulator, no math.h) to prove the path before SID is wired.
- Startup order: `audio_buffer_create` → `platform_audio_create` (calls `SDL_InitSubSystem(SDL_INIT_AUDIO)`) → `runtime_create` (receives buffer and actual rate) → `runtime_start` → `platform_audio_start`.
- SDL audio dependency is confined to `platform/`; `runtime/` and `util/` targets remain SDL-free.
- `audio_buffer.c` uses C11 `_Atomic` via a per-file CMake property; the public header is C99-compatible (fully opaque struct).

### SID functional audio (C64AUDFID_2)

- MOS 6581 SID emulation in `machine/sid.h` / `machine/sid.c`; attached to the bus at $D400–$D41F via `c64_bus_attach_sid`.
- Register map: voices at $D400–$D406 (v1), $D407–$D40D (v2), $D40E–$D414 (v3); filter at $D415–$D418; reads at $D419–$D41F.
- Waveforms: triangle (24-bit phase fold), sawtooth (linear ramp), pulse (12-bit PW threshold), noise (23-bit LFSR, taps 22/17, clocked on phase bit-19 low→high). TEST bit freezes phase and silences output.
- Combined waveforms: selected triangle/saw/pulse/noise outputs are combined with a deterministic bitwise/AND-style approximation in an unsigned 8-bit waveform domain; exact analog 6581/8580 blending remains deferred.
- Oscillator sync and ring modulation: sync resets a voice when its source voice wraps (1<-3, 2<-1, 3<-2); ring modulation affects triangle output from the source voice high bit.
- 23-bit LFSR output mapped via documented bit positions (20,18,14,11,9,5,2,0).
- ADSR envelope: fractional double accumulator; attack and decay/release rate tables at PAL 985248 Hz; sustain level = nibble × 17.
- Mixer: each voice scaled by envelope/255, summed, divided by 3 (anti-clip), multiplied by volume/15 (`$D418` bits 0–3), clamped to [-1, +1].
- Voice 3 disconnect: `$D418` bit 7 removes voice 3 from mix.
- State-variable Chamberlin filter (per-cycle): `$D417` bits 0..2 route voices 1..3 into the filter while unrouted voices bypass and mix back after mode selection; f = (cutoff+1)/4608 via `sid_filter_cutoff_factor()` to keep the coefficient below 0.5; q = 1 – res/20 (clamped 0.1..1.0); HP/BP/LP computed in that order; filter states clamped to [-2, +2]. Mode selected by `$D418` bits 4–6 (LP/BP/HP); no mode bits → audible output bypasses the filter.
- Voice 3 read-back: `$D41B` = phase bits 23..16; `$D41C` = current envelope byte.
- Paddle reads (`$D419`, `$D41A`) return 0xFF (not connected).
- `sid_sample()` is a trivially const read of `last_sample`; the SDL audio callback can call it safely with no machine pointers.
- `runtime_thread.c` reads `sid_sample(&rt->machine.sid)` from the cycle-stepping audio scheduler and averages per-cycle SID values across each host-sample interval before writing playback/recording samples.
- 60 tests in `tests/machine/test_sid.c` (register, voice, sync/ring/combined waveform behavior, ADSR, exponential ADSR shape, mixer/filter/routing, filter cutoff LUT range, filter regression, output conditioning, output HF rolloff, audio-flow smoke); all pass.

#### SID deferred items

- Exact 6581/8580 analog waveform blending (combined waveforms produce hardware-specific shapes, not bitwise OR).
- Paddle/potentiometer (`$D419`, `$D41C`) — policy: 0xFF until connected input is emulated.
- NTSC rate tables (current tables are PAL 985248 Hz only).

## Not implemented / deferred

- Full CIA accuracy and pin/race-level timing.
- Cycle-perfect video/audio timing.
- VIC-II light pen (`$D013/$D014` stubbed; Phase F skipped).
- Last-byte-on-bus open-bus behavior; unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access fetch behavior from `$3FFF` / `$39FF` in renderer.
- Exact RDY/AEC sub-cycle CPU pin timing.
- Perfect chip-revision/electrical behavior for unstable undocumented opcodes; the CPU has practical implementations, but not last-byte-on-bus or analog-dependent perfection. The debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather than illegal-opcode mnemonics.
- D64 writes, SAVE to disk, error channel, 1541 CPU/ROM emulation, IEC timing/protocol, fast loaders, devices beyond 8/9, full Commodore DOS pattern/type suffix semantics.
- Phase 13 deferred breakpoint actions: Type, Swap, and trace output/details.

## Runtime note

Real 64C ROM execution reaches BASIC READY with visible cursor and keyboard input.

Known smoke-trace observation after 1,000,000 cycles:

```text
PC=$FD7E
CIA #1 IRQ pending = true
CPU IRQ entries = 0
CIA #1 ICR reads = 0
```

This is expected in that trace because the CPU interrupt-disable flag remains set, so the pending CIA #1 IRQ is not entered.

### Host file load/save

- Machine tab layout: Disks ([8]/[9] mount + Eject), Programs ([Load]/[Save]), Emulator ([Configure...]/[Reset]).
- Load dialog: Name + Browse (no type filter); From File checkbox reads 2-byte address header (default on), manual hex field active when unchecked; Reset checkbox resets machine and waits for $E38B before injecting (default off); Basic Program checkbox fixes TXTTAB ($2B/$2C) and VARTAB ($2D/$2E) after load (default off).
- Save dialog: Name + Browse; Basic Program checkbox reads $2B/$2C (start) and $2D/$2E (end, exclusive) at save time and forces Write address header on (default off); Write address header checkbox (default on); Start/End hex fields are read-only when Basic Program is checked.
- Assembler tab: Reset checkbox above Assemble button (default on); when unchecked, assembles directly into live RAM in any exec state — if Auto Run is set, jumps to run address and resumes running.

## Human smoke still useful

- GUI D64 picker: mount a D64, type BASIC LOAD commands, confirm directory and PRG loads.
- Reset/PRG loader: verify reset-before-load and autostart collection PRGs.
- Autorun: `--prg foo.prg --autorun` should boot and immediately run; `--basic foo.bas --autorun` same; `--disk 8=game.d64 --autorun` should type `LOAD"*",8` and `RUN` automatically.
- Assembler tab: assemble with Reset on (existing flow) and Reset off (live inject + auto-run); error dialog; symbol display in disassembly.
- Host load/save: Load with file-address header; Load with Basic Program (check $2B–$2E updated); Save as Basic Program and reload; Save raw range with and without header; verify Eject button and Machine tab section order.
