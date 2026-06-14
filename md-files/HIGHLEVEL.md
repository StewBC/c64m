# HIGHLEVEL.md

# C64 Bring-Up Plan: Boot to BASIC

## Purpose

This document defines the high-level roadmap for bringing the emulator from a functioning CPU core to a system capable of booting into a recognizable Commodore 64 BASIC screen.

The intent is that each section becomes the basis for a separate detailed implementation guide. The focus here is sequencing, dependencies, milestones, and architectural goals rather than implementation specifics.

---

# Guiding Principle

Bring the machine up vertically rather than horizontally.

Avoid implementing every subsystem in depth before integration. Instead, build the smallest functional slice that moves the emulator closer to displaying a BASIC screen.

Each milestone should result in a demonstrably more complete machine.

---

# Phase 1: Memory and Bus Infrastructure

## Goal

Connect the existing CPU to a realistic C64 memory system.

## Deliverables

- System bus abstraction
- RAM implementation
- ROM implementation
- Memory map definition
- CPU read/write callbacks routed through the bus
- Support for 6510 banking via $0000/$0001
- Reset vector reads from ROM

## Success Criteria

- CPU can fetch instructions through the bus
- RAM read/write behavior is verified
- ROM regions are visible and protected
- Banking changes visibility correctly
- Reset sequence functions using ROM vectors

## Why First

Everything else depends on a functioning memory model.

---

# Phase 2: ROM Integration

## Goal

Introduce ROM loading and execution.

## Deliverables

- KERNAL ROM support
- BASIC ROM support
- Character ROM support
- ROM loading infrastructure
- Validation and error reporting

## Success Criteria

- ROMs load correctly
- Reset vector resolves from KERNAL ROM
- CPU begins executing real ROM code

## Why Next

A C64 cannot boot without ROM execution.

---

# Phase 3: CPU Validation Against the Machine

## Goal

Validate CPU behavior inside the real machine environment.

## Deliverables

- Bus-aware CPU tests
- Reset tests
- Banking tests
- ROM execution tests
- NMI entry path
- IRQ entry path foundations

## Success Criteria

- CPU executes correctly through the memory system
- Interrupt vectors can be reached
- Machine-level tests pass

## Why Next

The CPU core may already work in isolation, but must be validated in the actual machine architecture.

---

# Phase 4: Machine Clock and Scheduling

## Goal

Establish a central timing model.

## Deliverables

- Machine cycle step function
- Global timing ownership
- Component registration model
- Per-cycle advancement infrastructure

## Success Criteria

- CPU advances through machine cycles
- Other devices can be attached to the scheduler
- Timing is no longer CPU-centric

## Why Next

The VIC-II and CIAs depend on coordinated timing.

---

# Phase 5: Runtime Frame Pipeline

## Goal

Complete the display pipeline before implementing real video generation.

## Deliverables

- Frame buffer ownership model
- Runtime frame publication
- Frontend frame consumption
- SDL texture update path
- Test-pattern rendering

## Success Criteria

- Emulator can display generated frames
- Frontend never reads machine memory directly
- Frame publication architecture is proven

## Why Before VIC-II

This isolates display architecture from video emulation complexity.

---

# Phase 6: Minimal VIC-II Integration

## Goal

Produce the first machine-generated video output.

## Deliverables

- VIC-II device skeleton
- VIC-II memory access path
- Raster timing foundation
- Screen buffer generation
- Border rendering

## Success Criteria

- Stable frame generation
- Visible screen output
- Deterministic raster progression

## Non-Goals

- Cycle-perfect behavior
- Advanced raster effects
- Accurate badline behavior

These come later.

---

# Phase 7: CIA Foundations

## Goal

Provide the minimum functionality required for boot progression.

## Deliverables

- CIA register framework
- Timer infrastructure
- IRQ generation
- Keyboard matrix foundation

## Success Criteria

- ROM code can interact with CIA registers
- Interrupt paths become operational

## Why Here

Many ROM routines assume CIA availability.

---

# Phase 8: Character Display Path

## Goal

Display recognizable text output.

## Deliverables

- Character ROM access
- Screen RAM interpretation
- Character rendering
- Color RAM support

## Success Criteria

- Text can be displayed
- BASIC startup text becomes visible
- Screen updates are reflected visually

---

# Phase 9: First BASIC Screen

## Goal

Reach a visible BASIC startup screen.

## Deliverables

- Successful ROM boot sequence
- Video output connected to machine state
- Required interrupt functionality
- Minimal keyboard infrastructure if required

## Success Criteria

The emulator displays a recognizable Commodore 64 BASIC screen.

This milestone represents the first true machine bring-up completion.

---

# Deferred Work

The following are intentionally outside the scope of the initial bring-up effort:

- Cycle-perfect VIC-II behavior
- Advanced raster effects
- SID emulation
- Accurate tape support
- Accurate disk support
- Fast-loader compatibility
- Performance optimization
- Save states
- Debugger enhancements

These should only be addressed after successful BASIC boot.

---

# Immediate Next Task

Implement the memory and bus infrastructure.

Specifically:

1. Create the C64 bus abstraction.
2. Add RAM ownership.
3. Add ROM ownership.
4. Implement memory mapping.
5. Implement $0000/$0001 banking.
6. Route CPU memory accesses through the bus.
7. Verify reset vector execution.

Once complete, the project can begin executing real ROM code and move toward machine-level timing and video generation.
