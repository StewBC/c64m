#include "c1541_gcr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void test_nibble_roundtrip(void) {
    uint8_t raw[4] = {0x00, 0x12, 0xAB, 0xFF};
    uint8_t enc[5];
    uint8_t dec[4];
    size_t n;

    n = c1541_gcr_encode(raw, 4, enc);
    if (n != 5) fail("encode length");
    n = c1541_gcr_decode(enc, 5, dec);
    if (n != 4) fail("decode length");
    if (memcmp(raw, dec, 4) != 0) fail("nibble roundtrip");
    printf("PASS: test_nibble_roundtrip\n");
}

static void test_all_nybbles(void) {
    uint8_t raw[16];
    uint8_t enc[20];
    uint8_t dec[16];
    int i;

    for (i = 0; i < 16; ++i) {
        raw[i] = (uint8_t)((i << 4) | ((i + 3) & 0x0F));
    }
    if (c1541_gcr_encode(raw, 16, enc) != 20) fail("encode 16");
    if (c1541_gcr_decode(enc, 20, dec) != 16) fail("decode 16");
    if (memcmp(raw, dec, 16) != 0) fail("all nybbles roundtrip");
    printf("PASS: test_all_nybbles\n");
}

static void test_sector_block_roundtrip(void) {
    uint8_t sector[256];
    uint8_t raw[C1541_GCR_DATA_RAW];
    uint8_t enc[C1541_GCR_DATA_ENC];
    uint8_t dec_raw[C1541_GCR_DATA_RAW];
    uint8_t out[256];
    int i;

    for (i = 0; i < 256; ++i) {
        sector[i] = (uint8_t)(i * 3 + 7);
    }
    c1541_gcr_make_data_raw(sector, raw);
    if (c1541_gcr_encode(raw, C1541_GCR_DATA_RAW, enc) != C1541_GCR_DATA_ENC) {
        fail("data encode");
    }
    if (c1541_gcr_decode(enc, C1541_GCR_DATA_ENC, dec_raw) != C1541_GCR_DATA_RAW) {
        fail("data decode");
    }
    if (!c1541_gcr_data_raw_to_sector(dec_raw, out)) {
        fail("data raw to sector");
    }
    if (memcmp(sector, out, 256) != 0) {
        fail("sector content");
    }
    printf("PASS: test_sector_block_roundtrip\n");
}

static void test_header_fields(void) {
    uint8_t raw[C1541_GCR_HEADER_RAW];
    c1541_gcr_make_header_raw(18, 1, 0x41, 0x42, raw);
    if (raw[0] != 0x08) fail("header id");
    if (raw[2] != 1) fail("sector");
    if (raw[3] != 18) fail("track");
    if (raw[1] != (uint8_t)(1 ^ 18 ^ 0x41 ^ 0x42)) fail("checksum");
    printf("PASS: test_header_fields\n");
}

static void test_zones(void) {
    if (c1541_gcr_sectors_per_track(1) != 21) fail("spt1");
    if (c1541_gcr_sectors_per_track(18) != 19) fail("spt18");
    if (c1541_gcr_sectors_per_track(35) != 17) fail("spt35");
    if (c1541_gcr_density_for_track(1) != 3) fail("dens outer");
    if (c1541_gcr_density_for_track(35) != 0) fail("dens inner");
    if (c1541_gcr_cycles_per_byte(3) != 26) fail("cpb3");
    if (c1541_gcr_cycles_per_byte(0) != 32) fail("cpb0");
    if (c1541_gcr_d64_sector_offset(18, 0) != 0x16500) fail("bam offset");
    printf("PASS: test_zones\n");
}

int main(void) {
    test_nibble_roundtrip();
    test_all_nybbles();
    test_sector_block_roundtrip();
    test_header_fields();
    test_zones();
    printf("All c1541_gcr tests passed.\n");
    return 0;
}
