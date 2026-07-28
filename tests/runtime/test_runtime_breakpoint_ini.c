/* Round-trip guarded breakpoints through the DEBUG section of an .ini.
 *
 * The .ini breakpoint value is itself a comma-separated item list, so a
 * condition is persisted with ';' between its terms. This test drives the real
 * save and load entry points rather than a reimplementation of the format, so
 * a serializer that silently drops the guard fails here.
 */
#include "runtime_breakpoint_ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures++; \
        } \
    } while (0)

static void seed_breakpoint(
    runtime *rt,
    const char *when,
    uint16_t address,
    uint32_t access) {
    runtime_breakpoint *breakpoint = &rt->breakpoints[rt->breakpoint_count];
    char error[128];

    memset(breakpoint, 0, sizeof(*breakpoint));
    breakpoint->id = (uint32_t)(rt->breakpoint_count + 1u);
    breakpoint->enabled = true;
    breakpoint->start_address = address;
    breakpoint->end_address = address;
    breakpoint->access_mask = access;
    breakpoint->mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
    breakpoint->action_mask = RUNTIME_BREAKPOINT_ACTION_BREAK;
    breakpoint->reset_count = 1u;
    if (when != NULL) {
        CHECK(runtime_bp_condition_parse(
            when, &breakpoint->condition, error, sizeof(error)));
    }
    rt->breakpoint_count++;
}

int main(void) {
    runtime *rt = calloc(1u, sizeof(*rt));
    char path[] = "/tmp/c64m-bp-ini-XXXXXX";
    int fd;

    if (rt == NULL) {
        fprintf(stderr, "allocation failed\n");
        return 1;
    }

    fd = mkstemp(path);
    if (fd < 0) {
        fprintf(stderr, "could not create a temporary ini\n");
        free(rt);
        return 1;
    }
    close(fd);

    rt->ini_path = path;
    rt->use_ini = true;
    rt->save_ini = true;

    /* A two-term guard, a single-term guard, and an unguarded breakpoint. */
    seed_breakpoint(rt, "value!&1,mem($D000)>$F0", 0xD010u,
                    RUNTIME_BREAKPOINT_ACCESS_WRITE);
    seed_breakpoint(rt, "i==1", 0xD021u, RUNTIME_BREAKPOINT_ACCESS_WRITE);
    seed_breakpoint(rt, NULL, 0xC000u, RUNTIME_BREAKPOINT_ACCESS_EXECUTE);

    CHECK(runtime_save_breakpoints_to_ini(rt));

    /* Drop everything and reload from the file just written. */
    memset(rt->breakpoints, 0, sizeof(rt->breakpoints));
    rt->breakpoint_count = 0;
    rt->next_breakpoint_id = 0;

    CHECK(runtime_load_breakpoints_from_ini(rt));
    CHECK(rt->breakpoint_count == 3u);

    if (rt->breakpoint_count == 3u) {
        size_t i;
        bool saw_two_term = false;
        bool saw_one_term = false;
        bool saw_unguarded = false;

        for (i = 0; i < rt->breakpoint_count; ++i) {
            const runtime_breakpoint *bp = &rt->breakpoints[i];
            char text[RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX];

            CHECK(runtime_bp_condition_format(
                &bp->condition, text, sizeof(text)));

            if (bp->start_address == 0xD010u) {
                saw_two_term = true;
                CHECK(bp->condition.term_count == 2u);
                CHECK(bp->condition.terms[0].lhs == RUNTIME_BP_LHS_VALUE);
                CHECK(bp->condition.terms[0].op == RUNTIME_BP_OP_MASK_CLEAR);
                CHECK(bp->condition.terms[0].imm == 1u);
                CHECK(bp->condition.terms[1].lhs == RUNTIME_BP_LHS_MEM);
                CHECK(bp->condition.terms[1].mem_address == 0xD000u);
                CHECK(bp->condition.terms[1].op == RUNTIME_BP_OP_GT);
                CHECK(bp->condition.terms[1].imm == 0xF0u);
            } else if (bp->start_address == 0xD021u) {
                saw_one_term = true;
                CHECK(bp->condition.term_count == 1u);
                CHECK(bp->condition.terms[0].lhs == RUNTIME_BP_LHS_FLAG_I);
                CHECK(bp->condition.terms[0].op == RUNTIME_BP_OP_EQ);
                CHECK(bp->condition.terms[0].imm == 1u);
            } else if (bp->start_address == 0xC000u) {
                saw_unguarded = true;
                CHECK(bp->condition.term_count == 0u);
                CHECK(text[0] == '\0');
            }
        }
        CHECK(saw_two_term);
        CHECK(saw_one_term);
        CHECK(saw_unguarded);
    }

    remove(path);
    free(rt);

    if (failures != 0) {
        fprintf(stderr, "test_runtime_breakpoint_ini: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_runtime_breakpoint_ini: ok\n");
    return 0;
}
