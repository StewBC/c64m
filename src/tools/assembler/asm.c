// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

static void strip_comment(char *line, int *line_len) {
    int in_string = 0;
    for(int i = 0; i < *line_len; i++) {
        if(in_string) {
            if(line[i] == '\\' && i + 1 < *line_len) {
                i++;
            } else if(line[i] == '"') {
                in_string = 0;
            }
        } else if(line[i] == '"') {
            in_string = 1;
        } else if(line[i] == ';') {
            line[i] = '\0';
            *line_len = i;
            return;
        }
    }
}

static void reset_targets_for_pass(ASSEMBLER *as) {
    for(size_t i = 0; i < as->targets.items; i++) {
        TARGET *target = *ARRAY_GET(&as->targets, TARGET*, i);
        if(!target) {
            continue;
        }
        for(size_t j = 0; j < target->segments.items; j++) {
            SEGMENT *segment = *ARRAY_GET(&target->segments, SEGMENT*, j);
            segment->segment_output_address = segment->segment_start_address;
        }
        if(target->segments.items > 0) {
            target->active_segment = *ARRAY_GET(&target->segments, SEGMENT*, 0);
        }
    }
    if(as->targets.items > 0) {
        as->active_target = *ARRAY_GET(&as->targets, TARGET*, 0);
    }
}

static void reset_source_for_assemble(ASSEMBLER *as) {
    files_free(as);
    ARRAY_INIT(&as->files, ASM_FILE*);
    ARRAY_INIT(&as->file_stack, FILE_FRAME);
    as->root_file = NULL;
    as->current_file = NULL;
}

static void seed_predefines(ASSEMBLER *as) {
    for(size_t i = 0; i < as->predefines.items; i++) {
        DEFINE *d = ARRAY_GET(&as->predefines, DEFINE, i);
        define_add(as, d->from, d->from_len, d->to, d->to_len);
    }
}

static void reset_pass_state(ASSEMBLER *as) {
    reset_targets_for_pass(as);
    scope_reset_ids(as->root_scope);
    as->active_scope = as->root_scope;
    as->symbol_table = as->root_scope ? as->root_scope->symbol_table : NULL;
    as->scope_stack.items = 0;
    loop_stack_clear(as);
    macro_stack_clear(as);
    macro_definitions_clear(as);
    as->macro_id = 0;
    defines_free(as);
    seed_predefines(as);
    free((char *)as->strcode);
    as->strcode = NULL;
    as->if_stack.items = 0;
    as->if_skip_depth = 0;
    as->expression_depth = 0;
    as->expression_unknown = 0;
    as->cur = NULL;
    memset(&as->token, 0, sizeof(as->token));
}

static void parse_line(ASSEMBLER *as) {
    get_token(as);
    if(as->token.type == TOKEN_END) {
        return;
    }

    if(is_label(as)) {
        parse_label(as);
        get_token(as);
        if(as->token.type == TOKEN_END) {
            return;
        }
    }

    if(is_address(as)) {
        parse_address(as);
        return;
    }

    if(is_opcode(as)) {
        parse_opcode(as);
        return;
    }

    if(is_parse_dot_command(as)) {
        parse_dot_command(as);
        return;
    }

    if(as->token.type == TOKEN_VAR && parse_macro_if_is_macro(as)) {
        return;
    }

    if(is_variable(as)) {
        parse_variable(as);
        return;
    }

    if(as->token.type == TOKEN_VAR || as->token.type == TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token: %.*s",
                (int)as->token.name_length, as->token.name);
    } else if(as->token.name) {
        asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token: %.*s",
                (int)as->token.name_length, as->token.name);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token");
    }
}

static void asm_log_direct(ASSEMBLER *as, const char *fmt, ...) {
    if(!as || !as->errorlog) {
        return;
    }
    ERROR_ENTRY e;
    memset(&e, 0, sizeof(e));
    e.err_str = malloc(ASM_ERR_MAX_STR_LEN);
    if(!e.err_str) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    e.message_length = (size_t)vsnprintf(e.err_str, ASM_ERR_MAX_STR_LEN, fmt, args);
    va_end(args);
    errlog(as->errorlog, &e);
}

