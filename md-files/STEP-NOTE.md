# Step-Over / Step-Out Debugging Notes

## Current State (2026-06-22)

Diagnostic logging is **intentionally left in** `src/runtime/runtime_thread.c`
inside `runtime_step_over`.  Both functions also have a fast-limit fallback so
the UI never freezes indefinitely.

### What was added

| Location | What | Purpose |
|---|---|---|
| `runtime_step_over` | `fprintf(stderr, ...)` on start, every JSR/RTS/interrupt change, every 10 000 iterations, and on fallback | Diagnose hangs |
| `runtime_step_over` | `STEP_OVER_FAST_LIMIT = 500 000` fallback to RUNNING | Prevent infinite freeze |
| `runtime_step_out`  | `STEP_OUT_FAST_LIMIT = 10 000` fallback to RUNNING | Already in place |

---

## Known Issue: Step Over hangs on IEC serial routines

### Trigger

1. Emulator is paused inside KERNAL serial code around `$ED5D` (IEC serial
   byte-send/receive polling loop).
2. User manually sets PC to a different address (e.g. `$EDBB`) to escape the
   loop.
3. User presses **Step Over (F11)** on a JSR at that new address (e.g.
   `$EDBB: JSR $ED36`).
4. Terminal shows `INFO: STEP OVER requested` but the UI does not update.

### Root cause

`$ED36` (and the surrounding `$EDxx` area) are KERNAL IEC serial routines.
They spin in tight loops waiting for a signal from real Commodore hardware
(clock/data lines on the serial bus).  Since no actual hardware is connected,
the subroutine never reaches its own RTS.  `stop_pc` (`$EDBE` in the example)
is never reached, so `runtime_step_over`'s loop runs indefinitely.

This is the **same root cause** as the original `$ED5D` spin — manually
setting the PC moves execution to a different serial routine that has the same
hardware-wait problem.

### Why SEI/CLI is not the cause

`c6510_step` checks for pending interrupts **before** executing the opcode at
PC:

```c
if (c6510_take_nmi_if_pending(m)) { return ...; }
if (c6510_take_irq_if_pending(m)) { return ...; }
// ... only now execute the opcode
```

If an IRQ fires "at" the same PC as an RTS:
- The RTS has **not yet executed** — the interrupt fired instead.
- `interrupt_taken = true` → `interrupt_depth++`, `jsr_counter` unchanged.
  This is **correct**: the RTS will retry after the handler's RTI.
- After RTI, `interrupt_depth` returns to 0 and the RTS executes normally,
  decrementing `jsr_counter`.

SEI (sets I=1) prevents IRQs; CLI (clears I=1) allows them.  Either way the
tracking is correct.  SEI/CLI does not corrupt `jsr_counter` or
`interrupt_depth`.

---

## How to use the logs when the hang recurs

Reproduce: get to the hanging state, then check the terminal.  The output
should look like:

```
STEP OVER: start PC=EDBB stop_pc=EDBE
STEP OVER: JSR -> new_PC=ED36 jsr=1
STEP OVER: still running iter=10000 PC=ED5D stop=EDBE jsr=1 idepth=0
STEP OVER: still running iter=20000 PC=ED5D stop=EDBE jsr=1 idepth=0
...
STEP OVER: fallback to RUNNING after 500000 instructions PC=xxxx stop_pc=EDBE jsr=1 idepth=0
```

**What to look for:**

| Observation | Meaning |
|---|---|
| `jsr=1` stays constant across all "still running" lines | Subroutine never returned — it is looping internally |
| `jsr` keeps growing (2, 3, 4 ...) | Subroutine is calling deeper and deeper, never unwinding |
| `jsr` goes negative | Subroutine returned to the wrong address (stack mismatch from a manual set-PC) |
| `idepth` oscillates (0→1→0→1...) | IRQs firing during the loop — normal, not a bug |
| `idepth` grows without returning to 0 | Interrupt handler is itself stuck or RTI tracking is off |
| Fallback line shows `jsr=0 idepth=0` but PC ≠ stop_pc | Subroutine returned to the wrong address exactly once |

---

## To remove the logging

When the IEC serial hang is no longer a concern (e.g. serial routines are
properly handled or the emulator traps hardware-wait loops), remove the
diagnostic `fprintf` calls from `runtime_step_over` in
`src/runtime/runtime_thread.c` and delete this file.

The `STEP_OVER_FAST_LIMIT` fallback to RUNNING is **worth keeping** even after
removing logging — it prevents any future infinite-loop hang from silently
freezing the UI.

The `STEP_OVER_LOG_INTERVAL` and `STEP_OVER_FAST_LIMIT` constants are defined
at the top of `runtime_thread.c` in the anonymous enum near
`STEP_OUT_FAST_LIMIT`.
