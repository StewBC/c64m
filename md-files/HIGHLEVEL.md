# HIGHLEVEL.md

# C64 Bring-Up Roadmap

## Goal

```text
Reach a visible Commodore 64 BASIC startup screen.
```

## Guiding Principle

Build vertically.

Each phase must produce a demonstrably more complete machine.

Do not implement future phases early.

---

# Phase 1 - Memory And Bus

Goal:

```text
CPU -> C64 memory map
```

Completed.

---

# Phase 2 - ROM Integration

Goal:

```text
real ROMs -> reset vector -> ROM execution
```

Completed.

---

# Phase 3 - Machine Validation

Goal:

```text
CPU validation inside machine architecture
```

Completed.

---

# Phase 4 - Scheduler And Timing

Goal:

```text
machine-owned cycle progression
```

Completed.

---

# Phase 5 - Frame Pipeline

Goal:

```text
runtime -> copied frame -> frontend
```

Completed.

---

# Phase 6 - Minimal VIC-II Integration

Goal:

```text
machine-generated video output
```

Completed.

Result:

```text
border
background
raster timing
frame generation
```

---

# Phase 7 - Character Display

Goal:

```text
screen RAM
    + character ROM
    + color RAM
        -> visible PETSCII character display
```

Success Criteria:

- Character ROM glyphs render.
- Screen RAM participates in rendering.
- Color RAM affects character colors.
- Existing frame pipeline remains unchanged.
- Visible text appears on screen.

---

# Phase 8 - CIA Foundations

Goal:

```text
CIA #1
CIA #2
interrupt infrastructure
```

Success Criteria:

- CIA devices exist.
- Timer foundations exist.
- IRQ generation path exists.
- Runtime remains stable.

---

# Phase 9 - Interrupt-Driven Bring-Up

Goal:

```text
CPU
 <- IRQ/NMI
 <- CIA
 <- VIC-II
```

Success Criteria:

- IRQ path works.
- NMI path works.
- Interrupt vectors execute correctly.

---

# Phase 10 - Keyboard Matrix

Goal:

```text
host keyboard
    -> C64 keyboard matrix
```

Success Criteria:

- Key presses reach the machine.
- Keyboard scanning functions.

---

# Phase 11 - BASIC Startup

Goal:

```text
boot to recognizable BASIC screen
```

Success Criteria:

- BASIC startup sequence executes.
- Startup screen is visible.
- System reaches stable idle state.

---

# Future Work

```text
sprites
SID
disk support
cartridges
debugger expansion
timing accuracy
badlines
raster effects
compatibility work
```
