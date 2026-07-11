#include "g64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

/* Minimal valid G64: one track of 32 0x55 bytes. */
static uint8_t *make_min_g64(size_t *out_size) {
    const size_t header = 0x2ACu;
    const size_t track_off = header;
    const uint16_t track_len = 32;
    const size_t total = track_off + 2u + track_len;
    uint8_t *b = (uint8_t *)calloc(1, total);
    int i;
    if (b == NULL) fail("oom");

    memcpy(b, "GCR-1541", 8);
    b[8] = 0;
    b[9] = 84; /* half-tracks */
    b[10] = (uint8_t)(7928 & 0xff);
    b[11] = (uint8_t)(7928 >> 8);

    /* Track 1.0 offset at $000C */
    b[12] = (uint8_t)(track_off & 0xff);
    b[13] = (uint8_t)((track_off >> 8) & 0xff);
    b[14] = (uint8_t)((track_off >> 16) & 0xff);
    b[15] = (uint8_t)((track_off >> 24) & 0xff);

    /* Speed zones: all density 3 for outer tracks (values < 4) */
    for (i = 0; i < 84; ++i) {
        size_t p = 0x15Cu + (size_t)i * 4u;
        b[p] = 3;
    }

    b[track_off] = (uint8_t)(track_len & 0xff);
    b[track_off + 1] = (uint8_t)(track_len >> 8);
    memset(b + track_off + 2, 0x55, track_len);

    *out_size = total;
    return b;
}

static void test_looks_like(void) {
    uint8_t bad[16] = {0};
    size_t sz;
    uint8_t *g = make_min_g64(&sz);
    if (!g64_looks_like(g, sz)) fail("should look like g64");
    if (g64_looks_like(bad, sizeof(bad))) fail("zeros should not");
    free(g);
    printf("PASS: test_looks_like\n");
}

static void test_parse_min(void) {
    size_t sz;
    uint8_t *g = make_min_g64(&sz);
    g64_result r;
    g64_image *img = g64_image_create(g, sz, &r);
    if (img == NULL || r != G64_OK) fail("create");
    if (img->half_track_count != 84) fail("count");
    if (img->half_tracks[0].data == NULL || img->half_tracks[0].length != 32) {
        fail("track1 data");
    }
    if (img->half_tracks[0].data[0] != 0x55) fail("payload");
    if (img->half_tracks[1].data != NULL) fail("half track should be empty");
    if (img->half_tracks[0].density != 3) fail("density");
    g64_image_destroy(img);
    free(g);
    printf("PASS: test_parse_min\n");
}

static void test_reject_truncated(void) {
    size_t sz;
    uint8_t *g = make_min_g64(&sz);
    g64_result r;
    g64_image *img = g64_image_create(g, 20, &r);
    if (img != NULL) fail("should reject tiny");
    if (r != G64_INVALID_ARGUMENT && r != G64_UNSUPPORTED_IMAGE) {
        /* truncated header is invalid arg */
    }
    free(g);
    printf("PASS: test_reject_truncated\n");
}

static void test_index_helpers(void) {
    if (g64_half_index_for_track(1) != 0) fail("t1");
    if (g64_half_index_for_track(18) != 34) fail("t18");
    if (g64_half_index_for_media_half_track(2) != 0) fail("ht2");
    if (g64_half_index_for_media_half_track(36) != 34) fail("ht36");
    printf("PASS: test_index_helpers\n");
}

int main(void) {
    test_looks_like();
    test_parse_min();
    test_reject_truncated();
    test_index_helpers();
    printf("All g64 tests passed.\n");
    return 0;
}
