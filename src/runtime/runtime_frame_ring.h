#pragma once

/* Rolling framebuffer ring - the black box for one-frame glitches.
 *
 * The CPU flight recorder answers "what executed before it went wrong". It
 * cannot answer "what did the screen actually show three frames ago", and a
 * human pausing a second after seeing a glitch is ~50 PAL frames too late:
 * those pixels are gone the instant the beam moves on. This ring keeps the last
 * N completed frames so the bad frame can still be found afterwards.
 *
 * Entries are keyed by both frame number and machine cycle, so a frame found
 * here gives the timestamp needed to query the flight recorder for the same
 * moment.
 *
 * Frames are stored as-is in the machine's native indexed8 representation.
 * Control clients either receive those indices directly or expand them through
 * the shared Pepto palette for legacy ARGB responses.
 */

#include "c64_frame.h"
#include "mutex.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    /* ~827 PAL frames, about 16.5 seconds at 50 fps: comfortably longer than
       the second or so it takes a human to react to a glitch and hit pause. */
    RUNTIME_FRAME_RING_DEFAULT_MEMORY_MB = 128,
    RUNTIME_FRAME_RING_MAX_MEMORY_MB = 4096
};

/* The runtime thread pushes; control commands read from the main thread. Access
   is serialized by `mutex`, the same shape as runtime_frame_slot's cross-thread
   frame handoff. Contention is a lock per completed frame, and readers copy out
   rather than borrowing, so no pointer outlives the lock. */
typedef struct runtime_frame_ring {
    mutex *mutex;
    c64_frame *slots;      /* capacity entries; NULL when disabled */
    uint32_t capacity;
    uint32_t count;        /* live entries, <= capacity */
    uint32_t head;         /* next slot to write */
    uint64_t dropped;      /* entries overwritten because the ring was full */
    bool recording;
} runtime_frame_ring;

typedef struct runtime_frame_ring_info {
    uint32_t capacity;
    uint32_t count;
    uint64_t dropped;
    uint64_t oldest_frame;
    uint64_t newest_frame;
    uint64_t oldest_cycle;
    uint64_t newest_cycle;
    uint64_t bytes;        /* resident slab size */
    bool recording;
} runtime_frame_ring_info;

/* Size the ring from a byte budget. Returns false (and leaves the ring safely
   zeroed) when the budget cannot hold a single frame, so a mis-set budget
   disables the ring loudly instead of silently recording nothing. */
bool runtime_frame_ring_init(runtime_frame_ring *ring, uint64_t budget_bytes);

void runtime_frame_ring_destroy(runtime_frame_ring *ring);

/* Drop every retained frame; capacity and recording state are preserved. */
void runtime_frame_ring_clear(runtime_frame_ring *ring);

void runtime_frame_ring_set_recording(runtime_frame_ring *ring, bool recording);

/* Drop retained frames with machine_cycle < cycle. Recording state is kept. */
void runtime_frame_ring_drop_older_than(runtime_frame_ring *ring, uint64_t cycle);

/* Copy one completed frame in, overwriting the oldest when full. Returns false
   when the ring is disabled, not recording, or the frame is NULL. */
bool runtime_frame_ring_push(runtime_frame_ring *ring, const c64_frame *frame);

void runtime_frame_ring_get_info(
    const runtime_frame_ring *ring,
    runtime_frame_ring_info *out_info);

/* Copy out the nearest retained frame at or before the target. Returns false
   when the ring is empty or the target predates the retained window (the frame
   was dropped, and saying so beats returning a neighbour that looks right); a
   target past the newest clamps to the newest, so scrubbing forward from a
   stale target lands on the most recent frame. Non-Inspector forensics may
   keep using this; Inspector film blit must use the exact-cycle helper. */
bool runtime_frame_ring_copy_by_frame(
    runtime_frame_ring *ring,
    uint64_t frame_number,
    c64_frame *out_frame);

bool runtime_frame_ring_copy_by_cycle(
    runtime_frame_ring *ring,
    uint64_t machine_cycle,
    c64_frame *out_frame);

/* Copy out the retained frame whose machine_cycle equals the target exactly.
   Returns false on miss (empty ring, dropped still, or any neighbour-only
   hit) — never substitutes nearest-≤. */
bool runtime_frame_ring_copy_by_cycle_exact(
    runtime_frame_ring *ring,
    uint64_t machine_cycle,
    c64_frame *out_frame);
