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
    CONTROL_COMMAND_QUIT_CLIENT
} control_command_type;

typedef enum control_response_type {
    CONTROL_RESPONSE_OK = 0,
    CONTROL_RESPONSE_ERROR
} control_response_type;

typedef struct control_request {
    uint32_t id;
    control_command_type type;
} control_request;

typedef struct control_response {
    uint32_t id;
    control_response_type type;
    char text[CONTROL_RESPONSE_TEXT_MAX];
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

bool control_protocol_write_response_line(
    const control_response *response,
    char *out,
    size_t out_size);

