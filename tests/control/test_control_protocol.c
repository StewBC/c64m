#include "control_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, bool value)
{
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_false(const char *name, bool value)
{
    if (value) {
        fprintf(stderr, "%s: expected false\n", name);
        exit(1);
    }
}

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_int(const char *name, int expected, int actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        exit(1);
    }
}

static void expect_string(const char *name, const char *expected, const char *actual)
{
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n", name, expected, actual);
        exit(1);
    }
}

static void test_parse_known_commands(void)
{
    control_request request;
    control_response error;

    expect_true("parse hello", control_protocol_parse_request("1 hello\n", &request, &error));
    expect_u32("hello id", 1, request.id);
    expect_int("hello type", CONTROL_COMMAND_HELLO, request.type);

    expect_true("parse version", control_protocol_parse_request("2 version\r\n", &request, &error));
    expect_u32("version id", 2, request.id);
    expect_int("version type", CONTROL_COMMAND_VERSION, request.type);

    expect_true("parse capabilities", control_protocol_parse_request("3 capabilities", &request, &error));
    expect_int("capabilities type", CONTROL_COMMAND_CAPABILITIES, request.type);

    expect_true("parse ping", control_protocol_parse_request("4 ping", &request, &error));
    expect_int("ping type", CONTROL_COMMAND_PING, request.type);

    expect_true("parse quit-client", control_protocol_parse_request("5 quit-client", &request, &error));
    expect_int("quit-client type", CONTROL_COMMAND_QUIT_CLIENT, request.type);

    expect_true("parse reset", control_protocol_parse_request("6 reset", &request, &error));
    expect_int("reset type", CONTROL_COMMAND_RESET, request.type);

    expect_true("parse run", control_protocol_parse_request("7 run", &request, &error));
    expect_int("run type", CONTROL_COMMAND_RUN, request.type);

    expect_true("parse pause", control_protocol_parse_request("8 pause", &request, &error));
    expect_int("pause type", CONTROL_COMMAND_PAUSE, request.type);

    expect_true("parse step-cycle", control_protocol_parse_request("9 step-cycle", &request, &error));
    expect_int("step-cycle type", CONTROL_COMMAND_STEP_CYCLE, request.type);

    expect_true("parse step-instruction", control_protocol_parse_request("10 step-instruction", &request, &error));
    expect_int("step-instruction type", CONTROL_COMMAND_STEP_INSTRUCTION, request.type);

    expect_true("parse step-over", control_protocol_parse_request("11 step-over", &request, &error));
    expect_int("step-over type", CONTROL_COMMAND_STEP_OVER, request.type);

    expect_true("parse step-out", control_protocol_parse_request("12 step-out", &request, &error));
    expect_int("step-out type", CONTROL_COMMAND_STEP_OUT, request.type);

    expect_true("parse get-state", control_protocol_parse_request("13 get-state", &request, &error));
    expect_int("get-state type", CONTROL_COMMAND_GET_STATE, request.type);

    expect_true("parse get-cpu", control_protocol_parse_request("14 get-cpu", &request, &error));
    expect_int("get-cpu type", CONTROL_COMMAND_GET_CPU, request.type);

    expect_true("parse get-frame", control_protocol_parse_request("15 get-frame", &request, &error));
    expect_int("get-frame type", CONTROL_COMMAND_GET_FRAME, request.type);

    expect_true("parse get-frame format", control_protocol_parse_request("16 get-frame format=argb8888", &request, &error));
    expect_int("get-frame format type", CONTROL_COMMAND_GET_FRAME, request.type);

    expect_true("parse get-debug-memory", control_protocol_parse_request("17 get-debug-memory", &request, &error));
    expect_int("get-debug-memory type", CONTROL_COMMAND_GET_DEBUG_MEMORY, request.type);

    expect_true("parse get-debug-memory history", control_protocol_parse_request("18 get-debug-memory write-history=1", &request, &error));
    expect_int("get-debug-memory history type", CONTROL_COMMAND_GET_DEBUG_MEMORY, request.type);
    expect_true("get-debug-memory include history", request.args.include_write_history);

    expect_true("parse get-call-stack", control_protocol_parse_request("19 get-call-stack", &request, &error));
    expect_int("get-call-stack type", CONTROL_COMMAND_GET_CALL_STACK, request.type);

    expect_true("parse restore", control_protocol_parse_request("30 restore", &request, &error));
    expect_int("restore type", CONTROL_COMMAND_RESTORE, request.type);

    expect_true("parse break-list", control_protocol_parse_request("50 break-list", &request, &error));
    expect_int("break-list type", CONTROL_COMMAND_BREAK_LIST, request.type);

    expect_true("parse get-breakpoints", control_protocol_parse_request("51 get-breakpoints", &request, &error));
    expect_int("get-breakpoints type", CONTROL_COMMAND_BREAK_LIST, request.type);

    expect_true("parse break-clear-all", control_protocol_parse_request("52 break-clear-all", &request, &error));
    expect_int("break-clear-all type", CONTROL_COMMAND_BREAK_CLEAR_ALL, request.type);

    expect_true("parse rearm-oneshots", control_protocol_parse_request("53 rearm-oneshots", &request, &error));
    expect_int("rearm-oneshots type", CONTROL_COMMAND_REARM_ONESHOTS, request.type);

    expect_true("parse wait-paused", control_protocol_parse_request("54 wait-paused", &request, &error));
    expect_int("wait-paused type", CONTROL_COMMAND_WAIT_PAUSED, request.type);

    expect_true("parse wait-running", control_protocol_parse_request("55 wait-running", &request, &error));
    expect_int("wait-running type", CONTROL_COMMAND_WAIT_RUNNING, request.type);

    expect_true("parse wait-frame", control_protocol_parse_request("56 wait-frame 1", &request, &error));
    expect_int("wait-frame type", CONTROL_COMMAND_WAIT_FRAME, request.type);

    expect_true("parse wait-event", control_protocol_parse_request("57 wait-event frame", &request, &error));
    expect_int("wait-event type", CONTROL_COMMAND_WAIT_EVENT, request.type);
}

