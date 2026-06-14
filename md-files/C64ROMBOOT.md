# C64ROMBOOT.md

# Detailed Implementation Guide
# Phase 2 - ROM Integration and Minimal Boot Progression

## Purpose

This document is intended for an implementation agent.

The objective is to extend the Phase 1 memory/bus work into a minimal ROM-backed machine that can load real Commodore 64 ROM files, reset the CPU through the C64 memory map, and execute a controlled number of CPU steps from the KERNAL reset vector.

This phase must not assume that the full runtime, frame pipeline, VIC-II, CIA, SID, or display system exists.

The phrase "CPU begins executing real ROM code" means:

- ROM bytes are loaded into the machine-owned ROM arrays.
- The CPU reset vector is fetched through the C64 bus.
- The CPU program counter is initialized from that vector.
- A minimal stepping mechanism can execute a bounded number of CPU steps.
- Execution occurs on the runtime/emulation thread, not from frontend/UI code.

This is a bring-up phase, not a complete boot phase.

---

# Source Documents

This guide follows:

- HIGHLEVEL.md
- C64MEMBUS.md
- THREADS.md

Relevant principles:

- Bring the emulator up vertically, one demonstrable slice at a time.
- CPU memory access goes through the C64 bus.
- Runtime and live machine stay on the emulation thread.
- Frontend/UI must not touch live machine state.
- Runtime communication uses commands and events.
- Runtime publishes copied responses, not live pointers.

---

# Phase 2 Goal

Introduce ROM loading and controlled ROM execution.

At completion, the project should be able to:

1. Locate configured ROM files.
2. Load BASIC, KERNAL, and character ROM data.
3. Support both split ROM files and combined 16 KB C64C ROM files.
4. Reset the machine using the KERNAL reset vector.
5. Execute a small, bounded number of CPU instructions or cycles from ROM.
6. Report success/failure through runtime events or test output.
7. Preserve the threading and ownership model.

---

# Non-Goals

Do NOT implement:

- Full C64 boot to BASIC prompt
- VIC-II emulation
- CIA emulation
- SID emulation
- Keyboard matrix
- Display/frame generation
- Accurate timing
- Cycle-perfect scheduling
- Full debugger UI
- Full runtime speed control
- Cartridge support
- Disk/tape loading

This phase proves ROM-backed execution starts correctly. It does not need to complete startup.

---

# Architectural Rule

The live machine must be owned by runtime.

Allowed:

```text
UI/main thread
    -> runtime_client command
        -> runtime thread
            -> machine
                -> CPU
                -> bus
                -> RAM/ROM
```

Forbidden:

```text
frontend -> machine
main/UI  -> machine internals
runtime  -> SDL rendering
machine  -> runtime/frontend/platform
```

---

# Recommended File Scope

The agent should expect to touch or create files in these areas:

```text
src/machine/
    c64.c
    c64.h
    c64_bus.c
    c64_bus.h
    c64_rom.c
    c64_rom.h

src/runtime/
    runtime.c
    runtime.h
    runtime_command.c
    runtime_command.h
    runtime_event.c
    runtime_event.h
    runtime_client.c
    runtime_client.h
    runtime_thread.c
    runtime_internal.h

src/util/
    file_util.c
    file_util.h

assets/
    roms/
        README.md
        .gitignore
```

Adjust names to match the existing repository style.

Do not create circular dependencies.

---

# ROM Asset Layout

Use this repository layout:

```text
assets/
    roms/
        README.md
        .gitignore
        64c.251913-01.bin
        characters.901225-01.bin
```

The ROM binaries should normally not be committed.

`assets/roms/.gitignore`:

```gitignore
*.bin
```

`assets/roms/README.md` should explain that users must provide their own legally obtained ROM images.

---

# Supported ROM Inputs

## Combined C64C ROM

Support this file:

```text
64c.251913-01.bin
```

Expected size:

```text
16384 bytes
```

Layout:

```text
offset 0000-1FFF -> BASIC ROM,  A000-BFFF
offset 2000-3FFF -> KERNAL ROM, E000-FFFF
```

## Character ROM

Support this file:

```text
characters.901225-01.bin
```

Expected size:

```text
4096 bytes
```

Layout:

```text
offset 0000-0FFF -> character ROM, D000-DFFF when banked in
```

## Optional Split ROM Support

If easy, also support:

```text
basic.901226-01.bin      8192 bytes
kernal.901227-03.bin     8192 bytes
characters.901225-01.bin 4096 bytes
```

However, combined C64C ROM plus character ROM is sufficient for this phase.

---

# Internal ROM Representation

Regardless of physical file layout, keep internal ROM storage split:

```c
uint8_t basic_rom[0x2000];
uint8_t kernal_rom[0x2000];
uint8_t char_rom[0x1000];
```

