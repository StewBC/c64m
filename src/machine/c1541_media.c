#include "c1541_media.h"

#include "c1541.h"
#include "c1541_gcr.h"
#include "c64.h"
#include "g64.h"

#include <stdlib.h>
#include <string.h>

/* Whole track t (1-based) → half-slot index for track t.0. */
static int whole_track_slot(int track) {
    if (track < 1 || track > 42) {
        return -1;
    }
    return (track - 1) * 2;
}

static c1541_track *slot_track(c1541_media *m, int half_slot) {
    if (half_slot < 0 || half_slot >= C1541_MEDIA_HALF_SLOTS) {
        return NULL;
    }
    return &m->halves[half_slot];
}

void c1541_media_init(c1541_media *m) {
    memset(m, 0, sizeof(*m));
    m->half_track = C1541_MEDIA_MIN_HALF_TRACK;
    m->stepper_phase = 0;
    m->density = 3;
    m->shift10 = 0;
    m->port_a_byte = 0;
    m->last_write_bit = 0;
}

void c1541_media_free_tracks(c1541_media *m) {
    int t;
    for (t = 0; t < C1541_MEDIA_HALF_SLOTS; ++t) {
        free(m->halves[t].data);
        m->halves[t].data = NULL;
        m->halves[t].length = 0;
        m->halves[t].density = 0;
        m->halves[t].dirty = 0;
    }
    m->tracks_valid = 0;
    m->from_g64 = 0;
    m->built_from = NULL;
    m->built_size = 0;
    m->built_from_seq = 0;
}

void c1541_media_invalidate(c1541_media *m) {
    c1541_media_free_tracks(m);
}

void c1541_media_reset(c1541_media *m) {
    int enabled = m->enabled;
    c1541_media_free_tracks(m);
    c1541_media_init(m);
    m->enabled = enabled;
}

static size_t track_target_bytes(int density) {
    int cpb = c1541_gcr_cycles_per_byte(density);
    size_t n = (size_t)(C1541_MEDIA_CYCLES_PER_REV / (uint32_t)cpb);
    if (n < 1024u) {
        n = 1024u;
    }
    if (n > (size_t)C1541_GCR_MAX_TRACK_BYTES) {
        n = (size_t)C1541_GCR_MAX_TRACK_BYTES;
    }
    return n;
}

static void append_bytes(uint8_t *buf, size_t *len, size_t cap, const uint8_t *src, size_t n) {
    size_t i;
    for (i = 0; i < n && *len < cap; ++i) {
        buf[(*len)++] = src[i];
    }
}

static void append_fill(uint8_t *buf, size_t *len, size_t cap, uint8_t v, size_t n) {
    size_t i;
    for (i = 0; i < n && *len < cap; ++i) {
        buf[(*len)++] = v;
    }
}

static int build_one_track(
    c1541_track *tr,
    uint8_t track,
    const uint8_t *image,
    size_t image_size,
    uint8_t id_lo,
    uint8_t id_hi) {
    int spt = c1541_gcr_sectors_per_track(track);
    int dens = c1541_gcr_density_for_track(track);
    size_t cap = track_target_bytes(dens);
    uint8_t *buf;
    size_t len = 0;
    int sec;
    uint8_t sync[C1541_GCR_SYNC_BYTES];
    uint8_t hdr_raw[C1541_GCR_HEADER_RAW];
    uint8_t hdr_gcr[C1541_GCR_HEADER_ENC];
    uint8_t data_raw[C1541_GCR_DATA_RAW];
    uint8_t data_gcr[C1541_GCR_DATA_ENC];
    uint8_t sector[256];

    if (spt <= 0) {
        return 0;
    }

    buf = (uint8_t *)malloc(cap);
    if (buf == NULL) {
        return 0;
    }
    memset(sync, 0xFFu, sizeof(sync));

    for (sec = 0; sec < spt; ++sec) {
        int off = c1541_gcr_d64_sector_offset(track, (uint8_t)sec);
        size_t enc;

        if (off < 0 || (size_t)(off + 256) > image_size) {
            memset(sector, 0, 256);
        } else {
            memcpy(sector, image + off, 256);
        }

        append_bytes(buf, &len, cap, sync, C1541_GCR_SYNC_BYTES);
        c1541_gcr_make_header_raw(track, (uint8_t)sec, id_lo, id_hi, hdr_raw);
        enc = c1541_gcr_encode(hdr_raw, C1541_GCR_HEADER_RAW, hdr_gcr);
        if (enc != C1541_GCR_HEADER_ENC) {
            free(buf);
            return 0;
        }
        append_bytes(buf, &len, cap, hdr_gcr, enc);
        append_fill(buf, &len, cap, 0x55u, C1541_GCR_HEADER_GAP);

        append_bytes(buf, &len, cap, sync, C1541_GCR_SYNC_BYTES);
        c1541_gcr_make_data_raw(sector, data_raw);
        enc = c1541_gcr_encode(data_raw, C1541_GCR_DATA_RAW, data_gcr);
        if (enc != C1541_GCR_DATA_ENC) {
            free(buf);
            return 0;
        }
        append_bytes(buf, &len, cap, data_gcr, enc);
        append_fill(buf, &len, cap, 0x55u, 8u);
    }

    while (len < cap) {
        buf[len++] = 0x55u;
    }

    free(tr->data);
    tr->data = buf;
    tr->length = len;
    tr->density = dens;
    tr->dirty = 0;
    return 1;
}

