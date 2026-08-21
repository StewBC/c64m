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


Manual updates:
- 'manual/HELP_MARKDOWN.md' contains the rules for updating the user manual
  'manual/manual.md'
  
Component handoffs:

- `using-c64m.md` - **Consumer guide** for agents/scripts driving c64m over the
  control port (recipes, gotchas, co-op). Portable brief for other projects;
  maintain here when wire behavior scripts depend on changes. Deep contract:
  `control-port.md`.
- `machine.md` - C64 machine, CPU, bus, memory, interrupts, cartridges, snapshots
- `vicii.md` - VIC-II timing, rendering, sprites, PAL/NTSC, BA/AEC/RDY
- `cia.md` - CIA timers, interrupts, keyboard, joystick, IEC pins, TOD, serial
- `sid-audio.md` - SID behavior and runtime/platform audio transport
- `disk-iec1541.md` - D64/T64/CRT host I/O and optional 1541 ROM/media path
- `runtime-control.md` - runtime thread, commands, snapshots, control port
- `control-port.md` - wire protocol, Python client, command reference, payloads
  (implementer handoff; not the primary teaching doc — see `using-c64m.md`)
- `frontend-debugger.md` - SDL/Nuklear UI, debugger, input, configuration, help
- `tools.md` - assembler, disassembler, symbols, D64/T64/CRT/G64 parsers, util
- `cpu-flight-recorder.md` - implemented always-on, bounded in-memory 6510
  forensic recorder and C64M/3 query/API contract
- `cpu-flight-recorder-plan.md` - completed test-first implementation record,
  performance gates, measurements, and acceptance checklist
- `sessions.md` - **Closed foundation:** multi-asker sessions + `state-changed`
  inform (Inspector prep; no UI). Wire **C64M/7**; control TCP bind lives in
  `main.c` (c64m has no `control_dispatch.c`). Sibling A2M/11 in
  `../a2m/agents/sessions.md`.
- `guarded-breakpoints-plan.md` - **implemented** (Tier 1A): condition-guarded
  breakpoints (`when=`, bounded AND-list of CPU/flag/value/mem/raster terms);
  the "machine stops itself on nuance" primitive. Wire syntax and semantics
  live in `control-port.md`; the source is
  `src/runtime/runtime_breakpoint_condition.{c,h}`.
- `frame-ring-plan.md` - Tier 1B, **implemented**. Ring A: a rolling in-RAM
  ring of completed frames so a one-frame glitch survives a human pause seconds
  late (`src/runtime/runtime_frame_ring.{c,h}`). Ring B: per-line VIC-II derived
  state, including the sprite X actually latched for painting each line
  (`src/runtime/runtime_vic_ring.{c,h}`, record shape in `vicii.h`). Wire
  contract for both in `control-port.md`.
- `framebuffer-format-plan.md` - Stages 1–3 **implemented/measured**. Frames are
  native `indexed8` from VIC paint through runtime/ring storage, with one shared
  forward Pepto expansion at ARGB presentation boundaries. The 128 MiB ring
  grows from about 206 to about 827 PAL frames; frozen-binary wire/demo
  comparisons are byte-exact, core throughput is neutral, and matched indexed
  control latency improved another 22.5% over the frozen Stage 1 build.
- `crt-type19-plan.md` - CRT mapper roadmap: type 19 Magic Desk first, type
  checklist with OneLoad64 unlock counts; implement later, not current code
- `testing.md` - automated coverage, baseline command, known gaps, smoke checks
- `perf-baseline-turbo2.md` - free-run throughput baseline (turbo=2 bar), measure
  recipes, cost model, pitfalls; re-measure against this after performance work
- `vice-oracle.md` - VICE as display/timing oracle: `assets/prg/` load flags
  (`-autostartprgmode 1`, `-autoload`), **always `-VICIImodel 6569`**, binary
  monitor recipes, DISPLAY_GET alignment. Required before c64m-vs-VICE compares.
- `debug-ergonomics-looklater.md` - **Not active work.** Ranked look-later ideas
  (mem workshop fill/move/diff, live media session reconfig, state
  checkpoint/rewind, frame gate / HW snaps) harvested from an a2m-v2 vs
  AltirraBridge comparison. **BUT CHECK against this tree’s source before any
  implementation** — c64m may already cover large parts (e.g. `step-frame`,
  mount wire, VIC/CIA/frame rings). Sibling list:
  `../a2m-v2/agents/backlog.md` E1–E4.

Current baseline is 70/70 passing (includes `c64_snapshot_1541_midload`, the
history integration test, the machine/runtime recorder tests, the guarded-
breakpoint condition/ini/control tests, the frame-ring and VIC-ring
unit/control tests, and `help_view`, which renders the help window headlessly
and inspects the emitted nuklear draw commands). That baseline includes the real 1541 ROM/IEC, G64,
Arkanoid, Robocop, and full 1541 drive-object snapshot paths.

The verification command is:

```text
ctest --test-dir build --output-on-failure
```
