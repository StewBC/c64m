#include "c64.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
};

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %04x, got %04x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %llu, got %llu\n", name, (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

static void expect_u64_gt(const char *name, uint64_t lhs, uint64_t rhs) {
    if (lhs <= rhs) {
        fprintf(stderr, "%s: expected %llu > %llu\n", name, (unsigned long long)lhs, (unsigned long long)rhs);
        exit(1);
    }
}

static void test_runtime_turbo_csv_config(void) {
    runtime_config config = {0};

    expect_true("parse turbo csv", runtime_config_set_turbo_csv(&config, "2, 4,8"));
    expect_u8("turbo count", 3, config.turbo_speed_count);
    expect_u64("turbo first speed", 2, config.turbo_speeds[0]);
    expect_u64("turbo second speed", 4, config.turbo_speeds[1]);
    expect_u64("turbo active", 2, config.active_turbo_multiplier);

    expect_true("parse empty turbo csv", runtime_config_set_turbo_csv(&config, ""));
    expect_u8("empty turbo count", 1, config.turbo_speed_count);
    expect_u64("empty turbo active", 1, config.active_turbo_multiplier);

    if (runtime_config_set_turbo_csv(&config, "2,nope,8")) {
        fail("invalid turbo csv accepted");
    }
    expect_u8("invalid turbo count fallback", 1, config.turbo_speed_count);
    expect_u64("invalid turbo active fallback", 1, config.active_turbo_multiplier);
}

static uint32_t first_breakpoint_id(const runtime_event *event) {
    if (event->data.breakpoints.count == 0) {
        fail("expected at least one breakpoint");
    }
    return event->data.breakpoints.entries[0].id;
}

static void poll_breakpoint_count(runtime_client *client, runtime_event *event, uint16_t count) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE &&
                event->data.breakpoints.count == count) {
                return;
            }
        }
    }

    fail("expected breakpoint snapshot count not received");
}

static void build_roms(c64_rom_set *roms, uint16_t reset_vector) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffc] = (uint8_t)(reset_vector & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(reset_vector >> 8);
    roms->kernal[reset_vector - 0xe000] = 0xea;
}

static void reset_machine(c64_t *machine, const c64_rom_set *roms) {
    char error[256];

    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void test_single_machine_cycle(void) {
    c64_rom_set roms;
    c64_t machine;
    c64_machine_snapshot before;
    c64_machine_snapshot after;
    char error[256];

    build_roms(&roms, TEST_RESET_VECTOR);
    reset_machine(&machine, &roms);
    c64_copy_machine_snapshot(&machine, &before);
    expect_true("step one cycle", c64_step_cycle(&machine, error, sizeof(error)));
    c64_copy_machine_snapshot(&machine, &after);

    expect_u64("one machine cycle", before.cycle + 1, after.cycle);
    expect_u64("one scheduled CPU cycle", before.cpu_cycles + 1, after.cpu_cycles);
}

static void write_runtime_roms(void) {
    FILE *system = fopen("scheduler_64c.bin", "wb");
    FILE *character = fopen("scheduler_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create scheduler test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }

    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);

    for (i = 0; i < C64_CHAR_ROM_SIZE; i++) {
        fputc(0x00, character);
    }

    fclose(system);
    fclose(character);
}

static void patch_runtime_kernal_reset_code(const uint8_t *bytes, size_t count) {
    FILE *system = fopen("scheduler_64c.bin", "r+b");
    size_t i;

    if (!system) {
        fail("failed to patch scheduler test ROM");
    }

    fseek(system, (long)C64_BASIC_ROM_SIZE, SEEK_SET);
    for (i = 0; i < count; ++i) {
        fputc(bytes[i], system);
    }

    fclose(system);
}

static void write_test_prg(void) {
    FILE *prg = fopen("scheduler_test.prg", "wb");

    if (!prg) {
        fail("failed to create scheduler test PRG");
    }

    fputc(0x00, prg);
    fputc(0x20, prg);
    fputc(0x01, prg);
    fputc(0x02, prg);
    fputc(0x03, prg);
    fclose(prg);
}

static int poll_event(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static void drain_runtime_events(runtime_client *client) {
    runtime_event event;

    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            fprintf(stderr, "runtime error: %s\n", event.data.error.message);
            exit(1);
        }
    }
}

