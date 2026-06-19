# C64AUDFID_4.md
# VIC-II NTSC Sprite BA Timing Implementation Guide

## Component

C64MVICII_NEW

## Status

Coding-agent-ready implementation guide.

## Purpose

Add NTSC sprite BA timing parity with the existing PAL path. The emulator already
has broad VIC-II functionality; this guide addresses the small remaining NTSC
sprite bus-availability timing gap needed for PAL/NTSC fidelity.

## Required Reading Before Coding

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. Existing VIC-II planning docs relevant to the current codebase
5. `C64MVICII_NEW.md`
6. This guide

## Goal

Use a PAL or NTSC sprite BA timing table according to the machine video-standard
configuration. Preserve existing PAL behavior while adding the NTSC table and
selection path.

## In Scope

- Identify existing PAL sprite BA timing table and selection path.
- Add NTSC-specific sprite BA timing table using the same representation.
- Select PAL vs NTSC table through machine video-standard configuration.
- Preserve all existing PAL behavior.
- Add tests or diagnostics that distinguish PAL and NTSC sprite BA stalls.
- Update `STATUS.md`.

## Explicit Non-Goals

Do not implement these in this phase:

- VIC-II light pen.
- Last-byte-on-bus open-bus behavior.
- Exact RDY/AEC sub-cycle pin timing.
- NTSC color generation differences.
- Full demo-scene cycle-perfect validation.
- Sprite rendering rewrite.
- Collision rewrite.
- New video frontend.
- IEC serial bus.
- 1541 emulation.
- Fast loaders.
- D64 writes.
- Cartridge support.
- CIA Phase I or Phase J.

## Architecture Contract

VIC-II behavior belongs in `machine/`.

Allowed:

```text
runtime -> machine + util
machine -> util
frontend consumes copied video snapshots only
```

Forbidden:

```text
machine -> frontend
machine -> platform
frontend -> machine live state
```

PAL/NTSC selection must come from machine configuration, not frontend policy.

## Files To Inspect

Find actual names in the repo. Likely areas:

```text
src/machine/vicii.*
src/machine/c64.*
src/machine/config or video-standard files
src/runtime/config apply files
tests/*vic*
tests/*ntsc*
tests/*sprite*
STATUS.md
```

Search for:

```text
BA
badline
sprite fetch
sprite DMA
PAL
NTSC
cycles per line
```

## Implementation Strategy

1. Locate existing PAL sprite BA timing behavior.
2. Determine the internal representation:
   - explicit cycle table;
   - boolean per-cycle mask;
   - computed window function;
   - per-sprite fetch slots;
   - another project-specific representation.
3. Add NTSC data using the same representation.
4. Add a single selection helper so call sites do not duplicate PAL/NTSC checks.
5. Run existing PAL tests before changing behavior.
6. Add NTSC tests.
7. Update `STATUS.md`.

## Required Helper Shape

Prefer a helper equivalent to:

```c
const vicii_sprite_ba_table *vicii_sprite_ba_table_for_standard(vicii_video_standard standard);
```

or, if the project uses per-cycle queries:

```c
bool vicii_sprite_ba_active_for_cycle(const vicii *v, int raster_cycle, int sprite_index);
```

The important rule is that PAL/NTSC selection is centralized and driven by the
machine video standard.

## NTSC Table Source And Verification

Use the existing project's selected VIC-II reference convention for cycle
numbering. Do not mix cycle-numbering systems silently.

Before coding the NTSC table:

- Identify how the PAL table is indexed.
- Identify whether cycles are zero-based or one-based internally.
- Identify whether the table marks CPU stall cycles, sprite fetch cycles, or BA
  low lead cycles.
- Document the convention in code comments.

If the exact NTSC cycle values are already documented elsewhere in the repo, use
that source. If not, add a comment naming the external reference used by the
implementer. Do not guess cycle values.

## Preservation Rule For PAL

Do not edit existing PAL values unless a test proves they are wrong and the
change is explicitly accepted. The default task is additive:

```text
existing PAL table remains byte-for-byte or behavior-for-behavior equivalent
new NTSC table is selected only in NTSC mode
```

## Configuration Selection

The selected table must follow the same video standard used for:

- PAL/NTSC frame dimensions.
- Cycles per line.
- Lines per frame.
- Existing runtime config apply path.

Do not use frontend-only settings to choose sprite BA behavior.

## Tests And Diagnostics

Add tests where project harness supports them.

### Required PAL Regression Test

Prove existing PAL behavior remains unchanged. Options:

- Existing sprite BA test still passes.
- Add a compact test that samples known PAL sprite BA cycles and compares against
  pre-change expected behavior.

### Required NTSC Selection Test

Create a test that initializes or configures NTSC mode and proves the NTSC table
is selected.

Minimum acceptable assertion:

```text
PAL and NTSC mode do not use the same table pointer or same cycle mask when the
selected cycle differs between standards.
```

### Required NTSC Behavior Test

Add a diagnostic that enables one or more sprites and observes BA stall timing in
NTSC mode.

The test should assert:

- Stalls occur on expected NTSC cycles.
- PAL expected cycles are not incorrectly used in NTSC mode when they differ.
- Disabling sprite DMA removes the related sprite BA stalls.

### Required Integration Regression

Run existing tests for:

- boot;
- PAL video;
- NTSC video;
- sprites;
- collisions;
- debugger;
- config apply.

## STATUS.md Update

After implementation and tests, update `STATUS.md` with:

- NTSC sprite BA timing table implemented.
- PAL sprite BA behavior preserved.
- Selection is driven by machine video standard.
- Tests or diagnostics added.
- Deferred VIC-II items remain deferred: light pen, open bus, NTSC color, full
  cycle-perfect demo validation.

## Acceptance Checklist

This guide is complete only when all items below are true:

- Existing PAL sprite BA behavior is identified.
- NTSC sprite BA table or equivalent NTSC logic exists.
- PAL/NTSC selection uses machine video standard configuration.
- PAL tests still pass.
- NTSC mode does not use the PAL-only timing path.
- A test or diagnostic distinguishes PAL and NTSC sprite BA timing.
- Sprite rendering, priority, and collision behavior are not rewritten.
- Existing boot, display, sprite, collision, debugger, and config tests still
  pass.
- `STATUS.md` reflects the implemented NTSC sprite BA behavior and deferred VIC
  items.
