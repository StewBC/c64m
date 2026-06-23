# Cycle-Boundary Stepping Experiment Notes

Branch: `experiment/cycle-boundary-stepping`

## Phase 1: Current Timing Responsibilities

### Commands Run In Phase 1

- `git status --short --branch`
- `git branch --show-current`
- `rg --files`
- `git checkout -b experiment/cycle-boundary-stepping`
- `rg -n "#define CYCLE|\bCYCLE\(|cpu\.cycles|cycles" src/machine src/runtime tests | head -n 240`
- `rg -n "vicii_|VIC|raster|badline|BA|aec|frame|catch|advance|step|cycles" src/machine src/runtime tests/machine tests/runtime | head -n 260`
- `rg -n "sid_|SID|audio|sample|clock|cycle|advance|step" src/machine src/runtime src/platform tests tools md-files/STATUS.md | head -n 260`
- `rg -n "cia_|CIA|timer|TOD|irq|nmi|edge|cycle|advance|step" src/machine src/runtime tests/machine tests/runtime md-files/STATUS.md | head -n 320`
- `nl -ba src/machine/c6510_inln.h | sed -n '1,220p'`
- `nl -ba src/machine/c6510.c | sed -n '1,260p'`
- `nl -ba src/machine/c64.c | sed -n '1,140p'`
- `nl -ba src/machine/c64.c | sed -n '600,840p'`
- `nl -ba src/machine/c64.c | sed -n '1040,1128p'`
- `nl -ba src/machine/vicii.c | sed -n '1,260p'`
- `nl -ba src/machine/vicii.c | sed -n '260,620p'`
- `nl -ba src/machine/vicii.c | sed -n '620,980p'`
- `nl -ba src/machine/cia.c | sed -n '1,360p'`
- `nl -ba src/machine/cia.c | sed -n '360,760p'`
- `nl -ba src/machine/sid.c | sed -n '360,470p'`
- `nl -ba src/machine/c64_bus.c | sed -n '1,360p'`
- `nl -ba src/runtime/runtime_thread.c | sed -n '1,240p'`
- `nl -ba src/runtime/runtime_thread.c | sed -n '240,560p'`
- `nl -ba src/runtime/runtime_thread.c | sed -n '700,1040p'`
- `nl -ba src/runtime/runtime_thread.c | sed -n '1320,1435p'`
- `nl -ba src/runtime/runtime_thread.c | sed -n '2840,2940p'`
- `nl -ba tests/runtime/test_runtime_scheduler.c | sed -n '121,160p;420,526p'`
- `nl -ba tests/runtime/test_runtime_scheduler.c | sed -n '526,552p'`
- `nl -ba tests/machine/test_c64_cpu_validation.c | sed -n '360,540p'`
- `nl -ba tests/machine/test_c64_cia.c | sed -n '100,280p;650,780p'`
- `nl -ba tests/machine/test_c64_cia.c | sed -n '772,805p;940,977p'`
- `nl -ba tests/machine/test_c64_vicii.c | sed -n '160,280p;1000,1230p'`
- `rg -n "c64_step_cycle|c64_step_instruction|runtime_audio_advance_cycle|RUN_BATCH|run_cycles|next_frame_cycle|copy_completed_frame|publish_frame" src/runtime/runtime_thread.c src/runtime/runtime.c tests/runtime tests/machine | head -n 240`
- `mkdir -p notes`
- `git status --short --branch`
- `git diff -- notes/cycle-boundary-stepping.md`

No build, test, benchmark, or emulator run command was executed in Phase 1.

### Summary

The repo already has a machine-level per-cycle boundary in `src/machine/c64.c`, separate from the CPU core's `CYCLE(m)` macro. `CYCLE(m)` currently increments only `m->cpu.cycles` inside the 6510 core. Machine devices are advanced by `c64_advance_one_cycle()`, which steps VIC-II, CIA #1, CIA #2, and SID once, then increments `machine->clock.cycle`.

This means the question is not "should devices become per-cycle?" in general; they already are per machine cycle. The real experiment would be whether the CPU core's internal `CYCLE(m)` should also become the canonical machine-cycle boundary, replacing the current deferred/timed bus synchronization layer.

