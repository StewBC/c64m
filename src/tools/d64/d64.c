#include "d64.h"

#include <stdlib.h>
#include <string.h>

#define D64_SECTOR_COUNT 683u
#define D64_DIRECTORY_TRACK 18u
#define D64_DIRECTORY_SECTOR 1u
#define D64_BAM_TRACK 18u
#define D64_BAM_SECTOR 0u

struct d64_image {
    uint8_t bytes[D64_STANDARD_IMAGE_SIZE];
    d64_disk_info info;
    d64_directory_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
};

static const uint8_t d64_sectors_per_track[D64_TRACK_COUNT] = {
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    19, 19, 19, 19, 19, 19, 19,
    18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 17
};

const char *d64_result_string(d64_result result)
{
    switch (result) {
    case D64_OK:
        return "ok";
    case D64_INVALID_ARGUMENT:
        return "invalid argument";
    case D64_UNSUPPORTED_IMAGE:
        return "unsupported image";
    case D64_TRACK_OUT_OF_RANGE:
        return "track out of range";
    case D64_SECTOR_OUT_OF_RANGE:
        return "sector out of range";
    case D64_DIRECTORY_CHAIN_LOOP:
        return "directory chain loop";
    case D64_FILE_CHAIN_LOOP:
        return "file chain loop";
    case D64_MALFORMED_DIRECTORY:
        return "malformed directory";
    case D64_MALFORMED_FILE:
        return "malformed file";
    case D64_UNSUPPORTED_FILE_TYPE:
        return "unsupported file type";
    case D64_FILE_NOT_FOUND:
        return "file not found";
    case D64_PRG_TOO_SHORT:
        return "prg too short";
    case D64_OUT_OF_MEMORY:
        return "out of memory";
    default:
        return "unknown";
    }
}

const char *d64_file_type_string(d64_file_type type)
{
    switch (type) {
    case D64_FILE_DEL:
        return "DEL";
    case D64_FILE_SEQ:
        return "SEQ";
    case D64_FILE_PRG:
        return "PRG";
    case D64_FILE_USR:
        return "USR";
    case D64_FILE_REL:
        return "REL";
    case D64_FILE_UNKNOWN:
    default:
        return "???";
    }
}

bool d64_image_size_supported(size_t size)
{
    return size == D64_STANDARD_IMAGE_SIZE || size == D64_ERROR_INFO_IMAGE_SIZE;
}

d64_result d64_track_sector_offset(uint8_t track, uint8_t sector, size_t *out_offset)
{
    size_t offset;
    uint8_t current_track;

    if (out_offset == NULL) {
        return D64_INVALID_ARGUMENT;
    }
    if (track < 1 || track > D64_TRACK_COUNT) {
        return D64_TRACK_OUT_OF_RANGE;
    }
    if (sector >= d64_sectors_per_track[track - 1]) {
        return D64_SECTOR_OUT_OF_RANGE;
    }

    offset = 0;
    for (current_track = 1; current_track < track; ++current_track) {
        offset += (size_t)d64_sectors_per_track[current_track - 1] * D64_SECTOR_SIZE;
    }
    offset += (size_t)sector * D64_SECTOR_SIZE;
    if (offset + D64_SECTOR_SIZE > D64_STANDARD_IMAGE_SIZE) {
        return D64_SECTOR_OUT_OF_RANGE;
    }

    *out_offset = offset;
    return D64_OK;
}

static d64_file_type d64_decode_file_type(uint8_t raw_type)
{
    switch (raw_type & 0x07u) {
    case 0:
        return D64_FILE_DEL;
    case 1:
        return D64_FILE_SEQ;
    case 2:
        return D64_FILE_PRG;
    case 3:
        return D64_FILE_USR;
    case 4:
        return D64_FILE_REL;
    default:
        return D64_FILE_UNKNOWN;
    }
}

static size_t d64_trim_petscii_name(const uint8_t *name)
{
    size_t length;

    length = D64_DIRECTORY_NAME_SIZE;
    while (length > 0 && (name[length - 1] == 0xa0 || name[length - 1] == 0x20)) {
        --length;
    }
    return length;
}

static void d64_parse_disk_info(d64_image *image)
{
    const uint8_t *bam;
    size_t offset;
    uint8_t track;

    memset(&image->info, 0, sizeof(image->info));
    if (d64_track_sector_offset(D64_BAM_TRACK, D64_BAM_SECTOR, &offset) != D64_OK) {
        return;
    }

    bam = &image->bytes[offset];
    memcpy(image->info.title, &bam[0x90], D64_DIRECTORY_NAME_SIZE);
    image->info.title_length = d64_trim_petscii_name(image->info.title);
    image->info.disk_id[0] = bam[0xa2];
    image->info.disk_id[1] = bam[0xa3];
    image->info.dos_type[0] = bam[0xa5];
    image->info.dos_type[1] = bam[0xa6];

    for (track = 1; track <= D64_TRACK_COUNT; ++track) {
        if (track != D64_DIRECTORY_TRACK) {
            image->info.free_blocks += bam[4 + ((track - 1) * 4)];
        }
    }
}

