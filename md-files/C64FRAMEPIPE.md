# C64FRAMEPIPE.md

# Detailed Implementation Guide
# Phase 5 - Runtime Frame Pipeline

## Purpose

This document is intended for an implementation agent.

Phase 5 creates the frame publication path from the emulated machine/runtime to the frontend.

This phase is not real VIC-II emulation.

The goal is to prove that the emulator can safely move a complete video frame from the runtime side to the UI/frontend side without the frontend touching live machine memory.

At completion, the frontend should be able to display a generated test frame that came through the same ownership and copy pipeline that real VIC-II frames will later use.

---

# Context

Earlier phases established:

- CPU execution through the C64 bus
- ROM loading
- reset and bounded stepping
- runtime thread ownership of the machine
- scheduler/run/pause behavior
- developer hotkeys for run, step, and break

Phase 5 adds:

- frame buffer representation
- runtime-owned frame generation
- copied frame publication
- frontend consumption
- SDL texture upload path
- test-pattern rendering

---

# High-Level Goal

Create this flow:

```text
runtime thread
    -> machine/frame producer
        -> copied frame snapshot
            -> runtime event or frame queue
                -> frontend thread
                    -> SDL texture upload
                        -> visible window
```

The frontend must not read live machine state.

---

# Non-Goals

Do NOT implement:

- real VIC-II emulation
- character rendering
- C64 screen RAM interpretation
- color RAM interpretation
- sprites
- raster timing
- badlines
- raster interrupts
- PAL/NTSC accuracy
- frame pacing polish
- audio/video sync

Those belong to later phases.

This phase proves the frame transport and presentation architecture only.

---

# Architectural Rule

Allowed:

```text
runtime thread creates frame copy
frontend receives frame copy
frontend uploads copy to texture
```

Forbidden:

```text
frontend -> live machine RAM
frontend -> live VIC-II state
frontend -> live bus
frontend -> live runtime internals
```

The frame must cross the runtime/frontend boundary as owned/copyable data.

---

# Deliverable 1 - Define Frame Format

Create a simple frame structure.

Suggested format:

```c
#define C64_FRAME_WIDTH  384
#define C64_FRAME_HEIGHT 272

typedef struct c64_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint64_t frame_number;
    uint64_t machine_cycle;
    uint32_t pixels[C64_FRAME_WIDTH * C64_FRAME_HEIGHT];
} c64_frame;
```

Recommended pixel format:

```text
32-bit ARGB or RGBA
```

Pick the format that best matches the existing SDL texture code.

Document it clearly.

---

# Deliverable 2 - Frame Ownership Rules

Define ownership explicitly.

Runtime/machine side:

- owns the live working frame buffer
- may mutate it during generation
- must not expose live pointer to frontend

Runtime event/queue side:

- owns copied frame payload
- may pass it safely to frontend

Frontend side:

- consumes copied frame
- uploads copied pixels to texture
- may discard old frames

No shared mutable frame memory in this phase.

---

# Deliverable 3 - Frame Producer API

Create a frame producer function.

Suggested:

```c
bool c64_generate_test_frame(
    c64_t *c64,
    c64_frame *out_frame);
```

or:

```c
bool c64_make_frame_snapshot(
    c64_t *c64,
    c64_frame *out_frame);
```

Requirements:

- called on runtime thread
- fills an entire frame
- includes frame_number
- includes machine_cycle
- does not call SDL
- does not allocate every frame if avoidable

---

# Deliverable 4 - Test Pattern

Generate a visible deterministic test pattern.

Acceptable patterns:

- checkerboard
- color bars
- moving diagonal
- border rectangle
- frame counter pattern

Recommended:

```text
static border + changing inner pattern based on frame_number
```

This makes it obvious that frames are updating.

Example intent:

```text
outer border: fixed color
inner area: checkerboard or gradient
one small region changes every frame
```

No C64 accuracy is required here.

---

# Deliverable 5 - Runtime Frame Event Or Queue

Add a way for runtime to publish frames.

Acceptable options:

```text
RUNTIME_EVENT_FRAME_READY with copied frame payload
```

or:

```text
single-slot latest-frame queue
```

Recommended for early bring-up:

```text
single-slot latest-frame queue
```

Reason:

- avoids unbounded memory growth
- frontend only needs latest frame
- dropped frames are acceptable

The queue must be thread-safe.

---

# Deliverable 6 - Frame Publication Timing

Publish frames at a simple deterministic cadence.

For early bring-up, do not try to match PAL exactly.

Acceptable options:

```text
publish every N machine cycles
publish when runtime receives REQUEST_FRAME
publish periodically from runtime loop
```

Recommended initial option:

```text
publish every fixed number of machine cycles while running
```

Example:

