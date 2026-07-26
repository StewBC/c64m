#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRT_HEADER_NAME_SIZE 32u

typedef struct crt_image crt_image;

typedef enum crt_result {
    CRT_OK = 0,
    CRT_INVALID_ARGUMENT,
    CRT_UNSUPPORTED_IMAGE,
    CRT_MALFORMED_HEADER,
    CRT_MALFORMED_CHIP,
    CRT_OUT_OF_MEMORY
} crt_result;

typedef enum crt_chip_type {
    CRT_CHIP_TYPE_ROM = 0,
    CRT_CHIP_TYPE_RAM = 1,
    CRT_CHIP_TYPE_FLASH = 2,
    CRT_CHIP_TYPE_UNKNOWN = 255
} crt_chip_type;

typedef struct crt_header {
    uint32_t header_length;
    uint16_t version;
    uint16_t hardware_type;
    uint8_t exrom;
    uint8_t game;
    char name[CRT_HEADER_NAME_SIZE + 1u];
} crt_header;

typedef struct crt_chip {
    crt_chip_type type;
    uint16_t raw_type;
    uint16_t bank;
    uint16_t load_address;
    uint16_t rom_size;
    const uint8_t *bytes;
} crt_chip;

const char *crt_result_string(crt_result result);
const char *crt_chip_type_string(crt_chip_type type);

crt_image *crt_image_create(const uint8_t *bytes, size_t size, crt_result *out_result);
void crt_image_destroy(crt_image *image);

const crt_header *crt_image_header(const crt_image *image);
size_t crt_image_chip_count(const crt_image *image);
crt_result crt_image_chip(const crt_image *image, size_t index, crt_chip *out_chip);

bool crt_image_is_generic_supported(const crt_image *image);
/* CRT hardware type 19 (Magic Desk / Domark / HES): multi-bank 8K @ $8000. */
bool crt_image_is_magic_desk_supported(const crt_image *image);
/* CRT hardware type 5 (Ocean type 1): multi-bank 8K, max 64 banks. */
bool crt_image_is_ocean_supported(const crt_image *image);
/* CRT hardware type 15 (C64GS / System 3): up to 64 × 8K @ $8000. */
bool crt_image_is_c64gs_supported(const crt_image *image);
/* CRT hardware type 7 (Fun Play / Power Play): 16 × 8K @ $8000, scrambled banks. */
bool crt_image_is_funplay_supported(const crt_image *image);
/* CRT hardware type 8 (Super Games): 4 × 16K @ $8000. */
bool crt_image_is_super_games_supported(const crt_image *image);
/* CRT hardware type 17 (Dinamic): 16 × 8K @ $8000. */
bool crt_image_is_dinamic_supported(const crt_image *image);
/* True if c64m can attach this image with a known mapper. */
bool crt_image_is_supported(const crt_image *image);

/* Fun Play CRT chip.bank / IO register value <-> linear bank index.
   VICE funplay.c: linear = ((v >> 3) & 7) | ((v & 1) << 3). */
uint8_t crt_funplay_descramble_bank(uint8_t register_value);
