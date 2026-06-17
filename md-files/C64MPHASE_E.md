# C64MPHASE_E.md
# Phase E Implementation Guide: VIC-II Sprite Collision Detection and Priority

## Purpose

This document is a coding-agent-ready guide for implementing Phase E of the c64m VIC-II workstream.
Phase E adds the behavior needed after sprite display exists:

- Sprite priority control via `$D01B`
- Sprite-sprite collision detection via `$D01E`
- Sprite-background collision detection via `$D01F`
- VIC interrupt generation for sprite collisions through `$D019` / `$D01A`
- Final pixel priority composition between border, sprites, foreground graphics, and background pixels

This phase assumes Phase D has already implemented sprite fetching, decoding, expansion, multicolor sprite pixels, sprite enable, sprite positioning, and sprite pixel output candidates for all eight sprites.

## Source Context

This phase refines the Phase E section of `C64MVICII.md`. That source document defines Phase E as sprite collision detection and priority, with `$D01B`, `$D01E`, `$D01F`, IMMC, IMBC, and the front-to-back pixel MUX order as the required scope.

Primary external reference named by the parent plan:

- Christian Bauer, "The MOS 6567/6569 video controller (VIC-II) and its application in the Commodore 64" (1996)

Secondary external reference named by the parent plan:

- oxyron.de VIC-II register reference

## Non-Goals

Do not implement or modify these items in this phase unless a small local adjustment is required to connect Phase E cleanly:

- Sprite display fetching, decoding, expansion, or row counter behavior. That is Phase D.
- Bad Line timing, c-access timing, or BA/AEC cycle stealing. Those are Phase A and Phase H.
- New graphics modes. Those are Phase C.
- Light pen. That is Phase F.
- Open bus / last-byte behavior, except where `$D01E` and `$D01F` read-clear behavior overlaps with register reads. That is Phase G.

## Required Registers

### `$D01B`: Sprite Data Priority Register

Address: `$D01B`

Name: sprite priority / sprite-background priority

Bits:

- Bit 0 controls sprite 0
- Bit 1 controls sprite 1
- ...
- Bit 7 controls sprite 7

Behavior:

- Bit clear (`0`): sprite is in front of foreground background pixels.
- Bit set (`1`): sprite is behind foreground background pixels.
- Border remains in front of all sprites regardless of `$D01B`.
- `$D01B` affects display priority only. It must not suppress collision detection.

Implementation requirement:

- Store writes to `$D01B` exactly as an 8-bit register.
- Reads return the stored value.
- If register mirroring across `$D000-$D3FF` already exists, ensure `$D01B` participates in the existing mirror path.

### `$D01E`: Sprite-Sprite Collision Register

Address: `$D01E`

Name: sprite-sprite collision register

Bits:

- Bit N is set when sprite N participates in a collision with at least one other sprite.

Behavior:

- Read-only from the emulated CPU perspective.
- Writes have no effect.
- Read returns the current collision latch value, then clears the latch to `0x00`.
- Collision bits are set when two or more enabled sprites have non-transparent pixels at the same display pixel position.
- All sprites involved in the collision at that pixel must have their bits set simultaneously.
- A collision event sets the IMMC interrupt flag in `$D019` if this is a new latched collision condition. IRQ line assertion still depends on `$D01A` enable state, using the existing VIC IRQ aggregation behavior.

Implementation requirement:

- Add or complete a stored latch field, for example `sprite_sprite_collision`.
- On CPU read of `$D01E`, return the latch value and then clear it.
- Do not clear `$D019` from the `$D01E` read path. `$D019` is cleared only by its own documented write-to-clear path.
- CPU writes to `$D01E` must be ignored.

### `$D01F`: Sprite-Background Collision Register

Address: `$D01F`

Name: sprite-data collision register

Bits:

- Bit N is set when sprite N overlaps a foreground background graphics pixel.

Behavior:

- Read-only from the emulated CPU perspective.
- Writes have no effect.
- Read returns the current collision latch value, then clears the latch to `0x00`.
- A collision is between a non-transparent sprite pixel and a foreground background-layer pixel.
- Collision detection must still happen when the sprite is behind foreground graphics due to `$D01B`.
- Collision detection must still happen when `DEN=0`, provided the renderer still has a foreground/background pixel classification for the current position.
- A collision event sets the IMBC interrupt flag in `$D019` if this is a new latched collision condition. IRQ line assertion still depends on `$D01A` enable state, using the existing VIC IRQ aggregation behavior.

Implementation requirement:

- Add or complete a stored latch field, for example `sprite_background_collision`.
- On CPU read of `$D01F`, return the latch value and then clear it.
- Do not clear `$D019` from the `$D01F` read path. `$D019` is cleared only by its own documented write-to-clear path.
- CPU writes to `$D01F` must be ignored.

### `$D019` / `$D01A`: VIC IRQ Status and Enable Integration

This phase must integrate with existing IRQ status and enable handling.

Required collision interrupt bits:

- IMMC: sprite-sprite collision interrupt flag / enable
- IMBC: sprite-background collision interrupt flag / enable

Use the bit positions already implemented in c64m's VIC IRQ code. If they are not named yet, define constants instead of using magic numbers.

Expected conventional VIC-II bit positions:

- `$D019` bit 2: IMMC, sprite-sprite collision interrupt flag
- `$D019` bit 1: IMBC, sprite-background collision interrupt flag
- `$D01A` bit 2: enable sprite-sprite collision interrupt
- `$D01A` bit 1: enable sprite-background collision interrupt
- `$D019` bit 7: aggregate IRQ state, set when any enabled VIC interrupt source is pending

Implementation requirement:

- When `$D01E` changes from no relevant collision bit to having one or more newly set collision bits, set the IMMC flag in `$D019`.
- When `$D01F` changes from no relevant collision bit to having one or more newly set collision bits, set the IMBC flag in `$D019`.
- Recompute or update the aggregate IRQ bit and external IRQ line using the existing VIC IRQ helper.
- Do not repeatedly retrigger while the `$D019` flag remains pending. Subsequent collisions may OR more bits into `$D01E` / `$D01F`, but the IRQ source flag is already pending until CPU clears it through `$D019`.

## Pixel Model Required by Phase E

Phase E should make the renderer produce, for each visible pixel clock, enough information to answer three questions:

1. Is the border active?
2. Does the background graphics layer produce a foreground pixel at this position?
3. Which enabled sprites produce non-transparent pixels at this position, and what color would each sprite pixel have?

A good implementation shape is to use small local structs or equivalent internal values:

```c
typedef struct ViciiBgPixel {
    uint8_t color;
    bool is_foreground;
} ViciiBgPixel;

typedef struct ViciiSpritePixel {
    bool opaque;
    uint8_t color;
} ViciiSpritePixel;
```

These names are suggestions. Prefer names and style consistent with the existing codebase.

The important point is separation of concepts:

- Background color value is not enough by itself.
- Phase E needs a boolean foreground classification for the background graphics layer.
- Sprite color value is not enough by itself.
- Phase E needs a boolean opaque / non-transparent classification for each sprite.

## Background Foreground Classification

Sprite-background collision and `$D01B` priority depend on whether the background graphics layer pixel is foreground.

Use these rules:

### Standard Text Mode

- Glyph bit `1`: foreground pixel.
- Glyph bit `0`: background pixel.

### Multicolor Text Mode

For characters whose color RAM bit 3 is clear, use standard text classification.

For multicolor characters:

- Pixel pair `00`: background, not foreground.
- Pixel pair `01`: background color 1, treat as foreground for collision/priority.
- Pixel pair `10`: background color 2, treat as foreground for collision/priority.
- Pixel pair `11`: character color, treat as foreground for collision/priority.

### Standard Bitmap Mode

- Bitmap bit `1`: foreground pixel.
- Bitmap bit `0`: background pixel.

### Multicolor Bitmap Mode

- Pixel pair `00`: background color 0, not foreground.
- Pixel pair `01`: video matrix high nibble, foreground.
- Pixel pair `10`: video matrix low nibble, foreground.
- Pixel pair `11`: color RAM nibble, foreground.

### ECM Text Mode

- Glyph bit `1`: foreground pixel.
- Glyph bit `0`: selected background color pixel, not foreground.

