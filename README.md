# c64m — Commodore 64 Emulator

c64m is a Commodore 64 emulator written in C99. It was almost entirely written using
coding agents — Claude Code and ChatGPT's Codex. There are three exceptions:

1. Codex chose to use the CPU emulation I had written for
   [a2m](https://github.com/StewBC/a2m) (my Apple II emulator) verbatim.
2. Codex brought in the a2m built-in assembler, then modified and improved it — and
   scaled it back to just 6502. In hindsight it is the way I wish I had written it
   originally.
3. Claude Code wrote the manual (and this README.md), though it read the a2m manual first, so some concepts —
   especially in the assembler section — trace back there.

## What it does

c64m boots a real C64 ROM set and runs a broad range of software: BASIC programs, single-file PRG binaries, and games and demos loaded from read-only D64 disk images. It handles both PAL and NTSC timing. On an Apple M2 Mac Mini, the emulator runs at roughly the real C64's 1 MHz in normal mode and can be pushed to around **14.8 MHz** at maximum turbo — enough to make a lot of BASIC programs feel instant.

The built-in debugger gives you a live disassembler, a hex memory editor, a full breakpoint system with read/write/execute watchpoints, a call-stack view, and a hardware-state inspector covering VIC-II, both CIAs, and SID. Both the disassembly and memory views can be switched independently between three source modes — the CPU-mapped address space, raw RAM, or the physical ROM bytes — so you can inspect what the CPU sees, what is underneath it, or what is in the ROM regardless of which is currently banked in.

The integrated two-pass 6502 assembler lets you assemble, and run code without leaving the emulator. Assembled labels feed straight into the disassembler's symbol table.

Loading and saving host files is done from the Machine tab: you can load a PRG at any address, optionally reset the machine before loading, repair BASIC pointers after load, and save raw memory ranges or BASIC programs back to the host.

SID audio is functional — three voices, ADSR envelopes, waveform generation, a state-variable filter, and voice 3 read-back — though some hardware-specific behaviors (filter routing per voice, ring modulation, oscillator sync, analog waveform blending) remain deferred.

The manual is also online here: [c64m Manual](./manual/manual.md)
There's a details section with a lot more [technical details.](./manual/manual.md#Details)
There's a YouTube Video detailing the [emulator](https://youtu.be/LGlVHitZAtw) at the 60 hour mark.

## Quick Start

c64m requires C64 ROM files. Place files named `basic`, `kernal`, and `character` (any
extension) in the same directory as the executable, or in a `rom` or `roms`
subdirectory. A combined basic+kernal `system` ROM is also accepted.

Launch the application. The C64 display fills the window and the emulated machine boots
normally — you should see the familiar blue BASIC screen within a moment.

Press **Opt+H** (macOS) or **Alt+H** (Windows/Linux) at any time to open the built-in
manual. Everything — keyboard shortcuts, debugger controls, assembler syntax, INI file
format — is documented there.

Press **F9** to toggle the debugger layout.


## Issues

The emulation is a work in progress. Many games from single-load collections run
correctly, but accuracy gaps remain.

- D64 support is read-only. Disk writes, SAVE to disk, the error channel, 1541 CPU/ROM
  emulation, full IEC timing/protocol, fast loaders, and devices beyond 8 and 9 are not
  implemented.
- Some lower-level bus details are approximate: exact RDY/AEC sub-cycle CPU pin timing,
  last-byte-on-bus open-bus behavior and VIC idle-state fetches.

Testing has not been exhaustive — the project is still in active development.

Stefan Wessels  
swessels@email.com  
First commit - June 14, 2026