int c1541_media_build_from_d64(
    c1541_media *m,
    const uint8_t *image_bytes,
    size_t image_size) {
    uint8_t id_lo = 0x41u;
    uint8_t id_hi = 0x41u;
    int t;
    int bam_off;

    if (m == NULL || image_bytes == NULL || image_size < 174848u) {
        return 0;
    }

    bam_off = c1541_gcr_d64_sector_offset(18, 0);
    if (bam_off >= 0 && (size_t)(bam_off + 164) <= image_size) {
        id_lo = image_bytes[bam_off + 162];
        id_hi = image_bytes[bam_off + 163];
    }

    c1541_media_free_tracks(m);

    for (t = 1; t <= 35; ++t) {
        int slot = whole_track_slot(t);
        if (slot < 0 ||
            !build_one_track(
                &m->halves[slot], (uint8_t)t, image_bytes, image_size, id_lo, id_hi)) {
            c1541_media_free_tracks(m);
            return 0;
        }
    }

    m->tracks_valid = 1;
    m->from_g64 = 0;
    m->built_from = image_bytes;
    m->built_size = image_size;
    m->built_from_seq = 0; /* ensure_tracks sets this from the live slot seq */
    m->head_bit_pos = 0;
    m->bit_acc = 0;
    m->shift10 = 0;
    m->in_sync = 0;
    m->bits_in_byte = 0;
    m->byte_ready = 0;
    m->writing = 0;
    m->write_bits_left = 0;
    return 1;
}

static c1541_track *current_track(c1541_media *m) {
    int slot = g64_half_index_for_media_half_track(m->half_track);
    c1541_track *tr = slot_track(m, slot);
    if (tr == NULL || tr->data == NULL || tr->length == 0) {
        return NULL;
    }
    return tr;
}

int c1541_media_build_from_g64(
    c1541_media *m,
    const uint8_t *image_bytes,
    size_t image_size) {
    g64_image *g64;
    g64_result result;
    int i;

    if (m == NULL || image_bytes == NULL) {
        return 0;
    }

    g64 = g64_image_create(image_bytes, image_size, &result);
    if (g64 == NULL) {
        return 0;
    }

    c1541_media_free_tracks(m);

    for (i = 0; i < g64->half_track_count && i < C1541_MEDIA_HALF_SLOTS; ++i) {
        g64_half_track *src = &g64->half_tracks[i];
        c1541_track *dst = &m->halves[i];
        if (src->data == NULL || src->length == 0) {
            continue;
        }
        dst->data = (uint8_t *)malloc(src->length);
        if (dst->data == NULL) {
            g64_image_destroy(g64);
            c1541_media_free_tracks(m);
            return 0;
        }
        memcpy(dst->data, src->data, src->length);
        dst->length = src->length;
        dst->density = (src->density >= 0 && src->density <= 3)
            ? src->density
            : c1541_gcr_density_for_track((uint8_t)(i / 2 + 1));
        dst->dirty = 0;
    }

    g64_image_destroy(g64);
    m->tracks_valid = 1;
    m->from_g64 = 1;
    m->built_from = image_bytes;
    m->built_size = image_size;
    m->built_from_seq = 0; /* ensure_tracks sets this from the live slot seq */
    m->head_bit_pos = 0;
    m->bit_acc = 0;
    m->shift10 = 0;
    m->in_sync = 0;
    m->bits_in_byte = 0;
    m->byte_ready = 0;
    m->writing = 0;
    m->write_bits_left = 0;
    return 1;
}

