# C64PHASE11.md

# Phase 11 - BASIC Usability Polish

## Purpose

Improve the now-interactive BASIC experience without broadening into full hardware accuracy.

Phase 10 established runtime pacing and IRQ/CIA diagnostics. The emulator reaches `READY.` with keyboard input, so the next useful work is to make typing and simple BASIC programs feel reliable.

---

# Source Documents

Read:

```text
AGENTS.md
STATUS.md
HIGHLEVEL.md
KBDPASS1.md
```

Phase 10 is assumed complete.

---

# Goal

Make BASIC entry comfortable enough for small programs.

Target:

```basic
10 PRINT "HELLO"
20 GOTO 10
RUN
```

---

# Non-Goals

Do not implement:

```text
SID
sprites
disk
tape
serial bus devices
cycle-perfect CIA
cycle-perfect VIC-II
international keyboard layouts
full key remapping UI
```

---

# Deliverable 1 - Manual Keyboard Validation

Validate the current key map against the live BASIC prompt.

Check:

```text
A-Z
0-9
SPACE
RETURN
DELETE
SHIFT
quotes
colon
parentheses
equals
asterisk
plus/minus
comma/period/slash
```

Fix only incorrect mappings found during validation.

---

# Deliverable 2 - Repeat and Cursor Timing

Verify runtime pacing makes cursor flashing and key repeat plausible.

If repeat is still too fast, trace whether the cause is:

```text
runtime frame pacing
CIA timer behavior
KERNAL key repeat counters
host key repeat leakage
```

Patch the smallest proven cause.

---

# Deliverable 3 - Keyboard Regression Tests

Add tests for any corrected key mappings.

Keep tests at the machine/CIA/runtime boundary rather than injecting characters into screen RAM.

---

# Deliverable 4 - BASIC Smoke Test

Where practical, add a smoke test or documented manual validation transcript for entering:

```basic
10 PRINT "HELLO"
20 GOTO 10
RUN
```

Automate only if it can remain stable and not depend on wall-clock timing.

## Validation Transcript

Manual validation was performed at the live BASIC `READY.` prompt after the
semantic frontend keyboard mapper landed.

```text
Startup:
    emulator auto-runs after initialization
    real 64C ROM reaches READY.
    cursor is visible and flashes at plausible speed

Typed:
    10 PRINT "HELLO"
    20 GOTO 10
    RUN

Observed:
    A-Z, 0-9, SPACE, RETURN, and Backspace/DEL are accepted
    quotes, colon, parentheses, equals, asterisk, plus/minus, comma, period, and slash map semantically
    cursor arrows move in the expected host directions
    HOME maps to HOME and Shift+HOME maps to CLR/HOME
    ESC maps to RUN/STOP
    host Delete triggers RESTORE
    host Control maps to C64 CONTROL
    host Tab maps to Commodore
    Shift+letter produces the C64 left graphics character set
    Tab+letter produces the C64 Commodore graphics character set
```

Regression coverage:

```text
frontend_input:
    Shift+2 -> @
    quote/double quote synthetic chords
    Shift+letter graphics chord
    cursor-key synthetic chords
    Control, Commodore, RESTORE actions

c64_keyboard:
    matrix positions for RUN/STOP, HOME, cursor keys, CONTROL, Commodore, left-arrow, up-arrow, and pound
```

---

# Acceptance Criteria

Phase 11 is complete when:

- BASIC prompt accepts normal typing.
- The target BASIC program can be entered manually.
- Cursor flash and key repeat are plausible at normal runtime speed.
- Incorrect mappings discovered during validation are fixed.
- Existing tests continue to pass.
- New focused tests pass.

Status: complete.

---

# End State

```text
frontend SDL input
    -> runtime copied key commands
        -> machine keyboard matrix
            -> CIA #1 scan
                -> ROM BASIC input
```