static d64_result d64_append_directory_entry(d64_image *image, const d64_directory_entry *entry)
{
    d64_directory_entry *entries;
    size_t new_capacity;

    if (image->entry_count == image->entry_capacity) {
        new_capacity = image->entry_capacity == 0 ? 16 : image->entry_capacity * 2;
        entries = (d64_directory_entry *)realloc(
            image->entries,
            new_capacity * sizeof(*image->entries));
        if (entries == NULL) {
            return D64_OUT_OF_MEMORY;
        }
        image->entries = entries;
        image->entry_capacity = new_capacity;
    }

    image->entries[image->entry_count++] = *entry;
    return D64_OK;
}

static d64_result d64_parse_directory_slot(
    d64_image *image,
    const uint8_t *sector,
    size_t slot)
{
    size_t base;
    uint8_t raw_type;
    d64_directory_entry entry;

    base = slot * 32u;
    raw_type = sector[base + 2u];
    if ((raw_type & 0x07u) == 0 || raw_type == 0) {
        return D64_OK;
    }

    memset(&entry, 0, sizeof(entry));
    entry.raw_type = raw_type;
    entry.type = d64_decode_file_type(raw_type);
    entry.closed = (raw_type & 0x80u) != 0;
    entry.locked = (raw_type & 0x40u) != 0;
    entry.first_track = sector[base + 3u];
    entry.first_sector = sector[base + 4u];
    memcpy(entry.filename, &sector[base + 5u], D64_DIRECTORY_NAME_SIZE);
    entry.filename_length = d64_trim_petscii_name(entry.filename);
    entry.block_count = (uint16_t)sector[base + 30u] | ((uint16_t)sector[base + 31u] << 8);

    return d64_append_directory_entry(image, &entry);
}

static d64_result d64_parse_directory(d64_image *image)
{
    bool visited[D64_SECTOR_COUNT];
    uint8_t track;
    uint8_t sector_id;
    size_t offset;
    size_t visited_index;
    size_t slot;
    d64_result result;
    const uint8_t *sector;

    memset(visited, 0, sizeof(visited));
    track = D64_DIRECTORY_TRACK;
    sector_id = D64_DIRECTORY_SECTOR;

    while (track != 0) {
        result = d64_track_sector_offset(track, sector_id, &offset);
        if (result != D64_OK) {
            return result == D64_SECTOR_OUT_OF_RANGE ? D64_MALFORMED_DIRECTORY : result;
        }

        visited_index = offset / D64_SECTOR_SIZE;
        if (visited_index >= D64_SECTOR_COUNT) {
            return D64_MALFORMED_DIRECTORY;
        }
        if (visited[visited_index]) {
            return D64_DIRECTORY_CHAIN_LOOP;
        }
        visited[visited_index] = true;

        sector = &image->bytes[offset];
        for (slot = 0; slot < 8; ++slot) {
            result = d64_parse_directory_slot(image, sector, slot);
            if (result != D64_OK) {
                return result;
            }
        }

        track = sector[0];
        sector_id = sector[1];
    }

    return D64_OK;
}

d64_image *d64_image_create(const uint8_t *bytes, size_t size, d64_result *out_result)
{
    d64_image *image;
    d64_result result;

    if (out_result != NULL) {
        *out_result = D64_OK;
    }
    if (bytes == NULL) {
        if (out_result != NULL) {
            *out_result = D64_INVALID_ARGUMENT;
        }
        return NULL;
    }
    if (!d64_image_size_supported(size)) {
        if (out_result != NULL) {
            *out_result = D64_UNSUPPORTED_IMAGE;
        }
        return NULL;
    }

    image = (d64_image *)calloc(1, sizeof(*image));
    if (image == NULL) {
        if (out_result != NULL) {
            *out_result = D64_OUT_OF_MEMORY;
        }
        return NULL;
    }

    memcpy(image->bytes, bytes, D64_STANDARD_IMAGE_SIZE);
    d64_parse_disk_info(image);
    result = d64_parse_directory(image);
    if (result != D64_OK) {
        d64_image_destroy(image);
        if (out_result != NULL) {
            *out_result = result;
        }
        return NULL;
    }

    if (out_result != NULL) {
        *out_result = D64_OK;
    }
    return image;
}

void d64_image_destroy(d64_image *image)
{
    if (image == NULL) {
        return;
    }

    free(image->entries);
    free(image);
}

const d64_disk_info *d64_image_disk_info(const d64_image *image)
{
    if (image == NULL) {
        return NULL;
    }
    return &image->info;
}

size_t d64_image_directory_count(const d64_image *image)
{
    return image == NULL ? 0 : image->entry_count;
}

d64_result d64_image_directory_entry(
    const d64_image *image,
    size_t index,
    d64_directory_entry *out_entry)
{
    if (image == NULL || out_entry == NULL) {
        return D64_INVALID_ARGUMENT;
    }
    if (index >= image->entry_count) {
        return D64_FILE_NOT_FOUND;
    }

    *out_entry = image->entries[index];
    return D64_OK;
}

