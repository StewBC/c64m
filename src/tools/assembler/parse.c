// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

static const char *skip_space(const char *p) {
    while(*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int token_text_is(ASSEMBLER *as, const char *text) {
    size_t len = strlen(text);
    return as->token.name_length == len && 0 == asm_strnicmp(as->token.name, text, len);
}

static char *copy_trimmed_slice(const char *start, int len) {
    while(len > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        len--;
    }
    while(len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }
    char *out = malloc((size_t)len + 1);
    if(!out) {
        return NULL;
    }
    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    return out;
}

static char *copy_token_string(ASSEMBLER *as) {
    if(as->token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, "Expected quoted file name");
        return NULL;
    }
    char *out = malloc((size_t)as->token.name_length + 1);
    if(!out) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory copying string token");
        return NULL;
    }
    memcpy(out, as->token.name, as->token.name_length);
    out[as->token.name_length] = '\0';
    return out;
}

static int next_token_op_is(ASSEMBLER *as, char op) {
    const char *saved_cur = as->cur;
    TOKEN saved_token = as->token;
    get_token(as);
    int matched = as->token.type == TOKEN_OP && as->token.op == op;
    as->cur = saved_cur;
    as->token = saved_token;
    return matched;
}

static void set_current_output_address(ASSEMBLER *as, uint16_t address) {
    SEGMENT *segment = as->active_target ? as->active_target->active_segment : NULL;
    if(!segment) {
        asm_err(as, ASM_ERR_FATAL, "No active segment for address assignment");
        return;
    }
    if(address >= segment->segment_output_address) {
        segment->segment_output_address = address;
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Address set to $%04X but is already at $%04X",
                address, segment->segment_output_address);
    }
}

static int is_register_token(ASSEMBLER *as, char reg) {
    return as->token.type == TOKEN_VAR && as->token.name_length == 1 &&
           tolower((unsigned char)as->token.name[0]) == reg;
}

static int is_implied_instruction(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
    case GPERF_OPCODE_ASL:
    case GPERF_OPCODE_LSR:
    case GPERF_OPCODE_ROL:
    case GPERF_OPCODE_ROR:
        if(is_register_token(as, 'a')) {
            next_token(as);
        }
        return as->token.type == TOKEN_END;
    default:
        return as->opcode_info.width == 0;
    }
}

static int indirect_mode_from_line(ASSEMBLER *as, char *reg) {
    const char *p = as->token.name;
    int depth = 0;
    while(*p) {
        if(*p == '(') {
            depth++;
        } else if(*p == ')') {
            depth--;
            if(depth == 0) {
                const char *q = skip_space(p + 1);
                if(*q == ',') {
                    q = skip_space(q + 1);
                    if((q[0] == 'y' || q[0] == 'Y') && !isalnum((unsigned char)q[1]) && q[1] != '_') {
                        *reg = 'y';
                        return ADDRESS_MODE_INDIRECT_Y;
                    }
                }
                return ADDRESS_MODE_INDIRECT;
            }
        } else if(*p == ',' && depth > 0) {
            const char *q = skip_space(p + 1);
            if((q[0] == 'x' || q[0] == 'X') && !isalnum((unsigned char)q[1]) && q[1] != '_') {
                *reg = 'x';
                return ADDRESS_MODE_INDIRECT_X;
            }
        }
        p++;
    }
    return 0;
}

static void decode_abs_rel_zp_opcode(ASSEMBLER *as) {
    int relative = as->opcode_info.width == 1;
    as->opcode_info.addressing_mode = ADDRESS_MODE_ZEROPAGE;
    if(!relative) {
        if(as->opcode_info.value >= 256 || as->opcode_info.width > 8) {
            as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE;
            as->opcode_info.width = 16;
        }
        if(as->token.op == ',') {
            next_token(as);
            if(is_register_token(as, 'x')) {
                as->opcode_info.addressing_mode++;
            } else if(is_register_token(as, 'y')) {
                if(as->opcode_info.width < 16 && as->opcode_info.addressing_mode == ADDRESS_MODE_ZEROPAGE) {
                    as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE_Y;
                    as->opcode_info.width = 16;
                } else {
                    as->opcode_info.addressing_mode += 2;
                }
            } else {
                asm_err(as, ASM_ERR_RESOLVE, "Unexpected indexed addressing register");
            }
            next_token(as);
        }
    }
    emit_opcode(as);
}

static void dot_org(ASSEMBLER *as) {
    uint64_t value = (uint64_t)expr_full_evaluate(as);
    SEGMENT *segment = as->active_target->active_segment;
    if(segment->segment_output_address > value) {
        asm_err(as, ASM_ERR_RESOLVE, "Assigning address %04llX when address is already %04X error",
                (unsigned long long)value, segment->segment_output_address);
    } else {
        segment->segment_output_address = (uint16_t)value;
        if(!segment->segment_init) {
            segment->segment_start_address = (uint16_t)value;
            segment->segment_init = 1;
        }
    }
}

