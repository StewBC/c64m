# c64m agent handoff

This directory is the concise, implementation-oriented handoff for a fresh agent.
The C source and tests are authoritative. Do not read files under `md-files/` -
they are historical working notes kept for record-keeping only and are not
guaranteed to be accurate or current. If a handoff doc and the source disagree,
or if a handoff doc doesn't answer your question, trust the source, not md-files.

Read in this order:

1. `architecture.md`
2. The component handoff relevant to the task
3. `testing.md`
4. The source and tests named by that handoff

When using **VICE as the oracle** against titles under `assets/prg/`, also read
**`vice-oracle.md` before launching VICE**. Those files are one-load collection
PRGs (full inject, IRQ vector override); the wrong VICE flags look like an
emulator bug. See that note for the required `-autostartprgmode 1` / `-autoload`
command line.

No hacks allowed. This is an emulator and the goal is to meet hardware so all
future software that works on hardware (or vice) also "just works" here.

Docs must track source. If you change behavior in a way that makes a component
handoff inaccurate, update that document in the same change. Stale docs are
worse than no docs - don't leave them for the next agent to untangle.

## Diagnosis discipline: locate → kill → then model

Written after several sessions were lost to plausible mechanisms that were never
measured. The examples below are VIC-II, but the method is not: it applies just
as well to a CIA timer, a SID envelope, or a 1541 rotation bug.

1. **Locate the defect in observables first.** Before any mechanism talk: *where*
   is it wrong (x/y/raster/cycle/frame), *when* (which rows/frames), *what*
   differs (colour index? border vs field? sprite vs graphics?). One histogram or
   one dump that answers "where is every wrong pixel?" beats three clever
   theories. Note that a bug report marks the **symptom, not its extent** - an
   annotated screenshot arrowing the left edge once hid a defect that was three
   times worse on the right, and the wrong extent had been written into the
   handoff as the section heading.

2. **The handoff is a suspect, not a map.** Prior notes, code comments, tests,
   and "we already know it's X" must be falsified or confirmed by measurement.
   If the latches say `$B` and the story says `$8`, the story is dead. Do not
   deepen a dead story.

3. **One kill criterion per hypothesis.** For each competing idea, write the
   measurement that would kill it *before* you implement a fix:
   - "If black is only outside `[24,344)`, freecolor/latch theories are out."
   - "If mbff/CSEL match on good and bad rows, the border-flip-flop theory is out."

   If you can't name the kill test, you're smoking tobacco, not diagnosing.

4. **Instrument freely; land nothing until one mechanism is left standing.**
   Throwaway probe code is how you get to one hypothesis - the `Force XSCROLL=0`
   experiment is what proved the B0C pad. What multiplies thrash is *landing a
   fix* while two hypotheses still compete. Dump until the residual is one paint
   path (or one fetch path), then read VICE for that path only.

5. **Ground truth: VICE outranks our tests; hardware outranks VICE.** If a unit
   test encodes c64m's old model and VICE disagrees, the test is wrong - rewrite
   it, and don't keep bad physics to "preserve green". But VICE is a model too,
   and says so: the `gbuf_pipe0_reg = 0` line that the side-border fix rests on
   carries the comment *"It should probably be done somewhere around the fetch
   instead"*. Treat VICE as the default oracle and hardware as the tiebreak.

6. **Prove blast radius after the fix.** Same frames, the affected region only,
   plus the known demos (lft-nine, EoD checker). If the change *cannot* touch the
   display window, show that. No "should be fine".

Component handoffs:

- `machine.md` - C64 machine, CPU, bus, memory, interrupts, cartridges, snapshots
- `vicii.md` - VIC-II timing, rendering, sprites, PAL/NTSC, BA/AEC/RDY
- `cia.md` - CIA timers, interrupts, keyboard, joystick, IEC pins, TOD, serial
- `sid-audio.md` - SID behavior and runtime/platform audio transport
- `disk-iec1541.md` - D64/T64/CRT host I/O and optional 1541 ROM/media path
- `runtime-control.md` - runtime thread, commands, snapshots, control port
- `control-port.md` - wire protocol, Python client, command reference, payloads
- `frontend-debugger.md` - SDL/Nuklear UI, debugger, input, configuration, help
- `tools.md` - assembler, disassembler, symbols, D64/T64/CRT/G64 parsers, util
- `testing.md` - automated coverage, baseline command, known gaps, smoke checks
- `vice-oracle.md` - how to load `assets/prg/` one-load collection PRGs in VICE
  (`-autostartprgmode 1`, `-autoload`); required for c64m vs VICE compares

Current baseline is 51/51 passing. That baseline includes the real 1541 ROM/IEC,
G64, Arkanoid, and Robocop paths.

The verification command is:

```text
ctest --test-dir build --output-on-failure
```
