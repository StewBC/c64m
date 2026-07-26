# CPU flight recorder implementation plan

**Status:** implemented and verified (2026-07-25).

The normative behavior is in `cpu-flight-recorder.md`. This plan is ordered to
prove recording cost and correctness before committing to the remote query
surface. Each phase begins with focused tests or measurements and ends with an
explicit gate.

Do not implement later phases if an earlier performance/correctness gate is
unresolved. Do not weaken machine timing to recover recorder performance.

## Source map

Expected primary changes:

| Area | Files |
|---|---|
| Recorder store/query | new `src/runtime/runtime_history.{c,h}` |
| Machine observation hooks | `src/machine/c64.{c,h}`, possibly focused helpers in `c6510.{c,h}` |
| Runtime ownership/lifecycle | `src/runtime/runtime_internal.h`, `runtime.c`, `runtime_thread.c` |
| Runtime commands/events/client | `runtime_command.h`, `runtime_event.h`, `runtime_client.{c,h}` |
| Bulk result ownership | current RPC pool in `runtime_internal.h`, `runtime.c`, `runtime_thread.c`, `runtime_client.c` |
| Startup configuration | `src/app_options.{c,h}`, `src/main.c`, `src/runtime/runtime.h` |
| Control protocol | `src/control/control_protocol.{c,h}`, `control_deferred.{c,h}`, `src/main.c` |
| Build registration | `src/runtime/CMakeLists.txt`, top-level `CMakeLists.txt` |
| Tests | new recorder unit tests plus machine/runtime/control tests under `tests/` |
| Tools | `tools/profile_c64_hotloop.c`, `tools/profile_runtime_hotloop.c`, benchmark scripts |
| Documentation | `agents/*.md`, `manual/manual.md`, generated/manual help workflow |

Existing implementation to remove:

- `RUNTIME_CPU_HISTORY_MAX`, `runtime_cpu_history_entry`, and the by-value event
  snapshot in `runtime_event.h`;
- the history array/count/head in `runtime_internal.h`;
- `runtime_cpu_history_record()` / `runtime_publish_cpu_history()` and slow-loop
  history checks in `runtime_thread.c`;
- old runtime client commands;
- old control parser/dispatch/formatter;
- `tests/runtime/test_runtime_cpu_history.c`;
- old manual and handoff command descriptions.

## Phase 0: lock the measurements and hot-path test harness

### Tests/measurements first

1. Extend or wrap the existing benchmark tools so one invocation can run the
   same workload with:
   - observer absent;
   - observer installed but recording stopped;
   - instruction records only;
   - full specified access recording.
2. Keep video on and drives soft-powered off for the primary row.
3. Add an access-heavy deterministic workload in addition to idle BASIC.
4. Report:
   - Phi2 MHz;
   - execution records/second;
   - retained access records/second;
   - bytes/execution record;
   - bytes/instruction including accesses;
   - wrap count.
5. Preserve the measured legacy figures in the feature spec. New measurements
   always compare enabled/disabled in the same process/build and serial run.

Performance measurement is not a timing-sensitive CTest assertion. Add a
repeatable tool/script and record the command/results in
`agents/perf-baseline-turbo2.md` when the implementation lands.

### Work

- Add a minimal no-allocation observation sink to the machine benchmark before
  building the full arena.
- Confirm that callbacks invoked from `c64_step_cycles_ex()` do not force the
  runtime into the per-cycle slow loop.
- Sample the runtime thread to distinguish observer cost, encoding cost, and
  normal VIC/CPU cost.

### Gate

Proceed if a minimal fixed-width instruction append stays within 5% of the
matched observer-absent baseline. If it exceeds 5%, inspect callback placement,
inlining, and batching before adding variable accesses.

Do not accept the current history-on path as a meaningful prototype; its ~26%
loss is caused by known avoidable work.

## Phase 1: recorder arena module, tested in isolation

### Tests first

Add `tests/runtime/test_runtime_history.c` before integrating with `c64_t`.
Use a small arena/block size through test-only construction parameters.

Required unit cases:

1. Empty store status and first epoch.
2. Begin/access/complete one instruction; decode every field.
3. Operand capture and instruction length.
4. IRQ and NMI records.
5. Partial record before completion.
6. Maximum legal access count.
7. Access overflow sets `truncated` without corrupting the next record.
8. A cycle offset over 65,535 saturates and sets `timing-truncated`.
9. Record exactly fitting the end of a block.
10. Record causing a block transition.
11. Whole-block wrap and eviction.
12. Oldest/newest IDs after repeated wrap.
13. Timeline transition seals a block and accepts a lower machine cycle.
14. A 32-bit block cycle-delta overflow starts a new block.
15. Clear increments epoch and resets IDs.
16. Stop/resume preserves records and inserts a gap marker.
17. Marker encode/decode, including unknown marker IDs.
18. Corrupt/private-record decode fails safely in test helpers.
19. Sealing an active record as partial permits a marker/new record without
    accepting later accesses from the abandoned instruction.
