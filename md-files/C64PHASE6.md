C64 Bring-Up Phase 6

Minimal VIC-II Integration - Implementation Guide

Prepared for the implementation agent

Purpose

Phase 6 replaces the synthetic frame producer from Phase 5 with a minimal machine-owned VIC-II skeleton that generates visible output through the existing copied-frame pipeline.

This is not cycle-perfect VIC-II emulation. The goal is to add the first real video component inside machine/, prove deterministic raster progression, route minimal VIC-II register access through the C64 bus, and publish machine-generated frames without changing frontend ownership rules.

Source Context

High-Level Goal

runtime thread
    -> c64_step_cycle()
        -> cpu/bus work
        -> vicii_step_cycle()
            -> live VIC-II working frame
        -> completed c64_frame snapshot
            -> existing runtime latest-frame handoff
                -> frontend SDL texture upload
                    -> visible machine-generated output

Non-Goals

No cycle-perfect VIC-II behavior.

No badline accuracy.

No sprites.

No raster interrupts yet.

No character ROM glyph rendering yet.

No color RAM integration yet, unless it is already trivial and isolated.

No CIA dependency.

No frontend access to live machine, bus, RAM, or VIC-II state.

No SDL, Nuklear, or platform types in machine/ or runtime/.

No attempt to boot to the BASIC screen in this phase.

Architectural Rules

Allowed:
    machine/VIC-II mutates live video state on runtime thread
    machine creates a completed c64_frame snapshot
    runtime publishes the copied/latest frame
    frontend uploads copied frame pixels to SDL

Forbidden:
    frontend -> machine
    frontend -> live VIC-II state
    frontend -> bus/RAM
    runtime -> frontend/platform/SDL
    machine -> runtime/frontend/platform/SDL

Deliverable 1 - Add VIC-II Skeleton

Create a machine-owned VIC-II module with opaque public type and small lifecycle API.

src/machine/vicii.c
src/machine/vicii.h

typedef struct vicii vicii;

bool vicii_init(vicii *v, char *error, size_t error_size);
void vicii_reset(vicii *v);
void vicii_step_cycle(vicii *v);
void vicii_destroy(vicii *v);

The VIC-II object belongs to c64/machine state.

It must not allocate per cycle.

It must not call runtime, frontend, platform, SDL, or Nuklear.

It may own a live working frame buffer or draw directly into the machine-owned frame buffer introduced in Phase 5.

It must expose only snapshot and register APIs needed by machine/runtime.

Deliverable 2 - Integrate VIC-II into c64 Machine State

Add VIC-II ownership to the main C64 machine struct and initialize/reset/step it from machine code.

struct c64{
    /* existing CPU, bus, RAM, ROM, scheduler, frame fields */
    vicii vic;
};

bool c64_init(...){
    /* existing init */
    vicii_init(&c64->vic, error, error_size);
}

bool c64_reset(...){
    /* existing reset */
    vicii_reset(&c64->vic);
}

bool c64_step_cycle(c64_t *c64){
    /* existing CPU/scheduler cycle */
    vicii_step_cycle(&c64->vic);
    return true;
}

The scheduler remains the source of machine-cycle progression.

VIC-II stepping must be deterministic and tied to machine cycles.

Do not create a separate VIC thread.

Keep CPU execution through existing bus callbacks.

Deliverable 3 - VIC-II Register Surface

Implement a minimal register array for $D000-$D3FF visibility. Do not overbuild behavior.

#define VICII_REGISTER_COUNT 0x40

uint8_t vicii_read_register(vicii *v, uint16_t addr);
void vicii_write_register(vicii *v, uint16_t addr, uint8_t value);

/* addr is CPU address in $D000-$D3FF. */
uint8_t reg = addr & 0x3f;

Mirror the 64-byte register block across $D000-$D3FF using addr & 0x3f.

Store writes for later phases, even when the value has no current effect.