static int track_bit(const c1541_track *tr, uint32_t bit_pos) {
    size_t nbytes = tr->length;
    uint32_t nbits;
    uint32_t idx;
    size_t bi;
    int bit_in_byte;

    if (nbytes == 0) {
        return 0;
    }
    nbits = (uint32_t)nbytes * 8u;
    idx = bit_pos % nbits;
    bi = (size_t)(idx / 8u);
    bit_in_byte = (int)(7u - (idx % 8u));
    return (tr->data[bi] >> bit_in_byte) & 1;
}

static void set_track_bit(c1541_track *tr, uint32_t bit_pos, int bit) {
    size_t nbytes = tr->length;
    uint32_t nbits;
    uint32_t idx;
    size_t bi;
    int bit_in_byte;
    uint8_t mask;

    if (nbytes == 0) {
        return;
    }
    nbits = (uint32_t)nbytes * 8u;
    idx = bit_pos % nbits;
    bi = (size_t)(idx / 8u);
    bit_in_byte = (int)(7u - (idx % 8u));
    mask = (uint8_t)(1u << bit_in_byte);
    if (bit) {
        tr->data[bi] |= mask;
    } else {
        tr->data[bi] &= (uint8_t)~mask;
    }
    tr->dirty = 1;
}

static void media_shift_bit_read(c1541 *drive, int bit) {
    c1541_media *m = &drive->media;

    m->shift10 = (uint16_t)(((m->shift10 << 1) | (bit & 1)) & 0x3FFu);
    m->in_sync = (m->shift10 == 0x3FFu) ? 1 : 0;

    if (m->in_sync) {
        m->bits_in_byte = 0;
        m->shifting_byte = 0;
        return;
    }

    m->shifting_byte = (uint8_t)((m->shifting_byte << 1) | (bit & 1));
    m->bits_in_byte++;
    if (m->bits_in_byte >= 8) {
        m->bits_in_byte = 0;
        m->port_a_byte = m->shifting_byte;
        m->byte_ready = 1;
        m->so_pulse = 1;
    }
}

static void media_shift_bit_write(c1541 *drive, c1541_track *tr) {
    c1541_media *m = &drive->media;
    int bit;

    if (m->write_bits_left > 0) {
        bit = (m->write_shift >> 7) & 1;
        m->write_shift = (uint8_t)(m->write_shift << 1);
        m->write_bits_left--;
        m->last_write_bit = bit;
        m->bits_in_byte = 0;
        if (tr != NULL) {
            /* Only mutate flux while actively shifting a latched byte. */
            set_track_bit(tr, m->head_bit_pos, bit);
        }
        if (m->write_bits_left == 0) {
            /* Byte shifted out — BYTE READY until the next Port A write.
               The ROM may CLV/BVC several times per STA (e.g. sync pad). */
            m->byte_ready = 1;
            m->so_pulse = 1;
        }
    } else {
        /* No fresh latch: do not paint the track (write amp still gated but no
           new data). Re-assert BYTE READY every 8 bit-times for BVC pads. */
        bit = m->last_write_bit;
        m->bits_in_byte++;
        if (m->bits_in_byte >= 8) {
            m->bits_in_byte = 0;
            m->byte_ready = 1;
            m->so_pulse = 1;
        }
    }

    /* Keep SYNC detector roughly aware during write (mostly 1s for sync marks). */
    m->shift10 = (uint16_t)(((m->shift10 << 1) | (bit & 1)) & 0x3FFu);
    m->in_sync = (m->shift10 == 0x3FFu) ? 1 : 0;
}

static int track_bit_at(const c1541_track *tr, uint32_t bit_pos) {
    return track_bit(tr, bit_pos);
}

