#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CONTROL_LINE_MAX = 512,
    CONTROL_RESPONSE_TEXT_MAX = 256
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
    CONTROL_COMMAND_SET_TURBO,
    CONTROL_COMMAND_GET_STATE,
    CONTROL_COMMAND_GET_CPU,
    CONTROL_COMMAND_GET_FRAME,
    CONTROL_COMMAND_GET_MEMORY,
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

typedef struct control_args {
    uint64_t count;
    uint32_t id;
    uint32_t timeout_ms;
    uint16_t address;
    uint16_t length;
    uint16_t start_address;
    uint16_t end_address;
    uint16_t run_address;
    uint16_t turbo_multiplier;
    uint8_t memory_mode;
    uint8_t device;
    uint8_t port;
    uint8_t mask;
    uint8_t key;
    bool use_file_address;
    bool reset_first;
    bool is_basic;
    bool write_file_address;
    bool include_write_history;
    bool auto_run;
    bool has_run_address;
    char text[1024];
} control_args;

typedef enum control_response_type {
    CONTROL_RESPONSE_OK = 0,
    CONTROL_RESPONSE_ERROR,
    CONTROL_RESPONSE_DATA
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

void control_protocol_format_data(
    control_response *response,
    uint32_t id,
    const char *data_type,
    uint8_t *payload,
    size_t payload_size,
    const char *metadata,
    bool close_client);

bool control_protocol_write_response_line(
    const control_response *response,
    char *out,
    size_t out_size);

void control_response_release(control_response *response);
void control_request_release(control_request *request);
