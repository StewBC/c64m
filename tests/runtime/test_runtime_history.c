#include "runtime_history.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            return false; \
        } \
    } while (0)

enum {
    TEST_BLOCK_SIZE = 512,
    TEST_REQUEST_BYTES = 4096
};

static runtime_history_begin instruction_at(uint16_t pc, uint64_t cycle) {
    runtime_history_begin begin;
    memset(&begin, 0, sizeof(begin));
    begin.kind = RUNTIME_HISTORY_RECORD_INSTRUCTION;
    begin.pc = pc;
    begin.machine_cycle = cycle;
    begin.a = 0x11u;
    begin.x = 0x22u;
    begin.y = 0x33u;
    begin.sp = 0xfdu;
    begin.p = 0x24u;
    return begin;
}

static bool append_instruction(
    runtime_history *history,
    uint16_t pc,
    uint64_t cycle,
    unsigned nonfetch_accesses) {
    runtime_history_begin begin = instruction_at(pc, cycle);
    unsigned i;

    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPCODE_FETCH, pc, 0xeau, cycle));
    for (i = 0u; i < nonfetch_accesses; ++i) {
        CHECK(runtime_history_append_access(
            history,
            C6510_BUS_ACCESS_DATA_READ,
            (uint16_t)(0x2000u + i),
            (uint8_t)i,
            cycle + 1u + i));
    }
    CHECK(runtime_history_complete_record(history));
    return true;
}

static bool test_empty_store_and_first_epoch(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_status status;

    CHECK(history != NULL);
    runtime_history_get_status(history, &status);
    CHECK(status.available);
    CHECK(status.recording);
    CHECK(status.epoch == 1u);
    CHECK(status.timeline == 0u);
    CHECK(status.record_count == 0u);
    CHECK(status.oldest_id == 0u);
    CHECK(status.newest_id == 0u);
    runtime_history_destroy(history);
    return true;
}

static void *failing_alloc(size_t size, void *user) {
    (void)size;
    (void)user;
    return NULL;
}

static bool test_unavailable_construction(void) {
    runtime_history_allocator allocator = {
        .alloc = failing_alloc,
    };
    runtime_history *history =
        runtime_history_create_ex(
            TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, &allocator);
    runtime_history_status status;

    CHECK(history != NULL);
    runtime_history_get_status(history, &status);
    CHECK(!status.available);
    CHECK(!status.recording);
    CHECK(status.unavailable_reason ==
          RUNTIME_HISTORY_UNAVAILABLE_ALLOCATION_FAILED);
    runtime_history_destroy(history);

    history = runtime_history_create(0u);
    CHECK(history != NULL);
    runtime_history_get_status(history, &status);
    CHECK(!status.available);
    CHECK(status.unavailable_reason ==
          RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG);
    runtime_history_destroy(history);
    return true;
}

static bool test_instruction_decode_and_operands(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin begin = instruction_at(0xc000u, 100u);
    runtime_history_record record;

    CHECK(history != NULL);
    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPCODE_FETCH, 0xc000u, 0xadu, 100u));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPERAND_READ, 0xc001u, 0x34u, 101u));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPERAND_READ, 0xc002u, 0x12u, 102u));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_DATA_READ, 0x1234u, 0xabu, 104u));
    CHECK(runtime_history_complete_record(history));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(record.kind == RUNTIME_HISTORY_RECORD_INSTRUCTION);
    CHECK(record.machine_cycle == 100u);
    CHECK(record.pc == 0xc000u);
    CHECK(record.a == 0x11u && record.x == 0x22u && record.y == 0x33u);
    CHECK(record.sp == 0xfdu && record.p == 0x24u);
    CHECK(record.opcode == 0xadu);
    CHECK(record.operand1 == 0x34u && record.operand2 == 0x12u);
    CHECK(record.instruction_length == 3u);
    CHECK(record.access_count == 4u);
    CHECK(record.accesses[0].kind == C6510_BUS_ACCESS_OPCODE_FETCH);
    CHECK(record.accesses[1].kind == C6510_BUS_ACCESS_OPERAND_READ);
    CHECK(record.accesses[2].kind == C6510_BUS_ACCESS_OPERAND_READ);
    CHECK(record.accesses[3].address == 0x1234u);
    CHECK(record.accesses[3].value == 0xabu);
    CHECK(record.accesses[3].cycle_offset == 4u);
    CHECK(!record.partial);
    runtime_history_destroy(history);
    return true;
}