static runtime *start_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = "scheduler_64c.bin",
        .char_rom_path = "scheduler_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (!rt) {
        fail("runtime_create failed");
    }

    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("startup RESET_COMPLETE event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("startup CPU state not received");
    }
    drain_runtime_events(client);

    expect_true("runtime reset", runtime_client_reset(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("RESET_COMPLETE event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("reset CPU state not received");
    }
    drain_runtime_events(client);

    *out_client = client;
    return rt;
}

static runtime *start_runtime_with_turbo(runtime_client **out_client, const char *turbo_csv) {
    runtime_config config = {
        .system_rom_path = "scheduler_64c.bin",
        .char_rom_path = "scheduler_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    expect_true("parse runtime turbo test csv", runtime_config_set_turbo_csv(&config, turbo_csv));
    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (!rt) {
        fail("runtime_create failed");
    }

    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("startup RESET_COMPLETE event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("startup CPU state not received");
    }
    drain_runtime_events(client);

    *out_client = client;
    return rt;
}

static runtime *start_runtime_with_ini(runtime_client **out_client, const char *ini_path, bool save_ini) {
    runtime_config config = {
        .system_rom_path = "scheduler_64c.bin",
        .char_rom_path = "scheduler_character.bin",
        .ini_path = ini_path,
        .use_ini = true,
        .save_ini = save_ini,
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (!rt) {
        fail("runtime_create failed");
    }

    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("startup RESET_COMPLETE event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("startup CPU_STATE_RESPONSE event not received");
    }

    *out_client = client;
    return rt;
}

static void expect_turbo_multiplier(runtime_client *client, uint32_t expected) {
    runtime_event event;

    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("turbo machine state not received");
    }
    expect_u64("active turbo multiplier", expected, event.data.machine_state.active_turbo_multiplier);
}

static void request_and_expect_turbo_multiplier(runtime_client *client, uint32_t expected) {
    expect_true("request machine state for turbo", runtime_client_request_machine_state(client));
    expect_turbo_multiplier(client, expected);
}

static void stop_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
}

static void test_runtime_run_for_cycles(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("request initial machine state", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("initial machine state not received");
    }
    expect_u16("runtime reset PC", TEST_RESET_VECTOR, event.data.machine_state.pc);
    expect_u64("runtime initial cycle", 0, event.data.machine_state.cycle);

    expect_true("run cycles", runtime_client_run_cycles(client, 1000));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE)) {
        fail("RUN_COMPLETE not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("bounded machine state not received");
    }

    expect_u64("bounded run cycle count", 1000, event.data.machine_state.cycle);
    expect_u64("bounded run CPU cycles", 1000, event.data.machine_state.cpu_cycles);
    expect_u64("bounded run pauses", 0, event.data.machine_state.running);

    stop_runtime(rt, client);
}

static void test_runtime_cycle_turbo_speed(void) {
    runtime *rt;
    runtime_client *client;

    rt = start_runtime_with_turbo(&client, "2,4,8");

    request_and_expect_turbo_multiplier(client, 2);
    expect_true("cycle turbo to second speed", runtime_client_cycle_turbo_speed(client));
    expect_turbo_multiplier(client, 4);
    expect_true("cycle turbo to third speed", runtime_client_cycle_turbo_speed(client));
    expect_turbo_multiplier(client, 8);
    expect_true("cycle turbo wraps to first speed", runtime_client_cycle_turbo_speed(client));
    expect_turbo_multiplier(client, 2);

    stop_runtime(rt, client);
}

static void test_runtime_keyboard_event_reaches_machine(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("send key down", runtime_client_keyboard_key(client, C64_KEY_A, true));
    expect_true("send key up", runtime_client_keyboard_key(client, C64_KEY_A, false));
    expect_true("request machine state after key", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine state after key not received");
    }
    expect_u64("runtime keyboard events", 2, event.data.machine_state.keyboard_events);

    stop_runtime(rt, client);
}

static void test_runtime_restore_event_reaches_machine(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("send restore", runtime_client_restore(client));
    expect_true("request machine state after restore", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine state after restore not received");
    }
    expect_u64("runtime restore requests", 1, event.data.machine_state.restore_requests);

    stop_runtime(rt, client);
}

