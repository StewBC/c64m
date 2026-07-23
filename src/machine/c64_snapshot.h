#pragma once

#include "c64.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C64_SNAPSHOT_MAGIC 0x63363453u
/* v10 widened the VIC-II framebuffer from a 384-px crop to the full raster line
   (C64_FRAME_WIDTH), which changes both the stored frame geometry and the pixel
   payload size. Nothing in a v9-or-earlier file can be reinterpreted into the
   new buffer without inventing the columns that were never captured, so those
   versions are sunset rather than migrated: VERSION_MIN moves up with VERSION
   and the existing header check rejects them cleanly. */
#define C64_SNAPSHOT_VERSION 10u
#define C64_SNAPSHOT_VERSION_MIN 10u

typedef enum c64_snapshot_content_mode {
    C64_SNAPSHOT_CONTENT_REFERENCED = 1,
    C64_SNAPSHOT_CONTENT_SELF_CONTAINED = 2
} c64_snapshot_content_mode;

enum {
    C64_SNAPSHOT_FLAG_EXTERNAL_ROMS_REQUIRED = 0x00000001u,
    C64_SNAPSHOT_FLAG_EXTERNAL_MEDIA_REFERENCES = 0x00000002u,
    /* Legacy v8: drive *object* not in the snapshot; load hard-resets drives. */
    C64_SNAPSHOT_FLAG_1541_STATE_DEFERRED = 0x00000004u,
    /* v9+: full 1541 CPU/VIA/RAM/media state present as DR8C/DR9C chunks. */
    C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED = 0x00000008u
};

size_t c64_snapshot_size(const c64_t *m);
size_t c64_snapshot_save(const c64_t *m, uint8_t *out, size_t out_cap);
bool c64_snapshot_load(c64_t *m, const uint8_t *in, size_t in_len);