static void test_parse_command_arguments(void)
{
    control_request request;
    control_response error;

    expect_true("parse run-cycles", control_protocol_parse_request("20 run-cycles 123", &request, &error));
    expect_int("run-cycles type", CONTROL_COMMAND_RUN_CYCLES, request.type);
    expect_u32("run-cycles low count", 123, (uint32_t)request.args.count);

    expect_true("parse run-instructions", control_protocol_parse_request("21 run-instructions 42", &request, &error));
    expect_int("run-instructions type", CONTROL_COMMAND_RUN_INSTRUCTIONS, request.type);
    expect_u32("run-instructions low count", 42, (uint32_t)request.args.count);

    expect_true("parse run-to hex", control_protocol_parse_request("22 run-to 0xC000", &request, &error));
    expect_int("run-to type", CONTROL_COMMAND_RUN_TO, request.type);
    expect_u32("run-to hex address", 0xc000u, request.args.address);

    expect_true("parse run-to dollar hex", control_protocol_parse_request("23 run-to $0801", &request, &error));
    expect_u32("run-to dollar address", 0x0801u, request.args.address);

    expect_true("parse get-memory", control_protocol_parse_request("24 get-memory $0400 64 map", &request, &error));
    expect_int("get-memory type", CONTROL_COMMAND_GET_MEMORY, request.type);
    expect_u32("get-memory address", 0x0400u, request.args.address);
    expect_u32("get-memory length", 64u, request.args.length);
    expect_u32("get-memory mode", 0u, request.args.memory_mode);

    expect_true("parse get-memory ram", control_protocol_parse_request("25 get-memory 0x0801 16 ram", &request, &error));
    expect_u32("get-memory ram mode", 1u, request.args.memory_mode);

    expect_true("parse get-memory rom", control_protocol_parse_request("26 get-memory 0xE000 8 rom", &request, &error));
    expect_u32("get-memory rom mode", 2u, request.args.memory_mode);

    expect_true("parse key-down", control_protocol_parse_request("30 key-down return", &request, &error));
    expect_int("key-down type", CONTROL_COMMAND_KEY_DOWN, request.type);
    expect_u32("key-down return", 37u, request.args.key);

    expect_true("parse key-up", control_protocol_parse_request("31 key-up a", &request, &error));
    expect_int("key-up type", CONTROL_COMMAND_KEY_UP, request.type);
    expect_u32("key-up a", 0u, request.args.key);

    expect_true("parse joystick", control_protocol_parse_request("32 joystick 2 17", &request, &error));
    expect_int("joystick type", CONTROL_COMMAND_JOYSTICK, request.type);
    expect_u32("joystick port", 2u, request.args.port);
    expect_u32("joystick mask", 17u, request.args.mask);

    expect_true("parse paste-text", control_protocol_parse_request("33 paste-text HELLO WORLD", &request, &error));
    expect_int("paste-text type", CONTROL_COMMAND_PASTE_TEXT, request.type);
    expect_string("paste-text text", "HELLO WORLD", request.args.text);

    expect_true("parse paste-events", control_protocol_parse_request("34 paste-events A\\[RT]", &request, &error));
    expect_int("paste-events type", CONTROL_COMMAND_PASTE_EVENTS, request.type);
    expect_string("paste-events text", "A\\[RT]", request.args.text);

    expect_true("parse paste-text-data", control_protocol_parse_request("35 paste-text-data 12", &request, &error));
    expect_int("paste-text-data type", CONTROL_COMMAND_PASTE_TEXT_DATA, request.type);
    expect_u32("paste-text-data count", 12u, (uint32_t)request.payload_size);

    expect_true("parse paste-events-data", control_protocol_parse_request("36 paste-events-data 8", &request, &error));
    expect_int("paste-events-data type", CONTROL_COMMAND_PASTE_EVENTS_DATA, request.type);
    expect_u32("paste-events-data count", 8u, (uint32_t)request.payload_size);

    expect_true("parse load-prg", control_protocol_parse_request("37 load-prg game.prg", &request, &error));
    expect_int("load-prg type", CONTROL_COMMAND_LOAD_PRG, request.type);
    expect_string("load-prg path", "game.prg", request.args.text);

    expect_true("parse load-bin", control_protocol_parse_request("38 load-bin data.bin $0801 1 0 1", &request, &error));
    expect_int("load-bin type", CONTROL_COMMAND_LOAD_BIN, request.type);
    expect_u32("load-bin addr", 0x0801u, request.args.address);
    expect_true("load-bin file addr", request.args.use_file_address);
    expect_false("load-bin reset", request.args.reset_first);
    expect_true("load-bin basic", request.args.is_basic);

    expect_true("parse save-bin", control_protocol_parse_request("39 save-bin out.prg $0801 $0900 true false", &request, &error));
    expect_int("save-bin type", CONTROL_COMMAND_SAVE_BIN, request.type);
    expect_u32("save-bin start", 0x0801u, request.args.start_address);
    expect_u32("save-bin end", 0x0900u, request.args.end_address);
    expect_true("save-bin write addr", request.args.write_file_address);
    expect_false("save-bin basic", request.args.is_basic);

    expect_true("parse mount-d64", control_protocol_parse_request("40 mount-d64 8 disk.d64", &request, &error));
    expect_int("mount-d64 type", CONTROL_COMMAND_MOUNT_D64, request.type);
    expect_u32("mount-d64 device", 8u, request.args.device);

    expect_true("parse unmount-disk", control_protocol_parse_request("41 unmount-disk 8", &request, &error));
    expect_int("unmount-disk type", CONTROL_COMMAND_UNMOUNT_DISK, request.type);

    expect_true("parse get-disk-status", control_protocol_parse_request("42 get-disk-status 9", &request, &error));
    expect_int("get-disk-status type", CONTROL_COMMAND_GET_DISK_STATUS, request.type);
    expect_u32("get-disk-status device", 9u, request.args.device);

    expect_true("parse break-exec", control_protocol_parse_request("50 break-exec $C000", &request, &error));
    expect_int("break-exec type", CONTROL_COMMAND_BREAK_EXEC, request.type);
    expect_u32("break-exec address", 0xc000u, request.args.address);

    expect_true("parse break-clear", control_protocol_parse_request("51 break-clear 7", &request, &error));
    expect_int("break-clear type", CONTROL_COMMAND_BREAK_CLEAR, request.type);
    expect_u32("break-clear id", 7u, request.args.id);

    expect_true("parse break-enable", control_protocol_parse_request("52 break-enable 7 0", &request, &error));
    expect_int("break-enable type", CONTROL_COMMAND_BREAK_ENABLE, request.type);
    expect_u32("break-enable id", 7u, request.args.id);
    expect_false("break-enable flag", request.args.include_write_history);

    expect_true("parse break-create", control_protocol_parse_request("53 break-create exec $C000 actions=break counter=1 reset=0", &request, &error));
    expect_int("break-create type", CONTROL_COMMAND_BREAK_CREATE, request.type);
    expect_string("break-create definition", "exec $C000 actions=break counter=1 reset=0", request.args.text);

    expect_true("parse break-update", control_protocol_parse_request("54 break-update 7 exec $C001 enabled=1", &request, &error));
    expect_int("break-update type", CONTROL_COMMAND_BREAK_UPDATE, request.type);
    expect_u32("break-update id", 7u, request.args.id);
    expect_string("break-update definition", "exec $C001 enabled=1", request.args.text);

    expect_true("parse wait-paused timeout", control_protocol_parse_request("55 wait-paused 5000", &request, &error));
    expect_u32("wait-paused timeout", 5000u, request.args.timeout_ms);

    expect_true("parse wait-running timeout", control_protocol_parse_request("56 wait-running 3000", &request, &error));
    expect_u32("wait-running timeout", 3000u, request.args.timeout_ms);

    expect_true("parse wait-frame timeout", control_protocol_parse_request("57 wait-frame 10 5000", &request, &error));
    expect_u32("wait-frame delta", 10u, (uint32_t)request.args.count);
    expect_u32("wait-frame timeout", 5000u, request.args.timeout_ms);

    expect_true("parse wait-event timeout", control_protocol_parse_request("58 wait-event paused 1000", &request, &error));
    expect_string("wait-event name", "paused", request.args.text);
    expect_u32("wait-event timeout", 1000u, request.args.timeout_ms);
}