The bus should not care whether BASIC and KERNAL came from one physical file or two files.

---

# Deliverable 1
# ROM Loader API

Create a small ROM loading API in the machine layer.

Suggested interface:

```c
typedef struct c64_rom_set {
    uint8_t basic[0x2000];
    uint8_t kernal[0x2000];
    uint8_t character[0x1000];

    bool has_basic;
    bool has_kernal;
    bool has_character;
} c64_rom_set;

bool c64_rom_load_combined_64c(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size);

bool c64_rom_load_character(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size);

bool c64_rom_load_split(
    c64_rom_set *roms,
    const char *basic_path,
    const char *kernal_path,
    const char *character_path,
    char *error,
    size_t error_size);
```

Use existing project naming conventions if different.

---

# Deliverable 2
# Strict Size Validation

ROM loading must validate exact file sizes.

Required checks:

```text
combined C64C ROM: 16384 bytes
BASIC ROM:          8192 bytes
KERNAL ROM:         8192 bytes
character ROM:      4096 bytes
```

Rules:

- Do not silently truncate.
- Do not silently pad.
- Return a useful error message.
- Failed ROM load must leave the machine in a known invalid/not-ready state.
- The runtime should report ROM load failures through `RUNTIME_EVENT_ERROR` or equivalent.

---

# Deliverable 3
# Machine ROM Installation

The machine should expose an install/load path that copies validated ROM data into the bus-owned ROM arrays.

Suggested API:

```c
bool c64_install_roms(
    c64_t *c64,
    const c64_rom_set *roms,
    char *error,
    size_t error_size);
```

Requirements:

- Copy ROM bytes into bus storage.
- Set ROM-present flags.
- Refuse reset/execution if required ROMs are missing.
- Keep ownership simple: after installation, the machine owns its ROM bytes.

---

# Deliverable 4
# ROM Path Configuration

Add the smallest practical ROM path configuration.

Possible initial approach:

```ini
[roms]
system = assets/roms/64c.251913-01.bin
character = assets/roms/characters.901225-01.bin
```

Alternative acceptable approach for early bring-up:

```text
--rom64c <path>
--charrom <path>
```

Recommendation:

- Prefer config keys if the existing config system is easy to use.
- Add CLI overrides only if consistent with current app option style.
- Do not overbuild ROM discovery.
- Do not search arbitrary host directories.

---

# Deliverable 5
# Minimal Machine Reset

Add a machine-level reset function.

Suggested API:

```c
bool c64_reset(
    c64_t *c64,
    char *error,
    size_t error_size);
```

Behavior:

1. Verify required ROMs are present.
2. Initialize RAM deterministically.
3. Initialize 6510 port state to the chosen startup state.
4. Reset CPU through the bus.
5. Fetch reset vector through bus reads at:

```text
FFFC
FFFD
```

6. Set CPU program counter from the fetched vector.

Success criterion:

```text
PC == little_endian(bus_read(FFFC), bus_read(FFFD))
```

The reset vector must come from KERNAL ROM visibility, not from raw ROM array access.

---

# Deliverable 6
# Minimal Machine Step API

Add a machine-level stepping function.

The exact shape depends on the adapted CPU core.

Acceptable options:

```c
bool c64_step_cycle(c64_t *c64);
```

or:

```c
bool c64_step_instruction(c64_t *c64);
```

or both, if the CPU core naturally supports both.

Requirement:

- All CPU memory reads/writes must go through the C64 bus callbacks.
- The machine step must not call frontend, SDL, or platform code.
- It must be safe to call from the runtime thread.

This is not yet the final timing model. It is the minimal stepping bridge needed for controlled ROM execution.

---

# Deliverable 7
# Runtime Commands for Bring-Up

Extend runtime command handling only as much as necessary.

Required commands:

```text
RESET
STEP_INSTRUCTION or STEP_CYCLE
REQUEST_CPU_STATE
QUIT
```

Optional but useful:

```text
RUN_FOR_INSTRUCTIONS
RUN_FOR_CYCLES
```

Avoid implementing unbounded RUN in this phase unless the runtime loop already supports safe pause/quit polling.

---

# Deliverable 8
# Runtime Events for Bring-Up

Runtime should publish copied results.

Required events/responses:

```text
STARTED
STOPPED
ERROR
RESET_COMPLETE
STEP_COMPLETE
CPU_STATE_RESPONSE
```

If the existing event enum does not have these exact names, use the nearest existing names or add narrowly scoped ones.

CPU state response should include at least:

```c
typedef struct c64_cpu_snapshot {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint64_t cycles;
} c64_cpu_snapshot;
```

Do not return a pointer to the live CPU.

---

# Deliverable 9
# Runtime Thread Behavior

The runtime thread owns the live machine.

