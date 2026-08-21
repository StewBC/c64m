#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CONTROL_LINE_MAX = 512,
    CONTROL_RESPONSE_TEXT_MAX = 512,
    CONTROL_HISTORY_MAX_OPCODE_PATTERN = 32
};

typedef enum control_command_type {
    CONTROL_COMMAND_NONE = 0,
    CONTROL_COMMAND_HELLO,
    CONTROL_COMMAND_VERSION,
    CONTROL_COMMAND_CAPABILITIES,
    CONTROL_COMMAND_PING,
    CONTROL_COMMAND_QUIT_CLIENT,
    CONTROL_COMMAND_RESET,
    CONTROL_COMMAND_RUN,
    CONTROL_COMMAND_PAUSE,
    CONTROL_COMMAND_STEP_CYCLE,
    CONTROL_COMMAND_STEP_INSTRUCTION,
    CONTROL_COMMAND_STEP_OVER,
    CONTROL_COMMAND_STEP_OUT,
    CONTROL_COMMAND_RUN_CYCLES,
    CONTROL_COMMAND_RUN_INSTRUCTIONS,
    CONTROL_COMMAND_RUN_TO,
    CONTROL_COMMAND_STEP_FRAME,
    CONTROL_COMMAND_RUN_TO_RASTER,
    CONTROL_COMMAND_HISTORY_INFO,
    CONTROL_COMMAND_HISTORY_RECORD,
    CONTROL_COMMAND_HISTORY_CLEAR,
    CONTROL_COMMAND_HISTORY_FIND,
    CONTROL_COMMAND_HISTORY_NEXT,
    CONTROL_COMMAND_HISTORY_READ,
    CONTROL_COMMAND_HISTORY_CLOSE,
    CONTROL_COMMAND_SET_TURBO,
    CONTROL_COMMAND_GET_STATE,
    CONTROL_COMMAND_GET_CPU,
    CONTROL_COMMAND_GET_FRAME,
    CONTROL_COMMAND_GET_FRAME_AT,
    CONTROL_COMMAND_FRAME_RING_INFO,
    CONTROL_COMMAND_FRAME_RING_RECORD,
    CONTROL_COMMAND_FRAME_RING_CLEAR,
    CONTROL_COMMAND_VIC_RING_INFO,
    CONTROL_COMMAND_VIC_RING_RECORD,
    CONTROL_COMMAND_VIC_RING_CLEAR,
    CONTROL_COMMAND_VIC_RING_FIND,
    CONTROL_COMMAND_GET_VIC,
    CONTROL_COMMAND_GET_CIA,
    CONTROL_COMMAND_GET_MEMORY,
    CONTROL_COMMAND_SET_MEMORY,
    CONTROL_COMMAND_GET_DEBUG_MEMORY,
    CONTROL_COMMAND_GET_CALL_STACK,
    CONTROL_COMMAND_KEY_DOWN,
    CONTROL_COMMAND_KEY_UP,
    CONTROL_COMMAND_RESTORE,
    CONTROL_COMMAND_JOYSTICK,
    CONTROL_COMMAND_PASTE_TEXT,
    CONTROL_COMMAND_PASTE_EVENTS,
    CONTROL_COMMAND_PASTE_TEXT_DATA,
    CONTROL_COMMAND_PASTE_EVENTS_DATA,
    CONTROL_COMMAND_LOAD_PRG,
    CONTROL_COMMAND_LOAD_BIN,
    CONTROL_COMMAND_SAVE_BIN,
    CONTROL_COMMAND_LOAD_STATE,
    CONTROL_COMMAND_SAVE_STATE,
    CONTROL_COMMAND_MOUNT_D64,
    CONTROL_COMMAND_UNMOUNT_DISK,
    CONTROL_COMMAND_POWER_DRIVE,
    CONTROL_COMMAND_GET_DISK_STATUS,
    CONTROL_COMMAND_GET_DRIVE_CPU,
    CONTROL_COMMAND_BREAK_EXEC,
    CONTROL_COMMAND_BREAK_CLEAR,
    CONTROL_COMMAND_BREAK_ENABLE,
    CONTROL_COMMAND_BREAK_LIST,
    CONTROL_COMMAND_BREAK_CLEAR_ALL,
    CONTROL_COMMAND_BREAK_CREATE,
    CONTROL_COMMAND_BREAK_UPDATE,
    CONTROL_COMMAND_REARM_ONESHOTS,
    CONTROL_COMMAND_WAIT_PAUSED,
    CONTROL_COMMAND_WAIT_RUNNING,
    CONTROL_COMMAND_WAIT_FRAME,
    CONTROL_COMMAND_WAIT_EVENT,
    CONTROL_COMMAND_ASSEMBLE,
    CONTROL_COMMAND_FIND_SYMBOL
} control_command_type;