20. Resuming recording mid-instruction waits for the next execution begin.

### Work

Implement `runtime_history` without machine/runtime-thread dependencies beyond
standard integer/bool types:

- creation/destruction;
- available/recording/error status;
- fixed-block circular arena;
- begin instruction/interrupt;
- append access;
- complete current record;
- seal/abort current record as partial and arm at next execution begin;
- append marker;
- stop/resume;
- timeline transition;
- clear/new epoch;
- sequential forward/backward iterators;
- lookup by `(epoch,id)`;
- status counters.

Recommended public/private split:

```text
runtime_history.h
  opaque store
  logical record/access/query/status types
  lifecycle and iterator/query entry points

runtime_history.c
  private block descriptors
  private 22+6 byte encoding
  arena append/eviction/decoding
```

Do not expose arena pointers as durable handles. An iterator may hold a
short-lived const pointer while the runtime is paused, but its public identity is
epoch/generation/ID.

Use explicit byte readers/writers. Add compile-time/static assertions only for
public constants, not native struct packing.

### Gate

- Sanitizer/CTest unit cases pass.
- No append operation allocates or locks.
- Fixed-block wrap leaves no dangling active-record pointer.
- Corrupt test bytes cannot walk beyond a block’s `used` boundary.

## Phase 2: machine observation contract

### Tests first

Add focused machine tests, preferably a new
`tests/machine/test_c64_cpu_observer.c`, covering:

1. NOP/immediate/absolute instructions report pre-state and actual bytes.
2. Self-modifying code reports the byte fetched at execution time.
3. Reads and writes carry correct value/address/kind.
4. RMW reports data read, dummy write, and final write distinctly.
5. JSR/RTS/PHA/PLA exercise stack kinds.
6. IRQ/NMI exercise dummy, stack, and vector kinds.
7. An I/O dummy read is reported exactly once and retains its value.
8. A BA-stalled access reports the actual delayed machine cycle.
9. A practical undocumented opcode on the micro path.
10. An opcode that uses the compatibility/deferred replay path:
    - one begin;
    - no speculative access callbacks;
    - replayed accesses once each;
    - pre-state captured before speculative execution;
    - one completion.
11. Observer null changes no CPU/machine observable.
12. Reset-vector reads performed by `c64_reset()` are not mislabeled as an
    executed instruction.
13. Successful KERNAL LOAD/SAVE traps emit one trap notification; failed or
    bypassed traps do not.
14. A trap reported during an instruction becomes a marker after completion;
    if a discontinuity seals the instruction partial, marker ordering remains
    partial, trap, discontinuity.

### Work

Introduce a machine-level observer contract in `c64.h`. Keep payloads compact
and passed by const pointer/value. One likely shape is:

```c
typedef struct c64_cpu_observer {
    void (*begin)(void *user, const c64_cpu_observer_begin *begin);
    void (*access)(void *user, const c64_cpu_observer_access *access);
    void (*complete)(void *user);
    void (*host_trap)(void *user, const c64_cpu_observer_trap *trap);
} c64_cpu_observer;
```

The begin kind distinguishes instruction/IRQ/NMI. Access contains the existing
`c6510_bus_access_kind`. The trap payload contains a kind, start address, and
byte count.

Refactor `c64_set_memory_access_callback()` and runtime read/write breakpoint
observation into this contract if doing so avoids a second per-access callback.
Preserve breakpoint behavior exactly; current read watchpoints see all CPU read
callbacks, not merely effective-address data reads.

Hook placement:

- normal begin: immediately before `c6510_micro_begin()`;
- deferred compatibility begin: before speculative `c6510_step()`, while the
  CPU still contains the pre-instruction registers;
- interrupt begin: `c64_begin_interrupt_now()` after the final decision to take
  the interrupt;
- access: after the actual bus read/write value is known;
- deferred mode: suppress during trace generation, report from
  `c64_apply_cpu_bus_event()`;
- completion: both normal micro completion and deferred replay completion.
- host trap: the successful exit of the KERNAL LOAD/SAVE trap, mapped by the
  runtime to a history marker.

Keep observer state installed across `c64_reset()`.

### Gate

- Existing CPU, bus, VIC-II, breakpoint, and snapshot tests remain green.
- Observer traces are identical between instruction stepping and repeated cycle
  stepping for the same program.
