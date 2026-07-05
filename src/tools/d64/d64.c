#include "d64.h"

#include <stdlib.h>
#include <string.h>

#define D64_SECTOR_COUNT 683u
#define D64_DIRECTORY_TRACK 18u
#define D64_DIRECTORY_SECTOR 1u
#define D64_BAM_TRACK 18u
#define D64_BAM_SECTOR 0u
#define D64_FILE_INTERLEAVE 10u
#define D64_DIRECTORY_INTERLEAVE 3u

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
    case D64_FILE_EXISTS:
        return "file exists";
    case D64_PRG_TOO_SHORT:
        return "prg too short";
    case D64_DISK_FULL:
        return "disk full";
    case D64_DIRECTORY_FULL:
        return "directory full";
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

static uint8_t d64_ascii_to_petscii(uint8_t value)
{
    if (value >= 'a' && value <= 'z') {
        return (uint8_t)(value - ('a' - 'A'));
    }
    return value;
}

static bool d64_sector_index(uint8_t track, uint8_t sector, size_t *out_index)
{
    size_t offset;

    if (d64_track_sector_offset(track, sector, &offset) != D64_OK) {
        return false;
    }
    if (out_index != NULL) {
        *out_index = offset / D64_SECTOR_SIZE;
    }
    return true;
}

static uint8_t *d64_sector_ptr(d64_image *image, uint8_t track, uint8_t sector)
{
    size_t offset;

    if (image == NULL || d64_track_sector_offset(track, sector, &offset) != D64_OK) {
        return NULL;
    }
    return &image->bytes[offset];
}

static const uint8_t *d64_const_sector_ptr(
    const d64_image *image,
    uint8_t track,
    uint8_t sector)
{
    size_t offset;

    if (image == NULL || d64_track_sector_offset(track, sector, &offset) != D64_OK) {
        return NULL;
    }
    return &image->bytes[offset];
}

static void d64_clear_directory_cache(d64_image *image)
{
    if (image == NULL) {
        return;
    }
    free(image->entries);
    image->entries = NULL;
    image->entry_count = 0;
    image->entry_capacity = 0;
    memset(&image->info, 0, sizeof(image->info));
}

