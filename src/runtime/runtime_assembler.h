#pragma once

#include "c64.h"
#include "symbol_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool c64_assemble_file(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size);

bool runtime_assemble_file(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size);
