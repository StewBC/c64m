# c64m agent handoff

This directory is the current-state briefing for an agent working in this tree.
It is not a history of how the emulator was built, and it is not a design-plan
folder. Source and tests under `src/` and `tests/` are authoritative. If a
handoff and the code disagree, trust the code and fix the handoff in the same
change.

Do not read files under `md-files/`. They are historical working notes and are
not guaranteed to be accurate.

## Read in this order

1. This file (rules, diagnosis, where to look)
2. `architecture.md`
3. The component handoff for the task
4. `testing.md` before claiming a change is done
5. The source and tests named by that handoff

If the task is driving c64m over the control port, start with `using-c64m.md`.
If the task compares c64m to VICE on titles under `assets/prg/`, read
`vice-oracle.md` before launching VICE.

## Product

c64m is a C99 Commodore 64 emulator. It boots real ROM, runs PAL and NTSC, and
covers BASIC, PRG inject, D64/G64 (devices 8 and 9), CRT types 0, 5, 7, 8, 15,
17, and 19, SID audio, a debugger, Inspector time travel, an always-on CPU
flight recorder, and a localhost control port (`C64M/8`).

It is not a promise of cycle-perfect demo-scene behavior, every cartridge
mapper, analog SID, or full 1541 mechanics. See `known-gaps.md`.

No hacks. The goal is to meet hardware so software that works on hardware (or
VICE) also works here.

## Design docs (proposals, not product-as-is)

In-flight and historical design writeups live under [`design/`](../design/).
Start at [`design/README.md`](../design/README.md) for the index (active /
landed / abandoned). Do not treat design drafts as agent handoff truth —
source and `agents/*.md` win when they disagree.

## User manual

`manual/manual.md` is the user-facing manual. `tools/gen_help.py` compiles it
into the in-emulator help view. It is not an agent handoff.

If you add or change a user-facing feature (CLI flag, key, dialog, INI key,
control-port command a human would use, debugger behavior), update
`manual/manual.md` in the same change. Read `manual/HELP_MARKDOWN.md` first:
ASCII only, no Markdown links or autolinks, and only the Markdown subset the
help renderer understands.

## Diagnosis: locate, kill, then model

Written after sessions were lost to plausible mechanisms that were never
measured. The examples are VIC-II, but the method is not.

1. **Locate the defect in observables first.** Before any mechanism talk: where
   is it wrong (x/y/raster/cycle/frame), when (which rows/frames), what differs
   (colour? border vs field? sprite vs graphics?). One histogram that answers
   "where is every wrong pixel?" beats three clever theories. A bug report marks
   the symptom, not its extent.

2. **The handoff is a suspect, not a map.** Prior notes, comments, tests, and
   "we already know it's X" must be falsified or confirmed by measurement. If
   the latches say `$B` and the story says `$8`, the story is dead.

3. **One kill criterion per hypothesis.** Write the measurement that would kill
   the idea before you implement a fix. If you cannot name the kill test, you
   are not diagnosing.

4. **Instrument freely; land nothing until one mechanism is left standing.**
   Throwaway probes are how you get to one hypothesis. What multiplies thrash
   is landing a fix while two hypotheses still compete.

5. **Ground truth: VICE outranks our tests; hardware outranks VICE.** If a unit
   test encodes c64m's old model and VICE disagrees, rewrite the test. VICE is
   still a model: treat it as the default oracle and hardware as the tiebreak.
   Match VIC-II models before comparing (`vice-oracle.md`).

6. **Prove blast radius after the fix.** Same frames, the affected region, plus
   the known demos (lft-nine, Edge of Disgrace, Deus Ex Machina) when the change
   can touch them. No "should be fine".

## Where to look

| Task | Handoff | Source |
|------|---------|--------|
| Layers, threads, ownership | `architecture.md` | `src/main.c`, `src/*/CMakeLists.txt` |
| What is not done | `known-gaps.md` | comments / `TODO.txt` |
| CPU, bus, carts, snapshots | `machine.md` | `src/machine/c64*.c`, `c6510*`, `c64_bus`, `c64_snapshot` |
| VIC-II | `vicii.md` | `src/machine/vicii.*`, `c64_frame.*` |
| CIA, keyboard, IEC pins | `cia.md` | `src/machine/cia.*`, `keyboard.*` |
| SID and host audio | `sid-audio.md` | `src/machine/sid.*`, `src/util/audio_buffer.*`, `src/platform/platform_audio.*` |
| Disk, 1541, IEC | `disk-iec1541.md` | `src/machine/c1541*`, `via6522.*` |
| Runtime, Inspector, recorder, rings | `runtime-control.md` | `src/runtime/` |
| Control-port wire | `control-port.md` | `src/control/`, `src/main.c` dispatch |
| Driving c64m from a script | `using-c64m.md` | `tools/c64_control_client.py` |
| Debugger UI, input, help | `frontend-debugger.md` | `src/frontend/`, `src/main.c` |
| Assembler, parsers, util | `tools.md` | `src/tools/`, `src/util/` |
| Tests and measure recipes | `testing.md` | `CMakeLists.txt`, `tests/` |
| VICE as oracle | `vice-oracle.md` | local `x64sc` |

Keep docs current. Stale handoffs are worse than none.

## Verification

```text
ctest --test-dir build --output-on-failure
```

There are 77 registered tests. Ten of them SKIP (CTest code 77) when gitignored
`assets/` media is missing; that is not a regression. Details in `testing.md`.