static void test_runtime_run_pause(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint64_t cycle;

    rt = start_runtime(&client);

    expect_true("run command", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received");
    }

    expect_true("pause command", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("paused machine state not received");
    }

    cycle = event.data.machine_state.cycle;
    expect_u64_gt("run advanced cycles before pause", cycle, 0);
    expect_u64("pause snapshot not running", 0, event.data.machine_state.running);

    expect_true("request machine state after pause", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("post-pause machine state not received");
    }
    expect_u64("paused cycle remains stable", cycle, event.data.machine_state.cycle);

    stop_runtime(rt, client);
}

static void test_runtime_reset_resumes_running(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("run before reset", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event before reset not received");
    }

    expect_true("reset while running", runtime_client_reset(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("running reset complete event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event after reset not received");
    }

    expect_true("request machine state after running reset", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine state after running reset not received");
    }
    expect_u64("reset restored running state", 1, event.data.machine_state.running);

    stop_runtime(rt, client);
}

static void test_runtime_step_instruction_from_running_pauses(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint64_t cycle;

    rt = start_runtime(&client);

    expect_true("run before instruction step", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received before instruction step");
    }

    expect_true("step instruction while running", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE)) {
        fail("STEP_COMPLETE not received after running step");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after running step");
    }
    expect_u64_gt("running step advances CPU cycles", event.data.cpu_state.cycles, 0);

    expect_true("request machine state after running step", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine state not received after running step");
    }
    cycle = event.data.machine_state.cycle;
    expect_u64("running step leaves runtime paused", 0, event.data.machine_state.running);

    expect_true("request stable machine state after running step", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("stable machine state not received after running step");
    }
    expect_u64("cycle remains stable after running step", cycle, event.data.machine_state.cycle);

    stop_runtime(rt, client);
}

static void test_runtime_cpu_register_setters_are_paused_only(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("set PC while paused", runtime_client_set_pc(client, 0xc123));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after set PC");
    }
    expect_u16("paused set PC", 0xc123, event.data.cpu_state.pc);

    expect_true("set SP while paused", runtime_client_set_sp(client, 0x7e));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after set SP");
    }
    expect_u8("paused set SP", 0x7e, event.data.cpu_state.sp);

    expect_true("set A while paused", runtime_client_set_a(client, 0x55));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after set A");
    }
    expect_u8("paused set A", 0x55, event.data.cpu_state.a);

    expect_true("set X while paused", runtime_client_set_x(client, 0x66));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after set X");
    }
    expect_u8("paused set X", 0x66, event.data.cpu_state.x);

    expect_true("set Y while paused", runtime_client_set_y(client, 0x77));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after set Y");
    }
    expect_u8("paused set Y", 0x77, event.data.cpu_state.y);

    expect_true("set status while paused", runtime_client_set_status(client, 0xa5));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after set status");
    }
    expect_u8("paused set status", 0xa5, event.data.cpu_state.p);

    expect_true("restore PC before running ignore test", runtime_client_set_pc(client, TEST_RESET_VECTOR));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after restoring PC");
    }
    expect_u16("restored PC before running ignore test", TEST_RESET_VECTOR, event.data.cpu_state.pc);

    expect_true("run before ignored set A", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received before ignored set A");
    }

    expect_true("set A while running", runtime_client_set_a(client, 0x99));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state not received after ignored set A");
    }
    expect_u8("running set A ignored", 0x55, event.data.cpu_state.a);

    expect_true("pause after ignored set A", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received after ignored set A");
    }

    stop_runtime(rt, client);
}

