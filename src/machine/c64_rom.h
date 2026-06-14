#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c64_bus.h"

typedef struct c64_rom_set {
    uint8_t basic[C64_BASIC_ROM_SIZE];
    uint8_t kernal[C64_KERNAL_ROM_SIZE];
    uint8_t character[C64_CHAR_ROM_SIZE];
    bool has_basic;
    bool has_kernal;
    bool has_character;
} c64_rom_set;

void c64_rom_set_init(c64_rom_set *roms);

bool c64_rom_load_combined_64c(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size);

bool c64_rom_load_basic(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size);

bool c64_rom_load_kernal(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size);

bool c64_rom_load_character(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size);

bool c64_rom_load_split(
    c64_rom_set *roms,
    const char *basic_path,
    const char *kernal_path,
    const char *character_path,
    char *error,
    size_t error_size);
