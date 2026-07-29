/* Per-line VIC derived-state ring.
 *
 * The frame ring says *which* frame is wrong; this ring says *why*. It retains
 * the end-of-line VIC state - notably the sprite X actually latched for
 * painting, which is not necessarily what $D000..$D010 hold afterwards.
 */
#include "runtime_vic_ring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures++; \
        } \
    } while (0)

static void make_record(
    vicii_line_record *record,
    uint64_t frame,
    uint16_t raster,
    uint64_t cycle) {
    memset(record, 0, sizeof(*record));
    record->frame_number = frame;
    record->raster_line = raster;
    record->machine_cycle = cycle;
    /* Distinguishing payload so a query returning the wrong record is visible
       in the contents, not only in the keys. */
    record->sprite_x[0] = (uint16_t)(raster * 2u);
    record->border_color = (uint8_t)(frame & 0x0Fu);
}

static void push_frame(runtime_vic_ring *ring, uint64_t frame, uint16_t lines) {
    vicii_line_record record;
    uint16_t raster;

    for (raster = 0; raster < lines; ++raster) {
        make_record(&record, frame, raster, frame * 100000u + raster * 63u);
        runtime_vic_ring_push(ring, &record);
    }
}

static void test_budget_sets_capacity(void) {
    runtime_vic_ring ring;
    runtime_vic_ring_info info;

    CHECK(runtime_vic_ring_init(&ring, 100u * sizeof(vicii_line_record)));
    runtime_vic_ring_get_info(&ring, &info);
    CHECK(info.capacity == 100u);
    CHECK(info.count == 0u);
    CHECK(info.recording);
    runtime_vic_ring_destroy(&ring);

    CHECK(!runtime_vic_ring_init(&ring, 4u));
    runtime_vic_ring_destroy(&ring);
    CHECK(!runtime_vic_ring_init(&ring, 0u));
    runtime_vic_ring_destroy(&ring);
}

static void test_push_and_info(void) {
    runtime_vic_ring ring;
    runtime_vic_ring_info info;

    CHECK(runtime_vic_ring_init(&ring, 1000u * sizeof(vicii_line_record)));
    push_frame(&ring, 7u, 312u);

    runtime_vic_ring_get_info(&ring, &info);
    CHECK(info.count == 312u);
    CHECK(info.dropped == 0u);
    CHECK(info.oldest_frame == 7u);
    CHECK(info.newest_frame == 7u);
    CHECK(info.oldest_raster == 0u);
    CHECK(info.newest_raster == 311u);

    runtime_vic_ring_destroy(&ring);
}

static void test_wrap_drops_oldest(void) {
    runtime_vic_ring ring;
    runtime_vic_ring_info info;

    /* Room for two PAL frames; push three. */
    CHECK(runtime_vic_ring_init(&ring, 624u * sizeof(vicii_line_record)));
    push_frame(&ring, 1u, 312u);
    push_frame(&ring, 2u, 312u);
    push_frame(&ring, 3u, 312u);

    runtime_vic_ring_get_info(&ring, &info);
    CHECK(info.count == 624u);
    CHECK(info.dropped == 312u);
    CHECK(info.oldest_frame == 2u);
    CHECK(info.newest_frame == 3u);

    runtime_vic_ring_destroy(&ring);
}

static void test_copy_range_by_frame(void) {
    runtime_vic_ring ring;
    vicii_line_record *out = malloc(400u * sizeof(*out));
    uint32_t copied;

    CHECK(out != NULL);
    CHECK(runtime_vic_ring_init(&ring, 1000u * sizeof(vicii_line_record)));
    push_frame(&ring, 1u, 312u);
    push_frame(&ring, 2u, 312u);

    /* A whole frame. */
    copied = runtime_vic_ring_copy_range(&ring, true, 2u, 0u, 65535u, 400u, out);
    CHECK(copied == 312u);
    if (copied == 312u) {
        CHECK(out[0].frame_number == 2u);
        CHECK(out[0].raster_line == 0u);
        CHECK(out[311].raster_line == 311u);
        CHECK(out[100].sprite_x[0] == 200u);   /* payload follows the raster */
        CHECK(out[0].border_color == 2u);
    }

    /* A raster window inside one frame. */
    copied = runtime_vic_ring_copy_range(&ring, true, 1u, 100u, 109u, 400u, out);
    CHECK(copied == 10u);
    if (copied == 10u) {
        CHECK(out[0].raster_line == 100u);
        CHECK(out[9].raster_line == 109u);
        CHECK(out[0].frame_number == 1u);
    }

    /* A frame that was never recorded yields nothing. */
    copied = runtime_vic_ring_copy_range(&ring, true, 99u, 0u, 65535u, 400u, out);
    CHECK(copied == 0u);

    runtime_vic_ring_destroy(&ring);
    free(out);
}

