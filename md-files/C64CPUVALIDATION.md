# C64CPUVALIDATION.md

# Detailed Implementation Guide
# Phase 3 - CPU Validation Inside The Machine

## Purpose

Phase 1 established memory ownership and banking.
Phase 2 established ROM loading, runtime ownership, reset, and bounded execution.

Phase 3 validates that the CPU behaves correctly inside the actual C64 machine environment.

The goal is to prove that:

CPU -> Bus -> RAM/ROM/Banking

operates correctly as a complete system.

This phase focuses on validation, confidence building, and test infrastructure before timing, VIC-II, CIA, and display work begins.

---

# Goals

## Primary Goal

Establish confidence that the CPU executes correctly through the C64 bus.

## Secondary Goal

Create reusable machine-level validation infrastructure that future phases can continue using.

---

# Non-Goals

Do NOT implement:

- VIC-II
- CIA timers
- SID
- Raster logic
- Keyboard matrix
- BASIC startup completion
- Character rendering
- Frame generation

This phase is validation, not feature development.

---

# Architectural Rules

Every test in this phase must exercise:

CPU
 -> bus
 -> memory map

Forbidden:

- CPU directly reading RAM arrays
- CPU directly reading ROM arrays
- CPU bypassing the machine layer

Allowed:

- CPU reset through machine APIs
- CPU execution through bus callbacks
- CPU vector fetches through bus reads

---

# Deliverable 1 - Validation Harness

Create a dedicated machine validation harness.

Capabilities:

- Create machine
- Install synthetic ROM contents
- Reset machine
- Execute bounded instructions
- Capture CPU snapshots
- Validate expected outcomes

This harness should become the foundation for future emulator validation.

---

# Deliverable 2 - CPU Snapshot API

Create a reusable CPU snapshot structure.

Suggested contents:

- PC
- A
- X
- Y
- SP
- P
- Cycle count

Requirements:

- Copy only
- No live pointers
- Safe for runtime and tests

---

# Deliverable 3 - Deterministic Machine Builder

Create helpers that:

- Allocate machine
- Allocate bus
- Install synthetic ROMs
- Configure reset vectors
- Return a reset-ready machine

Tests should not require real ROM files.

---

# Deliverable 4 - Reset Validation

Test reset-vector behavior.

Example:

FFFC = 00
FFFD = E0

Expected:

PC = E000

Requirements:

- Reset vector fetched through bus
- No direct ROM access
- Failure if banking is broken

---

# Deliverable 5 - Instruction Fetch Validation

Install:

E000: NOP
E001: NOP
E002: NOP

Execute three instructions.

Expected:

PC = E003

All opcode fetches must travel through the bus.

---

# Deliverable 6 - Memory Access Validation

Example program:

LDA #$55
STA $1234
LDA $1234

Expected:

RAM[1234] = $55
A = $55

Verify that reads and writes occur through machine memory paths.

---

# Deliverable 7 - Stack Validation

Validate:

PHA
PLA

and multi-entry stack sequences.

Verify stack accesses use:

0100-01FF

through the bus.

Expected:

- Correct stack values
- Correct SP restoration

---

# Deliverable 8 - Branch Validation

Validate:

- BEQ
- BNE
- BMI
- BPL
- BCS
- BCC
- BVS
- BVC

Cases:

- Branch taken
- Branch not taken
- Page crossing

Use synthetic programs.

---

# Deliverable 9 - Page Wrap Validation

Validate:

- Zero-page wrapping
- Indexed wrapping
- Indirect wrap behavior

These tests frequently expose CPU integration defects.

---

# Deliverable 10 - Banking Validation

Use the actual C64 banking implementation.

Test:

Enable BASIC ROM.

Read A000.

Expect ROM value.

Disable BASIC ROM.

Read A000.

Expect RAM value.

Verify instruction execution follows the visible memory source.

---

# Deliverable 11 - Runtime Validation Commands

Support deterministic validation through runtime commands.

Required:

- RESET
- STEP_INSTRUCTION
- RUN_INSTRUCTIONS
- REQUEST_CPU_STATE

Avoid continuous execution.

Keep all validation bounded and deterministic.

---

# Deliverable 12 - Interrupt Entry Validation

Validate IRQ and NMI entry paths using synthetic ROM content.

IRQ test:

FFFE = 00
FFFF = D0

Expected:

PC = D000

NMI test:

FFFA = 00
FFFB = C0

Expected:

PC = C000

Vectors must be fetched through the bus.

No direct ROM access allowed.

---

# Deliverable 13 - Runtime Smoke Validation

Sequence:

1. Create runtime
2. Create machine
3. Install ROMs
4. Reset machine
5. Execute N instructions
6. Request CPU snapshot
7. Shutdown

Expected:

- Runtime responsive
- CPU advances
- Clean shutdown

---

# Deliverable 14 - Instrumentation

Optional but recommended.

Add counters such as:

- bus_reads
- bus_writes
- opcode_fetches

Use only for diagnostics.

Correctness must not depend on instrumentation.

---

# Required Tests

Minimum set:

- Reset vector test
- Instruction fetch test
- RAM read/write test
- Stack test
- Branch test
- Page wrap test
- Banking test
- IRQ vector test
- NMI vector test
- Runtime smoke test

Use synthetic ROM content.

Do not require copyrighted ROM images.

---

# Recommended Order

1. CPU snapshot API
2. Machine builder
3. Reset validation
4. Instruction fetch validation
5. RAM validation
6. Stack validation
7. Branch validation
8. Page-wrap validation
9. Banking validation
10. IRQ validation
11. NMI validation
12. Runtime smoke validation
13. Optional instrumentation

---

# Acceptance Criteria

Phase 3 is complete when:

- CPU executes exclusively through the bus.
- Reset vectors are fetched through the bus.
- IRQ vectors are fetched through the bus.
- NMI vectors are fetched through the bus.
- Banking affects execution correctly.
- Synthetic ROM programs execute correctly.
- Runtime can drive deterministic validation.
- Runtime remains responsive.
- All machine-level validation tests pass.

At that point the project may proceed to HighLevel Phase 4: Machine Clock and Scheduling.
