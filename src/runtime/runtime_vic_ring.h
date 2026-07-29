#pragma once

/* Per-line VIC-II derived-state ring.
 *
 * The frame ring retains what the screen showed; this retains what the VIC was
 * doing while it showed it. Pixels tell you *that* a frame is wrong - this
 * tells you *why*: which sprites actually had data on a line, whether it was a
 * bad line, and above all the sprite X (including the $D010 MSB) that was
 * really latched for painting, which the CPU flight recorder cannot
 * reconstruct from register writes alone.
 *
 * Records share `machine_cycle` with the frame ring and the flight recorder,
 * so one moment can be examined from all three.
 */

#include "mutex.h"
#include "vicii.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    /* ~157k lines, about 500 PAL frames or 10 seconds at 50 fps. Records are
       small, so this is cheap next to the frame ring. */
    RUNTIME_VIC_RING_DEFAULT_MEMORY_MB = 16,
    RUNTIME_VIC_RING_MAX_MEMORY_MB = 1024
};

typedef struct runtime_vic_ring {
    mutex *mutex;
    vicii_line_record *slots;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
    uint64_t dropped;
    bool recording;
} runtime_vic_ring;

typedef struct runtime_vic_ring_info {
    uint32_t capacity;
    uint32_t count;
    uint64_t dropped;
    uint64_t oldest_frame;
    uint64_t newest_frame;
    uint16_t oldest_raster;
    uint16_t newest_raster;
    uint64_t oldest_cycle;
    uint64_t newest_cycle;
    uint64_t bytes;
    bool recording;
} runtime_vic_ring_info;

bool runtime_vic_ring_init(runtime_vic_ring *ring, uint64_t budget_bytes);
void runtime_vic_ring_destroy(runtime_vic_ring *ring);
void runtime_vic_ring_clear(runtime_vic_ring *ring);
void runtime_vic_ring_set_recording(runtime_vic_ring *ring, bool recording);

/* Called from the VIC observer on the runtime thread, once per raster line. */
bool runtime_vic_ring_push(
    runtime_vic_ring *ring,
    const vicii_line_record *record);

void runtime_vic_ring_get_info(
    const runtime_vic_ring *ring,
    runtime_vic_ring_info *out_info);

/* Copy matching records oldest-first into `out` (which must hold `limit`).
   When `has_frame` is false the frame filter is skipped and the raster window
   matches in every retained frame. Returns the number copied. */
uint32_t runtime_vic_ring_copy_range(
    runtime_vic_ring *ring,
    bool has_frame,
    uint64_t frame_number,
    uint16_t raster_first,
    uint16_t raster_last,
    uint32_t limit,
    vicii_line_record *out);