static void test_runtime_memory_snapshots_and_writes_are_paused_only(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("request CPU-map memory", runtime_client_request_memory(
        client,
        TEST_RESET_VECTOR,
        1,
        RUNTIME_MEMORY_MODE_CPU_MAP));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("CPU-map memory response not received");
    }
    expect_u8("CPU-map sees KERNAL ROM", 0xea, event.data.memory.bytes[0]);

    expect_true("request RAM memory", runtime_client_request_memory(
        client,
        TEST_RESET_VECTOR,
        1,
        RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("RAM memory response not received");
    }
    expect_u8("RAM mode sees underlying RAM", 0x00, event.data.memory.bytes[0]);

    expect_true("write RAM while paused", runtime_client_write_memory_byte(
        client,
        0x2000,
        0x42,
        RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("paused RAM write response not received");
    }
    expect_u8("paused RAM write value", 0x42, event.data.memory.bytes[0]);

    expect_true("run before ignored memory write", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received before ignored memory write");
    }

    expect_true("write RAM while running", runtime_client_write_memory_byte(
        client,
        0x2000,
        0x99,
        RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("running RAM write response not received");
    }
    expect_u8("running RAM write ignored", 0x42, event.data.memory.bytes[0]);

    expect_true("pause after ignored memory write", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received after ignored memory write");
    }

    stop_runtime(rt, client);
}

static void test_runtime_stops_on_enabled_execute_breakpoint(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("set execute breakpoint", runtime_client_set_execute_breakpoint(client, TEST_RESET_VECTOR));
    poll_breakpoint_count(client, &event, 1);
    expect_u64("one breakpoint after set", 1, event.data.breakpoints.count);
    expect_u16("breakpoint address", TEST_RESET_VECTOR, event.data.breakpoints.entries[0].address);
    expect_u8("breakpoint enabled", 1, event.data.breakpoints.entries[0].enabled);

    expect_true("run into breakpoint", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received before breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received for breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received for breakpoint");
    }
    expect_u64("breakpoint hit leaves runtime paused", 0, event.data.machine_state.running);
    expect_u16("breakpoint hit PC unchanged", TEST_RESET_VECTOR, event.data.machine_state.pc);
    expect_u64("breakpoint stop reason", RUNTIME_STOP_REASON_BREAKPOINT, event.data.machine_state.stop_reason);
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint hit snapshot not received");
    }
    expect_u64("breakpoint hit counter", 1, event.data.breakpoints.entries[0].current_hits);

    stop_runtime(rt, client);
}

static void test_runtime_ignores_disabled_execute_breakpoint(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t id;

    rt = start_runtime(&client);

    expect_true("set disabled test breakpoint", runtime_client_set_execute_breakpoint(client, TEST_RESET_VECTOR));
    poll_breakpoint_count(client, &event, 1);
    id = first_breakpoint_id(&event);
    expect_true("disable breakpoint", runtime_client_set_breakpoint_enabled(client, id, false));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received after disable");
    }
    expect_u8("breakpoint disabled", 0, event.data.breakpoints.entries[0].enabled);

    expect_true("run cycles past disabled breakpoint", runtime_client_run_cycles(client, 16));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE)) {
        fail("RUN_COMPLETE not received for disabled breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received after disabled breakpoint run");
    }
    expect_u64("disabled breakpoint run advanced", 16, event.data.machine_state.cycle);
    expect_u64("disabled breakpoint run paused", 0, event.data.machine_state.running);

    stop_runtime(rt, client);
}

static void test_runtime_clear_breakpoint_removes_it(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t id;

    rt = start_runtime(&client);

    expect_true("set clear test breakpoint", runtime_client_set_execute_breakpoint(client, TEST_RESET_VECTOR));
    poll_breakpoint_count(client, &event, 1);
    id = first_breakpoint_id(&event);
    expect_true("clear breakpoint", runtime_client_clear_breakpoint(client, id));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received after clear");
    }
    expect_u64("clear removes breakpoint", 0, event.data.breakpoints.count);

    expect_true("run cycles after clear", runtime_client_run_cycles(client, 8));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE)) {
        fail("RUN_COMPLETE not received after clear");
    }

    stop_runtime(rt, client);
}

