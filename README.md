# c64m - Commodore 64 Emulator

c64m is a Commodore 64 emulator written in C99.  It was (almost) entirely written using coding agents — Claude Code and ChatGPT's Codex.  There are two exceptions:
1. Codex chose to use the CPU emulation I had written for [a2m](https://github.com/StewBC/a2m) - Apple II emulator - verbatim.
2. Codex brought the a2m built-in assembler in, but modified and improved it (and scaled it back to just 6502). In the bigger picture, it is now the way I wish I had written it initially.

## Features

---

### CPU

- MOS 6510 CPU with full 6502 instruction set
- Accurate instruction-level execution and cycle counting
- IRQ and NMI interrupt handling (edge-triggered NMI latch)
- RESTORE key routes through NMI path
- Timed bus events: reads and writes are classified and timestamped within each opcode so mid-instruction side effects land at the correct cycle
- Bad Line CPU stall via BA line (c-access windows and sprite p/s-access windows)
- Read vs. write cycle discrimination for BA stall behavior

---

### Memory & Bus

- Full 64K address space with C64 bank-switching
- RAM, BASIC ROM, KERNAL ROM, character ROM, and I/O banking controlled by CPU port ($0001)
- Character ROM mapped through VIC bus at $1000–$1FFF (bank 0) and $9000–$9FFF (bank 2)
- Debugger-safe bus peek API that avoids side effects (CIA ICR clear-on-read, etc.)
- VIC-II bank base derived from CIA #2 port A pins (four 16K banks)
- SID audio registers mapped at `$D400–$D41F` on the CPU bus

---

### VIC-II Graphics

- PAL (6569) and NTSC timing selectable from configuration
- Full 384×272 internal frame with border; presented as a balanced 352×240 crop
- Live raster rendering: pixels are emitted cycle-by-cycle as VIC time advances; mid-frame register changes take effect at their exact event cycle
- Border rendering with RSEL/CSEL 25/24-row and 40/38-column window clamps
- XSCROLL and YSCROLL applied as delayed display-window edges
- DEN bit: clearing blanks display and border pixels to background color ($D021)

**Graphics modes:**
- Standard text mode (40×25 characters from screen RAM and character ROM/RAM)
- Multicolor text mode
- Standard bitmap mode
- Multicolor bitmap mode
- ECM (Extended Color Mode) text
- Invalid modes 5, 6, 7 (black background/display layer)

**Sprites:**
- All 8 hardware sprites with independent X/Y position (9-bit X via MSB register)
- Per-sprite enable, hires (24×21) and multicolor (12×21 logical pixels) modes
- X-expand (double width) and Y-expand (double height) with correct flip-flop behavior
- Sprite pointer (p-access) and data (s-access) fetched from VIC bank
- Sprite-background priority ($D01B): per-sprite front/behind foreground selection
- Sprite-sprite collision latch ($D01E) with read-clear and write-ignore
- Sprite-background collision latch ($D01F) with foreground-pixel detection
- Sprite collision IRQs (IMMC/IMBC) wired through VIC IRQ path
- BA cycle stealing for sprite fetch windows with correct cross-line handling for sprites 3 and 4

**Registers:**
- Full register mirroring ($D000–$D3FF)
- Unused register high-bit masking per hardware spec ($D016, color registers, unused block $D02F–$D03F read as $FF)
- Open-bus high nibble on color registers reads as 1

**VIC-II IRQ:**
- Raster IRQ (programmable raster compare line)
- Sprite collision IRQs
- IRQ status ($D019) and enable ($D01A) with aggregate enabled-pending summary in bit 7

---

### CIA

**CIA #1** (keyboard / joystick / timer IRQ):
- Timer A and Timer B: 16-bit down-counters, continuous and one-shot modes
- Timer source selection: Phi2, CNT pulses, Timer A cascade, combined cascade+CNT
- PB6/PB7 timer outputs: pulse and toggle modes
- ICR interrupt mask, flag management, and clear-on-read
- Timer underflow IRQ routed to CPU IRQ line
- Bidirectional keyboard matrix scan (both Port A row and Port B column directions)
- Multi-key simultaneous press with correct active-low electrical combining
- Joystick port 1 (Port B bits 0–4) and joystick port 2 (Port A bits 0–4)
- Keyboard and joystick inputs combine on shared CIA lines

**CIA #2** (VIC bank / IEC / timer NMI):
- VIC bank selection from Port A PA0/PA1 with correct inverted mapping
- IEC ATN, CLK, DATA open-collector line representation
- Timer NMI routed to CPU NMI with edge latch (avoids repeated NMI while line stays asserted)

**Time-of-Day (TOD):**
- BCD second, minute, hour (12-hour AM/PM with correct 11→12 and 12→1 rollover)
- 60 Hz / 50 Hz source selection via CRA bit 7 (configurable PAL/NTSC frame cadence)
- Coherent read latching: reading hours latches all TOD fields; reading tenths releases latch
- Alarm with ICR bit 2 flagging and IRQ/NMI routing through Phase D mask logic
- Debugger-safe TOD peek without creating or releasing the CPU-visible latch

---

### Audio & SID

- Audio output infrastructure with lock-free SPSC ring buffer feeding SDL audio without blocking runtime or callback threads
- 48 kHz float audio path with internal mono sample generation expanded to the actual output channels by the platform callback
- PAL/NTSC cycle-to-sample conversion through a fractional cycle accumulator
- Turbo mode mutes audio output to prevent buffer flooding while SID state continues to advance
- Audio overrun and underrun counters for diagnostics
- `--audio-smoke` CLI flag for a 440 Hz square-wave audio-path test
- Functional MOS 6581 SID emulation mapped at `$D400–$D41F`
- Three SID voices with triangle, sawtooth, pulse, and noise waveforms
- ADSR envelope generation, sustain levels, and voice 3 oscillator/envelope read-back
- 3-voice mixer with master volume and voice 3 disconnect (`$D418` bit 7)
- Chamberlin state-variable filter with low-pass, band-pass, and high-pass modes
- SID tests cover register mapping, voice behavior, ADSR, mixer/filter, and audio-flow smoke cases

**SID deferred items:**
- Per-voice filter routing (`$D417` bits 0–2)
- Exact 6581/8580 combined-waveform analog blending
- Ring modulation and oscillator sync
- Connected paddle/potentiometer input beyond the current not-connected read policy
- NTSC-specific SID rate tables

---

### Disk (.d64)

- Read-only D64 image support (standard 35-track geometry)
- Two independent drive slots: device 8 and device 9
- Mount and unmount from the Machine tab UI or runtime commands
- KERNAL LOAD trap at $FFD5 for devices 8 and 9 (other devices fall through to ROM)
- `LOAD "NAME",8` — PRG load at BASIC start pointer with BASIC end-pointer updates
- `LOAD "NAME",8,1` — PRG load at embedded PRG load address
- `LOAD "$",8` — directory synthesis as tokenized BASIC program (disk title, file list, blocks free)
- Filename matching: exact, prefix wildcard (`LAKE*`), single-character wildcard (`?`), and `*` for first PRG
- BAM metadata: disk title, disk ID, DOS type, free-block count
- Directory enumeration with PETSCII filename preservation and ASCII debug names
- Malformed-chain protection: loop detection and out-of-range sector guards
- Device 8 and device 9 operate independently; unmounting one does not affect the other
- Disk status displayed in Machine tab (disk title or host filename)

---

### Assembler

Full two-pass 6502 assembler integrated into the emulator:

- All standard 6502 addressing modes and opcodes (65C02 extensions excluded)
- Labels, variables (`ident = expr`, `ident++`, `ident--`), and forward references
- `.org` / `* =` origin control
- Data directives: `.byte`, `.word`/`.addr`, `.dword`, `.qword`, reverse-order variants (`.drow` etc.), `.res`, `.align`
- String directives: `.string`/`.asciiz`, `.strcode`
- `.include` and `.incbin` with recursion guard and path resolution relative to the including file
- `.define` text substitution (identifier word-boundary aware, skips string literals)
- Conditional assembly: `.if`/`.else`/`.endif` with nesting, `.defined`, comparison operators (`.lt`, `.le`, `.gt`, `.ge`, `.eq`, `.ne`)
- Loops: `.for`/`.endfor` and `.repeat`/`.endrepeat`/`.endrep` (up to 64K iterations)
- Macros: `.macro`/`.endmacro` with parameter substitution and `.local` scoped names
- Scopes and procedures: `.scope`/`.endscope`, `.proc`/`.endproc`
- Named segments: `.segdef`/`.segment` with emit/noemit control
- Assembly loads directly into C64 RAM at the specified address
- Reset-and-assemble flow: machine resets, runs to BASIC ($E38B), then assembles
- Auto Run option: sets PC to run address and resumes the emulator after assembly
- Assembler labels are exported to the debugger symbol table immediately after assembly
- Scrollable per-line assembly error dialog

---

### Debugger

**CPU / Register View:**
- Live PC, SP, A, X, Y, and N V - B D I Z C flags from runtime snapshots
- Paused register and status flag editing

**Disassembly View:**
- 6502 disassembler with full addressing mode decode
- PC-following and manual navigation (Up/Down, PageUp/PageDown, Home/End)
- CPU-map and raw-RAM disassembly modes
- Assembler label display in disassembly (symbol resolver)
- Breakpoint gutter indicators; Option+B toggles an execute breakpoint at the cursor

**Memory View:**
- 16-byte hex + ASCII display of the full 64K address space
- CPU-map and raw-RAM modes (CPU-map uses side-effect-free debug peeks)
- Cursor navigation, PageUp/PageDown, Home/End
- Custom scrollbar for 64K address space drag
- Hex-nibble and ASCII in-place editing while paused

**Breakpoints / Watchpoints:**
- Execute, read, and write breakpoints with inclusive address ranges
- Map/ROM/RAM filter for ROM vs. RAM discrimination
- Stable runtime IDs, enabled/disabled state, hit counters, and reset counters
- Actions: Break, Fast (turbo), Slow (restore pacing), Tron, Troff
- Duplicate addresses and multiple breakpoints at the same address supported
- Breakpoint editor modal: create, edit, duplicate, access checkboxes, range, mapping, counters, actions
- Breakpoint persistence in INI file (`[DEBUG]` section, `break.<address>` keys)
- Clear, clear-all, enable/disable from the Breakpoints tab

**Symbol Table:**
- Assembler-sourced, file-sourced, user-defined, and built-in symbol kinds
- Exact-address and name lookup; nearest-symbol lookup
- Source-kind removal for reassembly/reload workflows
- Disassembler integration via symbol resolver

**Step Controls:**
- Instruction step (F10)
- Step out (Shift+F10)
- Step over (F11)
- Run (F12)
- Run to cursor (Shift+F12)
- Pause stops execution at the next instruction boundary

**Execution Trace (TRON/TROFF):**
- TRON action enables per-instruction tracing
- TROFF action disables tracing

---

### Keyboard Input

- Semantic host-to-C64 key mapping (not physical layout mapping)
- Letters A–Z, digits 0–9, common BASIC punctuation, shifted symbols
- Shift+letter preserves C64 left graphics characters; Tab+letter gives Commodore graphics characters
- C64 cursor keys from host arrow keys; Shift synthesized for left/up
- C64 CONTROL from host Control; C64 Commodore from host Tab
- RUN/STOP from ESC; DEL from Backspace; INST from Shift+Backspace; RESTORE from host Delete
- HOST and C64 keyboard matrix combined with joystick on shared CIA lines
- Clipboard paste (Option+Insert): injects host clipboard as C64 key events with cycle-accurate timing (~40ms hold, ~10ms gap per character, scales with turbo)
- Matrix paste (Shift+Option+Insert): direct matrix injection for reliable bulk paste
- All ASCII printable characters mapped; unmappable characters silently skipped

---

### Joystick

- Joystick port 1 emulated via CIA #1 Port B bits 0–4 (up/down/left/right/fire)
- Joystick port 2 emulated via CIA #1 Port A bits 0–4

---

### Display & UI

- Single-window application (no popup windows or docked panels)
- **Display-only mode**: C64 display fills the entire window
- **Debugger mode** (F9 toggle): C64 display + CPU, disassembly, memory, and misc panels
- Aspect-ratio-preserving scaling with letterbox/pillarbox
- Integer scaling and aspect correction options
- Configurable display dimensions
- Resizable window; size saved to INI on quit
- Adjustable splitter layout between debugger panels; layout saved to INI
- Disk activity LED visibility option
- Machine tab groups Disks, Programs, and Emulator controls, including D64 mount/eject, host Load/Save, Configure, and Reset

---

### Configuration & Persistence

- INI file (`c64m.ini`) for all persistent settings
- `[config]` section: video standard (PAL/NTSC), display size, scaling, aspect correction, filter, scroll wheel speed, turbo speed list, symbol file paths, disk LED, auto-save flag
- `[Window]` section: window width and height
- `[Layout]` section: debugger splitter positions
- `[DEBUG]` section: breakpoints with duplicate-suffix support
- Configure dialog with Machine and Emulator tabs; OK applies immediately, Cancel discards
- `--noini` skips startup INI load; `--nosaveini` disables save controls
- Changing the INI filename auto-enables save-on-quit

---

### Turbo & Pacing

- Normal PAL/NTSC paced running at ~60 Hz frame cadence
- Configurable turbo multiplier list (e.g. `2,4,8,16`)
- Option+T cycles through configured turbo speeds
- Fast breakpoint action switches to maximum turbo; Slow action restores paced mode
- Clipboard paste timing scales correctly with the active turbo multiplier

---

### PRG Loader

- Load `.prg` files directly into C64 RAM from the Machine tab
- Reset-before-load with automatic resume of pre-load run state
- Collection PRGs (keyboard buffer pre-fills) work correctly via deferred injection at BASIC warm-start ($E38B)
- Manual RESET cancels any pending PRG injection
- Host file load path can optionally repair BASIC pointers and defer injection until BASIC warm-start

---

### Host File Load/Save

- Unified Load and Save buttons on the Machine tab
- Load dialog supports host file selection, optional PRG address-header use, manual load address, reset-before-load, and BASIC-program pointer repair
- Save dialog supports raw memory ranges, optional address header, and BASIC Program mode using `$2B–$2E`
- BASIC Program save mode forces address-header output and uses TXTTAB/VARTAB to choose the save range
- Reset-before-load waits for BASIC warm-start (`$E38B`) before injecting program data
- Live assembler injection is supported when assembler Reset is unchecked; Auto Run can jump to the run address and resume execution

---

### Architecture

- Layered C99 architecture: Machine → Runtime → Frontend, with a Tools layer
- Machine and runtime run on a dedicated runtime thread; UI runs on the main thread
- All cross-thread data is passed as copied snapshots — no live machine pointers cross threads
- Command/event message queue between frontend and runtime
- Snapshot rule enforced: frontend receives only copied state and never reads live machine memory
- Machine owns monotonic master cycle; VIC, CIA, and SID hooks advance to timestamped CPU bus events before visible side effects

## Keyboard Quick Reference

Keys listed here control the **emulator** — they are intercepted before reaching the C64.
On macOS, **Opt** = Option/Alt.

---

### Emulator Controls

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| F9             | Toggle debugger UI on/off                                     |
| Cmd+Q (mac)    | Quit                                                          |

---

### Run / Step (active in all modes)

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| F12            | Run (resume)                                                  |
| Shift+F12      | Run to cursor (disassembly cursor address)                    |
| F10            | Step instruction (if paused) — or Pause (if running)         |
| Shift+F10      | Step out (run until return from current subroutine)           |
| F11            | Step over (step across JSR without entering)                  |
| Opt+S          | Step over (same as F11)                                       |

---

### Turbo

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| Opt+T          | Cycle through configured turbo speeds (default: 2×/4×/8×/16×)|

---

### Clipboard / Paste

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| Opt+Ins        | Paste clipboard as timed C64 keystrokes (~40 ms/key)          |
| Shift+Opt+Ins  | Paste clipboard via direct keyboard-matrix injection          |

---

### Gamepad / Joystick Port Mapping

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| Opt+1          | Map single gamepad to joystick port 1                        |
| Opt+2          | Map single gamepad to joystick port 2 (default)              |
| Opt+1 or Opt+2 | With two gamepads connected: swap port assignment             |

---

### Disassembly View  *(focus must be in the disassembly panel)*

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| Opt+B          | Toggle execute breakpoint at cursor (paused only)             |
| Ctrl+A         | Enter address-jump mode (type 4 hex digits, Enter to jump)   |
| Tab / Shift+Tab| Cycle symbol display mode (none → label → address+label)      |
| Up / Down      | Move cursor one instruction                                   |
| PgUp / PgDn    | Scroll one page                                               |
| Home / End     | Jump to start / end of view                                   |

---

### Memory View  *(focus must be in the memory panel)*

| Key            | Action                                                        |
|----------------|---------------------------------------------------------------|
| Ctrl+A         | Toggle address-entry mode (type 4 hex digits to jump)        |
| Ctrl+T         | Toggle hex ↔ ASCII edit mode                                  |
| Up / Down      | Move cursor one row (16 bytes)                               |
| Left / Right   | Move cursor one byte / nibble                                 |
| PgUp / PgDn    | Scroll one page                                               |
| Home           | Move cursor to start of current row (or first address digit) |
| Ctrl+Home      | Move cursor to start of visible window                       |
| End            | Move cursor to end of current row (or last address digit)    |
| Ctrl+End       | Move cursor to end of visible window                         |
| 0–9, A–F       | Edit hex nibble or ASCII byte at cursor (paused only)        |

---

### C64 Key Mappings (semantic, not physical layout)

| Host Key           | C64 Key                       |
|--------------------|-------------------------------|
| Letters A–Z        | A–Z                           |
| Shift+Letter       | Shifted letter (left graphics set) |
| Tab+Letter         | Commodore+Letter (right graphics set) |
| Digits 0–9         | 0–9                           |
| Shift+!            | !  (Shift+1)                  |
| Shift+"  or  '     | " (Shift+2 → @)               |
| Shift+#            | #  (Shift+3)                  |
| Shift+$            | $  (Shift+4)                  |
| Shift+%            | %  (Shift+5)                  |
| Shift+^  or  \     | ↑ (up-arrow key)              |
| Shift+&            | &  (Shift+6)                  |
| Shift+* or  ]      | *                             |
| Shift+(            | (  (Shift+8)                  |
| Shift+)            | )  (Shift+9)                  |
| =                  | =                             |
| Shift+=            | + (plus)                      |
| -                  | −                             |
| ;                  | ;                             |
| Shift+;            | :                             |
| '  (single quote)  | ' → synthesises Shift+7       |
| "  (double quote)  | " → synthesises Shift+2       |
| ,                  | ,                             |
| .                  | .                             |
| /                  | /                             |
| +  (or numpad +)   | +                             |
| −  (or numpad −)   | −                             |
| *  (or numpad *)   | *                             |
| /  (or numpad /)   | /                             |
| [  or  (           | @                             |
| `  (backtick)      | ← (left-arrow key)            |
| Return / KP Enter  | Return                        |
| Space              | Space                         |
| Backspace          | DEL                           |
| Shift+Backspace    | INST (insert)                 |
| Escape             | RUN/STOP                      |
| Delete             | RESTORE (triggers NMI)        |
| Home               | HOME / CLR HOME               |
| Shift+Home         | CLR (clear screen)            |
| Right arrow        | Cursor right                  |
| Left arrow         | Cursor left  (Shift+Right)    |
| Down arrow         | Cursor down                   |
| Up arrow           | Cursor up    (Shift+Down)     |
| Ctrl               | CONTROL                       |
| Tab                | Commodore (C= key)            |
| F1                 | F1                            |
| F2                 | F2  (Shift+F1)                |
| F3                 | F3                            |
| F4                 | F4  (Shift+F3)                |
| F5                 | F5                            |
| F6                 | F6  (Shift+F5)                |
| F7                 | F7                            |
| F8                 | F8  (Shift+F7)                |

## Notes

As of end-of-day June 18, the total time spent on this project was 32 hours.  That includes the time I was thinking, describing, and typing, as well as the time the agents thought and coded.  It also includes all the time testing and playing.  I mostly used one agent or the other, but there is some overlap where I used both at the same time.

June 19, at almost 6 hours, audio output infrastructure and functional SID audio support have been added.  The SID is now in: the emulator has a MOS 6581 register map at `$D400-$D41F`, three voices, waveform generation, ADSR envelopes, mixing, voice 3 read-back, and a first-pass filter.  This is not intended to mean cycle-perfect or analog-perfect SID emulation; several hardware-specific behaviors remain deferred.

## Issues

The emulation is not at all perfect.  Recent work has added audio/SID and host file load/save support, but several accuracy and completeness gaps remain.

* It does run the machine at "machine speed" and if I set the turbo to its maximum, 256, it is a tiny bit faster.  There's no real boost (a2m will run an Apple II on my M2 Mac at over 100 MHz, so 100x faster, for comparison).
* There are UI issues.  For example, if you change the PC, you need to click out of that box and back in to change it again.  And then there's no overtype.
* Step out is broken under some circumstances.
* When you play Galencia (it does interesting things with the border and sprites), you need to "trick" the game into starting — I know how but I haven't looked at why yet — and the sprites do get mangled, so the emulation isn't quite good enough yet.

* SID support is functional but incomplete: per-voice filter routing, exact combined-waveform behavior, ring modulation, oscillator sync, connected paddle input, and NTSC-specific SID rate tables are still deferred.
* Audio/video timing is not cycle-perfect.
* VIC-II light pen support is still stubbed/skipped.
* D64 support is read-only; disk writes, SAVE to disk, error channel, 1541 CPU/ROM emulation, full IEC timing/protocol, fast loaders, and devices beyond 8/9 are not implemented.
* Some lower-level bus details remain approximate, including exact RDY/AEC sub-cycle CPU pin timing, last-byte open-bus behavior, VIC idle-state fetches, and NTSC sprite BA timing.

That's just a small subset of the issues I am aware of.  Testing has not been thorough — I am mostly still making.  But, on the other hand, many games from the one-load collection work perfectly.

Stefan Wessels  
swessels@email.com  
June 14, 2026  
