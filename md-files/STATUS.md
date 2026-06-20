# STATUS_CLEAN.md

## Current status

The emulator is complete through:

- Core C64 runtime: 6510 CPU, RAM/ROM/banking/address decode, reset/boot path, runtime command/event model, run/pause/reset, cycle/instruction step, frame handoff.
- VIC-II through Phase J except skipped light pen: live raster timing, timed bus-visible writes, PAL/NTSC frame sizes, text/bitmap/multicolor/ECM/invalid modes, sprites, sprite priority/collisions, open/unused register reads, sprite BA stealing, DEN-off blanking.
- CIA through Phase G: CIA #1/#2 routing, timers, ICR/IRQ/NMI behavior, keyboard/joystick/RESTORE, CIA #2 VIC bank and IEC port pins, TOD/alarm.
- Debugger UI through Phase 13: CPU/registers, memory, disassembly, misc/debugger tabs, execute/read/write breakpoints/watchpoints, counters/actions, INI persistence. Call stack view implemented in Misc|Debugger tab.
- Configuration UI through Phase 14: Configure dialog, PAL/NTSC setting, display/turbo/symbol/INI options, runtime config apply and reboot on video-standard change.
- D64 disk support through Phase G: read-only tools parser, runtime mount/unmount for devices 8 and 9, KERNAL LOAD traps for PRG loads, LOAD "$" directory loads, exact/wildcard filename matching, Machine-tab disk UI/status.
- PRG loader polish: reset-before-load, pending injection after BASIC warm-start at $E38B, keyboard-buffer autostart PRGs supported.
- Assembler UI integration: Assembler tab, file picker, address/run address, auto-run, reset/run-to-BASIC assembly flow, assembler error event/dialog, symbol snapshot handoff to disassembler.
- Host file load/save UI: unified Load and Save buttons on Machine tab; Load dialog has From File address, Reset, and Basic Program checkboxes; Save dialog has Basic Program checkbox (reads $2B–$2E, forces header), Write address header, and Start/End range fields.
- Audio output infrastructure (C64AUDFID_1): lock-free SPSC ring buffer, SDL audio device, PAL/NTSC cycle-to-sample conversion, 440 Hz smoke tone, turbo mute, overrun/underrun counters.
- SID functional audio (C64AUDFID_2): triangle/saw/pulse/noise waveforms, ADSR envelope, Chamberlin SVF filter, 3-voice mixer, voice 3 read-back, $D400–$D41F register map; deferred: per-voice filter routing, ring/sync mod, combined-waveform blending, NTSC tables.

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
- Bad Line BA and sprite-fetch BA both stall CPU reads using CPU event read/write classification; writes continue where allowed.
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

### Debugger / UI / config

- Runtime owns machine state, breakpoints, watchpoints, stop reason, counters, and actions.
- Frontend renders copied snapshots only and sends intents/commands to runtime.
- Register and memory edits apply only while paused; running edits are ignored.
- Debugger input focus is explicit: C64 display vs debugger views.
- Symbol table is tools/frontend/debug-session-owned, separate from emulator machine and assembler internals.
- INI supports config and breakpoint persistence; invalid breakpoint entries are skipped while valid entries load.
- Call stack view (Misc|Debugger tab): runtime walks the 6510 stack each frame, verifies JSR opcode at each candidate return address via the CPU memory map, and publishes up to 16 entries as a `runtime_call_stack_snapshot`. Displays `XXXX | JSR label/YYYY` rows; clicking either column centers the disassembly view on that address.
- Hardware view (Misc|Hardware tab): collapsible `NK_TREE_TAB` sections render copied runtime snapshots for Memory/Banks, VIC-II, CIA #1/#2, SID, and counters. Memory/Banks shows CPU port banking, CPU-visible regions, VIC bank, and `$D018`-derived bases; VIC-II shows raster/IRQ/register/color/BA/sprite state; CIA shows ports, timers, ICR, TOD, and alarm; SID shows voice, filter, read-back, and sample state. The shared hardware rows use a wider static layout so the tab can scroll horizontally for long diagnostics.

### Audio output infrastructure (C64AUDFID_1)

- Lock-free SPSC ring buffer (`util/audio_buffer`) delivers float mono samples from the runtime thread to the SDL audio callback without blocking either side.
- SDL audio device managed by `platform/platform_audio`: opens at 48 kHz stereo float (`AUDIO_F32SYS`), accepts frequency/channel changes from SDL; expands internal mono to actual output channels in the callback.
- Runtime thread calls `runtime_audio_produce()` each batch: fractional cycle accumulator converts PAL (985248 Hz) or NTSC (1022727 Hz) machine cycles to host sample rate; clock frequency is looked up via `c64_config_clock_hz()`.
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
- Combined-waveform priority: noise > pulse > saw > triangle (analog blend deferred).
- 23-bit LFSR output mapped via documented bit positions (20,18,14,11,9,5,2,0).
- ADSR envelope: fractional double accumulator; attack and decay/release rate tables at PAL 985248 Hz; sustain level = nibble × 17.
- Mixer: each voice scaled by envelope/255, summed, divided by 3 (anti-clip), multiplied by volume/15 (`$D418` bits 0–3), clamped to [-1, +1].
- Voice 3 disconnect: `$D418` bit 7 removes voice 3 from mix.
- State-variable Chamberlin filter (per-cycle): f = (cutoff+1)/4096; q = 1 – res/20 (clamped 0.1..1.0); HP/BP/LP computed in that order; filter states clamped to [-2, +2]. Mode selected by `$D418` bits 4–6 (LP/BP/HP); no mode bits → bypass.
- Voice 3 read-back: `$D41B` = phase bits 23..16; `$D41C` = current envelope byte.
- Paddle reads (`$D419`, `$D41A`) return 0xFF (not connected).
- `sid_sample()` is a trivially const read of `last_sample`; the SDL audio callback can call it safely with no machine pointers.
- `runtime_thread.c` calls `sid_sample(&rt->machine.sid)` to fill the audio buffer each host sample.
- 28 tests in `tests/machine/test_sid.c` (register, voice, ADSR, mixer/filter, audio-flow smoke); all pass.

#### SID deferred items

- Per-voice filter routing (`$D417` bits 2..0): full mix always passes through the filter; independent voice-to-filter routing pending.
- Exact 6581/8580 analog waveform blending (combined waveforms produce hardware-specific shapes, not bitwise OR).
- Ring modulation and oscillator sync (`$D404` bits 1–2).
- Paddle/potentiometer (`$D419`, `$D41C`) — policy: 0xFF until connected input is emulated.
- NTSC rate tables (current tables are PAL 985248 Hz only).

## Not implemented / deferred

- Full CIA accuracy and pin/race-level timing.
- Cycle-perfect video/audio timing.
- VIC-II light pen (`$D013/$D014` stubbed; Phase F skipped).
- Last-byte-on-bus open-bus behavior; unused VIC registers currently return fixed values per Phase G.
- VIC idle-state g-access fetch behavior from `$3FFF` / `$39FF` in renderer.
- Exact RDY/AEC sub-cycle CPU pin timing.
- NTSC sprite BA timing table; current sprite BA table is PAL-only.
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
- Assembler tab: assemble with Reset on (existing flow) and Reset off (live inject + auto-run); error dialog; symbol display in disassembly.
- Host load/save: Load with file-address header; Load with Basic Program (check $2B–$2E updated); Save as Basic Program and reload; Save raw range with and without header; verify Eject button and Machine tab section order.