static d64_result d64_reparse(d64_image *image)
{
    d64_result result;

    if (image == NULL) {
        return D64_INVALID_ARGUMENT;
    }

    d64_clear_directory_cache(image);
    d64_parse_disk_info(image);
    result = d64_parse_directory(image);
    if (result != D64_OK) {
        d64_clear_directory_cache(image);
    }
    return result;
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

const uint8_t *d64_image_bytes(const d64_image *image, size_t *out_size)
{
    if (image == NULL) {
        if (out_size != NULL) {
            *out_size = 0;
        }
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = D64_STANDARD_IMAGE_SIZE;
    }
    return image->bytes;
}

static uint8_t *d64_bam_entry(d64_image *image, uint8_t track)
{
    uint8_t *bam;

    if (image == NULL || track < 1 || track > D64_TRACK_COUNT) {
        return NULL;
    }
    bam = d64_sector_ptr(image, D64_BAM_TRACK, D64_BAM_SECTOR);
    return bam == NULL ? NULL : &bam[4u + ((track - 1u) * 4u)];
}

static bool d64_bam_sector_free(const d64_image *image, uint8_t track, uint8_t sector)
{
    const uint8_t *bam;
    const uint8_t *entry;
    uint8_t mask;

    if (image == NULL || track < 1 || track > D64_TRACK_COUNT ||
        sector >= d64_sectors_per_track[track - 1u]) {
        return false;
    }
    bam = d64_const_sector_ptr(image, D64_BAM_TRACK, D64_BAM_SECTOR);
    if (bam == NULL) {
        return false;
    }
    entry = &bam[4u + ((track - 1u) * 4u)];
    mask = (uint8_t)(1u << (sector & 7u));
    return (entry[1u + (sector >> 3u)] & mask) != 0;
}

static d64_result d64_bam_mark_sector(
    d64_image *image,
    uint8_t track,
    uint8_t sector,
    bool free_sector)
{
    uint8_t *entry;
    uint8_t mask;
    uint8_t *byte;
    bool is_free;

    if (track < 1 || track > D64_TRACK_COUNT) {
        return D64_TRACK_OUT_OF_RANGE;
    }
    if (sector >= d64_sectors_per_track[track - 1u]) {
        return D64_SECTOR_OUT_OF_RANGE;
    }

    entry = d64_bam_entry(image, track);
    if (entry == NULL) {
        return D64_MALFORMED_DIRECTORY;
    }
    mask = (uint8_t)(1u << (sector & 7u));
    byte = &entry[1u + (sector >> 3u)];
    is_free = (*byte & mask) != 0;
    if (is_free == free_sector) {
        return D64_OK;
    }

    if (free_sector) {
        *byte |= mask;
        if (entry[0] < d64_sectors_per_track[track - 1u]) {
            entry[0]++;
        }
    } else {
        *byte &= (uint8_t)~mask;
        if (entry[0] > 0) {
            entry[0]--;
        }
    }
    return D64_OK;
}

static d64_result d64_find_next_free_on_track(
    const d64_image *image,
    uint8_t track,
    uint8_t preferred,
    uint8_t *out_sector)
{
    uint8_t sectors;
    uint8_t i;

    if (image == NULL || out_sector == NULL) {
        return D64_INVALID_ARGUMENT;
    }
    if (track < 1 || track > D64_TRACK_COUNT) {
        return D64_TRACK_OUT_OF_RANGE;
    }
    sectors = d64_sectors_per_track[track - 1u];
    for (i = 0; i < sectors; ++i) {
        uint8_t sector = (uint8_t)((preferred + i) % sectors);
        if (d64_bam_sector_free(image, track, sector)) {
            *out_sector = sector;
            return D64_OK;
        }
    }
    return D64_DISK_FULL;
}

static d64_result d64_alloc_sector(
    d64_image *image,
    uint8_t preferred_track,
    uint8_t preferred_sector,
    bool allow_directory_track,
    uint8_t *out_track,
    uint8_t *out_sector)
{
    uint8_t pass;

    if (image == NULL || out_track == NULL || out_sector == NULL) {
        return D64_INVALID_ARGUMENT;
    }

    for (pass = 0; pass < 2; ++pass) {
        uint8_t start = pass == 0 && preferred_track >= 1 && preferred_track <= D64_TRACK_COUNT ?
            preferred_track : 1u;
        uint8_t offset;

        for (offset = 0; offset < D64_TRACK_COUNT; ++offset) {
            uint8_t track = (uint8_t)(((start - 1u + offset) % D64_TRACK_COUNT) + 1u);
            uint8_t sector;

            if (!allow_directory_track && track == D64_DIRECTORY_TRACK) {
                continue;
            }
            if (d64_find_next_free_on_track(
                    image,
                    track,
                    track == preferred_track ? preferred_sector : 0,
                    &sector) == D64_OK) {
                d64_result result = d64_bam_mark_sector(image, track, sector, false);
                if (result != D64_OK) {
                    return result;
                }
                *out_track = track;
                *out_sector = sector;
                return D64_OK;
            }
        }
    }

    return D64_DISK_FULL;
}

static d64_result d64_free_file_chain(d64_image *image, uint8_t track, uint8_t sector_id)
{
    bool visited[D64_SECTOR_COUNT];

    memset(visited, 0, sizeof(visited));
    while (track != 0) {
        uint8_t *sector;
        uint8_t next_track;
        uint8_t next_sector;
        size_t index;
        d64_result result;

        if (!d64_sector_index(track, sector_id, &index)) {
            return D64_MALFORMED_FILE;
        }
        if (visited[index]) {
            return D64_FILE_CHAIN_LOOP;
        }
        visited[index] = true;

        sector = d64_sector_ptr(image, track, sector_id);
        if (sector == NULL) {
            return D64_MALFORMED_FILE;
        }
        next_track = sector[0];
        next_sector = sector[1];
        result = d64_bam_mark_sector(image, track, sector_id, true);
        if (result != D64_OK) {
            return result;
        }
        memset(sector, 0, D64_SECTOR_SIZE);
        track = next_track;
        sector_id = next_sector;
    }
    return D64_OK;
}

static bool d64_names_equal(
    const uint8_t *left,
    size_t left_len,
    const uint8_t *right,
    size_t right_len)
{
    size_t i;

    if (left_len != right_len) {
        return false;
    }
    for (i = 0; i < left_len; ++i) {
        uint8_t l = left[i];
        uint8_t r = right[i];
        if (l >= 'a' && l <= 'z') {
            l = (uint8_t)(l - ('a' - 'A'));
        }
        if (r >= 'a' && r <= 'z') {
            r = (uint8_t)(r - ('a' - 'A'));
        }
        if (l != r) {
            return false;
        }
    }
    return true;
}

static d64_result d64_normalize_name(
    const uint8_t *name,
    size_t name_len,
    uint8_t out_name[D64_DIRECTORY_NAME_SIZE],
    size_t *out_name_len,
    bool *out_replace)
{
    size_t i;
    bool replace = false;

    if (name == NULL || out_name == NULL || out_name_len == NULL || out_replace == NULL) {
        return D64_INVALID_ARGUMENT;
    }
    while (name_len >= 2 && name[0] == '"' && name[name_len - 1u] == '"') {
        name++;
        name_len -= 2u;
    }
    if (name_len >= 2 && name[0] == '@' && name[1] == ':') {
        replace = true;
        name += 2;
        name_len -= 2u;
    }
    while (name_len > 0 && (name[name_len - 1u] == ' ' || name[name_len - 1u] == 0xa0)) {
        name_len--;
    }
    if (name_len == 0 || name_len > D64_DIRECTORY_NAME_SIZE) {
        return D64_INVALID_ARGUMENT;
    }

    memset(out_name, 0xa0, D64_DIRECTORY_NAME_SIZE);
    for (i = 0; i < name_len; ++i) {
        out_name[i] = d64_ascii_to_petscii(name[i]);
    }
    *out_name_len = name_len;
    *out_replace = replace;
    return D64_OK;
}

static d64_result d64_find_directory_slot(
    d64_image *image,
    const uint8_t *name,
    size_t name_len,
    uint8_t **out_slot,
    d64_directory_entry *out_existing)
{
    bool visited[D64_SECTOR_COUNT];
    uint8_t track = D64_DIRECTORY_TRACK;
    uint8_t sector_id = D64_DIRECTORY_SECTOR;

    memset(visited, 0, sizeof(visited));
    while (track != 0) {
        uint8_t *sector;
        size_t index;
        size_t slot;

        if (!d64_sector_index(track, sector_id, &index)) {
            return D64_MALFORMED_DIRECTORY;
        }
        if (visited[index]) {
            return D64_DIRECTORY_CHAIN_LOOP;
        }
        visited[index] = true;

        sector = d64_sector_ptr(image, track, sector_id);
        if (sector == NULL) {
            return D64_MALFORMED_DIRECTORY;
        }

        for (slot = 0; slot < 8; ++slot) {
            uint8_t *entry = &sector[slot * 32u];
            uint8_t raw_type = entry[2];

            if ((raw_type & 0x07u) != 0 &&
                d64_names_equal(&entry[5], d64_trim_petscii_name(&entry[5]), name, name_len)) {
                if (out_existing != NULL) {
                    memset(out_existing, 0, sizeof(*out_existing));
                    out_existing->raw_type = raw_type;
                    out_existing->type = d64_decode_file_type(raw_type);
                    out_existing->closed = (raw_type & 0x80u) != 0;
                    out_existing->locked = (raw_type & 0x40u) != 0;
                    out_existing->first_track = entry[3];
                    out_existing->first_sector = entry[4];
                    memcpy(out_existing->filename, &entry[5], D64_DIRECTORY_NAME_SIZE);
                    out_existing->filename_length = d64_trim_petscii_name(out_existing->filename);
                    out_existing->block_count =
                        (uint16_t)entry[30] | ((uint16_t)entry[31] << 8);
                }
                if (out_slot != NULL) {
                    *out_slot = entry;
                }
                return D64_FILE_EXISTS;
            }
        }

        track = sector[0];
        sector_id = sector[1];
    }

    return D64_FILE_NOT_FOUND;
}

static d64_result d64_find_free_directory_slot(d64_image *image, uint8_t **out_slot)
{
    bool visited[D64_SECTOR_COUNT];
    uint8_t track = D64_DIRECTORY_TRACK;
    uint8_t sector_id = D64_DIRECTORY_SECTOR;
    uint8_t *last_sector = NULL;

    if (image == NULL || out_slot == NULL) {
        return D64_INVALID_ARGUMENT;
    }

    memset(visited, 0, sizeof(visited));
    while (track != 0) {
        uint8_t *sector;
        size_t index;
        size_t slot;

        if (!d64_sector_index(track, sector_id, &index)) {
            return D64_MALFORMED_DIRECTORY;
        }
        if (visited[index]) {
            return D64_DIRECTORY_CHAIN_LOOP;
        }
        visited[index] = true;

        sector = d64_sector_ptr(image, track, sector_id);
        if (sector == NULL) {
            return D64_MALFORMED_DIRECTORY;
        }
        for (slot = 0; slot < 8; ++slot) {
            uint8_t *entry = &sector[slot * 32u];
            if ((entry[2] & 0x07u) == 0 || entry[2] == 0) {
                *out_slot = entry;
                return D64_OK;
            }
        }

        last_sector = sector;
        track = sector[0];
        sector_id = sector[1];
    }

    if (last_sector != NULL) {
        uint8_t new_sector_id;
        d64_result result;
        uint8_t *new_sector;

        result = d64_find_next_free_on_track(
            image,
            D64_DIRECTORY_TRACK,
            (uint8_t)((last_sector[1] + D64_DIRECTORY_INTERLEAVE) %
                d64_sectors_per_track[D64_DIRECTORY_TRACK - 1u]),
            &new_sector_id);
        if (result != D64_OK) {
            return D64_DIRECTORY_FULL;
        }
        result = d64_bam_mark_sector(image, D64_DIRECTORY_TRACK, new_sector_id, false);
        if (result != D64_OK) {
            return result;
        }
        last_sector[0] = D64_DIRECTORY_TRACK;
        last_sector[1] = new_sector_id;
        new_sector = d64_sector_ptr(image, D64_DIRECTORY_TRACK, new_sector_id);
        if (new_sector == NULL) {
            return D64_MALFORMED_DIRECTORY;
        }
        memset(new_sector, 0, D64_SECTOR_SIZE);
        new_sector[0] = 0;
        new_sector[1] = 0xff;
        *out_slot = new_sector;
        return D64_OK;
    }

    return D64_DIRECTORY_FULL;
}

static d64_result d64_write_file_chain(
    d64_image *image,
    const uint8_t *data,
    size_t data_len,
    uint8_t *out_first_track,
    uint8_t *out_first_sector,
    uint16_t *out_blocks)
{
    uint8_t *tracks;
    uint8_t *sectors;
    size_t blocks;
    size_t i;
    size_t data_offset = 0;
    uint8_t preferred_track = 1;
    uint8_t preferred_sector = 0;

    if (data_len < 2) {
        return D64_PRG_TOO_SHORT;
    }
    blocks = (data_len + (D64_SECTOR_SIZE - 3u)) / (D64_SECTOR_SIZE - 2u);
    if (blocks == 0 || blocks > 65535u) {
        return D64_DISK_FULL;
    }

    tracks = (uint8_t *)calloc(blocks, sizeof(*tracks));
    sectors = (uint8_t *)calloc(blocks, sizeof(*sectors));
    if (tracks == NULL || sectors == NULL) {
        free(tracks);
        free(sectors);
        return D64_OUT_OF_MEMORY;
    }

    for (i = 0; i < blocks; ++i) {
        d64_result result = d64_alloc_sector(
            image,
            preferred_track,
            preferred_sector,
            false,
            &tracks[i],
            &sectors[i]);
        if (result != D64_OK) {
            free(tracks);
            free(sectors);
            return result;
        }
        preferred_track = tracks[i];
        preferred_sector = (uint8_t)(
            (sectors[i] + D64_FILE_INTERLEAVE) %
            d64_sectors_per_track[preferred_track - 1u]);
    }

    for (i = 0; i < blocks; ++i) {
        uint8_t *sector = d64_sector_ptr(image, tracks[i], sectors[i]);
        size_t chunk = data_len - data_offset;

        if (sector == NULL) {
            free(tracks);
            free(sectors);
            return D64_MALFORMED_FILE;
        }
        if (chunk > D64_SECTOR_SIZE - 2u) {
            chunk = D64_SECTOR_SIZE - 2u;
        }
        memset(sector, 0, D64_SECTOR_SIZE);
        if (i + 1u < blocks) {
            sector[0] = tracks[i + 1u];
            sector[1] = sectors[i + 1u];
        } else {
            sector[0] = 0;
            sector[1] = (uint8_t)(chunk + 1u);
        }
        memcpy(&sector[2], &data[data_offset], chunk);
        data_offset += chunk;
    }

    *out_first_track = tracks[0];
    *out_first_sector = sectors[0];
    *out_blocks = (uint16_t)blocks;
    free(tracks);
    free(sectors);
    return D64_OK;
}

static void d64_clear_directory_entry_slot(d64_image *image, uint8_t *slot)
{
    size_t offset;

    if (image == NULL || slot == NULL) {
        return;
    }

    offset = (size_t)(slot - image->bytes);
    if (offset < D64_STANDARD_IMAGE_SIZE && (offset % D64_SECTOR_SIZE) == 0) {
        memset(&slot[2], 0, 30);
    } else {
        memset(slot, 0, 32);
    }
}

d64_result d64_image_write_prg(
    d64_image *image,
    const uint8_t *name,
    size_t name_len,
    const uint8_t *data,
    size_t data_len,
    bool replace)
{
    uint8_t normalized[D64_DIRECTORY_NAME_SIZE];
    size_t normalized_len = 0;
    bool name_replace = false;
    uint8_t *backup;
    uint8_t *dir_slot = NULL;
    d64_directory_entry existing;
    uint8_t first_track = 0;
    uint8_t first_sector = 0;
    uint16_t blocks = 0;
    d64_result result;

    if (image == NULL || name == NULL || data == NULL) {
        return D64_INVALID_ARGUMENT;
    }

    result = d64_normalize_name(
        name,
        name_len,
        normalized,
        &normalized_len,
        &name_replace);
    if (result != D64_OK) {
        return result;
    }
    replace = replace || name_replace;

    backup = (uint8_t *)malloc(D64_STANDARD_IMAGE_SIZE);
    if (backup == NULL) {
        return D64_OUT_OF_MEMORY;
    }
    memcpy(backup, image->bytes, D64_STANDARD_IMAGE_SIZE);

    result = d64_find_directory_slot(image, normalized, normalized_len, &dir_slot, &existing);
    if (result == D64_FILE_EXISTS) {
        if (!replace) {
            result = D64_FILE_EXISTS;
            goto rollback;
        }
        result = d64_free_file_chain(image, existing.first_track, existing.first_sector);
        if (result != D64_OK) {
            goto rollback;
        }
        d64_clear_directory_entry_slot(image, dir_slot);
    } else if (result != D64_FILE_NOT_FOUND) {
        goto rollback;
    }

    result = d64_write_file_chain(
        image,
        data,
        data_len,
        &first_track,
        &first_sector,
        &blocks);
    if (result != D64_OK) {
        goto rollback;
    }

    result = d64_find_free_directory_slot(image, &dir_slot);
    if (result != D64_OK) {
        goto rollback;
    }
    d64_clear_directory_entry_slot(image, dir_slot);
    dir_slot[2] = 0x82;
    dir_slot[3] = first_track;
    dir_slot[4] = first_sector;
    memcpy(&dir_slot[5], normalized, D64_DIRECTORY_NAME_SIZE);
    dir_slot[30] = (uint8_t)(blocks & 0xffu);
    dir_slot[31] = (uint8_t)(blocks >> 8);

    result = d64_reparse(image);
    if (result != D64_OK) {
        goto rollback;
    }
    free(backup);
    return D64_OK;

rollback:
    memcpy(image->bytes, backup, D64_STANDARD_IMAGE_SIZE);
    free(backup);
    (void)d64_reparse(image);
    return result;
}