static bool test_irq_nmi_records(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin begin = instruction_at(0x3456u, 20u);
    runtime_history_record record;

    CHECK(history != NULL);
    begin.kind = RUNTIME_HISTORY_RECORD_IRQ;
    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_VECTOR_READ, 0xfffeu, 0x00u, 25u));
    CHECK(runtime_history_complete_record(history));
    begin.kind = RUNTIME_HISTORY_RECORD_NMI;
    begin.machine_cycle = 30u;
    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_STACK_WRITE, 0x01fdu, 0x34u, 31u));
    CHECK(runtime_history_complete_record(history));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(record.kind == RUNTIME_HISTORY_RECORD_IRQ);
    CHECK(record.instruction_length == 0u);
    CHECK(record.access_count == 1u);
    CHECK(runtime_history_lookup(history, 1u, 2u, &record));
    CHECK(record.kind == RUNTIME_HISTORY_RECORD_NMI);
    CHECK(record.accesses[0].kind == C6510_BUS_ACCESS_STACK_WRITE);
    runtime_history_destroy(history);
    return true;
}

static bool test_partial_record(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin begin = instruction_at(0x1000u, 9u);
    runtime_history_record record;

    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPCODE_FETCH, 0x1000u, 0xeau, 9u));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(record.partial);
    CHECK(runtime_history_complete_record(history));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(!record.partial);
    runtime_history_destroy(history);
    return true;
}

static bool test_access_limit_and_overflow(void) {
    runtime_history *history =
        runtime_history_create_ex(8192u, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin begin = instruction_at(0x2000u, 0u);
    runtime_history_record record;
    runtime_history_status status;
    unsigned i;

    CHECK(runtime_history_begin_record(history, &begin));
    for (i = 0u; i < RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD; ++i) {
        CHECK(runtime_history_append_access(
            history, C6510_BUS_ACCESS_DATA_READ,
            (uint16_t)i, (uint8_t)i, i));
    }
    CHECK(runtime_history_complete_record(history));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(record.access_count == RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD);
    CHECK(!record.access_truncated);

    begin.machine_cycle = 1000u;
    CHECK(runtime_history_begin_record(history, &begin));
    for (i = 0u; i < RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD + 1u; ++i) {
        CHECK(runtime_history_append_access(
            history, C6510_BUS_ACCESS_DATA_WRITE,
            (uint16_t)i, (uint8_t)i, 1000u + i));
    }
    CHECK(runtime_history_complete_record(history));
    CHECK(append_instruction(history, 0x2001u, 2000u, 0u));
    CHECK(runtime_history_lookup(history, 1u, 2u, &record));
    CHECK(record.access_count == RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD);
    CHECK(record.access_truncated);
    CHECK(runtime_history_lookup(history, 1u, 3u, &record));
    CHECK(record.pc == 0x2001u);
    runtime_history_get_status(history, &status);
    CHECK(status.truncated_accesses == 1u);
    runtime_history_destroy(history);
    return true;
}

static bool test_timing_saturation(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin begin = instruction_at(0x3000u, 100u);
    runtime_history_record record;

    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_DATA_READ, 0x1234u, 0x55u,
        100u + UINT16_MAX + 99u));
    CHECK(runtime_history_complete_record(history));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(record.timing_truncated);
    CHECK(record.accesses[0].cycle_offset == UINT16_MAX);
    runtime_history_destroy(history);
    return true;
}

