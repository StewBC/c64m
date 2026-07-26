#include "crt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CRT_TEST_HEADER_SIZE = 0x40,
    CRT_TEST_CHIP_HEADER_SIZE = 0x10
};

static int expect_result(crt_result actual, crt_result expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr,
            "%s: expected %s, got %s\n",
            label,
            crt_result_string(expected),
            crt_result_string(actual));
        return 1;
    }
    return 0;
}

static int expect_true(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "%s: expected true\n", label);
        return 1;
    }
    return 0;
}

static int expect_false(int condition, const char *label)
{
    if (condition) {
        fprintf(stderr, "%s: expected false\n", label);
        return 1;
    }
    return 0;
}

static int expect_size(size_t actual, size_t expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %zu, got %zu\n", label, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_u16(uint16_t actual, uint16_t expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %04x, got %04x\n", label, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_u8(uint8_t actual, uint8_t expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", label, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_string(const char *actual, const char *expected, const char *label)
{
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr,
            "%s: expected '%s', got '%s'\n",
            label,
            expected,
            actual == NULL ? "(null)" : actual);
        return 1;
    }
    return 0;
}

static void put_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)(value & 0xffu);
}

static void put_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)((value >> 16) & 0xffu);
    target[2] = (uint8_t)((value >> 8) & 0xffu);
    target[3] = (uint8_t)(value & 0xffu);
}

static uint8_t *make_crt(
    const char *name,
    uint16_t hardware_type,
    uint8_t exrom,
    uint8_t game,
    uint16_t chip_type,
    uint16_t bank,
    uint16_t load_address,
    uint16_t rom_size,
    size_t *out_size)
{
    uint8_t *bytes;
    size_t size = CRT_TEST_HEADER_SIZE + CRT_TEST_CHIP_HEADER_SIZE + (size_t)rom_size;
    size_t i;

    bytes = (uint8_t *)calloc(1, size);
    if (bytes == NULL) {
        return NULL;
    }

    memcpy(bytes, "C64 CARTRIDGE   ", 16);
    put_be32(&bytes[0x10], CRT_TEST_HEADER_SIZE);
    put_be16(&bytes[0x14], 0x0100);
    put_be16(&bytes[0x16], hardware_type);
    bytes[0x18] = exrom;
    bytes[0x19] = game;
    snprintf((char *)&bytes[0x20], 32, "%s", name);

    memcpy(&bytes[CRT_TEST_HEADER_SIZE], "CHIP", 4);
    put_be32(&bytes[CRT_TEST_HEADER_SIZE + 0x04],
        CRT_TEST_CHIP_HEADER_SIZE + (uint32_t)rom_size);
    put_be16(&bytes[CRT_TEST_HEADER_SIZE + 0x08], chip_type);
    put_be16(&bytes[CRT_TEST_HEADER_SIZE + 0x0a], bank);
    put_be16(&bytes[CRT_TEST_HEADER_SIZE + 0x0c], load_address);
    put_be16(&bytes[CRT_TEST_HEADER_SIZE + 0x0e], rom_size);
    for (i = 0; i < rom_size; ++i) {
        bytes[CRT_TEST_HEADER_SIZE + CRT_TEST_CHIP_HEADER_SIZE + i] =
            (uint8_t)(0x80u + (i & 0x7fu));
    }

    *out_size = size;
    return bytes;
}

