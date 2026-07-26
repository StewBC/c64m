#include "runtime_assembler.h"

#include "c64.h"
#include "symbol_table.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_source(char *path, size_t path_size, const char *source) {
    return c64m_test_write_temp_file(path, path_size, "c64m_runtime_assembler", source);
}

static int expect_u8(const char *label, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected $%02x, got $%02x\n", label, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_symbol(symbol_table *symbols, const char *name, uint16_t address) {
    symbol_info info;
    if (symbol_table_find_by_name(symbols, name, &info) != SYMBOL_OK) {
        fprintf(stderr, "symbol %s not found\n", name);
        return 1;
    }
    if (info.address != address ||
        info.source_kind != SYMBOL_SOURCE_ASSEMBLER ||
        strcmp(info.source_name, "current") != 0) {
        fprintf(stderr, "symbol %s mismatch: address=$%04x source=%d/%s\n",
                name,
                info.address,
                info.source_kind,
                info.source_name ? info.source_name : "(null)");
        return 1;
    }
    return 0;
}

static int test_assemble_imports_symbols(void) {
    char path[128];
    char error[1024];
    c64_t machine;
    symbol_table *symbols;
    int failures = 0;
    const char *source =
        "start:\n"
        "    lda #$42\n"
        ".scope Inner\n"
        "target:\n"
        "    sta $d020\n"
        ".endscope\n";

    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    c64_init(&machine);
    symbols = symbol_table_create();
    if (symbols == NULL) {
        c64m_test_remove_file(path);
        return 1;
    }

    symbol_table_add(symbols, 0x1234, "STALE", SYMBOL_SOURCE_ASSEMBLER, "current", false);
    symbol_table_add(symbols, 0x1235, "OLD_STALE", SYMBOL_SOURCE_ASSEMBLER, "old", false);
    symbol_table_add(symbols, 0xffd2, "CHROUT", SYMBOL_SOURCE_BUILTIN, "kernal", false);

    if (!c64_assemble_file(&machine, symbols, path, 0x0801, "current", error, sizeof(error))) {
        fprintf(stderr, "c64_assemble_file failed: %s\n", error);
        failures++;
    }

    failures += expect_u8("lda opcode", 0xa9, c64_debug_read_ram(&machine, 0x0801));
    failures += expect_u8("lda immediate", 0x42, c64_debug_read_ram(&machine, 0x0802));
    failures += expect_u8("sta opcode", 0x8d, c64_debug_read_ram(&machine, 0x0803));
    failures += expect_symbol(symbols, "start", 0x0801);
    failures += expect_symbol(symbols, "Inner::target", 0x0803);
    if (symbol_table_find_by_name(symbols, "STALE", &(symbol_info){0}) != SYMBOL_NOT_FOUND) {
        fprintf(stderr, "stale assembler symbol was not removed\n");
        failures++;
    }
    if (symbol_table_find_by_name(symbols, "OLD_STALE", &(symbol_info){0}) != SYMBOL_NOT_FOUND) {
        fprintf(stderr, "old-source stale assembler symbol was not removed\n");
        failures++;
    }
    if (symbol_table_find_by_name(symbols, "CHROUT", &(symbol_info){0}) != SYMBOL_OK) {
        fprintf(stderr, "builtin symbol was not preserved\n");
        failures++;
    }

    symbol_table_destroy(symbols);
    c64m_test_remove_file(path);
    return failures;
}

static int test_assemble_reports_errors(void) {
    char path[128];
    char error[1024];
    c64_t machine;
    int failures = 0;
    const char *source =
        "    lda missing_symbol\n";

    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    c64_init(&machine);
    if (c64_assemble_file(&machine, NULL, path, 0x0801, "current", error, sizeof(error))) {
        fprintf(stderr, "bad assembly unexpectedly succeeded\n");
        failures++;
    }
    if (strstr(error, "missing_symbol") == NULL) {
        fprintf(stderr, "assembler error did not mention missing symbol: %s\n", error);
        failures++;
    }

    c64m_test_remove_file(path);
    return failures;
}