static bool test_block_fit_transition_wrap_and_ids(void) {
    runtime_history *history =
        runtime_history_create_ex(1024u, 406u, NULL);
    runtime_history_status status;
    runtime_history_record record;
    unsigned i;

    CHECK(history != NULL);
    CHECK(append_instruction(
        history, 0x4000u, 0u,
        RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD));
    runtime_history_get_status(history, &status);
    CHECK(status.used_bytes == 406u);
    CHECK(append_instruction(
        history, 0x4001u, 100u,
        RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD));
    CHECK(append_instruction(
        history, 0x4002u, 200u,
        RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD));
    for (i = 3u; i < 8u; ++i) {
        CHECK(append_instruction(
            history, (uint16_t)(0x4000u + i), i * 100u,
            RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD));
    }
    runtime_history_get_status(history, &status);
    CHECK(status.wrap_count > 0u);
    CHECK(status.record_count <= 2u);
    CHECK(status.oldest_id > 1u);
    CHECK(status.newest_id == 8u);
    CHECK(!runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(runtime_history_lookup(history, 1u, 8u, &record));
    runtime_history_destroy(history);
    return true;
}

static bool test_timeline_and_cycle_delta_blocks(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_record record;

    CHECK(append_instruction(history, 0x5000u, 1000u, 0u));
    CHECK(runtime_history_transition_timeline(history));
    CHECK(append_instruction(history, 0x5001u, 0u, 0u));
    CHECK(runtime_history_lookup(history, 1u, 2u, &record));
    CHECK(record.timeline == 1u && record.machine_cycle == 0u);
    CHECK(append_instruction(
        history, 0x5002u, (uint64_t)UINT32_MAX + 10u, 0u));
    CHECK(runtime_history_lookup(history, 1u, 3u, &record));
    CHECK(record.machine_cycle == (uint64_t)UINT32_MAX + 10u);
    runtime_history_destroy(history);
    return true;
}

static bool test_clear_stop_resume_and_markers(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_record record;
    runtime_history_status status;

    CHECK(append_instruction(history, 0x6000u, 10u, 0u));
    CHECK(runtime_history_stop(history, 20u));
    runtime_history_get_status(history, &status);
    CHECK(!status.recording);
    CHECK(status.record_count == 2u);
    CHECK(runtime_history_lookup(history, 1u, 2u, &record));
    CHECK(record.kind == RUNTIME_HISTORY_RECORD_MARKER);
    CHECK(record.marker_kind == RUNTIME_HISTORY_MARKER_RECORDER_STOP);
    CHECK(runtime_history_stop(history, 21u));
    CHECK(runtime_history_resume(history, 30u));
    CHECK(runtime_history_resume(history, 31u));
    CHECK(runtime_history_lookup(history, 1u, 3u, &record));
    CHECK(record.marker_kind == RUNTIME_HISTORY_MARKER_RECORDER_RESUME);
    CHECK(runtime_history_append_marker(history, 999u, 7u, 8u, 40u));
    CHECK(runtime_history_lookup(history, 1u, 4u, &record));
    CHECK(record.marker_kind == 999u);
    CHECK(record.marker_arg0 == 7u && record.marker_arg1 == 8u);
    CHECK(runtime_history_clear(history, 50u));
    runtime_history_get_status(history, &status);
    CHECK(status.epoch == 2u);
    CHECK(status.record_count == 1u);
    CHECK(status.oldest_id == 1u && status.newest_id == 1u);
    CHECK(runtime_history_lookup(history, 2u, 1u, &record));
    CHECK(record.marker_kind == RUNTIME_HISTORY_MARKER_RECORDER_START);
    runtime_history_destroy(history);
    return true;
}

static bool test_seal_partial_and_resume_mid_instruction(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin begin = instruction_at(0x7000u, 0u);
    runtime_history_record record;

    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPCODE_FETCH, 0x7000u, 0xeau, 0u));
    CHECK(runtime_history_stop(history, 1u));
    CHECK(!runtime_history_append_access(
        history, C6510_BUS_ACCESS_DATA_READ, 0x1234u, 1u, 2u));
    CHECK(!runtime_history_complete_record(history));
    CHECK(runtime_history_resume(history, 3u));
    CHECK(!runtime_history_append_access(
        history, C6510_BUS_ACCESS_DATA_READ, 0x1234u, 1u, 4u));
    begin.pc = 0x7001u;
    begin.machine_cycle = 5u;
    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_complete_record(history));
    CHECK(runtime_history_lookup(history, 1u, 1u, &record));
    CHECK(record.partial);
    CHECK(runtime_history_lookup(history, 1u, 4u, &record));
    CHECK(record.pc == 0x7001u);
    runtime_history_destroy(history);
    return true;
}