```text
frame_interval_cycles = 20000
```

The exact value is not important in this phase.

It only needs to produce visible updates.

Also allow manual/requested frame publication if easy.

---

# Deliverable 7 - Runtime Commands

Add or support:

```text
REQUEST_FRAME
```

Optional:

```text
SET_FRAME_TEST_PATTERN
```

Required behavior:

- REQUEST_FRAME asks runtime to generate/publish one copied frame
- works while paused
- works while running
- does not expose live machine state

This is useful for testing before continuous frame publication is tuned.

---

# Deliverable 8 - Frontend Frame Consumption

Frontend should:

1. poll or receive latest frame
2. validate dimensions/pixel format
3. upload pixels to SDL texture
4. render texture
5. keep UI/debug overlay behavior intact

Do not block the frontend waiting for a frame.

If no frame is available, keep displaying the previous frame or clear to a default color.

---

# Deliverable 9 - SDL Texture Path

Create or update an SDL texture suitable for the frame format.

Requirements:

- texture dimensions match frame dimensions
- texture pixel format matches frame pixels or conversion is explicit
- texture update uses copied frame pixels
- render path does not touch runtime internals

Suggested behavior:

```text
on first frame:
    create texture

on later frames:
    update texture
```

If dimensions change unexpectedly, recreate the texture.

---

# Deliverable 10 - Debug Overlay Integration

Keep the existing developer debug controls useful.

Overlay should continue to show:

- RUNNING/PAUSED
- PC
- registers
- cycle count

Optionally add:

- frame number
- last frame cycle
- frame queue drops

This will help diagnose whether the runtime is producing frames.

---

# Deliverable 11 - Backpressure And Dropped Frames

Do not queue unlimited frames.

Preferred model:

```text
latest frame wins
```

If runtime produces a new frame before frontend consumes the old one:

```text
replace old frame
increment dropped-frame counter
```

Dropped frames are acceptable in this phase.

Unbounded frame queues are not acceptable.

---

# Deliverable 12 - Thread Safety

Frame handoff must be safe.

Acceptable approaches:

- mutex-protected latest-frame slot
- lock-free single-producer/single-consumer slot if already available
- runtime event queue with owned frame payload

Keep it simple.

Avoid sharing a pointer to a mutable runtime buffer.

---

# Deliverable 13 - Tests

Add tests where practical.

## Test 1 - Frame Generation

Call test-frame producer.

Expected:

- width is correct
- height is correct
- stride is correct
- pixels are not all zero
- frame number is set

## Test 2 - Frame Copy Ownership

Generate a frame.

Modify the machine's working buffer or generate another frame.

Verify the previously published frame copy remains stable.

## Test 3 - Runtime Request Frame

Send REQUEST_FRAME while paused.

Expected:

- frame is published
- runtime remains paused
- machine state is not advanced unexpectedly unless explicitly designed

## Test 4 - Runtime Frame While Running

Start RUN.

Allow enough cycles for frame publication.

Expected:

- at least one frame appears
- machine continues running
- runtime remains responsive to PAUSE

## Test 5 - Frontend Texture Update

If frontend tests exist:

- inject a known frame
- verify texture update path accepts it without crash

Manual visual verification is acceptable initially.

---

# Recommended Implementation Order

1. Define c64_frame structure and pixel format.
2. Add frame ownership comments/documentation.
3. Add test-frame generator on machine/runtime side.
4. Add frame_number and machine_cycle fields.
5. Add thread-safe latest-frame slot or frame event.
6. Add REQUEST_FRAME command.
7. Add runtime publication after REQUEST_FRAME.
8. Add frontend frame polling/consumption.
9. Add SDL texture creation/update.
10. Add continuous frame publication while running.
11. Add overlay fields for frame_number and frame cycle.
12. Add tests.
13. Verify run/pause/step hotkeys still work.

---

# Acceptance Criteria

Phase 5 is complete when:

- A frame structure exists.
- Pixel format is documented.
- Runtime can generate a complete test frame.
- Frame handoff crosses runtime/frontend boundary as a copy or owned payload.
- Frontend never touches live machine memory.
- Frontend can display the test frame.
- REQUEST_FRAME works while paused.
- Runtime can publish frames while running.
- Dropped frames cannot cause unbounded memory growth.
- Existing run/pause/step controls still work.
- Debug overlay remains usable.
- Tests or manual checks prove frame generation and display.

---

# End State

At the end of Phase 5, the emulator has a proven frame pipeline:

```text
runtime/machine
    -> generated frame
        -> copied handoff
            -> frontend
                -> SDL texture
                    -> visible output
```

The image is still synthetic.

That is intentional.

The next phase can replace the synthetic frame producer with a minimal VIC-II skeleton without changing the frontend ownership model.
