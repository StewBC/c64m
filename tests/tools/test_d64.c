#include "d64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

static int expect_result(d64_result actual, d64_result expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr,
            "%s: expected %s, got %s\n",
            label,
            d64_result_string(expected),
            d64_result_string(actual));
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

static int expect_true(bool condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "%s: expected true\n", label);
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

static int read_file(const char *path, uint8_t **out_bytes, size_t *out_size)
{
    FILE *file;
    long size;
    uint8_t *bytes;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "failed to open %s\n", path);
        return 1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return 1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }

    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return 1;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return 1;
    }
    fclose(file);

    *out_bytes = bytes;
    *out_size = (size_t)size;
    return 0;
}

static void fill_name(uint8_t *target, const char *name)
{
    size_t i;

    memset(target, 0xa0, D64_DIRECTORY_NAME_SIZE);
    for (i = 0; i < D64_DIRECTORY_NAME_SIZE && name[i] != '\0'; ++i) {
        target[i] = (uint8_t)name[i];
    }
}

static uint8_t *make_empty_image(void)
{
    uint8_t *image;
    size_t offset;
    uint8_t *bam;
    uint8_t *directory;

    image = (uint8_t *)calloc(1, D64_STANDARD_IMAGE_SIZE);
    if (image == NULL) {
        return NULL;
    }

    if (d64_track_sector_offset(18, 0, &offset) == D64_OK) {
        bam = &image[offset];
        bam[0] = 18;
        bam[1] = 1;
        bam[2] = 0x41;
        fill_name(&bam[0x90], "TEST DISK");
        bam[0xa2] = 'I';
        bam[0xa3] = 'D';
        bam[0xa5] = '2';
        bam[0xa6] = 'A';
    }
    if (d64_track_sector_offset(18, 1, &offset) == D64_OK) {
        directory = &image[offset];
        directory[0] = 0;
        directory[1] = 0xff;
    }

    return image;
}

static void put_directory_entry(
    uint8_t *image,
    size_t slot,
    uint8_t raw_type,
    const char *name,
    uint8_t first_track,
    uint8_t first_sector,
    uint16_t blocks)
{
    size_t offset;
    size_t base;
    uint8_t *directory;

    if (d64_track_sector_offset(18, 1, &offset) != D64_OK) {
        return;
    }
    directory = &image[offset];
    base = slot * 32u;
    directory[base + 2u] = raw_type;
    directory[base + 3u] = first_track;
    directory[base + 4u] = first_sector;
    fill_name(&directory[base + 5u], name);
    directory[base + 30u] = (uint8_t)(blocks & 0xffu);
    directory[base + 31u] = (uint8_t)(blocks >> 8);
}

static void put_final_file_sector(
    uint8_t *image,
    uint8_t track,
    uint8_t sector_id,
    const uint8_t *data,
    size_t data_size)
{
    size_t offset;
    uint8_t *sector;

    if (d64_track_sector_offset(track, sector_id, &offset) != D64_OK) {
        return;
    }
    sector = &image[offset];
    sector[0] = 0;
    sector[1] = (uint8_t)(data_size + 1u);
    memcpy(&sector[2], data, data_size);
}

static int test_geometry_and_size(void)
{
    int failures = 0;
    size_t offset = 0;

    failures += expect_true(d64_image_size_supported(D64_STANDARD_IMAGE_SIZE), "standard size supported");
    failures += expect_true(d64_image_size_supported(D64_ERROR_INFO_IMAGE_SIZE), "error-info size supported");
    failures += expect_true(!d64_image_size_supported(D64_STANDARD_IMAGE_SIZE + 1u), "odd size rejected");
    failures += expect_true(!d64_image_size_supported(196608u), "40-track size rejected");
    failures += expect_result(d64_track_sector_offset(1, 0, &offset), D64_OK, "track 1 sector 0");
    failures += expect_size(offset, 0, "track 1 sector 0 offset");
    failures += expect_result(d64_track_sector_offset(18, 0, &offset), D64_OK, "track 18 sector 0");
    failures += expect_size(offset, 91392, "track 18 sector 0 offset");
    failures += expect_result(d64_track_sector_offset(0, 0, &offset), D64_TRACK_OUT_OF_RANGE, "track 0 rejected");
    failures += expect_result(d64_track_sector_offset(36, 0, &offset), D64_TRACK_OUT_OF_RANGE, "track 36 rejected");
    failures += expect_result(d64_track_sector_offset(35, 17, &offset), D64_SECTOR_OUT_OF_RANGE, "track 35 sector 17 rejected");

    return failures;
}

