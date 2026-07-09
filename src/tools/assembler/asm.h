// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "asm_common.h"
#include "dynarray.h"
#include "errorlog.h"
#include "define.h"
#include "file.h"
#include "gperf.h"
#include "opcode.h"
#include "scope.h"
#include "segment.h"
#include "symbol.h"
#include "token.h"
#include "expr.h"
#include "emit.h"
#include "parse.h"

#define ASM_MAX_LINE 1024

// Emit one assembled byte. `target` is the per-target context: for the default
// (unnamed) output it is CB_ASM_CTX.default_target; for a named `.scope file="..."`
// redirect it is whatever target_open returned.
typedef void (*asm_output_byte_fn)(void *target, uint16_t addr, uint8_t val);

// Open an output redirect for a named `.scope name file="..." dest="..."`.
// Returns an opaque per-target context (passed back to output_byte / target_release),
// or NULL to signal the redirect could not be honoured. May be NULL in the callback
// struct, in which case `.scope` with file=/dest= is rejected with an error.
typedef void *(*asm_target_open_fn)(
    void *user,
    const char *name, int name_len,
    const char *file, int file_len,
    const char *dest, int dest_len);

// Release a context previously returned by target_open. May be NULL.
typedef void (*asm_target_release_fn)(void *user, void *target);

typedef struct {
    void *user;                            // host context, passed to target_open/target_release
    void *default_target;                  // per-target context for the initial unnamed target
    asm_output_byte_fn output_byte;        // required
    asm_target_open_fn target_open;        // optional (NULL => .scope file=/dest= unsupported)
    asm_target_release_fn target_release;  // optional
} CB_ASM_CTX;

typedef void (*assembler_symbol_cb)(
    const char *name,
    uint16_t address,
    void *user);

struct ASSEMBLER {
    CB_ASM_CTX cb;

    DYNARRAY files;
    DYNARRAY file_stack;
    ASM_FILE *root_file;
    ASM_FILE *current_file;

    char line[ASM_MAX_LINE];
    int line_len;
    const char *cur;
    TOKEN token;

    DYNARRAY defines;
    DYNARRAY predefines;

    SCOPE *root_scope;
    SCOPE *active_scope;
    DYNARRAY scope_stack;
    DYNARRAY *symbol_table;
    DYNARRAY anon_symbols;
    DYNARRAY loop_stack;
    DYNARRAY macros;
    DYNARRAY macro_stack;
    int macro_id;

    DYNARRAY if_stack;
    int if_skip_depth;

    OPCODEINFO opcode_info;
    int valid_opcodes;

    int expression_size;
    int expression_unknown;
    int expression_depth;
    TARGET *active_target;
    DYNARRAY targets;
    const char *strcode;

    ERRORLOG *errorlog;
    int pass;
    int error_log_level;
    const char *current_file_name;
    size_t current_line;
    char *root_dir;
};

static inline uint16_t current_output_address(ASSEMBLER *as) {
    if(!as->active_target || !as->active_target->active_segment) {
        return 0;
    }
    return as->active_target->active_segment->segment_output_address;
}

int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, CB_ASM_CTX *cb);
// Seed a text define that survives both passes (e.g. a build flag). Call after
// assembler_init and before assembler_assemble. `value` may be "" but not NULL.
int assembler_predefine(ASSEMBLER *as, const char *name, const char *value);
int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address);
void assembler_walk_symbols(ASSEMBLER *as, assembler_symbol_cb cb, void *user);
void assembler_shutdown(ASSEMBLER *as);
