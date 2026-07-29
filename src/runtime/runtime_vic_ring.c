#include "runtime_vic_ring.h"

#include <stdlib.h>
#include <string.h>

static uint32_t runtime_vic_ring_tail(const runtime_vic_ring *ring) {
    return (ring->head + ring->capacity - ring->count) % ring->capacity;
}

static const vicii_line_record *runtime_vic_ring_at(
    const runtime_vic_ring *ring,
    uint32_t index) {
    return &ring->slots[(runtime_vic_ring_tail(ring) + index) % ring->capacity];
}

static bool runtime_vic_ring_usable(const runtime_vic_ring *ring) {
    return ring != NULL && ring->slots != NULL && ring->capacity > 0u;
}

static void runtime_vic_ring_lock(runtime_vic_ring *ring) {
    if (ring->mutex != NULL) {
        mutex_lock(ring->mutex);
    }
}

static void runtime_vic_ring_unlock(runtime_vic_ring *ring) {
    if (ring->mutex != NULL) {
        mutex_unlock(ring->mutex);
    }
}

bool runtime_vic_ring_init(runtime_vic_ring *ring, uint64_t budget_bytes) {
    uint64_t capacity;

    if (ring == NULL) {
        return false;
    }
    memset(ring, 0, sizeof(*ring));

    capacity = budget_bytes / (uint64_t)sizeof(vicii_line_record);
    if (capacity == 0u) {
        return false;
    }
    if (capacity > 0xffffffffu) {
        capacity = 0xffffffffu;
    }

    ring->slots = calloc((size_t)capacity, sizeof(vicii_line_record));
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

void runtime_vic_ring_destroy(runtime_vic_ring *ring) {
    if (ring == NULL) {
        return;
    }
    mutex_destroy(ring->mutex);
    free(ring->slots);
    memset(ring, 0, sizeof(*ring));
}

void runtime_vic_ring_clear(runtime_vic_ring *ring) {
    if (ring == NULL) {
        return;
    }
    runtime_vic_ring_lock(ring);
    ring->count = 0u;
    ring->head = 0u;
    ring->dropped = 0u;
    runtime_vic_ring_unlock(ring);
}

void runtime_vic_ring_set_recording(runtime_vic_ring *ring, bool recording) {
    if (ring == NULL) {
        return;
    }
    runtime_vic_ring_lock(ring);
    ring->recording = recording;
    runtime_vic_ring_unlock(ring);
}

bool runtime_vic_ring_push(
    runtime_vic_ring *ring,
    const vicii_line_record *record) {
    if (!runtime_vic_ring_usable(ring) || record == NULL) {
        return false;
    }

    runtime_vic_ring_lock(ring);
    if (!ring->recording) {
        runtime_vic_ring_unlock(ring);
        return false;
    }
    ring->slots[ring->head] = *record;
    ring->head = (ring->head + 1u) % ring->capacity;
    if (ring->count < ring->capacity) {
        ring->count++;
    } else {
        ring->dropped++;
    }
    runtime_vic_ring_unlock(ring);
    return true;
}

void runtime_vic_ring_get_info(
    const runtime_vic_ring *ring,
    runtime_vic_ring_info *out_info) {
    runtime_vic_ring *mutable_ring = (runtime_vic_ring *)ring;

    if (out_info == NULL) {
        return;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (ring == NULL) {
        return;
    }

    runtime_vic_ring_lock(mutable_ring);
    out_info->capacity = ring->capacity;
    out_info->count = ring->count;
    out_info->dropped = ring->dropped;
    out_info->recording = ring->recording;
    out_info->bytes =
        (uint64_t)ring->capacity * (uint64_t)sizeof(vicii_line_record);

    if (runtime_vic_ring_usable(ring) && ring->count > 0u) {
        const vicii_line_record *oldest = runtime_vic_ring_at(ring, 0u);
        const vicii_line_record *newest =
            runtime_vic_ring_at(ring, ring->count - 1u);
        out_info->oldest_frame = oldest->frame_number;
        out_info->newest_frame = newest->frame_number;
        out_info->oldest_raster = oldest->raster_line;
        out_info->newest_raster = newest->raster_line;
        out_info->oldest_cycle = oldest->machine_cycle;
        out_info->newest_cycle = newest->machine_cycle;
    }
    runtime_vic_ring_unlock(mutable_ring);
}

uint32_t runtime_vic_ring_copy_range(
    runtime_vic_ring *ring,
    bool has_frame,
    uint64_t frame_number,
    uint16_t raster_first,
    uint16_t raster_last,
    uint32_t limit,
    vicii_line_record *out) {
    uint32_t copied = 0u;
    uint32_t i;

    if (!runtime_vic_ring_usable(ring) || out == NULL || limit == 0u) {
        return 0u;
    }

    runtime_vic_ring_lock(ring);
    /* Records are pushed in raster order, so a straight scan preserves order.
       The window is bounded by the byte budget and the scan is a couple of
       integer compares per record, so no index is warranted. */
    for (i = 0; i < ring->count && copied < limit; ++i) {
        const vicii_line_record *record = runtime_vic_ring_at(ring, i);

        if (has_frame && record->frame_number != frame_number) {
            /* Frames only advance, so once past the target there is no more. */
            if (record->frame_number > frame_number) {
                break;
            }
            continue;
        }
        if (record->raster_line < raster_first ||
            record->raster_line > raster_last) {
            continue;
        }
        out[copied++] = *record;
    }
    runtime_vic_ring_unlock(ring);

    return copied;
}
