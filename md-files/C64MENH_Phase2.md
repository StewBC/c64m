# C64MENH Phase 2: NTSC Sprite BA Timing Parity

## Purpose

Audit and, if needed, implement NTSC sprite BA timing parity with the existing PAL sprite BA path.

This is an analysis-first video-timing task. The goal is not full VIC-II cycle perfection. The goal is correct-enough PAL and NTSC sprite BA behavior for the current ordinary-software fidelity milestone.

## Background

The current milestone requires PAL and NTSC video paths to have correct-enough sprite BA behavior for selected tests.

Known project facts from the current status documents:

- VIC-II is implemented through the current practical phase set, excluding light pen.
- The emulator has live raster timing, timed bus-visible writes, PAL/NTSC frame sizes, sprites, priority/collisions, and sprite BA stealing.
- The deferred list still calls out NTSC sprite BA timing table, with current sprite BA table described as PAL-only.

This phase should determine whether that deferred item is still true and, if true, implement the smallest NTSC-specific correction.

## Required Reading

Read in this order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MVICII_NEW.md`, if present
5. Any current VIC-II phase or implementation guide, if present
6. Current VIC-II code and tests

If documentation and code disagree, inspect tests and implementation before changing status text.

## Suggested Branch

Use a dedicated branch:

```sh
git checkout -b enhancement/ntsc-sprite-ba
```

## Phase Goal

Answer these questions with evidence:

1. How does the current PAL sprite BA path work?
2. Is NTSC currently using PAL sprite BA timing?
3. What timing data or behavior should NTSC use for the current milestone?
4. Which tests currently cover PAL and NTSC sprite BA behavior?
5. Is the deferred `STATUS.md` item still accurate?
6. What is the smallest safe change, if any?

## Files And Areas To Inspect

At minimum, inspect:

```text
src/machine/vicii.c
src/machine/vicii.h
src/machine/c64.c
src/machine/c64_bus.c
src/machine/c6510.c
src/machine/c6510_inln.h
tests/machine/test_c64_vicii.c
tests/machine/test_c64_cpu_validation.c
tests/runtime/test_runtime_scheduler.c
md-files/STATUS.md
md-files/C64MVICII_NEW.md, if present
```

Search terms:

```text
sprite
BA
badline
bad line
NTSC
PAL
timing
raster
cycle
steal
DMA
vicii_ba_active
```

## Important Behavior To Verify

Verify and document:

- how sprite BA windows are represented;
- how PAL and NTSC video standards select timing tables;
- whether NTSC has distinct sprite BA timing today;
- how bad-line BA and sprite BA interact;
- how BA stalls CPU reads and internal cycles;
- how CPU writes are allowed during BA;
- how sprite fetch timing affects rendering and collision behavior;
- whether tests assert only final state or exact event cycles.

## Analysis Commands

Suggested inspection commands:

```sh
git status --short --branch
rg -n "sprite|BA|badline|bad line|NTSC|PAL|raster|steal|DMA|vicii_ba_active|video_standard" src tests md-files
nl -ba src/machine/vicii.c | sed -n '1,260p'
nl -ba src/machine/vicii.c | sed -n '260,620p'
nl -ba src/machine/vicii.c | sed -n '620,980p'
nl -ba tests/machine/test_c64_vicii.c | sed -n '1,320p'
nl -ba tests/machine/test_c64_vicii.c | sed -n '1000,1240p'
```

Adjust line ranges as needed.

## Stop / Continue Gate

After analysis, stop and write a short decision note before implementing anything.

Continue only if:

- NTSC sprite BA timing is confirmed missing, wrong, or untested;
- an authoritative-enough timing source or existing planning document supports the intended NTSC behavior;
- a small table/config selection change can address the issue;
- focused tests can pin the behavior.

Stop without implementation if:

- current NTSC sprite BA behavior is already correct and tested;
- the remaining uncertainty requires full cycle-perfect VIC-II research beyond the milestone;
- the proposed change risks PAL regressions without clear NTSC benefit.

## Implementation Guidance If Continuing

If implementation is needed:

1. Add focused tests that fail under the current NTSC behavior.
2. Keep the existing PAL path stable.
3. Introduce explicit PAL/NTSC timing selection through machine configuration, not frontend policy.
4. Implement the smallest NTSC sprite BA timing table or adjustment.
5. Re-run PAL sprite BA tests to prove no regression.
6. Re-run NTSC tests to prove the new behavior.

Avoid speculative VIC-II refactors. Do not implement light pen, open-bus behavior, full sub-cycle RDY/AEC modeling, or demo-scene compatibility as part of this phase.

## Acceptance Criteria

This phase is complete when:

- PAL sprite BA behavior remains covered and passing.
- NTSC sprite BA behavior is either confirmed correct or corrected with tests.
- CPU read stalls and write allowance during BA remain correct.
- Existing VIC, CPU, CIA, SID, runtime, PRG, D64, PAL, and NTSC smoke tests still pass.
- `STATUS.md` reflects reality if the deferred NTSC sprite BA item is resolved.

## Required Commands Before Final Hand-Off

At minimum:

```sh
cmake --build build
ctest --test-dir build
```

If a targeted test runner exists, also run targeted VIC-II and CPU timing tests.

Do not run `./build/c64m` without a timeout.

## Hand-Off Report

End with a concise hand-off report containing:

- branch name;
- commit hash or note that changes are uncommitted;
- exact commands run;
- files inspected;
- files changed;
- tests run and results;
- current PAL sprite BA behavior summary;
- current NTSC sprite BA behavior summary;
- whether the deferred NTSC sprite BA item is resolved;
- known limitations;
- recommended next step.