### CPU Timing And Bus Event Model

- `src/machine/c6510_inln.h` defines `CYCLE(m)` as `do { (m)->cpu.cycles++; } while(0)`.
- The CPU core calls memory read/write callbacks before `CYCLE(m)` in the helper routines, so the macro currently behaves like an end-of-CPU-bus-cycle accounting point.
- `c6510_step()` returns `m->cpu.cycles - start_cycle`, giving instruction duration in CPU cycles.
- `c64_step_instruction()` runs the CPU with `C64_CPU_BUS_MODE_TIMED_IMMEDIATE`. Before each CPU bus read/write, the bus callback advances devices to `cpu_trace_start_cycle + current_cpu_cycle_offset`. After the instruction, it advances devices to `start_cycle + total_cycles`.
- `c64_step_cycle()` runs a deferred trace model. It first executes the full CPU instruction into a trace without mutating writes, then applies bus events cycle by cycle while device cycles advance. This allows BA stalls to hold pending reads while still letting writes complete.

### Current Per-Cycle Device Work

`c64_advance_one_cycle()` performs this order:

1. `vicii_step_cycle(&machine->vic, &machine->bus, machine->clock.cycle)`
2. `cia_step_cycle(&machine->cia1)`
3. `cia_step_cycle(&machine->cia2)`
4. `sid_advance_cycles(&machine->sid, 1)`
5. `machine->clock.cycle++`

Counters:

- `clock.vic_cycles` increments after VIC step.
- `clock.cia_cycles` increments after CIA #2 only, so it counts one shared CIA cycle per machine cycle, not one per CIA chip.
- `clock.cpu_cycles` increments only when the CPU is allowed to advance; BA-stalled read cycles advance `clock.cycle` but not `clock.cpu_cycles`.

### VIC-II

VIC-II is advanced exactly one machine cycle at a time through `vicii_step_cycle()`.

Responsibilities inside the step:

- start-of-line raster IRQ compare;
- bad-line detection and display state setup;
- sprite fetch activation at cycle 0;
- bad-line BA assertion at cycle 12;
- c-access fetches during cycles 15-54;
- PAL sprite BA windows;
- live pixel rendering for the current cycle;
- line/frame counter advancement and completed-frame handoff.

CPU-visible timing dependencies:

- `vicii_ba_active(v, machine->clock.cycle)` is checked before starting or advancing a deferred CPU trace.
- BA stalls pending CPU reads and internal cycles, but pending writes are allowed to complete.
- VIC register reads expose current raster state, IRQ status, and read-clear sprite collision registers.
- VIC writes are applied by the bus at the CPU event cycle; tests assert a `$D020` write absolute cycle.

### SID And Audio

SID chip state is advanced once per machine cycle by `sid_advance_cycles(&machine->sid, 1)` from `c64_advance_one_cycle()`.

SID responsibilities inside the step:

- voice phase advancement;
- oscillator sync/ring-related source state;
- ADSR envelope advancement;
- voice 3 oscillator/envelope read-back;
- optional waveform/mixer/filter/output conditioning when sample output is enabled.

Runtime audio scheduling is separate from SID advancement:

- `runtime_step_cycle()` calls `c64_step_cycle()`, then `runtime_audio_advance_cycle()`.
- `runtime_audio_advance_cycle()` samples `sid_sample(&rt->machine.sid)` after each completed C64 cycle, accumulates per-cycle SID samples, and emits host samples at PAL/NTSC fractional sample deadlines.
- `runtime_step_instruction()` and `runtime_run_instructions()` call `c64_step_instruction()` directly and do not advance runtime audio per individual machine cycle. Free-running and run-cycles mode use the cycle path.

### CIA

Both CIAs are advanced once per machine cycle by `cia_step_cycle()`.

Responsibilities inside the step:

- clear one-cycle PB output pulse state from the previous cycle;
- step Timer A;
- step Timer B, including Timer A cascade modes;
- step TOD at configured 50/60 Hz cycle cadence;
- clear the one-cycle CNT pulse latch.

CPU-visible timing dependencies:

- CIA register reads can have side effects. ICR reads increment `icr_reads`, return pending enabled flags with bit 7, clear reported interrupt flags, and clear timer underflow latches.
- TOD hour reads latch coherent TOD state; tenths reads release the latch.
- CIA #1 IRQ and CIA #2 NMI pending callbacks are queried by the CPU before instruction execution.
- CIA #2 port A / DDRA writes refresh the cached VIC bank base through the bus.

### Runtime Cycle Path

- Free running mode processes up to `RUNTIME_RUN_BATCH_CYCLES` machine cycles per outer loop, but each iteration calls `runtime_step_cycle()`.
- `runtime_step_cycle()` calls `c64_step_cycle()` then runtime audio scheduling.
- Completed frames are published after `c64_consume_frame_complete()` reports a frame boundary.
- `runtime_run_cycles()` also advances by repeated `runtime_step_cycle()` calls.

### Delta/Catch-Up Work

The notable catch-up path is not a batch device model; it is a synchronization helper:

- `c64_advance_devices_to(target_cycle)` loops over `c64_advance_one_cycle()` until the machine reaches a CPU bus-event target.
- `c64_step_instruction()` uses this to align devices before CPU-visible bus reads/writes and after full instruction completion.

No current VIC/CIA/SID path appears to advance by a large mathematical delta. They are all per-cycle loops today.

### Before/After CPU-Visible Side Effects

Current ordering from the code:

- For `c64_step_instruction()` timed immediate mode, devices advance to the CPU event's cycle offset before the CPU-visible bus read or write is applied.
- For `c64_step_cycle()` deferred mode, pending CPU events at the current elapsed cycle are applied before `c64_advance_one_cycle()`. That means the CPU-visible side effect is applied, then VIC/CIA/SID run for that machine cycle.
- For BA in deferred mode, the current machine cycle's already-computed VIC BA window decides whether the CPU may advance. BA-stalled read/internal cycles still advance devices and master machine cycle.

This split is the main ordering ambiguity for any `CYCLE(m)` migration: instruction stepping and cycle stepping preserve slightly different implementation shapes, even though tests expect their visible results to agree.

### Existing Test Coverage Relevant To Timing

- CPU trace event offsets and `$D020` event-cycle timing are covered in `tests/machine/test_c64_cpu_validation.c`.
- BA behavior is covered for bad lines, sprite windows, read stalls, and write allowance.
- VIC raster progression, IRQ status, read-clear collision registers, bad-line BA, sprite BA windows, frame/live rendering are covered in `tests/machine/test_c64_vicii.c`.
- CIA timer, PB output, CNT/cascade, TOD cadence/latch/alarm, ICR read side effects, machine-level CIA stepping, IRQ/NMI routing, and CPU-visible ICR timing are covered in `tests/machine/test_c64_cia.c`.
- SID per-cycle advancement and audio-flow behavior are covered in `tests/machine/test_sid.c`.
- Runtime run-cycles, single-cycle stepping, and audio sample scheduling/no batch-hold behavior are covered in `tests/runtime/test_runtime_scheduler.c`.

### Phase 1 Conclusions

- Devices currently advanced per CPU/machine cycle: VIC-II, CIA #1, CIA #2, SID chip state.
- Devices currently advanced by delta/catch-up: none of VIC/CIA/SID in a true bulk-delta sense; catch-up loops are per-cycle and used to align devices to CPU bus-event timestamps.
- Updates that must happen before CPU-visible bus read/write: in timed instruction stepping, device catch-up to the event cycle happens before the bus access. This is required for raster/IRQ/timer state visible to reads and for correctly timestamped writes.
- Updates that must happen after CPU-visible side effects: in deferred cycle stepping, CPU bus events for the elapsed cycle are applied before the device cycle advances. This supports event-cycle writes such as `$D020` and read/write BA behavior.
- Good immediate candidate for a `CYCLE(m)` migration: none without first resolving the ordering split. SID chip stepping is the lowest bus-order risk, but audio scheduling still lives in runtime and must remain after completed machine cycles.
- Highest-risk candidate: VIC-II, because BA windows, bad lines, sprite DMA, frame rendering, and CPU bus event timing are tightly coupled to `machine->clock.cycle` and already covered by timing tests.

## Phase 2: Can `CYCLE(m)` Become The Machine-Cycle Boundary?