/* Collect nbits MSB-first into dst bytes starting at bit_pos. Returns bits consumed. */
static uint32_t collect_bits(
    const c1541_track *tr,
    uint32_t bit_pos,
    uint32_t nbits,
    uint8_t *dst) {
    uint32_t i;
    uint32_t nbits_track = (uint32_t)tr->length * 8u;

    memset(dst, 0, (nbits + 7u) / 8u);
    for (i = 0; i < nbits; ++i) {
        int bit = track_bit_at(tr, (bit_pos + i) % nbits_track);
        if (bit) {
            dst[i / 8u] |= (uint8_t)(0x80u >> (i % 8u));
        }
    }
    return nbits;
}

/* Bit-oriented scan: ROM writes are not byte-aligned to our buffer. */
static int decode_track_to_d64(
    const c1541_track *tr,
    uint8_t track_num,
    uint8_t *image,
    size_t image_size) {
    uint32_t nbits;
    uint32_t pos;
    int updated = 0;
    uint16_t win = 0;

    if (tr == NULL || tr->data == NULL || image == NULL || tr->length == 0) {
        return 0;
    }

    nbits = (uint32_t)tr->length * 8u;
    /* Walk once around the track looking for header/data pairs. */
    for (pos = 0; pos < nbits; ++pos) {
        int bit = track_bit_at(tr, pos);
        uint8_t hdr_gcr[C1541_GCR_HEADER_ENC];
        uint8_t hdr_raw[C1541_GCR_HEADER_RAW];
        uint8_t data_gcr[C1541_GCR_DATA_ENC];
        uint8_t data_raw[C1541_GCR_DATA_RAW];
        uint8_t sector[256];
        uint32_t p;
        uint32_t ones;
        int off;

        win = (uint16_t)(((win << 1) | (bit & 1)) & 0x3FFu);
        if (win != 0x3FFu) {
            continue;
        }

        /* End of a ≥10-bit sync run: skip remaining 1s. */
        p = pos + 1u;
        ones = 0;
        while (ones < 80u && track_bit_at(tr, p % nbits) == 1) {
            p++;
            ones++;
        }
        /* First 0 begins framed data (or pure 1s gap — still try). */
        if (track_bit_at(tr, p % nbits) == 0) {
            /* include the 0 as first data bit */
        }

        collect_bits(tr, p, (uint32_t)C1541_GCR_HEADER_ENC * 8u, hdr_gcr);
        if (c1541_gcr_decode(hdr_gcr, C1541_GCR_HEADER_ENC, hdr_raw)
            != C1541_GCR_HEADER_RAW) {
            continue;
        }
        if (hdr_raw[0] != 0x08u || hdr_raw[3] != track_num) {
            continue;
        }

        /* After header GCR, find next sync then data block. */
        p = (p + (uint32_t)C1541_GCR_HEADER_ENC * 8u) % nbits;
        {
            uint32_t searched = 0;
            uint16_t w2 = 0;
            int found = 0;
            while (searched < nbits) {
                int b = track_bit_at(tr, p);
                w2 = (uint16_t)(((w2 << 1) | (b & 1)) & 0x3FFu);
                p = (p + 1u) % nbits;
                searched++;
                if (w2 == 0x3FFu) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                continue;
            }
            /* Skip rest of sync 1s. */
            ones = 0;
            while (ones < 80u && track_bit_at(tr, p) == 1) {
                p = (p + 1u) % nbits;
                ones++;
            }
        }

        collect_bits(tr, p, (uint32_t)C1541_GCR_DATA_ENC * 8u, data_gcr);
        if (c1541_gcr_decode(data_gcr, C1541_GCR_DATA_ENC, data_raw)
            != C1541_GCR_DATA_RAW) {
            continue;
        }
        if (!c1541_gcr_data_raw_to_sector(data_raw, sector)) {
            continue;
        }

        off = c1541_gcr_d64_sector_offset(track_num, hdr_raw[2]);
        if (off >= 0 && (size_t)(off + 256) <= image_size) {
            if (memcmp(image + off, sector, 256) != 0) {
                memcpy(image + off, sector, 256);
                updated = 1;
            }
        }
    }

    return updated;
}

