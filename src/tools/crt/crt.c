#include "crt.h"

#include <stdlib.h>
#include <string.h>

enum {
    CRT_MIN_HEADER_SIZE = 0x40,
    CRT_MAGIC_SIZE = 16,
    CRT_HEADER_LENGTH_OFFSET = 0x10,
    CRT_VERSION_OFFSET = 0x14,
    CRT_HARDWARE_TYPE_OFFSET = 0x16,
    CRT_EXROM_OFFSET = 0x18,
    CRT_GAME_OFFSET = 0x19,
    CRT_NAME_OFFSET = 0x20,
    CRT_CHIP_HEADER_SIZE = 0x10,
    CRT_CHIP_PACKET_LENGTH_OFFSET = 0x04,
    CRT_CHIP_TYPE_OFFSET = 0x08,
    CRT_CHIP_BANK_OFFSET = 0x0a,
    CRT_CHIP_LOAD_OFFSET = 0x0c,
    CRT_CHIP_SIZE_OFFSET = 0x0e,
    CRT_HARDWARE_TYPE_NORMAL = 0
};

typedef struct crt_chip_record {
    crt_chip_type type;
    uint16_t raw_type;
    uint16_t bank;
    uint16_t load_address;
    uint16_t rom_size;
    uint8_t *bytes;
} crt_chip_record;

struct crt_image {
    crt_header header;
    crt_chip_record *chips;
    size_t chip_count;
};

static uint16_t crt_read_be16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1];
}

static uint32_t crt_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) |
        (uint32_t)bytes[3];
}

static crt_chip_type crt_chip_type_from_raw(uint16_t raw_type) {
    switch (raw_type) {
    case 0:
        return CRT_CHIP_TYPE_ROM;
    case 1:
        return CRT_CHIP_TYPE_RAM;
    case 2:
        return CRT_CHIP_TYPE_FLASH;
    default:
        return CRT_CHIP_TYPE_UNKNOWN;
    }
}

static bool crt_has_magic(const uint8_t *bytes, size_t size) {
    static const uint8_t magic[CRT_MAGIC_SIZE] = {
        'C', '6', '4', ' ', 'C', 'A', 'R', 'T',
        'R', 'I', 'D', 'G', 'E', ' ', ' ', ' '
    };

    return bytes != NULL && size >= CRT_MIN_HEADER_SIZE &&
        memcmp(bytes, magic, sizeof(magic)) == 0;
}

static void crt_copy_name(char *target, const uint8_t *source) {
    size_t length = 0;

    while (length < CRT_HEADER_NAME_SIZE && source[length] != 0) {
        target[length] = (char)source[length];
        length++;
    }
    while (length > 0 && target[length - 1u] == ' ') {
        length--;
    }
    target[length] = '\0';
}

static void crt_image_free_chips(crt_image *image) {
    size_t i;

    if (image == NULL || image->chips == NULL) {
        return;
    }
    for (i = 0; i < image->chip_count; ++i) {
        free(image->chips[i].bytes);
    }
    free(image->chips);
    image->chips = NULL;
    image->chip_count = 0;
}

static crt_result crt_image_append_chip(
    crt_image *image,
    const uint8_t *packet,
    size_t packet_length)
{
    crt_chip_record *new_chips;
    crt_chip_record *chip;
    uint16_t rom_size;

    if (packet_length < CRT_CHIP_HEADER_SIZE) {
        return CRT_MALFORMED_CHIP;
    }

    rom_size = crt_read_be16(&packet[CRT_CHIP_SIZE_OFFSET]);
    if (rom_size == 0 ||
        packet_length != CRT_CHIP_HEADER_SIZE + (size_t)rom_size) {
        return CRT_MALFORMED_CHIP;
    }

    new_chips = (crt_chip_record *)realloc(
        image->chips,
        (image->chip_count + 1u) * sizeof(*image->chips));
    if (new_chips == NULL) {
        return CRT_OUT_OF_MEMORY;
    }
    image->chips = new_chips;
    chip = &image->chips[image->chip_count];
    memset(chip, 0, sizeof(*chip));

    chip->raw_type = crt_read_be16(&packet[CRT_CHIP_TYPE_OFFSET]);
    chip->type = crt_chip_type_from_raw(chip->raw_type);
    chip->bank = crt_read_be16(&packet[CRT_CHIP_BANK_OFFSET]);
    chip->load_address = crt_read_be16(&packet[CRT_CHIP_LOAD_OFFSET]);
    chip->rom_size = rom_size;
    chip->bytes = (uint8_t *)malloc(rom_size);
    if (chip->bytes == NULL) {
        return CRT_OUT_OF_MEMORY;
    }
    memcpy(chip->bytes, &packet[CRT_CHIP_HEADER_SIZE], rom_size);
    image->chip_count++;
    return CRT_OK;
}

const char *crt_result_string(crt_result result) {
    switch (result) {
    case CRT_OK:
        return "ok";
    case CRT_INVALID_ARGUMENT:
        return "invalid argument";
    case CRT_UNSUPPORTED_IMAGE:
        return "unsupported image";
    case CRT_MALFORMED_HEADER:
        return "malformed header";
    case CRT_MALFORMED_CHIP:
        return "malformed chip";
    case CRT_OUT_OF_MEMORY:
        return "out of memory";
    default:
        return "unknown error";
    }
}