Implement enough readable state for raster diagnostics: current raster low byte and high bit if the existing register model exposes it.

For unimplemented reads, return the stored register value or a documented deterministic placeholder.

Do not implement IRQ side effects yet unless they are fully isolated and tested.

Deliverable 4 - Route I/O Bus Access to VIC-II

Replace the placeholder I/O behavior for the VIC-II address range with calls into the machine-owned VIC-II object.

if(addr >= 0xd000 && addr <= 0xd3ff){
    return vicii_read_register(&c64->vic, addr);
}

if(addr >= 0xd000 && addr <= 0xd3ff){
    vicii_write_register(&c64->vic, addr, value);
    return;
}

This must occur only when the I/O region is visible according to the existing C64 banking model.

If RAM is banked into $D000-$DFFF, reads/writes must continue to hit RAM rather than VIC-II registers.

Do not route frontend or runtime around the bus for register access.

Keep SID, CIA, color RAM, and expansion I/O as placeholders unless already implemented elsewhere.

Deliverable 5 - Raster Timing Foundation

Add a simple deterministic raster counter. Accuracy is intentionally limited; repeatability matters more than hardware exactness in this phase.

typedef struct vicii_timing{
    uint32_t cycles_per_line;
    uint32_t lines_per_frame;
    uint32_t cycle_in_line;
    uint32_t raster_line;
    uint64_t frame_number;
    bool frame_complete;
} vicii_timing;

Use named constants, not magic values scattered through code.

Start with one selected standard from configuration, but make the timing struct capable of PAL/NTSC later.

When cycle_in_line reaches cycles_per_line, reset it and increment raster_line.

When raster_line reaches lines_per_frame, reset it, increment frame_number, and mark frame_complete.

Expose a machine-side way to test and clear frame_complete when producing snapshots.

void vicii_step_cycle(vicii *v){
    v->timing.cycle_in_line++;

    if(v->timing.cycle_in_line >= v->timing.cycles_per_line){
        v->timing.cycle_in_line = 0;
        v->timing.raster_line++;

        if(v->timing.raster_line >= v->timing.lines_per_frame){
            v->timing.raster_line = 0;
            v->timing.frame_number++;
            v->timing.frame_complete = true;
        }
    }
}

Deliverable 6 - Minimal Screen Geometry

Render into the existing fixed frontend surface size.

#define C64_FRAME_WIDTH   384
#define C64_FRAME_HEIGHT  272

#define VICII_ACTIVE_X     32
#define VICII_ACTIVE_Y     36
#define VICII_ACTIVE_W     320
#define VICII_ACTIVE_H     200

The exact border placement can be adjusted later; keep it documented and stable for tests.

Fill the entire 384 x 272 frame every completed frame.

Outer region is border.

Inner 320 x 200 region is the minimal screen area.

Do not resize the frontend window or texture contract.

Deliverable 7 - Border Rendering

Render a visible border using a small C64-like palette table. Phase 6 may use stored register values for border/background if trivial.

static const uint32_t vicii_palette_argb[16] = {
    0xff000000, /* black */
    0xffffffff, /* white */
    0xff813338, /* red */
    0xff75cec8, /* cyan */
    0xff8e3c97, /* purple */
    0xff56ac4d, /* green */
    0xff2e2c9b, /* blue */
    0xffedf171, /* yellow */
    0xff8e5029, /* orange */
    0xff553800, /* brown */
    0xffc46c71, /* light red */
    0xff4a4a4a, /* dark gray */
    0xff7b7b7b, /* gray */
    0xffa9ff9f, /* light green */
    0xff706deb, /* light blue */
    0xffb2b2b2  /* light gray */
};

If the existing frame format is RGBA rather than ARGB, convert the table once and document it.

Use $D020 low nibble as border color if register storage is available.

Use a deterministic default border color after reset.

Tests should not depend on exact palette aesthetics; they should verify stable nonzero output and expected region changes.

