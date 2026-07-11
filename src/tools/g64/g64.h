#ifndef G64_H
#define G64_H

#include <stddef.h>
#include <stdint.h>

/* G64 (raw GCR track dump) parser — tools layer only.
   Spec: Peter Schepers G64.TXT / VICE file formats. */

enum {
    G64_SIGNATURE_LEN = 8,
    G64_MAX_HALF_TRACKS = 84, /* tracks 1.0 .. 42.5 */
    G64_HEADER_SIZE = 0x2ACu, /* signature through speed zone table end */
    G64_MAX_TRACK_BYTES = 16384
};

typedef enum g64_result {
    G64_OK = 0,
    G64_INVALID_ARGUMENT,
    G64_UNSUPPORTED_IMAGE,
    G64_TRUNCATED,
    G64_BAD_OFFSET,
    G64_OUT_OF_MEMORY
} g64_result;

typedef struct g64_half_track {
    uint8_t *data;     /* owned copy of actual track bytes (not max-padded) */
    size_t length;     /* actual stored track size */
    int density;       /* 0..3 when speed entry is constant; -1 if variable/unknown */
} g64_half_track;

typedef struct g64_image {
    uint8_t version;
    int half_track_count; /* number of entries in file (usually 84) */
    size_t max_track_size;
    g64_half_track half_tracks[G64_MAX_HALF_TRACKS];
} g64_image;

const char *g64_result_string(g64_result result);

/* True if size is large enough to possibly be a G64 header. */
int g64_looks_like(const uint8_t *bytes, size_t size);

/* Parse a G64 image into an owned structure. On failure *out_result is set. */
g64_image *g64_image_create(const uint8_t *bytes, size_t size, g64_result *out_result);
void g64_image_destroy(g64_image *image);

/* Map whole track number 1..42 to half-track index for track N.0. */
int g64_half_index_for_track(int track);

/* Map media half_track (2=track1, 3=track1.5, ...) to G64 index. */
int g64_half_index_for_media_half_track(int half_track);

#endif /* G64_H */
