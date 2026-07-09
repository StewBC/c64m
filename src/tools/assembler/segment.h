// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef struct {
    const char *segment_name;
    uint32_t segment_name_length;
    uint64_t segment_name_hash;
    uint16_t segment_start_address;
    uint16_t segment_output_address;
    int do_not_emit;
    int segment_init;
} SEGMENT;

#ifndef ASM_TARGET_TYPEDEF
#define ASM_TARGET_TYPEDEF
typedef struct TARGET TARGET;
#endif

struct TARGET {
    DYNARRAY segments;
    SEGMENT *active_segment;
    void *ctx;              // per-target output context passed to output_byte
    struct TARGET *parent;  // target that was active when this one opened (NULL for default)
};

SEGMENT *segment_find(DYNARRAY *segments, const SEGMENT *seg);
TARGET *add_target(ASSEMBLER *as, void *ctx);
void targets_free(ASSEMBLER *as);