enum {
    CONTROL_FRAME_FORMAT_ARGB8888 = 0,
    CONTROL_FRAME_FORMAT_INDEXED8 = 1
};

typedef struct control_args {
    uint64_t count;
    uint32_t id;
    uint32_t timeout_ms;
    uint16_t address;
    uint32_t length; /* get-memory: 1..65536; set-memory still 1..1024 */
    uint16_t raster_line;
    uint16_t raster_cycle;
    bool has_raster_cycle;
    bool history_record_enabled;
    uint16_t history_limit;
    uint16_t history_before;
    uint16_t history_after;
    uint64_t history_cursor;
    uint64_t history_id;
    uint64_t history_epoch;
    uint64_t history_from_id;
    uint8_t history_from_kind;
    bool history_query_has_epoch;
    bool history_query_has_timeline;
    bool history_query_has_cycle;
    bool history_query_has_pc;
    bool history_query_has_address;
    bool history_query_has_access;
    bool history_query_has_value;
    uint64_t history_query_epoch;
    uint32_t history_query_timeline;
    uint64_t history_cycle_first;
    uint64_t history_cycle_last;
    uint16_t history_pc_first;
    uint16_t history_pc_last;
    uint16_t history_address_first;
    uint16_t history_address_last;
    uint16_t history_access_mask;
    uint8_t history_value;
    uint8_t history_value_mask;
    uint8_t history_opcode_pattern_length;
    uint8_t history_opcode_values[CONTROL_HISTORY_MAX_OPCODE_PATTERN];
    uint8_t history_opcode_masks[CONTROL_HISTORY_MAX_OPCODE_PATTERN];
    uint8_t history_direction;
    uint16_t start_address;
    uint16_t end_address;
    uint16_t run_address;
    uint16_t turbo_multiplier;
    uint8_t memory_mode;
    uint8_t device;
    uint8_t port;
    uint8_t mask;
    uint8_t key;
    uint8_t frame_format;
    uint64_t frame_ring_target;
    bool frame_ring_by_cycle;
    bool frame_ring_record_enabled;
    uint64_t vic_ring_frame;
    bool vic_ring_has_frame;
    uint16_t vic_ring_raster_first;
    uint16_t vic_ring_raster_last;
    uint32_t vic_ring_limit;
    bool vic_ring_record_enabled;
    uint8_t cia_index; /* 1 or 2 for get-cia */
    bool power_drive_on; /* power-drive: true=on (default), false=off */
    bool use_file_address;
    bool reset_first;
    bool is_basic;
    bool write_file_address;
    bool include_write_history;
    bool auto_run;
    bool basic_run;
    bool has_run_address;
    char text[1024];
} control_args;

typedef enum control_response_type {
    CONTROL_RESPONSE_OK = 0,
    CONTROL_RESPONSE_ERROR,
    CONTROL_RESPONSE_DATA,
    /* Unsolicited out-of-band line: "<id> event <name> [fields...]".
       Request id 0 is the reserved event channel. */
    CONTROL_RESPONSE_EVENT
} control_response_type;

typedef struct control_request {
    uint32_t id;
    control_command_type type;
    control_args args;
    uint8_t *payload;
    size_t payload_size;
} control_request;

typedef struct control_response {
    uint32_t id;
    control_response_type type;
    char text[CONTROL_RESPONSE_TEXT_MAX];
    char data_type[32];
    char metadata[CONTROL_RESPONSE_TEXT_MAX];
    uint8_t *payload;
    size_t payload_size;
    bool close_client;
} control_response;

bool control_protocol_parse_request(
    const char *line,
    control_request *out_request,
    control_response *out_error);

void control_protocol_format_ok(
    control_response *response,
    uint32_t id,
    const char *text,
    bool close_client);

void control_protocol_format_error(
    control_response *response,
    uint32_t id,
    const char *code,
    const char *message,
    bool close_client);

void control_protocol_format_event(
    control_response *response,
    uint32_t id,
    const char *text);

void control_protocol_format_data(
    control_response *response,
    uint32_t id,
    const char *data_type,
    uint8_t *payload,
    size_t payload_size,
    const char *metadata,
    bool close_client);

/* Deferred completion gate (Phase 0.5): non-zero deferred tokens only accept
   events that echo the same token. Token 0 deferred keeps legacy type-only
   matching for paths not yet tokenized. */
static inline bool control_deferred_token_matches(
    uint64_t deferred_request_token,
    uint64_t event_request_token)
{
    if (deferred_request_token == 0u) {
        return true;
    }
    return event_request_token == deferred_request_token;
}

bool control_protocol_write_response_line(
    const control_response *response,
    char *out,
    size_t out_size);

void control_response_release(control_response *response);
void control_request_release(control_request *request);
