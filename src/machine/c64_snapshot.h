#pragma once

#include "c64.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C64_SNAPSHOT_MAGIC 0x63363453u
#define C64_SNAPSHOT_VERSION 2u

typedef enum c64_snapshot_content_mode {
    C64_SNAPSHOT_CONTENT_REFERENCED = 1,
    C64_SNAPSHOT_CONTENT_SELF_CONTAINED = 2
} c64_snapshot_content_mode;

enum {
    C64_SNAPSHOT_FLAG_EXTERNAL_ROMS_REQUIRED = 0x00000001u,
    C64_SNAPSHOT_FLAG_EXTERNAL_MEDIA_REFERENCES = 0x00000002u,
    C64_SNAPSHOT_FLAG_1541_STATE_DEFERRED = 0x00000004u
};

size_t c64_snapshot_size(const c64_t *m);
size_t c64_snapshot_save(const c64_t *m, uint8_t *out, size_t out_cap);
bool c64_snapshot_load(c64_t *m, const uint8_t *in, size_t in_len);
