#include "runtime_frame_ring.h"

#include <stdlib.h>
#include <string.h>

/* Oldest live entry. Valid only when count > 0. Callers hold the lock. */
static uint32_t runtime_frame_ring_tail(const runtime_frame_ring *ring) {
    return (ring->head + ring->capacity - ring->count) % ring->capacity;
}

static const c64_frame *runtime_frame_ring_at(
    const runtime_frame_ring *ring,
    uint32_t index) {
    return &ring->slots[(runtime_frame_ring_tail(ring) + index) % ring->capacity];
}

static bool runtime_frame_ring_usable(const runtime_frame_ring *ring) {
    return ring != NULL && ring->slots != NULL && ring->capacity > 0u;
}

static void runtime_frame_ring_lock(runtime_frame_ring *ring) {
    if (ring->mutex != NULL) {
        mutex_lock(ring->mutex);
    }
}

static void runtime_frame_ring_unlock(runtime_frame_ring *ring) {
    if (ring->mutex != NULL) {
        mutex_unlock(ring->mutex);
    }
}

bool runtime_frame_ring_init(runtime_frame_ring *ring, uint64_t budget_bytes) {
    uint64_t capacity;

    if (ring == NULL) {
        return false;
    }
    memset(ring, 0, sizeof(*ring));

    capacity = budget_bytes / (uint64_t)sizeof(c64_frame);
    if (capacity == 0u) {
        return false;
    }
    if (capacity > 0xffffffffu) {
        capacity = 0xffffffffu;
    }

    ring->slots = calloc((size_t)capacity, sizeof(c64_frame));
    if (ring->slots == NULL) {
        return false;
    }
    ring->mutex = mutex_create();
    if (ring->mutex == NULL) {
        free(ring->slots);
        memset(ring, 0, sizeof(*ring));
        return false;
    }
    ring->capacity = (uint32_t)capacity;
    ring->recording = true;
    return true;
}

void runtime_frame_ring_destroy(runtime_frame_ring *ring) {
    if (ring == NULL) {
        return;
    }
    mutex_destroy(ring->mutex);
    free(ring->slots);
    memset(ring, 0, sizeof(*ring));
}

void runtime_frame_ring_clear(runtime_frame_ring *ring) {
    if (ring == NULL) {
        return;
    }
    runtime_frame_ring_lock(ring);
    ring->count = 0u;
    ring->head = 0u;
    ring->dropped = 0u;
    runtime_frame_ring_unlock(ring);
}

void runtime_frame_ring_set_recording(runtime_frame_ring *ring, bool recording) {
    if (ring == NULL) {
        return;
    }
    runtime_frame_ring_lock(ring);
    ring->recording = recording;
    runtime_frame_ring_unlock(ring);
}

void runtime_frame_ring_drop_older_than(runtime_frame_ring *ring, uint64_t cycle) {
    if (ring == NULL || !runtime_frame_ring_usable(ring)) {
        return;
    }
    runtime_frame_ring_lock(ring);
    while (ring->count > 0u) {
        uint32_t tail =
            (ring->head + ring->capacity - ring->count) % ring->capacity;
        if (ring->slots[tail].machine_cycle >= cycle) {
            break;
        }
        ring->count--;
    }
    runtime_frame_ring_unlock(ring);
}

bool runtime_frame_ring_push(runtime_frame_ring *ring, const c64_frame *frame) {
    if (!runtime_frame_ring_usable(ring) || frame == NULL) {
        return false;
    }

    runtime_frame_ring_lock(ring);
    if (!ring->recording) {
        runtime_frame_ring_unlock(ring);
        return false;
    }
    ring->slots[ring->head] = *frame;
    ring->head = (ring->head + 1u) % ring->capacity;
    if (ring->count < ring->capacity) {
        ring->count++;
    } else {
        ring->dropped++;
    }
    runtime_frame_ring_unlock(ring);
    return true;
}

void runtime_frame_ring_get_info(
    const runtime_frame_ring *ring,
    runtime_frame_ring_info *out_info) {
    runtime_frame_ring *mutable_ring = (runtime_frame_ring *)ring;

    if (out_info == NULL) {
        return;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (ring == NULL) {
        return;
    }

    runtime_frame_ring_lock(mutable_ring);
    out_info->capacity = ring->capacity;
    out_info->count = ring->count;
    out_info->dropped = ring->dropped;
    out_info->recording = ring->recording;
    out_info->bytes = (uint64_t)ring->capacity * (uint64_t)sizeof(c64_frame);

    if (runtime_frame_ring_usable(ring) && ring->count > 0u) {
        const c64_frame *oldest = runtime_frame_ring_at(ring, 0u);
        const c64_frame *newest = runtime_frame_ring_at(ring, ring->count - 1u);
        out_info->oldest_frame = oldest->frame_number;
        out_info->newest_frame = newest->frame_number;
        out_info->oldest_cycle = oldest->machine_cycle;
        out_info->newest_cycle = newest->machine_cycle;
    }
    runtime_frame_ring_unlock(mutable_ring);
}

/* Entries are pushed in increasing frame/cycle order, so the live window is
   sorted and a binary search finds the last entry at or before the target.
   Callers hold the lock. */
static const c64_frame *runtime_frame_ring_find_locked(
    const runtime_frame_ring *ring,
    uint64_t target,
    bool by_cycle) {
    uint32_t low = 0u;
    uint32_t high;
    const c64_frame *best = NULL;

    if (!runtime_frame_ring_usable(ring) || ring->count == 0u) {
        return NULL;
    }

    high = ring->count - 1u;
    for (;;) {
        uint32_t mid = low + (high - low) / 2u;
        const c64_frame *entry = runtime_frame_ring_at(ring, mid);
        uint64_t key = by_cycle ? entry->machine_cycle : entry->frame_number;

        if (key <= target) {
            best = entry;
            if (mid == high) {
                break;
            }
            low = mid + 1u;
        } else {
            if (mid == low) {
                break;
            }
            high = mid - 1u;
        }
    }

    return best;
}

static bool runtime_frame_ring_copy(
    runtime_frame_ring *ring,
    uint64_t target,
    bool by_cycle,
    c64_frame *out_frame) {
    const c64_frame *found;
    bool ok = false;

    if (ring == NULL || out_frame == NULL) {
        return false;
    }

    runtime_frame_ring_lock(ring);
    found = runtime_frame_ring_find_locked(ring, target, by_cycle);
    if (found != NULL) {
        *out_frame = *found;
        ok = true;
    }
    runtime_frame_ring_unlock(ring);
    return ok;
}

bool runtime_frame_ring_copy_by_frame(
    runtime_frame_ring *ring,
    uint64_t frame_number,
    c64_frame *out_frame) {
    return runtime_frame_ring_copy(ring, frame_number, false, out_frame);
}

bool runtime_frame_ring_copy_by_cycle(
    runtime_frame_ring *ring,
    uint64_t machine_cycle,
    c64_frame *out_frame) {
    return runtime_frame_ring_copy(ring, machine_cycle, true, out_frame);
}
