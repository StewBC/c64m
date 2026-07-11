#include "c1541.h"
#include "c1541_gcr.h"
#include "c1541_media.h"
#include "c64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

/* Minimal blank-ish D64: 174848 zeros with BAM ID. */
static uint8_t *make_blank_d64(void) {
    uint8_t *img = (uint8_t *)calloc(1, 174848);
    int bam;
    if (img == NULL) fail("oom");
    bam = c1541_gcr_d64_sector_offset(18, 0);
    img[bam + 0] = 18; /* first dir track */
    img[bam + 1] = 1;
    img[bam + 2] = 'A'; /* DOS version */
    img[bam + 162] = 0x30;
    img[bam + 163] = 0x31;
    return img;
}

static void test_build_from_d64(void) {
    c1541_media m;
    uint8_t *img = make_blank_d64();

    c1541_media_init(&m);
    if (!c1541_media_build_from_d64(&m, img, 174848)) fail("build");
    if (!m.tracks_valid) fail("valid");
    if (m.tracks[18].data == NULL || m.tracks[18].length < 1000) fail("track18");
    if (m.tracks[1].density != 3) fail("dens1");
    if (m.tracks[35].density != 0) fail("dens35");
    c1541_media_free_tracks(&m);
    free(img);
    printf("PASS: test_build_from_d64\n");
}

static void test_stepper_and_head_stop(void) {
    static c64_t c64;
    static c1541 drive;

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 1;
    c1541_init(&drive, &c64, 8);
    drive.rom_loaded = 1;
    drive.media.enabled = 1;
    drive.media.half_track = 10;
    drive.media.stepper_phase = 0;

    /* PB0/PB1 outputs, step +1 phase (00 -> 01). */
    drive.via2.ddrb = 0x03u;
    drive.via2.orb = 0x01u;
    c1541_media_step(&drive);
    if (drive.media.half_track != 11) fail("step out");

    drive.via2.orb = 0x00u; /* 01 -> 00 is reverse (diff 3) */
    c1541_media_step(&drive);
    if (drive.media.half_track != 10) fail("step in");

    drive.media.half_track = C1541_MEDIA_MIN_HALF_TRACK;
    drive.media.stepper_phase = 0;
    drive.via2.orb = 0x03u; /* would step in further */
    /* 00 -> 11 is diff 3? (3-0)&3 = 3 → step in, clamped */
    c1541_media_step(&drive);
    if (drive.media.half_track < C1541_MEDIA_MIN_HALF_TRACK) fail("underflow");

    c1541_destroy(&drive);
    printf("PASS: test_stepper_and_head_stop\n");
}

static void test_motor_spinup_and_rotation(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img = make_blank_d64();
    uint32_t i;
    int saw_sync = 0;
    int saw_byte = 0;

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 1;
    c1541_init(&drive, &c64, 8);
    drive.rom_loaded = 1;
    drive.media.enabled = 1;
    if (!c1541_media_build_from_d64(&drive.media, img, 174848)) fail("build");

    /* Park head on track 18. */
    drive.media.half_track = 36;
    drive.media.stepper_phase = 0;

    /* Motor off: no rotation progress. */
    drive.via2.ddrb = 0x04u;
    drive.via2.orb = 0x00u;
    drive.media.head_bit_pos = 100;
    c1541_media_step(&drive);
    if (drive.media.head_bit_pos != 100) fail("motor off moved head");

    /* Motor on: spin-up then rotate; expect SYNC and BYTE READY eventually. */
    drive.via2.orb = 0x04u;
    drive.via2.ddrb = 0x64u; /* motor + density outputs */
    drive.via2.orb = 0x64u;  /* motor on, dens 3 */
    for (i = 0; i < C1541_MEDIA_SPINUP_CYCLES + 1u; ++i) {
        c1541_media_step(&drive);
    }
    if (!drive.media.motor_ready) fail("motor not ready");

    for (i = 0; i < 500000u; ++i) {
        c1541_media_step(&drive);
        if (drive.media.in_sync) {
            saw_sync = 1;
        }
        if (drive.media.byte_ready) {
            saw_byte = 1;
        }
        if (saw_sync && saw_byte) {
            break;
        }
    }
    if (!saw_sync) fail("never saw SYNC");
    if (!saw_byte) fail("never saw BYTE READY");

    /* Port A should present a GCR byte while ready. */
    if (drive.via2.port_a_in != drive.media.port_a_byte) fail("port a not latched");

    c1541_destroy(&drive);
    free(img);
    printf("PASS: test_motor_spinup_and_rotation\n");
}

static void test_wps_follows_writable(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img = make_blank_d64();

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 1;
    c1541_init(&drive, &c64, 8);
    drive.rom_loaded = 1;
    drive.media.enabled = 1;

    if (c64_mount_d64(&c64, 8, img, 174848, NULL, 0, "t", "", "", "", 0) != C64_DRIVE_STATUS_OK) {
        fail("mount");
    }
    /* Mount is read-only by default. */
    c64_set_drive_writable(&c64, 8, false);
    c1541_media_step(&drive);
    if ((drive.via2.port_b_in & 0x10u) != 0) fail("WPS should be clear when protected");

    c64_set_drive_writable(&c64, 8, true);
    c1541_media_step(&drive);
    if ((drive.via2.port_b_in & 0x10u) == 0) fail("WPS should be set when writable");

    c64_unmount_drive(&c64, 8);
    c1541_destroy(&drive);
    free(img);
    printf("PASS: test_wps_follows_writable\n");
}

