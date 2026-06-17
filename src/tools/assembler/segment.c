// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

SEGMENT *segment_find(DYNARRAY *segments, const SEGMENT *seg) {
    for(size_t si = 0; si < segments->items; si++) {
        SEGMENT *s = *ARRAY_GET(segments, SEGMENT*, si);
        if(s->segment_name_length == seg->segment_name_length &&
           0 == asm_strnicmp(s->segment_name, seg->segment_name, seg->segment_name_length)) {
            return s;
        }
    }
    return NULL;
}

TARGET *add_target(ASSEMBLER *as) {
    SEGMENT *segment = malloc(sizeof(SEGMENT));
    if(!segment) {
        return NULL;
    }

    TARGET *target = malloc(sizeof(TARGET));
    if(!target) {
        free(segment);
        return NULL;
    }

    memset(segment, 0, sizeof(SEGMENT));
    memset(target, 0, sizeof(TARGET));

    ARRAY_INIT(&target->segments, SEGMENT*);
    if(ASM_OK != ARRAY_ADD(&target->segments, segment)) {
        array_free(&target->segments);
        free(segment);
        free(target);
        return NULL;
    }
    target->active_segment = segment;
    if(ASM_OK != ARRAY_ADD(&as->targets, target)) {
        array_free(&target->segments);
        free(segment);
        free(target);
        return NULL;
    }
    return target;
}

void targets_free(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->targets.items; i++) {
        TARGET *t = *ARRAY_GET(&as->targets, TARGET*, i);
        if(!t) {
            continue;
        }
        for(size_t j = 0; j < t->segments.items; j++) {
            SEGMENT *s = *ARRAY_GET(&t->segments, SEGMENT*, j);
            free(s);
        }
        array_free(&t->segments);
        free(t);
    }
    array_free(&as->targets);
    as->active_target = NULL;
}
