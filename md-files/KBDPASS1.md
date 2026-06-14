# KBDPASS1.md

# Keyboard Pass 1 - BASIC Interaction

## Purpose

The emulator now reaches a recognizable Commodore 64 BASIC startup screen.

The next milestone is not higher accuracy.

The next milestone is interaction.

This task introduces the minimum keyboard path required to type BASIC programs at the READY prompt.

The goal is to make the machine useful.

---

# Goal

Allow a user to type BASIC programs into the running emulator.

Target example:

```basic
10 PRINT "HELLO WORLD"
20 GOTO 10
RUN
```

The machine should accept input through the normal C64 keyboard path.

---

# Source Documents

Read:

```text
AGENTS.md
STATUS.md
HIGHLEVEL.md
```

This task assumes:

```text
ROM boot reaches READY.
Character display works.
CIA foundations exist.
```

---

# Architectural Requirements

Input must follow ownership rules.

Allowed:

```text
SDL event
    -> frontend
        -> runtime command/event
            -> machine keyboard state
                -> CIA
                    -> ROM keyboard scan
```

Forbidden:

```text
frontend -> machine
frontend -> CIA
frontend -> keyboard matrix
SDL callback -> machine mutation
```

Frontend must never modify machine state directly.

---

# Deliverable 1 - Keyboard Matrix State

Create machine-owned keyboard matrix storage.

Requirements:

```text
deterministic reset state
all keys released after reset
machine ownership
runtime-thread ownership
```

Suggested representation:

```c
uint8_t keyboard_rows[8];
```

or equivalent.

Implementation details are flexible.

---

# Deliverable 2 - Runtime Keyboard Events

Introduce copied key events.

Required:

```text
KEY_DOWN
KEY_UP
```

Events must cross the frontend/runtime boundary as copied data.

No shared mutable state.

---

# Deliverable 3 - SDL Key Mapping

Map a minimal subset of host keys to C64 keys.

Required:

```text
A-Z
0-9
SPACE
RETURN
BACKSPACE
SHIFT
```

Initial US keyboard assumptions are acceptable.

Do not implement layout customization.

---

# Deliverable 4 - CIA Keyboard Reads

Connect keyboard matrix state to CIA reads.

Goal:

```text
ROM keyboard scanning sees key state.
```

Do not bypass ROM scanning.

Do not inject characters directly into screen RAM.

The ROM must remain responsible for text entry.

---

# Deliverable 5 - Shift Support

Implement the minimum shift behavior required for BASIC entry.

Examples:

```text
"
:
*
=
(
)
```

Perfect keyboard accuracy is not required.

Practical BASIC usability is the goal.

---

# Deliverable 6 - Manual Validation

Verify:

```basic
10 PRINT "HELLO"
20 GOTO 10
RUN
```

can be entered from the host keyboard.

Verify:

```text
READY.
```

accepts user input normally.

---

# Deliverable 7 - Tests

Add tests where practical.

Test:

```text
key press updates matrix
key release updates matrix
runtime event reaches machine
CIA reads reflect matrix state
```

Existing tests must continue to pass.

---

# Non-Goals

Do not implement:

```text
joysticks
paste support
international layouts
key remapping UI
full keyboard accuracy
RESTORE key
function keys
SID
sprites
```

These belong to later work.

---

# Acceptance Criteria

Task is complete when:

* BASIC prompt accepts keyboard input.
* Letters can be typed.
* Numbers can be typed.
* RETURN submits a line.
* SHIFT enables practical BASIC entry.
* Input reaches the machine through CIA scanning.
* No frontend-to-machine ownership violations are introduced.
* Existing tests continue to pass.
* New keyboard tests pass.

---

# Success Definition

A user can:

```basic
10 PRINT "HELLO WORLD"
20 GOTO 10
RUN
```

and observe the program executing inside the emulator.

At that point the emulator has transitioned from a passive boot demonstration to an interactive Commodore 64.