### Invalid Modes

The parent plan says invalid background/display modes force background/display pixels to black while memory timing still occurs normally.

For Phase E:

- Invalid-mode background pixels should not be treated as foreground for sprite-background collision.
- Invalid-mode background pixels should not hide behind-priority sprites through `$D01B`.
- Border behavior is unchanged.

## Sprite Opaqueness Classification

Use the decoded sprite pixel information from Phase D.

### Standard Sprite

- Sprite data bit `0`: transparent, no sprite pixel.
- Sprite data bit `1`: opaque, color is `$D027 + sprite_index` low nibble.

### Multicolor Sprite

- Pixel pair `00`: transparent, no sprite pixel.
- Pixel pair `01`: opaque, color is `$D025` low nibble.
- Pixel pair `10`: opaque, color is `$D027 + sprite_index` low nibble.
- Pixel pair `11`: opaque, color is `$D026` low nibble.

Expanded sprites must use the final expanded pixel stream. Collision happens on actual displayed pixel positions after X expansion and Y expansion effects.

## Priority Composition

For every display pixel, choose the final output color in this exact front-to-back order:

1. Border
2. Front-priority sprites, `$D01B` bit clear, lower sprite number first
3. Foreground background graphics pixel
4. Behind-priority sprites, `$D01B` bit set, lower sprite number first
5. Background graphics/background color pixel

Rules:

- Border suppresses all sprite and background display output.
- Border is visually on top of sprites.
- Sprite number order is strict: sprite 0 is in front of sprite 1, sprite 1 is in front of sprite 2, and so on through sprite 7.
- Sprite-sprite collision detection is independent of which sprite wins priority.
- Sprite-background collision detection is independent of whether the sprite is in front of or behind the background.

Suggested implementation flow per pixel:

```c
uint8_t vicii_compose_pixel(Vicii *vic, ViciiBgPixel bg, ViciiSpritePixel sprites[8], bool border_active)
{
    vicii_update_sprite_collisions(vic, bg, sprites);

    if(border_active) {
        return vic->border_color & 0x0f;
    }

    for(int i = 0; i < 8; i++) {
        if(sprites[i].opaque && ((vic->sprite_priority & (1u << i)) == 0)) {
            return sprites[i].color & 0x0f;
        }
    }

    if(bg.is_foreground) {
        return bg.color & 0x0f;
    }

    for(int i = 0; i < 8; i++) {
        if(sprites[i].opaque && ((vic->sprite_priority & (1u << i)) != 0)) {
            return sprites[i].color & 0x0f;
        }
    }

    return bg.color & 0x0f;
}
```

Adapt this to the project's renderer shape. The important sequencing is that collision detection sees the sprite and background candidates before visual priority removes any of them.

## Collision Detection Algorithm

Run collision detection once per output pixel after the background pixel and all eight sprite pixel candidates have been decoded for that pixel.

### Sprite-Sprite Collision

Algorithm:

1. Build an 8-bit mask of sprites whose current pixel is opaque.
2. If the mask has two or more bits set, OR the mask into `$D01E`'s latch.
3. If this operation added any new bit to the latch, set the IMMC flag in `$D019`.

Example:

```c
static void vicii_check_sprite_sprite_collision(Vicii *vic, uint8_t opaque_mask)
{
    if((opaque_mask & (opaque_mask - 1)) == 0) {
        return;
    }

    uint8_t before = vic->sprite_sprite_collision;
    uint8_t after = before | opaque_mask;
    vic->sprite_sprite_collision = after;

    if(after != before) {
        vicii_set_irq_flag(vic, VICII_IRQ_IMMC);
    }
}
```

Notes:

- This sets all participating sprites at once.
- It works for two-sprite and many-sprite collisions.
- It does not care which sprite appears in front visually.

### Sprite-Background Collision

Algorithm:

1. If the background pixel is not foreground, no sprite-background collision occurs.
2. Build an 8-bit mask of sprites whose current pixel is opaque.
3. If the mask is non-zero, OR the mask into `$D01F`'s latch.
4. If this operation added any new bit to the latch, set the IMBC flag in `$D019`.

Example:

```c
static void vicii_check_sprite_background_collision(Vicii *vic, bool bg_foreground, uint8_t opaque_mask)
{
    if(!bg_foreground || opaque_mask == 0) {
        return;
    }

    uint8_t before = vic->sprite_background_collision;
    uint8_t after = before | opaque_mask;
    vic->sprite_background_collision = after;

    if(after != before) {
        vicii_set_irq_flag(vic, VICII_IRQ_IMBC);
    }
}
```

Notes:

- This intentionally ignores `$D01B`.
- A behind-priority sprite still collides with foreground graphics.
- A front-priority sprite also collides with foreground graphics.

## Register Read/Write Behavior

Update the VIC register read/write dispatch.

### Reads

- `$D01B`: return stored sprite priority register.
- `$D01E`: return `sprite_sprite_collision`, then clear `sprite_sprite_collision` to `0x00`.
- `$D01F`: return `sprite_background_collision`, then clear `sprite_background_collision` to `0x00`.

Pseudocode:

```c
case 0x1b:
    return vic->sprite_priority;

case 0x1e: {
    uint8_t value = vic->sprite_sprite_collision;
    vic->sprite_sprite_collision = 0;
    return value;
}

case 0x1f: {
    uint8_t value = vic->sprite_background_collision;
    vic->sprite_background_collision = 0;
    return value;
}
```

### Writes

- `$D01B`: store value.
- `$D01E`: ignore.
- `$D01F`: ignore.

Pseudocode:

```c
case 0x1b:
    vic->sprite_priority = value;
    break;

case 0x1e:
case 0x1f:
    break;
```

## IRQ Integration Details

Use the existing IRQ update mechanism. Do not create a second IRQ path.

Expected helper shape, if not already present:

```c
static void vicii_set_irq_flag(Vicii *vic, uint8_t flag)
{
    vic->irq_status |= flag;
    vicii_update_irq_line(vic);
}
```

The existing IRQ update function should set or clear `$D019` bit 7 and the machine IRQ line based on:

```c
(vic->irq_status & vic->irq_enable & VICII_IRQ_SOURCE_MASK) != 0
```

where the source mask excludes bit 7 and includes raster, IMBC, IMMC, and light pen source bits as appropriate.

Important:

- Reading `$D01E` or `$D01F` clears only the collision latch register, not the `$D019` IRQ flag.
- The CPU clears `$D019` source bits through the existing write-one-to-clear behavior.
- If a collision occurs again after the CPU clears `$D019`, the IRQ must be able to fire again even if `$D01E` or `$D01F` still contains previously latched bits from an earlier collision.

To satisfy the last point, do not gate IRQ setting only on `$D01E` / `$D01F` changing from zero to non-zero. Instead, set the IRQ source flag when a collision condition is observed and the corresponding `$D019` source flag is not already pending. Also OR the collision bits into the latch.

Better logic:

```c
static void vicii_note_sprite_sprite_collision(Vicii *vic, uint8_t mask)
{
    if((mask & (mask - 1)) == 0) {
        return;
    }

    vic->sprite_sprite_collision |= mask;

    if((vic->irq_status & VICII_IRQ_IMMC) == 0) {
        vicii_set_irq_flag(vic, VICII_IRQ_IMMC);
    }
}

static void vicii_note_sprite_background_collision(Vicii *vic, bool bg_foreground, uint8_t mask)
{
    if(!bg_foreground || mask == 0) {
        return;
    }

    vic->sprite_background_collision |= mask;

    if((vic->irq_status & VICII_IRQ_IMBC) == 0) {
        vicii_set_irq_flag(vic, VICII_IRQ_IMBC);
    }
}
```

This matches the desired behavior: the first collision after the IRQ flag is clear raises the interrupt; more collisions while the flag is pending update the collision register but do not create a new edge.

## DEN and Border Edge Cases

### DEN Clear

The parent plan explicitly requires collision detection even when `DEN=0`.

Implementation guidance:

- Do not disable sprite collision detection just because `DEN` is clear.
- Continue evaluating sprite-sprite collisions whenever sprite pixel candidates exist.
- For sprite-background collisions, use the background foreground classification produced by the display pipeline. If the current code suppresses background generation entirely when `DEN=0`, refactor enough to still know whether the underlying graphics pixel would have been foreground.