static void dot_align(ASSEMBLER *as) {
    uint64_t value = (uint64_t)expr_full_evaluate(as);
    if(!value) {
        asm_err(as, ASM_ERR_RESOLVE, ".align value must be greater than zero");
        return;
    }
    uint16_t aligned_address = (uint16_t)((current_output_address(as) + (value - 1)) & ~(value - 1));
    while(current_output_address(as) < aligned_address) {
        emit_byte(as, 0);
    }
}

static void dot_res(ASSEMBLER *as) {
    uint64_t length = (uint64_t)expr_full_evaluate(as);
    if(length > 0x10000ULL - current_output_address(as)) {
        asm_err(as, ASM_ERR_RESOLVE, "Reserving %llX bytes when only %04X remain in 64K",
                (unsigned long long)length, 0x10000 - current_output_address(as));
        return;
    }

    uint64_t value = 0;
    if(as->token.op == ',') {
        value = (uint64_t)expr_full_evaluate(as);
        if(value > 0xFF) {
            asm_err(as, ASM_ERR_RESOLVE, ".res cannot fill with %llX. Only 0x00 - 0xFF allowed",
                    (unsigned long long)value);
            return;
        }
    }

    while(length-- > 0) {
        emit_byte(as, (uint8_t)value);
    }
}

static void dot_byte(ASSEMBLER *as) {
    next_token(as);
    while(as->token.type != TOKEN_END) {
        if(as->token.type == TOKEN_STR) {
            emit_string(as, NULL);
        } else {
            emit_values(as, (uint64_t)expr_evaluate(as), 8, BYTE_ORDER_LO);
        }

        if(as->token.op == ',') {
            next_token(as);
        } else {
            break;
        }
    }
}

static void dot_string(ASSEMBLER *as, int terminate) {
    SYMBOL_LABEL *sl = NULL;
    if(as->strcode) {
        sl = symbol_store_in_scope(as, as->active_scope, "_", 1, SYMBOL_VARIABLE, 0);
    }

    next_token(as);
    while(as->token.type != TOKEN_END) {
        if(as->token.type == TOKEN_STR) {
            emit_string(as, sl);
        } else {
            uint64_t value = (uint64_t)expr_evaluate(as);
            emit_byte(as, (uint8_t)value);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Value %llu not between 0 and 255",
                        (unsigned long long)value);
            }
        }

        if(as->token.op == ',') {
            next_token(as);
        } else {
            break;
        }
    }

    if(terminate) {
        emit_byte(as, 0);
    }
}

static void dot_strcode(ASSEMBLER *as) {
    const char *start = skip_space(as->cur);
    char *copy = copy_trimmed_slice(start, (int)strlen(start));
    if(!copy) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory storing .strcode expression");
        return;
    }
    free((char *)as->strcode);
    as->strcode = copy;
    while(as->token.type != TOKEN_END) {
        next_token(as);
    }
}

static void dot_include(ASSEMBLER *as) {
    next_token(as);
    char *path = copy_token_string(as);
    if(!path) {
        return;
    }
    file_load(as, path);
    free(path);
}

static void dot_incbin(ASSEMBLER *as) {
    next_token(as);
    char *path = copy_token_string(as);
    if(!path) {
        return;
    }

    char *full_path = file_resolve_path(as, path);
    free(path);
    if(!full_path) {
        asm_err(as, ASM_ERR_FATAL, "Unable to resolve .incbin path");
        return;
    }

    FILE *fp = fopen(full_path, "rb");
    if(!fp) {
        asm_err(as, ASM_ERR_FATAL, "Unable to open .incbin file: %s", full_path);
        free(full_path);
        return;
    }

    int c;
    while((c = fgetc(fp)) != EOF) {
        emit_byte(as, (uint8_t)c);
    }
    fclose(fp);
    free(full_path);
}

static void dot_define(ASSEMBLER *as) {
    const char *p = skip_space(as->cur);
    const char *from = p;
    while(*p && *p != ' ' && *p != '\t') {
        p++;
    }
    int from_len = (int)(p - from);
    p = skip_space(p);
    const char *to = p;
    int to_len = (int)strlen(to);
    while(to_len > 0 && (to[to_len - 1] == ' ' || to[to_len - 1] == '\t')) {
        to_len--;
    }

    if(from_len <= 0) {
        asm_err(as, ASM_ERR_DEFINE, ".define requires a source pattern");
        return;
    }
    define_add(as, from, from_len, to, to_len);
    while(as->token.type != TOKEN_END) {
        next_token(as);
    }
}