static int test_parse_generic_8k(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result;
    crt_image *image;
    const crt_header *header;
    crt_chip chip;

    bytes = make_crt("GENERIC 8K", 0, 0, 1, 0, 0, 0x8000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }

    image = crt_image_create(bytes, size, &result);
    failures += expect_result(result, CRT_OK, "generic 8K parse result");
    failures += expect_true(image != NULL, "generic 8K image");
    header = crt_image_header(image);
    failures += expect_true(header != NULL, "generic 8K header");
    if (header != NULL) {
        failures += expect_u16(header->version, 0x0100, "generic 8K version");
        failures += expect_u16(header->hardware_type, 0, "generic 8K hardware");
        failures += expect_u8(header->exrom, 0, "generic 8K exrom");
        failures += expect_u8(header->game, 1, "generic 8K game");
        failures += expect_string(header->name, "GENERIC 8K", "generic 8K name");
    }
    failures += expect_size(crt_image_chip_count(image), 1, "generic 8K chip count");
    failures += expect_result(crt_image_chip(image, 0, &chip), CRT_OK, "generic 8K chip");
    failures += expect_u16(chip.raw_type, 0, "generic 8K chip raw type");
    failures += expect_true(chip.type == CRT_CHIP_TYPE_ROM, "generic 8K chip type");
    failures += expect_u16(chip.bank, 0, "generic 8K bank");
    failures += expect_u16(chip.load_address, 0x8000, "generic 8K load");
    failures += expect_u16(chip.rom_size, 0x2000, "generic 8K size");
    failures += expect_u8(chip.bytes[0], 0x80, "generic 8K first byte");
    failures += expect_true(crt_image_is_generic_supported(image), "generic 8K supported");

    crt_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_parse_generic_16k(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result;
    crt_image *image;
    const crt_header *header;
    crt_chip chip;

    bytes = make_crt("INTERNATIONAL SOCCER", 0, 0, 0, 0, 0, 0x8000, 0x4000, &size);
    if (bytes == NULL) {
        return 1;
    }

    image = crt_image_create(bytes, size, &result);
    failures += expect_result(result, CRT_OK, "generic 16K parse result");
    failures += expect_true(image != NULL, "generic 16K image");
    header = crt_image_header(image);
    failures += expect_true(header != NULL, "generic 16K header");
    if (header != NULL) {
        failures += expect_string(header->name, "INTERNATIONAL SOCCER", "generic 16K name");
        failures += expect_u8(header->exrom, 0, "generic 16K exrom");
        failures += expect_u8(header->game, 0, "generic 16K game");
    }
    failures += expect_result(crt_image_chip(image, 0, &chip), CRT_OK, "generic 16K chip");
    failures += expect_u16(chip.load_address, 0x8000, "generic 16K load");
    failures += expect_u16(chip.rom_size, 0x4000, "generic 16K size");
    failures += expect_true(crt_image_is_generic_supported(image), "generic 16K supported");

    crt_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_rejects_bad_magic(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result = CRT_OK;
    crt_image *image;

    bytes = make_crt("BAD", 0, 0, 1, 0, 0, 0x8000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }
    bytes[0] = 'X';

    image = crt_image_create(bytes, size, &result);
    failures += expect_true(image == NULL, "bad magic no image");
    failures += expect_result(result, CRT_UNSUPPORTED_IMAGE, "bad magic result");

    free(bytes);
    return failures;
}

static int test_rejects_truncated_header(void)
{
    int failures = 0;
    uint8_t bytes[16] = {0};
    crt_result result = CRT_OK;

    failures += expect_true(
        crt_image_create(bytes, sizeof(bytes), &result) == NULL,
        "truncated header no image");
    failures += expect_result(result, CRT_MALFORMED_HEADER, "truncated header result");
    return failures;
}

static int test_rejects_truncated_chip_header(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result = CRT_OK;

    bytes = make_crt("TRUNC", 0, 0, 1, 0, 0, 0x8000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }

    failures += expect_true(
        crt_image_create(bytes, CRT_TEST_HEADER_SIZE + 8u, &result) == NULL,
        "truncated chip no image");
    failures += expect_result(result, CRT_MALFORMED_CHIP, "truncated chip result");

    free(bytes);
    return failures;
}

static int test_rejects_short_chip_packet_length(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result = CRT_OK;

    bytes = make_crt("SHORT", 0, 0, 1, 0, 0, 0x8000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }
    put_be32(&bytes[CRT_TEST_HEADER_SIZE + 0x04], 8);

    failures += expect_true(
        crt_image_create(bytes, size, &result) == NULL,
        "short packet no image");
    failures += expect_result(result, CRT_MALFORMED_CHIP, "short packet result");

    free(bytes);
    return failures;
}

static int test_rejects_oversized_chip_packet_length(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result = CRT_OK;

    bytes = make_crt("LONG", 0, 0, 1, 0, 0, 0x8000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }
    put_be32(&bytes[CRT_TEST_HEADER_SIZE + 0x04], (uint32_t)(size + 1u));

    failures += expect_true(
        crt_image_create(bytes, size, &result) == NULL,
        "oversized packet no image");
    failures += expect_result(result, CRT_MALFORMED_CHIP, "oversized packet result");

    free(bytes);
    return failures;
}

static int test_parses_unsupported_hardware(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result;
    crt_image *image;

    bytes = make_crt("UNSUPPORTED", 32, 0, 1, 0, 0, 0x8000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }

    image = crt_image_create(bytes, size, &result);
    failures += expect_result(result, CRT_OK, "unsupported hardware parse");
    failures += expect_true(image != NULL, "unsupported hardware image");
    failures += expect_false(crt_image_is_generic_supported(image), "unsupported hardware generic");

    crt_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_classifies_unsupported_load_address(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result;
    crt_image *image;

    bytes = make_crt("BADLOAD", 0, 0, 1, 0, 0, 0xc000, 0x2000, &size);
    if (bytes == NULL) {
        return 1;
    }

    image = crt_image_create(bytes, size, &result);
    failures += expect_result(result, CRT_OK, "bad load parse");
    failures += expect_true(image != NULL, "bad load image");
    failures += expect_false(crt_image_is_generic_supported(image), "bad load unsupported generic");

    crt_image_destroy(image);
    free(bytes);
    return failures;
}

static uint8_t *make_magic_desk_crt(size_t bank_count, size_t *out_size)
{
    size_t i;
    size_t size;
    uint8_t *bytes;
    size_t offset;

    if (bank_count == 0) {
        return NULL;
    }

    size = CRT_TEST_HEADER_SIZE +
        bank_count * (CRT_TEST_CHIP_HEADER_SIZE + 0x2000u);
    bytes = (uint8_t *)calloc(1, size);
    if (bytes == NULL) {
        return NULL;
    }

    memcpy(bytes, "C64 CARTRIDGE   ", 16);
    put_be32(&bytes[0x10], CRT_TEST_HEADER_SIZE);
    put_be16(&bytes[0x14], 0x0100);
    put_be16(&bytes[0x16], 19); /* Magic Desk */
    bytes[0x18] = 0; /* EXROM */
    bytes[0x19] = 1; /* GAME */
    snprintf((char *)&bytes[0x20], 32, "%s", "MAGIC DESK TEST");

    offset = CRT_TEST_HEADER_SIZE;
    for (i = 0; i < bank_count; ++i) {
        size_t j;
        memcpy(&bytes[offset], "CHIP", 4);
        put_be32(&bytes[offset + 0x04], CRT_TEST_CHIP_HEADER_SIZE + 0x2000u);
        put_be16(&bytes[offset + 0x08], 0); /* ROM */
        put_be16(&bytes[offset + 0x0a], (uint16_t)i);
        put_be16(&bytes[offset + 0x0c], 0x8000);
        put_be16(&bytes[offset + 0x0e], 0x2000);
        for (j = 0; j < 0x2000u; ++j) {
            bytes[offset + CRT_TEST_CHIP_HEADER_SIZE + j] =
                (uint8_t)((i << 4) | (j & 0x0fu));
        }
        offset += CRT_TEST_CHIP_HEADER_SIZE + 0x2000u;
    }

    *out_size = size;
    return bytes;
}

static int test_parse_magic_desk(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result;
    crt_image *image;
    const crt_header *header;
    crt_chip chip;

    bytes = make_magic_desk_crt(4, &size);
    if (bytes == NULL) {
        return 1;
    }

    image = crt_image_create(bytes, size, &result);
    failures += expect_result(result, CRT_OK, "magic desk parse");
    failures += expect_true(image != NULL, "magic desk image");
    header = crt_image_header(image);
    failures += expect_true(header != NULL, "magic desk header");
    if (header != NULL) {
        failures += expect_u16(header->hardware_type, 19, "magic desk type");
        failures += expect_u8(header->exrom, 0, "magic desk exrom");
        failures += expect_u8(header->game, 1, "magic desk game");
    }
    failures += expect_size(crt_image_chip_count(image), 4, "magic desk chips");
    failures += expect_result(crt_image_chip(image, 2, &chip), CRT_OK, "magic desk chip2");
    failures += expect_u16(chip.bank, 2, "magic desk chip2 bank");
    failures += expect_true(crt_image_is_magic_desk_supported(image), "magic desk supported");
    failures += expect_true(crt_image_is_supported(image), "magic desk is_supported");
    failures += expect_false(crt_image_is_generic_supported(image), "magic desk not generic");

    crt_image_destroy(image);
    free(bytes);
    return failures;
}

static uint8_t *make_ocean_crt(size_t bank_count, size_t *out_size)
{
    size_t i;
    size_t size;
    uint8_t *bytes;
    size_t offset;

    if (bank_count == 0) {
        return NULL;
    }

    size = CRT_TEST_HEADER_SIZE +
        bank_count * (CRT_TEST_CHIP_HEADER_SIZE + 0x2000u);
    bytes = (uint8_t *)calloc(1, size);
    if (bytes == NULL) {
        return NULL;
    }

    memcpy(bytes, "C64 CARTRIDGE   ", 16);
    put_be32(&bytes[0x10], CRT_TEST_HEADER_SIZE);
    put_be16(&bytes[0x14], 0x0100);
    put_be16(&bytes[0x16], 5); /* Ocean */
    bytes[0x18] = 0;
    bytes[0x19] = 0; /* 16K game lines common for Ocean type A */
    snprintf((char *)&bytes[0x20], 32, "%s", "OCEAN TEST");

    offset = CRT_TEST_HEADER_SIZE;
    for (i = 0; i < bank_count; ++i) {
        size_t j;
        memcpy(&bytes[offset], "CHIP", 4);
        put_be32(&bytes[offset + 0x04], CRT_TEST_CHIP_HEADER_SIZE + 0x2000u);
        put_be16(&bytes[offset + 0x08], 0);
        put_be16(&bytes[offset + 0x0a], (uint16_t)i);
        put_be16(&bytes[offset + 0x0c], 0x8000);
        put_be16(&bytes[offset + 0x0e], 0x2000);
        for (j = 0; j < 0x2000u; ++j) {
            bytes[offset + CRT_TEST_CHIP_HEADER_SIZE + j] =
                (uint8_t)((i << 4) | (j & 0x0fu));
        }
        offset += CRT_TEST_CHIP_HEADER_SIZE + 0x2000u;
    }

    *out_size = size;
    return bytes;
}

static int test_parse_ocean(void)
{
    int failures = 0;
    uint8_t *bytes;
    size_t size = 0;
    crt_result result;
    crt_image *image;
    const crt_header *header;

    bytes = make_ocean_crt(8, &size);
    if (bytes == NULL) {
        return 1;
    }

    image = crt_image_create(bytes, size, &result);
    failures += expect_result(result, CRT_OK, "ocean parse");
    failures += expect_true(image != NULL, "ocean image");
    header = crt_image_header(image);
    failures += expect_true(header != NULL, "ocean header");
    if (header != NULL) {
        failures += expect_u16(header->hardware_type, 5, "ocean type");
    }
    failures += expect_size(crt_image_chip_count(image), 8, "ocean chips");
    failures += expect_true(crt_image_is_ocean_supported(image), "ocean supported");
    failures += expect_true(crt_image_is_supported(image), "ocean is_supported");
    failures += expect_false(crt_image_is_generic_supported(image), "ocean not generic");
    failures += expect_false(crt_image_is_magic_desk_supported(image), "ocean not magic desk");

    crt_image_destroy(image);
    free(bytes);
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_parse_generic_8k();
    failures += test_parse_generic_16k();
    failures += test_rejects_bad_magic();
    failures += test_rejects_truncated_header();
    failures += test_rejects_truncated_chip_header();
    failures += test_rejects_short_chip_packet_length();
    failures += test_rejects_oversized_chip_packet_length();
    failures += test_parses_unsupported_hardware();
    failures += test_classifies_unsupported_load_address();
    failures += test_parse_magic_desk();
    failures += test_parse_ocean();

    return failures == 0 ? 0 : 1;
}