static int test_blank_fixture(void)
{
    int failures = 0;
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;
    d64_result result;
    d64_image *image;

    snprintf(path, sizeof(path), "%s/assets/disks/blank.d64", C64M_SOURCE_DIR);
    if (read_file(path, &bytes, &size) != 0) {
        return 1;
    }

    image = d64_image_create(bytes, size, &result);
    failures += expect_result(result, D64_OK, "blank image parse");
    failures += expect_true(image != NULL, "blank image allocated");
    if (image != NULL) {
        failures += expect_size(d64_image_directory_count(image), 0, "blank visible files");
    }

    d64_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_odell_fixture(void)
{
    static const char *expected_names[] = {
        "ODELL LAKE",
        "MENU1",
        "LAKESPT.BIN",
        "LAKE1.HRS",
        "LAKESTR.TXT",
        "GENINFO.TXT",
        "SSTACK.OBJ",
        "LAKE1.FNT",
        "LOGOMOVE.OBJ",
        "LOGO.SPT",
        "SCP3.OBJ",
        "INPUT3.OBJ",
        "LAKE1.TEX",
        "LAKE1.COL",
        "FISHDXY.TXT",
        "STRFLP.OBJ",
        "LAKESPTPNT.BIN",
        "MHTNT.OBJ",
        "MSIRQ.OBJ",
        "REMROS.OBJ",
        "LAKESPTCOL.BIN",
        "LAKERASDAT.BIN",
        "SCNUM.OBJ",
        "LAKESPTREC.BIN",
        "SPTEAT.OBJ",
        "HOOK.SPT",
        "NET.SPT",
        "DISPATCH.OBJ",
        "TTFER.OBJ"
    };
    int failures = 0;
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;
    d64_result result;
    d64_image *image;
    size_t i;
    d64_directory_entry entry;
    d64_file_data file;
    char name[D64_DIRECTORY_NAME_SIZE + 1];
    const d64_disk_info *info;
    d64_directory_entry title_entry;

    snprintf(path, sizeof(path), "%s/assets/disks/ODELLLAK.D64", C64M_SOURCE_DIR);
    if (read_file(path, &bytes, &size) != 0) {
        return 1;
    }

    image = d64_image_create(bytes, size, &result);
    failures += expect_result(result, D64_OK, "ODELL image parse");
    failures += expect_true(image != NULL, "ODELL image allocated");
    if (image != NULL) {
        info = d64_image_disk_info(image);
        memset(&title_entry, 0, sizeof(title_entry));
        memcpy(title_entry.filename, info->title, D64_DIRECTORY_NAME_SIZE);
        title_entry.filename_length = info->title_length;
        failures += expect_result(
            d64_entry_name_ascii(&title_entry, name, sizeof(name)),
            D64_OK,
            "ODELL title ascii");
        failures += expect_string(name, "ASS PRESENTS:", "ODELL disk title");
        failures += expect_size(
            d64_image_directory_count(image),
            sizeof(expected_names) / sizeof(expected_names[0]),
            "ODELL directory count");

        for (i = 0; i < sizeof(expected_names) / sizeof(expected_names[0]); ++i) {
            failures += expect_result(
                d64_image_find_entry_ascii(image, expected_names[i], &entry),
                D64_OK,
                expected_names[i]);
        }

        failures += expect_result(d64_image_find_entry_ascii(image, "MENU1", &entry), D64_OK, "find MENU1");
        failures += expect_result(d64_entry_name_ascii(&entry, name, sizeof(name)), D64_OK, "MENU1 name ascii");
        failures += expect_string(name, "MENU1", "MENU1 name");
        if (entry.type != D64_FILE_PRG) {
            fprintf(stderr, "MENU1: expected PRG, got %s\n", d64_file_type_string(entry.type));
            failures++;
        }
        memset(&file, 0, sizeof(file));
        failures += expect_result(d64_image_extract_prg(image, &entry, &file), D64_OK, "extract MENU1");
        failures += expect_true(file.size >= 2, "MENU1 has load address");
        d64_file_data_free(&file);

        failures += expect_result(
            d64_image_find_entry_ascii(image, "LAKESTR.TXT", &entry),
            D64_OK,
            "find SEQ");
        if (entry.type != D64_FILE_SEQ) {
            fprintf(stderr, "LAKESTR.TXT: expected SEQ, got %s\n", d64_file_type_string(entry.type));
            failures++;
        }
        memset(&file, 0, sizeof(file));
        failures += expect_result(
            d64_image_extract_prg(image, &entry, &file),
            D64_UNSUPPORTED_FILE_TYPE,
            "reject SEQ extraction");
    }

    d64_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_generated_minimal_prg(void)
{
    int failures = 0;
    uint8_t *bytes;
    d64_result result;
    d64_image *image;
    d64_directory_entry entry;
    d64_file_data file;
    uint8_t prg[] = {0x01, 0x08, 0xaa, 0xbb, 0xcc};

    bytes = make_empty_image();
    if (bytes == NULL) {
        return 1;
    }
    put_directory_entry(bytes, 0, 0x82, "HELLO", 1, 0, 1);
    put_final_file_sector(bytes, 1, 0, prg, sizeof(prg));

    image = d64_image_create(bytes, D64_STANDARD_IMAGE_SIZE, &result);
    failures += expect_result(result, D64_OK, "minimal image parse");
    failures += expect_true(image != NULL, "minimal image allocated");
    if (image != NULL) {
        failures += expect_size(d64_image_directory_count(image), 1, "minimal directory count");
        failures += expect_result(d64_image_find_entry_ascii(image, "hello", &entry), D64_OK, "case-fold find");
        memset(&file, 0, sizeof(file));
        failures += expect_result(d64_image_extract_prg(image, &entry, &file), D64_OK, "minimal PRG extract");
        failures += expect_size(file.size, sizeof(prg), "minimal PRG size");
        if (file.size == sizeof(prg) && memcmp(file.bytes, prg, sizeof(prg)) != 0) {
            fprintf(stderr, "minimal PRG bytes mismatch\n");
            failures++;
        }
        d64_file_data_free(&file);
    }

    d64_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_malformed_directory_pointer(void)
{
    int failures = 0;
    uint8_t *bytes;
    d64_result result;
    d64_image *image;
    size_t offset;

    bytes = make_empty_image();
    if (bytes == NULL) {
        return 1;
    }
    if (d64_track_sector_offset(18, 1, &offset) == D64_OK) {
        bytes[offset] = 35;
        bytes[offset + 1] = 17;
    }

    image = d64_image_create(bytes, D64_STANDARD_IMAGE_SIZE, &result);
    failures += expect_result(result, D64_MALFORMED_DIRECTORY, "bad directory pointer");
    failures += expect_true(image == NULL, "bad directory image rejected");

    d64_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_directory_loop(void)
{
    int failures = 0;
    uint8_t *bytes;
    d64_result result;
    d64_image *image;
    size_t offset;

    bytes = make_empty_image();
    if (bytes == NULL) {
        return 1;
    }
    if (d64_track_sector_offset(18, 1, &offset) == D64_OK) {
        bytes[offset] = 18;
        bytes[offset + 1] = 1;
    }

    image = d64_image_create(bytes, D64_STANDARD_IMAGE_SIZE, &result);
    failures += expect_result(result, D64_DIRECTORY_CHAIN_LOOP, "directory loop");
    failures += expect_true(image == NULL, "directory loop image rejected");

    d64_image_destroy(image);
    free(bytes);
    return failures;
}

static int test_file_chain_loop_and_bad_sector(void)
{
    int failures = 0;
    uint8_t *bytes;
    d64_result result;
    d64_image *image;
    d64_directory_entry entry;
    d64_file_data file;
    size_t offset;

    bytes = make_empty_image();
    if (bytes == NULL) {
        return 1;
    }
    put_directory_entry(bytes, 0, 0x82, "LOOP", 1, 0, 1);
    if (d64_track_sector_offset(1, 0, &offset) == D64_OK) {
        bytes[offset] = 1;
        bytes[offset + 1] = 0;
    }
    image = d64_image_create(bytes, D64_STANDARD_IMAGE_SIZE, &result);
    failures += expect_result(result, D64_OK, "loop file image parse");
    if (image != NULL) {
        failures += expect_result(d64_image_find_entry_ascii(image, "LOOP", &entry), D64_OK, "find loop file");
        memset(&file, 0, sizeof(file));
        failures += expect_result(d64_image_extract_prg(image, &entry, &file), D64_FILE_CHAIN_LOOP, "file loop rejected");
    }
    d64_image_destroy(image);
    free(bytes);

    bytes = make_empty_image();
    if (bytes == NULL) {
        return failures + 1;
    }
    put_directory_entry(bytes, 0, 0x82, "BADSECTOR", 35, 17, 1);
    image = d64_image_create(bytes, D64_STANDARD_IMAGE_SIZE, &result);
    failures += expect_result(result, D64_OK, "bad sector file image parse");
    if (image != NULL) {
        failures += expect_result(d64_image_find_entry_ascii(image, "BADSECTOR", &entry), D64_OK, "find bad sector file");
        memset(&file, 0, sizeof(file));
        failures += expect_result(d64_image_extract_prg(image, &entry, &file), D64_MALFORMED_FILE, "bad sector rejected");
    }
    d64_image_destroy(image);
    free(bytes);

    return failures;
}

static int test_short_prg(void)
{
    int failures = 0;
    uint8_t *bytes;
    d64_result result;
    d64_image *image;
    d64_directory_entry entry;
    d64_file_data file;
    uint8_t short_prg[] = {0x01};

    bytes = make_empty_image();
    if (bytes == NULL) {
        return 1;
    }
    put_directory_entry(bytes, 0, 0x82, "SHORT", 1, 0, 1);
    put_final_file_sector(bytes, 1, 0, short_prg, sizeof(short_prg));

    image = d64_image_create(bytes, D64_STANDARD_IMAGE_SIZE, &result);
    failures += expect_result(result, D64_OK, "short PRG image parse");
    if (image != NULL) {
        failures += expect_result(d64_image_find_entry_ascii(image, "SHORT", &entry), D64_OK, "find short PRG");
        memset(&file, 0, sizeof(file));
        failures += expect_result(d64_image_extract_prg(image, &entry, &file), D64_PRG_TOO_SHORT, "short PRG rejected");
    }

    d64_image_destroy(image);
    free(bytes);
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_geometry_and_size();
    failures += test_blank_fixture();
    failures += test_odell_fixture();
    failures += test_generated_minimal_prg();
    failures += test_malformed_directory_pointer();
    failures += test_directory_loop();
    failures += test_file_chain_loop_and_bad_sector();
    failures += test_short_prg();

    return failures == 0 ? 0 : 1;
}