#define OVERLAP_MAX_SEGS 64

static void check_segment_overlaps(ASSEMBLER *as) {
    for(size_t ti = 0; ti < as->targets.items; ti++) {
        TARGET *target = *ARRAY_GET(&as->targets, TARGET*, ti);
        if(!target) {
            continue;
        }

        SEGMENT *segs[OVERLAP_MAX_SEGS];
        int count = 0;

        for(size_t si = 0; si < target->segments.items && count < OVERLAP_MAX_SEGS; si++) {
            SEGMENT *s = *ARRAY_GET(&target->segments, SEGMENT*, si);
            if(s->do_not_emit) {
                continue;
            }
            if(s->segment_output_address == s->segment_start_address) {
                continue;
            }
            segs[count++] = s;
        }

        if(count < 2) {
            continue;
        }

        // Stable insertion sort by start address (preserves definition order for ties)
        for(int i = 1; i < count; i++) {
            SEGMENT *key = segs[i];
            int j = i - 1;
            while(j >= 0 && segs[j]->segment_start_address > key->segment_start_address) {
                segs[j + 1] = segs[j];
                j--;
            }
            segs[j + 1] = key;
        }

        int issues = 0;

        // Wrap-around: output_address < start_address in uint16 arithmetic
        for(int i = 0; i < count; i++) {
            if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                const char *name = segs[i]->segment_name ? segs[i]->segment_name : "<default>";
                asm_log_direct(as, "Segment \"%.*s\" wraps past $FFFF (start $%04X end $%04X)",
                    (int)segs[i]->segment_name_length, name,
                    segs[i]->segment_start_address, segs[i]->segment_output_address);
                issues++;
            }
        }

        // Overlap: for each pair (i,j) with i<j in sorted order, b_start < a_end means overlap
        for(int i = 0; i < count - 1; i++) {
            if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                continue;
            }
            for(int j = i + 1; j < count; j++) {
                if(segs[j]->segment_output_address < segs[j]->segment_start_address) {
                    continue;
                }
                if(segs[j]->segment_start_address < segs[i]->segment_output_address) {
                    const char *na = segs[i]->segment_name ? segs[i]->segment_name : "<default>";
                    const char *nb = segs[j]->segment_name ? segs[j]->segment_name : "<default>";
                    asm_log_direct(as,
                        "Segment \"%.*s\" [$%04X..$%04X) overlaps \"%.*s\" [$%04X..$%04X)",
                        (int)segs[i]->segment_name_length, na,
                        segs[i]->segment_start_address, segs[i]->segment_output_address,
                        (int)segs[j]->segment_name_length, nb,
                        segs[j]->segment_start_address, segs[j]->segment_output_address);
                    issues++;
                }
            }
        }

        if(issues > 0) {
            asm_log_direct(as, "Segments overlap -- suggested addresses:");
            uint16_t next_addr = segs[0]->segment_start_address;
            for(int i = 0; i < count; i++) {
                if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                    continue;
                }
                const char *name = segs[i]->segment_name ? segs[i]->segment_name : "<default>";
                uint16_t size = segs[i]->segment_output_address - segs[i]->segment_start_address;
                asm_log_direct(as, "  Suggest \"%.*s\" at $%04X",
                    (int)segs[i]->segment_name_length, name, next_addr);
                next_addr += size;
            }
        }
    }
}