- No duplicated access exists on the deferred path.
- Observer-null benchmark is within 1% of the pre-change baseline.

## Phase 3: runtime integration and always-on lifecycle

### Tests first

Replace the old `test_runtime_cpu_history` with runtime flight-recorder tests:

1. Default nonzero configuration is available and recording.
2. `history_memory_mb=0` is unavailable by configuration but runtime starts.
3. Injected allocation failure is nonfatal and exposes allocation-failed state.
4. Several stepped instructions appear with correct fields.
5. Free-running recording stays on the simple batched path.
6. `record off` freezes count/used bytes during execution.
7. `record on` adds a resume marker then records.
8. Clear starts a new epoch and preserves recording state.
9. Reset retains old IDs, increments timeline, and records new cycle-zero data.
10. Save-state changes nothing in history status.
11. Successful load-state clears and starts a new epoch.
12. Failed load-state preserves epoch/records.
13. Program injection/direct memory mutation markers occur only after success.
14. Pausing mid-instruction exposes a partial record.
15. Runtime destruction releases the arena after allocation failure and success.
16. Reset/record-off/direct mutation during a partial instruction seals it,
    marks the discontinuity, and resumes only at the next instruction begin.
17. Every current `runtime_reset_machine()` call site emits its specified stable
    reset kind: startup, explicit, machine-config, CRT attach, program load,
    reset-first assembly, or reset-first BIN load.
18. CRT attach records mutation then reset across the timeline boundary;
    reset-first program/BIN/assembly paths record reset then later mutation.
19. A successful first mutation remains marked if a following reset/load step
    fails.
20. A video-standard clock-rate change without reset starts a new timeline with
    `clock-discontinuity(video-standard-change)`; a config-triggered full reset
    emits only `reset-complete(machine-config)`.

Provide an allocator hook or construct the history store separately so allocation
failure can be deterministic in tests. Do not attempt to exhaust host memory.

### Work

- Add `history_memory_mb` to `app_options` and `runtime_config`.
- Default to 256.
- Parse `[debug] history_memory_mb`.
- Parse `--history-memory=<MiB>` with `0` or 16..4096 validation.
- On platforms where the requested byte count exceeds `SIZE_MAX` or safe
  multiplication bounds, report unavailable/invalid rather than wrapping.
- Allocate history during `runtime_create()`. Failure does not fail
  `runtime_create()`.
- Initialize descriptors without zeroing the full record-data arena.
- Install the machine observer after `c64_init()` and before initial reset/boot.
- Add runtime adapter callbacks that append directly to `runtime_history`.
- Remove history from `runtime_free_run_is_simple()` rare-path gating.
- Add lifecycle markers/timeline handling at successful reset, load, injection,
  assembly, and direct memory mutation points.
- Change the runtime reset helper to accept an explicit history reset cause;
  map every existing call site to the stable HST1 reset-kind enum.
- Place mutation markers at their actual commit points. In particular, append
  CRT attach before invoking its subsequent reset, while reset-first loaders
  append their mutation only after the later injection succeeds.
- Compare the old/new configured C64 clock rate. A no-reset rate change starts a
  new timeline with the stable video-standard discontinuity reason; a full
  config reset uses only the machine-config reset cause.
- Clear only after successful `c64_snapshot_load()`.
- Invalidate the history cursor once before resume/step or any runtime command
  that can execute, append, reset, or directly mutate the machine. Do not update
  cursor generation in the observer access hot path.
- Destroy history in every runtime destruction path.

Do not expose configuration in the frontend dialog in this phase.

### Performance gate

Run the Phase 0 tools after full access recording:

- <=5% loss: proceed normally;
- >5% and <=10%: profile and optimize before protocol expansion;
- >10%: stop. Do not hide the result by defaulting recording off.

Also record actual 256 MiB retention for idle BASIC and the access-heavy
workload. Idle BASIC must retain at least 8 million execution records.

## Phase 4: query engine

### Tests first

Extend `test_runtime_history.c` with deterministic stores for:

1. Empty-filter forward/backward paging.
2. Exact PC and inclusive PC range; reversed ranges are rejected.
3. Exact address and inclusive address range.
4. Every canonical access kind and every alias.
5. Value exact and value/mask.
6. Timeline and machine-cycle bounds.
7. `from=oldest`, `from=newest`, and explicit ID.
8. Default newest-first ordering.
9. Opcode patterns:
   - exact;
   - full wildcard;
   - high/low nibble wildcard;
   - overlap;
   - no cross-marker/IRQ/NMI/timeline match.
