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

static void expect_u64(const char *name, uint64_t expected, uint64_t actual)
{
    if (expected != actual) {
        fprintf(
            stderr,
            "%s: expected %llu, got %llu\n",
            name,
            (unsigned long long)expected,
            (unsigned long long)actual);
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

    expect_true("parse set-turbo", control_protocol_parse_request("12 set-turbo 1", &request, &error));
    expect_int("set-turbo type", CONTROL_COMMAND_SET_TURBO, request.type);

    expect_true("parse get-state", control_protocol_parse_request("13 get-state", &request, &error));
    expect_int("get-state type", CONTROL_COMMAND_GET_STATE, request.type);

    expect_true(
        "parse leave-inspector",
        control_protocol_parse_request("40 leave-inspector", &request, &error));
    expect_int("leave-inspector type", CONTROL_COMMAND_LEAVE_INSPECTOR, request.type);

    expect_true(
        "parse enter-inspector",
        control_protocol_parse_request("41 enter-inspector", &request, &error));
    expect_int("enter-inspector type", CONTROL_COMMAND_ENTER_INSPECTOR, request.type);

    expect_true("parse get-cpu", control_protocol_parse_request("14 get-cpu", &request, &error));
    expect_int("get-cpu type", CONTROL_COMMAND_GET_CPU, request.type);

    expect_true("parse get-frame", control_protocol_parse_request("15 get-frame", &request, &error));
    expect_int("get-frame type", CONTROL_COMMAND_GET_FRAME, request.type);

    expect_true("parse get-frame format", control_protocol_parse_request("16 get-frame format=argb8888", &request, &error));
    expect_int("get-frame format type", CONTROL_COMMAND_GET_FRAME, request.type);
    expect_u32("get-frame argb format", CONTROL_FRAME_FORMAT_ARGB8888, request.args.frame_format);

    expect_true("parse get-frame indexed8", control_protocol_parse_request("16 get-frame format=indexed8", &request, &error));
    expect_int("get-frame indexed type", CONTROL_COMMAND_GET_FRAME, request.type);
    expect_u32("get-frame indexed format", CONTROL_FRAME_FORMAT_INDEXED8, request.args.frame_format);

    expect_true("parse get-debug-memory", control_protocol_parse_request("17 get-debug-memory", &request, &error));
    expect_int("get-debug-memory type", CONTROL_COMMAND_GET_DEBUG_MEMORY, request.type);

    expect_true("parse get-debug-memory history", control_protocol_parse_request("18 get-debug-memory write-history=1", &request, &error));
    expect_int("get-debug-memory history type", CONTROL_COMMAND_GET_DEBUG_MEMORY, request.type);
    expect_true("get-debug-memory include history", request.args.include_write_history);

    expect_true("parse get-call-stack", control_protocol_parse_request("19 get-call-stack", &request, &error));
    expect_int("get-call-stack type", CONTROL_COMMAND_GET_CALL_STACK, request.type);

    expect_true("parse get-vic", control_protocol_parse_request("70 get-vic", &request, &error));
    expect_int("get-vic type", CONTROL_COMMAND_GET_VIC, request.type);

    expect_true("parse get-cia", control_protocol_parse_request("71 get-cia 1", &request, &error));
    expect_int("get-cia type", CONTROL_COMMAND_GET_CIA, request.type);
    expect_u32("get-cia index", 1u, request.args.cia_index);

    expect_true("parse get-cia 2", control_protocol_parse_request("72 get-cia 2", &request, &error));
    expect_u32("get-cia 2 index", 2u, request.args.cia_index);

    expect_true("parse step-frame", control_protocol_parse_request("73 step-frame", &request, &error));
    expect_int("step-frame type", CONTROL_COMMAND_STEP_FRAME, request.type);

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

    expect_true("parse set-turbo minimum", control_protocol_parse_request("23 set-turbo 1", &request, &error));
    expect_u32("set-turbo minimum", 1u, request.args.turbo_multiplier);

    expect_true("parse set-turbo max mode", control_protocol_parse_request("23 set-turbo 2", &request, &error));
    expect_u32("set-turbo max mode", 2u, request.args.turbo_multiplier);

    expect_true("parse set-turbo warp", control_protocol_parse_request("23 set-turbo 3", &request, &error));
    expect_u32("set-turbo warp", 3u, request.args.turbo_multiplier);

    expect_true("parse get-memory", control_protocol_parse_request("24 get-memory $0400 64 map", &request, &error));
    expect_int("get-memory type", CONTROL_COMMAND_GET_MEMORY, request.type);
    expect_u32("get-memory address", 0x0400u, request.args.address);
    expect_u32("get-memory length", 64u, request.args.length);
    expect_u32("get-memory mode", 0u, request.args.memory_mode);

    expect_true("parse set-memory", control_protocol_parse_request("124 set-memory $0400 16 ram", &request, &error));
    expect_int("set-memory type", CONTROL_COMMAND_SET_MEMORY, request.type);
    expect_u32("set-memory address", 0x0400u, request.args.address);
    expect_u32("set-memory length", 16u, request.args.length);
    expect_u32("set-memory mode", 1u, request.args.memory_mode);
    expect_u32("set-memory payload size", 16u, (uint32_t)request.payload_size);

    expect_true("parse get-memory ram", control_protocol_parse_request("25 get-memory 0x0801 16 ram", &request, &error));
    expect_u32("get-memory ram mode", 1u, request.args.memory_mode);

    expect_true("parse get-memory rom", control_protocol_parse_request("26 get-memory 0xE000 8 rom", &request, &error));
    expect_u32("get-memory rom mode", 2u, request.args.memory_mode);

    expect_true(
        "parse get-memory drive8",
        control_protocol_parse_request("27 get-memory $0160 32 drive8", &request, &error));
    expect_u32("get-memory drive8 mode", 3u, request.args.memory_mode);
    expect_u32("get-memory drive8 address", 0x0160u, request.args.address);

    expect_true(
        "parse get-memory drive9",
        control_protocol_parse_request("28 get-memory $0300 16 drive9", &request, &error));
    expect_u32("get-memory drive9 mode", 4u, request.args.memory_mode);

    expect_true(
        "parse get-drive-cpu",
        control_protocol_parse_request("29 get-drive-cpu 8", &request, &error));
    expect_int("get-drive-cpu type", CONTROL_COMMAND_GET_DRIVE_CPU, request.type);
    expect_u32("get-drive-cpu device", 8u, request.args.device);

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

    expect_true("parse load-prg path spaces", control_protocol_parse_request("37 load-prg assets/prg/Fort Apocalypse.prg", &request, &error));
    expect_string("load-prg spaced path", "assets/prg/Fort Apocalypse.prg", request.args.text);

    expect_true("parse load-bin", control_protocol_parse_request("38 load-bin data.bin $0801 1 0 1", &request, &error));
    expect_int("load-bin type", CONTROL_COMMAND_LOAD_BIN, request.type);
    expect_string("load-bin path", "data.bin", request.args.text);
    expect_u32("load-bin addr", 0x0801u, request.args.address);
    expect_true("load-bin file addr", request.args.use_file_address);
    expect_false("load-bin reset", request.args.reset_first);
    expect_true("load-bin basic", request.args.is_basic);

    expect_true("parse load-bin path spaces", control_protocol_parse_request("38 load-bin assets/prg/Fort Apocalypse.bin $0801 1 0 1", &request, &error));
    expect_string("load-bin spaced path", "assets/prg/Fort Apocalypse.bin", request.args.text);
    expect_u32("load-bin spaced addr", 0x0801u, request.args.address);
    expect_true("load-bin spaced file addr", request.args.use_file_address);
    expect_false("load-bin spaced reset", request.args.reset_first);
    expect_true("load-bin spaced basic", request.args.is_basic);

    expect_true("parse save-bin", control_protocol_parse_request("39 save-bin out.prg $0801 $0900 true false", &request, &error));
    expect_int("save-bin type", CONTROL_COMMAND_SAVE_BIN, request.type);
    expect_string("save-bin path", "out.prg", request.args.text);
    expect_u32("save-bin start", 0x0801u, request.args.start_address);
    expect_u32("save-bin end", 0x0900u, request.args.end_address);
    expect_true("save-bin write addr", request.args.write_file_address);
    expect_false("save-bin basic", request.args.is_basic);

    expect_true("parse save-bin path spaces", control_protocol_parse_request("39 save-bin output/Fort Apocalypse.prg $0801 $0900 true false", &request, &error));
    expect_string("save-bin spaced path", "output/Fort Apocalypse.prg", request.args.text);
    expect_u32("save-bin spaced start", 0x0801u, request.args.start_address);
    expect_u32("save-bin spaced end", 0x0900u, request.args.end_address);
    expect_true("save-bin spaced write addr", request.args.write_file_address);
    expect_false("save-bin spaced basic", request.args.is_basic);

    expect_true("parse load-state", control_protocol_parse_request("43 load-state snap.c64state", &request, &error));
    expect_int("load-state type", CONTROL_COMMAND_LOAD_STATE, request.type);
    expect_string("load-state path", "snap.c64state", request.args.text);

    expect_true("parse load-state path spaces", control_protocol_parse_request("43 load-state states/Fort Apocalypse.c64state", &request, &error));
    expect_string("load-state spaced path", "states/Fort Apocalypse.c64state", request.args.text);

    expect_true("parse save-state", control_protocol_parse_request("44 save-state out.c64state", &request, &error));
    expect_int("save-state type", CONTROL_COMMAND_SAVE_STATE, request.type);
    expect_string("save-state path", "out.c64state", request.args.text);

    expect_true("parse save-state path spaces", control_protocol_parse_request("44 save-state states/Fort Apocalypse.c64state", &request, &error));
    expect_string("save-state spaced path", "states/Fort Apocalypse.c64state", request.args.text);

    expect_true("parse mount-d64", control_protocol_parse_request("40 mount-d64 8 disk.d64", &request, &error));
    expect_int("mount-d64 type", CONTROL_COMMAND_MOUNT_D64, request.type);
    expect_u32("mount-d64 device", 8u, request.args.device);
    expect_string("mount-d64 path", "disk.d64", request.args.text);

    expect_true("parse mount-d64 path spaces", control_protocol_parse_request("40 mount-d64 8 assets/disks/Fort Apocalypse.d64", &request, &error));
    expect_u32("mount-d64 spaced device", 8u, request.args.device);
    expect_string("mount-d64 spaced path", "assets/disks/Fort Apocalypse.d64", request.args.text);

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

    expect_true("parse break-create write", control_protocol_parse_request("53 break-create write $D012 end=$D012 actions=break", &request, &error));
    expect_string("break-create write definition", "write $D012 end=$D012 actions=break", request.args.text);

    expect_true("parse break-create read count", control_protocol_parse_request("53 break-create read $0400 actions=none", &request, &error));
    expect_string("break-create read definition", "read $0400 actions=none", request.args.text);

    expect_true("parse break-create store alias", control_protocol_parse_request("53 break-create store $D000 end=$D02E actions=break", &request, &error));
    expect_string("break-create store definition", "store $D000 end=$D02E actions=break", request.args.text);

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

static void test_parse_assemble_and_symbol(void)
{
    control_request request;
    control_response error;

    /* Bare form: path only, tab defaults applied. */
    expect_true("parse assemble bare", control_protocol_parse_request("60 assemble prog.asm", &request, &error));
    expect_int("assemble type", CONTROL_COMMAND_ASSEMBLE, request.type);
    expect_string("assemble path", "prog.asm", request.args.text);
    expect_u32("assemble default address", 0x8000u, request.args.address);
    expect_u32("assemble default run-address", 0x8000u, request.args.run_address);
    expect_false("assemble default auto-run", request.args.auto_run);
    expect_true("assemble default reset", request.args.reset_first);

    /* All options set; run-address defaults from address when omitted. */
    expect_true("parse assemble options", control_protocol_parse_request("61 assemble address=$C000 auto-run=1 reset=0 prog.asm", &request, &error));
    expect_u32("assemble opt address", 0xc000u, request.args.address);
    expect_u32("assemble opt run-address default", 0xc000u, request.args.run_address);
    expect_true("assemble opt auto-run", request.args.auto_run);
    expect_false("assemble opt basic-run default", request.args.basic_run);
    expect_false("assemble opt reset", request.args.reset_first);
    expect_string("assemble opt path", "prog.asm", request.args.text);

    /* basic-run is a distinct run mode from auto-run. */
    expect_true("parse assemble basic-run", control_protocol_parse_request("65 assemble basic-run=1 reset=0 prog.asm", &request, &error));
    expect_true("assemble basic-run set", request.args.basic_run);
    expect_false("assemble basic-run auto-run clear", request.args.auto_run);

    /* auto-run and basic-run are mutually exclusive. */
    expect_false("reject assemble both run modes",
                 control_protocol_parse_request("66 assemble auto-run=1 basic-run=1 prog.asm", &request, &error));

    /* Explicit run-address overrides the address default. */
    expect_true("parse assemble run-address", control_protocol_parse_request("62 assemble address=$0801 run-address=$080D prog.asm", &request, &error));
    expect_u32("assemble explicit address", 0x0801u, request.args.address);
    expect_u32("assemble explicit run-address", 0x080du, request.args.run_address);

    /* Path with spaces is the rest of the line after the options. */
    expect_true("parse assemble spaced path", control_protocol_parse_request("63 assemble auto-run=1 assets/src/My Demo.asm", &request, &error));
    expect_string("assemble spaced path", "assets/src/My Demo.asm", request.args.text);
    expect_true("assemble spaced auto-run", request.args.auto_run);

    expect_true("parse find-symbol", control_protocol_parse_request("64 find-symbol init", &request, &error));
    expect_int("find-symbol type", CONTROL_COMMAND_FIND_SYMBOL, request.type);
    expect_string("find-symbol name", "init", request.args.text);
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

    expect_false("reject zero turbo", control_protocol_parse_request("11 set-turbo 0\n", &request, &error));
    expect_u32("zero turbo response id", 11, error.id);

    expect_false("reject turbo over maximum", control_protocol_parse_request("11 set-turbo 4\n", &request, &error));
    expect_u32("high turbo response id", 11, error.id);

    expect_false("reject legacy turbo 256", control_protocol_parse_request("11 set-turbo 256\n", &request, &error));
    expect_u32("legacy turbo 256 response id", 11, error.id);

    expect_false("reject missing turbo", control_protocol_parse_request("11 set-turbo\n", &request, &error));
    expect_u32("missing turbo response id", 11, error.id);

    expect_false("reject memory length 0", control_protocol_parse_request("12 get-memory $0400 0 map\n", &request, &error));
    expect_u32("memory length 0 response id", 12, error.id);

    expect_true(
        "accept get-memory 1024",
        control_protocol_parse_request("200 get-memory $0000 1024 map\n", &request, &error));
    expect_u32("get-memory 1024 length", 1024u, request.args.length);

    expect_true(
        "accept get-memory 65536",
        control_protocol_parse_request("201 get-memory $0000 65536 ram\n", &request, &error));
    expect_u32("get-memory 65536 length", 65536u, request.args.length);
    expect_u32("get-memory 65536 mode", 1u, request.args.memory_mode);

    expect_true(
        "accept get-memory FFFF len 1",
        control_protocol_parse_request("202 get-memory $FFFF 1 map\n", &request, &error));
    expect_u32("FFFF+1 address", 0xFFFFu, request.args.address);
    expect_u32("FFFF+1 length", 1u, request.args.length);

    expect_false(
        "reject get-memory 65537",
        control_protocol_parse_request("203 get-memory $0000 65537 map\n", &request, &error));
    expect_u32("65537 response id", 203, error.id);
    expect_false(
        "reject get-memory FFFF len 2",
        control_protocol_parse_request("204 get-memory $FFFF 2 map\n", &request, &error));
    expect_u32("FFFF+2 response id", 204, error.id);
    expect_false(
        "reject get-memory FF00 len 0x101",
        control_protocol_parse_request("205 get-memory $FF00 257 map\n", &request, &error));
    expect_u32("FF00+257 response id", 205, error.id);
    expect_true(
        "accept get-memory FF00 len 0x100",
        control_protocol_parse_request("206 get-memory $FF00 256 map\n", &request, &error));
    expect_u32("FF00+256 length", 256u, request.args.length);

    expect_false(
        "reject set-memory 1025",
        control_protocol_parse_request("207 set-memory $0400 1025 ram\n", &request, &error));
    expect_u32("set-memory 1025 response id", 207, error.id);

    expect_false("reject memory mode", control_protocol_parse_request("13 get-memory $0400 8 io\n", &request, &error));
    expect_u32("memory mode response id", 13, error.id);

    expect_false("reject set-memory rom", control_protocol_parse_request("13 set-memory $0400 8 rom\n", &request, &error));
    expect_u32("set-memory rom response id", 13, error.id);

    expect_false("reject set-memory drive8", control_protocol_parse_request("13 set-memory $0400 8 drive8\n", &request, &error));
    expect_u32("set-memory drive8 response id", 13, error.id);

    expect_false("reject frame format", control_protocol_parse_request("14 get-frame format=rgb\n", &request, &error));
    expect_u32("frame format response id", 14, error.id);

    expect_false("reject get-cia index", control_protocol_parse_request("14 get-cia 3\n", &request, &error));
    expect_u32("bad get-cia response id", 14, error.id);

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

    expect_false("reject assemble missing path", control_protocol_parse_request("25 assemble\n", &request, &error));
    expect_u32("bad assemble response id", 25, error.id);

    expect_false("reject assemble option only", control_protocol_parse_request("26 assemble address=$8000\n", &request, &error));
    expect_u32("bad assemble option response id", 26, error.id);

    expect_false("reject assemble bad option", control_protocol_parse_request("27 assemble auto-run=maybe prog.asm\n", &request, &error));
    expect_u32("bad assemble option value response id", 27, error.id);

    expect_false("reject find-symbol missing", control_protocol_parse_request("28 find-symbol\n", &request, &error));
    expect_u32("bad find-symbol response id", 28, error.id);
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

    control_protocol_format_event(
        &response,
        0u,
        "state-changed reason=step session=2 cycles=12345 frame=1 epoch=1");
    expect_true(
        "write event",
        control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string(
        "event line",
        "0 event state-changed reason=step session=2 cycles=12345 frame=1 epoch=1\n",
        line);
}

static void test_parse_run_to_raster(void)
{
    control_request request;
    control_response error;

    expect_true(
        "parse run-to-raster line",
        control_protocol_parse_request("50 run-to-raster 100\n", &request, &error));
    expect_int("run-to-raster type", CONTROL_COMMAND_RUN_TO_RASTER, request.type);
    expect_u32("run-to-raster line", 100u, request.args.raster_line);
    expect_false("run-to-raster no cycle", request.args.has_raster_cycle);

    expect_true(
        "parse run-to-raster line+cycle",
        control_protocol_parse_request("51 run-to-raster 48 12\n", &request, &error));
    expect_u32("run-to-raster line2", 48u, request.args.raster_line);
    expect_true("run-to-raster has cycle", request.args.has_raster_cycle);
    expect_u32("run-to-raster cycle", 12u, request.args.raster_cycle);

    expect_false(
        "reject run-to-raster missing",
        control_protocol_parse_request("52 run-to-raster\n", &request, &error));
}

static void test_deferred_token_matches(void)
{
    /* Legacy deferred (token 0): any event token is allowed at the gate;
       type matching still applies in main. */
    expect_true("legacy accepts token0", control_deferred_token_matches(0u, 0u));
    expect_true("legacy accepts nonzero", control_deferred_token_matches(0u, 42u));

    /* Solicited deferred: only exact token. */
    expect_true("exact match", control_deferred_token_matches(7u, 7u));
    expect_false("ui token0 steals", control_deferred_token_matches(7u, 0u));
    expect_false("wrong token", control_deferred_token_matches(7u, 8u));
}

static void test_parse_history_commands(void)
{
    control_request request;
    control_response error;
    char long_pattern[256];
    size_t used = 0u;
    unsigned i;
    static const struct {
        const char *name;
        uint16_t mask;
    } access_names[] = {
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

    expect_true(
        "history info",
        control_protocol_parse_request(
            "80 history-info", &request, &error));
    expect_int(
        "history info type", CONTROL_COMMAND_HISTORY_INFO, request.type);
    expect_true(
        "history record on",
        control_protocol_parse_request(
            "81 history-record on", &request, &error));
    expect_int(
        "history record type", CONTROL_COMMAND_HISTORY_RECORD, request.type);
    expect_true("history record enabled", request.args.history_record_enabled);
    expect_true(
        "history record off",
        control_protocol_parse_request(
            "82 history-record off", &request, &error));
    expect_false(
        "history record disabled", request.args.history_record_enabled);
    expect_true(
        "history clear",
        control_protocol_parse_request(
            "83 history-clear", &request, &error));
    expect_int(
        "history clear type", CONTROL_COMMAND_HISTORY_CLEAR, request.type);

    expect_true(
        "history find empty",
        control_protocol_parse_request(
            "84 history-find", &request, &error));
    expect_int(
        "history find type", CONTROL_COMMAND_HISTORY_FIND, request.type);
    expect_u32("history default limit", 64u, request.args.history_limit);
    expect_u32(
        "history default direction", 0u, request.args.history_direction);

    expect_true(
        "history find options",
        control_protocol_parse_request(
            "85 history-find epoch=3 timeline=2 cycle=100-200 "
            "from=oldest direction=forward pc=$E000-$EFFF "
            "address=$D015 access=write,opcode value=A0/F0 "
            "opcodes=A9,??,8D limit=32",
            &request,
            &error));
    expect_true("history epoch present", request.args.history_query_has_epoch);
    expect_u64("history epoch", 3u, request.args.history_query_epoch);
    expect_u32("history timeline", 2u, request.args.history_query_timeline);
    expect_u64("history cycle first", 100u, request.args.history_cycle_first);
    expect_u64("history cycle last", 200u, request.args.history_cycle_last);
    expect_u32("history from oldest", 2u, request.args.history_from_kind);
    expect_u32("history forward", 1u, request.args.history_direction);
    expect_u32("history pc first", 0xe000u, request.args.history_pc_first);
    expect_u32("history pc last", 0xefffu, request.args.history_pc_last);
    expect_u32(
        "history access alias mask",
        (1u << 1) | (1u << 2) | (1u << 5) | (1u << 7),
        request.args.history_access_mask);
    expect_u32("history value", 0xa0u, request.args.history_value);
    expect_u32("history value mask", 0xf0u, request.args.history_value_mask);
    expect_u32(
        "history pattern length",
        3u,
        request.args.history_opcode_pattern_length);
    expect_u32("history wildcard mask", 0u, request.args.history_opcode_masks[1]);
    expect_u32("history limit", 32u, request.args.history_limit);
    for (i = 0u;
         i < sizeof(access_names) / sizeof(access_names[0]);
         ++i) {
        char line[128];
        snprintf(
            line,
            sizeof(line),
            "85 history-find access=%s",
            access_names[i].name);
        expect_true(
            "history canonical/alias access",
            control_protocol_parse_request(line, &request, &error));
        expect_u32(
            "history canonical/alias mask",
            access_names[i].mask,
            request.args.history_access_mask);
    }

    expect_true(
        "history find explicit from and nibbles",
        control_protocol_parse_request(
            "86 history-find from=123 opcodes=A?,?D", &request, &error));
    expect_u32("history from id kind", 1u, request.args.history_from_kind);
    expect_u64("history from id", 123u, request.args.history_from_id);
    expect_u32("history high nibble mask", 0xf0u,
               request.args.history_opcode_masks[0]);
    expect_u32("history low nibble mask", 0x0fu,
               request.args.history_opcode_masks[1]);

    expect_true(
        "history next default",
        control_protocol_parse_request(
            "87 history-next 9", &request, &error));
    expect_int(
        "history next type", CONTROL_COMMAND_HISTORY_NEXT, request.type);
    expect_u64("history next cursor", 9u, request.args.history_cursor);
    expect_u32("history next limit default", 64u, request.args.history_limit);
    expect_true(
        "history next limit",
        control_protocol_parse_request(
            "88 history-next 10 limit=256", &request, &error));
    expect_u32("history next limit 256", 256u, request.args.history_limit);

    expect_true(
        "history read defaults",
        control_protocol_parse_request(
            "89 history-read 42", &request, &error));
    expect_int(
        "history read type", CONTROL_COMMAND_HISTORY_READ, request.type);
    expect_u64("history read id", 42u, request.args.history_id);
    expect_u32("history before default", 32u, request.args.history_before);
    expect_u32("history after default", 8u, request.args.history_after);
    expect_true(
        "history read options",
        control_protocol_parse_request(
            "90 history-read 42 epoch=3 before=0 after=256",
            &request,
            &error));
    expect_u64("history read epoch", 3u, request.args.history_epoch);
    expect_u32("history before zero", 0u, request.args.history_before);
    expect_u32("history after max", 256u, request.args.history_after);

    expect_true(
        "history close",
        control_protocol_parse_request(
            "91 history-close 9", &request, &error));
    expect_int(
        "history close type", CONTROL_COMMAND_HISTORY_CLOSE, request.type);
    expect_u64("history close cursor", 9u, request.args.history_cursor);

    expect_false(
        "legacy set history removed",
        control_protocol_parse_request(
            "92 set-cpu-history on", &request, &error));
    expect_false(
        "legacy get history removed",
        control_protocol_parse_request(
            "93 get-cpu-history", &request, &error));
    expect_false(
        "duplicate history option",
        control_protocol_parse_request(
            "94 history-find pc=$1000 pc=$2000", &request, &error));
    expect_false(
        "unknown history option",
        control_protocol_parse_request(
            "95 history-find nope=1", &request, &error));
    expect_false(
        "reverse history range",
        control_protocol_parse_request(
            "96 history-find address=$2000-$1000", &request, &error));
    expect_false(
        "bad history value",
        control_protocol_parse_request(
            "97 history-find value=A/F0", &request, &error));
    expect_false(
        "history next garbage",
        control_protocol_parse_request(
            "98 history-next 1 limit=2 garbage", &request, &error));
    expect_false(
        "history read duplicate",
        control_protocol_parse_request(
            "99 history-read 1 before=2 before=3", &request, &error));

    used = (size_t)snprintf(
        long_pattern, sizeof(long_pattern), "100 history-find opcodes=");
    for (i = 0u; i < 33u; ++i) {
        used += (size_t)snprintf(
            long_pattern + used,
            sizeof(long_pattern) - used,
            "%sEA",
            i == 0u ? "" : ",");
    }
    expect_false(
        "history opcode limit",
        control_protocol_parse_request(
            long_pattern, &request, &error));
}

int main(void)
{
    test_parse_known_commands();
    test_parse_command_arguments();
    test_parse_assemble_and_symbol();
    test_parse_rejects_invalid_input();
    test_response_formatting();
    test_deferred_token_matches();
    test_parse_run_to_raster();
    test_parse_history_commands();
    return 0;
}
