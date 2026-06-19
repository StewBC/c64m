#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int expect_result(symbol_result actual, symbol_result expected, const char *label)
{
    if (actual != expected) {
        fprintf(stderr, "%s: expected result %d, got %d\n", label, expected, actual);
        return 1;
    }

    return 0;
}

static int expect_string(const char *actual, const char *expected, const char *label)
{
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n",
            label,
            expected,
            actual == NULL ? "(null)" : actual);
        return 1;
    }

    return 0;
}

static int expect_symbol(
    const symbol_info *info,
    uint16_t address,
    const char *name,
    symbol_source_kind source_kind,
    const char *source_name,
    const char *label)
{
    int failures = 0;

    if (info->address != address) {
        fprintf(stderr, "%s: expected address $%04X, got $%04X\n", label, address, info->address);
        failures++;
    }
    failures += expect_string(info->name, name, label);
    if (info->source_kind != source_kind) {
        fprintf(stderr, "%s: expected source kind %d, got %d\n", label, source_kind, info->source_kind);
        failures++;
    }
    failures += expect_string(info->source_name, source_name, label);

    return failures;
}

static int test_add_find_and_resolver(void)
{
    int failures = 0;
    symbol_table *table;
    symbol_info info;
    symbol_resolver resolver;
    disasm_6502_line line;
    uint8_t jsr[] = {0x20, 0xd2, 0xff};
    uint16_t address = 0;
    symbol_entry entries[4];
    size_t count;

    table = symbol_table_create();
    if (table == NULL) {
        fprintf(stderr, "symbol_table_create failed\n");
        return 1;
    }

    failures += expect_result(
        symbol_table_add(table, 0xffd2, "CHROUT", SYMBOL_SOURCE_BUILTIN, "kernal", false),
        SYMBOL_OK,
        "add CHROUT");
    failures += expect_result(
        symbol_table_find_by_address(table, 0xffd2, &info),
        SYMBOL_OK,
        "find CHROUT by address");
    failures += expect_symbol(&info, 0xffd2, "CHROUT", SYMBOL_SOURCE_BUILTIN, "kernal", "CHROUT info");
    failures += expect_result(
        symbol_table_find_by_name(table, "CHROUT", &info),
        SYMBOL_OK,
        "find CHROUT by name");
    failures += expect_symbol(&info, 0xffd2, "CHROUT", SYMBOL_SOURCE_BUILTIN, "kernal", "CHROUT name info");

    symbol_table_make_resolver(table, &resolver);
    if (resolver.label_to_address(resolver.userdata, "CHROUT", &address) != SYMBOL_LOOKUP_FOUND ||
        address != 0xffd2) {
        fprintf(stderr, "resolver label_to_address failed\n");
        failures++;
    }

    line = disasm_6502_decode_line(0x0801, jsr, sizeof(jsr), &resolver);
    failures += expect_string(line.text, "JSR CHROUT", "resolver disassembly");

    count = resolver.enumerate(resolver.userdata, entries, 4);
    if (count != 1 || entries[0].address != 0xffd2 || strcmp(entries[0].label, "CHROUT") != 0) {
        fprintf(stderr, "resolver enumerate mismatch\n");
        failures++;
    }

    symbol_table_destroy(table);
    return failures;
}