Deliverable 8 - Minimal Screen Area Generation

Draw a simple machine-generated inner screen. It should come from VIC-II state, not the old synthetic frame generator.

For Phase 6, acceptable inner content is a deterministic pattern driven by raster/frame/VIC registers.

Preferred: fill inner screen with background color from $D021 and overlay a simple moving raster/stripe pattern based on frame_number.

Do not interpret screen RAM or character ROM yet; that is Phase 8.

Do not read frontend state to decide what to draw.

for(y = 0; y < C64_FRAME_HEIGHT; y++){
    for(x = 0; x < C64_FRAME_WIDTH; x++){
        bool active = x >= VICII_ACTIVE_X && x < VICII_ACTIVE_X + VICII_ACTIVE_W &&
                      y >= VICII_ACTIVE_Y && y < VICII_ACTIVE_Y + VICII_ACTIVE_H;

        if(active){
            pixel = background_color;
            if(((x + y + v->timing.frame_number) & 0x20) == 0){
                pixel = alternate_color;
            }
        } else{
            pixel = border_color;
        }

        frame->pixels[y * C64_FRAME_WIDTH + x] = pixel;
    }
}

Deliverable 9 - Replace Synthetic Frame Producer

Keep the Phase 5 frame handoff intact, but change the machine-side producer to ask the VIC-II for the latest completed frame.

bool c64_make_frame_snapshot(c64_t *c64, c64_frame *out_frame){
    if(c64 == NULL || out_frame == NULL){
        return false;
    }

    return vicii_make_frame_snapshot(&c64->vic, out_frame, c64->machine_cycle);
}

The output type should remain c64_frame unless there is a strong existing reason to rename it.

frame_number should come from VIC-II timing, not a standalone synthetic-frame counter.

machine_cycle should remain populated.

REQUEST_FRAME while paused should still work by rendering the current VIC-II state without requiring machine advancement.

Running publication should occur on completed VIC-II frames or a documented temporary cadence.

Deliverable 10 - Runtime Publication Policy

Use the existing latest-frame publication path. The runtime should not gain video-specific knowledge beyond requesting a c64_frame snapshot from the machine.

When running, publish when the VIC-II reports a completed frame.

When paused and REQUEST_FRAME arrives, publish one snapshot immediately.

Do not enqueue unlimited frames.

Do not block the runtime waiting for the frontend.

Do not change the frontend to read machine state directly.

Deliverable 11 - Minimal VIC-II Snapshot for Debugging

Add a copied VIC-II snapshot only if it fits the current runtime request/response model. This is useful for debugging, but visible video is the priority.

typedef struct c64_vicii_snapshot{
    uint32_t raster_line;
    uint32_t cycle_in_line;
    uint64_t frame_number;
    uint8_t border_color;
    uint8_t background_color;
} c64_vicii_snapshot;

If REQUEST_VIC_STATE already exists in the command enum, wire it to this copied snapshot.

Do not expose a vicii pointer to runtime_client or frontend.

The debug overlay may display raster_line and frame_number if easy.

Deliverable 12 - Tests

Add tests where practical. Synthetic ROMs are acceptable. Do not require copyrighted ROMs.

Test 1 - VIC-II Reset State

Create c64 or vicii directly through test helpers.

Call reset.

Expected: raster_line == 0, cycle_in_line == 0, frame_number == 0, default colors are deterministic.

Test 2 - Raster Progression

Step exactly cycles_per_line cycles.

Expected: cycle_in_line returns to 0 and raster_line increments by 1.

Step enough cycles for one frame.

Expected: frame_number increments and frame_complete becomes true.

Test 3 - Register Mirroring

Write a value to $D020.

Read from $D020 and a mirrored address such as $D060 if the bus maps $D000-$D3FF.

Expected: low 6-bit register mirror behavior is deterministic.

Test 4 - Banking Respects I/O Visibility