### Commands Run In Phase 2

- `git status --short --branch`
- `sed -n '1,260p' notes/cycle-boundary-stepping.md`
- `rg -n "#define CYCLE|read_from_memory\\(|write_to_memory\\(|CYCLE\\(m\\)|c64_advance_one_cycle|c64_step_instruction|c64_step_cycle_internal|c64_cpu_read|c64_cpu_write|vicii_ba_active|cia_irq_pending|nmi_pending|runtime_audio_advance_cycle" src/machine src/runtime tests/machine tests/runtime`
- `nl -ba src/machine/c64.c | sed -n '739,860p;1048,1094p'`
- `nl -ba src/machine/c6510_inln.h | sed -n '500,545p;1138,1200p'`
- `nl -ba src/machine/sid.c | sed -n '40,150p;383,490p'`
- `nl -ba tests/machine/test_c64_cpu_validation.c | sed -n '396,526p'`
- `nl -ba tests/machine/test_c64_cia.c | sed -n '753,805p;940,972p'`
- `nl -ba src/machine/c6510_inln.h | sed -n '460,503p'`
- `nl -ba src/machine/c64.c | sed -n '80,121p;719,737p'`
- `nl -ba src/runtime/runtime_thread.c | sed -n '258,303p;1337,1348p;2858,2922p'`
- `nl -ba tests/runtime/test_runtime_scheduler.c | sed -n '449,552p'`
- `rg -n "sid_register|write_sid_register|sid_sample|voice3|advance" tests/machine/test_sid.c tests/runtime/test_runtime_scheduler.c | head -n 120`
- `rg -n "c64_try_kernal_load_trap|KERNAL_LOAD|clock\\.cycle\\+\\+|clock\\.cpu_cycles\\+\\+" src/machine/c64.c tests/machine/test_c64_disk_load.c`
- `nl -ba src/machine/c64.c | sed -n '520,604p'`
- `nl -ba tests/machine/test_c64_disk_load.c | sed -n '292,378p'`

No build, test, benchmark, or emulator run command was executed in Phase 2.

### Current `CYCLE(m)` Semantics

`CYCLE(m)` is post-CPU-bus-cycle accounting. It is not the bus access point itself and is not a machine-cycle boundary.

Evidence:

- CPU helpers call `read_from_memory()` or `write_to_memory()` first, then call `CYCLE(m)`.
- Opcode fetch in `c6510_step()` also reads memory first, then calls `CYCLE(m)`.
- Store helpers such as `sta_a16()` write to the bus first, then call `CYCLE(m)`.
- Interrupt entry helpers perform their dummy/opcode/stack bus actions and call `CYCLE(m)` after those actions.

Therefore the macro means: "the CPU core has completed one 6510 bus/internal cycle; increment the CPU core's private cycle counter."

Important caveat: during `C64_CPU_BUS_MODE_DEFER_WRITES`, `c6510_step()` runs the whole instruction early to build a bus-event trace. In that mode, `CYCLE(m)` advances `machine->cpu.cpu.cycles` and records offsets, but real machine time has not advanced yet. This alone prevents `CYCLE(m)` from directly becoming the machine-cycle boundary without a larger CPU stepping redesign.

### Current `c64_advance_one_cycle()` Semantics

`c64_advance_one_cycle()` is the current machine-level device cycle advance. It means: "advance VIC-II, CIA #1, CIA #2, and SID for the current absolute machine cycle, then commit `machine->clock.cycle++`."

Its order is:

1. step VIC-II using the current `machine->clock.cycle`;
2. step CIA #1;
3. step CIA #2;
4. step SID by one cycle;
5. increment the master machine cycle.

In normal cycle stepping, the CPU bus event for elapsed cycle N is applied before `c64_advance_one_cycle()` advances devices for machine cycle N. In instruction stepping, `c64_advance_devices_to(start + N)` advances devices for all cycles before event N, then the CPU bus access for event N occurs; the later catch-up advances devices for cycle N. Those are equivalent for non-stalled cycles if interpreted as:

```text
CPU-visible event for cycle N happens before devices advance/render/clock cycle N.
```

