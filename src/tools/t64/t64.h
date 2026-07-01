#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum t64_result {
    T64_OK = 0,
    T64_INVALID_ARGUMENT,
    T64_UNSUPPORTED_IMAGE,
    T64_MALFORMED_DIRECTORY,
    T64_FILE_NOT_FOUND,
    T64_ENTRY_RANGE_INVALID,
    T64_OUT_OF_MEMORY
} t64_result;

typedef struct t64_file_data {
    uint8_t *bytes;
    size_t size;
} t64_file_data;

const char *t64_result_string(t64_result result);

t64_result t64_extract_first_prg(
    const uint8_t *bytes,
    size_t size,
    t64_file_data *out_file);

void t64_file_data_free(t64_file_data *file);
