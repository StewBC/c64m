# C64M VIC-II / CPU Bus Timing Follow-on Plan

## Purpose

This plan moves c64m toward a cycle-scheduled VIC-II / 6510 bus model without
silently expanding the current ordinary-software milestone into complete
cycle-perfect hardware recreation.

The practical objective is to make the observable relationship between CPU bus
cycles, VIC-II bus ownership, register-write timing, and raster progression
explicit, testable, and progressively more faithful for PAL and NTSC. It is a
future fidelity follow-on. Items that AGENTS.md explicitly leaves out of the
current milestone remain out of scope unless separately accepted.

This plan supersedes no completed VIC-II work. It builds on the timed bus-event
foundation, live renderer, bad-line sequencing, and PAL/NTSC sprite BA work.

## Scope and non-goals

In scope:

- A per-Phi2 machine scheduling contract shared by CPU bus accesses and VIC-II
  bus work.
- Accurate enough CPU stalls and bus-event ordering for selected raster code,
  bad lines, sprite DMA, and normal PAL/NTSC software.
- Incremental VIC-II fetch scheduling: c-accesses, g-accesses, sprite pointer
  and data accesses, and the corresponding BA/RDY behavior.
- Deterministic, machine-level timing traces and regression tests.
- Replacing instruction-replay approximations only after equivalent behavior is
  proven by tests.

Not in scope without a new approval:

- Analog/open-bus last-byte behavior as a general feature.
- VIC-II light pen.
- CIA sub-Phi2 timing, FLAG, PC pulse, or handshake-line work.
- Exact 1541 timing, GCR, or fast-loader support.
- A frontend timing debugger. Optional internal trace data must remain inside
  machine tests unless a separate debugger requirement is accepted.
- Claiming bit-perfect or universal demo-scene compatibility.

## Current reconciliation

The current implementation is not a blank slate:

- `c64_t` owns a monotonic machine cycle and advances devices to timestamped
  CPU bus events.
- VIC-II bus-visible writes are applied before the VIC advances that cycle.
- Bad-line and sprite BA are represented as absolute-cycle low windows and
  feed one `vicii_ba_active()` predicate.
- PAL and NTSC have separate sprite-BA assert tables.
- The CPU core remains instruction-oriented. For an instruction needing
  contention handling, the machine can execute against a frozen device world,
  then replay its recorded bus events (`C64_CPU_BUS_MODE_DEFER_WRITES`).

That last point is the central limitation. The event stream carries enough
information for many practical cases, but the CPU, VIC-II, and bus arbiter do
not yet execute as one authoritative Phi2 schedule. The work below therefore
must preserve the current public instruction-stepping API while moving the
internal implementation toward one.

## Timing contract

Every emulated Phi2 cycle has this order:

1. Establish the cycle number and the VIC-II operation scheduled for it.
2. Determine whether the VIC-II withdraws BA and whether a CPU bus access may
   proceed this cycle.
3. If the CPU owns an eligible bus cycle, perform exactly one CPU-visible read
   or write at that cycle. A stalled read remains pending; a write follows the
   chosen, documented RDY policy.
4. Apply that bus side effect before any device observes the remainder of the
   cycle. A VIC register write is therefore visible to the VIC-II at its own
   scheduled boundary.
5. Advance VIC-II, CIA, SID, and CPU state exactly once for the completed
   Phi2 cycle, publishing no live pointers outside `machine/`.

The exact CPU write/RDY and dummy-read rules are to be established by trace
fixtures before being generalized. Do not treat BA-low as a blanket instruction
stall or infer an unmeasured rule from an unrelated opcode.

## Phases

### Phase 0 - Reconcile and measure the existing model

Goal: establish a tested baseline before changing scheduler behavior.

Work:

- Inventory all `c64.c` CPU bus modes and every path that advances machine
  time, including instruction, cycle, trace, IRQ, NMI, and reset paths.
- Document the current event ordering and cycle offset semantics in this file.
- Add a machine-test trace helper that records, without changing behavior:
  master cycle, raster line/cycle, BA state, CPU event type/address/value, and
  whether the event executed or stalled.
- Build small deterministic fixtures for: no contention, a bad line, each
  sprite BA group, a cross-line sprite window, and a timed VIC write.
- Run the existing VIC-II, CPU-validation, frame, and snapshot tests as the
  baseline.

Acceptance:

- Each fixture produces a stable trace suitable for later golden comparisons.
- The current CPU event ordering is stated with source references rather than
  inferred from comments.
- No functional behavior changes in this phase.

### Phase 1 - Define typed CPU bus-cycle traces

Goal: replace ambiguous event classification with a compact, explicit CPU
micro-cycle trace contract while retaining instruction-level execution.

Work:

- Define event kinds required by the 6510 core: opcode fetch, operand read,
  data read, dummy read, stack read/write, vector read, and data write.
- Record the exact cycle offset and whether an event is repeatable when RDY
  stalls it.
- Audit every addressing mode and interrupt sequence against this contract.
- Add targeted tests for representative read, write, read-modify-write,
  branch, stack, IRQ, and NMI instructions.

Acceptance:

- No event kind is described as "unknown" in a contended execution path.
- Existing instruction results and cycle counts remain unchanged when BA is
  high.
- The trace can distinguish a stalled read from a write that proceeds.

### Phase 2 - Install a single machine Phi2 arbiter

Goal: make one scheduler the authority for advancing all emulated devices and
for deciding whether a pending CPU bus cycle completes.

Work:

