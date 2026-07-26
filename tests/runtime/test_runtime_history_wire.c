#include "runtime_history_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            free(bytes); \
            return 1; \
        } \
    } while (0)

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const uint8_t *p) {
    return (uint64_t)read_u32(p) |
        ((uint64_t)read_u32(p + 4) << 32);
}

int main(void) {
    runtime_history_record records[2];
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    size_t encoded = 0u;
    bool clipped = false;
    const uint8_t *first;
    const uint8_t *second;

    memset(records, 0, sizeof(records));
    records[0].epoch = 0x0102030405060708ull;
    records[0].id = 0x1112131415161718ull;
    records[0].timeline = 0x21222324u;
    records[0].machine_cycle = 0x3132333435363738ull;
    records[0].kind = RUNTIME_HISTORY_RECORD_INSTRUCTION;
    records[0].pc = 0x1234u;
    records[0].a = 1u;
    records[0].x = 2u;
    records[0].y = 3u;
    records[0].sp = 4u;
    records[0].p = 5u;
    records[0].opcode = 0xadu;
    records[0].operand1 = 0x00u;
    records[0].operand2 = 0xd0u;
    records[0].instruction_length = 3u;
    records[0].partial = true;
    records[0].access_truncated = true;
    records[0].timing_truncated = true;
    records[0].access_count = 2u;
    records[0].accesses[0].address = 0x1234u;
    records[0].accesses[0].cycle_offset = 0u;
    records[0].accesses[0].value = 0xadu;
    records[0].accesses[0].kind = C6510_BUS_ACCESS_OPCODE_FETCH;
    records[0].accesses[1].address = 0xd000u;
    records[0].accesses[1].cycle_offset = 4u;
    records[0].accesses[1].value = 0x55u;
    records[0].accesses[1].kind = C6510_BUS_ACCESS_DATA_READ;

    records[1].epoch = records[0].epoch;
    records[1].id = records[0].id + 1u;
    records[1].timeline = 7u;
    records[1].machine_cycle = 9u;
    records[1].kind = RUNTIME_HISTORY_RECORD_MARKER;
    records[1].marker_kind = RUNTIME_HISTORY_MARKER_RESET_COMPLETE;
    records[1].marker_arg0 = RUNTIME_HISTORY_RESET_MACHINE_CONFIG;
    records[1].marker_arg1 = 0xaabbccddu;

    CHECK(RUNTIME_HISTORY_MARKER_RESET_COMPLETE == 4);
    CHECK(RUNTIME_HISTORY_MARKER_CLOCK_DISCONTINUITY == 12);
    CHECK(RUNTIME_HISTORY_RESET_INITIAL_STARTUP == 1);
    CHECK(RUNTIME_HISTORY_RESET_BIN_LOAD_RESET_FIRST == 7);
    CHECK(RUNTIME_HISTORY_CLOCK_DISCONTINUITY_VIDEO_STANDARD_CHANGE == 1);
    CHECK(runtime_history_wire_encode(
        records[0].epoch,
        records,
        2u,
        false,
        records[1].id,
        &bytes,
        &length,
        &encoded,
        &clipped) == RUNTIME_HISTORY_WIRE_OK);
    CHECK(encoded == 2u && !clipped);
    CHECK(length == 24u + 64u + 48u);
    CHECK(memcmp(bytes, "HST1", 4u) == 0);
    CHECK(read_u16(bytes + 4) == 1u);
    CHECK(read_u16(bytes + 6) == 0u);
    CHECK(read_u64(bytes + 8) == records[0].epoch);
    CHECK(read_u32(bytes + 16) == 2u);
    CHECK(read_u32(bytes + 20) == 0u);

    first = bytes + 24u;
    CHECK(read_u16(first + 0) == 64u);
    CHECK(first[2] == RUNTIME_HISTORY_RECORD_INSTRUCTION);
    CHECK(first[3] == 0x0bu);
    CHECK(read_u32(first + 4) == records[0].timeline);
    CHECK(read_u64(first + 8) == records[0].id);
    CHECK(read_u64(first + 16) == records[0].machine_cycle);
    CHECK(read_u16(first + 24) == 0x1234u);
    CHECK(first[31] == 0xadu && first[34] == 3u && first[35] == 2u);
    CHECK(read_u16(first + 36) == 0u);
    CHECK(read_u16(first + 48) == 0x1234u);
    CHECK(first[53] == C6510_BUS_ACCESS_OPCODE_FETCH);
    CHECK(read_u16(first + 56) == 0xd000u);
    CHECK(read_u16(first + 58) == 4u);
    CHECK(first[60] == 0x55u);
    CHECK(first[61] == C6510_BUS_ACCESS_DATA_READ);

    second = first + 64u;
    CHECK(read_u16(second + 0) == 48u);
    CHECK(second[2] == RUNTIME_HISTORY_RECORD_MARKER);
    CHECK(second[3] == 0x04u);
    CHECK(read_u16(second + 36) ==
          RUNTIME_HISTORY_MARKER_RESET_COMPLETE);
    CHECK(read_u32(second + 40) ==
          RUNTIME_HISTORY_RESET_MACHINE_CONFIG);
    CHECK(read_u32(second + 44) == 0xaabbccddu);

    free(bytes);
    printf("test_runtime_history_wire: ok\n");
    return 0;
}
