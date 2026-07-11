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
- BA/RDY is derived from scheduled Phi2 VIC accesses into one absolute-cycle
  low interval and feeds one `vicii_ba_active()` predicate.
- PAL and NTSC have separate sprite data-slot tables; a shared lead/release
  rule derives the corresponding BA/RDY interval.
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

The cycle-level rule is now explicit: RDY low holds CPU reads, while AEC low
denies the Phi2 bus to every CPU access. Thus a write may complete in a BA/RDY
lead or release cycle only while AEC remains high. Analog or half-cycle pin
waveforms remain outside this model.

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

Phase 0 follow-up completed on 2026-07-10:

- A real PAL badline fixture starts a CPU instruction on VIC cycle 12, proves
  the first CPU event completes before BA assertion, then proves 43 subsequent
  master cycles advance while the pending CPU read is held.
- PAL and NTSC sprite-0 fixtures lock the different late BA assertion cycles
  (PAL 54, NTSC 56) and their six-cycle windows.
- A PAL sprite-3 fixture locks the cross-line assertion at cycle 60 of line
  N-1 and proves the held read remains pending after raster line N begins.

These are still baseline fixtures for the current BA-window implementation.
They do not yet provide a per-cycle VIC fetch log or distinguish CPU opcode,
operand, dummy, and data reads. That is the remaining Phase 0/Phase 1 handoff.

## Phase 1 progress (2026-07-10)

The trace now carries a semantic access kind from the 6510 helper that issued
the bus callback. The initial vocabulary is: opcode fetch, operand read, data
read/write, dummy read, RMW dummy write, stack read/write, and vector read.
This metadata is observational only; it does not alter the existing BA decision
or CPU execution order.

The first regression set proves the tags for:

- `STA abs`: opcode fetch, two operand reads, and data write.
- `PHP`: opcode fetch, dummy read, and stack write.
- `BRK`: opcode fetch, padding-byte operand read, three stack writes, and the
  two IRQ-vector reads.
- `ASL abs`: data read, RMW dummy write of the old value, and final data write.

The helper audit is deliberately incomplete: direct reads in some instruction
helpers, especially immediate and indexed forms, still inherit the generic
data-read tag. The next Phase 1 slice is to route those through named operand
and dummy helpers, then add page-crossing, branch, pull/RTI, IRQ, and NMI
fixtures before the trace is used to change scheduler behavior.

Phase 1 follow-up completed on 2026-07-10:

- Direct immediate and branch-displacement reads now use the operand-read
  helper. Direct PC reads used for implied, accumulator, and indexed timing
  cycles now use the dummy-read helper.
- New trace fixtures cover a taken branch, `PLA`, an IRQ after `CLI`'s required
  one-instruction deferral, and a RESTORE-driven NMI. They verify the operand,
  dummy, stack, and vector access sequence without changing execution.

Remaining Phase 1 work is now narrower: audit the indirect/indexed address
generation helpers, especially their page-crossing dummy reads, and add an
`RTI` stack-read fixture. Once that is done, typed traces are sufficient to
begin the Phase 2 arbiter behind a selected opcode-family gate.

Phase 1 completion and Phase 2 entry (2026-07-10):

- Indexed/page-crossing address-generation accesses are now typed as dummy
  reads. The `LDA $12FF,X` fixture verifies the intermediate `$1200` dummy
  cycle and final `$1300` data read. An `RTI` fixture verifies its three stack
  pulls.
- `c64_step_instruction()` now runs an instruction through the same existing
  per-Phi2 replay arbiter used by `c64_step_cycle()`. This removes the former
  timed-immediate instruction path, which could bypass BA stalls and produce a
  different raster position from cycle stepping.
- A parity fixture starts `LDA abs` under BA-low and proves that instruction
  stepping and cycle stepping finish with identical PC, CPU/master cycle
  counts, event count, and data-read absolute cycle.

The next Phase 2 slice is no longer public-step convergence; it is replacing
the frozen `c6510_step()` trace/replay implementation itself with a resumable
CPU micro-cycle executor. That is a separate, higher-risk change and must be
introduced behind opcode-family trace-equivalence gates.

## Phase 2 / 3 progress (2026-07-10)

The first resumable executor is now active. It issues one real CPU bus callback
per Phi2 cycle through the arbiter rather than executing an instruction against
a frozen device world. The migrated, trace-gated families are:

- NOP; immediate loads, ALU, compare, and common flag operations.
- Zero-page and absolute loads/stores.
- Zero-page ALU and compare operations.
- X/Y-indexed zero-page loads/stores, including their pre-index dummy read.
- X/Y-indexed absolute loads/stores, including page-cross and store dummy
  reads before the final effective-memory access.
- Zero-page and absolute ASL/ROL/LSR/ROR/DEC/INC, including the NMOS old-value
  dummy write.