- Introduce an internal machine scheduler API owned by `machine/`; it must not
  leak into runtime or frontend.
- Feed Phase 1 CPU events into the arbiter one cycle at a time, initially for
  selected opcode families only.
- Preserve public run/step-instruction/step-cycle behavior and snapshot
  semantics.
- Retire `DEFER_WRITES` only for opcode families proven equivalent under the
  new path; keep a narrowly documented compatibility path for the rest.

Acceptance:

- A bad-line read is held at its pending bus cycle and resumes at the first
  eligible Phi2 cycle.
- A timed VIC register write is observed at its measured bus cycle, not at
  instruction completion.
- CPU, VIC, CIA, and SID cycle counters remain coherent across pause, reset,
  IRQ/NMI, and single-step.

### Phase 3 - Make VIC-II bus work an explicit per-cycle schedule

Goal: derive BA from scheduled VIC bus use rather than from independent
duration windows.

Work, in this order:

1. Model bad-line c-accesses individually across their established window,
   retaining the existing latched-video result.
2. Schedule sprite pointer/data accesses using the existing PAL/NTSC tables,
   including cross-line lookahead.
3. Model idle/display g-access selection and its memory addresses only after
   the c-access and sprite trace tests are stable.
4. Route BA assertion from the schedule with its measured lead time; retain one
   BA predicate as the CPU-facing interface.

Acceptance:

- Golden traces show every scheduled VIC access and the same CPU availability
  result as the Phase 2 arbiter.
- Existing rendering, border, sprite, and collision tests remain green.
- PAL and NTSC schedules have separately named constants and fixtures.

### Phase 4 - Migrate the remaining CPU execution paths

Goal: eliminate split "execute then replay" behavior for normal 6510 execution.

Work:

- Migrate remaining legal and implemented practical-undocumented opcode
  families in small groups.
- Make IRQ/NMI, BRK, reset, page-crossing, branch, and RMW sequences use the
  same scheduler.
- Keep a per-opcode migration checklist; an opcode remains on the compatibility
  path until its high-BA and no-BA traces pass.

Acceptance:

- Normal free-running, instruction-step, and cycle-step execution use the same
  timing authority.
- No pending CPU bus operation is lost, duplicated, or reordered through a
  stall or interrupt.
- Existing CPU validation and machine smoke tests pass in PAL and NTSC.

### Phase 5 - Compatibility validation and bounded expansion decision

Goal: prove a material compatibility gain and decide whether further work is
worth expanding scope.

Work:

- Add a small, versioned corpus of legal local trace fixtures and selected
  ordinary software/raster programs with reproducible expected outcomes.
- Compare timing-sensitive cases against documented hardware timing and, where
  available, VICE observations. Store only project-owned test programs/traces;
  do not vendor unlicensed corpora.
- Reassess optional follow-ons separately: last-byte/open-bus behavior,
  light-pen support, CIA sub-cycle work, and broader FLI/demo-scene targets.

Acceptance:

- The project can name specific incompatibilities fixed by the new timing
  model, not merely a claim of greater accuracy.
- Any unresolved feature remains explicitly listed in `docs/status/DEFERRED.md`.
- A decision is recorded on whether to pursue full cycle-perfect recreation.

## Guardrails

- Do not add SDL, frontend, or runtime dependencies to `machine/`.
- Do not expose live machine timing state to the UI; use copied snapshots only
  if visibility later becomes necessary.
- Preserve a buildable, tested state after each opcode family or VIC schedule
  slice. Do not land a broad core rewrite without trace-based acceptance tests.
- Treat discrepancies between Bauer-style timing references, VICE behavior,
  and the current tests as reconciliation tasks. Record the chip revision and
  PAL/NTSC target before choosing behavior.
- Update STATUS.md, docs/status/VICII.md, docs/status/CPU_MACHINE.md,
  docs/status/TESTING.md, and docs/status/DEFERRED.md when an implemented phase
  changes their facts.

## Phase 0 findings (2026-07-10)

Initial source inspection confirms the expected split model:

- `c64_step_instruction()` selects `C64_CPU_BUS_MODE_DEFER_WRITES` for a
  traced instruction and then replays events through `c64_advance_devices_to()`.
- `c64_deferred_io_read()` projects selected VIC register reads to the replayed
  bus-event cycle, but it is a targeted compatibility mechanism rather than a
  general cycle-interleaved CPU implementation.
- `vicii_step_cycle()` currently batches a bad-line video-matrix latch at the
  start of the line and sprite data fetches at cycle zero. BA itself is scheduled
  later through absolute low windows. This is appropriate for the existing
  renderer but is the first VIC-II behavior Phase 3 must make trace-visible.

The next active work item is therefore Phase 0's trace-helper design and
baseline tests, not a rewrite of BA timing.

Phase 0 has now added a test-only timing sampler in
`tests/machine/test_c64_cpu_validation.c`. Its first fixture records a pending
`LDA abs` data read across BA-low cycles: master/VIC time advances, CPU elapsed
time remains fixed, and the read completes on the first BA-high cycle. This is
deliberately a predicate-level baseline, not evidence that the current model
has a complete VIC-II fetch schedule.

Baseline verification passed on 2026-07-10:

- `c64_vicii`
- `c64_cpu_validation`
- `c64_frame`
- `c64_snapshot`

The next Phase 0 slice is to replace the synthetic BA window in that fixture
with fixtures driven by an actual bad line and each PAL/NTSC sprite-BA group,
then record the CPU event trace alongside raster position.
