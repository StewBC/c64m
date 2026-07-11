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
    C1541_MEDIA_TRACK_COUNT = 36,     /* 1..35 usable + spare */
    /* Motor spin-up in drive cycles (~50 ms at 1 MHz). Short enough for tests. */
    C1541_MEDIA_SPINUP_CYCLES = 50000u,
    /* Nominal revolution at 300 rpm: 200 ms → 200000 cycles @ 1 MHz. */
    C1541_MEDIA_CYCLES_PER_REV = 200000u
};

typedef struct c1541_track {
    uint8_t *data;
    size_t length; /* GCR bytes */
    int density;   /* 0..3 zone used when synthesised */
} c1541_track;

typedef struct c1541_media {
    int enabled; /* 1 when media_1541 config path is active for this drive */

    int motor_on;
    int motor_ready;
    uint32_t motor_spin_left;

    int half_track; /* even = whole track; 2 = track 1 */
    uint8_t stepper_phase; /* last STP0/STP1 (PB0/PB1) */
    int density; /* latched DS0/DS1 */

    uint32_t bit_acc; /* fractional bit clock */
    uint32_t head_bit_pos; /* bit index into current track */

    uint16_t shift10; /* last 10 bits under head (for SYNC) */
    int in_sync;
    int bits_in_byte;
    uint8_t shifting_byte;
    uint8_t port_a_byte;
    int byte_ready;
    int so_pulse; /* edge request for CPU SO */

    c1541_track tracks[C1541_MEDIA_TRACK_COUNT];
    int tracks_valid;
    const uint8_t *built_from; /* image_bytes pointer used for last build */
    size_t built_size;
} c1541_media;

void c1541_media_init(c1541_media *m);
void c1541_media_reset(c1541_media *m);
void c1541_media_free_tracks(c1541_media *m);

/* Build standard GCR tracks from a mounted 35-track D64 image. */
int c1541_media_build_from_d64(
    c1541_media *m,
    const uint8_t *image_bytes,
    size_t image_size);

/* One drive-cycle media step: sample disk VIA, rotate, update SYNC/Port A/SO. */
void c1541_media_step(struct c1541 *drive);

/* Called when the drive CPU reads disk-controller VIA Port A ($1C01). */
void c1541_media_on_port_a_read(struct c1541 *drive);

/* True when media mode should satisfy physical READ via rotation (no intercept). */
int c1541_media_physical_read_active(const struct c1541 *drive);

/* Invalidate synthesised tracks (e.g. after mount/unmount). */
void c1541_media_invalidate(c1541_media *m);

#endif /* C1541_MEDIA_H */