However, they are not fully equivalent execution paths. `c64_step_cycle()` owns BA/RDY-style stalls. `c64_step_instruction()` catches devices up to CPU bus-event timestamps but does not model BA-stalled reads during the instruction it executes.

There is also a fast KERNAL load trap exception: both cycle and instruction stepping increment `clock.cycle` and `clock.cpu_cycles` for the synthetic trap without calling `c64_advance_one_cycle()`. Any future alignment assertions must either exclude that path or make the trap advance devices deliberately.

### Required Ordering Analysis

CPU-visible VIC register reads and writes:

- Reads go through `c64_bus_read()` to `vicii_read_register()`. Timed instruction stepping catches devices up to the read event before the read; cycle stepping applies the pending read before advancing the current device cycle.
- Writes go through `c64_bus_write()` to `vicii_write_register()`. Writes are applied at the CPU bus-event cycle before the device cycle for that absolute cycle runs.

`$D020` / border-color write event-cycle tests:

- `test_sta_d020_applies_at_event_cycle` asserts the `$D020` write event has absolute cycle `start_cycle + 3`, and the instruction ends at `start_cycle + 4`.
- This matches the event-before-device-cycle model: the write belongs to cycle 3, and device/frame work for cycle 3 must see the new register value.

Badline BA stalls:

- `vicii_step_cycle()` asserts bad-line BA during the VIC step for cycle 12, extending the BA-low window into later absolute cycles.
- `c64_step_cycle_internal()` checks `vicii_ba_active(&machine->vic, machine->clock.cycle)` before preparing or advancing CPU work.
- This requires machine device advancement to be able to continue while CPU cycle advancement is withheld.

Sprite BA stalls:

- PAL sprite BA windows are generated inside `vicii_step_cycle()` based on the current raster cycle and absolute cycle.
- The CPU stall predicate is the same `vicii_ba_active()` check, so sprite BA has the same requirement: device cycles may advance without CPU cycles.

CPU reads stalled by BA:

- `c64_cpu_cycle_stalled_by_ba()` stalls pending read events when BA is active.
- The pending event is not applied, `pending_cpu_elapsed` does not advance, and `clock.cpu_cycles` does not advance.
- `c64_advance_one_cycle()` still advances machine/device time.

CPU writes allowed during BA:

- `c64_cpu_cycle_stalled_by_ba()` explicitly allows a pending write event when BA is active.
- The write is applied, then devices advance for that machine cycle, and CPU elapsed/cycle counters advance.

CIA ICR read side effects:

- `cia_read_register()` for ICR counts the read, returns enabled pending flags, clears reported interrupt flags, and clears timer underflow latches.
- `test_cia_icr_read_clears_at_cpu_bus_cycle` verifies the ICR is not read during opcode/address fetch cycles and clears exactly on the data bus cycle.
- A `CYCLE(m)` hook cannot move this side effect earlier or later without changing observable interrupt behavior.

CIA IRQ/NMI pending checks before instruction execution:

- `c6510_step()` samples NMI first, then IRQ, before opcode fetch.
- `c64_cpu_irq_pending()` reads CIA #1 and VIC IRQ state.
- These checks are CPU instruction-entry checks, not per-machine-cycle device checks.

CIA #2 NMI edge behavior:

- `c64_cpu_nmi_pending()` detects a CIA #2 rising edge using `machine->cia2_nmi_line`, updates that latch, and clears RESTORE pending state.
- Calling this from a generic machine-cycle hook would be wrong; it must remain tied to CPU NMI sampling points.

SID register writes versus SID per-cycle advancement:

- SID writes update registers immediately when the CPU bus event is applied.
- `sid_advance_cycles(&machine->sid, 1)` then uses the new state when the device cycle advances.
- Moving SID advancement into `CYCLE(m)` risks double-stepping unless the existing `c64_advance_one_cycle()` path is gated. It also does not solve anything by itself because SID is already per-machine-cycle.

Runtime audio sampling after completed machine cycles:

- `runtime_step_cycle()` calls `c64_step_cycle()` and then `runtime_audio_advance_cycle()`.
- `runtime_audio_advance_cycle()` samples `sid_sample()` after the completed machine cycle and emits host audio at fractional cycle deadlines.
- This must stay in `runtime/`, not the CPU core or SDL callback. A CPU-core `CYCLE(m)` hook would need a separate post-machine-cycle notification to preserve audio behavior.