int c1541_media_sync_dirty_to_d64(c1541 *drive) {
    c1541_media *m;
    c64_drive_slot *slot;
    int t;
    int any = 0;

    if (drive == NULL) {
        return 0;
    }
    m = &drive->media;
    if (!m->enabled || !m->tracks_valid) {
        return 0;
    }

    slot = c64_get_drive_slot_mut(drive->c64, drive->device_number);
    if (slot == NULL || !slot->mounted || slot->image_bytes == NULL ||
        slot->image_kind != C64_DRIVE_IMAGE_D64 || !slot->writable) {
        return 0;
    }

    if (m->from_g64) {
        return 0; /* G64 mounts are read-only in v1; no D64 sector mirror. */
    }

    for (t = 1; t <= 35; ++t) {
        int hs = whole_track_slot(t);
        c1541_track *tr = slot_track(m, hs);
        if (tr == NULL || !tr->dirty || tr->data == NULL) {
            continue;
        }
        if (decode_track_to_d64(tr, (uint8_t)t, slot->image_bytes, slot->image_size)) {
            any = 1;
        }
        tr->dirty = 0;
    }

    if (any) {
        slot->dirty = true;
        slot->image_content_seq++;
        /* Keep built_from coherent so ensure_tracks does not rebuild over writes. */
        m->built_from = slot->image_bytes;
        m->built_size = slot->image_size;
        m->built_from_seq = slot->image_content_seq;
    }
    return any;
}

static int slot_is_writable(c1541 *drive) {
    const c64_drive_slot *slot = c64_get_drive_slot(drive->c64, drive->device_number);
    return (slot != NULL && slot->mounted && slot->writable) ? 1 : 0;
}

/* Place the head on the first data bit after a SYNC that precedes a long
   inter-sync gap (data block), not a short header gap.

   Mid-sector seek phase makes the first measured gap a partial; aligning to
   any post-sync fixes that. But stage-3 self-checks need 16 *equal* gaps, and
   after SO/byte-phase lock the sampler typically only catches every other SYNC
   (data marks). Starting on a short header gap makes entry 0 differ from the
   rest; starting on a long data gap makes all 16 match. */
static void media_align_after_sync(c1541_media *m) {
    c1541_track *tr = current_track(m);
    uint32_t nbits;
    uint32_t i;
    uint16_t sh = 0;
    uint32_t posts[64];
    uint16_t post_sh[64];
    int npost = 0;
    int k;
    uint32_t best_pos;
    uint16_t best_sh;
    const uint32_t long_gap_min = 512u; /* header gaps ~170 bits; data ~2700 */

    m->bits_in_byte = 0;
    m->byte_ready = 0;
    m->shifting_byte = 0;
    m->head_bit_pos = 0;
    m->shift10 = 0;
    m->in_sync = 0;
    if (tr == NULL || tr->data == NULL || tr->length <= 0) {
        return;
    }
    nbits = (uint32_t)tr->length * 8u;

    for (i = 0; i < nbits && npost < 64; ) {
        int bit = track_bit(tr, i);
        sh = (uint16_t)(((sh << 1) | (bit & 1)) & 0x3FFu);
        i++;
        if (sh != 0x3FFu) {
            continue;
        }
        /* Consume rest of this sync run; first non-1 is post-sync data. */
        while (i < nbits) {
            int b2 = track_bit(tr, i);
            sh = (uint16_t)(((sh << 1) | (b2 & 1)) & 0x3FFu);
            i++;
            if (sh != 0x3FFu) {
                posts[npost] = i - 1u; /* first data bit (already in sh) */
                post_sh[npost] = sh;
                npost++;
                break;
            }
        }
    }
    if (npost == 0) {
        return;
    }

    best_pos = posts[0];
    best_sh = post_sh[0];
    for (k = 0; k < npost; k++) {
        uint32_t a = posts[k];
        uint32_t b = posts[(k + 1) % npost];
        uint32_t gap = (b >= a) ? (b - a) : (nbits - a + b);
        if (gap >= long_gap_min) {
            best_pos = a;
            best_sh = post_sh[k];
            break;
        }
    }

    m->head_bit_pos = best_pos;
    m->shift10 = best_sh;
    m->in_sync = 0;
}

void c1541_media_align_after_sync(struct c1541 *drive) {
    if (drive == NULL || !drive->media.enabled || !drive->media.tracks_valid) {
        return;
    }
    media_align_after_sync(&drive->media);
}

