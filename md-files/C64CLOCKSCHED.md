# C64CLOCKSCHED.md

# Detailed Implementation Guide
# Phase 4 - Machine Clock and Scheduling

## Purpose

This document is intended for an implementation agent.

Phase 4 introduces the central machine timing model.

Earlier phases establish:

- CPU execution through the C64 bus
- RAM, ROM, and banking
- ROM loading and reset-vector execution
- Bounded stepping through runtime commands
- CPU validation inside the machine

Phase 4 changes the emulator from manually stepping isolated CPU work into a machine that owns time.

The purpose is to create the scheduling foundation that later allows CPU, VIC-II, CIA, SID, and runtime frame publication to advance in a coordinated way.

This phase does not require accurate VIC-II or CIA emulation yet.

It creates the clock and scheduling structure they will plug into.

---

# High-Level Goal

Create a central machine step loop.

The machine should expose a function like:

```c
bool c64_step_cycle(c64_t *c64);
```

Each call advances the emulated C64 by one master scheduling unit.

For this phase, the CPU may be the only meaningful device attached.

Future phases will attach:

- VIC-II
- CIA1
- CIA2
- SID
- frame generation
- interrupt sources

---

# Important Scope Boundary

This phase does not mean:

```text
start the app and immediately boot to BASIC
```

This phase means:

```text
runtime can run the machine continuously using a central scheduler
```

The machine may still fail or stall later when ROM code reaches unimplemented I/O.

That is acceptable.

The scheduler must be correct enough to support future device integration.

---

# Architectural Rule

The runtime thread owns live execution.

Allowed:

```text
runtime thread
    -> c64_run/scheduler
        -> c64_step_cycle()
            -> CPU
            -> bus
            -> future devices
```

Forbidden:

```text
frontend thread -> c64_step_cycle()
frontend thread -> live CPU
frontend thread -> live bus
frontend thread -> live RAM
frontend thread -> live ROM
```

Frontend or UI may only send commands and receive copied snapshots/events.

---

# Phase 4 Non-Goals

Do NOT implement:

- real VIC-II rendering
- real CIA timers
- SID audio
- keyboard matrix behavior
- frame publication
- full BASIC startup
- disk/tape/cartridge support
- cycle-perfect badlines
- raster interrupts
- fast-forward or throttling polish

These belong to later phases.

---

# Deliverable 1 - Machine Clock State

Add central timing state to the machine.

Suggested fields:

```c
typedef struct c64_clock {
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
} c64_clock;
```

The minimum required field is:

```c
uint64_t cycle;
```

This value represents the global emulated machine cycle count.

It must be deterministic and monotonic.

---

# Deliverable 2 - Central Step Function

Add:

```c
bool c64_step_cycle(c64_t *c64);
```

Requirements:

- May only be called by the runtime/emulation thread.
- Advances the global machine cycle count.
- Advances the CPU according to the current minimal CPU stepping model.
- Provides hooks for future devices.
- Does not call frontend, SDL, platform rendering, or UI code.
- Does not sleep.
- Does not throttle.
- Does not block.

This function is the future heart of the emulator.

---

# Deliverable 3 - CPU Scheduling Adapter

The existing CPU core may expose one of several models:

```text
step one instruction
step one CPU cycle
execute instruction and return cycle count
tick until current instruction completes
```

Phase 4 must adapt that model to the machine scheduler.

Preferred model:

```text
machine cycle -> CPU tick
```

If the CPU is currently instruction-based, use an adapter.

Example:

```c
if(cpu_cycles_remaining == 0)
{
    cpu_cycles_remaining = cpu_step_instruction(cpu);
}

cpu_cycles_remaining--;
clock.cycle++;
```

This is not fully cycle-accurate, but it creates a scheduler-compatible structure.

Later CPU work can replace the adapter without changing runtime ownership.

---

# Deliverable 4 - Device Hook Skeleton

Create placeholder hooks for future devices.

Suggested shape:

```c
static void c64_step_vic(c64_t *c64);
static void c64_step_cia1(c64_t *c64);
static void c64_step_cia2(c64_t *c64);
static void c64_step_sid(c64_t *c64);
```

For now these may do nothing except increment counters or remain empty.

The step order should be explicit and documented.

Example initial order:

```text
1. advance CPU adapter
2. advance VIC placeholder
3. advance CIA placeholders
4. advance SID placeholder
5. increment global machine cycle
```

If the chosen implementation increments the global cycle first, document that consistently.

---

# Deliverable 5 - Runtime Run/Pause State

Add runtime execution state.

Suggested enum:

```c
typedef enum runtime_exec_state {
    RUNTIME_EXEC_STOPPED,
    RUNTIME_EXEC_PAUSED,
    RUNTIME_EXEC_RUNNING
} runtime_exec_state;
```

Meaning:

- STOPPED: runtime thread is shutting down or not active
- PAUSED: runtime owns machine but does not advance it automatically
- RUNNING: runtime advances machine using the scheduler

Startup should normally enter PAUSED after machine creation/reset.

---

# Deliverable 6 - Runtime Commands

Add or finalize commands:

```text
RESET
STEP_CYCLE
STEP_INSTRUCTION
RUN
PAUSE
RUN_FOR_CYCLES
RUN_FOR_INSTRUCTIONS
REQUEST_CPU_STATE
REQUEST_MACHINE_STATE
QUIT
```

Required for Phase 4:

```text
RUN
PAUSE
STEP_CYCLE
RUN_FOR_CYCLES
REQUEST_MACHINE_STATE
QUIT
```

STEP_INSTRUCTION may remain from earlier phases for debugging.

RUN_FOR_INSTRUCTIONS is useful but not required if cycle stepping is available.

---

# Deliverable 7 - Runtime Main Loop

The runtime loop should process commands and advance the machine when running.

Pseudo-code:

```c
while(runtime->alive)
{
    runtime_process_pending_commands(runtime);

    if(runtime->exec_state == RUNTIME_EXEC_RUNNING)
    {
        c64_step_cycle(runtime->machine);
    }
    else
    {
        runtime_wait_for_command_or_timeout(runtime);
    }
}
```

Important:

- RUNNING mode must still poll commands frequently.
- PAUSE and QUIT must be responsive.
- Do not run an uninterruptible huge loop without command polling.
- Do not sleep inside c64_step_cycle().

---

# Deliverable 8 - Bounded Run Commands

Implement bounded run commands for deterministic tests.

Examples:

```text
RUN_FOR_CYCLES 1000
RUN_FOR_CYCLES 100000
```

Behavior:

```text
execute exactly N scheduler cycles unless error occurs
publish completion event
return to paused state
```

This is critical for tests and debugging.

Do not rely only on open-ended RUN.

---

# Deliverable 9 - Machine State Snapshot

Add a copied machine snapshot.

Suggested structure:

```c
typedef struct c64_machine_snapshot {
    uint64_t cycle;
    uint64_t cpu_cycles;

    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;

    bool running;
} c64_machine_snapshot;
```

Requirements:

- copy only
- no live pointers
- safe to send across runtime/frontend boundary
- generated on runtime thread

---

# Deliverable 10 - Error and Halt Handling

The scheduler must define what happens when execution cannot continue.

Examples:

- missing ROM
- invalid machine state
- CPU execution failure
- unimplemented fatal bus/device behavior
- runtime command error

Recommended behavior:

```text
1. stop automatic running
2. enter PAUSED or ERROR state
3. publish ERROR event
4. keep runtime responsive if possible
```

Do not crash the frontend for expected bring-up failures.

---

# Deliverable 11 - Autostart Policy

Do not make unconditional autostart the default unless the project explicitly wants that behavior.

Recommended default:

```text
app starts
runtime starts
machine loads/resets
runtime enters PAUSED
```

Optional config:

```text
autostart = true
```

If enabled:

```text
app starts
runtime starts
machine loads/resets
runtime receives RUN
```

For development, also support:

```text
run_for_cycles_on_start = N
```

This allows deterministic startup smoke tests without an infinite run loop.

---

# Deliverable 12 - Tests

Add tests for the scheduler and runtime behavior.

## Test 1 - Single Cycle Step

Setup:

- create machine
- install synthetic ROM
- reset
- record cycle count
- step one cycle

Expected:

- global cycle count increases by 1
- runtime remains paused if using STEP_CYCLE command

## Test 2 - Bounded Run

Run:

```text
RUN_FOR_CYCLES 1000
```

Expected:

- machine cycle count increases by 1000
- runtime returns to PAUSED
- completion event is published

## Test 3 - Run/Pause Responsiveness

Start RUN.

Then send PAUSE.

Expected:

- runtime stops advancing
- machine remains valid
- command is handled promptly

## Test 4 - Quit Responsiveness

Start RUN.

Then send QUIT.

Expected:

- runtime exits cleanly
- runtime thread joins
- no deadlock

## Test 5 - Snapshot During Pause

Pause runtime.

Request machine state.

Expected:

- copied snapshot returned
- no live machine pointers exposed

## Test 6 - Startup Default

Start app/runtime.

Expected:

- runtime is alive
- machine exists
- machine is not advancing unless autostart is enabled

---

# Recommended Implementation Order

1. Add machine clock fields.
2. Add c64_step_cycle().
3. Adapt CPU stepping to cycle scheduling.
4. Add placeholder device hooks.
5. Add machine snapshot API.
6. Add runtime execution state.
7. Add STEP_CYCLE command.
8. Add RUN_FOR_CYCLES command.
9. Add RUN command.
10. Add PAUSE command.
11. Ensure QUIT interrupts RUN promptly.
12. Add scheduler tests.
13. Add runtime responsiveness tests.
14. Add optional autostart config.

---

# Acceptance Criteria

Phase 4 is complete when:

- The machine has a central cycle counter.
- c64_step_cycle() exists and is the scheduler entry point.
- CPU execution is driven through the scheduler.
- Future device hooks exist.
- Runtime has explicit RUN and PAUSE states.
- Runtime can run continuously without blocking command handling.
- Runtime can execute a bounded number of cycles deterministically.
- Runtime can publish copied machine snapshots.
- Frontend does not touch live machine state.
- Startup defaults to paused unless autostart is explicitly configured.
- RUN, PAUSE, STEP_CYCLE, RUN_FOR_CYCLES, REQUEST_MACHINE_STATE, and QUIT are tested.
- Runtime shutdown is clean.

---

# End State

At the end of Phase 4, the emulator has a real execution spine:

```text
runtime thread
    -> scheduler
        -> machine cycle
            -> CPU
            -> future VIC-II
            -> future CIAs
            -> future SID
```

The machine may not yet produce frames.

The machine may not yet complete BASIC startup.

But it now has the timing and execution structure needed for VIC-II, CIA, and frame publication work.
