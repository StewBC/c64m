# C64MPHASE16.md

# Phase 16 - Timed Bus Events and Live VIC-II Raster Rendering

## Purpose

Move c64m from whole-frame VIC-II snapshot rendering toward option 3.a:

```text
Opcode-level CPU execution API
+ timestamped CPU bus events
+ machine-owned timed scheduler
+ VIC-II advances and renders as emulated time advances
```

This phase deliberately skips the public Phase 15 number because the ongoing VIC-II work
is considered Phase 15. Phase 16 is the next architecture phase that makes raster-time
video effects possible without requiring a full immediate rewrite to true cycle-
interleaved CPU/VIC/CIA/SID execution.

The goal is not yet a perfect cycle-interleaved emulator. The goal is to make
VIC-visible CPU writes happen at the correct cycle inside each instruction, and to make
VIC-II output respond as time advances rather than only when a final frame snapshot is
rendered.

## Source Basis

Read these first:

```text
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MVICII.md
5. C64VICPHASE_C.md, only as current VIC-II Phase C context
```

Important baseline facts from the latest snapshots:

- c64m is a C99 Commodore 64 emulator.
- The architecture boundary remains:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
```

- The live machine exists only on the runtime thread.
- Frontend receives copied snapshots only.
- Machine owns CPU, RAM, ROM, memory map, VIC-II, SID, CIA, interrupts, cycle timing,
  and bus behavior.
- Runtime owns the live machine and publishes copied snapshots/events to the UI.
- The VIC-II belongs to machine and produces host-side pixel snapshots; frontend only
  uploads/draws the resulting pixels.
- Current STATUS says Phase 14 is complete and the BASIC prompt is visible.
- Current STATUS also says VIC-II Bad Line detection, c-access line buffers,
  VC/VCBASE/RC, PAL/NTSC timing, RSEL/CSEL, XSCROLL/YSCROLL snapshot behavior, and
  ECM/BMM/MCM graphics-mode snapshot rendering are implemented.
- Current STATUS explicitly says exact BA/AEC/RDY cycle stealing is not implemented,
  sprite fetch BA events are not implemented, and per-cycle/mid-frame pixel rendering
  effects are not implemented.

## Design Decision

Implement 3.a now.

Do not implement 3.b as the main architecture.

Do not jump directly to 4.a in this phase.

Definitions:

```text
3.a = opcode step + timed bus events + VIC advances/draws as time advances
3.b = opcode step + timed bus events + replay logged raster events after the frame
4.a = true cycle-interleaved CPU/VIC/CIA/SID + VIC advances/draws as time advances
```

Rationale:

- 3.a fixes the main raster-effect defect: VIC-visible writes no longer take effect only
  after an instruction or only on the next whole-frame snapshot render.
- 3.a keeps the current public CPU instruction-step/debugger model intact.
- 3.a establishes the same machine scheduler shape that 4.a will need later.
- 3.b would create a second truth model: emulation happens now, rendering interprets logs
  later. That is useful as debugging instrumentation but should not become the main
  architecture.
- 4.a is the correct destination, but doing it now would combine CPU core restructuring,
  machine scheduler restructuring, VIC rendering restructuring, CIA timing, SID timing,
  debugger stepping, and breakpoint/watchpoint behavior in one high-risk phase.

The rule for this phase:

```text
Take the machine timing pain now.
Do not take the full CPU-core rewrite pain yet.
```

## End State

At the end of Phase 16:

- The machine has a single monotonic master cycle counter.
- The CPU can still be stepped one opcode at a time by runtime/debugger callers.
- The CPU opcode execution path exposes timestamped bus events for memory reads,
  memory writes, I/O reads, I/O writes, and internal/non-bus cycles.
- The machine scheduler advances VIC-II/CIA/SID placeholders to the cycle of each CPU
  bus event before applying that event.
- VIC-visible writes to $D000-$D3FF take effect at the event cycle, not after the full
  opcode completes.
- VIC-II has an incremental pixel/frame path that is driven by `vicii_step_cycle` or
  equivalent timed advancement.
- The current whole-frame `vicii_make_frame_snapshot` path is no longer the sole video
  truth for live running. It may remain as a compatibility/debug helper during the
  transition, but runtime-published frames must come from the live raster path.
- The design admits true cycle-interleaved CPU execution later by replacing the source
  of CPU events, not by rewriting the machine scheduler again.

## Non-Goals

Do not implement these in Phase 16 unless required to keep existing tests compiling:

- Hardware sprite display completeness.
- Sprite collision/priority completeness.
- Full sprite fetch BA timing.
- Light pen.
- Full open-bus/last-byte behavior.
- Full SID timing or audio output.
- Full CIA timer cycle perfection beyond advancing existing CIA state on the machine
  clock.
- A complete 6510 per-cycle public execution API.
- Demo-grade cycle perfection for every illegal opcode and bus quirk.

## Architecture Requirements

Follow AGENTS.md and MASTER.md.

Machine owns:

```text
master cycle
CPU bus event stream
memory map side effects
VIC-II timing and pixel generation
CIA/SID advancement hooks
BA/RDY/AEC state
completed host-side frame buffers
```

Runtime owns:

```text
run/pause/reset/step control
breakpoints/watchpoints
publishing copied snapshots
pacing/turbo behavior
```

Frontend owns:

```text
presentation only
SDL texture upload
Nuklear UI
copied snapshot consumption
```

Forbidden:

```text
frontend -> machine
machine -> runtime
machine -> frontend
machine -> platform
runtime -> frontend
runtime -> platform
live machine pointer crossing to UI thread
```

## Phase 16 Work Breakdown

### 16.1 - Introduce Machine Time Primitives

Add explicit machine-owned time primitives, preferably in `src/machine/c64.c` and
minimal declarations in `src/machine/c64.h` only if needed by runtime.

Recommended internal helpers:

```c
static void c64_advance_devices_to(c64 *m, uint64_t target_cycle);
static void c64_advance_one_cycle(c64 *m);
static uint64_t c64_current_cycle(const c64 *m);
```

Rules:

- `target_cycle` must never be less than the current machine cycle.
- Advancing one cycle must advance VIC-II exactly one dot/cycle unit according to the
  active PAL/NTSC timing model.
- CIA and SID may initially use placeholder cycle advancement if their current APIs are
  coarser, but calls should be placed now so later accuracy does not require changing
  scheduler shape.
- Frame completion is detected by the VIC-II live raster path, not by asking the old
  snapshot renderer to synthesize a frame from final register state.

Acceptance:

- Existing instruction-step and cycle-step UI commands still work.
- Machine cycle counts remain monotonic.
- PAL/NTSC line/frame totals remain selected from machine configuration.

### 16.2 - Define CPU Bus Event Records

Add a CPU bus event representation inside machine/CPU boundaries.

Suggested type:

```c
typedef enum c64_cpu_bus_event_kind {
    C64_CPU_BUS_EVENT_INTERNAL = 0,
    C64_CPU_BUS_EVENT_READ,
    C64_CPU_BUS_EVENT_WRITE
} c64_cpu_bus_event_kind;

