#include "runtime_assembler.h"

#include "asm.h"
#include "errorlog.h"

#include <stdio.h>
#include <string.h>

typedef struct assembler_output_ctx {
    c64_t *machine;
} assembler_output_ctx;

typedef struct assembler_symbol_import {
    symbol_table *symbols;
    const char *source_name;
    bool ok;
} assembler_symbol_import;

static void runtime_assembler_output_byte(void *user, uint16_t addr, uint8_t val) {
    assembler_output_ctx *ctx = (assembler_output_ctx *)user;
    c64_debug_write_ram(ctx->machine, addr, val);
}

static void runtime_assembler_import_symbol(const char *name, uint16_t address, void *user) {
    assembler_symbol_import *import = (assembler_symbol_import *)user;

    if (symbol_table_add(
            import->symbols,
            address,
            name,
            SYMBOL_SOURCE_ASSEMBLER,
            import->source_name,
            true) == SYMBOL_OUT_OF_MEMORY) {
        import->ok = false;
    }
}

static void runtime_assembler_format_errors(const ERRORLOG *log, char *error, size_t error_size) {
    size_t written = 0;

    if (error == NULL || error_size == 0) {
        return;
    }
    error[0] = '\0';

    if (log == NULL || log->log_array.items == 0) {
        snprintf(error, error_size, "assembly failed");
        return;
    }

    for (size_t i = 0; i < log->log_array.items; i++) {
        const ERROR_ENTRY *entry = ARRAY_GET((DYNARRAY *)&log->log_array, ERROR_ENTRY, i);
        int n;

        if (entry == NULL || entry->err_str == NULL) {
            continue;
        }
        n = snprintf(
            error + written,
            written < error_size ? error_size - written : 0,
            "%s%s",
            written > 0 ? "\n" : "",
            entry->err_str);
        if (n < 0) {
            return;
        }
        written += (size_t)n;
        if (written >= error_size) {
            error[error_size - 1] = '\0';
            return;
        }
    }

    if (written == 0) {
        snprintf(error, error_size, "assembly failed");
    }
}

bool c64_assemble_file(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size) {
    ERRORLOG log;
    ASSEMBLER assembler;
    assembler_output_ctx output_ctx;
    CB_ASM_CTX cb;
    bool ok = false;

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (machine == NULL || path == NULL || path[0] == '\0') {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "invalid assembler input");
        }
        return false;
    }

    errlog_init(&log);
    output_ctx.machine = machine;
    memset(&cb, 0, sizeof(cb));
    cb.user = &output_ctx;
    cb.default_target = &output_ctx;
    cb.output_byte = runtime_assembler_output_byte;
    // No target_open: assembling live into machine RAM has no per-file targets,
    // so a named `.scope file="..."` is rejected rather than silently ignored.

    if (assembler_init(&assembler, &log, &cb) != ASM_OK) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "assembler initialization failed");
        }
        errlog_shutdown(&log);
        return false;
    }

    // Let source detect it is being assembled live in the emulator (vs the c64masm CLI).
    assembler_predefine(&assembler, "C64MASM", "0");

    if (assembler_assemble(&assembler, path, address) == ASM_OK) {
        ok = true;
        if (symbols != NULL) {
            assembler_symbol_import import;
            import.symbols = symbols;
            import.source_name = source_name != NULL && source_name[0] != '\0' ? source_name : path;
            import.ok = true;
            symbol_table_remove_kind(symbols, SYMBOL_SOURCE_ASSEMBLER);
            assembler_walk_symbols(&assembler, runtime_assembler_import_symbol, &import);
            if (!import.ok) {
                ok = false;
                if (error != NULL && error_size > 0) {
                    snprintf(error, error_size, "failed to import assembler symbols");
                }
            }
        }
    } else {
        runtime_assembler_format_errors(&log, error, error_size);
    }

    assembler_shutdown(&assembler);
    errlog_shutdown(&log);
    return ok;
}

bool runtime_assemble_file(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size) {
    return c64_assemble_file(machine, symbols, path, address, source_name, error, error_size);
}