static bool test_iterators_and_corrupt_decode(void) {
    runtime_history *history =
        runtime_history_create_ex(TEST_REQUEST_BYTES, TEST_BLOCK_SIZE, NULL);
    runtime_history_record record;

    CHECK(append_instruction(history, 0x8000u, 0u, 0u));
    CHECK(append_instruction(history, 0x8001u, 2u, 0u));
    CHECK(runtime_history_first(history, &record));
    CHECK(record.id == 1u);
    CHECK(runtime_history_next(history, 1u, 1u, &record));
    CHECK(record.id == 2u);
    CHECK(runtime_history_last(history, &record));
    CHECK(record.id == 2u);
    CHECK(runtime_history_previous(history, 1u, 2u, &record));
    CHECK(record.id == 1u);
    CHECK(runtime_history_test_corrupt_record_size(history, 1u, 255u));
    CHECK(!runtime_history_lookup(history, 1u, 1u, &record));
    runtime_history_destroy(history);
    return true;
}

static bool append_query_instruction(
    runtime_history *history,
    uint16_t pc,
    uint64_t cycle,
    uint8_t opcode,
    c6510_bus_access_kind access_kind,
    uint16_t address,
    uint8_t value) {
    runtime_history_begin begin = instruction_at(pc, cycle);

    CHECK(runtime_history_begin_record(history, &begin));
    CHECK(runtime_history_append_access(
        history, C6510_BUS_ACCESS_OPCODE_FETCH, pc, opcode, cycle));
    if (access_kind <= C6510_BUS_ACCESS_VECTOR_READ) {
        CHECK(runtime_history_append_access(
            history, access_kind, address, value, cycle + 1u));
    }
    CHECK(runtime_history_complete_record(history));
    return true;
}

static runtime_history *create_query_store(void) {
    runtime_history *history =
        runtime_history_create_ex(32768u, TEST_BLOCK_SIZE, NULL);
    runtime_history_begin interrupt = instruction_at(0xe123u, 140u);

    if (history == NULL ||
        !append_query_instruction(
            history, 0x1000u, 100u, 0xa9u,
            C6510_BUS_ACCESS_DATA_READ, 0x2000u, 0x12u) ||
        !append_query_instruction(
            history, 0x1002u, 110u, 0x8du,
            C6510_BUS_ACCESS_DATA_WRITE, 0xd015u, 0x55u) ||
        !runtime_history_append_marker(
            history, RUNTIME_HISTORY_MARKER_PROGRAM_INJECT,
            0u, 0u, 120u) ||
        !append_query_instruction(
            history, 0x1005u, 130u, 0xa9u,
            C6510_BUS_ACCESS_STACK_WRITE, 0x01fdu, 0x33u)) {
        runtime_history_destroy(history);
        return NULL;
    }
    interrupt.kind = RUNTIME_HISTORY_RECORD_IRQ;
    if (!runtime_history_begin_record(history, &interrupt) ||
        !runtime_history_append_access(
            history, C6510_BUS_ACCESS_VECTOR_READ,
            0xfffeu, 0x34u, 145u) ||
        !runtime_history_complete_record(history) ||
        !runtime_history_transition_timeline(history) ||
        !append_query_instruction(
            history, 0x2000u, 10u, 0xa9u,
            C6510_BUS_ACCESS_DATA_READ, 0x2001u, 0xabu) ||
        !append_query_instruction(
            history, 0x2002u, 20u, 0xeau,
            (c6510_bus_access_kind)255u, 0u, 0u) ||
        !append_query_instruction(
            history, 0x2003u, 30u, 0x8du,
            C6510_BUS_ACCESS_DATA_WRITE, 0xd015u, 0xaau)) {
        runtime_history_destroy(history);
        return NULL;
    }
    return history;
}

