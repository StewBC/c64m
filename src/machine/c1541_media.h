#ifndef C1541_MEDIA_H
#define C1541_MEDIA_H

#include <stddef.h>
#include <stdint.h>

struct c1541;
struct c64_drive_slot;

/* Maximum half-track index (track 42). Track N uses half_track = 2*N. */
enum {
    C1541_MEDIA_MIN_HALF_TRACK = 2,   /* track 1 head-stop */
    C1541_MEDIA_MAX_HALF_TRACK = 84,  /* track 42 */
    /* 84 G64 half-track slots: index 0 = track 1.0, 1 = 1.5, ... */
    C1541_MEDIA_HALF_SLOTS = 84,
    /* Legacy whole-track count (D64 uses tracks 1..35). */
    C1541_MEDIA_TRACK_COUNT = 36,
    /* Motor spin-up in drive cycles (~50 ms at 1 MHz). Short enough for tests. */
    C1541_MEDIA_SPINUP_CYCLES = 50000u,
    /* Nominal revolution at 300 rpm: 200 ms → 200000 cycles @ 1 MHz. */
    C1541_MEDIA_CYCLES_PER_REV = 200000u,
    /* VICE DRIVE_ATTACH_DELAY = 3*600000: blank GCR + WPS closed while the
       new disk is "inserted". Multi-disk loaders (Edge of Disgrace) expect this. */
    C1541_MEDIA_ATTACH_DELAY = 1800000u
};

typedef struct c1541_track {
    uint8_t *data;
    size_t length; /* GCR bytes */
    int density;   /* 0..3 zone used when synthesised */
    int dirty;     /* 1 if GCR stream was written since last D64 sync */
} c1541_track;

typedef struct c1541_media {
    int enabled; /* 1 when media_1541 config path is active for this drive */

    int motor_on;
    int motor_ready;
    uint32_t motor_spin_left;

    int half_track; /* even = whole track; 2 = track 1 */
    uint8_t stepper_phase; /* last STP0/STP1 (PB0/PB1) */
    int density; /* latched DS0/DS1 */

    uint32_t bit_acc; /* fractional bit clock, in 16 MHz reference ticks */
    uint32_t ref_cycle;
    uint32_t bit_event_ref;
    uint32_t ref_tick_accum;
    unsigned ref_advance; /* reference ticks already run ahead for Port-A bus latency */
    unsigned req_ref_cycles; /* bus latency requested by a Port-A read */
    uint32_t flux_acc;
    int filter_counter;
    int filter_state;
    int filter_last_state;
    int no_flux_cycles;
    uint32_t flux_rand; /* VICE-compatible deterministic pseudo-flux source */
    int ue7_counter;
    int uf4_counter;
    uint32_t head_bit_pos; /* bit index into current track */

    uint16_t shift10; /* last 10 bits under head (for SYNC) */
    int in_sync;
    int bits_in_byte;
    uint8_t shifting_byte;
    uint8_t port_a_byte;
    int byte_ready;
    int so_pulse; /* edge request for CPU SO */
    int so_delay; /* reference ticks until BYTE READY/SO */

    /* Write path (DDRA all outputs). */
    int writing;          /* 1 while Port A is write mode */
    int write_bits_left;  /* remaining bits of current write byte (0 = need latch) */
    uint8_t write_shift;  /* bits still to write, MSB first */
    int last_write_bit;   /* held while waiting for next Port A write */
    /* Remaining hold-1 stream advances for DOS SYNC pad (ROM: STA $FF + 5×BVC).
       Counts down only while writing; 0 disables hold-1 stream advance (data). */
    int write_sync_hold_left;

    /* Flux store indexed by (half_track - MIN): [0]=1.0, [1]=1.5, ... */
    c1541_track halves[C1541_MEDIA_HALF_SLOTS];
    int tracks_valid;
    int from_g64; /* 1 when built from a G64 image (no D64 sector mirror) */
    const uint8_t *built_from; /* image_bytes pointer used for last build */
    size_t built_size;
    /* Matches c64_drive_slot.image_content_seq at last build/poke so offline
       D64 writes (media off / KERNAL trap) force a rebuild when media returns. */
    uint32_t built_from_seq;
    /* Drive cycles remaining in post-mount attach blanking (VICE attach_clk). */
    uint32_t attach_left;
    /* Set when a live disk is invalidated so the next rebuild blank-attaches. */
    int attach_pending;
} c1541_media;

void c1541_media_init(c1541_media *m);
void c1541_media_reset(c1541_media *m);
void c1541_media_free_tracks(c1541_media *m);

/* Build standard GCR tracks from a mounted 35-track D64 image. */
int c1541_media_build_from_d64(
    c1541_media *m,
    const uint8_t *image_bytes,
    size_t image_size);

/* Attach raw GCR half-tracks from a parsed G64 image.
   Host write-back is separate (c1541_media_sync_dirty_to_g64 when writable). */
int c1541_media_build_from_g64(
    c1541_media *m,
    const uint8_t *image_bytes,
    size_t image_size);

/* One drive-cycle media step: sample disk VIA, rotate, update SYNC/Port A/SO. */
void c1541_media_step(struct c1541 *drive);

/* Align head after a SYNC that precedes a long gap (full data-block sample). */
void c1541_media_align_after_sync(struct c1541 *drive);

/* Align after long-gap SYNC, then skip GCR bytes (match dual-BVC pre-roll). */
void c1541_media_align_after_sync_skip(struct c1541 *drive, unsigned skip_bytes);

/* Called when the drive CPU reads disk-controller VIA Port A ($1C01). */
void c1541_media_on_port_a_read(struct c1541 *drive);

/* Called when the drive CPU writes disk-controller VIA Port A ($1C01). */
void c1541_media_on_port_a_write(struct c1541 *drive, uint8_t value);

/* True when media mode should satisfy physical READ via rotation (no intercept). */
int c1541_media_physical_read_active(const struct c1541 *drive);

/* True when media mode has valid tracks (Port A write path is live). */
int c1541_media_physical_write_active(const struct c1541 *drive);

/* Rewrite one standard sector's data block in the synthesised GCR track
   (used by the hybrid WRITE intercept to keep flux coherent with D64). */
int c1541_media_poke_sector(
    struct c1541 *drive,
    uint8_t track,
    uint8_t sector,
    const uint8_t data[256]);

/* Rebuild one track's GCR from the mounted D64 image (after format erase). */
int c1541_media_rebuild_track(struct c1541 *drive, uint8_t track);

/* Decode dirty GCR tracks back into the mounted D64 image_bytes; marks dirty. */
int c1541_media_sync_dirty_to_d64(struct c1541 *drive);

/* Export dirty G64 half-tracks into slot->image_bytes (host mirror only).
   Live halves[] are never rotated; export may phase-normalize the host payload.
   Coheres built_from_seq so ensure_tracks does not rebuild from the snapshot. */
int c1541_media_sync_dirty_to_g64(struct c1541 *drive);

/* Dispatch Stage A: D64 sector mirror or G64 flux export. */
int c1541_media_sync_dirty(struct c1541 *drive);

/* Invalidate synthesised tracks (e.g. after mount/unmount). */
void c1541_media_invalidate(c1541_media *m);

#endif /* C1541_MEDIA_H */