static void test_runtime_clear_all_breakpoints_removes_all(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("set clear-all breakpoint 1", runtime_client_set_execute_breakpoint(client, TEST_RESET_VECTOR));
    poll_breakpoint_count(client, &event, 1);
    expect_true("set clear-all breakpoint 2", runtime_client_set_execute_breakpoint(client, (uint16_t)(TEST_RESET_VECTOR + 1u)));
    poll_breakpoint_count(client, &event, 2);
    expect_u64("two breakpoints before clear all", 2, event.data.breakpoints.count);

    expect_true("clear all breakpoints", runtime_client_clear_all_breakpoints(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received after clear all");
    }
    expect_u64("clear all removes breakpoints", 0, event.data.breakpoints.count);

    stop_runtime(rt, client);
}

static void test_runtime_create_update_duplicate_breakpoint_definitions(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;
    uint32_t id;

    rt = start_runtime(&client);

    definition.enabled = 1;
    definition.start_address = 0xd000;
    definition.end_address = 0xd0ff;
    definition.has_end_address = 1;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_READ | RUNTIME_BREAKPOINT_ACCESS_WRITE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_RAM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_SWAP | RUNTIME_BREAKPOINT_ACTION_TYPE;
    definition.use_counter = 1;
    definition.initial_count = 10;
    definition.reset_count = 2;

    expect_true("create general breakpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    id = first_breakpoint_id(&event);
    expect_u16("breakpoint start address", 0xd000, event.data.breakpoints.entries[0].start_address);
    expect_u16("breakpoint end address", 0xd0ff, event.data.breakpoints.entries[0].end_address);
    expect_u8("breakpoint has range", 1, event.data.breakpoints.entries[0].has_end_address);
    expect_u64("breakpoint access mask", definition.access, event.data.breakpoints.entries[0].access);
    expect_u64("breakpoint mapping", RUNTIME_BREAKPOINT_MAPPING_RAM, event.data.breakpoints.entries[0].mapping);
    expect_u64("breakpoint actions", definition.actions, event.data.breakpoints.entries[0].actions);
    expect_u8("breakpoint uses counter", 1, event.data.breakpoints.entries[0].use_counter);
    expect_u64("breakpoint initial count", 10, event.data.breakpoints.entries[0].initial_count);
    expect_u64("breakpoint reset count", 2, event.data.breakpoints.entries[0].reset_count);
    expect_u64("breakpoint counter starts at initial", 10, event.data.breakpoints.entries[0].counter);

    expect_true("duplicate general breakpoint", runtime_client_duplicate_breakpoint(client, id));
    poll_breakpoint_count(client, &event, 2);
    expect_u16("duplicate start address", 0xd000, event.data.breakpoints.entries[1].start_address);
    if (event.data.breakpoints.entries[0].id == event.data.breakpoints.entries[1].id) {
        fail("duplicate breakpoint reused runtime id");
    }

    definition.enabled = 0;
    definition.start_address = 0xc000;
    definition.end_address = 0xc000;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 0;
    definition.initial_count = 0;
    definition.reset_count = 0;

    expect_true("update breakpoint by id", runtime_client_update_breakpoint(client, id, &definition));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received after update");
    }
    expect_u16("updated breakpoint start address", 0xc000, event.data.breakpoints.entries[0].start_address);
    expect_u8("updated breakpoint disabled", 0, event.data.breakpoints.entries[0].enabled);
    expect_u64("updated breakpoint access", RUNTIME_BREAKPOINT_ACCESS_EXECUTE, event.data.breakpoints.entries[0].access);

    stop_runtime(rt, client);
}

static void test_runtime_execute_breakpoint_supports_ranges_and_mapping(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;

    write_runtime_roms();
    rt = start_runtime(&client);

    definition.enabled = 1;
    definition.start_address = 0xe000;
    definition.end_address = 0xe0ff;
    definition.has_end_address = 1;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_ROM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 0;
    definition.initial_count = 0;
    definition.reset_count = 0;

    expect_true("create ROM execute range breakpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    expect_true("run into ROM execute range breakpoint", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received before ROM range breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received for ROM range breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received for ROM range breakpoint");
    }
    expect_u16("ROM range breakpoint PC", TEST_RESET_VECTOR, event.data.machine_state.pc);
    expect_u64("ROM range breakpoint stop reason", RUNTIME_STOP_REASON_BREAKPOINT, event.data.machine_state.stop_reason);

    stop_runtime(rt, client);
}

static void test_runtime_read_write_watchpoints_use_access_and_mapping(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;
    const uint8_t lda_abs_2000[] = { 0xad, 0x00, 0x20 };
    const uint8_t sta_abs_2000[] = { 0x8d, 0x00, 0x20 };

    write_runtime_roms();
    patch_runtime_kernal_reset_code(lda_abs_2000, sizeof(lda_abs_2000));
    rt = start_runtime(&client);

    definition.enabled = 1;
    definition.start_address = 0x2000;
    definition.end_address = 0x2000;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_READ;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_RAM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 0;
    definition.initial_count = 0;
    definition.reset_count = 0;

    expect_true("create RAM read watchpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    expect_true("step into RAM read watchpoint", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received for RAM read watchpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received for RAM read watchpoint");
    }
    expect_u64("RAM read watchpoint stop reason", RUNTIME_STOP_REASON_BREAKPOINT, event.data.machine_state.stop_reason);
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received for RAM read watchpoint");
    }
    expect_u64("RAM read watchpoint hit count", 1, event.data.breakpoints.entries[0].current_hits);

    stop_runtime(rt, client);

    write_runtime_roms();
    patch_runtime_kernal_reset_code(sta_abs_2000, sizeof(sta_abs_2000));
    rt = start_runtime(&client);

    definition.access = RUNTIME_BREAKPOINT_ACCESS_WRITE;
    expect_true("create RAM write watchpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    expect_true("step into RAM write watchpoint", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received for RAM write watchpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received for RAM write watchpoint");
    }
    expect_u64("RAM write watchpoint stop reason", RUNTIME_STOP_REASON_BREAKPOINT, event.data.machine_state.stop_reason);
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received for RAM write watchpoint");
    }
    expect_u64("RAM write watchpoint hit count", 1, event.data.breakpoints.entries[0].current_hits);

    stop_runtime(rt, client);
    write_runtime_roms();
}

