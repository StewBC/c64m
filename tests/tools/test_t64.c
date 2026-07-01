#include "t64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, int value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_size(const char *name, size_t expected, size_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %zu, got %zu\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u8(const char *name, unsigned expected, unsigned actual) {
    if ((expected & 0xffu) != (actual & 0xffu)) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected & 0xffu, actual & 0xffu);
        exit(1);
    }
}

static void make_minimal_t64(unsigned char *bytes, size_t size) {
    memset(bytes, 0, size);
    memcpy(bytes, "C64 tape image file", 19);
    bytes[0x20] = 0x00;
    bytes[0x21] = 0x01;
    bytes[0x22] = 0x01;
    bytes[0x23] = 0x00;
    bytes[0x24] = 0x01;
    bytes[0x25] = 0x00;
    bytes[0x40] = 0x01;
    bytes[0x41] = 0x82;
    bytes[0x42] = 0x00;
    bytes[0x43] = 0x20;
    bytes[0x44] = 0x03;
    bytes[0x45] = 0x20;
    bytes[0x48] = 0x60;
    bytes[0x60] = 0xaa;
    bytes[0x61] = 0xbb;
    bytes[0x62] = 0xcc;
}

static unsigned char *read_file(const char *path, size_t *out_size) {
    FILE *file;
    long length;
    unsigned char *bytes;

    file = fopen(path, "rb");
    if (file == NULL) {
        fail("failed to open sample T64");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fail("failed to seek sample T64");
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fail("failed to inspect sample T64");
    }
    bytes = malloc((size_t)length);
    if (bytes == NULL) {
        fail("failed to allocate sample T64 buffer");
    }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        fail("failed to read sample T64");
    }
    fclose(file);
    *out_size = (size_t)length;
    return bytes;
}

static void test_extract_minimal(void) {
    unsigned char bytes[0x63];
    t64_file_data file = {0};

    make_minimal_t64(bytes, sizeof(bytes));

    expect_true("extract minimal", t64_extract_first_prg(bytes, sizeof(bytes), &file) == T64_OK);
    expect_size("minimal size", 5, file.size);
    expect_u8("minimal load lo", 0x00, file.bytes[0]);
    expect_u8("minimal load hi", 0x20, file.bytes[1]);
    expect_u8("minimal byte 0", 0xaa, file.bytes[2]);
    expect_u8("minimal byte 1", 0xbb, file.bytes[3]);
    expect_u8("minimal byte 2", 0xcc, file.bytes[4]);

    t64_file_data_free(&file);
}

static void test_extracts_with_bogus_end_address(void) {
    unsigned char bytes[0x63];
    t64_file_data file = {0};

    make_minimal_t64(bytes, sizeof(bytes));
    bytes[0x44] = 0xc6;
    bytes[0x45] = 0xc3;

    expect_true(
        "extract bogus end",
        t64_extract_first_prg(bytes, sizeof(bytes), &file) == T64_OK);
    expect_size("bogus end size", 5, file.size);
    expect_u8("bogus end load lo", 0x00, file.bytes[0]);
    expect_u8("bogus end load hi", 0x20, file.bytes[1]);
    expect_u8("bogus end byte 0", 0xaa, file.bytes[2]);

    t64_file_data_free(&file);
}

static void test_rejects_bad_range(void) {
    unsigned char bytes[0x63];
    t64_file_data file = {0};

    make_minimal_t64(bytes, sizeof(bytes));
    bytes[0x48] = 0x70;

    expect_true(
        "reject bad range",
        t64_extract_first_prg(bytes, sizeof(bytes), &file) == T64_ENTRY_RANGE_INVALID);
}

static void test_extract_snakebyt_sample(void) {
    unsigned char *bytes;
    size_t size;
    t64_file_data file = {0};

    bytes = read_file(C64M_SOURCE_DIR "/assets/tapes/SNAKEBYT.T64", &size);

    expect_true("extract sample", t64_extract_first_prg(bytes, size, &file) == T64_OK);
    expect_size("sample prg size", 8960, file.size);
    expect_u8("sample load lo", 0x01, file.bytes[0]);
    expect_u8("sample load hi", 0x08, file.bytes[1]);
    expect_u8("sample first payload lo", 0x0b, file.bytes[2]);
    expect_u8("sample first payload hi", 0x08, file.bytes[3]);

    t64_file_data_free(&file);
    free(bytes);
}

int main(void) {
    test_extract_minimal();
    test_extracts_with_bogus_end_address();
    test_rejects_bad_range();
    test_extract_snakebyt_sample();
    return 0;
}
