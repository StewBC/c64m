#pragma once

#include "control_protocol.h"

#include <stdbool.h>
#include <stdint.h>

enum { CONTROL_DEFERRED_CAPACITY = 16 };

typedef struct deferred_control_response {
    bool active;
    uint32_t request_id;
    control_command_type command_type;
    uint64_t deadline_ms;
    uint64_t request_token;
    uint64_t connection_epoch;
    uint16_t memory_address;
    uint32_t memory_length;
    uint8_t memory_mode;
    uint16_t expected_breakpoint_count;
    uint32_t expected_breakpoint_id;
    bool expected_breakpoint_enabled;
    uint16_t expected_breakpoint_start;
    bool has_expected_breakpoint_count;
    bool has_expected_breakpoint_enabled;
    bool has_expected_breakpoint_start;
    bool expect_breakpoint_absent;
    bool include_write_history;
    uint8_t frame_format;
    uint8_t cia_index;
    uint64_t start_frame_number;
    uint64_t frame_delta;
    uint64_t wait_after_seq;
    char wait_event_name[32];
} deferred_control_response;

typedef struct deferred_control_table {
    deferred_control_response entries[CONTROL_DEFERRED_CAPACITY];
} deferred_control_table;

bool control_deferred_is_wait(control_command_type type);

/* Reserve a free slot (does not set active). NULL if full or wait conflict.
   When NULL and table non-NULL, *out_busy_msg is set. */
deferred_control_response *control_deferred_reserve(
    deferred_control_table *table,
    bool is_wait,
    const char **out_busy_msg);

void control_deferred_clear_slot(deferred_control_response *d);