### Border Active

The parent plan explicitly says border always appears in front of sprites.

Implementation guidance:

- Border must win final color output.
- Do not use border activity to suppress sprite-sprite collision detection.
- For sprite-background collision, prefer hardware-faithful behavior if already known from the codebase or tests. If no project-specific rule exists, keep the collision algorithm independent of visual priority and do not let border visual priority erase the underlying sprite/background candidates.
- Add a code comment if this behavior is intentionally approximate.

## Suggested Internal Fields

Add fields to the VIC-II state struct if not already present:

```c
uint8_t sprite_priority;              /* $D01B */
uint8_t sprite_sprite_collision;      /* $D01E latch, clear on read */
uint8_t sprite_background_collision;  /* $D01F latch, clear on read */
```

Use existing naming conventions in the project. If register storage is centralized in a register array, still consider named fields or accessors for these latches because `$D01E` and `$D01F` are not ordinary read/write registers.

## Implementation Steps

1. Inspect the current VIC-II register read/write implementation.
   - Confirm `$D01B`, `$D01E`, and `$D01F` behavior.
   - Add named constants for register offsets and IRQ bits if missing.

2. Add collision latch state.
   - Initialize both collision latches to `0x00` on reset.
   - Preserve normal reset behavior for `$D01B` according to the project's existing register reset policy.

3. Update register reads and writes.
   - `$D01B` read/write is ordinary storage.
   - `$D01E` and `$D01F` read-clear.
   - `$D01E` and `$D01F` writes ignored.

4. Extend the background pixel path.
   - Return both color and foreground/non-foreground classification.
   - Verify classification for every Phase C graphics mode.
   - Treat invalid-mode display pixels as non-foreground black.

5. Extend the sprite pixel path.
   - Produce per-sprite opaque/color candidates for the current pixel.
   - Use final expanded sprite pixels, not raw sprite data bits.
   - Preserve sprite number order.

6. Add collision detection to the per-pixel render path.
   - Build an opaque sprite mask.
   - OR collision participants into `$D01E` and `$D01F` latches.
   - Set IMMC / IMBC through the existing IRQ mechanism.

7. Update final pixel priority composition.
   - Border first.
   - Front-priority sprites in ascending sprite number.
   - Foreground background pixel.
   - Behind-priority sprites in ascending sprite number.
   - Background pixel.

8. Add tests.
   - Prefer small deterministic tests that set registers directly and render a controlled frame or line.
   - Include register read-clear behavior tests.
   - Include IRQ behavior tests.
   - Include visual priority tests if the project has pixel/framebuffer assertions.

## Test Plan

### Unit Tests: Register Behavior

Test `$D01B`:

- Write `0x00`, read back `0x00`.
- Write `0xff`, read back `0xff`.
- Write `0xa5`, read back `0xa5`.

Test `$D01E`:

- Force internal latch to `0x03`.
- Read `$D01E`; expect `0x03`.
- Read `$D01E` again; expect `0x00`.
- Write `0xff` to `$D01E`; read should still reflect no write effect.

Test `$D01F`:

- Force internal latch to `0x04`.
- Read `$D01F`; expect `0x04`.
- Read `$D01F` again; expect `0x00`.
- Write `0xff` to `$D01F`; read should still reflect no write effect.

### Unit Tests: Sprite-Sprite Collision

Set up two enabled sprites with opaque pixels at the same screen coordinate.

Expected:

- `$D01E` contains both sprite bits, for example `0x03` for sprite 0 and sprite 1.
- If `$D01A` enables IMMC, `$D019` has IMMC pending and aggregate IRQ set.
- Reading `$D01E` clears the collision latch.
- Clearing `$D019` and causing a new collision can trigger IMMC again.

Also test three sprites overlapping one pixel:

- Expected `$D01E` mask includes all three participants.

### Unit Tests: Sprite-Background Collision

Set up one enabled sprite with an opaque pixel over a foreground background pixel.

Expected:

- `$D01F` contains that sprite bit.
- If `$D01A` enables IMBC, `$D019` has IMBC pending and aggregate IRQ set.
- Reading `$D01F` clears the collision latch.
- `$D01B` front/behind setting does not change whether the collision is detected.

