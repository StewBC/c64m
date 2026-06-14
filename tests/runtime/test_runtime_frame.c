#include "runtime.h"
#include "runtime_client.h"

#include "c64_bus.h"

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

static void expect_u32(const char *name, uint32_t expected, uint32_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %llu, got %llu\n", name, (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

static void write_runtime_roms(void) {
    FILE *system = fopen("frame_64c.bin", "wb");
    FILE *character = fopen("frame_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create frame test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }

    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc((int)(TEST_RESET_VECTOR & 0xff), system);
    fputc((int)(TEST_RESET_VECTOR >> 8), system);

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

static runtime *start_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = "frame_64c.bin",
        .char_rom_path = "frame_character.bin",
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

    expect_true("runtime reset", runtime_client_reset(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("RESET_COMPLETE event not received");
    }

    *out_client = client;
    return rt;
}

static void stop_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
}

static void test_request_frame_while_paused(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;

    rt = start_runtime(&client);

    expect_true("request frame", runtime_client_request_frame(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("FRAME_READY not received");
    }
    expect_true("poll copied frame", runtime_client_poll_frame(client, &frame));

    expect_u32("frame width", C64_FRAME_WIDTH, frame.width);
    expect_u32("frame height", C64_FRAME_HEIGHT, frame.height);
    expect_u32("frame pixel format", C64_FRAME_PIXEL_FORMAT_ARGB8888, frame.pixel_format);
    expect_u64("paused frame cycle", 0, frame.machine_cycle);

    expect_true("request machine state", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("MACHINE_STATE not received");
    }
    expect_u64("request frame does not advance cycles", 0, event.data.machine_state.cycle);
    expect_u64("runtime remains paused", 0, event.data.machine_state.running);

    stop_runtime(rt, client);
}

static void test_frame_while_running(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;

    rt = start_runtime(&client);

    expect_true("run command", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("running FRAME_READY not received");
    }
    expect_true("poll running frame", runtime_client_poll_frame(client, &frame));
    if (frame.machine_cycle == 0) {
        fail("running frame did not carry an advanced machine cycle");
    }

    expect_true("pause command", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received");
    }

    stop_runtime(rt, client);
}

int main(void) {
    write_runtime_roms();
    test_request_frame_while_paused();
    test_frame_while_running();
    remove("frame_64c.bin");
    remove("frame_character.bin");
    return 0;
}