int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, CB_ASM_CTX *cb) {
    if(!as || !errorlog || !cb || !cb->output_byte) {
        return ASM_ERR;
    }
    memset(as, 0, sizeof(*as));
    as->cb = *cb;
    as->errorlog = errorlog;
    as->error_log_level = 0;
    ARRAY_INIT(&as->files, ASM_FILE*);
    ARRAY_INIT(&as->file_stack, FILE_FRAME);
    ARRAY_INIT(&as->defines, DEFINE);
    ARRAY_INIT(&as->scope_stack, SCOPE*);
    ARRAY_INIT(&as->anon_symbols, uint16_t);
    ARRAY_INIT(&as->loop_stack, LOOP);
    ARRAY_INIT(&as->macros, MACRO);
    ARRAY_INIT(&as->macro_stack, MACRO_EXPAND);
    ARRAY_INIT(&as->if_stack, IF_FRAME);
    ARRAY_INIT(&as->targets, TARGET*);
    ARRAY_INIT(&as->predefines, DEFINE);
    as->valid_opcodes = 0;

    as->root_scope = malloc(sizeof(SCOPE));
    if(!as->root_scope || ASM_OK != scope_init(as->root_scope, 0)) {
        free(as->root_scope);
        as->root_scope = NULL;
        assembler_shutdown(as);
        return ASM_ERR;
    }
    as->active_scope = as->root_scope;
    as->symbol_table = as->root_scope->symbol_table;
    // The default (unnamed) target routes emitted bytes to the host's default_target
    // context; fall back to `user` so a host that only sets user+output_byte still works.
    as->active_target = add_target(as, cb->default_target ? cb->default_target : cb->user);
    if(!as->active_target) {
        assembler_shutdown(as);
        return ASM_ERR;
    }
    return ASM_OK;
}

int assembler_predefine(ASSEMBLER *as, const char *name, const char *value) {
    if(!as || !name || name[0] == '\0' || !value) {
        return ASM_ERR;
    }
    DEFINE d;
    memset(&d, 0, sizeof(d));
    d.from_len = (int)strlen(name);
    d.to_len = (int)strlen(value);
    d.from = malloc((size_t)d.from_len + 1);
    d.to = malloc((size_t)d.to_len + 1);
    if(!d.from || !d.to) {
        free(d.from);
        free(d.to);
        return ASM_ERR;
    }
    memcpy(d.from, name, (size_t)d.from_len + 1);
    memcpy(d.to, value, (size_t)d.to_len + 1);
    if(ASM_OK != ARRAY_ADD(&as->predefines, d)) {
        free(d.from);
        free(d.to);
        return ASM_ERR;
    }
    return ASM_OK;
}

int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address) {
    if(!as || !input_file || !as->active_target || !as->active_target->active_segment) {
        return ASM_ERR;
    }

    size_t initial_errors = as->errorlog ? as->errorlog->log_array.items : 0;

    reset_source_for_assemble(as);
    as->anon_symbols.items = 0;

    SEGMENT *default_segment = as->active_target->active_segment;
    default_segment->segment_start_address = address;
    default_segment->segment_output_address = address;
    default_segment->segment_init = 1;

    for(as->pass = 1; as->pass <= 2; as->pass++) {
        reset_pass_state(as);

        if(as->pass == 1) {
            if(ASM_OK != file_load(as, input_file)) {
                return ASM_ERR;
            }
            free(as->root_dir);
            as->root_dir = NULL;
            if(as->root_file && as->root_file->display_name) {
                const char *slash = strrchr(as->root_file->display_name, '/');
                if(slash) {
                    size_t dir_len = (size_t)(slash - as->root_file->display_name + 1);
                    as->root_dir = malloc(dir_len + 1);
                    if(as->root_dir) {
                        memcpy(as->root_dir, as->root_file->display_name, dir_len);
                        as->root_dir[dir_len] = '\0';
                    }
                }
            }
        } else if(ASM_OK != file_stack_reset_for_pass2(as)) {
            return ASM_ERR;
        }

        while(as->file_stack.items > 0) {
            if(!file_read_line(as)) {
                file_stack_pop(as);
                continue;
            }

            strip_comment(as->line, &as->line_len);
            if(as->if_skip_depth > 0) {
                parse_if_skip(as);
                continue;
            }

            macro_substitute_line(as);
            define_substitute(as);
            as->cur = as->line;
            parse_line(as);
        }

        if(as->scope_stack.items > 0) {
            asm_err(as, ASM_ERR_RESOLVE, "Unclosed scope at end of assembly");
        }
        if(as->loop_stack.items > 0) {
            asm_err(as, ASM_ERR_RESOLVE, "Unclosed loop at end of assembly");
        }
        if(as->if_stack.items > 0 || as->if_skip_depth > 0) {
            asm_err(as, ASM_ERR_RESOLVE, "Unclosed conditional assembly block");
        }
    }

    if(!as->errorlog || as->errorlog->log_array.items == initial_errors) {
        check_segment_overlaps(as);
    }

    return as->errorlog && as->errorlog->log_array.items > initial_errors ? ASM_ERR : ASM_OK;
}