/* Regression: the _ex reporting must describe where code actually landed
   (lowest emitted origin and one-past-the-end), not the requested host default
   that the source overrides. This is what assemble-complete reports and what
   BASIC-run uses to set VARTAB. */
static int test_assemble_reports_emitted_extent(void) {
    char path[128];
    char error[1024];
    c64_t machine;
    int failures = 0;
    uint16_t start = 0xffff;
    uint16_t end = 0;
    uint32_t count = 0;
    /* Requested address is the $8000 host default, but the source re-anchors to
       $0801 and emits 5 bytes (2 + 3). */
    const char *source =
        "* = $0801\n"
        "    lda #$01\n"   /* 2 bytes: $0801,$0802 */
        "    sta $d020\n"; /* 3 bytes: $0803,$0804,$0805 */

    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    c64_init(&machine);
    if (!runtime_assemble_file_ex(&machine, NULL, path, 0x8000, "current",
                                  &start, &end, &count, error, sizeof(error))) {
        fprintf(stderr, "runtime_assemble_file_ex failed: %s\n", error);
        c64m_test_remove_file(path);
        return 1;
    }

    if (start != 0x0801u) {
        fprintf(stderr, "emitted start: expected $0801, got $%04x\n", start);
        failures++;
    }
    if (end != 0x0806u) {
        fprintf(stderr, "emitted end: expected $0806, got $%04x\n", end);
        failures++;
    }
    if (count != 5u) {
        fprintf(stderr, "emitted count: expected 5, got %u\n", count);
        failures++;
    }

    c64m_test_remove_file(path);
    return failures;
}

static int test_assemble_auto_adjusts_segments(void) {
    char path[128];
    char error[1024];
    char notice[1024];
    c64_t machine;
    runtime_assembler_options options = {
        .auto_adjust_segments = true,
    };
    uint16_t start = 0xffff;
    uint16_t end = 0;
    uint32_t count = 0;
    int failures = 0;
    const char *source =
        ".segdef \"A\", $1000\n"
        ".segdef \"B\", $1080\n"
        ".segdef \"C\", $1200\n"
        ".segment \"A\"\n"
        "    .res $100\n"
        "    .byte $a1\n"
        ".segment \"B\"\n"
        "    .align $100\n"
        "    .byte $b1\n"
        ".segment \"C\"\n"
        "    .byte $c1\n";

    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    c64_init(&machine);
    if (!runtime_assemble_file_ex_options(
            &machine,
            NULL,
            path,
            0x0801,
            "current",
            &options,
            &start,
            &end,
            &count,
            notice,
            sizeof(notice),
            error,
            sizeof(error))) {
        fprintf(stderr, "auto-adjust runtime assembly failed: %s\n", error);
        c64m_test_remove_file(path);
        return 1;
    }
    failures += expect_u8(
        "auto-adjust A marker",
        0xa1,
        c64_debug_read_ram(&machine, 0x1100));
    failures += expect_u8(
        "auto-adjust B marker",
        0xb1,
        c64_debug_read_ram(&machine, 0x1200));
    failures += expect_u8(
        "auto-adjust C marker",
        0xc1,
        c64_debug_read_ram(&machine, 0x1201));
    if (start != 0x1000 || end != 0x1202 || count != 514) {
        fprintf(stderr,
                "auto-adjust extent mismatch: $%04x-$%04x, %u bytes\n",
                start,
                end,
                count);
        failures++;
    }
    if (strstr(notice, "Suggest \"B\" at $1101") == NULL ||
        strstr(notice, "Suggest \"C\" at $1201") == NULL) {
        fprintf(stderr, "auto-adjust notice mismatch: %s\n", notice);
        failures++;
    }

    c64m_test_remove_file(path);
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_assemble_imports_symbols();
    failures += test_assemble_reports_errors();
    failures += test_assemble_reports_emitted_extent();
    failures += test_assemble_auto_adjusts_segments();

    return failures == 0 ? 0 : 1;
}