int is_label(ASSEMBLER *as) {
    if(as->token.type == TOKEN_OP && as->token.op == ':') {
        return 1;
    }
    if(as->token.type != TOKEN_VAR) {
        return 0;
    }
    return next_token_op_is(as, ':');
}

int is_address(ASSEMBLER *as) {
    if(as->token.type != TOKEN_OP || as->token.op != '*') {
        return 0;
    }
    const char *p = skip_space(as->cur);
    return *p == '=' || *p == '+';
}

int is_opcode(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR || as->token.name_length != 3 || as->token.name[0] == '.') {
        return 0;
    }

    OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
    if(!info || info->mnemonic[0] == '.') {
        return 0;
    }
    as->opcode_info = *info;
    return 1;
}

int is_parse_dot_command(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR || as->token.name_length < 2 || as->token.name[0] != '.') {
        return 0;
    }

    OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
    if(!info || info->mnemonic[0] != '.') {
        return 0;
    }
    as->opcode_info = *info;
    return 1;
}

int is_variable(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR || as->token.name[0] == '.') {
        return 0;
    }

    const char *p = skip_space(as->cur);
    return *p == '=' || (p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-');
}

void parse_address(ASSEMBLER *as) {
    next_token(as);
    int relative = 0;
    if(as->token.type == TOKEN_OP && as->token.op == '+') {
        relative = 1;
        next_token(as);
        if(as->token.type == TOKEN_OP && as->token.op == '=') {
            next_token(as);
        }
    } else if(as->token.type == TOKEN_OP && as->token.op == '=') {
        next_token(as);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Address assign error");
        return;
    }

    uint16_t address;
    int64_t value = expr_evaluate(as);
    if(relative) {
        address = current_output_address(as) + value;
    } else {
        address = (uint16_t)value;
        if(as->active_target && as->active_target->active_segment) {
            as->active_target->active_segment->segment_init = 1;
        }
    }

    if(current_output_address(as) > address) {
        asm_err(as, ASM_ERR_RESOLVE, "Assigning address %04X when address is already %04X error",
                address, current_output_address(as));
    } else {
        set_current_output_address(as, address);
    }
}

void parse_dot_command(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
    case GPERF_DOT_6502:
        as->valid_opcodes = 0;
        next_token(as);
        break;
    case GPERF_DOT_65c02:
        as->valid_opcodes = 1;
        next_token(as);
        break;
    case GPERF_DOT_ALIGN:
        dot_align(as);
        break;
    case GPERF_DOT_ASCIIZ:
        dot_string(as, 1);
        break;
    case GPERF_DOT_BYTE:
        dot_byte(as);
        break;
    case GPERF_DOT_DEFINE:
        dot_define(as);
        break;
    case GPERF_DOT_DROW:
        emit_cs_values(as, 16, BYTE_ORDER_HI);
        break;
    case GPERF_DOT_DROWD:
        emit_cs_values(as, 32, BYTE_ORDER_HI);
        break;
    case GPERF_DOT_DROWQ:
        emit_cs_values(as, 64, BYTE_ORDER_HI);
        break;
    case GPERF_DOT_DWORD:
        emit_cs_values(as, 32, BYTE_ORDER_LO);
        break;
    case GPERF_DOT_INCBIN:
        dot_incbin(as);
        break;
    case GPERF_DOT_INCLUDE:
        dot_include(as);
        break;
    case GPERF_DOT_ORG:
        dot_org(as);
        break;
    case GPERF_DOT_QWORD:
        emit_cs_values(as, 64, BYTE_ORDER_LO);
        break;
    case GPERF_DOT_RES:
        dot_res(as);
        break;
    case GPERF_DOT_STRCODE:
        dot_strcode(as);
        break;
    case GPERF_DOT_STRING:
        dot_string(as, 0);
        break;
    case GPERF_DOT_WORD:
        emit_cs_values(as, 16, BYTE_ORDER_LO);
        break;
    case GPERF_DOT_ELSE:
    case GPERF_DOT_ENDIF:
    case GPERF_DOT_IF:
        asm_err(as, ASM_ERR_RESOLVE, "Conditional assembly parser not implemented yet");
        while(as->token.type != TOKEN_END) {
            next_token(as);
        }
        break;
    default:
        asm_err(as, ASM_ERR_RESOLVE, "Dot command %.*s not implemented yet",
                (int)as->token.name_length, as->token.name);
        while(as->token.type != TOKEN_END) {
            next_token(as);
        }
        break;
    }
}

