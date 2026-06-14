#include "c64_rom.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static bool read_exact_file(
    const char *path,
    uint8_t *destination,
    size_t expected_size,
    char *error,
    size_t error_size) {
    FILE *file;
    long file_size;
    size_t bytes_read;

    assert(destination);

    if (!path || path[0] == '\0') {
        set_error(error, error_size, "ROM path is empty");
        return false;
    }

    file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "failed to open ROM file: %s", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(error, error_size, "failed to seek ROM file: %s", path);
        return false;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        snprintf(error, error_size, "failed to read ROM file size: %s", path);
        return false;
    }

    if ((size_t)file_size != expected_size) {
        fclose(file);
        snprintf(
            error,
            error_size,
            "ROM file %s has size %ld, expected %lu",
            path,
            file_size,
            (unsigned long)expected_size);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        snprintf(error, error_size, "failed to rewind ROM file: %s", path);
        return false;
    }

    bytes_read = fread(destination, 1, expected_size, file);
    fclose(file);

    if (bytes_read != expected_size) {
        snprintf(error, error_size, "failed to read complete ROM file: %s", path);
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

void c64_rom_set_init(c64_rom_set *roms) {
    assert(roms);

    memset(roms, 0, sizeof(*roms));
}

bool c64_rom_load_combined_64c(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size) {
    uint8_t buffer[C64_BASIC_ROM_SIZE + C64_KERNAL_ROM_SIZE];

    assert(roms);

    if (!read_exact_file(path, buffer, sizeof(buffer), error, error_size)) {
        return false;
    }

    memcpy(roms->basic, buffer, sizeof(roms->basic));
    memcpy(roms->kernal, buffer + sizeof(roms->basic), sizeof(roms->kernal));
    roms->has_basic = true;
    roms->has_kernal = true;
    return true;
}

bool c64_rom_load_basic(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size) {
    assert(roms);

    if (!read_exact_file(path, roms->basic, sizeof(roms->basic), error, error_size)) {
        return false;
    }

    roms->has_basic = true;
    return true;
}

bool c64_rom_load_kernal(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size) {
    assert(roms);

    if (!read_exact_file(path, roms->kernal, sizeof(roms->kernal), error, error_size)) {
        return false;
    }

    roms->has_kernal = true;
    return true;
}

bool c64_rom_load_character(
    c64_rom_set *roms,
    const char *path,
    char *error,
    size_t error_size) {
    assert(roms);

    if (!read_exact_file(path, roms->character, sizeof(roms->character), error, error_size)) {
        return false;
    }

    roms->has_character = true;
    return true;
}

bool c64_rom_load_split(
    c64_rom_set *roms,
    const char *basic_path,
    const char *kernal_path,
    const char *character_path,
    char *error,
    size_t error_size) {
    assert(roms);

    if (!c64_rom_load_basic(roms, basic_path, error, error_size)) {
        return false;
    }

    if (!c64_rom_load_kernal(roms, kernal_path, error, error_size)) {
        return false;
    }

    return c64_rom_load_character(roms, character_path, error, error_size);
}