typedef struct c64_cpu_bus_event {
    uint8_t cycle_offset;
    c64_cpu_bus_event_kind kind;
    uint16_t address;
    uint8_t value;
    uint8_t is_io;
} c64_cpu_bus_event;
```

The exact names can differ, but the information must be present.

Rules:

- `cycle_offset` is relative to the start of the opcode.
- Offset 0 means the first emulated CPU cycle of the instruction.
- Events must be ordered by cycle offset and then by their natural bus order.
- Reads must report the address and the value observed by the CPU.
- Writes must report the address and value at the cycle the write becomes visible on
  the bus.
- Internal cycles are included when useful for timing/stall accounting, but they do not
  touch memory.

Acceptance:

- A test opcode with a known store, such as `STA abs`, produces a write event at a
  stable documented offset.
- Branch, read-modify-write, stack, interrupt, and indexed addressing opcodes have a
  path to represent all bus-visible accesses, even if this phase initially starts with
  a minimal opcode subset and expands test coverage immediately after.

### 16.3 - Split CPU Execution From Bus Application

Refactor the current CPU opcode path so the machine can apply bus effects at event time.

The current code likely performs reads and writes directly through the machine bus while
executing an opcode. Phase 16 should move toward one of these shapes:

Preferred eventual shape:

```text
CPU core requests bus operations through callbacks.
Machine callback records timestamped bus events and supplies read values.
Machine scheduler applies side effects at the correct cycle.
```

Acceptable transitional shape:

```text
CPU opcode helper emits a precomputed event stream for bus-visible operations.
Machine scheduler advances devices to each event and applies that event.
CPU state commit remains at opcode end.
```

Rules:

- Do not allow a CPU write to `$D020`, `$D011`, `$D012`, `$D016`, `$D018`, `$D019`,
  `$D01A`, or any other VIC register to update VIC state before the scheduler reaches
  that event cycle.
- Do not publish a completed frame from an intermediate partially-applied instruction.
- Keep runtime breakpoints/watchpoints behavior stable. Runtime may still observe
  machine-reported access events, but those events must now be reported when the
  scheduler applies them, not after opcode completion.

Acceptance:

- A tight loop that alternates writes to `$D020` produces multiple timestamped VIC
  register writes inside the frame.
- Existing ROM boot and keyboard tests continue to pass.
- Existing breakpoint/watchpoint tests continue to pass or are deliberately updated to
  the new event timing without changing ownership boundaries.

### 16.4 - Add Timed Machine Scheduler for Opcode Execution

Replace the old conceptual model:

```text
execute opcode completely
advance VIC by elapsed cycles
```

with:

```text
start_cycle = machine.cycle
CPU produces opcode bus events and total_cycles
for each event:
    c64_advance_devices_to(start_cycle + event.cycle_offset)
    c64_apply_cpu_bus_event(event)