/* Like align_after_sync, then skip skip_bytes GCR bytes (for dual-BVC pre-roll). */
void c1541_media_align_after_sync_skip(struct c1541 *drive, unsigned skip_bytes) {
    c1541_media *m;
    c1541_track *tr;
    uint32_t nbits;

    if (drive == NULL || !drive->media.enabled || !drive->media.tracks_valid) {
        return;
    }
    media_align_after_sync(&drive->media);
    if (skip_bytes == 0) {
        return;
    }
    m = &drive->media;
    tr = current_track(m);
    if (tr == NULL || tr->data == NULL || tr->length <= 0) {
        return;
    }
    nbits = (uint32_t)tr->length * 8u;
    m->head_bit_pos = (m->head_bit_pos + (uint32_t)skip_bytes * 8u) % nbits;
    m->bits_in_byte = 0;
    m->byte_ready = 0;
    m->shifting_byte = 0;
    /* shift10/in_sync will resync from flux on subsequent bit clocks */
    m->shift10 = 0;
    m->in_sync = 0;
}

static void sample_disk_via_outputs(c1541 *drive) {
    c1541_media *m = &drive->media;
    uint8_t orb = drive->via2.orb;
    uint8_t ddrb = drive->via2.ddrb;
    uint8_t ddra = drive->via2.ddra;
    uint8_t phase;
    uint8_t out_b;
    int motor;
    int want_write;

    out_b = (uint8_t)(orb & ddrb);

    motor = ((ddrb & 0x04u) != 0u && (orb & 0x04u) != 0u) ? 1 : 0;
    if (motor && !m->motor_on) {
        m->motor_spin_left = C1541_MEDIA_SPINUP_CYCLES;
        m->motor_ready = 0;
    }
    if (!motor) {
        m->motor_ready = 0;
        m->motor_spin_left = 0;
    }
    m->motor_on = motor;

    if ((ddrb & 0x60u) == 0x60u) {
        m->density = (int)((out_b >> 5) & 0x03u);
    }

    if ((ddrb & 0x03u) == 0x03u) {
        phase = (uint8_t)(out_b & 0x03u);
        if (phase != m->stepper_phase) {
            int diff = (int)((phase - m->stepper_phase) & 3);
            if (diff == 1) {
                if (m->half_track < C1541_MEDIA_MAX_HALF_TRACK) {
                    m->half_track++;
                }
            } else if (diff == 3) {
                if (m->half_track > C1541_MEDIA_MIN_HALF_TRACK) {
                    m->half_track--;
                }
            }
            m->stepper_phase = phase;
            m->write_bits_left = 0;
            media_align_after_sync(m);
        }
    }

    /* Write gate: DDRA all outputs AND PCR CB2 manual-low (ROM uses
       (PCR & $1F) | $C0 to enable writing, | $E0 to release). */
    want_write = (ddra == 0xFFu
                  && (drive->via2.pcr & 0xE0u) == 0xC0u
                  && m->motor_ready) ? 1 : 0;
    if (want_write && !m->writing) {
        m->writing = 1;
        m->write_bits_left = 0;
        m->bits_in_byte = 0;
        m->byte_ready = 1;
        m->so_pulse = 1;
        m->in_sync = 0;
    } else if (!want_write && m->writing) {
        m->writing = 0;
        m->write_bits_left = 0;
        m->bits_in_byte = 0;
        /* Leaving write mode: push dirty GCR back into the D64 sector image. */
        (void)c1541_media_sync_dirty_to_d64(drive);
    }
}

static void update_disk_via_inputs(c1541 *drive) {
    c1541_media *m = &drive->media;
    uint8_t pb_in = 0;

    if (slot_is_writable(drive)) {
        pb_in |= 0x10u;
    }
    if (!m->in_sync) {
        pb_in |= 0x80u;
    }

    via6522_set_port_b_inputs(&drive->via2, pb_in);
    if (!m->writing) {
        via6522_set_port_a_inputs(&drive->via2, m->port_a_byte);
    }
}

