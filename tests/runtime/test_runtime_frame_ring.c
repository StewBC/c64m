/* Rolling framebuffer ring: the black box for one-frame glitches.
 *
 * A human pausing "a second late" is ~50 PAL frames past the glitch, and the
 * bad frame's pixels are gone the instant the beam moves on. The ring keeps the
 * last N completed frames so the frame can still be found afterwards, keyed by
 * both frame number and machine cycle so it cross-references the CPU flight
 * recorder.
 */
#include "runtime_frame_ring.h"

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

/* Each synthetic frame is filled with a value derived from its number so a
   lookup returning the wrong frame is visible in the pixels, not just in the
   metadata. */
static void make_frame(c64_frame *frame, uint64_t number, uint64_t cycle) {
    size_t i;
    size_t pixels = (size_t)C64_FRAME_WIDTH * (size_t)C64_FRAME_HEIGHT;

    frame->width = C64_FRAME_PAL_WIDTH;
    frame->height = C64_FRAME_PAL_HEIGHT;
    frame->stride_bytes = C64_FRAME_WIDTH;
    frame->pixel_format = C64_FRAME_PIXEL_FORMAT_INDEXED8;
    frame->frame_number = number;
    frame->machine_cycle = cycle;
    for (i = 0; i < pixels; ++i) {
        frame->pixels[i] = (uint8_t)(number + (uint64_t)i);
    }
}

static bool frame_pixels_match(const c64_frame *frame, uint64_t number) {
    size_t i;
    size_t pixels = (size_t)C64_FRAME_WIDTH * (size_t)C64_FRAME_HEIGHT;

    for (i = 0; i < pixels; ++i) {
        if (frame->pixels[i] != (uint8_t)(number + (uint64_t)i)) {
            return false;
        }
    }
    return true;
}

static void push_n(runtime_frame_ring *ring, uint64_t first, uint64_t count) {
    c64_frame *frame = malloc(sizeof(*frame));
    uint64_t n;

    if (frame == NULL) {
        return;
    }
    for (n = 0; n < count; ++n) {
        make_frame(frame, first + n, (first + n) * 1000u);
        runtime_frame_ring_push(ring, frame);
    }
    free(frame);
}

static void test_budget_sets_capacity(void) {
    runtime_frame_ring ring;
    runtime_frame_ring_info info;

    /* A budget of exactly four frames. */
    CHECK(runtime_frame_ring_init(&ring, 4u * sizeof(c64_frame)));
    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.capacity == 4u);
    CHECK(info.count == 0u);
    CHECK(info.dropped == 0u);
    CHECK(info.recording);
    runtime_frame_ring_destroy(&ring);

    /* A budget too small for even one frame must fail rather than allocate a
       zero-slot ring that silently records nothing. */
    CHECK(!runtime_frame_ring_init(&ring, 16u));
    runtime_frame_ring_destroy(&ring);

    /* Zero budget disables the ring explicitly. */
    CHECK(!runtime_frame_ring_init(&ring, 0u));
    runtime_frame_ring_destroy(&ring);
}

static void test_push_and_info(void) {
    runtime_frame_ring ring;
    runtime_frame_ring_info info;

    CHECK(runtime_frame_ring_init(&ring, 8u * sizeof(c64_frame)));
    push_n(&ring, 100u, 3u);

    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.count == 3u);
    CHECK(info.capacity == 8u);
    CHECK(info.oldest_frame == 100u);
    CHECK(info.newest_frame == 102u);
    CHECK(info.oldest_cycle == 100000u);
    CHECK(info.newest_cycle == 102000u);
    CHECK(info.dropped == 0u);

    runtime_frame_ring_destroy(&ring);
}

static void test_wrap_drops_oldest(void) {
    runtime_frame_ring ring;
    runtime_frame_ring_info info;
    c64_frame *got = malloc(sizeof(*got));

    CHECK(got != NULL);
    CHECK(runtime_frame_ring_init(&ring, 4u * sizeof(c64_frame)));
    push_n(&ring, 1u, 10u);   /* 6 frames past capacity */

    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.count == 4u);
    CHECK(info.dropped == 6u);
    CHECK(info.oldest_frame == 7u);
    CHECK(info.newest_frame == 10u);

    /* The surviving window must still carry the right pixels. */
    CHECK(runtime_frame_ring_copy_by_frame(&ring, 7u, got));
    CHECK(got->frame_number == 7u);
    CHECK(frame_pixels_match(got, 7u));
    CHECK(runtime_frame_ring_copy_by_frame(&ring, 10u, got));
    CHECK(got->frame_number == 10u);
    CHECK(frame_pixels_match(got, 10u));

    /* A dropped frame is gone, not silently substituted. */
    CHECK(!runtime_frame_ring_copy_by_frame(&ring, 6u, got));

    runtime_frame_ring_destroy(&ring);
    free(got);
}

static void test_find_by_frame_nearest(void) {
    runtime_frame_ring ring;
    c64_frame *got = malloc(sizeof(*got));

    CHECK(got != NULL);
    CHECK(runtime_frame_ring_init(&ring, 8u * sizeof(c64_frame)));
    push_n(&ring, 10u, 5u);   /* frames 10..14 */

    /* Exact hits. */
    CHECK(runtime_frame_ring_copy_by_frame(&ring, 12u, got));
    CHECK(got->frame_number == 12u);
    CHECK(frame_pixels_match(got, 12u));

    /* Past the newest clamps to the newest: scrubbing forward from a stale
       target should land on the most recent frame, not fail. */
    CHECK(runtime_frame_ring_copy_by_frame(&ring, 999u, got));
    CHECK(got->frame_number == 14u);

    /* Before the oldest has no answer. */
    CHECK(!runtime_frame_ring_copy_by_frame(&ring, 9u, got));

    runtime_frame_ring_destroy(&ring);
    free(got);
}