static int test_conflicts_and_overwrite(void)
{
    int failures = 0;
    symbol_table *table;
    symbol_info info;

    table = symbol_table_create();
    if (table == NULL) {
        fprintf(stderr, "symbol_table_create failed\n");
        return 1;
    }

    failures += expect_result(
        symbol_table_add(table, 0x1000, "START", SYMBOL_SOURCE_FILE, "main.lbl", false),
        SYMBOL_OK,
        "add START");
    failures += expect_result(
        symbol_table_add(table, 0x1000, "START", SYMBOL_SOURCE_FILE, "main.lbl", false),
        SYMBOL_ALREADY_EXISTS,
        "duplicate exact");
    failures += expect_result(
        symbol_table_add(table, 0x1000, "ENTRY", SYMBOL_SOURCE_FILE, "other.lbl", false),
        SYMBOL_CONFLICT,
        "address conflict");
    failures += expect_result(
        symbol_table_add(table, 0x1200, "START", SYMBOL_SOURCE_USER, "manual", false),
        SYMBOL_CONFLICT,
        "name conflict");

    failures += expect_result(
        symbol_table_add(table, 0x1000, "ENTRY", SYMBOL_SOURCE_FILE, "other.lbl", true),
        SYMBOL_REPLACED,
        "overwrite address conflict");
    failures += expect_result(
        symbol_table_find_by_address(table, 0x1000, &info),
        SYMBOL_OK,
        "find overwritten address");
    failures += expect_symbol(&info, 0x1000, "ENTRY", SYMBOL_SOURCE_FILE, "other.lbl", "overwritten address info");
    failures += expect_result(
        symbol_table_find_by_name(table, "START", &info),
        SYMBOL_NOT_FOUND,
        "old name removed");

    failures += expect_result(
        symbol_table_add(table, 0x2000, "ENTRY", SYMBOL_SOURCE_USER, "manual", true),
        SYMBOL_REPLACED,
        "overwrite name conflict");
    failures += expect_result(
        symbol_table_find_by_name(table, "ENTRY", &info),
        SYMBOL_OK,
        "find rebound name");
    failures += expect_symbol(&info, 0x2000, "ENTRY", SYMBOL_SOURCE_USER, "manual", "rebound name info");
    failures += expect_result(
        symbol_table_find_by_address(table, 0x1000, &info),
        SYMBOL_NOT_FOUND,
        "old address removed");

    symbol_table_destroy(table);
    return failures;
}

static int test_source_removal_clear_and_nearest(void)
{
    int failures = 0;
    symbol_table *table;
    symbol_info info;
    uint16_t offset = 0;

    table = symbol_table_create();
    if (table == NULL) {
        fprintf(stderr, "symbol_table_create failed\n");
        return 1;
    }

    failures += expect_result(
        symbol_table_add(table, 0x0801, "BASIC_START", SYMBOL_SOURCE_BUILTIN, "basic", false),
        SYMBOL_OK,
        "add basic");
    failures += expect_result(
        symbol_table_add(table, 0x0810, "CURRENT", SYMBOL_SOURCE_ASSEMBLER, "current", false),
        SYMBOL_OK,
        "add current");
    failures += expect_result(
        symbol_table_add(table, 0x1000, "FILE_LABEL", SYMBOL_SOURCE_FILE, "main.sym", false),
        SYMBOL_OK,
        "add file label");

    failures += expect_result(
        symbol_table_find_nearest_before(table, 0x0805, 4, &info, &offset),
        SYMBOL_OK,
        "nearest within range");
    failures += expect_symbol(&info, 0x0801, "BASIC_START", SYMBOL_SOURCE_BUILTIN, "basic", "nearest info");
    if (offset != 4) {
        fprintf(stderr, "nearest offset: expected 4, got %u\n", offset);
        failures++;
    }
    failures += expect_result(
        symbol_table_find_nearest_before(table, 0x0805, 3, &info, &offset),
        SYMBOL_NOT_FOUND,
        "nearest outside range");

    failures += expect_result(
        symbol_table_remove_source(table, SYMBOL_SOURCE_ASSEMBLER, "current"),
        SYMBOL_OK,
        "remove current source");
    failures += expect_result(
        symbol_table_find_by_name(table, "CURRENT", &info),
        SYMBOL_NOT_FOUND,
        "removed assembler symbol");
    failures += expect_result(
        symbol_table_find_by_name(table, "BASIC_START", &info),
        SYMBOL_OK,
        "kept builtin symbol");
    if (symbol_table_count(table) != 2) {
        fprintf(stderr, "expected two symbols after source removal, got %zu\n", symbol_table_count(table));
        failures++;
    }

    failures += expect_result(
        symbol_table_remove_kind(table, SYMBOL_SOURCE_FILE),
        SYMBOL_OK,
        "remove file symbols");
    failures += expect_result(
        symbol_table_find_by_name(table, "FILE_LABEL", &info),
        SYMBOL_NOT_FOUND,
        "removed file symbol");
    failures += expect_result(
        symbol_table_find_by_name(table, "BASIC_START", &info),
        SYMBOL_OK,
        "kept builtin after kind removal");

    symbol_table_clear(table);
    if (symbol_table_count(table) != 0) {
        fprintf(stderr, "expected zero symbols after clear, got %zu\n", symbol_table_count(table));
        failures++;
    }

    symbol_table_destroy(table);
    return failures;
}