c64_advance_devices_to(start_cycle + total_cycles)
commit/finish instruction as needed
```

Suggested internal shape:

```c
static void c64_step_instruction_timed(c64 *m)
{
    uint64_t start = m->cycle;
    c64_cpu_instruction_trace trace;

    cpu_6510_execute_instruction_events(m->cpu, &trace);

    for(size_t i = 0; i < trace.event_count; i++) {
        uint64_t when = start + trace.events[i].cycle_offset;
        c64_advance_devices_to(m, when);
        c64_apply_cpu_bus_event(m, &trace.events[i]);
    }

    c64_advance_devices_to(m, start + trace.total_cycles);
}
```

Do not copy this literally if the existing CPU API suggests cleaner naming. The required
property is the ordering.

Acceptance:

- VIC register writes are applied at their event cycle.
- CIA register writes are applied at their event cycle.
- RAM/ROM/I/O visibility rules remain owned by the machine memory map.
- Instruction stepping still steps exactly one opcode from the user/debugger point of
  view.

### 16.5 - Convert VIC-II Rendering to Live Raster Advancement

Add a live raster frame buffer path to `src/machine/vicii.c`.

Current STATUS says the renderer is still a whole-frame snapshot using current register
state. Phase 16 changes that.

Recommended VIC-II state additions:

```c
typedef struct vicii_render_state {
    uint32_t pixels[384 * 272];
    uint8_t frame_ready;
    uint64_t frame_number;
    int pixel_x;
    int raster_y;
} vicii_render_state;
```

Use existing dimensions if the project already defines them. Do not invent a different
host output geometry unless required.

Rules:

- `vicii_step_cycle` advances the VIC raster counters and emits pixels/segments into
  the current frame buffer.
- Border/background/display decisions use the register values as of that exact cycle.
- `$D020` border color changes mid-line must affect pixels after the write cycle.
- `$D021` background changes mid-line must affect later background pixels.
- `$D011`/`$D016` mode/scroll/border changes must affect subsequent timing/rendering
  according to current implemented granularity.
- Existing Phase C graphics mode pixel functions should be reused for per-character or
  per-pixel output rather than duplicated as a second renderer.

Implementation advice:

- First extract pixel selection helpers from `vicii_make_frame_snapshot`.
- Then call those helpers from the live raster path.
- Leave `vicii_make_frame_snapshot` as a test/debug helper only if needed.
- Avoid maintaining two independently-evolving graphics-mode implementations.

Acceptance:

- A program that changes `$D020` in a raster loop produces visible horizontal raster
  bands or bars in the live frame output.
- A program that changes `$D021` mid-frame affects later pixels without waiting for the
  next frame.
- The BASIC startup screen still appears.
- PAL/NTSC output dimensions and frontend crop behavior remain compatible with the
  current frontend expectations.

### 16.6 - Integrate Bad Line BA/RDY Approximation With Event Timing

Current STATUS says BA is asserted on Bad Lines but the model does not distinguish CPU
read cycles from write cycles. Phase 16 should make the scheduler ready for correct
read/write discrimination.

Rules:

- BA state remains computed from VIC-II timing state.
- CPU event kinds must distinguish read, write, and internal cycles.
- If BA is low, read events are stall candidates.
- Write events continue when BA is low.
- If exact RDY behavior is too much for this phase, preserve the existing conservative
  behavior but route it through the new event kind structure so Phase H can become a
  refinement rather than a scheduler rewrite.

Recommended staged behavior:

```text
Phase 16 minimum:
    preserve existing Bad Line stall totals through the new scheduler
    classify CPU events as read/write/internal