static void test_copy_range_any_frame_and_limit(void) {
    runtime_vic_ring ring;
    vicii_line_record *out = malloc(700u * sizeof(*out));
    uint32_t copied;

    CHECK(out != NULL);
    CHECK(runtime_vic_ring_init(&ring, 1000u * sizeof(vicii_line_record)));
    push_frame(&ring, 1u, 312u);
    push_frame(&ring, 2u, 312u);

    /* Without a frame filter the raster window matches across every frame. */
    copied = runtime_vic_ring_copy_range(&ring, false, 0u, 50u, 50u, 700u, out);
    CHECK(copied == 2u);
    if (copied == 2u) {
        CHECK(out[0].frame_number == 1u);
        CHECK(out[1].frame_number == 2u);
        CHECK(out[0].raster_line == 50u);
    }

    /* Limit truncates, oldest-first. */
    copied = runtime_vic_ring_copy_range(&ring, true, 1u, 0u, 65535u, 5u, out);
    CHECK(copied == 5u);
    if (copied == 5u) {
        CHECK(out[0].raster_line == 0u);
        CHECK(out[4].raster_line == 4u);
    }

    /* A zero limit copies nothing and must not write through the pointer. */
    copied = runtime_vic_ring_copy_range(&ring, true, 1u, 0u, 65535u, 0u, out);
    CHECK(copied == 0u);

    runtime_vic_ring_destroy(&ring);
    free(out);
}

static void test_recording_toggle_and_clear(void) {
    runtime_vic_ring ring;
    runtime_vic_ring_info info;

    CHECK(runtime_vic_ring_init(&ring, 1000u * sizeof(vicii_line_record)));
    push_frame(&ring, 1u, 10u);

    runtime_vic_ring_set_recording(&ring, false);
    push_frame(&ring, 2u, 10u);
    runtime_vic_ring_get_info(&ring, &info);
    CHECK(!info.recording);
    CHECK(info.count == 10u);

    runtime_vic_ring_set_recording(&ring, true);
    push_frame(&ring, 3u, 10u);
    runtime_vic_ring_get_info(&ring, &info);
    CHECK(info.count == 20u);
    CHECK(info.newest_frame == 3u);

    runtime_vic_ring_clear(&ring);
    runtime_vic_ring_get_info(&ring, &info);
    CHECK(info.count == 0u);
    CHECK(info.dropped == 0u);
    CHECK(info.capacity == 1000u);

    runtime_vic_ring_destroy(&ring);
}

static void test_null_safety(void) {
    runtime_vic_ring ring;
    runtime_vic_ring_info info;
    vicii_line_record out;

    memset(&ring, 0, sizeof(ring));
    runtime_vic_ring_get_info(&ring, &info);
    CHECK(info.capacity == 0u);
    CHECK(info.count == 0u);
    CHECK(!runtime_vic_ring_push(&ring, NULL));
    CHECK(runtime_vic_ring_copy_range(&ring, false, 0u, 0u, 65535u, 1u, &out) == 0u);
    runtime_vic_ring_destroy(&ring);
}

int main(void) {
    test_budget_sets_capacity();
    test_push_and_info();
    test_wrap_drops_oldest();
    test_copy_range_by_frame();
    test_copy_range_any_frame_and_limit();
    test_recording_toggle_and_clear();
    test_null_safety();

    if (failures != 0) {
        fprintf(stderr, "test_runtime_vic_ring: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_runtime_vic_ring: ok\n");
    return 0;
}