Also test an opaque sprite over a background-color pixel:

- Expected no `$D01F` bit set.

### Unit Tests: Priority

Use two overlapping sprites with different colors:

- Sprite 0 and sprite 1 overlap.
- Both are front priority.
- Expected output color is sprite 0 color.
- `$D01E` contains both bits.

Use one front-priority sprite over foreground graphics:

- `$D01B` bit clear.
- Expected output color is sprite color.
- `$D01F` contains sprite bit.

Use one behind-priority sprite over foreground graphics:

- `$D01B` bit set.
- Expected output color is background foreground color.
- `$D01F` still contains sprite bit.

Use one behind-priority sprite over background-color graphics:

- `$D01B` bit set.
- Expected output color is sprite color, because there is no foreground background pixel to hide it.
- `$D01F` is not set.

Use border active over a sprite:

- Expected output color is border color.
- Sprite-sprite collision should still be evaluated if another sprite overlaps at that pixel.

### Integration Tests

If the emulator has a framebuffer test harness:

- Render a small scene with all eight sprites stacked at one coordinate. Verify sprite 0 is visually frontmost and `$D01E == 0xff` before read-clear.
- Render a sprite behind text. Verify text is visible, sprite-background collision is latched, and the sprite appears in background-only gaps.
- Render a sprite in front of text. Verify sprite is visible, sprite-background collision is latched.

If the emulator has a 6510-driven test harness:

- Write a small program that enables collision IRQs, waits for raster/frame progression, reads `$D01E` / `$D01F`, and confirms read-clear semantics.

## Acceptance Criteria

Phase E is complete when all of these are true:

- `$D01B` controls whether each sprite appears in front of or behind foreground background graphics.
- Border always appears in front of sprites.
- Sprite priority order is strictly sprite 0 over sprite 1 over ... over sprite 7.
- `$D01E` sets all participating sprite bits when two or more non-transparent sprite pixels overlap.
- `$D01E` is read-only and clears on read.
- `$D01F` sets sprite bits when non-transparent sprite pixels overlap foreground background pixels.
- `$D01F` is read-only and clears on read.
- Sprite collision detection uses non-transparent sprite pixels only.
- Sprite-background collision uses foreground background pixels only.
- Sprite-background collision still happens for sprites behind the background through `$D01B`.
- Collision detection still happens when `DEN=0`.
- IMMC and IMBC set `$D019` through the existing VIC IRQ path and respect `$D01A` enables for external IRQ assertion.
- Existing Phase A-D behavior continues to pass.

## Common Failure Modes

- Treating any non-zero background color as foreground. This is wrong; foreground is mode-dependent.
- Letting `$D01B` suppress sprite-background collision. Priority affects display, not collision.
- Clearing `$D019` when `$D01E` or `$D01F` is read. Only the collision latch clears on collision-register read.
- Failing to set all participants in a sprite-sprite collision. If sprites 0, 3, and 7 overlap at one pixel, `$D01E` must include bits 0, 3, and 7.
- Applying sprite priority before collision detection. Collisions must see all opaque sprite candidates.
- Ignoring expanded sprite pixels. Collisions occur after X/Y expansion effects.
- Treating invalid graphics modes as foreground black. The parent plan says invalid modes render black display pixels; for Phase E they should not become foreground blockers.
- Re-triggering collision IRQs every pixel while the IRQ flag is already pending. Latch collision bits continuously, but only create a new IRQ source event after the CPU has cleared the relevant `$D019` flag.

## Handoff Notes for the Coding Agent

Before editing code, locate:

- The VIC-II state struct.
- The VIC-II register read/write functions.
- The pixel/background renderer.
- The sprite pixel generation path from Phase D.
- The existing VIC IRQ status/enable helper.
- The test harness for VIC registers and framebuffer output.

Keep changes local to VIC-II unless the existing IRQ line helper requires a small machine-level call. Do not introduce timing changes in this phase.

Prefer simple, explicit code over cleverness. Phase E's correctness depends on preserving separate concepts: sprite opacity, background foreground classification, collision latches, IRQ flags, and final visual priority.
