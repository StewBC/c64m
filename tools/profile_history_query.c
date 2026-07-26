#include "runtime_history.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    PROFILE_RECORD_COUNT = 9500000
};

static double elapsed_ms(clock_t start, clock_t end) {
    return (double)(end - start) * 1000.0 / (double)CLOCKS_PER_SEC;
}

static bool append_record(runtime_history *history, uint64_t index) {
    static const uint8_t opcodes[] = { 0xa9u, 0xeau, 0x8du, 0x4cu };
    runtime_history_begin begin;
    uint16_t pc = (uint16_t)(0x0800u + (index & 0x0fffu));
    uint64_t cycle = index * 4u;

    memset(&begin, 0, sizeof(begin));
    begin.kind = RUNTIME_HISTORY_RECORD_INSTRUCTION;
    begin.machine_cycle = cycle;
    begin.pc = pc;
    begin.sp = 0xfdu;
    begin.p = 0x24u;
    return runtime_history_begin_record(history, &begin) &&
        runtime_history_append_access(
            history,
            C6510_BUS_ACCESS_OPCODE_FETCH,
            pc,
            opcodes[index % (sizeof(opcodes) / sizeof(opcodes[0]))],
            cycle) &&
        runtime_history_append_access(
            history,
            (index & 1u) != 0u ?
                C6510_BUS_ACCESS_DATA_WRITE :
                C6510_BUS_ACCESS_DATA_READ,
            (uint16_t)(0x2000u + (index & 0x00ffu)),
            (uint8_t)index,
            cycle + 2u) &&
        runtime_history_complete_record(history);
}

static bool run_query(
    const char *name,
    runtime_history *history,
    runtime_history_query *query,
    uint64_t from_id,
    size_t limit) {
    runtime_history_record records[RUNTIME_HISTORY_MAX_QUERY_RECORDS];
    runtime_history_page page;
    runtime_history_query_stats stats;
    runtime_history_query_result result;
    clock_t start = clock();
    clock_t end;

    result = runtime_history_find(
        history, query, from_id, limit, records, &page, &stats);
    end = clock();
    if (result != RUNTIME_HISTORY_QUERY_OK) {
        fprintf(stderr, "%s failed: %d\n", name, (int)result);
        return false;
    }
    printf(
        "%s ms=%.3f matches=%zu more=%u blocks=%llu "
        "records=%llu bytes=%llu\n",
        name,
        elapsed_ms(start, end),
        page.count,
        page.more ? 1u : 0u,
        (unsigned long long)stats.blocks_visited,
        (unsigned long long)stats.records_decoded,
        (unsigned long long)stats.bytes_scanned);
    return true;
}

int main(void) {
    runtime_history *history = runtime_history_create(
        (size_t)RUNTIME_HISTORY_DEFAULT_MEMORY_MB * 1024u * 1024u);
    runtime_history_status status;
    runtime_history_query query;
    uint64_t i;

    if (history == NULL) {
        fprintf(stderr, "history allocation failed\n");
        return 1;
    }
    for (i = 0u; i < PROFILE_RECORD_COUNT; ++i) {
        if (!append_record(history, i)) {
            fprintf(stderr, "append failed at %llu\n",
                    (unsigned long long)i);
            runtime_history_destroy(history);
            return 1;
        }
    }
    runtime_history_get_status(history, &status);
    printf(
        "retained=%llu used_bytes=%zu capacity_bytes=%zu "
        "oldest=%llu newest=%llu wraps=%llu\n",
        (unsigned long long)status.record_count,
        status.used_bytes,
        status.capacity_bytes,
        (unsigned long long)status.oldest_id,
        (unsigned long long)status.newest_id,
        (unsigned long long)status.wrap_count);

    memset(&query, 0, sizeof(query));
    query.has_address = true;
    query.address_first = query.address_last = 0x20ffu;
    query.has_access = true;
    query.access_mask = RUNTIME_HISTORY_ACCESS_DATA_WRITE;
    if (!run_query(
            "address-near-newest", history, &query,
            status.newest_id, 1u)) {
        runtime_history_destroy(history);
        return 1;
    }

    query.address_first = query.address_last = 0xdeadu;
    if (!run_query("address-full-miss", history, &query, 0u, 1u)) {
        runtime_history_destroy(history);
        return 1;
    }

    memset(&query, 0, sizeof(query));
    query.has_pc = true;
    query.pc_first = 0x0f00u;
    query.pc_last = 0x0f0fu;
    if (!run_query("pc-range", history, &query, 0u, 256u)) {
        runtime_history_destroy(history);
        return 1;
    }

    memset(&query, 0, sizeof(query));
    query.opcode_pattern_length = 3u;
    query.opcode_pattern[0].value = 0xa9u;
    query.opcode_pattern[0].mask = 0xffu;
    query.opcode_pattern[1].value = 0u;
    query.opcode_pattern[1].mask = 0u;
    query.opcode_pattern[2].value = 0x8du;
    query.opcode_pattern[2].mask = 0xffu;
    if (!run_query("opcode-pattern", history, &query, 0u, 256u)) {
        runtime_history_destroy(history);
        return 1;
    }

    runtime_history_destroy(history);
    return 0;
}
