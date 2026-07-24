#include "control_deferred.h"

#include <stddef.h>
#include <string.h>

bool control_deferred_is_wait(control_command_type type)
{
    return type == CONTROL_COMMAND_WAIT_PAUSED ||
           type == CONTROL_COMMAND_WAIT_RUNNING ||
           type == CONTROL_COMMAND_WAIT_FRAME ||
           type == CONTROL_COMMAND_WAIT_EVENT;
}

static bool deferred_table_has_wait(const deferred_control_table *table)
{
    size_t i;

    if (table == NULL) {
        return false;
    }
    for (i = 0; i < CONTROL_DEFERRED_CAPACITY; ++i) {
        if (table->entries[i].active &&
            control_deferred_is_wait(table->entries[i].command_type)) {
            return true;
        }
    }
    return false;
}

void control_deferred_clear_slot(deferred_control_response *d)
{
    if (d == NULL) {
        return;
    }
    d->has_expected_breakpoint_count = false;
    d->expected_breakpoint_count = 0;
    d->has_expected_breakpoint_enabled = false;
    d->has_expected_breakpoint_start = false;
    d->expect_breakpoint_absent = false;
    d->expected_breakpoint_id = 0;
    d->start_frame_number = 0;
    d->frame_delta = 0;
    d->wait_after_seq = 0;
    d->frame_format = CONTROL_FRAME_FORMAT_ARGB8888;
    d->cia_index = 1u;
    d->request_token = 0u;
    d->connection_epoch = 0u;
    d->wait_event_name[0] = '\0';
    d->memory_address = 0;
    d->memory_length = 0;
    d->include_write_history = false;
}

deferred_control_response *control_deferred_reserve(
    deferred_control_table *table,
    bool is_wait,
    const char **out_busy_msg)
{
    size_t i;

    if (out_busy_msg != NULL) {
        *out_busy_msg = "deferred-table-full";
    }
    if (table == NULL) {
        return NULL;
    }
    if (is_wait && deferred_table_has_wait(table)) {
        if (out_busy_msg != NULL) {
            *out_busy_msg = "wait already active";
        }
        return NULL;
    }
    for (i = 0; i < CONTROL_DEFERRED_CAPACITY; ++i) {
        if (!table->entries[i].active) {
            control_deferred_clear_slot(&table->entries[i]);
            return &table->entries[i];
        }
    }
    return NULL;
}
