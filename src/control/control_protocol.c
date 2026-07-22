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
    if (length == 10 && strncmp(name, "get-memory", length) == 0) {
        return CONTROL_COMMAND_GET_MEMORY;
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
    } else if (type == CONTROL_COMMAND_GET_MEMORY) {
        uint64_t length = 0;
        if (!parse_u16_token(cursor, &cursor, &args.address)) {
            set_parse_error(out_error, id, "bad-args", "expected 16-bit address");
            return false;
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (!parse_u64_token(cursor, &cursor, &length) ||
            length == 0 || length > 1024) {
            set_parse_error(out_error, id, "bad-args", "expected length 1..1024");
            return false;
        }
        args.length = (uint16_t)length;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (!parse_memory_mode_token(cursor, &cursor, &args.memory_mode)) {
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
        if (strncmp(cursor, "format=argb8888", 15) == 0) {
            cursor += 15;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
        }
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