Phase 16 preferred:
    stall only read events while BA is low
    allow write events to complete
    continue advancing VIC while CPU is stalled
```

Acceptance:

- Existing Bad Line c-access timing remains 40 cycles.
- Existing boot behavior does not regress.
- Tests can assert that CPU write cycles are distinguishable from read cycles at the
  scheduler interface.

### 16.7 - Frame Publication Path

Update the runtime frame publication path so published frames come from completed live
VIC-II buffers.

Rules:

- Runtime still publishes copied frame snapshots only.
- Frontend still consumes completed snapshots only.
- Frontend must not know whether the frame came from the old whole-frame renderer or the
  new live raster path.
- Runtime may drop older complete frames in turbo mode and publish the latest complete
  frame, preserving the existing UI contract.

Acceptance:

- UI displays the BASIC screen using the new live frame source.
- Turbo mode remains functional.
- Paused/debugger frame display remains stable.
- No live machine frame buffer pointer crosses to the UI thread.

### 16.8 - Tests and Diagnostics

Add focused regression tests rather than demo-scale validation first.

Required tests:

1. CPU bus event ordering

```text
Given a small memory program with known reads/writes,
when one instruction is stepped,
then emitted events have expected order and cycle offsets.
```

2. Timed VIC register write

```text
Given an instruction that writes $D020 at cycle offset N,
when the machine steps that instruction,
then VIC border color changes at start_cycle + N.
```

3. Mid-frame border color effect

```text
Given code that waits for a raster line and changes $D020,
when a frame is produced,
then pixels before and after the write differ on that raster line or following region.
```

4. Whole-frame fallback does not mask live timing

```text
Given a mid-frame color change,
when the frame is published,
then output is not equal to a final-register-only snapshot.
```

5. Existing smoke tests

```text
ROM boot reaches BASIC READY.
Keyboard input still works.
PAL/NTSC configuration still selects correct timing totals.
Existing debugger stepping still steps one opcode from the UI point of view.
```

Useful diagnostics:

- Optional trace mode for CPU bus events:

```text
cycle, pc, rw, address, value, vic_x, vic_y
```

- Optional trace mode for VIC raster writes:

```text
cycle, register, old_value, new_value, raster_x, raster_y
```

Do not expose these diagnostics to frontend unless a later debugger phase asks for them.
A simple test-only or logging-only path is enough.

## Files Likely Modified

Likely machine files:

```text
src/machine/c64.c
src/machine/c64.h
src/machine/cpu_6510.c
src/machine/cpu_6510.h
src/machine/vicii.c
src/machine/vicii.h
src/machine/cia.c
src/machine/cia.h
src/machine/sid.c
src/machine/sid.h
```

Likely runtime files:

```text
src/runtime/runtime.c
```

Only update runtime if frame publication or stepping calls require it. Do not move
machine timing truth into runtime.

Likely tests:

```text
tests/machine/test_c64_cpu.c
tests/machine/test_c64_vicii.c
tests/machine/test_c64_timing.c
```

Exact file names should follow existing project conventions.

## Migration Strategy

Use a vertical slice sequence.

### Slice 1 - Event Skeleton, No Visual Change

- Add event record type.
- Emit event records for a small set of opcodes used by tests.
- Scheduler applies events in timestamp order.
- Existing rendering may still be snapshot-based.

Exit criteria:

- Tests prove event order and offsets for selected opcodes.
- Existing boot tests still pass.

### Slice 2 - VIC Register Writes at Event Time

- Route writes to `$D000-$D3FF` through timed bus event application.
- Add test-only observability for cycle of VIC register mutation.

Exit criteria:

- `$D020` write is observed at the event cycle.
- Instruction-step still appears as one opcode externally.

### Slice 3 - Live Border/Background Raster Path

- Add live frame buffer.
- Render border/background per cycle or per pixel group as VIC advances.
- Keep display-layer content simple if needed.

Exit criteria:

- Mid-frame `$D020` changes are visible.
- BASIC screen still renders at least as border/background plus existing display where
  implemented.

### Slice 4 - Reuse Phase C Display Pixel Logic

- Extract current graphics-mode pixel selection from snapshot renderer.
- Use it from live raster rendering.

Exit criteria:

- Standard text and Phase C graphics modes render through live raster path.
- Existing Phase C tests are either reused or updated to validate the live path.

### Slice 5 - Runtime Frame Publication Switch

- Publish completed live VIC-II frames.
- Keep snapshot renderer only as debug/fallback if still needed.

Exit criteria:

- Frontend displays live frames.
- Turbo and paced modes still work.
- No thread ownership rules are violated.

### Slice 6 - BA/RDY Read/Write Foundation

- Classify CPU events as read/write/internal across the opcode set.
- Route Bad Line stalls through event classification.

Exit criteria:

- Existing conservative Bad Line behavior is preserved or improved.
- Scheduler is ready for Phase H exact BA/AEC/RDY behavior.

## Compatibility With Future 4.a

Phase 16 must leave this future replacement possible:

```text
Current Phase 16:
    CPU emits all events for one opcode
    machine scheduler consumes those events

