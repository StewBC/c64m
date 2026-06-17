// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

char *set_name(char **s, const char *name, const int name_length) {
    *s = malloc((size_t)name_length + 1);
    if(*s) {
        memcpy(*s, name, (size_t)name_length);
        (*s)[name_length] = '\0';
    }
    return *s;
}

static void scope_to_scope(ASSEMBLER *as, SCOPE *s) {
    as->active_scope = s;
    as->symbol_table = s->symbol_table;
}

void scope_push(ASSEMBLER *as, SCOPE *next) {
    ARRAY_ADD(&as->scope_stack, as->active_scope);
    scope_to_scope(as, next);
}

int scope_pop(ASSEMBLER *as) {
    if(as->scope_stack.items == 0) {
        return 0;
    }
    SCOPE *prev = *ARRAY_GET(&as->scope_stack, SCOPE*, as->scope_stack.items - 1);
    as->scope_stack.items--;
    scope_to_scope(as, prev);
    return 1;
}

SCOPE *scope_find_child(SCOPE *parent, const char *name, int name_length) {
    for(size_t si = 0; si < parent->child_scopes.items; si++) {
        SCOPE *s = *ARRAY_GET(&parent->child_scopes, SCOPE*, si);
        if(name_length == s->scope_name_length && 0 == asm_strnicmp(name, s->scope_name, (size_t)name_length)) {
            return s;
        }
    }
    return NULL;
}

int scope_init(SCOPE *s, int type) {
    memset(s, 0, sizeof(SCOPE));
    ARRAY_INIT(&s->child_scopes, SCOPE*);
    s->symbol_table = malloc(sizeof(DYNARRAY) * HASH_BUCKETS);
    if(!s->symbol_table) {
        return ASM_ERR;
    }

    for(int bucket = 0; bucket < HASH_BUCKETS; bucket++) {
        ARRAY_INIT(&s->symbol_table[bucket], SYMBOL_LABEL);
    }

    s->scope_type = type;
    return ASM_OK;
}

void scope_destroy(SCOPE *s) {
    if(!s) {
        return;
    }
    while(s->child_scopes.items) {
        scope_destroy(*ARRAY_GET(&s->child_scopes, SCOPE*, s->child_scopes.items - 1));
        s->child_scopes.items--;
    }
    array_free(&s->child_scopes);
    free(s->scope_name);
    if(s->symbol_table) {
        for(int bucket = 0; bucket < HASH_BUCKETS; bucket++) {
            for(size_t i = 0; i < s->symbol_table[bucket].items; i++) {
                SYMBOL_LABEL *sl = ARRAY_GET(&s->symbol_table[bucket], SYMBOL_LABEL, i);
                free((char *)sl->symbol_name);
            }
            array_free(&s->symbol_table[bucket]);
        }
    }
    free(s->symbol_table);
    free(s);
}

SCOPE *scope_add(ASSEMBLER *as, const char *name, const int name_length, SCOPE *parent, int type) {
    (void)as;
    SCOPE *s = malloc(sizeof(SCOPE));
    if(!s || ASM_OK != scope_init(s, type)) {
        free(s);
        return NULL;
    }
    if(!set_name(&s->scope_name, name, name_length)) {
        scope_destroy(s);
        return NULL;
    }
    s->scope_name_length = name_length;
    s->parent_scope = parent;
    s->scope_type = type;
    if(ASM_OK != ARRAY_ADD(&parent->child_scopes, s)) {
        scope_destroy(s);
        return NULL;
    }
    return *ARRAY_GET(&parent->child_scopes, SCOPE*, parent->child_scopes.items - 1);
}

void scope_reset_ids(SCOPE *s) {
    if(!s) {
        return;
    }
    s->anon_scope_id = 0;
    for(size_t child_id = 0; child_id < s->child_scopes.items; child_id++) {
        scope_reset_ids(*ARRAY_GET(&s->child_scopes, SCOPE*, child_id));
    }
}