10. Conjunctive anchor filters.
    - PC excludes markers but includes IRQ/NMI pre-entry PC;
    - execute/opcode patterns exclude IRQ/NMI;
    - physical access filters include IRQ/NMI;
    - empty filters include markers.
11. Cursor continuation without duplicate/missing matches.
12. New find replaces old cursor.
13. Resume/step and every lifecycle/direct-mutation operation make the cursor
    stale before recorder contents can change.
14. ID lookup before/after eviction.
15. Context read at oldest/newest boundaries.
16. 256-match and 1 MiB payload/page boundaries.
17. No-match find returns a successful empty page and closes its cursor.
18. Cursor IDs are nonzero/non-reused and become zero in the final page.

### Work

- Define a parsed `runtime_history_query`.
- Implement block-metadata rejection first, then linear record scanning.
- Implement one runtime-owned cursor containing the complete query and next
  position.
- Keep search results logical; binary encoding belongs in a separate encoder
  helper so a future UI can consume logical records directly.
- Materialize opcode/operand fetch access entries in every logical result so the
  query library and wire encoder expose one consistent access list.
- Track query scan statistics in debug builds/tools: blocks visited, bytes
  scanned, records decoded.

Do not add Bloom filters yet.

### Query performance gate

Fill a 256 MiB store and measure:

- exact-address hit near newest;
- exact-address miss requiring full scan;
- PC range;
- three-opcode wildcard pattern.

If full exact-address scan is <=500 ms on the reference M2, the linear version
is acceptable for first delivery. If it exceeds 500 ms, add a sealed-block
address/PC index and re-run both query and recording benchmarks.

Any index that pushes recording over the 10% ceiling is rejected.

## Phase 5: generic reliable bulk RPC

### Tests first

Add/extend runtime delivery tests for:

1. Token-matched history info/control completion.
2. Token-matched binary payload claim.
3. Wrong token cannot claim or complete.
4. Pool full returns `busy`.
5. Event queue full releases payload and returns deterministic failure where
   possible.
6. Disconnect/cancel releases payload.
7. Runtime shutdown releases every unclaimed payload.
8. Memory RPC behavior remains unchanged after any pool refactor.
9. Two payload types cannot be confused under the same/wrong token.

### Work

Prefer refactoring `runtime_rpc_memory_pool` into a generic token-keyed owned
payload pool:

```text
token
payload kind
owned bytes
byte length
small kind-specific metadata
in-use flag
```

If this refactor creates disproportionate risk, an equivalent dedicated history
pool is acceptable, but ownership and cancellation rules must be identical.

Add runtime commands/events for:

- history info;
- record on/off;
- clear;
- find;
- next;
- read;
- close.

Every command issued for control uses a nonzero request token. Completion events
remain small. Runtime-client claim transfers the heap payload out of the pool.

### Gate

- `sizeof(runtime_event)` does not grow due to history result capacity.
- The event queue’s up-front allocation remains effectively unchanged.
- Reliable paths have no known queue-full/disconnect leak.

## Phase 6: C64M/3 control protocol

### Parser tests first

Update `tests/control/test_control_protocol.c` before dispatch:

1. Remove old command acceptance.
2. Parse every new command and default.
3. Reject duplicate/unknown options.
4. Validate all numeric/range limits.
5. Parse address forms already supported by the protocol.
6. Parse all access kinds/aliases.
7. Parse exact/nibble/full-wildcard opcode patterns.
8. Reject patterns over 32 instructions.
9. Reject value without a valid byte/mask.
10. Reject trailing garbage.
11. Preserve request IDs on token-matched deferred history-record replies.

The last case specifically prevents the former `set-cpu-history` bug where an
accepted command can be formatted with wire ID 0.

### Wire encoder tests first

Add a focused history payload encoder test:

- exact 24-byte payload header;
- exact 48-byte record header;
- exact 8-byte access entries;
- little-endian golden bytes;
- instruction, IRQ, NMI, marker, partial, access/timing-truncation, and
  anchor-match flags;
- stable marker, reset-kind, and clock-discontinuity numeric values;
- opcode/operand fetch materialization;
- byte-count/record-size validation;
- 1 MiB cap.

### Work

- Bump `hello` / `version` to C64M/3.
- Raise `CONTROL_RESPONSE_TEXT_MAX` to 512 for complete `history-info` status.
- Replace command enum/arguments and parser branches.
- Add `history` to capabilities.
- Add main-thread dispatch using nonzero runtime tokens.
- Mark history operations as exclusive deferred work.
- Claim payloads and format `data history`.
- Use 10-second deadline for find; 2 seconds for the other operations.
- Map runtime statuses to the specified control errors.
- Make `history-info` the authoritative allocation/availability report.