static void test_physical_read_gate(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img = make_blank_d64();

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 0;
    c1541_init(&drive, &c64, 8);
    drive.media.enabled = 0;
    if (c1541_media_physical_read_active(&drive)) fail("inactive without media");

    drive.media.enabled = 1;
    if (!c1541_media_build_from_d64(&drive.media, img, 174848)) fail("build");
    if (!c1541_media_physical_read_active(&drive)) fail("active with tracks");
    if (!c1541_media_physical_write_active(&drive)) fail("write gate with tracks");

    c1541_destroy(&drive);
    free(img);
    printf("PASS: test_physical_read_gate\n");
}

/* Replace track 1 GCR with content from a second image and sync back to D64. */
static void test_sync_dirty_track_to_d64(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img;
    uint8_t *img2;
    c1541_media m2;
    int off;
    int i;

    img = make_blank_d64();
    img2 = make_blank_d64();
    off = c1541_gcr_d64_sector_offset(1, 0);
    for (i = 0; i < 256; ++i) {
        img[off + i] = 0x11;
        img2[off + i] = 0xA5;
    }

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 1;
    c1541_init(&drive, &c64, 8);
    drive.rom_loaded = 1;
    drive.media.enabled = 1;

    if (c64_mount_d64_ex(
            &c64, 8, img, 174848, NULL, 0, "t", "", "", "", 0, true)
        != C64_DRIVE_STATUS_OK) {
        fail("mount");
    }
    free(img);
    img = NULL;

    if (!c1541_media_build_from_d64(
            &drive.media,
            c64_get_drive_slot(&c64, 8)->image_bytes,
            174848)) {
        fail("build primary");
    }

    c1541_media_init(&m2);
    if (!c1541_media_build_from_d64(&m2, img2, 174848)) fail("build secondary");
    free(img2);

    /* Swap track 1 GCR for the 0xA5-filled variant and mark dirty. */
    free(drive.media.tracks[1].data);
    drive.media.tracks[1].data = m2.tracks[1].data;
    drive.media.tracks[1].length = m2.tracks[1].length;
    drive.media.tracks[1].density = m2.tracks[1].density;
    drive.media.tracks[1].dirty = 1;
    m2.tracks[1].data = NULL; /* ownership transferred */
    c1541_media_free_tracks(&m2);

    if (!c1541_media_sync_dirty_to_d64(&drive)) fail("sync returned 0");

    {
        const c64_drive_slot *slot = c64_get_drive_slot(&c64, 8);
        if (!slot->dirty) fail("slot not dirty after sync");
        for (i = 0; i < 256; ++i) {
            if (slot->image_bytes[off + i] != 0xA5) {
                fail("sector not updated from dirty track");
            }
        }
    }

    c64_unmount_drive(&c64, 8);
    c1541_destroy(&drive);
    printf("PASS: test_sync_dirty_track_to_d64\n");
}

static void test_write_mode_mutates_track(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img = make_blank_d64();
    uint32_t i;
    uint8_t before;
    int pos;

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 1;
    c1541_init(&drive, &c64, 8);
    drive.rom_loaded = 1;
    drive.media.enabled = 1;

    if (c64_mount_d64_ex(
            &c64, 8, img, 174848, NULL, 0, "t", "", "", "", 0, true)
        != C64_DRIVE_STATUS_OK) {
        fail("mount");
    }
    free(img);

    if (!c1541_media_build_from_d64(
            &drive.media,
            c64_get_drive_slot(&c64, 8)->image_bytes,
            174848)) {
        fail("build");
    }

    drive.media.half_track = 2; /* track 1 */
    drive.media.motor_on = 1;
    drive.media.motor_ready = 1;
    drive.via2.ddrb = 0x64u;
    drive.via2.orb = 0x64u;
    drive.via2.ddra = 0xFFu;
    drive.via2.pcr = 0xC0u; /* CB2 manual low = write gate on */
    drive.media.head_bit_pos = 0;
    before = drive.media.tracks[1].data[0];

    /* Enter write, latch a non-gap byte, clock enough bits to commit it. */
    c1541_media_step(&drive);
    if (!drive.media.writing) fail("not in write mode");
    c1541_media_on_port_a_write(&drive, 0x00u);

    for (i = 0; i < 1000u; ++i) {
        c1541_media_step(&drive);
        if (drive.media.tracks[1].dirty) {
            break;
        }
    }
    if (!drive.media.tracks[1].dirty) fail("track not dirty after write");

    pos = 0;
    /* At least one bit under the head should have changed for 0x00 vs typical 0xFF sync. */
    if (drive.media.tracks[1].data[0] == before && before == 0xFFu) {
        /* First byte was sync 0xFF; writing 0x00 must clear bits. */
        fail("track byte unchanged after writing 0x00 over sync");
    }

    /* Leave write mode → sync attempt (may or may not change D64 sectors). */
    drive.via2.ddra = 0x00u;
    c1541_media_step(&drive);
    if (drive.media.writing) fail("still writing after DDRA clear");

    c64_unmount_drive(&c64, 8);
    c1541_destroy(&drive);
    printf("PASS: test_write_mode_mutates_track\n");
    (void)pos;
}

int main(void) {
    test_build_from_d64();
    test_stepper_and_head_stop();
    test_motor_spinup_and_rotation();
    test_wps_follows_writable();
    test_physical_read_gate();
    test_sync_dirty_track_to_d64();
    test_write_mode_mutates_track();
    printf("All c1541_media tests passed.\n");
    return 0;
}
