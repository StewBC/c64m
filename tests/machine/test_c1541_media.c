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
    /* halves[]: track N.0 at index (N-1)*2 */
    if (m.halves[34].data == NULL || m.halves[34].length < 1000) fail("track18");
    if (m.halves[0].density != 3) fail("dens1");
    if (m.halves[68].density != 0) fail("dens35");
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

    /* Swap track 1.0 GCR for the 0xA5-filled variant and mark dirty. */
    free(drive.media.halves[0].data);
    drive.media.halves[0].data = m2.halves[0].data;
    drive.media.halves[0].length = m2.halves[0].length;
    drive.media.halves[0].density = m2.halves[0].density;
    drive.media.halves[0].dirty = 1;
    m2.halves[0].data = NULL; /* ownership transferred */
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
    before = drive.media.halves[0].data[0];

    /* Enter write, latch a non-gap byte, clock enough bits to commit it. */
    c1541_media_step(&drive);
    if (!drive.media.writing) fail("not in write mode");
    c1541_media_on_port_a_write(&drive, 0x00u);

    for (i = 0; i < 1000u; ++i) {
        c1541_media_step(&drive);
        if (drive.media.halves[0].dirty) {
            break;
        }
    }
    if (!drive.media.halves[0].dirty) fail("track not dirty after write");

    pos = 0;
    /* At least one bit under the head should have changed for 0x00 vs typical 0xFF sync. */
    if (drive.media.halves[0].data[0] == before && before == 0xFFu) {
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

/* Build a tiny G64 containing one whole track of 0xFF sync noise. */
static uint8_t *make_min_g64(size_t *out_size) {
    const size_t header = 0x2ACu;
    const size_t track_off = header;
    const uint16_t track_len = 64;
    const size_t total = track_off + 2u + track_len;
    uint8_t *b = (uint8_t *)calloc(1, total);
    int i;
    if (b == NULL) fail("oom g64");
    memcpy(b, "GCR-1541", 8);
    b[8] = 0;
    b[9] = 84;
    b[10] = (uint8_t)(7928 & 0xff);
    b[11] = (uint8_t)(7928 >> 8);
    b[12] = (uint8_t)(track_off & 0xff);
    b[13] = (uint8_t)((track_off >> 8) & 0xff);
    for (i = 0; i < 84; ++i) {
        b[0x15Cu + (size_t)i * 4u] = 3;
    }
    b[track_off] = (uint8_t)(track_len & 0xff);
    b[track_off + 1] = (uint8_t)(track_len >> 8);
    memset(b + track_off + 2, 0xFF, track_len);
    *out_size = total;
    return b;
}

static void test_build_from_g64(void) {
    size_t sz;
    uint8_t *g = make_min_g64(&sz);
    c1541_media m;

    c1541_media_init(&m);
    if (!c1541_media_build_from_g64(&m, g, sz)) fail("g64 build");
    if (!m.tracks_valid || !m.from_g64) fail("g64 flags");
    if (m.halves[0].data == NULL || m.halves[0].length != 64) fail("g64 track1");
    if (m.halves[0].data[0] != 0xFF) fail("g64 payload");
    c1541_media_free_tracks(&m);
    free(g);
    printf("PASS: test_build_from_g64\n");
}

static void test_mount_g64_readonly(void) {
    static c64_t c64;
    size_t sz;
    uint8_t *g = make_min_g64(&sz);

    c64_init(&c64);
    if (c64_mount_g64(&c64, 8, g, sz, "test.g64") != C64_DRIVE_STATUS_OK) {
        fail("mount g64");
    }
    free(g);
    {
        const c64_drive_slot *slot = c64_get_drive_slot(&c64, 8);
        if (!slot->mounted || slot->image_kind != C64_DRIVE_IMAGE_G64) fail("kind");
        if (slot->writable) fail("g64 should be read-only");
        if (c64_set_drive_writable(&c64, 8, true)) fail("cannot force writable");
        if (slot->writable) fail("still writable");
    }
    c64_unmount_drive(&c64, 8);
    printf("PASS: test_mount_g64_readonly\n");
}

/* Regression: D64 writes while media is off must force GCR rebuild when media
   returns. Previously ensure_tracks only keyed on image pointer/size, so a
   media-on LOAD"$" could still show a directory from an older GCR cache. */
static void test_media_rebuild_after_offline_d64_write(void) {
    static c64_t c64;
    c1541 *drive;
    uint8_t *img = make_blank_d64();
    c64_drive_slot *slot;
    uint32_t seq_after_build;
    int dir_sec = c1541_gcr_d64_sector_offset(18, 1); /* first directory sector */

    c64_init(&c64);
    c64.config.emulate_1541 = 1;
    c64.config.media_1541 = 1;
    if (c64_mount_d64_ex(
            &c64, 8, img, 174848, NULL, 0, "scratch.d64", "", "", "", 0, true)
        != C64_DRIVE_STATUS_OK) {
        fail("mount d64");
    }
    free(img); /* machine owns a copy */

    /* Use the machine-owned drive so c64_set_config invalidates the same media. */
    drive = &c64.drive8;
    drive->rom_loaded = 1;
    drive->media.enabled = 1;

    /* Build GCR while media is on. */
    c1541_media_step(drive);
    if (!drive->media.tracks_valid) fail("tracks after first step");
    seq_after_build = drive->media.built_from_seq;
    slot = c64_get_drive_slot_mut(&c64, 8);
    if (slot == NULL || slot->image_content_seq != seq_after_build) {
        fail("built seq matches slot after build");
    }

    /* Simulate media-off SAVE: mutate the live D64 and bump the content seq. */
    slot->image_bytes[dir_sec] = 0xA5u;
    slot->image_content_seq++;
    slot->dirty = true;

    /* Same pointer/size would previously skip rebuild — content seq must force it. */
    if (drive->media.built_from == slot->image_bytes &&
        drive->media.built_size == slot->image_size &&
        drive->media.built_from_seq == slot->image_content_seq) {
        fail("seq should differ after offline write");
    }

    c1541_media_step(drive);
    if (!drive->media.tracks_valid) fail("tracks after rebuild step");
    if (drive->media.built_from_seq != slot->image_content_seq) {
        fail("built seq not updated after offline write rebuild");
    }
    if (drive->media.built_from_seq == seq_after_build) {
        fail("built seq should advance after offline write");
    }

    /* Config toggle media off/on must also drop any cache (belt and braces). */
    {
        c64_config cfg = c64.config;
        cfg.media_1541 = 0;
        c64_set_config(&c64, &cfg);
        if (drive->media.tracks_valid) fail("tracks should clear when media disabled");
        cfg.media_1541 = 1;
        c64_set_config(&c64, &cfg);
        drive->media.enabled = 1; /* mirror c1541_advance_one_cycle enable latch */
        c1541_media_step(drive);
        if (!drive->media.tracks_valid) fail("tracks rebuild after re-enable");
        if (drive->media.built_from_seq != slot->image_content_seq) {
            fail("seq match after re-enable rebuild");
        }
    }

    c64_unmount_drive(&c64, 8);
    printf("PASS: test_media_rebuild_after_offline_d64_write\n");
}

int main(void) {
    test_build_from_d64();
    test_stepper_and_head_stop();
    test_motor_spinup_and_rotation();
    test_wps_follows_writable();
    test_physical_read_gate();
    test_sync_dirty_track_to_d64();
    test_write_mode_mutates_track();
    test_build_from_g64();
    test_mount_g64_readonly();
    test_media_rebuild_after_offline_d64_write();
    printf("All c1541_media tests passed.\n");
    return 0;
}