Future 4.a:
    CPU emits or executes one cycle/event at a time
    same machine scheduler primitives consume that cycle/event
```

Therefore:

- Do not bake opcode-sized assumptions into VIC-II or CIA advancement.
- Do not make the renderer depend on instruction boundaries.
- Do not make runtime aware of CPU bus events.
- Do not make frontend aware of raster timing details.
- Keep `c64_advance_devices_to` and `c64_apply_cpu_bus_event` small and central.

## Acceptance Criteria

Phase 16 is complete when:

```text
- CPU instruction stepping still works as one opcode externally.
- Machine advances VIC-II/CIA/SID hooks to CPU bus event cycles before applying events.
- VIC-visible writes are applied at their event cycles.
- Live VIC-II raster rendering, not final-register snapshot rendering, produces the
  runtime-published frame.
- A mid-frame $D020 write visibly changes only subsequent pixels/scanline regions.
- BASIC startup screen still appears.
- Existing boot, keyboard, debugger stepping, breakpoint/watchpoint, PAL/NTSC, and
  Phase C graphics-mode tests pass or are updated for the new live-rendering truth.
- Architecture rules remain intact.
- Thread ownership rules remain intact.
- STATUS.md is updated to describe Phase 16 completion and remaining gaps.
```

## STATUS.md Update Template

When Phase 16 is complete, update STATUS.md with something like:

```text
- Phase 16 timed bus event and live VIC-II raster foundation:
  - machine owns a monotonic master cycle and advances devices to timestamped CPU bus
    events
  - CPU opcode stepping remains the external runtime/debugger API, but bus-visible
    reads/writes are classified and timestamped within each opcode
  - VIC-visible writes take effect at their event cycle rather than after opcode
    completion
  - VIC-II live raster rendering produces runtime-published frames
  - mid-frame border/background color changes are visible in frame output
  - current BA/RDY handling is routed through read/write event classification, with
    full sprite fetch BA/AEC accuracy still deferred
```

Remaining Not Implemented should still include, unless separately completed:

```text
- full hardware sprite display and timing if not completed
- sprite priority/collision detection
- light pen
- full open-bus / last-byte-on-bus behavior
- exact sprite fetch BA events
- complete true cycle-interleaved CPU/VIC/CIA/SID execution
- full SID/audio timing
```

## Practical Notes

This phase is expected to be invasive but bounded. The project should resist the urge to
complete every VIC-II feature while touching timing. The useful win is that `$D020`,
`$D021`, `$D011`, `$D016`, `$D018`, and mode/color changes can become raster-time
visible. That will make the emulator feel much closer to real C64 behavior while keeping
the CPU public stepping model and debugger behavior stable.

The most important implementation invariant is:

```text
No device observes a CPU bus side effect before machine time reaches that side effect.
```

The second most important invariant is:

```text
The renderer is not a replay system. The VIC-II renders because time advances.
```