static void ensure_tracks(c1541 *drive) {
    c1541_media *m = &drive->media;
    const c64_drive_slot *slot;
    int ok = 0;

    if (!m->enabled) {
        return;
    }

    slot = c64_get_drive_slot(drive->c64, drive->device_number);
    if (slot == NULL || !slot->mounted || slot->image_bytes == NULL) {
        return;
    }

    /* Rebuild when the host image pointer/size changes OR when the D64 was
       modified while media was off / via job intercept without a GCR poke. */
    if (m->tracks_valid && m->built_from == slot->image_bytes &&
        m->built_size == slot->image_size &&
        m->built_from_seq == slot->image_content_seq) {
        return;
    }

    if (slot->image_kind == C64_DRIVE_IMAGE_G64) {
        ok = c1541_media_build_from_g64(m, slot->image_bytes, slot->image_size);
    } else if (slot->image_kind == C64_DRIVE_IMAGE_D64) {
        ok = c1541_media_build_from_d64(m, slot->image_bytes, slot->image_size);
    }
    if (ok) {
        m->built_from_seq = slot->image_content_seq;
    }
}

void c1541_media_step(c1541 *drive) {
    c1541_media *m;
    c1541_track *tr;
    int cpb;

    if (drive == NULL) {
        return;
    }
    m = &drive->media;
    if (!m->enabled) {
        return;
    }

    ensure_tracks(drive);
    sample_disk_via_outputs(drive);

    if (m->motor_on && !m->motor_ready) {
        if (m->motor_spin_left > 0u) {
            m->motor_spin_left--;
        }
        if (m->motor_spin_left == 0u) {
            m->motor_ready = 1;
        }
    }

    /*
     * Flux clock only while the spindle is up. Stepper/WPS/write-gate still
     * sample every cycle above. When motor is off, skip the bit loop entirely
     * (common dual-drive idle case).
     */
    if (m->motor_ready && m->tracks_valid) {
        tr = current_track(m);
        /*
         * Bit rate: prefer VIA DS0/DS1 when both are outputs (software-selected
         * zone — required by loaders that retune density). Otherwise use the
         * track's native G64/D64 zone density.
         */
        if ((drive->via2.ddrb & 0x60u) == 0x60u) {
            cpb = c1541_gcr_cycles_per_byte(m->density);
        } else if (tr != NULL && tr->density >= 0 && tr->density <= 3) {
            cpb = c1541_gcr_cycles_per_byte(tr->density);
        } else {
            cpb = c1541_gcr_cycles_per_byte(m->density);
        }
        if (cpb < 1) {
            cpb = 26;
        }

        m->bit_acc += 8u;
        while (m->bit_acc >= (uint32_t)cpb) {
            m->bit_acc -= (uint32_t)cpb;

            if (m->writing) {
                if (slot_is_writable(drive)) {
                    media_shift_bit_write(drive, tr);
                } else {
                    /* Write-protected: still advance time / BYTE READY, no mutate. */
                    if (m->write_bits_left > 0) {
                        m->write_shift = (uint8_t)(m->write_shift << 1);
                        m->write_bits_left--;
                        if (m->write_bits_left == 0) {
                            m->byte_ready = 1;
                            m->so_pulse = 1;
                        }
                    }
                }
            } else {
                int bit = 0;
                if (tr != NULL && tr->length > 0) {
                    bit = track_bit(tr, m->head_bit_pos);
                }
                media_shift_bit_read(drive, bit);
            }

            if (tr != NULL && tr->length > 0) {
                m->head_bit_pos++;
                if (m->head_bit_pos >= (uint32_t)tr->length * 8u) {
                    m->head_bit_pos = 0;
                }
            }
        }
    }

    update_disk_via_inputs(drive);

    if (m->so_pulse) {
        /* One GCR byte completed under the head — discrete transfer event for UI LEDs. */
        if (drive->c64 != NULL) {
            if (m->writing) {
                c64_disk_activity_write(drive->c64, drive->device_number);
            } else {
                c64_disk_activity_read(drive->c64, drive->device_number);
            }
        }
        m->so_pulse = 0;
        c6510_set_overflow(&drive->cpu);
    }
}

void c1541_media_on_port_a_read(c1541 *drive) {
    if (drive == NULL || !drive->media.enabled) {
        return;
    }
    if (!drive->media.writing) {
        drive->media.byte_ready = 0;
    }
}

