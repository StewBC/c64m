# C64PHASE9.md

# Phase 9 - ROM Boot Progression

## Purpose

Use the machine pieces now in place to push ROM execution toward a recognizable Commodore 64 BASIC startup screen.

Phase 9 should focus on boot blockers discovered by running the real ROM path, not on broad accuracy work.

---

# Source Documents

Read:

```text
AGENTS.md
STATUS.md
HIGHLEVEL.md
```

Phase 8 is assumed complete.

---

# Goal

Progress from reset-vector execution to visible ROM-driven screen contents.

At completion, the emulator should either display a recognizable BASIC startup screen or document the smallest remaining blocker with a focused next phase.

---

# Non-Goals

Do not implement:

```text
SID
sprites
cycle-perfect VIC-II
cycle-perfect CIA
disk
tape
serial bus devices
advanced raster effects
```

---

# Deliverable 1 - Boot Trace Checkpointing

Add a small machine/runtime-visible way to inspect boot progress in tests.

Useful checkpoints may include:

```text
PC progression through KERNAL reset
screen RAM writes
VIC register writes
CIA register interactions
interrupt entry attempts
```

Keep this test-oriented and do not expose live machine pointers to frontend.

---

# Deliverable 2 - CPU Interrupt Validation

Validate the existing IRQ path against the machine bus and ROM vectors.

Add or fix:

```text
IRQ vector fetch through $FFFE/$FFFF
status/PC stack pushes
interrupt-disable behavior
CIA #1 IRQ pending interaction
```

If NMI is required for boot progression, add the smallest CPU NMI entry path and tests.

---

# Deliverable 3 - ROM-Driven Screen Writes

Ensure ROM writes to screen RAM and color RAM are reflected by the Phase 7 renderer.

Verify:

```text
$0400 screen RAM updates
$D800 color RAM updates
character ROM glyph rendering
frame snapshot updates after writes
```

Avoid replacing ROM output with synthetic text.

---

# Deliverable 4 - Minimal VIC Register Compatibility

Implement only VIC-II register behavior that ROM boot actually needs.

Candidates:

```text
memory pointer register $D018
control register stored bits
border/background color writes
```

Do not implement badlines, sprites, or raster IRQs unless proven necessary for the boot milestone.

---

# Deliverable 5 - Minimal CIA Compatibility

Patch CIA behavior only where boot tracing proves it is needed.

Candidates:

```text
timer control edge cases
ICR read/write behavior
CIA #1 keyboard no-key reads
CIA #2 port defaults
```

Keep timer accuracy limited to deterministic ROM progression support.

---

# Deliverable 6 - Tests

Add tests around each boot blocker fixed.

Test:

```text
IRQ entry through the machine
screen RAM writes produce changed glyph pixels
color RAM writes produce changed foreground pixels
ROM boot progresses beyond known checkpoints
runtime frame publication still uses copied snapshots
```

Existing tests must continue to pass.

---

# Acceptance Criteria

Phase 9 is complete when:

- Real ROM execution progresses measurably beyond Phase 8.
- ROM-driven screen or color RAM writes appear in generated frames.
- Required interrupt behavior is tested.
- No frontend live-machine access is introduced.
- Existing tests continue to pass.
- New boot progression tests pass.
- Remaining blockers, if any, are documented precisely.

---

# End State

```text
runtime
    -> machine
        -> ROM execution
        -> bus
            -> VIC-II text frame
            -> CIA boot support
        -> copied frame snapshots
            -> frontend
```