static void test_runtime_breakpoint_counters_gate_triggers(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;

    write_runtime_roms();
    rt = start_runtime(&client);

    definition.enabled = 1;
    definition.start_address = 0xe000;
    definition.end_address = 0xe0ff;
    definition.has_end_address = 1;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_ROM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 1;
    definition.initial_count = 2;
    definition.reset_count = 2;

    expect_true("create counted execute breakpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);

    expect_true("first counted step does not break", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE)) {
        fail("STEP_COMPLETE not received for first counted step");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU snapshot not received for first counted step");
    }
    expect_true("request counted breakpoint after first hit", runtime_client_request_breakpoints(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received after first counted hit");
    }
    expect_u64("first counted hit count", 1, event.data.breakpoints.entries[0].current_hits);
    expect_u64("first counted counter", 1, event.data.breakpoints.entries[0].counter);

    expect_true("second counted step breaks", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received for counted breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received for counted breakpoint");
    }
    expect_u64("counted breakpoint stop reason", RUNTIME_STOP_REASON_BREAKPOINT, event.data.machine_state.stop_reason);
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received for counted breakpoint");
    }
    expect_u64("second counted hit count", 2, event.data.breakpoints.entries[0].current_hits);
    expect_u64("counted counter reloads", 2, event.data.breakpoints.entries[0].counter);

    stop_runtime(rt, client);
}

static void test_runtime_breakpoint_counter_zero_and_disabled_rules(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;
    uint32_t id;

    write_runtime_roms();
    rt = start_runtime(&client);

    definition.enabled = 1;
    definition.start_address = TEST_RESET_VECTOR;
    definition.end_address = TEST_RESET_VECTOR;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_ROM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 1;
    definition.initial_count = 0;
    definition.reset_count = 0;

    expect_true("create count zero breakpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    expect_true("count zero step breaks immediately", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received for count zero breakpoint");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine snapshot not received for count zero breakpoint");
    }
    expect_u64("count zero breakpoint stop reason", RUNTIME_STOP_REASON_BREAKPOINT, event.data.machine_state.stop_reason);

    stop_runtime(rt, client);

    write_runtime_roms();
    rt = start_runtime(&client);

    definition.enabled = 0;
    definition.initial_count = 2;
    definition.reset_count = 2;
    expect_true("create disabled counted breakpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    id = first_breakpoint_id(&event);
    expect_true("run cycles with disabled counted breakpoint", runtime_client_run_cycles(client, 8));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE)) {
        fail("RUN_COMPLETE not received for disabled counted breakpoint");
    }
    expect_true("request disabled counted breakpoint", runtime_client_request_breakpoints(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received for disabled counted breakpoint");
    }
    expect_u64("disabled counted breakpoint hits", 0, event.data.breakpoints.entries[0].current_hits);
    expect_u64("disabled counted breakpoint counter", 2, event.data.breakpoints.entries[0].counter);
    expect_true("enable disabled counted breakpoint", runtime_client_set_breakpoint_enabled(client, id, true));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received after enabling counted breakpoint");
    }
    expect_u64("enabled counted breakpoint counter preserved", 2, event.data.breakpoints.entries[0].counter);

    stop_runtime(rt, client);
}

