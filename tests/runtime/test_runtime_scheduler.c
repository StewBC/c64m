#include "c64.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

int main(void) {
    write_runtime_roms();
    test_single_machine_cycle();
    test_runtime_run_for_cycles();
    test_runtime_keyboard_event_reaches_machine();
    test_runtime_restore_event_reaches_machine();
    test_runtime_run_pause();
    test_runtime_step_instruction_from_running_pauses();
    test_runtime_cpu_register_setters_are_paused_only();
    test_runtime_memory_snapshots_and_writes_are_paused_only();
    remove("scheduler_64c.bin");
    remove("scheduler_character.bin");
    return 0;
}