For this phase, use this model:

```text
paused:
    wait for command

on RESET:
    load/install ROMs if not already loaded
    reset machine
    publish RESET_COMPLETE or ERROR

on STEP_INSTRUCTION:
    execute one bounded CPU instruction
    publish STEP_COMPLETE and/or CPU_STATE_RESPONSE

on STEP_CYCLE:
    execute one CPU cycle if supported
    publish STEP_COMPLETE and/or CPU_STATE_RESPONSE

on REQUEST_CPU_STATE:
    copy CPU state into event
    publish CPU_STATE_RESPONSE

on QUIT:
    stop loop
    publish STOPPED
```

Do not block forever when running a bounded step command.

Do not let the UI thread directly call `c64_step_*`.

---

# Deliverable 10
# Bounded ROM Execution Smoke Test

Add a smoke test or bring-up mode that performs:

```text
1. Start runtime.
2. Load ROM paths.
3. Send RESET.
4. Wait/poll for RESET_COMPLETE.
5. Request CPU state.
6. Verify PC equals KERNAL reset vector.
7. Send STEP_INSTRUCTION N times, where N is small.
8. Verify PC/cycle count changes.
9. Send QUIT.
10. Join runtime thread cleanly.
```

Suggested initial value:

```text
N = 1 to 20 instructions
```

Do not try to run to the BASIC screen yet.

The machine may hit unimplemented I/O quickly. That is acceptable if the failure is controlled and reported.

---

# Deliverable 11
# Error Handling

Errors must be explicit.

Examples:

```text
ROM file missing
ROM file has wrong size
BASIC ROM missing
KERNAL ROM missing
character ROM missing
reset vector could not be read
unsupported runtime command
CPU step failed
unimplemented I/O access if treated as fatal
```

Runtime errors should become runtime events.

During early bring-up, stdout logging is acceptable, but event reporting is still required for UI/runtime correctness.

---

# Deliverable 12
# Tests

Add tests at the lowest practical layer.

## ROM Loader Tests

- Combined 16 KB ROM splits correctly.
- Wrong combined ROM size fails.
- Character ROM 4 KB loads correctly.
- Wrong character ROM size fails.

Use generated test files where possible instead of real copyrighted ROMs.

## ROM Install Tests

- BASIC bytes reach bus BASIC ROM storage.
- KERNAL bytes reach bus KERNAL ROM storage.
- Character bytes reach bus character ROM storage.

## Reset Vector Test

Use generated ROM content:

```text
KERNAL offset 1FFC = low byte
KERNAL offset 1FFD = high byte
```

Then assert:

```text
bus read FFFC/FFFD returns those bytes
CPU PC equals that address after reset
```

## Runtime Step Test

If the CPU can execute a simple generated instruction stream:

```asm
NOP
NOP
NOP
```

place it at the reset vector target and step a bounded number of instructions.

Assert:

```text
PC advances
cycle count advances if available
runtime remains responsive
QUIT shuts down cleanly
```

---

# Minimal Implementation Order

Recommended order:

```text
1. Add assets/roms README and .gitignore.
2. Add ROM loader API with strict file-size validation.
3. Add combined 64C ROM split support.
4. Add character ROM load support.
5. Add c64_install_roms().
6. Add c64_reset() using bus reads for FFFC/FFFD.
7. Add c64_step_instruction() or c64_step_cycle().
8. Add CPU snapshot copy API.
9. Add runtime RESET command.
10. Add runtime STEP command.
11. Add runtime REQUEST_CPU_STATE command.
12. Add ROM loader tests using generated files.
13. Add reset-vector test using generated ROM content.
14. Add bounded runtime smoke test.
15. Confirm clean shutdown.
```

---

# Acceptance Criteria

Phase 2 is complete when:

- ROM files can be loaded from configured paths.
- Combined 16 KB C64C ROM is split into BASIC and KERNAL internally.
- Character ROM loads separately.
- Incorrect ROM sizes fail clearly.
- Machine refuses to reset without required ROMs.
- Reset vector is fetched through the bus from KERNAL ROM.
- CPU PC is initialized from that vector.
- Runtime owns the live machine.
- UI/frontend/main does not directly touch live machine state.
- A bounded step command can execute at least one CPU instruction or cycle.
- Runtime can return a copied CPU state snapshot.
- Runtime remains responsive after stepping.
- Shutdown is clean and joined.
- Tests pass.

---

# Important Boundary

Do not attempt to boot fully to BASIC in this phase.

This phase ends when the emulator can prove:

```text
real ROM bytes -> installed into machine -> reset vector fetched through bus -> CPU starts executing under runtime control
```

The next phases will add deeper CPU validation, timing, VIC-II, CIA, character rendering, and eventually visible BASIC startup.
