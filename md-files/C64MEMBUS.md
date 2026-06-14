# C64MEMBUS.md

# Detailed Implementation Guide
# Phase 1 - C64 Memory and Bus Infrastructure

## Purpose

This document is intended for an implementation agent.

The objective is to build the first machine-level integration layer between the existing CPU and the future C64 hardware devices.

This phase is NOT about emulating the VIC-II, CIA, SID, or full machine behavior.

This phase is solely about creating a correct memory ownership and access model that allows the CPU to execute through a realistic C64 address space.

At completion:

- The CPU must execute through a C64 bus.
- RAM must exist.
- ROM must exist.
- Banking must function.
- Reset vectors must be fetched correctly.
- The machine must be capable of beginning ROM execution.

---

# Goals

## Primary Goal

Create a C64 bus implementation that becomes the single authority for all CPU memory accesses.

The CPU must never directly access RAM or ROM.

Every read and write must pass through the bus.

---

# Non-Goals

Do NOT implement:

- VIC-II behavior
- CIA behavior
- SID behavior
- Raster timing
- Interrupt generation
- Keyboard scanning
- Display generation

Only create placeholders where future routing is required.

---

# Architectural Principles

## Principle 1

The CPU owns execution.

The machine owns memory.

The CPU must not know where memory comes from.

The CPU only performs:

```c
uint8_t read(uint16_t address);
void write(uint16_t address, uint8_t value);
```

Everything else belongs to the machine.

---

## Principle 2

The bus owns address decoding.

No address decoding should appear in the CPU.

The CPU must remain reusable.

---

## Principle 3

Future devices attach to the bus.

Eventually:

- VIC-II
- CIA1
- CIA2
- SID
- Cartridge
- Expansion hardware

will all participate through the same address routing system.

Design with this in mind.

---

# Deliverable 1
# Create C64 Bus Object

## Required Object

Create a machine-level bus object.

Suggested name:

```c
c64_bus_t
```

Responsibilities:

- Address decoding
- RAM ownership
- ROM ownership
- Banking logic
- Future I/O dispatch

---

## Required Public Interface

Suggested interface:

```c
uint8_t c64_bus_read(c64_bus_t *bus, uint16_t address);

void c64_bus_write(
    c64_bus_t *bus,
    uint16_t address,
    uint8_t value);
```

These become the only CPU-visible memory functions.

---

# Deliverable 2
# Add RAM Ownership

## Requirement

Bus owns all machine RAM.

Suggested allocation:

```c
uint8_t ram[65536];
```

Do not optimize.

Do not use sparse memory.

Keep implementation simple.

---

## Behavior

RAM must:

- retain writes
- return written values
- initialize deterministically

Preferred initialization:

```c
memset(ram, 0, sizeof(ram));
```

---

# Deliverable 3
# Add ROM Ownership

## Required ROM Regions

### BASIC ROM

```text
A000-BFFF
```

Size:

```text
8192 bytes
```

---

### Character ROM

```text
D000-DFFF
```

Size:

```text
4096 bytes
```

---

### KERNAL ROM

```text
E000-FFFF
```

Size:

```text
8192 bytes
```

---

## Storage

Suggested:

```c
uint8_t basic_rom[0x2000];
uint8_t char_rom[0x1000];
uint8_t kernal_rom[0x2000];
```

---

# Deliverable 4
# ROM Loading Support

Create ROM loading functions.

Example:

```c
bool c64_load_basic_rom(...);
bool c64_load_char_rom(...);
bool c64_load_kernal_rom(...);
```

Requirements:

- validate file size
- reject incorrect ROMs
- load exact byte counts

Do not silently truncate.

---

# Deliverable 5
# Implement Address Decoding

Initial decoding should support:

```text
0000-9FFF -> RAM

A000-BFFF -> BASIC ROM or RAM

C000-CFFF -> RAM

D000-DFFF -> I/O or CHAR ROM or RAM

E000-FFFF -> KERNAL ROM or RAM
```

Routing behavior depends on banking state.

---

# Deliverable 6
# Implement 6510 Banking

## Purpose

The C64 does not permanently expose ROM.

Visibility is controlled through CPU port registers:

```text
$0000
$0001
```

The critical register is:

```text
$0001
```

---

## Initial Implementation Requirement

Implement only enough behavior to correctly expose:

- BASIC ROM
- KERNAL ROM
- Character ROM
- RAM

Ignore cartridge behavior.

Ignore Ultimax.

Ignore expansion devices.

---

## Internal State

Suggested:

```c
uint8_t cpu_port_direction;
uint8_t cpu_port_data;
```

Stored in the bus.

---

## Required Behavior

Writes to:

```text
$0000
$0001
```

must update banking state.

Subsequent reads must reflect the new memory visibility.

---

## Verification

Agent must verify:

- BASIC visible when expected
- BASIC hidden when expected
- KERNAL visible when expected
- KERNAL hidden when expected
- Character ROM visible when expected

---

# Deliverable 7
# Future I/O Placeholder

The D000-DFFF region eventually becomes:

```text
VIC-II
SID
CIA
Color RAM
```

For now:

create a dispatch layer.

Example:

```c
if(io_visible)
{
    return c64_io_read(...);
}
```

The implementation may simply return:

```c
0xFF
```

initially.

The routing architecture matters more than functionality.

---

# Deliverable 8
# CPU Integration

Connect CPU memory callbacks to the bus.

Before:

```c
cpu -> memory
```

After:

```text
CPU
 |
 v
BUS
 |
 +-- RAM
 +-- BASIC ROM
 +-- CHAR ROM
 +-- KERNAL ROM
 +-- future I/O
```

The CPU must not know what lies behind the bus.

---

# Deliverable 9
# Reset Vector Validation

## Purpose

Prove ROM execution is possible.

---

## Test

Read:

```text
FFFC
FFFD
```

through the bus.

Verify values originate from KERNAL ROM.

---

## Success

CPU reset vector comes from ROM.

CPU begins executing ROM code.

No direct memory access paths remain.

---

# Required Test Suite

The implementation is not complete until the following tests exist.

## RAM Test

Write:

```text
1234 = 56
```

Read:

```text
1234
```

Expect:

```text
56
```

---

## ROM Visibility Test

Enable ROM.

Read:

```text
E000
```

Verify ROM value.

Disable ROM.

Read:

```text
E000
```

Verify RAM value.

---

## Banking Test

Write to:

```text
0001
```

Verify visibility changes.

---

## Reset Vector Test

Read:

```text
FFFC
FFFD
```

Verify KERNAL ROM source.

---

# Definition of Done

Phase 1 is complete only when:

- Bus exists
- RAM exists
- ROMs load correctly
- CPU accesses memory only through the bus
- Banking functions
- Reset vectors are fetched through ROM
- CPU begins execution from ROM code
- Tests pass

Once these conditions are met, Phase 2 (ROM Integration and Boot Progression) may begin.