static void test_parse_rejects_invalid_input(void)
{
    control_request request;
    control_response error;

    expect_false("reject bad id", control_protocol_parse_request("x ping\n", &request, &error));
    expect_u32("bad id response id", 0, error.id);
    expect_int("bad id response type", CONTROL_RESPONSE_ERROR, error.type);

    expect_false("reject unknown", control_protocol_parse_request("7 frob\n", &request, &error));
    expect_u32("unknown response id", 7, error.id);
    expect_int("unknown response type", CONTROL_RESPONSE_ERROR, error.type);

    expect_false("reject args", control_protocol_parse_request("8 ping extra\n", &request, &error));
    expect_u32("args response id", 8, error.id);
    expect_int("args response type", CONTROL_RESPONSE_ERROR, error.type);

    expect_false("reject missing count", control_protocol_parse_request("9 run-cycles\n", &request, &error));
    expect_u32("missing count response id", 9, error.id);

    expect_false("reject zero count", control_protocol_parse_request("10 run-instructions 0\n", &request, &error));
    expect_u32("zero count response id", 10, error.id);

    expect_false("reject bad address", control_protocol_parse_request("11 run-to 100000\n", &request, &error));
    expect_u32("bad address response id", 11, error.id);

    expect_false("reject memory length", control_protocol_parse_request("12 get-memory $0400 0 map\n", &request, &error));
    expect_u32("memory length response id", 12, error.id);

    expect_false("reject memory mode", control_protocol_parse_request("13 get-memory $0400 8 io\n", &request, &error));
    expect_u32("memory mode response id", 13, error.id);

    expect_false("reject frame format", control_protocol_parse_request("14 get-frame format=rgb\n", &request, &error));
    expect_u32("frame format response id", 14, error.id);

    expect_false("reject bad key", control_protocol_parse_request("15 key-down nope\n", &request, &error));
    expect_u32("bad key response id", 15, error.id);

    expect_false("reject joystick port", control_protocol_parse_request("16 joystick 3 1\n", &request, &error));
    expect_u32("bad joystick response id", 16, error.id);

    expect_false("reject paste payload size", control_protocol_parse_request("17 paste-text-data 4097\n", &request, &error));
    expect_u32("bad paste payload response id", 17, error.id);

    expect_false("reject load-bin flag", control_protocol_parse_request("18 load-bin x $0801 maybe 0 0\n", &request, &error));
    expect_u32("bad load-bin response id", 18, error.id);

    expect_false("reject break-exec address", control_protocol_parse_request("19 break-exec nope\n", &request, &error));
    expect_u32("bad break-exec response id", 19, error.id);

    expect_false("reject break-enable flag", control_protocol_parse_request("20 break-enable 1 maybe\n", &request, &error));
    expect_u32("bad break-enable response id", 20, error.id);

    expect_false("reject break-create empty", control_protocol_parse_request("21 break-create\n", &request, &error));
    expect_u32("bad break-create response id", 21, error.id);

    expect_false("reject wait-frame zero", control_protocol_parse_request("22 wait-frame 0\n", &request, &error));
    expect_u32("bad wait-frame response id", 22, error.id);

    expect_false("reject wait timeout", control_protocol_parse_request("23 wait-paused nope\n", &request, &error));
    expect_u32("bad wait timeout response id", 23, error.id);

    expect_false("reject wait-event missing", control_protocol_parse_request("24 wait-event\n", &request, &error));
    expect_u32("bad wait-event response id", 24, error.id);
}

static void test_response_formatting(void)
{
    control_response response;
    char line[128];

    control_protocol_format_ok(&response, 9, NULL, false);
    expect_true("write ok", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("ok line", "9 ok\n", line);

    control_protocol_format_ok(&response, 10, "protocol=C64M/1", false);
    expect_true("write ok text", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("ok text line", "10 ok protocol=C64M/1\n", line);

    control_protocol_format_error(&response, 11, "bad-request", "missing command", false);
    expect_true("write error", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("error line", "11 error bad-request missing command\n", line);

    control_protocol_format_data(&response, 12, "memory", NULL, 4, "addr=0400 length=4 mode=0", false);
    expect_true("write data", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("data line", "12 data memory 4 addr=0400 length=4 mode=0\n", line);
    control_response_release(&response);
}

int main(void)
{
    test_parse_known_commands();
    test_parse_command_arguments();
    test_parse_rejects_invalid_input();
    test_response_formatting();
    return 0;
}