static int assembler_symbol_is_macro_local(const SYMBOL_LABEL *symbol) {
    return symbol->symbol_length == GEN_NAME_LEN &&
        0 == strncmp(symbol->symbol_name, "__macro_local_", 14);
}

static int assembler_symbol_append(char *out, size_t out_size, size_t *len, const char *text, size_t text_len) {
    if(*len + text_len >= out_size) {
        return 0;
    }
    memcpy(out + *len, text, text_len);
    *len += text_len;
    out[*len] = '\0';
    return 1;
}

static void assembler_walk_scope_symbols(SCOPE *scope, char *prefix, size_t prefix_len, assembler_symbol_cb cb, void *user) {
    char name[512];

    if(!scope || !cb) {
        return;
    }

    for(int bucket = 0; bucket < HASH_BUCKETS; bucket++) {
        DYNARRAY *symbols = &scope->symbol_table[bucket];
        for(size_t i = 0; i < symbols->items; i++) {
            SYMBOL_LABEL *symbol = ARRAY_GET(symbols, SYMBOL_LABEL, i);
            size_t name_len = 0;
            if(symbol->symbol_type != SYMBOL_ADDRESS || assembler_symbol_is_macro_local(symbol)) {
                continue;
            }
            if(!assembler_symbol_append(name, sizeof(name), &name_len, prefix, prefix_len)) {
                continue;
            }
            if(!assembler_symbol_append(name, sizeof(name), &name_len, symbol->symbol_name, symbol->symbol_length)) {
                continue;
            }
            cb(name, (uint16_t)symbol->symbol_value, user);
        }
    }

    for(size_t i = 0; i < scope->child_scopes.items; i++) {
        SCOPE *child = *ARRAY_GET(&scope->child_scopes, SCOPE*, i);
        char child_prefix[512];
        size_t child_prefix_len = prefix_len;
        memcpy(child_prefix, prefix, prefix_len);
        child_prefix[child_prefix_len] = '\0';
        if(!assembler_symbol_append(child_prefix, sizeof(child_prefix), &child_prefix_len, child->scope_name, (size_t)child->scope_name_length)) {
            continue;
        }
        if(!assembler_symbol_append(child_prefix, sizeof(child_prefix), &child_prefix_len, "::", 2)) {
            continue;
        }
        assembler_walk_scope_symbols(child, child_prefix, child_prefix_len, cb, user);
    }
}

void assembler_walk_symbols(ASSEMBLER *as, assembler_symbol_cb cb, void *user) {
    char prefix[1] = {0};

    if(!as || !cb || !as->root_scope) {
        return;
    }
    assembler_walk_scope_symbols(as->root_scope, prefix, 0, cb, user);
}

void assembler_shutdown(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    free(as->root_dir);
    as->root_dir = NULL;
    defines_free(as);
    for(size_t i = 0; i < as->predefines.items; i++) {
        DEFINE *d = ARRAY_GET(&as->predefines, DEFINE, i);
        free(d->from);
        free(d->to);
    }
    array_free(&as->predefines);
    macro_stack_clear(as);
    macro_definitions_clear(as);
    files_free(as);
    scope_destroy(as->root_scope);
    as->root_scope = NULL;
    as->active_scope = NULL;
    as->symbol_table = NULL;
    array_free(&as->scope_stack);
    array_free(&as->anon_symbols);
    loop_stack_clear(as);
    array_free(&as->loop_stack);
    array_free(&as->macros);
    array_free(&as->macro_stack);
    array_free(&as->if_stack);
    targets_free(as);
    free((char *)as->strcode);
    as->strcode = NULL;
}
