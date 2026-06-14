# DEBUGKEYS.md

# Detailed Implementation Guide
# Developer Bring-Up Hotkeys

## Purpose

Provide lightweight developer controls for machine execution during bring-up.

This is not a debugger.

This is a thin UI layer over existing runtime commands that allows:

- run
- pause
- single-step
- machine state observation

before frame generation and full machine emulation exist.

---

# Key Bindings

Required bindings:

F9      Toggle UI visibility
F10     Run
F11     Step Instruction
CTRL+S  Step Instruction
F12     Break / Pause
CTRL+C  Break / Pause

---

# Architectural Rule

Allowed:

UI
 -> runtime client
     -> runtime command queue
         -> runtime thread
             -> machine

Forbidden:

UI -> CPU
UI -> machine
UI -> RAM
UI -> ROM

All execution control must flow through runtime commands.

---

# Deliverable 1 - Runtime Commands

Ensure these commands exist:

- RUN
- PAUSE
- STEP_INSTRUCTION

Existing commands may be reused.

STEP_INSTRUCTION is preferred over STEP_CYCLE for human-driven bring-up.

---

# Deliverable 2 - Run Semantics

F10 sends RUN.

Behavior:

- runtime enters RUNNING state
- machine begins advancing through scheduler
- repeated RUN commands are harmless

---

# Deliverable 3 - Pause Semantics

F12 and CTRL+C send PAUSE.

Behavior:

- runtime enters PAUSED state
- machine stops advancing
- repeated PAUSE commands are harmless

---

# Deliverable 4 - Step Semantics

F11 and CTRL+S send STEP_INSTRUCTION.

When paused:

- execute exactly one instruction
- remain paused

When running:

- pause first
- execute one instruction
- remain paused

This guarantees deterministic behavior.

---

# Deliverable 5 - Runtime State Display

Expose runtime state.

Minimum states:

- RUNNING
- PAUSED

Optional:

- ERROR

State must originate from runtime, not be inferred by UI code.

---

# Deliverable 6 - CPU State Display

Display copied CPU snapshot data.

Suggested fields:

- PC
- A
- X
- Y
- SP
- P
- cycle count

Example:

PAUSED
PC=FCE2
A=00 X=00 Y=00
SP=FF
CYCLES=12345

The exact presentation is not important.

Visibility is the goal.

---

# Deliverable 7 - Runtime Event Integration

After:

- RESET
- STEP_INSTRUCTION
- RUN
- PAUSE

the UI should be able to refresh displayed state.

Polling is acceptable.

Event-driven updates are preferred.

---

# Deliverable 8 - Logging

Add lightweight bring-up logging.

Examples:

RUN command received
PAUSE command received
STEP instruction
PC=FCE2

Avoid per-cycle spam.

Instruction-level logging during stepping is acceptable.

---

# Tests

Test 1

Press F11.

Expected:

- one instruction executes
- PC changes
- runtime remains paused

Test 2

Press F10.

Expected:

- runtime enters RUNNING
- machine cycles advance

Test 3

Press F12.

Expected:

- runtime enters PAUSED
- machine cycles stop advancing

Test 4

Sequence:

F10
F12
F11

Expected:

- run
- pause
- single-step

works predictably

Test 5

CTRL+S behaves the same as F11.

Test 6

CTRL+C behaves the same as F12.

---

# Acceptance Criteria

Task is complete when:

- F10 starts execution
- F11 steps one instruction
- CTRL+S steps one instruction
- F12 pauses execution
- CTRL+C pauses execution
- F9 still toggles UI
- runtime ownership is preserved
- UI never manipulates machine state directly
- runtime state is visible
- CPU state is visible
- stepping is deterministic
- run/pause behavior is deterministic

---

# Purpose

A developer should be able to:

load ROMs
reset machine

F11
F11
F11

observe PC movement

F10

observe execution

F12

inspect machine state

without requiring a full debugger or frame-generation pipeline.