When I/O is visible, $D020 write updates VIC-II border register.

When RAM is banked over I/O, $D020 write updates RAM and does not update VIC-II.

Expected: existing banking rules remain authoritative.

Test 5 - Frame Snapshot Geometry

Request a frame snapshot.

Expected: width 384, height 272, stride unchanged, frame_number set from VIC-II, machine_cycle set.

Test 6 - Border Region

Set border color register.

Request a frame.

Expected: corner pixels use border color.

Test 7 - Active Region

Set background color register.

Request a frame.

Expected: a pixel inside the active 320 x 200 region differs from border or follows documented background behavior.

Test 8 - Runtime Requested Frame

Start runtime, reset/load as existing tests do.

Send REQUEST_FRAME while paused.

Expected: latest frame is published and runtime remains paused.

Test 9 - Runtime Running Frame

Start RUN.

Allow enough cycles for at least one VIC-II frame completion.

Expected: at least one frame appears, runtime remains responsive to PAUSE.

Test 10 - Frontend Manual Check

Run ./build/c64m --noini.

Expected: visible non-synthetic VIC-II border and inner screen pattern.

F9, F10, F11/Ctrl+S, F12/Ctrl+C remain usable.

Recommended Implementation Order

Add src/machine/vicii.h and src/machine/vicii.c.

Add vicii ownership to c64 machine state.

Initialize, reset, destroy, and step VIC-II from c64 machine code.

Add minimal VIC-II register storage and read/write functions.

Route $D000-$D3FF visible-I/O bus access to VIC-II.

Add raster timing counters and frame_complete flag.

Add deterministic frame generation into the existing c64_frame format.

Replace or redirect the Phase 5 synthetic frame producer to VIC-II snapshot generation.

Publish frames through the existing runtime latest-frame path.

Keep REQUEST_FRAME behavior working while paused.

Add optional copied VIC-II snapshot response for debugging.

Add machine tests for reset, raster progression, bus routing, and frame geometry.

Add runtime tests for requested and running VIC-II frame publication.

Run the full build/test/manual command set.

Build and Verification Commands

cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/test_c64_bus
./build/test_c64_frame
./build/test_c64_cpu_validation
./build/test_runtime_scheduler
./build/test_runtime_frame
./build/c64m --noini

Add new tests to this command set as they are introduced, for example test_c64_vicii and test_runtime_vicii_frame.

Acceptance Criteria

A machine-owned VIC-II skeleton exists under src/machine/.

The C64 machine owns, initializes, resets, steps, and destroys the VIC-II.

$D000-$D3FF visible-I/O access is routed to VIC-II register functions through the existing bus path.

Banking still controls whether $D000 accesses hit I/O or RAM.

Raster counters advance deterministically with machine cycles.

A completed frame increments a VIC-II frame counter or sets a frame-complete signal.

The existing c64_frame geometry and copied frame publication contract remain intact.

Synthetic Phase 5 frame generation is no longer the normal displayed frame path.

Frontend receives copied frames only and still does not touch live machine memory.

The visible output shows a border and inner screen area generated by VIC-II state.

REQUEST_FRAME works while paused.

Running mode publishes frames and remains responsive to PAUSE.

Existing run, pause, step, reset, scheduler, CPU validation, and frame pipeline tests still pass.

New VIC-II machine/runtime tests pass or manual checks are documented where automation is not yet practical.

End State

runtime
    -> c64 machine scheduler
        -> CPU and bus
        -> VIC-II skeleton
            -> deterministic raster state
            -> border + minimal screen pixels
        -> c64_frame snapshot
            -> latest-frame copy handoff
                -> frontend SDL texture
                    -> visible machine-generated output

At the end of Phase 6, the emulator should no longer display a purely synthetic test pattern during normal operation. It should display a minimal VIC-II-generated frame. The result is still not a BASIC screen; it is the first real video-device integration step that enables CIA foundations and later character display work.