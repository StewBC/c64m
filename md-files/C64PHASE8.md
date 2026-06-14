# C64PHASE8.md

# Phase 8 - CIA Foundations

## Purpose

Add the minimum Complex Interface Adapter foundations needed for ROM boot progression.

This phase follows Phase 7 character display bring-up. The emulator can now render a deterministic text screen through screen RAM, character ROM, and color RAM. CIA work should now begin without changing the copied frame handoff or frontend ownership model.

---

# Source Documents

Read:

```text
AGENTS.md
STATUS.md
HIGHLEVEL.md
```

Phase 7 is assumed complete.

---

# Goal

Introduce CIA #1 and CIA #2 skeletons with stable register behavior, timer foundations, and minimal interrupt plumbing.

The goal is not full CIA accuracy. The goal is to let ROM code interact with deterministic CIA devices as boot support work continues.

---

# Non-Goals

Do not implement:

```text
SID
sprites
cycle-perfect CIA timing
full keyboard input UI
serial bus devices
tape
disk
BASIC startup completion
```

---

# Deliverable 1 - CIA Device Skeleton

Create a machine-owned CIA module.

Requirements:

```text
CIA #1 instance
CIA #2 instance
register storage
reset behavior
bus read/write API
machine-cycle step API
```

The live CIA instances belong to the runtime-thread machine state.

---

# Deliverable 2 - Bus Mapping

Route I/O-visible bus access:

```text
$DC00-$DCFF -> CIA #1
$DD00-$DDFF -> CIA #2
```

Preserve banking behavior:

```text
I/O visible:
    CIA registers
I/O hidden:
    RAM under I/O
```

Do not expose live CIA pointers to runtime or frontend.

---

# Deliverable 3 - Timer Foundation

Implement deterministic timer A and timer B register storage and countdown foundations sufficient for tests.

Include:

```text
latches
current counter values
control registers
underflow state
```

Exact hardware edge cases may remain deferred.

---

# Deliverable 4 - Interrupt Foundation

Add minimal IRQ/NMI signaling from CIA to machine CPU interrupt lines.

Initial target:

```text
CIA #1 -> IRQ
CIA #2 -> NMI foundation
```

Keep behavior deterministic and tested.

---

# Deliverable 5 - Keyboard Matrix Foundation

Add CIA #1 keyboard matrix storage and register behavior needed for later keyboard input.

This phase may use a deterministic no-key-pressed state.

Do not wire SDL keyboard events directly into machine state.

---

# Deliverable 6 - Runtime Integration

Step CIA devices from the existing machine cycle scheduler.

Keep existing ownership rules:

```text
runtime thread owns live machine
frontend receives copied snapshots only
```

---

# Deliverable 7 - Tests

Add machine tests.

Test:

```text
CIA reset state
register mirroring or masking
bus mapping
RAM under I/O behavior
timer countdown basics
IRQ/NMI line foundation
no-key keyboard matrix reads
```

Existing tests must continue to pass.

---

# Acceptance Criteria

Phase 8 is complete when:

- CIA #1 and CIA #2 are machine-owned and reset deterministically.
- `$DC00-$DCFF` and `$DD00-$DDFF` route through the bus when I/O is visible.
- RAM under I/O remains accessible when I/O is hidden.
- Timer foundations are present and tested.
- CIA interrupt foundations are present and tested.
- Keyboard matrix no-key state is deterministic.
- Existing frame pipeline remains unchanged.
- Existing tests continue to pass.
- New CIA tests pass.

---

# End State

```text
runtime
    -> machine
        -> CPU
        -> bus
            -> VIC-II
            -> CIA #1
            -> CIA #2
```