static bool test_query_empty_paging_and_context(void) {
    runtime_history *history = create_query_store();
    runtime_history_query query;
    runtime_history_record records[8];
    runtime_history_page page;
    runtime_history_query_stats stats;

    CHECK(history != NULL);
    memset(&query, 0, sizeof(query));
    CHECK(runtime_history_find(
        history, &query, 0u, 3u, records, &page, &stats) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 3u && page.more && page.next_id == 5u);
    CHECK(records[0].id == 8u && records[1].id == 7u &&
          records[2].id == 6u);
    CHECK(stats.blocks_visited > 0u && stats.records_decoded == 3u);
    CHECK(runtime_history_find(
        history, &query, page.next_id, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 5u && !page.more && page.next_id == 0u);
    CHECK(records[0].id == 5u && records[4].id == 1u);

    query.direction = RUNTIME_HISTORY_QUERY_FORWARD;
    CHECK(runtime_history_find(
        history, &query, 0u, 2u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(records[0].id == 1u && records[1].id == 2u);
    CHECK(page.more && page.next_id == 3u);

    CHECK(runtime_history_read(
        history, 1u, 4u, 2u, 1u, records, 8u, &page) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 4u && records[0].id == 2u &&
          records[2].id == 4u && records[3].id == 5u && !page.more);
    CHECK(runtime_history_read(
        history, 1u, 1u, 32u, 0u, records, 8u, &page) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && page.more);
    CHECK(runtime_history_read(
        history, 2u, 1u, 0u, 0u, records, 8u, &page) ==
        RUNTIME_HISTORY_QUERY_EPOCH_MISMATCH);
    CHECK(runtime_history_read(
        history, 1u, 99u, 0u, 0u, records, 8u, &page) ==
        RUNTIME_HISTORY_QUERY_RECORD_NOT_RETAINED);
    runtime_history_destroy(history);
    return true;
}

static bool test_query_pc_timeline_cycle_and_validation(void) {
    runtime_history *history = create_query_store();
    runtime_history_query query;
    runtime_history_record records[8];
    runtime_history_page page;

    CHECK(history != NULL);
    memset(&query, 0, sizeof(query));
    query.has_pc = true;
    query.pc_first = 0x1000u;
    query.pc_last = 0x1005u;
    query.direction = RUNTIME_HISTORY_QUERY_FORWARD;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 3u);
    CHECK(records[0].id == 1u && records[1].id == 2u &&
          records[2].id == 4u);

    query.pc_first = query.pc_last = 0xe123u;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].kind == RUNTIME_HISTORY_RECORD_IRQ);

    query.has_pc = false;
    query.has_timeline = true;
    query.timeline = 1u;
    query.has_cycle = true;
    query.cycle_first = 15u;
    query.cycle_last = 25u;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].id == 7u);

    query.has_cycle = false;
    query.has_pc = true;
    query.pc_first = 0x2000u;
    query.pc_last = 0x1000u;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_INVALID);
    runtime_history_destroy(history);
    return true;
}