void parse_if_skip(ASSEMBLER *as) {
    as->cur = as->line;
    get_token(as);
    if(as->token.type != TOKEN_VAR || as->token.name_length < 2 || as->token.name[0] != '.') {
        return;
    }

    OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
    if(!info || info->mnemonic[0] != '.') {
        return;
    }

    switch(info->opcode_id) {
    case GPERF_DOT_IF:
        as->if_skip_depth++;
        break;
    case GPERF_DOT_ELSE:
        if(as->if_skip_depth == 1) {
            as->if_skip_depth = 0;
        }
        break;
    case GPERF_DOT_ENDIF:
        if(as->if_skip_depth > 0) {
            as->if_skip_depth--;
        }
        break;
    default:
        break;
    }
}

void parse_label(ASSEMBLER *as) {
    if(as->token.type == TOKEN_OP && as->token.op == ':') {
        if(as->pass == 1) {
            uint16_t address = current_output_address(as);
            ARRAY_ADD(&as->anon_symbols, address);
        }
    } else if(as->token.type == TOKEN_VAR && next_token_op_is(as, ':')) {
        symbol_write(as, as->token.name, as->token.name_length, SYMBOL_ADDRESS, current_output_address(as));
        get_token(as);
    }
}

int parse_macro_if_is_macro(ASSEMBLER *as) {
    (void)as;
    return 0;
}

void parse_opcode(ASSEMBLER *as) {
    next_token(as);
    if(is_implied_instruction(as)) {
        if(!as->valid_opcodes && !asm_opcode_type[as->opcode_info.opcode_id][ADDRESS_MODE_ACCUMULATOR]) {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %.3s with mode %s only valid in 65c02",
                    as->opcode_info.mnemonic, address_mode_txt[ADDRESS_MODE_ACCUMULATOR]);
        }
        emit_byte(as, asm_opcode[as->opcode_info.opcode_id][ADDRESS_MODE_ACCUMULATOR]);
        return;
    }

    int processed = 0;
    switch(as->token.op) {
    case '#':
        as->opcode_info.value = (uint64_t)expr_full_evaluate(as);
        as->opcode_info.addressing_mode = ADDRESS_MODE_IMMEDIATE;
        emit_opcode(as);
        processed = 1;
        break;
    case '(': {
        char reg = 0;
        int indirect = indirect_mode_from_line(as, &reg);
        if(indirect) {
            if(reg == 'x') {
                as->opcode_info.value = (uint64_t)expr_full_evaluate(as);
            } else {
                as->opcode_info.value = (uint64_t)expr_evaluate(as);
            }
            as->opcode_info.addressing_mode = indirect;
            emit_opcode(as);
            if(indirect == ADDRESS_MODE_INDIRECT_X) {
                next_token(as);
                if(!is_register_token(as, 'x')) {
                    asm_err(as, ASM_ERR_RESOLVE, "Expected ,x");
                }
                next_token(as);
                if(as->token.type == TOKEN_OP && as->token.op == ')') {
                    next_token(as);
                }
            } else if(indirect == ADDRESS_MODE_INDIRECT_Y) {
                if(as->token.op == ',') {
                    next_token(as);
                }
                if(!is_register_token(as, 'y')) {
                    asm_err(as, ASM_ERR_RESOLVE, "Expected ,y");
                }
                next_token(as);
            }
            processed = 1;
        }
        break;
    }
    default:
        break;
    }

    if(!processed) {
        if(as->token.type == TOKEN_END) {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %.3s with mode %s expects an operand",
                    as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
            return;
        }
        as->opcode_info.value = (uint64_t)expr_evaluate(as);
        if(as->opcode_info.width >= 8 && as->expression_size > 8) {
            as->opcode_info.width = 16;
        }
        decode_abs_rel_zp_opcode(as);
    }
}

void parse_variable(ASSEMBLER *as) {
    const char *symbol_name = as->token.name;
    uint32_t name_length = as->token.name_length;
    next_token(as);

    if(as->token.type == TOKEN_OP && as->token.op == '=') {
        next_token(as);
        symbol_write(as, symbol_name, name_length, SYMBOL_VARIABLE, (uint64_t)expr_evaluate(as));
    } else if(as->token.type == TOKEN_OP && (as->token.op == '+' || as->token.op == '-')) {
        char op = as->token.op;
        next_token(as);
        if(as->token.type != TOKEN_OP || as->token.op != op) {
            asm_err(as, ASM_ERR_RESOLVE, "Expected %c%c", op, op);
            return;
        }
        SYMBOL_LABEL *sl = symbol_read(as, symbol_name, name_length);
        uint64_t value = sl ? sl->symbol_value : 0;
        if(op == '+') {
            value++;
        } else {
            value--;
        }
        symbol_write(as, symbol_name, name_length, SYMBOL_VARIABLE, value);
        next_token(as);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Expected assignment operator");
    }
}
