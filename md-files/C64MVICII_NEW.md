# C64MVICII_NEW.md

# VIC-II Remaining Work Plan for c64m

## Purpose

This document identifies the VIC-II work still relevant to the current PAL/NTSC
fidelity milestone. It does not replace the existing VIC-II plan history. It is a
new high-level planning document for remaining work.

## Current Position

The emulator already has a competent VIC-II implementation with live raster
timing, PAL and NTSC frame sizes, graphics modes, sprites, priority, collisions,
VIC bank awareness, BA stealing, DEN-off behavior, and timed bus-visible writes.

The current milestone should not reopen completed VIC-II work unless a specific
remaining item is required for PAL/NTSC fidelity.

## Goal

Close the small set of VIC-II gaps that block acceptable PAL and NTSC fidelity
for ordinary software.

Required for this milestone:

```text
- NTSC sprite BA timing table and selection.
```

Important but not blocking:

```text
- VIC idle-state g-access fetch behavior from $3FFF / $39FF.
- Better validation corpus for PAL and NTSC raster/sprite timing.
```

Explicitly out of scope for this milestone:

```text
- VIC-II light pen.
- Last-byte-on-bus recreation.
- Exact RDY/AEC sub-cycle pin timing.
- NTSC color generation differences.
- Full demo-scene cycle-perfect validation.
```

## Work Area A - NTSC Sprite BA Timing

### Why It Matters

The current sprite BA timing table is PAL-only. If NTSC is a first-class target,
then NTSC sprite fetch stalls must follow an NTSC-specific timing table.

### Observable Behavior

Wrong NTSC sprite BA timing can cause:

```text
- incorrect CPU/VIC contention timing;
- sprite-heavy software glitches;
- raster timing code that works in PAL but fails in NTSC;
- inconsistent behavior between configured PAL and NTSC machines.
```

### Planning Direction

The detailed guide should:

```text
- identify the existing PAL sprite BA table and its selection path;
- define the NTSC table using the same internal representation;
- switch tables through the existing video-standard configuration;
- add tests or diagnostics that distinguish PAL and NTSC behavior;
- avoid changing unrelated sprite rendering behavior.
```

### Acceptance Direction

```text
- PAL behavior remains unchanged.
- NTSC mode uses an NTSC-specific sprite BA table.
- Sprite BA stalls occur at expected NTSC cycles for selected diagnostics.
- Existing boot, display, sprite, collision, debugger, and config tests pass.
```

## Work Area B - VIC Idle g-access Fetch Behavior

### Why It Matters

Some software and diagnostics observe VIC idle fetch behavior. STATUS.md records
this as deferred.

### Milestone Status

This is important but not required for the current milestone unless a selected
ordinary-software compatibility target needs it.

### Planning Direction

A later guide should:

```text
- define when idle g-access fetches occur;
- define $3FFF / $39FF address behavior for the implemented VIC model;
- decide whether behavior affects only renderer data, bus-visible memory fetches,
  or both;
- test that ordinary rendering remains unchanged.
```

## Work Area C - Validation Corpus

### Why It Matters

VIC-II regressions are easy to introduce when adjusting timing.

### Planning Direction

Create a small validation set focused on accepted scope:

```text
- PAL raster smoke;
- NTSC raster smoke;
- sprite enable and position smoke;
- sprite BA timing diagnostic;
- sprite collision diagnostic;
- DEN-off behavior check;
- bank-aware character/sprite fetch check.
```

## Suggested Detailed Specs To Write Later

```text
1. NTSC sprite BA timing implementation guide.
2. VIC idle g-access fetch behavior guide, if accepted later.
3. VIC PAL/NTSC validation corpus guide.
```