static bool test_query_access_value_and_fetch_materialization(void) {
    runtime_history *history = create_query_store();
    runtime_history_query query;
    runtime_history_record records[8];
    runtime_history_page page;

    CHECK(history != NULL);
    memset(&query, 0, sizeof(query));
    query.has_address = true;
    query.address_first = query.address_last = 0xd015u;
    query.has_access = true;
    query.access_mask = RUNTIME_HISTORY_ACCESS_DATA_WRITE;
    query.direction = RUNTIME_HISTORY_QUERY_FORWARD;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 2u && records[0].id == 2u &&
          records[1].id == 8u);

    query.has_value = true;
    query.value = 0xa0u;
    query.value_mask = 0xf0u;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].id == 8u);

    memset(&query, 0, sizeof(query));
    query.has_access = true;
    query.access_mask = RUNTIME_HISTORY_ACCESS_VECTOR_READ;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].kind == RUNTIME_HISTORY_RECORD_IRQ);

    memset(&query, 0, sizeof(query));
    query.has_access = true;
    query.access_mask = RUNTIME_HISTORY_ACCESS_EXECUTE;
    query.has_address = true;
    query.address_first = query.address_last = 0x1000u;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].id == 1u);
    CHECK(records[0].access_count == 2u);
    CHECK(records[0].accesses[0].kind ==
          C6510_BUS_ACCESS_OPCODE_FETCH);
    runtime_history_destroy(history);
    return true;
}

static bool test_query_opcode_patterns_and_boundaries(void) {
    runtime_history *history = create_query_store();
    runtime_history_query query;
    runtime_history_record records[8];
    runtime_history_page page;

    CHECK(history != NULL);
    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_FORWARD;
    query.opcode_pattern_length = 2u;
    query.opcode_pattern[0].value = 0xa9u;
    query.opcode_pattern[0].mask = 0xffu;
    query.opcode_pattern[1].value = 0x8du;
    query.opcode_pattern[1].mask = 0xffu;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].id == 1u);

    query.opcode_pattern_length = 3u;
    query.opcode_pattern[0].value = 0xa0u;
    query.opcode_pattern[0].mask = 0xf0u;
    query.opcode_pattern[1].value = 0u;
    query.opcode_pattern[1].mask = 0u;
    query.opcode_pattern[2].value = 0x0du;
    query.opcode_pattern[2].mask = 0x0fu;
    CHECK(runtime_history_find(
        history, &query, 0u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].id == 6u);

    query.opcode_pattern_length = 2u;
    query.opcode_pattern[0].value = 0xa9u;
    query.opcode_pattern[0].mask = 0xffu;
    query.opcode_pattern[1].value = 0u;
    query.opcode_pattern[1].mask = 0u;
    CHECK(runtime_history_find(
        history, &query, 4u, 8u, records, &page, NULL) ==
        RUNTIME_HISTORY_QUERY_OK);
    CHECK(page.count == 1u && records[0].id == 6u);
    runtime_history_destroy(history);
    return true;
}

typedef bool (*test_fn)(void);

int main(void) {
    static const struct {
        const char *name;
        test_fn fn;
    } tests[] = {
        { "empty_store_and_first_epoch", test_empty_store_and_first_epoch },
        { "unavailable_construction", test_unavailable_construction },
        { "instruction_decode_and_operands", test_instruction_decode_and_operands },
        { "irq_nmi_records", test_irq_nmi_records },
        { "partial_record", test_partial_record },
        { "access_limit_and_overflow", test_access_limit_and_overflow },
        { "timing_saturation", test_timing_saturation },
        { "block_fit_transition_wrap_and_ids", test_block_fit_transition_wrap_and_ids },
        { "timeline_and_cycle_delta_blocks", test_timeline_and_cycle_delta_blocks },
        { "clear_stop_resume_and_markers", test_clear_stop_resume_and_markers },
        { "seal_partial_and_resume_mid_instruction", test_seal_partial_and_resume_mid_instruction },
        { "iterators_and_corrupt_decode", test_iterators_and_corrupt_decode },
        { "query_empty_paging_and_context", test_query_empty_paging_and_context },
        { "query_pc_timeline_cycle_and_validation", test_query_pc_timeline_cycle_and_validation },
        { "query_access_value_and_fetch_materialization", test_query_access_value_and_fetch_materialization },
        { "query_opcode_patterns_and_boundaries", test_query_opcode_patterns_and_boundaries },
    };
    size_t i;

    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].fn()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
    }
    printf("test_runtime_history: ok (%u cases)\n",
           (unsigned)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
