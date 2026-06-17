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

static void reset_pass_state(ASSEMBLER *as) {
    reset_targets_for_pass(as);
    scope_reset_ids(as->root_scope);
    as->active_scope = as->root_scope;
    as->symbol_table = as->root_scope ? as->root_scope->symbol_table : NULL;
    as->scope_stack.items = 0;
    as->macro_stack.items = 0;
    defines_free(as);
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

    if(is_variable(as)) {
        if(parse_macro_if_is_macro(as)) {
            return;
        }
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
    ARRAY_INIT(&as->macro_stack, MACRO_EXPAND);
    ARRAY_INIT(&as->if_stack, IF_FRAME);
    ARRAY_INIT(&as->targets, TARGET*);
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
    as->active_target = add_target(as);
    if(!as->active_target) {
        assembler_shutdown(as);
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

            define_substitute(as);
            as->cur = as->line;
            parse_line(as);
        }

        if(as->scope_stack.items > 0) {
            asm_err(as, ASM_ERR_RESOLVE, "Unclosed scope at end of assembly");
        }
        if(as->if_stack.items > 0 || as->if_skip_depth > 0) {
            asm_err(as, ASM_ERR_RESOLVE, "Unclosed conditional assembly block");
        }
    }

    return as->errorlog && as->errorlog->log_array.items > initial_errors ? ASM_ERR : ASM_OK;
}

void assembler_shutdown(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    defines_free(as);
    files_free(as);
    scope_destroy(as->root_scope);
    as->root_scope = NULL;
    as->active_scope = NULL;
    as->symbol_table = NULL;
    array_free(&as->scope_stack);
    array_free(&as->anon_symbols);
    array_free(&as->macro_stack);
    array_free(&as->if_stack);
    targets_free(as);
    free((char *)as->strcode);
    as->strcode = NULL;
}
