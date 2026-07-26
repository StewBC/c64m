#include "runtime_assembler.h"

#include "asm.h"
#include "errorlog.h"

#include <stdio.h>
#include <string.h>

typedef struct assembler_output_ctx {
    c64_t *machine;
    uint16_t first_address;
    uint16_t last_address;
    uint32_t byte_count;
    bool has_output;
} assembler_output_ctx;

typedef struct assembler_symbol_import {
    symbol_table *symbols;
    const char *source_name;
    bool ok;
} assembler_symbol_import;

typedef struct assembler_adjustment_format {
    char *buffer;
    size_t size;
    size_t written;
    bool has_adjustments;
} assembler_adjustment_format;

static void runtime_assembler_output_byte(void *user, uint16_t addr, uint8_t val) {
    assembler_output_ctx *ctx = (assembler_output_ctx *)user;
    c64_debug_write_ram(ctx->machine, addr, val);
    if (!ctx->has_output) {
        ctx->first_address = addr;
        ctx->last_address = addr;
        ctx->has_output = true;
    } else {
        if (addr < ctx->first_address) {
            ctx->first_address = addr;
        }
        if (addr > ctx->last_address) {
            ctx->last_address = addr;
        }
    }
    ctx->byte_count++;
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

static void runtime_assembler_format_adjustment(
    size_t target_index,
    const char *segment_name,
    uint16_t address,
    void *user) {
    assembler_adjustment_format *format =
        (assembler_adjustment_format *)user;
    const char *name = segment_name && segment_name[0] ?
        segment_name : "<default>";
    int n;

    if (format->buffer == NULL || format->size == 0 ||
        format->written >= format->size) {
        return;
    }
    if (!format->has_adjustments) {
        n = snprintf(
            format->buffer + format->written,
            format->size - format->written,
            "Assembly succeeded with adjusted segment addresses; update the source to:");
        if (n < 0) {
            return;
        }
        format->written += (size_t)n;
        format->has_adjustments = true;
    }
    if (format->written >= format->size) {
        format->buffer[format->size - 1] = '\0';
        return;
    }
    if (target_index == 0) {
        n = snprintf(
            format->buffer + format->written,
            format->size - format->written,
            "\n  Suggest \"%s\" at $%04X",
            name,
            address);
    } else {
        n = snprintf(
            format->buffer + format->written,
            format->size - format->written,
            "\n  Target %zu: Suggest \"%s\" at $%04X",
            target_index,
            name,
            address);
    }
    if (n < 0) {
        return;
    }
    format->written += (size_t)n;
    if (format->written >= format->size) {
        format->buffer[format->size - 1] = '\0';
    }
}

static bool c64_assemble_file_ex(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    const runtime_assembler_options *options,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *notice,
    size_t notice_size,
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
    if (notice != NULL && notice_size > 0) {
        notice[0] = '\0';
    }
    if (machine == NULL || path == NULL || path[0] == '\0') {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "invalid assembler input");
        }
        return false;
    }

    errlog_init(&log);
    memset(&output_ctx, 0, sizeof(output_ctx));
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
    assembler_set_auto_adjust_segments(
        &assembler,
        options != NULL && options->auto_adjust_segments);

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
        if (ok && notice != NULL && notice_size > 0) {
            assembler_adjustment_format format;
            memset(&format, 0, sizeof(format));
            format.buffer = notice;
            format.size = notice_size;
            assembler_walk_segment_adjustments(
                &assembler,
                runtime_assembler_format_adjustment,
                &format);
        }
    } else {
        runtime_assembler_format_errors(&log, error, error_size);
    }

    assembler_shutdown(&assembler);
    errlog_shutdown(&log);
    if (ok) {
        if (out_start_address != NULL) {
            *out_start_address =
                output_ctx.has_output ? output_ctx.first_address : address;
        }
        if (out_end_address != NULL) {
            *out_end_address = output_ctx.has_output
                ? (uint16_t)(output_ctx.last_address + 1u)
                : address;
        }
        if (out_byte_count != NULL) {
            *out_byte_count = output_ctx.byte_count;
        }
    }
    return ok;
}

bool c64_assemble_file(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size) {
    return c64_assemble_file_ex(
        machine,
        symbols,
        path,
        address,
        source_name,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        error,
        error_size);
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

bool runtime_assemble_file_ex(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *error,
    size_t error_size) {
    return c64_assemble_file_ex(
        machine,
        symbols,
        path,
        address,
        source_name,
        NULL,
        out_start_address,
        out_end_address,
        out_byte_count,
        NULL,
        0,
        error,
        error_size);
}

bool runtime_assemble_file_ex_options(
    c64_t *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    const runtime_assembler_options *options,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *notice,
    size_t notice_size,
    char *error,
    size_t error_size) {
    return c64_assemble_file_ex(
        machine,
        symbols,
        path,
        address,
        source_name,
        options,
        out_start_address,
        out_end_address,
        out_byte_count,
        notice,
        notice_size,
        error,
        error_size);
}