static void test_runtime_non_break_actions_do_not_pause(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;

    write_runtime_roms();
    rt = start_runtime(&client);

    definition.enabled = 1;
    definition.start_address = TEST_RESET_VECTOR;
    definition.end_address = TEST_RESET_VECTOR;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_ROM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_TRON;
    definition.use_counter = 0;
    definition.initial_count = 0;
    definition.reset_count = 0;

    expect_true("create tron-only breakpoint", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);
    expect_true("step over tron-only breakpoint", runtime_client_step_instruction(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE)) {
        fail("STEP_COMPLETE not received for tron-only breakpoint");
    }
    expect_true("request tron-only breakpoint snapshot", runtime_client_request_breakpoints(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) {
        fail("breakpoint snapshot not received for tron-only breakpoint");
    }
    expect_u64("tron-only breakpoint hit count", 1, event.data.breakpoints.entries[0].current_hits);

    stop_runtime(rt, client);
}

static void test_runtime_loads_breakpoints_from_ini(void) {
    FILE *file;
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    write_runtime_roms();
    file = fopen("scheduler_debug.ini", "w");
    if (!file) {
        fail("failed to create scheduler debug ini");
    }
    fprintf(file, "[DEBUG]\n");
    fprintf(file, "break.C000=execute,map,break\n");
    fprintf(file, "break.C000.1=write,ram,break\n");
    fprintf(file, "break.D000-D0FF=read,rom,tron,count=3\n");
    fprintf(file, "break.GGGG=execute,map,break\n");
    fprintf(file, "break.C100=execute,map,break,count=-1\n");
    fclose(file);

    rt = start_runtime_with_ini(&client, "scheduler_debug.ini", false);
    poll_breakpoint_count(client, &event, 3);

    expect_u16("ini breakpoint 0 start", 0xc000, event.data.breakpoints.entries[0].start_address);
    expect_u64("ini breakpoint 0 access", RUNTIME_BREAKPOINT_ACCESS_EXECUTE, event.data.breakpoints.entries[0].access);
    expect_u16("ini duplicate breakpoint start", 0xc000, event.data.breakpoints.entries[1].start_address);
    expect_u64("ini duplicate access", RUNTIME_BREAKPOINT_ACCESS_WRITE, event.data.breakpoints.entries[1].access);
    expect_u64("ini duplicate mapping", RUNTIME_BREAKPOINT_MAPPING_RAM, event.data.breakpoints.entries[1].mapping);
    expect_u16("ini range start", 0xd000, event.data.breakpoints.entries[2].start_address);
    expect_u16("ini range end", 0xd0ff, event.data.breakpoints.entries[2].end_address);
    expect_u8("ini range flag", 1, event.data.breakpoints.entries[2].has_end_address);
    expect_u64("ini reset defaults to count", 3, event.data.breakpoints.entries[2].reset_count);

    stop_runtime(rt, client);
    remove("scheduler_debug.ini");
}

static void test_runtime_saves_breakpoints_to_ini_with_suffixes(void) {
    FILE *file;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition definition;
    char contents[2048];
    size_t length;

    write_runtime_roms();
    file = fopen("scheduler_save_debug.ini", "w");
    if (!file) {
        fail("failed to create scheduler save debug ini");
    }
    fprintf(file, "[Video]\nstandard=NTSC\n\n[DEBUG]\nbreak.1234=execute,map,break\n");
    fclose(file);

    rt = start_runtime_with_ini(&client, "scheduler_save_debug.ini", true);
    poll_breakpoint_count(client, &event, 1);
    expect_true("clear loaded breakpoint before save test", runtime_client_clear_all_breakpoints(client));
    poll_breakpoint_count(client, &event, 0);

    definition.enabled = 1;
    definition.start_address = 0xc000;
    definition.end_address = 0xc000;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 0;
    definition.initial_count = 0;
    definition.reset_count = 0;
    expect_true("create save breakpoint 1", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 1);

    definition.enabled = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_READ | RUNTIME_BREAKPOINT_ACCESS_WRITE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_RAM;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_SWAP | RUNTIME_BREAKPOINT_ACTION_TYPE;
    definition.use_counter = 1;
    definition.initial_count = 10;
    definition.reset_count = 2;
    expect_true("create save breakpoint duplicate", runtime_client_create_breakpoint(client, &definition));
    poll_breakpoint_count(client, &event, 2);

    runtime_client_quit(client);
    runtime_stop(rt);
    expect_true("save runtime debug ini", runtime_save_debug_ini(rt));
    runtime_destroy(rt);
    runtime_shutdown();

    file = fopen("scheduler_save_debug.ini", "r");
    if (!file) {
        fail("failed to read saved scheduler debug ini");
    }
    length = fread(contents, 1, sizeof(contents) - 1u, file);
    contents[length] = '\0';
    fclose(file);

    if (strstr(contents, "standard=NTSC") == NULL ||
        strstr(contents, "break.1234") != NULL ||
        strstr(contents, "break.C000=execute,map,break") == NULL ||
        strstr(contents, "break.C000.1=disabled,read,write,ram,break,swap,type,count=10,reset=2") == NULL) {
        fprintf(stderr, "saved ini contents unexpected:\n%s\n", contents);
        exit(1);
    }

    remove("scheduler_save_debug.ini");
}

static void test_runtime_load_prg_writes_ram(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("dirty RAM before PRG reset", runtime_client_write_memory_byte(
        client,
        0x2003,
        0x99,
        RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("dirty RAM write response not received");
    }

    expect_true("load PRG", runtime_client_load_prg(client, "scheduler_test.prg"));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("PRG load reset event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("PRG load memory response not received");
    }
    expect_u16("PRG load address", 0x2000, event.data.memory.address);
    expect_u8("PRG byte 0", 0x01, event.data.memory.bytes[0]);
    expect_u8("PRG byte 1", 0x02, event.data.memory.bytes[1]);
    expect_u8("PRG byte 2", 0x03, event.data.memory.bytes[2]);

    expect_true("request RAM after PRG load", runtime_client_request_memory(
        client,
        0x2000,
        3,
        RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("RAM response after PRG load not received");
    }
    expect_u8("loaded RAM byte 0", 0x01, event.data.memory.bytes[0]);
    expect_u8("loaded RAM byte 1", 0x02, event.data.memory.bytes[1]);
    expect_u8("loaded RAM byte 2", 0x03, event.data.memory.bytes[2]);

    expect_true("request reset-cleared RAM after PRG load", runtime_client_request_memory(
        client,
        0x2003,
        1,
        RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("reset-cleared RAM response after PRG load not received");
    }
    expect_u8("PRG load reset cleared RAM", 0x00, event.data.memory.bytes[0]);

    stop_runtime(rt, client);
}

static void test_runtime_load_prg_resumes_running(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_runtime(&client);

    expect_true("run before PRG load", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event before PRG load not received");
    }

    expect_true("load PRG while running", runtime_client_load_prg(client, "scheduler_test.prg"));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("running PRG load reset event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("running PRG load memory response not received");
    }
    expect_u16("running PRG load address", 0x2000, event.data.memory.address);
    expect_u8("running PRG byte 0", 0x01, event.data.memory.bytes[0]);
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event after PRG load not received");
    }

    expect_true("request machine state after running PRG load", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine state after running PRG load not received");
    }
    expect_u64("PRG load restored running state", 1, event.data.machine_state.running);

    stop_runtime(rt, client);
}

int main(void) {
    test_runtime_turbo_csv_config();
    write_runtime_roms();
    write_test_prg();
    test_single_machine_cycle();
    test_runtime_run_for_cycles();
    test_runtime_cycle_turbo_speed();
    test_runtime_keyboard_event_reaches_machine();
    test_runtime_restore_event_reaches_machine();
    test_runtime_run_pause();
    test_runtime_reset_resumes_running();
    test_runtime_step_instruction_from_running_pauses();
    test_runtime_cpu_register_setters_are_paused_only();
    test_runtime_memory_snapshots_and_writes_are_paused_only();
    test_runtime_stops_on_enabled_execute_breakpoint();
    test_runtime_ignores_disabled_execute_breakpoint();
    test_runtime_clear_breakpoint_removes_it();
    test_runtime_clear_all_breakpoints_removes_all();
    test_runtime_create_update_duplicate_breakpoint_definitions();
    test_runtime_execute_breakpoint_supports_ranges_and_mapping();
    test_runtime_read_write_watchpoints_use_access_and_mapping();
    test_runtime_breakpoint_counters_gate_triggers();
    test_runtime_breakpoint_counter_zero_and_disabled_rules();
    test_runtime_non_break_actions_do_not_pause();
    test_runtime_loads_breakpoints_from_ini();
    test_runtime_saves_breakpoints_to_ini_with_suffixes();
    test_runtime_load_prg_writes_ram();
    test_runtime_load_prg_resumes_running();
    remove("scheduler_64c.bin");
    remove("scheduler_character.bin");
    remove("scheduler_test.prg");
    return 0;
}