static void test_find_by_cycle_nearest(void) {
    runtime_frame_ring ring;
    c64_frame *got = malloc(sizeof(*got));

    CHECK(got != NULL);
    CHECK(runtime_frame_ring_init(&ring, 8u * sizeof(c64_frame)));
    push_n(&ring, 10u, 5u);   /* cycles 10000..14000, step 1000 */

    CHECK(runtime_frame_ring_copy_by_cycle(&ring, 12000u, got));
    CHECK(got->frame_number == 12u);

    /* Between two frames resolves to the frame at or before the target - the
       frame that was on screen at that cycle. */
    CHECK(runtime_frame_ring_copy_by_cycle(&ring, 12500u, got));
    CHECK(got->frame_number == 12u);

    CHECK(runtime_frame_ring_copy_by_cycle(&ring, 999999u, got));
    CHECK(got->frame_number == 14u);

    CHECK(!runtime_frame_ring_copy_by_cycle(&ring, 9999u, got));

    runtime_frame_ring_destroy(&ring);
    free(got);
}

static void test_find_by_cycle_exact(void) {
    runtime_frame_ring ring;
    c64_frame *got = malloc(sizeof(*got));

    CHECK(got != NULL);
    CHECK(runtime_frame_ring_init(&ring, 8u * sizeof(c64_frame)));
    push_n(&ring, 10u, 5u);   /* cycles 10000..14000, step 1000 */

    /* Exact hit copies the matching still, pixels included. */
    CHECK(runtime_frame_ring_copy_by_cycle_exact(&ring, 12000u, got));
    CHECK(got->frame_number == 12u);
    CHECK(got->machine_cycle == 12000u);
    CHECK(frame_pixels_match(got, 12u));

    /* Between retained cycles is a miss — no nearest-≤ neighbour. */
    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 12500u, got));

    /* Past the newest is also a miss (unlike nearest, which clamps). */
    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 999999u, got));

    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 9999u, got));

    /* Null out_frame / empty ring stay false. */
    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 12000u, NULL));
    runtime_frame_ring_clear(&ring);
    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 12000u, got));

    runtime_frame_ring_destroy(&ring);
    free(got);
}

static void test_recording_toggle(void) {
    runtime_frame_ring ring;
    runtime_frame_ring_info info;

    CHECK(runtime_frame_ring_init(&ring, 8u * sizeof(c64_frame)));
    push_n(&ring, 1u, 2u);

    runtime_frame_ring_set_recording(&ring, false);
    push_n(&ring, 3u, 5u);
    runtime_frame_ring_get_info(&ring, &info);
    CHECK(!info.recording);
    CHECK(info.count == 2u);          /* nothing recorded while off */
    CHECK(info.newest_frame == 2u);

    runtime_frame_ring_set_recording(&ring, true);
    push_n(&ring, 8u, 1u);
    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.recording);
    CHECK(info.count == 3u);
    CHECK(info.newest_frame == 8u);

    runtime_frame_ring_destroy(&ring);
}

static void test_clear(void) {
    runtime_frame_ring ring;
    runtime_frame_ring_info info;

    CHECK(runtime_frame_ring_init(&ring, 4u * sizeof(c64_frame)));
    push_n(&ring, 1u, 10u);
    runtime_frame_ring_clear(&ring);

    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.count == 0u);
    CHECK(info.dropped == 0u);
    CHECK(info.capacity == 4u);
    {
        c64_frame *got = malloc(sizeof(*got));
        CHECK(got != NULL);
        CHECK(!runtime_frame_ring_copy_by_frame(&ring, 10u, got));
        free(got);
    }

    /* Still usable after a clear. */
    push_n(&ring, 50u, 1u);
    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.count == 1u);
    CHECK(info.newest_frame == 50u);

    runtime_frame_ring_destroy(&ring);
}

static void test_empty_and_null_safety(void) {
    runtime_frame_ring ring;
    runtime_frame_ring_info info;

    c64_frame *got = malloc(sizeof(*got));

    CHECK(got != NULL);
    CHECK(runtime_frame_ring_init(&ring, 4u * sizeof(c64_frame)));
    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.count == 0u);
    CHECK(!runtime_frame_ring_copy_by_frame(&ring, 0u, got));
    CHECK(!runtime_frame_ring_copy_by_cycle(&ring, 0u, got));
    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 0u, got));
    runtime_frame_ring_destroy(&ring);

    /* An uninitialized (failed-init) ring must answer safely. */
    memset(&ring, 0, sizeof(ring));
    runtime_frame_ring_get_info(&ring, &info);
    CHECK(info.capacity == 0u);
    CHECK(info.count == 0u);
    CHECK(!runtime_frame_ring_copy_by_frame(&ring, 1u, got));
    CHECK(!runtime_frame_ring_copy_by_cycle_exact(&ring, 1u, got));
    CHECK(!runtime_frame_ring_push(&ring, NULL));
    runtime_frame_ring_destroy(&ring);
    free(got);
}

int main(void) {
    test_budget_sets_capacity();
    test_push_and_info();
    test_wrap_drops_oldest();
    test_find_by_frame_nearest();
    test_find_by_cycle_nearest();
    test_find_by_cycle_exact();
    test_recording_toggle();
    test_clear();
    test_empty_and_null_safety();

    if (failures != 0) {
        fprintf(stderr, "test_runtime_frame_ring: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_runtime_frame_ring: ok\n");
    return 0;
}
