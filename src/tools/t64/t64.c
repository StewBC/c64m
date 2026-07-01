#include "t64.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum {
    T64_SIGNATURE_SIZE = 32,
    T64_HEADER_SIZE = 64,
    T64_ENTRY_OFFSET = 64,
    T64_ENTRY_SIZE = 32,
    T64_ENTRY_TYPE_OFFSET = 0,
    T64_ENTRY_CBM_TYPE_OFFSET = 1,
    T64_ENTRY_START_OFFSET = 2,
    T64_ENTRY_END_OFFSET = 4,
    T64_ENTRY_DATA_OFFSET = 8
};

static uint16_t t64_read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t t64_read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static bool t64_has_signature(const uint8_t *bytes, size_t size) {
    static const char prefix[] = "C64";
    static const char tape[] = "tape";
    size_t i;

    if (bytes == NULL || size < T64_HEADER_SIZE) {
        return false;
    }

    if (memcmp(bytes, prefix, sizeof(prefix) - 1u) != 0) {
        return false;
    }

    for (i = 0; i + sizeof(tape) - 1u <= T64_SIGNATURE_SIZE; ++i) {
        if (memcmp(&bytes[i], tape, sizeof(tape) - 1u) == 0) {
            return true;
        }
    }

    return false;
}

static size_t t64_next_data_offset(
    const uint8_t *bytes,
    size_t size,
    size_t entry_count,
    uint32_t current_offset)
{
    size_t next_offset = size;
    size_t i;

    for (i = 0; i < entry_count; ++i) {
        const uint8_t *entry = &bytes[T64_ENTRY_OFFSET + (i * T64_ENTRY_SIZE)];
        uint8_t entry_type = entry[T64_ENTRY_TYPE_OFFSET];
        uint8_t cbm_type = entry[T64_ENTRY_CBM_TYPE_OFFSET];
        uint32_t data_offset = t64_read_le32(&entry[T64_ENTRY_DATA_OFFSET]);

        if (entry_type == 0 || cbm_type == 0 || data_offset <= current_offset) {
            continue;
        }
        if ((size_t)data_offset < next_offset) {
            next_offset = (size_t)data_offset;
        }
    }

    return next_offset;
}

const char *t64_result_string(t64_result result) {
    switch (result) {
    case T64_OK:
        return "ok";
    case T64_INVALID_ARGUMENT:
        return "invalid argument";
    case T64_UNSUPPORTED_IMAGE:
        return "unsupported image";
    case T64_MALFORMED_DIRECTORY:
        return "malformed directory";
    case T64_FILE_NOT_FOUND:
        return "file not found";
    case T64_ENTRY_RANGE_INVALID:
        return "entry range invalid";
    case T64_OUT_OF_MEMORY:
        return "out of memory";
    default:
        return "unknown error";
    }
}

t64_result t64_extract_first_prg(
    const uint8_t *bytes,
    size_t size,
    t64_file_data *out_file)
{
    uint16_t max_entries;
    uint16_t used_entries;
    size_t entry_count;
    size_t directory_end;
    size_t i;

    if (bytes == NULL || out_file == NULL) {
        return T64_INVALID_ARGUMENT;
    }

    out_file->bytes = NULL;
    out_file->size = 0;

    if (!t64_has_signature(bytes, size)) {
        return T64_UNSUPPORTED_IMAGE;
    }

    max_entries = t64_read_le16(&bytes[0x22]);
    used_entries = t64_read_le16(&bytes[0x24]);
    entry_count = used_entries;
    if (max_entries < used_entries) {
        return T64_MALFORMED_DIRECTORY;
    }

    directory_end = T64_ENTRY_OFFSET + ((size_t)max_entries * T64_ENTRY_SIZE);
    if (directory_end > size) {
        return T64_MALFORMED_DIRECTORY;
    }

    for (i = 0; i < entry_count; ++i) {
        const uint8_t *entry = &bytes[T64_ENTRY_OFFSET + (i * T64_ENTRY_SIZE)];
        uint8_t entry_type = entry[T64_ENTRY_TYPE_OFFSET];
        uint8_t cbm_type = entry[T64_ENTRY_CBM_TYPE_OFFSET];
        uint16_t load_address = t64_read_le16(&entry[T64_ENTRY_START_OFFSET]);
        uint16_t end_address = t64_read_le16(&entry[T64_ENTRY_END_OFFSET]);
        uint32_t data_offset = t64_read_le32(&entry[T64_ENTRY_DATA_OFFSET]);
        size_t next_offset;
        size_t payload_size;
        uint8_t *prg;

        if (entry_type == 0 || cbm_type == 0) {
            continue;
        }
        if ((size_t)data_offset > size) {
            return T64_ENTRY_RANGE_INVALID;
        }

        next_offset = t64_next_data_offset(bytes, size, entry_count, data_offset);
        if (next_offset < (size_t)data_offset || next_offset > size) {
            return T64_ENTRY_RANGE_INVALID;
        }
        payload_size = next_offset - (size_t)data_offset;
        if (end_address > load_address) {
            size_t address_size = (size_t)(end_address - load_address);
            if (address_size <= size - (size_t)data_offset &&
                (uint32_t)load_address + address_size <= 0x10000u) {
                payload_size = address_size;
            }
        }

        if (payload_size == 0 || payload_size > 65536u ||
            (uint32_t)load_address + payload_size > 0x10000u) {
            return T64_ENTRY_RANGE_INVALID;
        }

        prg = malloc(payload_size + 2u);
        if (prg == NULL) {
            return T64_OUT_OF_MEMORY;
        }

        prg[0] = (uint8_t)(load_address & 0xffu);
        prg[1] = (uint8_t)((load_address >> 8) & 0xffu);
        memcpy(&prg[2], &bytes[data_offset], payload_size);

        out_file->bytes = prg;
        out_file->size = payload_size + 2u;
        return T64_OK;
    }

    return T64_FILE_NOT_FOUND;
}

void t64_file_data_free(t64_file_data *file) {
    if (file == NULL) {
        return;
    }

    free(file->bytes);
    file->bytes = NULL;
    file->size = 0;
}
