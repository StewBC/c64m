# C64PHASE10.md

# Phase 10 - IRQ and CIA Boot Compatibility

## Purpose

Continue real ROM boot progression from the Phase 9 checkpoint.

Phase 9 proved that real 64C ROM execution reaches screen RAM, VIC-II, CIA #1, and CIA #2 activity. The remaining observed blocker is a pending CIA #1 IRQ during ROM execution.

---

# Source Documents

Read:

```text
AGENTS.md
STATUS.md
HIGHLEVEL.md
```

Phase 9 is assumed complete.

---

# Goal

Improve IRQ and CIA behavior just enough for ROM boot to progress beyond the current interrupt-related checkpoint.

At completion, the real ROM smoke trace should move past the Phase 9 observed state and either reach a recognizable BASIC screen or identify the next smallest blocker.

---

# Current Checkpoint

After a 500,000-cycle real 64C ROM trace:

```text
PC=$FD70
screen RAM writes > 0
VIC writes > 0
CIA #1 writes > 0
CIA #2 writes > 0
CIA #1 IRQ pending = true
CIA #2 NMI pending = false
```

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

# Deliverable 1 - IRQ Acknowledge Trace

Add focused tests or diagnostics around ROM interrupt handling.

Track:

```text
IRQ vector entries
CIA #1 ICR reads
CIA #1 ICR writes
IRQ pending clear behavior
CPU interrupt-disable state
```

Keep diagnostics machine/runtime-owned; do not expose live state to frontend.

---

# Deliverable 2 - CIA ICR Compatibility

Refine CIA interrupt control behavior where ROM tests prove it is needed.

Likely areas:

```text
ICR read clears only reported flags
ICR mask behavior
timer interrupt flag persistence
underflow reload timing
control register force-load behavior
```

Do not broaden into cycle-perfect CIA behavior.

---

# Deliverable 3 - Timer Edge Cases

Patch timer A/B behavior needed by the KERNAL path.

Test:

```text
one-shot start/stop
continuous reload
latch writes while stopped
latch writes while running
underflow flag timing
```

---

# Deliverable 4 - CPU Interrupt Regression Tests

Keep the Phase 9 IRQ entry tests passing while adding any needed CPU-side interrupt fixes.

If ROM boot proves NMI is required, add the smallest CPU NMI entry path with tests.

---

# Deliverable 5 - Real ROM Progression Test

Extend the real ROM smoke test to assert progress beyond the Phase 9 checkpoint.

Useful checks:

```text
CIA #1 IRQ eventually acknowledged or cleared
PC moves past the known checkpoint
screen RAM contents continue changing
generated frame shows ROM-written text/color state
```

---

# Acceptance Criteria

Phase 10 is complete when:

- The Phase 9 real ROM checkpoint advances measurably.
- CIA #1 IRQ pending behavior is explained by tests.
- Any IRQ/CIA fixes are narrow and covered.
- Existing tests continue to pass.
- New boot compatibility tests pass.
- Remaining blocker, if any, is documented precisely.

---

# End State

```text
runtime
    -> machine
        -> CPU IRQ handling
        -> CIA #1 interrupt behavior
        -> ROM boot progression
        -> copied frame snapshots
```