static char d64_petscii_to_ascii(uint8_t value)
{
    if (value == 0xa0) {
        return '\0';
    }
    if (value >= 0x20 && value <= 0x7e) {
        return (char)value;
    }
    if (value >= 0xc1 && value <= 0xda) {
        return (char)('A' + (value - 0xc1));
    }
    return '?';
}

d64_result d64_entry_name_ascii(
    const d64_directory_entry *entry,
    char *out_name,
    size_t out_name_size)
{
    size_t i;
    size_t length;

    if (entry == NULL || out_name == NULL || out_name_size == 0) {
        return D64_INVALID_ARGUMENT;
    }

    length = entry->filename_length;
    if (length + 1 > out_name_size) {
        return D64_INVALID_ARGUMENT;
    }

    for (i = 0; i < length; ++i) {
        out_name[i] = d64_petscii_to_ascii(entry->filename[i]);
        if (out_name[i] == '\0') {
            break;
        }
    }
    out_name[i] = '\0';
    return D64_OK;
}

static char d64_ascii_upper(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - ('a' - 'A'));
    }
    return value;
}

static bool d64_ascii_equal_fold(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (d64_ascii_upper(*left) != d64_ascii_upper(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

d64_result d64_image_find_entry_ascii(
    const d64_image *image,
    const char *name,
    d64_directory_entry *out_entry)
{
    size_t i;
    char entry_name[D64_DIRECTORY_NAME_SIZE + 1];

    if (image == NULL || name == NULL || out_entry == NULL) {
        return D64_INVALID_ARGUMENT;
    }

    for (i = 0; i < image->entry_count; ++i) {
        if (d64_entry_name_ascii(&image->entries[i], entry_name, sizeof(entry_name)) != D64_OK) {
            continue;
        }
        if (d64_ascii_equal_fold(entry_name, name)) {
            *out_entry = image->entries[i];
            return D64_OK;
        }
    }

    return D64_FILE_NOT_FOUND;
}

static d64_result d64_file_append(
    d64_file_data *file,
    const uint8_t *bytes,
    size_t size)
{
    uint8_t *new_bytes;

    if (size == 0) {
        return D64_OK;
    }

    new_bytes = (uint8_t *)realloc(file->bytes, file->size + size);
    if (new_bytes == NULL) {
        return D64_OUT_OF_MEMORY;
    }

    memcpy(&new_bytes[file->size], bytes, size);
    file->bytes = new_bytes;
    file->size += size;
    return D64_OK;
}

d64_result d64_image_extract_prg(
    const d64_image *image,
    const d64_directory_entry *entry,
    d64_file_data *out_file)
{
    bool visited[D64_SECTOR_COUNT];
    uint8_t track;
    uint8_t sector_id;
    size_t offset;
    size_t visited_index;
    const uint8_t *sector;
    d64_result result;
    d64_file_data file;

    if (image == NULL || entry == NULL || out_file == NULL) {
        return D64_INVALID_ARGUMENT;
    }
    if (entry->type != D64_FILE_PRG) {
        return D64_UNSUPPORTED_FILE_TYPE;
    }
    if (entry->first_track == 0) {
        return D64_MALFORMED_FILE;
    }

    memset(&file, 0, sizeof(file));
    memset(visited, 0, sizeof(visited));
    track = entry->first_track;
    sector_id = entry->first_sector;

    while (track != 0) {
        result = d64_track_sector_offset(track, sector_id, &offset);
        if (result != D64_OK) {
            d64_file_data_free(&file);
            return result == D64_SECTOR_OUT_OF_RANGE ? D64_MALFORMED_FILE : result;
        }

        visited_index = offset / D64_SECTOR_SIZE;
        if (visited_index >= D64_SECTOR_COUNT) {
            d64_file_data_free(&file);
            return D64_MALFORMED_FILE;
        }
        if (visited[visited_index]) {
            d64_file_data_free(&file);
            return D64_FILE_CHAIN_LOOP;
        }
        visited[visited_index] = true;

        sector = &image->bytes[offset];
        if (sector[0] == 0) {
            size_t final_size;

            final_size = sector[1] <= 1 ? 0 : (size_t)sector[1] - 1u;
            if (final_size > D64_SECTOR_SIZE - 2u) {
                final_size = D64_SECTOR_SIZE - 2u;
            }
            result = d64_file_append(&file, &sector[2], final_size);
            if (result != D64_OK) {
                d64_file_data_free(&file);
                return result;
            }
            break;
        }

        result = d64_file_append(&file, &sector[2], D64_SECTOR_SIZE - 2u);
        if (result != D64_OK) {
            d64_file_data_free(&file);
            return result;
        }
        track = sector[0];
        sector_id = sector[1];
    }

    if (file.size < 2) {
        d64_file_data_free(&file);
        return D64_PRG_TOO_SHORT;
    }

    *out_file = file;
    return D64_OK;
}

void d64_file_data_free(d64_file_data *file)
{
    if (file == NULL) {
        return;
    }

    free(file->bytes);
    file->bytes = NULL;
    file->size = 0;
}
