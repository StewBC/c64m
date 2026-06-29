#include "control_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_u32(const char *text, const char **out_end, uint32_t *out_value)
{
    char *end;
    unsigned long value;

    if (text == NULL || !isdigit((unsigned char)text[0])) {
        return false;
    }
    value = strtoul(text, &end, 10);
    if (end == text || value > 0xfffffffful) {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    if (out_value != NULL) {
        *out_value = (uint32_t)value;
    }
    return true;
}

static control_command_type command_from_name(const char *name, size_t length)
{
    if (length == 5 && strncmp(name, "hello", length) == 0) {
        return CONTROL_COMMAND_HELLO;
    }
    if (length == 7 && strncmp(name, "version", length) == 0) {
        return CONTROL_COMMAND_VERSION;
    }
    if (length == 12 && strncmp(name, "capabilities", length) == 0) {
        return CONTROL_COMMAND_CAPABILITIES;
    }
    if (length == 4 && strncmp(name, "ping", length) == 0) {
        return CONTROL_COMMAND_PING;
    }
    if (length == 11 && strncmp(name, "quit-client", length) == 0) {
        return CONTROL_COMMAND_QUIT_CLIENT;
    }
    return CONTROL_COMMAND_NONE;
}

static void set_parse_error(
    control_response *out_error,
    uint32_t id,
    const char *code,
    const char *message)
{
    if (out_error != NULL) {
        control_protocol_format_error(out_error, id, code, message, false);
    }
}

bool control_protocol_parse_request(
    const char *line,
    control_request *out_request,
    control_response *out_error)
{
    const char *cursor;
    const char *command_start;
    const char *command_end;
    uint32_t id = 0;
    control_command_type type;

    if (line == NULL || out_request == NULL) {
        set_parse_error(out_error, 0, "bad-request", "missing request");
        return false;
    }

    cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (!parse_u32(cursor, &cursor, &id)) {
        set_parse_error(out_error, 0, "bad-id", "request id must be a decimal integer");
        return false;
    }
    if (*cursor != ' ' && *cursor != '\t') {
        set_parse_error(out_error, id, "bad-request", "missing command");
        return false;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    command_start = cursor;
    while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n' &&
           *cursor != ' ' && *cursor != '\t') {
        cursor++;
    }
    command_end = cursor;
    if (command_end == command_start) {
        set_parse_error(out_error, id, "bad-request", "missing command");
        return false;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
        set_parse_error(out_error, id, "bad-args", "command does not take arguments");
        return false;
    }

    type = command_from_name(command_start, (size_t)(command_end - command_start));
    if (type == CONTROL_COMMAND_NONE) {
        set_parse_error(out_error, id, "unknown-command", "unknown command");
        return false;
    }

    memset(out_request, 0, sizeof(*out_request));
    out_request->id = id;
    out_request->type = type;
    return true;
}

void control_protocol_format_ok(
    control_response *response,
    uint32_t id,
    const char *text,
    bool close_client)
{
    if (response == NULL) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->id = id;
    response->type = CONTROL_RESPONSE_OK;
    response->close_client = close_client;
    if (text != NULL && text[0] != '\0') {
        snprintf(response->text, sizeof(response->text), "%s", text);
    }
}

void control_protocol_format_error(
    control_response *response,
    uint32_t id,
    const char *code,
    const char *message,
    bool close_client)
{
    if (response == NULL) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->id = id;
    response->type = CONTROL_RESPONSE_ERROR;
    response->close_client = close_client;
    snprintf(
        response->text,
        sizeof(response->text),
        "%s %s",
        code != NULL ? code : "error",
        message != NULL ? message : "");
}

bool control_protocol_write_response_line(
    const control_response *response,
    char *out,
    size_t out_size)
{
    int written;

    if (response == NULL || out == NULL || out_size == 0) {
        return false;
    }

    if (response->type == CONTROL_RESPONSE_OK) {
        if (response->text[0] != '\0') {
            written = snprintf(
                out,
                out_size,
                "%u ok %s\n",
                response->id,
                response->text);
        } else {
            written = snprintf(out, out_size, "%u ok\n", response->id);
        }
    } else {
        written = snprintf(
            out,
            out_size,
            "%u error %s\n",
            response->id,
            response->text);
    }

    return written >= 0 && (size_t)written < out_size;
}

