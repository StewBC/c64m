// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef struct {
    int was_true;
    int else_seen;
} IF_FRAME;

int is_address(ASSEMBLER *as);
int is_label(ASSEMBLER *as);
int is_opcode(ASSEMBLER *as);
int is_parse_dot_command(ASSEMBLER *as);
int is_variable(ASSEMBLER *as);

void parse_address(ASSEMBLER *as);
void parse_dot_command(ASSEMBLER *as);
void parse_if_skip(ASSEMBLER *as);
void parse_label(ASSEMBLER *as);
int parse_macro_if_is_macro(ASSEMBLER *as);
void parse_opcode(ASSEMBLER *as);
void parse_variable(ASSEMBLER *as);