- Absolute JMP; conditional branches including page-cross dummy cycles.
- Register transfer, register increment/decrement, and accumulator-shift
  instructions, each with its Phi2 dummy read.
- JSR/RTS, PHA/PHP/PLA/PLP, RTI, and BRK.
- IRQ/NMI entry, including its dummy reads, stack writes, and vector reads.

All other opcode families deliberately remain on compatibility replay. This is
not yet a complete replacement for `c6510_step()` and must not be described as
full cycle-perfect CPU execution.

VIC-II now also records its current scheduled bus operation for bad-line
c-accesses and PAL/NTSC sprite fetch cycles. BA continues to use the existing
tested lead-window predicate. The schedule marker is the Phase 3 bridge toward
individual fetch scheduling; idle g-accesses and per-byte sprite data accesses
remain future work.

The complete 46-test CTest suite passes after these changes. Phase 4 remains active:
the remaining legal and practical-undocumented opcode families and complete VIC
fetch scheduling have not migrated. Phase 5's full compatibility decision
therefore cannot yet be closed honestly.

## Current-model PAL/NTSC baseline (2026-07-10)

Before further CPU-family or VIC-II schedule migration, the project now records
its current PAL/NTSC timing fixtures in
`timing-baselines/PAL_NTSC_CURRENT.md`. This is an evaluation baseline: it
documents the current scheduler's observable signatures and points to their
executable assertions, but does not label them as real-chip golden traces.

The baseline covers the current bad-line and sprite BA ordering, PAL sprite-3
cross-line behavior, PAL/NTSC sprite-0 differences, and a timed VIC register
write. Any subsequent change to one of those signatures must update the fixture
and baseline record together, with either a regression rationale or a cited
hardware target.

## Phase 4 documented-opcode migration (2026-07-10)

The resumable Phi2 executor now gates every documented NMOS 6502/6510 opcode
and addressing form. The final migration slice added direct and indexed
ALU/compare operations, `BIT`, `(zp,X)` and `(zp),Y` reads/stores,
zero-page/absolute indexed RMW operations, and NMOS indirect-JMP page-wrap
behavior. Machine tests cover direct, indexed page-cross, indirect, indexed RMW,
and indirect-JMP trace sequences.

The Phase 4 checklist is therefore:

| Family | Resumable path | Status |
|---|---|---|
| Documented NMOS 6502/6510 opcodes | Yes | Complete |
| IRQ/NMI entry and BRK | Yes | Complete |
| Stable practical undocumented families (SLO/RLA/SRE/RRA/DCP/ISC/LAX/SAX) | Yes | Trace-gated |
| Chip-dependent undocumented forms (XAA/AHX/SHX/SHY/TAS/LAS/LAX #imm/JAM) | No | Compatibility replay retained |
| Reset sequence | N/A | Machine reset path, not an executing opcode |

This closes the documented portion and the stable practical-undocumented slice
of Phase 4. It does not make the VIC-II fetch schedule complete or claim
chip-revision accuracy for unstable undocumented opcodes.

### Stable undocumented-opcode migration (2026-07-11)

The resumable Phi2 executor now handles the stable unofficial opcode families
used by compact C64 software: SLO, RLA, SRE, RRA, DCP, ISC/ISB, LAX, and SAX.
Composite RMW instructions retain their NMOS sequence of effective-memory read,
old-value dummy write, then modified final write before applying their combined
ALU operation. Tests validate family semantics and compare normal versus PAL
bad-line traces for representative direct, indirect-X, and indirect-Y forms.

The unstable bus/chip-dependent unofficial operations remain intentionally on
compatibility replay. That boundary is explicit: migration here improves BA
interleaving without falsely claiming revision-independent results.

### Complete VIC fetch schedule (2026-07-11)

The VIC-II now schedules character/color c-accesses, graphics and idle accesses,
sprite pointers, and sprite-data accesses on explicit Phi1/Phi2 slots. BA is
derived from the scheduled Phi2 accesses with the established three-cycle lead
and two-cycle release margin; separate PAL/NTSC sprite BA-assert tables are
gone. The live renderer continues
to pre-latch a complete sprite row before drawing a line, while the scheduled
pointer/data reads preserve the authoritative timing order.

### Migrated-family BA validation (2026-07-11)

The migrated documented addressing families now have a PAL bad-line regression
gate in addition to their normal expected bus traces. For each representative
family, the gate compares normal and BA-contended execution for identical CPU
state, memory result, opcode PC, bus-event type/access/address/value, and CPU
cycle offset. BA may delay only the event's absolute machine cycle. The fixture
uses the current-model PAL baseline (raster `$33`, cycle 12), and separately
asserts that pending store and RMW write phases are allowed to proceed while BA
is low.

This validates the current model's contention behavior; it is not a claim that
the resulting timings are yet validated against a specific VIC-II revision.
