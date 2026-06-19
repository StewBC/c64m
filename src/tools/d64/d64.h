#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define D64_STANDARD_IMAGE_SIZE 174848u
#define D64_ERROR_INFO_IMAGE_SIZE 175531u
#define D64_TRACK_COUNT 35u
#define D64_SECTOR_SIZE 256u
#define D64_DIRECTORY_NAME_SIZE 16u

typedef struct d64_image d64_image;

typedef enum d64_result {
    D64_OK = 0,
    D64_INVALID_ARGUMENT,
    D64_UNSUPPORTED_IMAGE,
    D64_TRACK_OUT_OF_RANGE,
    D64_SECTOR_OUT_OF_RANGE,
    D64_DIRECTORY_CHAIN_LOOP,
    D64_FILE_CHAIN_LOOP,
    D64_MALFORMED_DIRECTORY,
    D64_MALFORMED_FILE,
    D64_UNSUPPORTED_FILE_TYPE,
    D64_FILE_NOT_FOUND,
    D64_PRG_TOO_SHORT,
    D64_OUT_OF_MEMORY
} d64_result;

typedef enum d64_file_type {
    D64_FILE_DEL = 0,
    D64_FILE_SEQ = 1,
    D64_FILE_PRG = 2,
    D64_FILE_USR = 3,
    D64_FILE_REL = 4,
    D64_FILE_UNKNOWN = 255
} d64_file_type;

typedef struct d64_disk_info {
    uint8_t title[D64_DIRECTORY_NAME_SIZE];
    size_t title_length;
    uint8_t disk_id[2];
    uint8_t dos_type[2];
    uint16_t free_blocks;
} d64_disk_info;

typedef struct d64_directory_entry {
    uint8_t raw_type;
    d64_file_type type;
    bool closed;
    bool locked;
    uint8_t first_track;
    uint8_t first_sector;
    uint8_t filename[D64_DIRECTORY_NAME_SIZE];
    size_t filename_length;
    uint16_t block_count;
} d64_directory_entry;

typedef struct d64_file_data {
    uint8_t *bytes;
    size_t size;
} d64_file_data;

const char *d64_result_string(d64_result result);
const char *d64_file_type_string(d64_file_type type);

bool d64_image_size_supported(size_t size);
d64_result d64_track_sector_offset(uint8_t track, uint8_t sector, size_t *out_offset);

d64_image *d64_image_create(const uint8_t *bytes, size_t size, d64_result *out_result);
void d64_image_destroy(d64_image *image);

const d64_disk_info *d64_image_disk_info(const d64_image *image);
size_t d64_image_directory_count(const d64_image *image);
d64_result d64_image_directory_entry(
    const d64_image *image,
    size_t index,
    d64_directory_entry *out_entry);

d64_result d64_image_find_entry_ascii(
    const d64_image *image,
    const char *name,
    d64_directory_entry *out_entry);

d64_result d64_entry_name_ascii(
    const d64_directory_entry *entry,
    char *out_name,
    size_t out_name_size);

d64_result d64_image_extract_prg(
    const d64_image *image,
    const d64_directory_entry *entry,
    d64_file_data *out_file);

void d64_file_data_free(d64_file_data *file);
