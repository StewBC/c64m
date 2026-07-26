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

static bool parse_u64_token(const char *text, const char **out_end, uint64_t *out_value)
{
    char *end;
    unsigned long long value;

    if (text == NULL || !isdigit((unsigned char)text[0])) {
        return false;
    }
    value = strtoull(text, &end, 0);
    if (end == text) {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    if (out_value != NULL) {
        *out_value = (uint64_t)value;
    }
    return true;
}

static bool parse_u16_token(const char *text, const char **out_end, uint16_t *out_value)
{
    char *end;
    unsigned long value;
    int base = 0;

    if (text == NULL) {
        return false;
    }
    if (text[0] == '$') {
        text++;
        base = 16;
    }
    if (!isxdigit((unsigned char)text[0])) {
        return false;
    }
    value = strtoul(text, &end, base);
    if (end == text || value > 0xfffful) {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    if (out_value != NULL) {
        *out_value = (uint16_t)value;
    }
    return true;
}

static bool parse_memory_mode_token(const char *text, const char **out_end, uint8_t *out_mode)
{
    const char *end = text;
    size_t length;

    if (text == NULL) {
        return false;
    }
    while (*end != '\0' && *end != '\r' && *end != '\n' &&
           *end != ' ' && *end != '\t') {
        end++;
    }
    length = (size_t)(end - text);
    if (length == 3 && strncmp(text, "map", length) == 0) {
        *out_mode = 0; /* RUNTIME_MEMORY_MODE_CPU_MAP */
    } else if (length == 3 && strncmp(text, "ram", length) == 0) {
        *out_mode = 1; /* RUNTIME_MEMORY_MODE_RAM */
    } else if (length == 3 && strncmp(text, "rom", length) == 0) {
        *out_mode = 2; /* RUNTIME_MEMORY_MODE_ROM */
    } else if (length == 6 && strncmp(text, "drive8", length) == 0) {
        *out_mode = 3; /* RUNTIME_MEMORY_MODE_DRIVE8_MAP */
    } else if (length == 6 && strncmp(text, "drive9", length) == 0) {
        *out_mode = 4; /* RUNTIME_MEMORY_MODE_DRIVE9_MAP */
    } else {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

static void skip_spaces(const char **cursor)
{
    while (**cursor == ' ' || **cursor == '\t') {
        (*cursor)++;
    }
}

static bool token_bounds(const char *cursor, const char **out_start, const char **out_end)
{
    skip_spaces(&cursor);
    if (*cursor == '\0' || *cursor == '\r' || *cursor == '\n') {
        return false;
    }
    *out_start = cursor;
    while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n' &&
           *cursor != ' ' && *cursor != '\t') {
        cursor++;
    }
    *out_end = cursor;
    return true;
}

static bool copy_token(const char *cursor, const char **out_end, char *out, size_t out_size)
{
    const char *start;
    const char *end;
    size_t length;

    if (out == NULL || out_size == 0 || !token_bounds(cursor, &start, &end)) {
        return false;
    }
    length = (size_t)(end - start);
    if (length >= out_size) {
        return false;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

static bool copy_rest_argument(const char *cursor, const char **out_end, char *out, size_t out_size)
{
    const char *start = cursor;
    const char *end;
    size_t length;

    if (out == NULL || out_size == 0) {
        return false;
    }
    skip_spaces(&start);
    if (*start == '\0' || *start == '\r' || *start == '\n') {
        return false;
    }
    end = start;
    while (*end != '\0' && *end != '\r' && *end != '\n') {
        end++;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    length = (size_t)(end - start);
    if (length == 0 || length >= out_size) {
        return false;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

static bool split_trailing_tokens(
    const char *cursor,
    size_t trailing_count,
    char *out_path,
    size_t out_path_size,
    const char **out_tokens)
{
    const char *start = cursor;
    const char *end;
    size_t i;
    size_t length;

    if (out_path == NULL || out_path_size == 0 || out_tokens == NULL || trailing_count == 0) {
        return false;
    }
    skip_spaces(&start);
    if (*start == '\0' || *start == '\r' || *start == '\n') {
        return false;
    }
    end = start;
    while (*end != '\0' && *end != '\r' && *end != '\n') {
        end++;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    for (i = trailing_count; i > 0; i--) {
        const char *token_end = end;
        const char *token_start;
        while (token_end > start && (token_end[-1] == ' ' || token_end[-1] == '\t')) {
            token_end--;
        }
        if (token_end == start) {
            return false;
        }
        token_start = token_end;
        while (token_start > start && token_start[-1] != ' ' && token_start[-1] != '\t') {
            token_start--;
        }
        out_tokens[i - 1u] = token_start;
        end = token_start;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    length = (size_t)(end - start);
    if (length == 0 || length >= out_path_size) {
        return false;
    }
    memcpy(out_path, start, length);
    out_path[length] = '\0';
    return true;
}

static bool parse_bool_token(const char *cursor, const char **out_end, bool *out_value)
{
    const char *start;
    const char *end;
    size_t length;

    if (!token_bounds(cursor, &start, &end)) {
        return false;
    }
    length = (size_t)(end - start);
    if ((length == 1 && start[0] == '0') ||
        (length == 5 && strncmp(start, "false", length) == 0)) {
        *out_value = false;
    } else if ((length == 1 && start[0] == '1') ||
               (length == 4 && strncmp(start, "true", length) == 0)) {
        *out_value = true;
    } else {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
    return true;
}

static bool parse_u8_token(const char *cursor, const char **out_end, uint8_t *out_value)
{
    uint64_t value;

    if (!parse_u64_token(cursor, out_end, &value) || value > 255u) {
        return false;
    }
    *out_value = (uint8_t)value;
    return true;
}

static bool key_name_to_value(const char *start, size_t length, uint8_t *out_key)
{
    static const char *const names[] = {
        "a","b","c","d","e","f","g","h","i","j","k","l","m",
        "n","o","p","q","r","s","t","u","v","w","x","y","z",
        "0","1","2","3","4","5","6","7","8","9",
        "space","return","delete","left-shift","right-shift",
        "plus","minus","asterisk","equals","colon","semicolon",
        "comma","period","slash","at","cursor-right","cursor-down",
        "home","run-stop","control","commodore","left-arrow","up-arrow",
        "pound","f1","f3","f5","f7"
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strlen(names[i]) == length && strncmp(names[i], start, length) == 0) {
            *out_key = (uint8_t)i;
            return true;
        }
    }
    return false;
}

static bool parse_key_token(const char *cursor, const char **out_end, uint8_t *out_key)
{
    const char *start;
    const char *end;

    if (!token_bounds(cursor, &start, &end) ||
        !key_name_to_value(start, (size_t)(end - start), out_key)) {
        return false;
    }
    if (out_end != NULL) {
        *out_end = end;
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
    if (length == 5 && strncmp(name, "reset", length) == 0) {
        return CONTROL_COMMAND_RESET;
    }
    if (length == 3 && strncmp(name, "run", length) == 0) {
        return CONTROL_COMMAND_RUN;
    }
    if (length == 5 && strncmp(name, "pause", length) == 0) {
        return CONTROL_COMMAND_PAUSE;
    }
    if (length == 10 && strncmp(name, "step-cycle", length) == 0) {
        return CONTROL_COMMAND_STEP_CYCLE;
    }
    if (length == 16 && strncmp(name, "step-instruction", length) == 0) {
        return CONTROL_COMMAND_STEP_INSTRUCTION;
    }
    if (length == 9 && strncmp(name, "step-over", length) == 0) {
        return CONTROL_COMMAND_STEP_OVER;
    }
    if (length == 8 && strncmp(name, "step-out", length) == 0) {
        return CONTROL_COMMAND_STEP_OUT;
    }
    if (length == 10 && strncmp(name, "run-cycles", length) == 0) {
        return CONTROL_COMMAND_RUN_CYCLES;
    }
    if (length == 16 && strncmp(name, "run-instructions", length) == 0) {
        return CONTROL_COMMAND_RUN_INSTRUCTIONS;
    }
    if (length == 6 && strncmp(name, "run-to", length) == 0) {
        return CONTROL_COMMAND_RUN_TO;
    }
    if (length == 10 && strncmp(name, "step-frame", length) == 0) {
        return CONTROL_COMMAND_STEP_FRAME;
    }
    if (length == 13 && strncmp(name, "run-to-raster", length) == 0) {
        return CONTROL_COMMAND_RUN_TO_RASTER;
    }
    if (length == 12 && strncmp(name, "history-info", length) == 0) {
        return CONTROL_COMMAND_HISTORY_INFO;
    }
    if (length == 14 && strncmp(name, "history-record", length) == 0) {
        return CONTROL_COMMAND_HISTORY_RECORD;
    }
    if (length == 13 && strncmp(name, "history-clear", length) == 0) {
        return CONTROL_COMMAND_HISTORY_CLEAR;
    }
    if (length == 12 && strncmp(name, "history-find", length) == 0) {
        return CONTROL_COMMAND_HISTORY_FIND;
    }
    if (length == 12 && strncmp(name, "history-next", length) == 0) {
        return CONTROL_COMMAND_HISTORY_NEXT;
    }
    if (length == 12 && strncmp(name, "history-read", length) == 0) {
        return CONTROL_COMMAND_HISTORY_READ;
    }
    if (length == 13 && strncmp(name, "history-close", length) == 0) {
        return CONTROL_COMMAND_HISTORY_CLOSE;
    }
    if (length == 9 && strncmp(name, "set-turbo", length) == 0) {
        return CONTROL_COMMAND_SET_TURBO;
    }
    if (length == 9 && strncmp(name, "get-state", length) == 0) {
        return CONTROL_COMMAND_GET_STATE;
    }
    if (length == 7 && strncmp(name, "get-cpu", length) == 0) {
        return CONTROL_COMMAND_GET_CPU;
    }
    if (length == 9 && strncmp(name, "get-frame", length) == 0) {
        return CONTROL_COMMAND_GET_FRAME;
    }
    if (length == 7 && strncmp(name, "get-vic", length) == 0) {
        return CONTROL_COMMAND_GET_VIC;
    }
    if (length == 7 && strncmp(name, "get-cia", length) == 0) {
        return CONTROL_COMMAND_GET_CIA;
    }
    if (length == 10 && strncmp(name, "get-memory", length) == 0) {
        return CONTROL_COMMAND_GET_MEMORY;
    }
    if (length == 10 && strncmp(name, "set-memory", length) == 0) {
        return CONTROL_COMMAND_SET_MEMORY;
    }
    if (length == 16 && strncmp(name, "get-debug-memory", length) == 0) {
        return CONTROL_COMMAND_GET_DEBUG_MEMORY;
    }
    if (length == 14 && strncmp(name, "get-call-stack", length) == 0) {
        return CONTROL_COMMAND_GET_CALL_STACK;
    }
    if (length == 8 && strncmp(name, "key-down", length) == 0) {
        return CONTROL_COMMAND_KEY_DOWN;
    }
    if (length == 6 && strncmp(name, "key-up", length) == 0) {
        return CONTROL_COMMAND_KEY_UP;
    }
    if (length == 7 && strncmp(name, "restore", length) == 0) {
        return CONTROL_COMMAND_RESTORE;
    }
    if (length == 8 && strncmp(name, "joystick", length) == 0) {
        return CONTROL_COMMAND_JOYSTICK;
    }
    if (length == 10 && strncmp(name, "paste-text", length) == 0) {
        return CONTROL_COMMAND_PASTE_TEXT;
    }
    if (length == 12 && strncmp(name, "paste-events", length) == 0) {
        return CONTROL_COMMAND_PASTE_EVENTS;
    }
    if (length == 15 && strncmp(name, "paste-text-data", length) == 0) {
        return CONTROL_COMMAND_PASTE_TEXT_DATA;
    }
    if (length == 17 && strncmp(name, "paste-events-data", length) == 0) {
        return CONTROL_COMMAND_PASTE_EVENTS_DATA;
    }
    if (length == 8 && strncmp(name, "load-prg", length) == 0) {
        return CONTROL_COMMAND_LOAD_PRG;
    }
    if (length == 8 && strncmp(name, "load-bin", length) == 0) {
        return CONTROL_COMMAND_LOAD_BIN;
    }
    if (length == 8 && strncmp(name, "save-bin", length) == 0) {
        return CONTROL_COMMAND_SAVE_BIN;
    }
    if (length == 10 && strncmp(name, "load-state", length) == 0) {
        return CONTROL_COMMAND_LOAD_STATE;
    }
    if (length == 10 && strncmp(name, "save-state", length) == 0) {
        return CONTROL_COMMAND_SAVE_STATE;
    }
    if (length == 9 && strncmp(name, "mount-d64", length) == 0) {
        return CONTROL_COMMAND_MOUNT_D64;
    }
    if (length == 12 && strncmp(name, "unmount-disk", length) == 0) {
        return CONTROL_COMMAND_UNMOUNT_DISK;
    }
    if (length == 11 && strncmp(name, "power-drive", length) == 0) {
        return CONTROL_COMMAND_POWER_DRIVE;
    }
    if (length == 15 && strncmp(name, "get-disk-status", length) == 0) {
        return CONTROL_COMMAND_GET_DISK_STATUS;
    }
    if (length == 13 && strncmp(name, "get-drive-cpu", length) == 0) {
        return CONTROL_COMMAND_GET_DRIVE_CPU;
    }
    if (length == 10 && strncmp(name, "break-exec", length) == 0) {
        return CONTROL_COMMAND_BREAK_EXEC;
    }
    if (length == 11 && strncmp(name, "break-clear", length) == 0) {
        return CONTROL_COMMAND_BREAK_CLEAR;
    }
    if (length == 12 && strncmp(name, "break-enable", length) == 0) {
        return CONTROL_COMMAND_BREAK_ENABLE;
    }
    if ((length == 10 && strncmp(name, "break-list", length) == 0) ||
        (length == 15 && strncmp(name, "get-breakpoints", length) == 0)) {
        return CONTROL_COMMAND_BREAK_LIST;
    }
    if (length == 15 && strncmp(name, "break-clear-all", length) == 0) {
        return CONTROL_COMMAND_BREAK_CLEAR_ALL;
    }
    if (length == 12 && strncmp(name, "break-create", length) == 0) {
        return CONTROL_COMMAND_BREAK_CREATE;
    }
    if (length == 12 && strncmp(name, "break-update", length) == 0) {
        return CONTROL_COMMAND_BREAK_UPDATE;
    }
    if (length == 14 && strncmp(name, "rearm-oneshots", length) == 0) {
        return CONTROL_COMMAND_REARM_ONESHOTS;
    }
    if (length == 11 && strncmp(name, "wait-paused", length) == 0) {
        return CONTROL_COMMAND_WAIT_PAUSED;
    }
    if (length == 12 && strncmp(name, "wait-running", length) == 0) {
        return CONTROL_COMMAND_WAIT_RUNNING;
    }
    if (length == 10 && strncmp(name, "wait-frame", length) == 0) {
        return CONTROL_COMMAND_WAIT_FRAME;
    }
    if (length == 10 && strncmp(name, "wait-event", length) == 0) {
        return CONTROL_COMMAND_WAIT_EVENT;
    }
    if (length == 8 && strncmp(name, "assemble", length) == 0) {
        return CONTROL_COMMAND_ASSEMBLE;
    }
    if (length == 11 && strncmp(name, "find-symbol", length) == 0) {
        return CONTROL_COMMAND_FIND_SYMBOL;
    }
    return CONTROL_COMMAND_NONE;
}

static bool command_requires_count(control_command_type type)
{
    return type == CONTROL_COMMAND_RUN_CYCLES ||
        type == CONTROL_COMMAND_RUN_INSTRUCTIONS;
}

static bool command_requires_address(control_command_type type)
{
    return type == CONTROL_COMMAND_RUN_TO;
}

static bool command_allows_optional_args(control_command_type type)
{
    return type == CONTROL_COMMAND_GET_FRAME ||
        type == CONTROL_COMMAND_GET_DEBUG_MEMORY;
}

/* Parse one leading "key=value" assembler option token. Returns:
     1  a recognized option was consumed (cursor advanced past the token),
     0  the token is not a recognized option (cursor unchanged; path begins here),
    -1  a recognized option key carried a malformed value. */
static int parse_assemble_option(const char **cursor, control_args *args)
{
    const char *start;
    const char *end;
    const char *value_end;
    size_t length;

    if (!token_bounds(*cursor, &start, &end)) {
        return 0;
    }
    length = (size_t)(end - start);
    if (length > 8 && strncmp(start, "address=", 8) == 0) {
        if (!parse_u16_token(start + 8, &value_end, &args->address) || value_end != end) {
            return -1;
        }
    } else if (length > 12 && strncmp(start, "run-address=", 12) == 0) {
        if (!parse_u16_token(start + 12, &value_end, &args->run_address) || value_end != end) {
            return -1;
        }
        args->has_run_address = true;
    } else if (length > 9 && strncmp(start, "auto-run=", 9) == 0) {
        if (!parse_bool_token(start + 9, &value_end, &args->auto_run) || value_end != end) {
            return -1;
        }
    } else if (length > 6 && strncmp(start, "reset=", 6) == 0) {
        if (!parse_bool_token(start + 6, &value_end, &args->reset_first) || value_end != end) {
            return -1;
        }
    } else {
        return 0;
    }
    *cursor = end;
    return 1;
}

static bool parse_optional_timeout_ms(
    const char **cursor,
    uint32_t *out_timeout_ms)
{
    uint64_t value;

    skip_spaces(cursor);
    if (**cursor == '\0' || **cursor == '\r' || **cursor == '\n') {
        return true;
    }
    if (!parse_u64_token(*cursor, cursor, &value) || value == 0 || value > 600000u) {
        return false;
    }
    *out_timeout_ms = (uint32_t)value;
    skip_spaces(cursor);
    return true;
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

static bool parse_history_u16_range(
    const char *start,
    const char *end,
    uint16_t *out_first,
    uint16_t *out_last) {
    const char *value_end;
    uint16_t first;
    uint16_t last;

    if (!parse_u16_token(start, &value_end, &first)) {
        return false;
    }
    if (value_end == end) {
        *out_first = first;
        *out_last = first;
        return true;
    }
    if (*value_end != '-' ||
        !parse_u16_token(value_end + 1, &value_end, &last) ||
        value_end != end || last < first) {
        return false;
    }
    *out_first = first;
    *out_last = last;
    return true;
}

static bool parse_history_u64_range(
    const char *start,
    const char *end,
    uint64_t *out_first,
    uint64_t *out_last) {
    const char *value_end;
    uint64_t first;
    uint64_t last;

    if (!parse_u64_token(start, &value_end, &first)) {
        return false;
    }
    if (value_end == end) {
        *out_first = first;
        *out_last = first;
        return true;
    }
    if (*value_end != '-' ||
        !parse_u64_token(value_end + 1, &value_end, &last) ||
        value_end != end || last < first) {
        return false;
    }
    *out_first = first;
    *out_last = last;
    return true;
}

static int history_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool parse_history_value(
    const char *start,
    const char *end,
    uint8_t *out_value,
    uint8_t *out_mask) {
    int high;
    int low;
    int mask_high = 15;
    int mask_low = 15;

    if ((size_t)(end - start) != 2u &&
        (size_t)(end - start) != 5u) {
        return false;
    }
    high = history_hex_nibble(start[0]);
    low = history_hex_nibble(start[1]);
    if (high < 0 || low < 0) {
        return false;
    }
    if ((size_t)(end - start) == 5u) {
        if (start[2] != '/') {
            return false;
        }
        mask_high = history_hex_nibble(start[3]);
        mask_low = history_hex_nibble(start[4]);
        if (mask_high < 0 || mask_low < 0) {
            return false;
        }
    }
    *out_mask = (uint8_t)((mask_high << 4) | mask_low);
    *out_value =
        (uint8_t)(((high << 4) | low) & *out_mask);
    return true;
}

static bool history_access_name_mask(
    const char *start,
    size_t length,
    uint16_t *out_mask) {
    static const struct {
        const char *name;
        uint16_t mask;
    } names[] = {
        { "data-read", 1u << 0 },
        { "data-write", 1u << 1 },
        { "opcode", 1u << 2 },
        { "operand", 1u << 3 },
        { "dummy-read", 1u << 4 },
        { "rmw-dummy-write", 1u << 5 },
        { "stack-read", 1u << 6 },
        { "stack-write", 1u << 7 },
        { "vector-read", 1u << 8 },
        { "execute", 1u << 9 },
        { "fetch", (1u << 2) | (1u << 3) },
        { "read", (1u << 0) | (1u << 2) | (1u << 3) |
                  (1u << 4) | (1u << 6) | (1u << 8) },
        { "write", (1u << 1) | (1u << 5) | (1u << 7) },
        { "data", (1u << 0) | (1u << 1) },
    };
    size_t i;

    for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strlen(names[i].name) == length &&
            strncmp(start, names[i].name, length) == 0) {
            *out_mask = names[i].mask;
            return true;
        }
    }
    return false;
}

static bool parse_history_access_list(
    const char *start,
    const char *end,
    uint16_t *out_mask) {
    uint16_t mask = 0u;

    if (start == end) {
        return false;
    }
    while (start < end) {
        const char *item_end = start;
        uint16_t item_mask;
        while (item_end < end && *item_end != ',') {
            item_end++;
        }
        if (item_end == start ||
            !history_access_name_mask(
                start, (size_t)(item_end - start), &item_mask)) {
            return false;
        }
        mask |= item_mask;
        if (item_end == end) {
            break;
        }
        start = item_end + 1;
    }
    *out_mask = mask;
    return mask != 0u;
}

static bool parse_history_opcode_pattern(
    const char *start,
    const char *end,
    control_args *args) {
    uint8_t count = 0u;

    if (start == end) {
        return false;
    }
    while (start < end) {
        const char *item_end = start;
        int high;
        int low;
        uint8_t value = 0u;
        uint8_t mask = 0u;
        while (item_end < end && *item_end != ',') {
            item_end++;
        }
        if ((size_t)(item_end - start) != 2u ||
            count >= CONTROL_HISTORY_MAX_OPCODE_PATTERN) {
            return false;
        }
        if (start[0] != '?') {
            high = history_hex_nibble(start[0]);
            if (high < 0) {
                return false;
            }
            value |= (uint8_t)(high << 4);
            mask |= 0xf0u;
        }
        if (start[1] != '?') {
            low = history_hex_nibble(start[1]);
            if (low < 0) {
                return false;
            }
            value |= (uint8_t)low;
            mask |= 0x0fu;
        }
        args->history_opcode_values[count] = value;
        args->history_opcode_masks[count] = mask;
        count++;
        if (item_end == end) {
            break;
        }
        start = item_end + 1;
    }
    args->history_opcode_pattern_length = count;
    return count != 0u;
}

static bool parse_history_find_options(
    const char **cursor,
    control_args *args,
    const char **out_message) {
    enum {
        SEEN_EPOCH = 1u << 0,
        SEEN_TIMELINE = 1u << 1,
        SEEN_CYCLE = 1u << 2,
        SEEN_FROM = 1u << 3,
        SEEN_DIRECTION = 1u << 4,
        SEEN_PC = 1u << 5,
        SEEN_ADDRESS = 1u << 6,
        SEEN_ACCESS = 1u << 7,
        SEEN_VALUE = 1u << 8,
        SEEN_OPCODES = 1u << 9,
        SEEN_LIMIT = 1u << 10
    };
    uint16_t seen = 0u;

    args->history_limit = 64u;
    args->history_direction = 0u;
    args->history_from_kind = 0u;
    for (;;) {
        const char *start;
        const char *end;
        const char *equals;
        const char *value;
        uint16_t bit = 0u;
        const char *value_end;
        uint64_t number;

        if (!token_bounds(*cursor, &start, &end)) {
            break;
        }
        equals = start;
        while (equals < end && *equals != '=') {
            equals++;
        }
        if (equals == start || equals == end || equals + 1 == end) {
            *out_message = "history option must be key=value";
            return false;
        }
        value = equals + 1;
        if ((size_t)(equals - start) == 5u &&
            strncmp(start, "epoch", 5u) == 0) {
            bit = SEEN_EPOCH;
            if (!parse_u64_token(value, &value_end, &number) ||
                value_end != end) {
                *out_message = "invalid history epoch";
                return false;
            }
            args->history_query_has_epoch = true;
            args->history_query_epoch = number;
        } else if ((size_t)(equals - start) == 8u &&
                   strncmp(start, "timeline", 8u) == 0) {
            bit = SEEN_TIMELINE;
            if (!parse_u64_token(value, &value_end, &number) ||
                value_end != end || number > UINT32_MAX) {
                *out_message = "invalid history timeline";
                return false;
            }
            args->history_query_has_timeline = true;
            args->history_query_timeline = (uint32_t)number;
        } else if ((size_t)(equals - start) == 5u &&
                   strncmp(start, "cycle", 5u) == 0) {
            bit = SEEN_CYCLE;
            if (!parse_history_u64_range(
                    value, end,
                    &args->history_cycle_first,
                    &args->history_cycle_last)) {
                *out_message = "invalid history cycle range";
                return false;
            }
            args->history_query_has_cycle = true;
        } else if ((size_t)(equals - start) == 4u &&
                   strncmp(start, "from", 4u) == 0) {
            bit = SEEN_FROM;
            if ((size_t)(end - value) == 6u &&
                strncmp(value, "oldest", 6u) == 0) {
                args->history_from_kind = 2u;
            } else if ((size_t)(end - value) == 6u &&
                       strncmp(value, "newest", 6u) == 0) {
                args->history_from_kind = 3u;
            } else if (parse_u64_token(value, &value_end, &number) &&
                       value_end == end && number != 0u) {
                args->history_from_kind = 1u;
                args->history_from_id = number;
            } else {
                *out_message = "invalid history start record";
                return false;
            }
        } else if ((size_t)(equals - start) == 9u &&
                   strncmp(start, "direction", 9u) == 0) {
            bit = SEEN_DIRECTION;
            if ((size_t)(end - value) == 8u &&
                strncmp(value, "backward", 8u) == 0) {
                args->history_direction = 0u;
            } else if ((size_t)(end - value) == 7u &&
                       strncmp(value, "forward", 7u) == 0) {
                args->history_direction = 1u;
            } else {
                *out_message = "invalid history direction";
                return false;
            }
        } else if ((size_t)(equals - start) == 2u &&
                   strncmp(start, "pc", 2u) == 0) {
            bit = SEEN_PC;
            if (!parse_history_u16_range(
                    value, end,
                    &args->history_pc_first,
                    &args->history_pc_last)) {
                *out_message = "invalid history PC range";
                return false;
            }
            args->history_query_has_pc = true;
        } else if ((size_t)(equals - start) == 7u &&
                   strncmp(start, "address", 7u) == 0) {
            bit = SEEN_ADDRESS;
            if (!parse_history_u16_range(
                    value, end,
                    &args->history_address_first,
                    &args->history_address_last)) {
                *out_message = "invalid history address range";
                return false;
            }
            args->history_query_has_address = true;
        } else if ((size_t)(equals - start) == 6u &&
                   strncmp(start, "access", 6u) == 0) {
            bit = SEEN_ACCESS;
            if (!parse_history_access_list(
                    value, end, &args->history_access_mask)) {
                *out_message = "invalid history access list";
                return false;
            }
            args->history_query_has_access = true;
        } else if ((size_t)(equals - start) == 5u &&
                   strncmp(start, "value", 5u) == 0) {
            bit = SEEN_VALUE;
            if (!parse_history_value(
                    value, end,
                    &args->history_value,
                    &args->history_value_mask)) {
                *out_message = "invalid history value/mask";
                return false;
            }
            args->history_query_has_value = true;
        } else if ((size_t)(equals - start) == 7u &&
                   strncmp(start, "opcodes", 7u) == 0) {
            bit = SEEN_OPCODES;
            if (!parse_history_opcode_pattern(value, end, args)) {
                *out_message = "invalid history opcode pattern";
                return false;
            }
        } else if ((size_t)(equals - start) == 5u &&
                   strncmp(start, "limit", 5u) == 0) {
            bit = SEEN_LIMIT;
            if (!parse_u64_token(value, &value_end, &number) ||
                value_end != end || number == 0u || number > 256u) {
                *out_message = "history limit must be 1..256";
                return false;
            }
            args->history_limit = (uint16_t)number;
        } else {
            *out_message = "unknown history option";
            return false;
        }
        if ((seen & bit) != 0u) {
            *out_message = "duplicate history option";
            return false;
        }
        seen |= bit;
        *cursor = end;
        skip_spaces(cursor);
    }
    return true;
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
    control_args args;

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
    type = command_from_name(command_start, (size_t)(command_end - command_start));
    if (type == CONTROL_COMMAND_NONE) {
        set_parse_error(out_error, id, "unknown-command", "unknown command");
        return false;
    }

    memset(&args, 0, sizeof(args));
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (command_requires_count(type)) {
        if (!parse_u64_token(cursor, &cursor, &args.count) || args.count == 0) {
            set_parse_error(out_error, id, "bad-args", "expected positive count");
            return false;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
    } else if (command_requires_address(type)) {
        if (!parse_u16_token(cursor, &cursor, &args.address)) {
            set_parse_error(out_error, id, "bad-args", "expected 16-bit address");
            return false;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
    } else if (type == CONTROL_COMMAND_HISTORY_RECORD) {
        if (strncmp(cursor, "on", 2) == 0 &&
            (cursor[2] == '\0' || cursor[2] == ' ' || cursor[2] == '\t' ||
             cursor[2] == '\r' || cursor[2] == '\n')) {
            args.history_record_enabled = true;
            cursor += 2;
        } else if (strncmp(cursor, "off", 3) == 0) {
            args.history_record_enabled = false;
            cursor += 3;
        } else {
            set_parse_error(out_error, id, "bad-args", "expected on|off");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_HISTORY_FIND) {
        const char *message = "invalid history query";
        if (!parse_history_find_options(&cursor, &args, &message)) {
            set_parse_error(out_error, id, "bad-args", message);
            return false;
        }
    } else if (type == CONTROL_COMMAND_HISTORY_NEXT) {
        const char *start;
        const char *end;
        const char *value_end;
        uint64_t value;
        if (!parse_u64_token(cursor, &cursor, &args.history_cursor) ||
            args.history_cursor == 0u) {
            set_parse_error(
                out_error, id, "bad-args", "expected nonzero history cursor");
            return false;
        }
        args.history_limit = 64u;
        skip_spaces(&cursor);
        if (token_bounds(cursor, &start, &end)) {
            if ((size_t)(end - start) <= 6u ||
                strncmp(start, "limit=", 6u) != 0 ||
                !parse_u64_token(start + 6u, &value_end, &value) ||
                value_end != end || value == 0u || value > 256u) {
                set_parse_error(
                    out_error, id, "bad-args",
                    "expected optional limit=1..256");
                return false;
            }
            args.history_limit = (uint16_t)value;
            cursor = end;
            skip_spaces(&cursor);
        }
    } else if (type == CONTROL_COMMAND_HISTORY_READ) {
        uint8_t seen = 0u;
        if (!parse_u64_token(cursor, &cursor, &args.history_id) ||
            args.history_id == 0u) {
            set_parse_error(
                out_error, id, "bad-args", "expected retained history record ID");
            return false;
        }
        args.history_before = 32u;
        args.history_after = 8u;
        skip_spaces(&cursor);
        for (;;) {
            const char *start;
            const char *end;
            const char *value_end;
            uint64_t value;
            uint8_t bit;
            if (!token_bounds(cursor, &start, &end)) {
                break;
            }
            if ((size_t)(end - start) > 6u &&
                strncmp(start, "epoch=", 6u) == 0) {
                bit = 1u;
                if (!parse_u64_token(start + 6u, &value_end, &value) ||
                    value_end != end) {
                    set_parse_error(
                        out_error, id, "bad-args", "invalid history epoch");
                    return false;
                }
                args.history_epoch = value;
            } else if ((size_t)(end - start) > 7u &&
                       strncmp(start, "before=", 7u) == 0) {
                bit = 2u;
                if (!parse_u64_token(start + 7u, &value_end, &value) ||
                    value_end != end || value > 256u) {
                    set_parse_error(
                        out_error, id, "bad-args",
                        "history before must be 0..256");
                    return false;
                }
                args.history_before = (uint16_t)value;
            } else if ((size_t)(end - start) > 6u &&
                       strncmp(start, "after=", 6u) == 0) {
                bit = 4u;
                if (!parse_u64_token(start + 6u, &value_end, &value) ||
                    value_end != end || value > 256u) {
                    set_parse_error(
                        out_error, id, "bad-args",
                        "history after must be 0..256");
                    return false;
                }
                args.history_after = (uint16_t)value;
            } else {
                set_parse_error(
                    out_error, id, "bad-args", "unknown history-read option");
                return false;
            }
            if ((seen & bit) != 0u) {
                set_parse_error(
                    out_error, id, "bad-args", "duplicate history-read option");
                return false;
            }
            seen |= bit;
            cursor = end;
            skip_spaces(&cursor);
        }
    } else if (type == CONTROL_COMMAND_HISTORY_CLOSE) {
        if (!parse_u64_token(cursor, &cursor, &args.history_cursor)) {
            set_parse_error(
                out_error, id, "bad-args", "expected history cursor");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_RUN_TO_RASTER) {
        uint64_t line = 0;
        uint64_t cycle = 0;
        if (!parse_u64_token(cursor, &cursor, &line) || line > 65535ull) {
            set_parse_error(out_error, id, "bad-args", "expected raster line 0..65535");
            return false;
        }
        args.raster_line = (uint16_t)line;
        args.has_raster_cycle = false;
        args.raster_cycle = 0;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
            if (!parse_u64_token(cursor, &cursor, &cycle) || cycle > 65535ull) {
                set_parse_error(
                    out_error,
                    id,
                    "bad-args",
                    "expected optional cycle-in-line 0..65535");
                return false;
            }
            args.has_raster_cycle = true;
            args.raster_cycle = (uint16_t)cycle;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
        }
    } else if (type == CONTROL_COMMAND_GET_MEMORY ||
               type == CONTROL_COMMAND_SET_MEMORY) {
        uint64_t length = 0;
        if (!parse_u16_token(cursor, &cursor, &args.address)) {
            set_parse_error(out_error, id, "bad-args", "expected 16-bit address");
            return false;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (!parse_u64_token(cursor, &cursor, &length)) {
            set_parse_error(out_error, id, "bad-args", "expected length");
            return false;
        }
        if (type == CONTROL_COMMAND_SET_MEMORY) {
            if (length == 0 || length > 1024) {
                set_parse_error(out_error, id, "bad-args", "expected length 1..1024");
                return false;
            }
        } else {
            /* get-memory: full space allowed; reject wrap past 65536. */
            if (length == 0 || length > 65536) {
                set_parse_error(
                    out_error,
                    id,
                    "bad-args",
                    "expected length 1..65536");
                return false;
            }
            if ((uint64_t)args.address + length > 65536ull) {
                set_parse_error(
                    out_error,
                    id,
                    "bad-args",
                    "address+length exceeds 65536");
                return false;
            }
        }
        args.length = (uint32_t)length;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (type == CONTROL_COMMAND_SET_MEMORY) {
            /* Writable modes only; rom/drive maps are rejected at parse time. */
            if (!parse_memory_mode_token(cursor, &cursor, &args.memory_mode) ||
                (args.memory_mode != 0u && args.memory_mode != 1u)) {
                set_parse_error(
                    out_error,
                    id,
                    "bad-args",
                    "expected writable memory mode map or ram");
                return false;
            }
        } else if (!parse_memory_mode_token(cursor, &cursor, &args.memory_mode)) {
            set_parse_error(
                out_error,
                id,
                "bad-args",
                "expected memory mode map, ram, rom, drive8, or drive9");
            return false;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
    } else if (type == CONTROL_COMMAND_GET_FRAME) {
        args.frame_format = CONTROL_FRAME_FORMAT_ARGB8888;
        if (strncmp(cursor, "format=argb8888", 15) == 0) {
            args.frame_format = CONTROL_FRAME_FORMAT_ARGB8888;
            cursor += 15;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
        } else if (strncmp(cursor, "format=indexed8", 15) == 0) {
            args.frame_format = CONTROL_FRAME_FORMAT_INDEXED8;
            cursor += 15;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
        }
    } else if (type == CONTROL_COMMAND_GET_CIA) {
        if (!parse_u8_token(cursor, &cursor, &args.cia_index) ||
            (args.cia_index != 1u && args.cia_index != 2u)) {
            set_parse_error(out_error, id, "bad-args", "expected CIA index 1 or 2");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_SET_TURBO) {
        uint64_t multiplier = 0;
        if (!parse_u64_token(cursor, &cursor, &multiplier) ||
            multiplier < 1u || multiplier > 3u) {
            set_parse_error(out_error, id, "bad-args", "expected turbo mode 1..3 (1=normal,2=max,3=warp)");
            return false;
        }
        args.turbo_multiplier = (uint16_t)multiplier;
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_GET_DEBUG_MEMORY) {
        if (strncmp(cursor, "write-history=0", 15) == 0) {
            args.include_write_history = false;
            cursor += 15;
        } else if (strncmp(cursor, "write-history=1", 15) == 0) {
            args.include_write_history = true;
            cursor += 15;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
    } else if (type == CONTROL_COMMAND_KEY_DOWN || type == CONTROL_COMMAND_KEY_UP) {
        if (!parse_key_token(cursor, &cursor, &args.key)) {
            set_parse_error(out_error, id, "bad-args", "expected C64 key name");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_JOYSTICK) {
        if (!parse_u8_token(cursor, &cursor, &args.port) ||
            (args.port != 1u && args.port != 2u)) {
            set_parse_error(out_error, id, "bad-args", "expected joystick port 1 or 2");
            return false;
        }
        skip_spaces(&cursor);
        if (!parse_u8_token(cursor, &cursor, &args.mask)) {
            set_parse_error(out_error, id, "bad-args", "expected joystick mask");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_PASTE_TEXT ||
               type == CONTROL_COMMAND_PASTE_EVENTS) {
        skip_spaces(&cursor);
        if (*cursor == '\0' || *cursor == '\r' || *cursor == '\n') {
            set_parse_error(out_error, id, "bad-args", "expected text");
            return false;
        }
        {
            const char *end = cursor;
            size_t length;
            while (*end != '\0' && *end != '\r' && *end != '\n') {
                end++;
            }
            length = (size_t)(end - cursor);
            if (length >= sizeof(args.text)) {
                set_parse_error(out_error, id, "bad-args", "text too long");
                return false;
            }
            memcpy(args.text, cursor, length);
            args.text[length] = '\0';
            cursor = end;
        }
    } else if (type == CONTROL_COMMAND_PASTE_TEXT_DATA ||
               type == CONTROL_COMMAND_PASTE_EVENTS_DATA) {
        if (!parse_u64_token(cursor, &cursor, &args.count) ||
            args.count == 0 || args.count > 4096) {
            set_parse_error(out_error, id, "bad-args", "expected byte count 1..4096");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_LOAD_PRG ||
               type == CONTROL_COMMAND_LOAD_STATE ||
               type == CONTROL_COMMAND_SAVE_STATE) {
        if (!copy_rest_argument(cursor, &cursor, args.text, sizeof(args.text))) {
            set_parse_error(out_error, id, "bad-args", "expected path");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_LOAD_BIN) {
        const char *tokens[4];
        const char *token_end;
        if (!split_trailing_tokens(cursor, 4, args.text, sizeof(args.text), tokens)) {
            set_parse_error(out_error, id, "bad-args", "expected path and load arguments");
            return false;
        }
        if (!parse_u16_token(tokens[0], &token_end, &args.address) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected load address");
            return false;
        }
        if (!parse_bool_token(tokens[1], &token_end, &args.use_file_address) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected use_file_addr flag");
            return false;
        }
        if (!parse_bool_token(tokens[2], &token_end, &args.reset_first) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected reset_first flag");
            return false;
        }
        if (!parse_bool_token(tokens[3], &token_end, &args.is_basic) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected is_basic flag");
            return false;
        }
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
            cursor++;
        }
    } else if (type == CONTROL_COMMAND_SAVE_BIN) {
        const char *tokens[4];
        const char *token_end;
        if (!split_trailing_tokens(cursor, 4, args.text, sizeof(args.text), tokens)) {
            set_parse_error(out_error, id, "bad-args", "expected path and save arguments");
            return false;
        }
        if (!parse_u16_token(tokens[0], &token_end, &args.start_address) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected start address");
            return false;
        }
        if (!parse_u16_token(tokens[1], &token_end, &args.end_address) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected end address");
            return false;
        }
        if (!parse_bool_token(tokens[2], &token_end, &args.write_file_address) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected write_file_addr flag");
            return false;
        }
        if (!parse_bool_token(tokens[3], &token_end, &args.is_basic) ||
            (*token_end != ' ' && *token_end != '\t' &&
             *token_end != '\0' && *token_end != '\r' && *token_end != '\n')) {
            set_parse_error(out_error, id, "bad-args", "expected is_basic flag");
            return false;
        }
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
            cursor++;
        }
    } else if (type == CONTROL_COMMAND_MOUNT_D64) {
        if (!parse_u8_token(cursor, &cursor, &args.device)) {
            set_parse_error(out_error, id, "bad-args", "expected disk device");
            return false;
        }
        skip_spaces(&cursor);
        if (!copy_rest_argument(cursor, &cursor, args.text, sizeof(args.text))) {
            set_parse_error(out_error, id, "bad-args", "expected path");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_UNMOUNT_DISK ||
               type == CONTROL_COMMAND_GET_DISK_STATUS ||
               type == CONTROL_COMMAND_GET_DRIVE_CPU) {
        if (!parse_u8_token(cursor, &cursor, &args.device)) {
            set_parse_error(out_error, id, "bad-args", "expected disk device");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_POWER_DRIVE) {
        char mode[16];
        if (!parse_u8_token(cursor, &cursor, &args.device)) {
            set_parse_error(out_error, id, "bad-args", "expected disk device");
            return false;
        }
        skip_spaces(&cursor);
        args.power_drive_on = true; /* bare `power-drive 8` powers on */
        if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
            size_t n = 0;
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
                   *cursor != '\r' && *cursor != '\n' && n + 1u < sizeof(mode)) {
                mode[n++] = *cursor++;
            }
            mode[n] = '\0';
            skip_spaces(&cursor);
            if (strcmp(mode, "on") == 0 || strcmp(mode, "1") == 0) {
                args.power_drive_on = true;
            } else if (strcmp(mode, "off") == 0 || strcmp(mode, "0") == 0) {
                args.power_drive_on = false;
            } else {
                set_parse_error(
                    out_error, id, "bad-args", "expected optional on|off after device");
                return false;
            }
        }
    } else if (type == CONTROL_COMMAND_BREAK_EXEC) {
        if (!parse_u16_token(cursor, &cursor, &args.address)) {
            set_parse_error(out_error, id, "bad-args", "expected 16-bit address");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_BREAK_CLEAR) {
        uint64_t break_id = 0;
        if (!parse_u64_token(cursor, &cursor, &break_id) || break_id > 0xffffffffull) {
            set_parse_error(out_error, id, "bad-args", "expected breakpoint id");
            return false;
        }
        args.id = (uint32_t)break_id;
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_BREAK_ENABLE) {
        uint64_t break_id = 0;
        if (!parse_u64_token(cursor, &cursor, &break_id) || break_id > 0xffffffffull) {
            set_parse_error(out_error, id, "bad-args", "expected breakpoint id");
            return false;
        }
        args.id = (uint32_t)break_id;
        skip_spaces(&cursor);
        if (!parse_bool_token(cursor, &cursor, &args.include_write_history)) {
            set_parse_error(out_error, id, "bad-args", "expected enable flag");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_BREAK_CREATE) {
        skip_spaces(&cursor);
        if (*cursor == '\0' || *cursor == '\r' || *cursor == '\n') {
            set_parse_error(out_error, id, "bad-args", "expected breakpoint definition");
            return false;
        }
        {
            const char *end = cursor;
            size_t length;
            while (*end != '\0' && *end != '\r' && *end != '\n') {
                end++;
            }
            length = (size_t)(end - cursor);
            if (length >= sizeof(args.text)) {
                set_parse_error(out_error, id, "bad-args", "definition too long");
                return false;
            }
            memcpy(args.text, cursor, length);
            args.text[length] = '\0';
            cursor = end;
        }
    } else if (type == CONTROL_COMMAND_BREAK_UPDATE) {
        uint64_t break_id = 0;
        if (!parse_u64_token(cursor, &cursor, &break_id) || break_id > 0xffffffffull) {
            set_parse_error(out_error, id, "bad-args", "expected breakpoint id");
            return false;
        }
        args.id = (uint32_t)break_id;
        skip_spaces(&cursor);
        if (*cursor == '\0' || *cursor == '\r' || *cursor == '\n') {
            set_parse_error(out_error, id, "bad-args", "expected breakpoint definition");
            return false;
        }
        {
            const char *end = cursor;
            size_t length;
            while (*end != '\0' && *end != '\r' && *end != '\n') {
                end++;
            }
            length = (size_t)(end - cursor);
            if (length >= sizeof(args.text)) {
                set_parse_error(out_error, id, "bad-args", "definition too long");
                return false;
            }
            memcpy(args.text, cursor, length);
            args.text[length] = '\0';
            cursor = end;
        }
    } else if (type == CONTROL_COMMAND_WAIT_PAUSED ||
               type == CONTROL_COMMAND_WAIT_RUNNING) {
        if (!parse_optional_timeout_ms(&cursor, &args.timeout_ms)) {
            set_parse_error(out_error, id, "bad-args", "expected timeout_ms 1..600000");
            return false;
        }
    } else if (type == CONTROL_COMMAND_WAIT_FRAME) {
        if (!parse_u64_token(cursor, &cursor, &args.count) || args.count == 0) {
            set_parse_error(out_error, id, "bad-args", "expected positive frame delta");
            return false;
        }
        skip_spaces(&cursor);
        if (!parse_optional_timeout_ms(&cursor, &args.timeout_ms)) {
            set_parse_error(out_error, id, "bad-args", "expected timeout_ms 1..600000");
            return false;
        }
    } else if (type == CONTROL_COMMAND_ASSEMBLE) {
        /* Optional key=value settings precede the source path. Defaults mirror
           the Misc->Assembler tab: address $8000, run address = address,
           auto-run off, reset on. */
        args.address = 0x8000u;
        args.run_address = 0x8000u;
        args.reset_first = true;
        for (;;) {
            int opt = parse_assemble_option(&cursor, &args);
            if (opt < 0) {
                set_parse_error(out_error, id, "bad-args", "invalid assembler option");
                return false;
            }
            if (opt == 0) {
                break;
            }
        }
        if (!args.has_run_address) {
            args.run_address = args.address;
        }
        if (!copy_rest_argument(cursor, &cursor, args.text, sizeof(args.text))) {
            set_parse_error(out_error, id, "bad-args", "expected source path");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_FIND_SYMBOL) {
        if (!copy_token(cursor, &cursor, args.text, sizeof(args.text))) {
            set_parse_error(out_error, id, "bad-args", "expected symbol name");
            return false;
        }
        skip_spaces(&cursor);
    } else if (type == CONTROL_COMMAND_WAIT_EVENT) {
        if (!copy_token(cursor, &cursor, args.text, sizeof(args.text))) {
            set_parse_error(out_error, id, "bad-args", "expected event name");
            return false;
        }
        skip_spaces(&cursor);
        if (!parse_optional_timeout_ms(&cursor, &args.timeout_ms)) {
            set_parse_error(out_error, id, "bad-args", "expected timeout_ms 1..600000");
            return false;
        }
    } else if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n' &&
               !command_allows_optional_args(type)) {
        set_parse_error(out_error, id, "bad-args", "unexpected arguments");
        return false;
    }
    if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n') {
        set_parse_error(out_error, id, "bad-args", "unexpected arguments");
        return false;
    }

    memset(out_request, 0, sizeof(*out_request));
    out_request->id = id;
    out_request->type = type;
    out_request->args = args;
    if (type == CONTROL_COMMAND_PASTE_TEXT_DATA ||
        type == CONTROL_COMMAND_PASTE_EVENTS_DATA) {
        out_request->payload_size = (size_t)args.count;
    } else if (type == CONTROL_COMMAND_SET_MEMORY) {
        out_request->payload_size = (size_t)args.length;
    }
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

void control_protocol_format_data(
    control_response *response,
    uint32_t id,
    const char *data_type,
    uint8_t *payload,
    size_t payload_size,
    const char *metadata,
    bool close_client)
{
    if (response == NULL) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->id = id;
    response->type = CONTROL_RESPONSE_DATA;
    response->payload = payload;
    response->payload_size = payload_size;
    response->close_client = close_client;
    if (data_type != NULL) {
        snprintf(response->data_type, sizeof(response->data_type), "%s", data_type);
    }
    if (metadata != NULL) {
        snprintf(response->metadata, sizeof(response->metadata), "%s", metadata);
    }
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
    } else if (response->type == CONTROL_RESPONSE_ERROR) {
        written = snprintf(
            out,
            out_size,
            "%u error %s\n",
            response->id,
            response->text);
    } else {
        if (response->metadata[0] != '\0') {
            written = snprintf(
                out,
                out_size,
                "%u data %s %zu %s\n",
                response->id,
                response->data_type,
                response->payload_size,
                response->metadata);
        } else {
            written = snprintf(
                out,
                out_size,
                "%u data %s %zu\n",
                response->id,
                response->data_type,
                response->payload_size);
        }
    }

    return written >= 0 && (size_t)written < out_size;
}

void control_response_release(control_response *response)
{
    if (response == NULL) {
        return;
    }
    free(response->payload);
    response->payload = NULL;
    response->payload_size = 0;
}

void control_request_release(control_request *request)
{
    if (request == NULL) {
        return;
    }
    free(request->payload);
    request->payload = NULL;
    request->payload_size = 0;
}