static int write_symbol_file(char *path, size_t path_size, const char *source)
{
    FILE *file;
    int fd;

    snprintf(path, path_size, "/tmp/c64m_symbols_XXXXXX");
    fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }

    file = fdopen(fd, "w");
    if (file == NULL) {
        perror("fdopen");
        close(fd);
        unlink(path);
        return 1;
    }

    fputs(source, file);
    if (fclose(file) != 0) {
        perror("fclose");
        unlink(path);
        return 1;
    }
    return 0;
}

static int test_symbol_file_best_effort_load(void)
{
    char path[128];
    symbol_table *table;
    symbol_info info;
    size_t loaded = 0;
    int failures = 0;
    const char *source =
        "\n"
        "    AF08 EVSTR And this is ignored\n"
        "bad line\n"
        "0xC000 Start\n"
        "$FFD2 CHROUT trailing words\n"
        "wordAF09 nope\n"
        "12345 TOO_LONG\n"
        "$D020 BORDER\n";

    if (write_symbol_file(path, sizeof(path), source) != 0) {
        return 1;
    }

    table = symbol_table_create();
    if (table == NULL) {
        unlink(path);
        return 1;
    }

    failures += expect_result(
        symbol_table_load_file(table, path, path, &loaded),
        SYMBOL_OK,
        "load symbol file");
    if (loaded != 4) {
        fprintf(stderr, "expected 4 loaded symbols, got %zu\n", loaded);
        failures++;
    }
    if (symbol_table_find_by_name(table, "EVSTR", &info) != SYMBOL_OK || info.address != 0xaf08) {
        fprintf(stderr, "EVSTR not loaded from indented bare address\n");
        failures++;
    }
    if (symbol_table_find_by_address(table, 0xc000, &info) != SYMBOL_OK ||
        strcmp(info.name, "Start") != 0) {
        fprintf(stderr, "Start not loaded from 0x address\n");
        failures++;
    }
    if (symbol_table_find_by_name(table, "CHROUT", &info) != SYMBOL_OK || info.address != 0xffd2) {
        fprintf(stderr, "CHROUT not loaded from $ address\n");
        failures++;
    }
    if (symbol_table_find_by_name(table, "BORDER", &info) != SYMBOL_OK || info.address != 0xd020) {
        fprintf(stderr, "BORDER not loaded\n");
        failures++;
    }
    if (symbol_table_find_by_name(table, "TOO_LONG", &info) != SYMBOL_NOT_FOUND) {
        fprintf(stderr, "oversized address token was loaded\n");
        failures++;
    }
    if (symbol_table_find_by_name(table, "nope", &info) != SYMBOL_NOT_FOUND) {
        fprintf(stderr, "embedded address token was loaded\n");
        failures++;
    }

    symbol_table_destroy(table);
    unlink(path);
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_add_find_and_resolver();
    failures += test_conflicts_and_overwrite();
    failures += test_source_removal_clear_and_nearest();
    failures += test_symbol_file_best_effort_load();

    return failures == 0 ? 0 : 1;
}