const char *crt_chip_type_string(crt_chip_type type) {
    switch (type) {
    case CRT_CHIP_TYPE_ROM:
        return "ROM";
    case CRT_CHIP_TYPE_RAM:
        return "RAM";
    case CRT_CHIP_TYPE_FLASH:
        return "FLASH";
    case CRT_CHIP_TYPE_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

crt_image *crt_image_create(const uint8_t *bytes, size_t size, crt_result *out_result) {
    crt_image *image;
    uint32_t header_length;
    size_t offset;
    crt_result result = CRT_OK;

    if (out_result != NULL) {
        *out_result = CRT_OK;
    }
    if (bytes == NULL) {
        if (out_result != NULL) {
            *out_result = CRT_INVALID_ARGUMENT;
        }
        return NULL;
    }
    if (size < CRT_MIN_HEADER_SIZE) {
        if (out_result != NULL) {
            *out_result = CRT_MALFORMED_HEADER;
        }
        return NULL;
    }
    if (!crt_has_magic(bytes, size)) {
        if (out_result != NULL) {
            *out_result = CRT_UNSUPPORTED_IMAGE;
        }
        return NULL;
    }

    header_length = crt_read_be32(&bytes[CRT_HEADER_LENGTH_OFFSET]);
    if (header_length < CRT_MIN_HEADER_SIZE || header_length > size) {
        if (out_result != NULL) {
            *out_result = CRT_MALFORMED_HEADER;
        }
        return NULL;
    }

    image = (crt_image *)calloc(1, sizeof(*image));
    if (image == NULL) {
        if (out_result != NULL) {
            *out_result = CRT_OUT_OF_MEMORY;
        }
        return NULL;
    }

    image->header.header_length = header_length;
    image->header.version = crt_read_be16(&bytes[CRT_VERSION_OFFSET]);
    image->header.hardware_type = crt_read_be16(&bytes[CRT_HARDWARE_TYPE_OFFSET]);
    image->header.exrom = bytes[CRT_EXROM_OFFSET];
    image->header.game = bytes[CRT_GAME_OFFSET];
    crt_copy_name(image->header.name, &bytes[CRT_NAME_OFFSET]);

    offset = header_length;
    while (offset < size) {
        uint32_t packet_length;

        if (size - offset < CRT_CHIP_HEADER_SIZE) {
            result = CRT_MALFORMED_CHIP;
            break;
        }
        if (memcmp(&bytes[offset], "CHIP", 4) != 0) {
            result = CRT_MALFORMED_CHIP;
            break;
        }
        packet_length = crt_read_be32(&bytes[offset + CRT_CHIP_PACKET_LENGTH_OFFSET]);
        if (packet_length < CRT_CHIP_HEADER_SIZE ||
            packet_length > size - offset) {
            result = CRT_MALFORMED_CHIP;
            break;
        }
        result = crt_image_append_chip(image, &bytes[offset], packet_length);
        if (result != CRT_OK) {
            break;
        }
        offset += packet_length;
    }

    if (result == CRT_OK && image->chip_count == 0) {
        result = CRT_MALFORMED_CHIP;
    }
    if (result != CRT_OK) {
        crt_image_destroy(image);
        if (out_result != NULL) {
            *out_result = result;
        }
        return NULL;
    }

    if (out_result != NULL) {
        *out_result = CRT_OK;
    }
    return image;
}

void crt_image_destroy(crt_image *image) {
    if (image == NULL) {
        return;
    }
    crt_image_free_chips(image);
    free(image);
}

const crt_header *crt_image_header(const crt_image *image) {
    if (image == NULL) {
        return NULL;
    }
    return &image->header;
}

size_t crt_image_chip_count(const crt_image *image) {
    if (image == NULL) {
        return 0;
    }
    return image->chip_count;
}

crt_result crt_image_chip(const crt_image *image, size_t index, crt_chip *out_chip) {
    const crt_chip_record *chip;

    if (image == NULL || out_chip == NULL) {
        return CRT_INVALID_ARGUMENT;
    }
    if (index >= image->chip_count) {
        return CRT_INVALID_ARGUMENT;
    }

    chip = &image->chips[index];
    out_chip->type = chip->type;
    out_chip->raw_type = chip->raw_type;
    out_chip->bank = chip->bank;
    out_chip->load_address = chip->load_address;
    out_chip->rom_size = chip->rom_size;
    out_chip->bytes = chip->bytes;
    return CRT_OK;
}

bool crt_image_is_generic_supported(const crt_image *image) {
    size_t i;

    if (image == NULL ||
        image->header.hardware_type != CRT_HARDWARE_TYPE_NORMAL ||
        image->chip_count == 0) {
        return false;
    }

    for (i = 0; i < image->chip_count; ++i) {
        const crt_chip_record *chip = &image->chips[i];

        if (chip->type != CRT_CHIP_TYPE_ROM || chip->bank != 0) {
            return false;
        }
        if (!((chip->load_address == 0x8000u &&
               (chip->rom_size == 0x2000u || chip->rom_size == 0x4000u)) ||
              (chip->load_address == 0xa000u && chip->rom_size == 0x2000u))) {
            return false;
        }
    }

    return true;
}