### Can The Concepts Be Unified?

Not safely as a direct Phase 3 migration.

The CPU macro and machine boundary encode related but different layers:

- `CYCLE(m)` is CPU-local post-cycle accounting and trace offset generation.
- `c64_advance_one_cycle()` is real machine/device time advancement.
- In deferred trace mode, CPU cycles are computed before real machine cycles happen.
- In BA stalls, real machine cycles advance while CPU cycles intentionally do not.
- NMI edge sampling is tied to CPU instruction entry, not every machine cycle.
- Runtime audio sampling is tied to completed runtime machine cycles, not CPU-core accounting.

A full unification would require a new CPU execution model that can yield before/after each bus event, accept RDY/BA stalls before advancing CPU state, and notify the C64 wrapper after completed machine cycles. That is an architecture migration, not a small experiment.

### Smaller Useful Change

The safe Phase 3 candidate is a non-behavioral boundary clarification and instrumentation prototype:

- Keep `c64_advance_one_cycle()` as the machine/device boundary.
- Keep `CYCLE(m)` CPU-local.
- Replace the raw macro body with a named inline helper such as `c6510_complete_cycle()` or `c6510_account_cycle()` to document that it only advances the CPU core counter.
- Rename or comment `c64_advance_one_cycle()` to make clear that it advances one machine/device cycle after the current CPU-visible event.
- Add debug assertions around non-trap paths that check `clock.vic_cycles`, `clock.cia_cycles`, and `clock.cycle` remain aligned after device advancement.
- Add or strengthen tests comparing instruction-step and cycle-step ordering for CPU-visible side effects where applicable, while documenting that BA stalls are cycle-step authoritative.

### Recommendation

Do not migrate `CYCLE(m)` to call the machine-cycle boundary.

Proceed only with a narrow Phase 3 clarification/instrumentation prototype if more work is desired. The useful result would be better names, comments, assertions, and ordering tests, not changed timing behavior.

### Expected Risk

- Full `CYCLE(m)` migration: high risk. Likely to break BA stalls, deferred trace stepping, CIA ICR timing, NMI edge sampling, and runtime audio scheduling unless the CPU core is redesigned.
- Boundary clarification/instrumentation: low risk if assertions avoid the KERNAL load trap exception and remain debug-only or test-only.

### Expected Performance Impact

- Full migration: no clear upside. Devices are already per-cycle, so the best case removes little or no work; the likely case adds callback/gating overhead and risks double-stepping.
- Clarification/instrumentation: release performance should be unchanged if assertions are debug-only. Tests may gain small overhead if extra checks are added.

### Expected Accuracy Impact

- Full migration: likely accuracy regression unless BA/RDY and bus-event ordering are redesigned first.
- Clarification/instrumentation: preserves current accuracy; may expose existing edge-case ambiguity, especially around instruction-step behavior during BA windows and the KERNAL load trap's skipped device advancement.

### Phase 3 Files/Functions If Proceeding

- `src/machine/c6510_inln.h`: replace the raw `CYCLE(m)` macro body with a named inline CPU accounting helper, leaving behavior unchanged.
- `src/machine/c64.c`: clarify `c64_advance_one_cycle()`, `c64_advance_devices_to()`, `c64_step_cycle_internal()`, and the KERNAL load trap exception.
- `tests/machine/test_c64_cpu_validation.c`: add/strengthen event-order equivalence tests for instruction-step versus cycle-step where BA is not involved.
- `tests/machine/test_c64_cia.c`: add/strengthen CIA ICR ordering tests for instruction-step final behavior and cycle-step per-cycle behavior.
- `tests/runtime/test_runtime_scheduler.c`: keep runtime audio sample-count and no-batch-hold tests as required guards.

### Tests Required After Any Phase 3 Prototype

- `cmake --build build`
- `ctest --test-dir build`
- Targeted machine tests covering CPU trace/event cycles, VIC-II BA windows, CIA ICR/TOD/IRQ/NMI, SID, and runtime scheduler/audio.
- Hot-loop benchmark only if any production code path changes beyond comments/tests/debug-only assertions.
