#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static int poll_event_timeout(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static int poll_event_or_error(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                return -1;
            }
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static void write_savestate_roms(uint8_t variant) {
    FILE *system = fopen("savestate_64c.bin", "wb");
    FILE *character = fopen("savestate_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create save-state test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc((int)(0xea ^ variant), system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc((int)(0xea ^ (uint8_t)(variant << 1)), system);
    }

    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);
    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x0000), SEEK_SET);
    fputc(0xea, system);

    for (i = 0; i < C64_CHAR_ROM_SIZE; i++) {
        fputc((int)(variant + (uint8_t)i), character);
    }

    fclose(system);
    fclose(character);
}

static runtime *start_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = "savestate_64c.bin",
        .char_rom_path = "savestate_character.bin",
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
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("startup RESET_COMPLETE not received");
    }

    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.1) {
            while (runtime_client_poll_event(client, &event)) { }
        }
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

static void write_paused_ram(runtime_client *client, uint16_t address, uint8_t value) {
    runtime_event event;

    expect_true(
        "write paused ram",
        runtime_client_write_memory_byte(client, address, value, RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("memory write response not received");
    }
}

static uint8_t read_paused_ram(runtime_client *client, uint16_t address) {
    runtime_event event;

    expect_true(
        "request paused ram",
        runtime_client_request_memory(client, address, 1, RUNTIME_MEMORY_MODE_RAM));
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("memory read response not received");
    }
    return event.data.memory.bytes[0];
}

static void test_runtime_save_load_roundtrip(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    int result;

    write_savestate_roms(0);
    rt = start_runtime(&client);

    write_paused_ram(client, 0xc000, 0x42);
    expect_true("save state", runtime_client_save_state(client, "runtime_roundtrip.c64state"));
    result = poll_event_or_error(client, &event, RUNTIME_EVENT_SAVE_STATE_COMPLETE);
    if (result <= 0) {
        fail("save-state completion not received");
    }

    write_paused_ram(client, 0xc000, 0x99);
    expect_true("load state", runtime_client_load_state(client, "runtime_roundtrip.c64state"));
    result = poll_event_or_error(client, &event, RUNTIME_EVENT_LOAD_STATE_COMPLETE);
    if (result <= 0) {
        fail("load-state completion not received");
    }

    expect_u8("roundtrip restored byte", 0x42, read_paused_ram(client, 0xc000));

    stop_runtime(rt, client);
    remove("runtime_roundtrip.c64state");
}

static void test_runtime_save_finishes_pending_instruction(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    int result;

    write_savestate_roms(0);
    rt = start_runtime(&client);

    expect_true("run one cycle", runtime_client_run_cycles(client, 1));
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_RUN_COMPLETE)) {
        fail("RUN_COMPLETE after one cycle not received");
    }
    expect_true("save mid-instruction state",
                runtime_client_save_state(client, "runtime_mid_instruction.c64state"));
    result = poll_event_or_error(client, &event, RUNTIME_EVENT_SAVE_STATE_COMPLETE);
    if (result <= 0) {
        fail("mid-instruction save-state completion not received");
    }

    stop_runtime(rt, client);
    remove("runtime_mid_instruction.c64state");
}

static void test_runtime_load_rejects_bad_snapshot_and_preserves_machine(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *bad;
    int result;

    write_savestate_roms(0);
    bad = fopen("runtime_bad.c64state", "wb");
    if (!bad) {
        fail("failed to create bad snapshot");
    }
    fputs("not a snapshot", bad);
    fclose(bad);

    rt = start_runtime(&client);
    write_paused_ram(client, 0xc001, 0x55);
    expect_true("load bad state", runtime_client_load_state(client, "runtime_bad.c64state"));
    result = poll_event_or_error(client, &event, RUNTIME_EVENT_LOAD_STATE_COMPLETE);
    if (result >= 0) {
        fail("bad snapshot should have produced an error");
    }
    expect_u8("bad load preserved byte", 0x55, read_paused_ram(client, 0xc001));

    stop_runtime(rt, client);
    remove("runtime_bad.c64state");
}

static void test_runtime_load_rejects_rom_mismatch(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    int result;

    write_savestate_roms(0);
    rt = start_runtime(&client);
    write_paused_ram(client, 0xc002, 0x77);
    expect_true("save mismatch state", runtime_client_save_state(client, "runtime_mismatch.c64state"));
    result = poll_event_or_error(client, &event, RUNTIME_EVENT_SAVE_STATE_COMPLETE);
    if (result <= 0) {
        fail("mismatch save-state completion not received");
    }
    stop_runtime(rt, client);

    write_savestate_roms(1);
    rt = start_runtime(&client);
    write_paused_ram(client, 0xc002, 0xaa);
    expect_true("load mismatch state", runtime_client_load_state(client, "runtime_mismatch.c64state"));
    result = poll_event_or_error(client, &event, RUNTIME_EVENT_LOAD_STATE_COMPLETE);
    if (result >= 0) {
        fail("ROM mismatch should have produced an error");
    }
    expect_u8("ROM mismatch preserved byte", 0xaa, read_paused_ram(client, 0xc002));
    stop_runtime(rt, client);

    remove("runtime_mismatch.c64state");
}

int main(void) {
    test_runtime_save_load_roundtrip();
    test_runtime_save_finishes_pending_instruction();
    test_runtime_load_rejects_bad_snapshot_and_preserves_machine();
    test_runtime_load_rejects_rom_mismatch();
    remove("savestate_64c.bin");
    remove("savestate_character.bin");
    return 0;
}
