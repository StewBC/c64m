#include "c1541_media.h"

#include "c1541.h"
#include "c1541_gcr.h"
#include "c64.h"

#include <stdlib.h>
#include <string.h>

void c1541_media_init(c1541_media *m) {
    memset(m, 0, sizeof(*m));
    m->half_track = C1541_MEDIA_MIN_HALF_TRACK;
    m->stepper_phase = 0;
    m->density = 3;
    m->shift10 = 0;
    m->port_a_byte = 0;
}

void c1541_media_free_tracks(c1541_media *m) {
    int t;
    for (t = 0; t < C1541_MEDIA_TRACK_COUNT; ++t) {
        free(m->tracks[t].data);
        m->tracks[t].data = NULL;
        m->tracks[t].length = 0;
        m->tracks[t].density = 0;
    }
    m->tracks_valid = 0;
    m->built_from = NULL;
    m->built_size = 0;
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
    /* bytes per revolution ≈ cycles_per_rev / cycles_per_byte */
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

        /* Inter-sector gap: leave room; final pad fills the revolution. */
        append_fill(buf, &len, cap, 0x55u, 8u);
    }

    while (len < cap) {
        buf[len++] = 0x55u;
    }

    free(tr->data);
    tr->data = buf;
    tr->length = len;
    tr->density = dens;
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
        if (!build_one_track(&m->tracks[t], (uint8_t)t, image_bytes, image_size, id_lo, id_hi)) {
            c1541_media_free_tracks(m);
            return 0;
        }
    }

    m->tracks_valid = 1;
    m->built_from = image_bytes;
    m->built_size = image_size;
    m->head_bit_pos = 0;
    m->bit_acc = 0;
    m->shift10 = 0;
    m->in_sync = 0;
    m->bits_in_byte = 0;
    m->byte_ready = 0;
    return 1;
}

static c1541_track *current_track(c1541_media *m) {
    int track;

    if ((m->half_track & 1) != 0) {
        return NULL; /* half-track: no standard data */
    }
    track = m->half_track / 2;
    if (track < 1 || track >= C1541_MEDIA_TRACK_COUNT) {
        return NULL;
    }
    if (m->tracks[track].data == NULL || m->tracks[track].length == 0) {
        return NULL;
    }
    return &m->tracks[track];
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

static void media_shift_bit(c1541 *drive, int bit) {
    c1541_media *m = &drive->media;

    m->shift10 = (uint16_t)(((m->shift10 << 1) | (bit & 1)) & 0x3FFu);
    m->in_sync = (m->shift10 == 0x3FFu) ? 1 : 0;

    if (m->in_sync) {
        /* Stay in sync run: no framed data bytes. */
        m->bits_in_byte = 0;
        m->shifting_byte = 0;
        return;
    }

    /* Assemble GCR data bytes once out of sync. */
    m->shifting_byte = (uint8_t)((m->shifting_byte << 1) | (bit & 1));
    m->bits_in_byte++;
    if (m->bits_in_byte >= 8) {
        m->bits_in_byte = 0;
        m->port_a_byte = m->shifting_byte;
        m->byte_ready = 1;
        m->so_pulse = 1;
    }
}

static void sample_disk_via_outputs(c1541 *drive) {
    c1541_media *m = &drive->media;
    uint8_t orb = drive->via2.orb;
    uint8_t ddrb = drive->via2.ddrb;
    uint8_t phase;
    uint8_t out_b;
    int motor;

    out_b = (uint8_t)(orb & ddrb);

    /* Motor: PB2 high = on when configured as output. */
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

    /* Density DS0/DS1 on PB5/PB6. */
    if ((ddrb & 0x60u) == 0x60u) {
        m->density = (int)((out_b >> 5) & 0x03u);
    }

    /* Stepper STP0/STP1 on PB0/PB1. */
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
            /* Changing track restarts bit framing. */
            m->bits_in_byte = 0;
            m->byte_ready = 0;
            m->head_bit_pos = 0;
            m->shift10 = 0;
            m->in_sync = 0;
        }
    }
}

static void update_disk_via_inputs(c1541 *drive) {
    c1541_media *m = &drive->media;
    const c64_drive_slot *slot;
    uint8_t pb_in = 0;
    int writable = 0;

    slot = c64_get_drive_slot(drive->c64, drive->device_number);
    if (slot != NULL && slot->mounted && slot->writable) {
        writable = 1;
    }

    /* PB4 write-protect sense: 1 = not protected (can write). */
    if (writable) {
        pb_in |= 0x10u;
    }

    /* PB7 SYNC: 0 = sync mark under head, 1 = no sync. */
    if (!m->in_sync) {
        pb_in |= 0x80u;
    }

    via6522_set_port_b_inputs(&drive->via2, pb_in);
    via6522_set_port_a_inputs(&drive->via2, m->port_a_byte);
}

static void ensure_tracks(c1541 *drive) {
    c1541_media *m = &drive->media;
    const c64_drive_slot *slot;

    if (!m->enabled) {
        return;
    }

    slot = c64_get_drive_slot(drive->c64, drive->device_number);
    if (slot == NULL || !slot->mounted || slot->image_bytes == NULL ||
        slot->image_kind != C64_DRIVE_IMAGE_D64) {
        /* Do not free here — unmount explicitly invalidates.  Leaving tracks
           alone also allows unit tests to inject synthesised media. */
        return;
    }

    if (m->tracks_valid && m->built_from == slot->image_bytes &&
        m->built_size == slot->image_size) {
        return;
    }

    (void)c1541_media_build_from_d64(m, slot->image_bytes, slot->image_size);
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

    if (m->motor_ready && m->tracks_valid) {
        tr = current_track(m);
        cpb = c1541_gcr_cycles_per_byte(m->density);
        /* Prefer track's native density if DS not yet programmed. */
        if (tr != NULL && (drive->via2.ddrb & 0x60u) != 0x60u) {
            cpb = c1541_gcr_cycles_per_byte(tr->density);
        }

        m->bit_acc += 8u;
        while (m->bit_acc >= (uint32_t)cpb) {
            int bit = 0;
            m->bit_acc -= (uint32_t)cpb;
            if (tr != NULL && tr->length > 0) {
                bit = track_bit(tr, m->head_bit_pos);
                m->head_bit_pos++;
                if (m->head_bit_pos >= (uint32_t)tr->length * 8u) {
                    m->head_bit_pos = 0;
                }
            }
            media_shift_bit(drive, bit);
        }
    }

    update_disk_via_inputs(drive);

    if (m->so_pulse) {
        m->so_pulse = 0;
        c6510_set_overflow(&drive->cpu);
    }
}

void c1541_media_on_port_a_read(c1541 *drive) {
    if (drive == NULL || !drive->media.enabled) {
        return;
    }
    drive->media.byte_ready = 0;
}

int c1541_media_physical_read_active(const c1541 *drive) {
    if (drive == NULL) {
        return 0;
    }
    return drive->media.enabled && drive->media.tracks_valid;
}