Update `tools/c64_control_client.py` with helpers that decode HST1 safely:

```text
history_info()
history_find(...)
history_next(cursor, limit=...)
history_read(id, epoch=..., before=..., after=...)
```

The decoder validates magic, format version, record size, access count, and
payload bounds before returning records.

### Integration tests

Add a headless/control integration workflow that:

1. connects and confirms C64M/3;
2. checks `history-info`;
3. resets/runs/pauses;
4. finds a known PC and memory write;
5. reads surrounding records;
6. verifies binary fields;
7. resumes, pauses, and observes old cursor stale;
8. saves state without changing epoch;
9. loads successfully and observes a new epoch;
10. exercises unavailable-by-config in a second runtime.

### Gate

No response is type-only matched. No history request can be completed by token-0
UI telemetry or another client request.

## Phase 7: markers and forensic dogfooding

### Tests first

Add focused success/failure marker tests at the runtime operation sites. A failed
file load, assembly, reset, or state load must not claim a successful marker.
Assert causal marker order for both mutation-then-reset and reset-then-mutation
paths, including partial success where only the first operation commits.

### Dogfood scenarios

Use at least three repository workloads:

1. Boot/control flow:
   - reset;
   - run to a known BASIC/KERNAL PC;
   - pause;
   - recover the path and reset marker.
2. Memory corruption:
   - run code that writes a selected RAM/I/O address;
   - find the last write/value;
   - read context and identify the responsible instruction.
3. Interrupt path:
   - trigger IRQ or NMI;
   - find vector/stack accesses;
   - recover handler entry sequence.

If an available title reproduces the motivating long boot, measure whether the
default arena retains the full ~1.6-million-instruction interval and document the
actual space used.

Record whether the recorder changes the number of debugging iterations compared
with TRON/breakpoint-only investigation. This is qualitative but is the product
payoff check.

## Phase 8: documentation reconciliation and final verification

### Update handoffs

- `agents/README.md`: list the implemented recorder handoff.
- `agents/runtime-control.md`: ownership, observer, payload pool, pause/search
  semantics, lifecycle.
- `agents/control-port.md`: C64M/3 grammar, binary HST1 decoder/example,
  errors, cursor behavior.
- `agents/machine.md`: observer contract and snapshot exclusion.
- `agents/testing.md`: new tests and benchmark commands.
- `agents/perf-baseline-turbo2.md`: matched off/on measurements and retention.
- This spec/plan: change status from design to implemented and reconcile any
  measured encoding/index deviations.

### Update user documentation

- Remove old 64-entry commands from `manual/manual.md`.
- Add configuration and remote API commands.
- Follow `manual/HELP_MARKDOWN.md` for any generated help artifacts.
- Update every in-repo script/example that claims C64M/2.

### Verification

Run focused tests throughout, then:

```text
ctest --test-dir build --output-on-failure
```

Run the serial performance matrix from Phase 0 after the correctness suite.
Do not run benchmark instances concurrently.

For machine observer changes, include the CPU/bus and PAL/NTSC regression groups
called out by `agents/testing.md`. If the observer code can affect access timing,
dogfood the existing lft-nine/EoD paths as appropriate; recording must be
observational only.

### Final acceptance checklist

- [x] Default 256 MiB recorder is available and recording when allocation works.
- [x] Allocation failure is nonfatal and visible through `history-info`.
- [x] Main 6510 only; no accidental drive/VIC event capture.
- [x] Actual instruction bytes and specified access kinds are retained.
- [x] Reset retains history with a new timeline.
- [x] Successful state load starts a new epoch; failed load/save do not.
- [x] No history is serialized.
- [x] Search requires explicit pause and never exposes ring pointers.
- [x] Result delivery is bounded, binary, token-matched, and leak-free.
- [x] Old commands are removed and all in-repo clients use C64M/3.
- [x] Enabled throughput loss is <=10%; the accepted matched result was ~7.4%.
- [x] Disabled regression is <=1% against the pre-feature runtime baseline.
- [x] Default capacity retains >=8 million idle-BASIC execution records.
- [x] Full-buffer search stays under the interactive ceiling.
- [x] Full CTest and relevant automated emulator regression groups pass (60/60).

## Deliberate follow-ups, not implementation spillover

After the recorder is used in practice, write separate briefs for:

1. Frontend history browser using the logical runtime query API.
2. Optional drive 8/9 history streams.
3. Time travel based on lightweight checkpoints plus deterministic replay.
4. Additional sealed-block indexes or aggregations justified by measured query
   workloads.

Do not silently expand the first implementation into any of these.