void c1541_media_on_port_a_write(c1541 *drive, uint8_t value) {
    c1541_media *m;

    if (drive == NULL || !drive->media.enabled) {
        return;
    }
    m = &drive->media;
    if (!m->writing) {
        return;
    }
    m->write_shift = value;
    m->write_bits_left = 8;
    m->byte_ready = 0;
    m->port_a_byte = value;
}

int c1541_media_physical_read_active(const c1541 *drive) {
    if (drive == NULL) {
        return 0;
    }
    return drive->media.enabled && drive->media.tracks_valid;
}

int c1541_media_physical_write_active(const c1541 *drive) {
    if (drive == NULL) {
        return 0;
    }
    return drive->media.enabled && drive->media.tracks_valid;
}

/* Layout produced by build_one_track — keep in lockstep with that function. */
static size_t sector_data_gcr_offset(uint8_t sector) {
    /* per sector: sync5 + hdr10 + gap9 + sync5 + data325 + gap8 */
    const size_t per = (size_t)C1541_GCR_SYNC_BYTES + C1541_GCR_HEADER_ENC
        + C1541_GCR_HEADER_GAP + C1541_GCR_SYNC_BYTES + C1541_GCR_DATA_ENC + 8u;
    return (size_t)sector * per
        + (size_t)C1541_GCR_SYNC_BYTES + C1541_GCR_HEADER_ENC
        + C1541_GCR_HEADER_GAP + C1541_GCR_SYNC_BYTES;
}

int c1541_media_poke_sector(
    c1541 *drive,
    uint8_t track,
    uint8_t sector,
    const uint8_t data[256]) {
    c1541_media *m;
    c1541_track *tr;
    uint8_t raw[C1541_GCR_DATA_RAW];
    uint8_t gcr[C1541_GCR_DATA_ENC];
    size_t off;
    size_t enc;
    int spt;

    if (drive == NULL || data == NULL) {
        return 0;
    }
    m = &drive->media;
    if (!m->enabled || !m->tracks_valid || m->from_g64 || track < 1 || track > 35) {
        return 0;
    }
    spt = c1541_gcr_sectors_per_track(track);
    if (spt <= 0 || sector >= (uint8_t)spt) {
        return 0;
    }
    tr = slot_track(m, whole_track_slot(track));
    if (tr == NULL || tr->data == NULL) {
        return 0;
    }

    c1541_gcr_make_data_raw(data, raw);
    enc = c1541_gcr_encode(raw, C1541_GCR_DATA_RAW, gcr);
    if (enc != C1541_GCR_DATA_ENC) {
        return 0;
    }

    off = sector_data_gcr_offset(sector);
    if (off + C1541_GCR_DATA_ENC > tr->length) {
        return 0;
    }
    memcpy(tr->data + off, gcr, C1541_GCR_DATA_ENC);
    tr->dirty = 0; /* already mirrored into D64 by the caller */
    {
        const c64_drive_slot *slot = c64_get_drive_slot(drive->c64, drive->device_number);
        if (slot != NULL) {
            m->built_from = slot->image_bytes;
            m->built_size = slot->image_size;
            m->built_from_seq = slot->image_content_seq;
        }
    }
    return 1;
}

int c1541_media_rebuild_track(c1541 *drive, uint8_t track) {
    c1541_media *m;
    const c64_drive_slot *slot;
    uint8_t id_lo = 0x41u;
    uint8_t id_hi = 0x41u;
    int bam_off;

    if (drive == NULL || track < 1 || track > 35) {
        return 0;
    }
    m = &drive->media;
    if (!m->enabled) {
        return 0;
    }
    slot = c64_get_drive_slot(drive->c64, drive->device_number);
    if (slot == NULL || !slot->mounted || slot->image_bytes == NULL) {
        return 0;
    }
    bam_off = c1541_gcr_d64_sector_offset(18, 0);
    if (bam_off >= 0 && (size_t)(bam_off + 164) <= slot->image_size) {
        id_lo = slot->image_bytes[bam_off + 162];
        id_hi = slot->image_bytes[bam_off + 163];
    }
    {
        int hs = whole_track_slot(track);
        if (hs < 0) {
            return 0;
        }
        return build_one_track(
            &m->halves[hs],
            track,
            slot->image_bytes,
            slot->image_size,
            id_lo,
            id_hi);
    }
}
