# c64m — Commodore 64 Emulator

c64m is a Commodore 64 emulator written in C99. It was almost entirely written using
coding agents — ChatGPT's Codex (5.x), Claude Code (4.8) and Grok (4.5). There are a few exceptions:

1. Codex chose to use the CPU emulation I had written for
   [a2m](https://github.com/StewBC/a2m) (my Apple II emulator).  It later updated and improved this CPU.
2. Codex brought in the a2m built-in assembler, then modified and improved it — and
   scaled it back to just 6502. In hindsight it is the way I wish I had written it
   originally.
3. Claude Code wrote the manual (and this README.md), though it read the a2m manual first, so some concepts —
   especially in the assembler section — trace back there.
4. The code in external was added to the project by Codex.  I added the font for the help screens.

## What it does

c64m boots a real C64 ROM set and runs a broad range of software: BASIC programs, single-file PRG binaries, and games and demos loaded from D64 and G64 disk images (read-only by default, optionally writable). It also attaches generic 8K/16K `.crt` cartridges and can save and restore full machine snapshots to `.c64state` files. It handles both PAL and NTSC timing. On an Apple M2 Mac Mini, the emulator runs at roughly the real C64's 1 MHz in normal mode, free-runs around **5 MHz** with full live rendering (turbo max), and can go faster in warp mode (paint off, for skip-ahead only).

The built-in debugger gives you a live disassembler, a hex memory editor, a full breakpoint system with read/write/execute watchpoints, a call-stack view, and a hardware-state inspector covering VIC-II, both CIAs, and SID. Both the disassembly and memory views can be switched independently between three source modes — the CPU-mapped address space, raw RAM, or the physical ROM bytes — so you can inspect what the CPU sees, what is underneath it, or what is in the ROM regardless of which is currently banked in.

The integrated two-pass 6502 assembler lets you assemble, and run code without leaving the emulator. Assembled labels feed straight into the disassembler's symbol table.

Loading and saving host files is done from the Machine tab: you can load a PRG at any address, optionally reset the machine before loading, repair BASIC pointers after load, and save raw memory ranges or BASIC programs back to the host.

SID audio is functional — three voices, ADSR envelopes, waveform generation, hard sync and ring modulation, per-voice filter routing, a state-variable filter, and voice 3 read-back — though some hardware-specific behaviors (analog waveform blending, runtime 8580 switching, paddles) remain deferred.

The manual is also online here: [c64m Manual](./manual/manual.md)  
There's a details section with a lot more [technical details.](./manual/manual.md#Details)  
There's a [YouTube Video](https://youtu.be/LGlVHitZAtw) detailing the emulator at the 60 hour mark.  

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

- G64: Still missing: pure media-level write fidelity polish, G64 empty-track
  grow/format rebuild, cross-drive copy, block/memory-execute commands, devices
  beyond 8 and 9, and exhaustive fast-loader coverage.
- RDY/AEC are modeled at cycle granularity only (not half-cycle/analog pin timing).
  General last-byte-on-bus open-bus is not modeled; the BA-lead cbuf case
  (`ram[PC] & 0x0f`) is.

Testing has not been exhaustive — the project is still in active development.

## Credit and thanks

Stefan would like to acknowledge that none of this would have been possible without the countless people who have spent countless hours meticulously documenting the Commodore 64 and creating accurate emulations of its hardware.

In particular, the agents used the following resources extensively during the development of c64m:

* The VIC-II implementation was informed extensively by Christian Bauer's *The MOS 6567/6569 Video Controller (VIC-II) and Its Application in the Commodore 64*.

* The VICE Team. Throughout development, the VICE emulator was used as an oracle. The agents ran VICE extensively to measure timings, compare behaviour, and understand the causes of bugs in c64m.

* The following demos, which push the C64 hardware and served as the primary sources for compatibility and accuracy testing:

  * *Nine* by lft (Linus Akesson).
  * *Edge of Disgrace* by Booze Design. Code and design by HCL; music, code, and graphics by Dane; and graphics and design by Jailbird.
  * *Deus Ex Machina* by Crest and Oxyron. Code by Crossbow, graphics by DeeKay, and music by Jeff.

* The following games, each of which was used to test hardware behaviour or compatibility issues requiring special attention:

  * *Donkey Kong* (2016) by Oxyron.
  * *Galencia* by Jason Aldred.
  * *RoboCop* by Data East and Ocean.
  * *Arkanoid: Revenge of Doh* by Taito.
  * *Fort Apocalypse* by Steve Hales and Joe Vierra, published by Synapse Software.

* Special thanks to StatMat for the *OneLoad64* Games Collection.

Thank you all for your magnificent contributions.

Stefan Wessels  
swessels@email.com  
First commit - June 14, 2026
