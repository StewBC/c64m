#include "g64.h"

#include <stdlib.h>
#include <string.h>

static const char g64_signature[G64_SIGNATURE_LEN] = {
    'G', 'C', 'R', '-', '1', '5', '4', '1'
};

const char *g64_result_string(g64_result result) {
    switch (result) {
        case G64_OK: return "ok";
        case G64_INVALID_ARGUMENT: return "invalid argument";
        case G64_UNSUPPORTED_IMAGE: return "unsupported image";
        case G64_TRUNCATED: return "truncated";
        case G64_BAD_OFFSET: return "bad offset";
        case G64_OUT_OF_MEMORY: return "out of memory";
        default: return "unknown";
    }
}

int g64_looks_like(const uint8_t *bytes, size_t size) {
    if (bytes == NULL || size < 12u) {
        return 0;
    }
    return memcmp(bytes, g64_signature, G64_SIGNATURE_LEN) == 0;
}

int g64_half_index_for_track(int track) {
    if (track < 1 || track > 42) {
        return -1;
    }
    return (track - 1) * 2;
}

int g64_half_index_for_media_half_track(int half_track) {
    /* Media: 2 = track 1.0, 3 = 1.5, ... 84 = 42.0 */
    if (half_track < 2 || half_track > 84) {
        return -1;
    }
    return half_track - 2;
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void g64_image_destroy(g64_image *image) {
    int i;
    if (image == NULL) {
        return;
    }
    for (i = 0; i < G64_MAX_HALF_TRACKS; ++i) {
        free(image->half_tracks[i].data);
        image->half_tracks[i].data = NULL;
        image->half_tracks[i].length = 0;
    }
    free(image);
}

g64_image *g64_image_create(const uint8_t *bytes, size_t size, g64_result *out_result) {
    g64_image *image;
    int ntracks;
    size_t max_track;
    int i;
    g64_result local;

    if (out_result == NULL) {
        out_result = &local;
    }
    *out_result = G64_OK;

    if (bytes == NULL || size < G64_HEADER_SIZE) {
        *out_result = G64_INVALID_ARGUMENT;
        return NULL;
    }
    if (!g64_looks_like(bytes, size)) {
        *out_result = G64_UNSUPPORTED_IMAGE;
        return NULL;
    }

    ntracks = (int)bytes[9];
    if (ntracks <= 0 || ntracks > G64_MAX_HALF_TRACKS) {
        *out_result = G64_UNSUPPORTED_IMAGE;
        return NULL;
    }
    max_track = (size_t)read_u16_le(bytes + 10);
    if (max_track == 0 || max_track > (size_t)G64_MAX_TRACK_BYTES) {
        *out_result = G64_UNSUPPORTED_IMAGE;
        return NULL;
    }

    image = (g64_image *)calloc(1, sizeof(*image));
    if (image == NULL) {
        *out_result = G64_OUT_OF_MEMORY;
        return NULL;
    }
    image->version = bytes[8];
    image->half_track_count = ntracks;
    image->max_track_size = max_track;

    for (i = 0; i < ntracks; ++i) {
        size_t off_pos = 12u + (size_t)i * 4u;
        size_t spd_pos = 0x15Cu + (size_t)i * 4u;
        uint32_t toff;
        uint32_t speed;
        uint16_t actual;
        uint8_t *copy;

        if (off_pos + 4u > size || spd_pos + 4u > size) {
            g64_image_destroy(image);
            *out_result = G64_TRUNCATED;
            return NULL;
        }

        toff = read_u32_le(bytes + off_pos);
        speed = read_u32_le(bytes + spd_pos);

        if (toff == 0u) {
            image->half_tracks[i].density = (speed < 4u) ? (int)speed : -1;
            continue;
        }
        if ((size_t)toff + 2u > size) {
            g64_image_destroy(image);
            *out_result = G64_BAD_OFFSET;
            return NULL;
        }
        actual = read_u16_le(bytes + toff);
        if (actual == 0u || (size_t)actual > max_track) {
            g64_image_destroy(image);
            *out_result = G64_BAD_OFFSET;
            return NULL;
        }
        if ((size_t)toff + 2u + (size_t)actual > size) {
            g64_image_destroy(image);
            *out_result = G64_TRUNCATED;
            return NULL;
        }

        copy = (uint8_t *)malloc(actual);
        if (copy == NULL) {
            g64_image_destroy(image);
            *out_result = G64_OUT_OF_MEMORY;
            return NULL;
        }
        memcpy(copy, bytes + toff + 2u, actual);
        image->half_tracks[i].data = copy;
        image->half_tracks[i].length = actual;
        image->half_tracks[i].density = (speed < 4u) ? (int)speed : -1;
    }

    return image;
}
