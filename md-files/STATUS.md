# STATUS.md

## Current State

Completed through Phase 11.

Implemented:

- 6510 CPU integrated through C64 bus.
- RAM, ROMs, banking, and address decoding.
- ROM loading and reset-vector boot path.
- Runtime thread and command/event model.
- Run, pause, reset, cycle-step, instruction-step.
- Frame pipeline with copied runtime-to-frontend handoff.
- VIC-II skeleton:
  - register storage
  - register mirroring
  - raster timing foundation
  - frame generation
  - border/background rendering
- Character display bring-up:
  - screen RAM fetch from $0400
  - character ROM glyph fetch
  - color RAM nibble storage and fetch
  - 40x25 text rendering into the active display area
- CIA foundations:
  - CIA #1 and CIA #2 machine-owned devices
  - `$DC00-$DCFF` and `$DD00-$DDFF` bus routing
  - register storage and mirroring
  - timer A/B latch, counter, and underflow foundations
  - interrupt mask/flag foundations
  - CIA #1 IRQ pending callback path
  - CIA #2 NMI pending foundation
  - deterministic no-key keyboard matrix reads
- ROM boot progression:
  - machine/runtime boot checkpoint counters
  - IRQ vector entry validation through the machine bus
  - IRQ stack push validation
  - ROM-driven screen RAM writes reflected in frames
  - ROM-driven color RAM writes reflected in frames
  - VIC-II `$D018` screen/character pointer support
  - real 64C ROM smoke checkpoint reaches VIC/CIA/screen activity
- Keyboard Pass 1 plumbing:
  - machine-owned C64 keyboard matrix
  - key press/release state
  - CIA #1 keyboard scan reads through `$DC00/$DC01`
  - runtime copied key down/up commands
  - SDL key mapping for letters, digits, space, return, delete, shift, and common BASIC punctuation keys
  - semantic host cursor arrows:
    - right/down map to C64 cursor keys
    - left/up synthesize Shift + C64 cursor keys
  - ESC maps to C64 RUN/STOP
  - Backspace maps to C64 DEL
  - host Delete maps to RESTORE
- Keyboard Pass 2 / Phase 11 BASIC typing polish:
  - SDL-to-C64 key translation moved out of `main.c` into frontend-owned input mapping
  - runtime still receives copied project-level keyboard/RESTORE commands, not SDL events
  - default mapping is semantic host typing rather than physical C64 key layout
  - focused frontend mapper regression tests cover shifted punctuation, remembered synthetic releases, cursor keys, CONTROL, Commodore, and RESTORE
  - C64 CONTROL is mapped from host Control
  - C64 Commodore is mapped from host Tab
  - emulator controls use Option+R run, Option+S step, and Option+P pause
  - F10/F11/F12 remain available for run/step/pause
  - host quote/double-quote, colon, plus, parentheses, asterisk, @, cursor arrows, HOME/CLR HOME, RUN/STOP, RESTORE, left-arrow, and up-arrow have semantic mappings
  - Shift+letter preserves the C64 left graphics character set
  - Tab+letter provides the C64 Commodore graphics character set
  - manual BASIC validation transcript added for:
    - `10 PRINT "HELLO"`
    - `20 GOTO 10`
    - `RUN`
- IRQ/CIA boot compatibility:
  - CIA #1 ICR read/write diagnostics
  - CIA interrupt assertion diagnostics
  - CPU IRQ entry diagnostics
- CPU NMI entry path for RESTORE
  - CIA zero-latch timer reload behavior
  - CIA one-shot timer stop behavior
  - normal runtime RUN pacing at roughly 60 Hz frame cadence
- App startup:
  - reset screen starts clear
  - frontend queues Run automatically after initialization
- SDL display of machine-generated frames.
- Phase 12 debugger UI foundation, View 1:
  - slim CPU/register view renders from copied runtime CPU snapshots
  - PC, SP, A, X, Y, and `N V - B D I Z C` flags display in fixed-width uppercase/readable form
  - paused register/status edits emit frontend debugger intents
  - `main.c` translates debugger intents into runtime_client commands
  - runtime owns and applies CPU register mutations only while paused
  - running register mutations are ignored by runtime
  - regression coverage validates paused CPU register setters and running-state rejection

## Not Implemented

- Phase 12 debugger Views 2+:
  - disassembly view
  - memory view
  - misc/debugger breakpoint/status panel
- Full CIA accuracy.
- Sprites.
- SID.
- Cycle-perfect video/audio timing.

## Current Runtime Notes

Real 64C ROM execution reaches the BASIC READY prompt with a visible cursor and keyboard input.

After a 1,000,000-cycle smoke trace:

```text
PC=$FD7E
CIA #1 IRQ pending = true
CPU IRQ entries = 0
CIA #1 ICR reads = 0
```

The pending CIA #1 IRQ is not currently observed as a CPU IRQ entry because the CPU interrupt-disable flag remains set during the trace.

## Next Phase

Phase 12, View 2.

Goal:

```text
Implement the disassembly view from md-files/C64PHASE12.md.
```
