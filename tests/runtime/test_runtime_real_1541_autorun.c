#include "runtime.h"
#include "runtime_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum {
    BASIC_LOAD_ADDRESS = 0x0801,
    AUTORUN_TIMEOUT_SECONDS = 30
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

static int poll_event(runtime_client *client, runtime_event *event, runtime_event_type type) {
    time_t start = time(NULL);

    while (time(NULL) - start < 5) {
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

static runtime *start_real_1541_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = C64M_SOURCE_DIR "/roms/system.rom",
        .char_rom_path = C64M_SOURCE_DIR "/roms/character.rom",
        .rom1541_path = C64M_SOURCE_DIR "/roms/1541.rom",
        .autorun = true,
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    config.machine_config.emulate_1541 = 1;
    config.turbo_speeds[0] = 3; /* warp: free-run skip-ahead for load */
    config.turbo_speed_count = 1;
    config.active_turbo_multiplier = 3;

    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (rt == NULL) {
        fail("runtime_create failed");
    }

    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("RESET_COMPLETE event not received");
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

static bool poll_basic_memory_loaded(runtime_client *client) {
    time_t start = time(NULL);
    runtime_event event;
    bool request_pending = false;

    while (time(NULL) - start < AUTORUN_TIMEOUT_SECONDS) {
        if (!request_pending) {
            if (!runtime_client_request_memory(client, BASIC_LOAD_ADDRESS, 8, RUNTIME_MEMORY_MODE_RAM)) {
                continue;
            }
            request_pending = true;
        }

        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_MEMORY_RESPONSE &&
                event.data.memory.address == BASIC_LOAD_ADDRESS &&
                event.data.memory.length >= 2 &&
                event.data.memory.bytes[0] != 0) {
                return true;
            }
            if (event.type == RUNTIME_EVENT_MEMORY_RESPONSE &&
                event.data.memory.address == BASIC_LOAD_ADDRESS) {
                request_pending = false;
            }
        }
    }

    return false;
}

static void test_real_1541_autorun_loads_first_program(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    const char *disk_path = C64M_SOURCE_DIR "/assets/disks/GALENCIA.D64";

    rt = start_real_1541_runtime(&client);

    expect_true("mount GALENCIA", runtime_client_mount_d64(client, 8, disk_path));
    if (!poll_event(client, &event, RUNTIME_EVENT_DISK_STATUS_RESPONSE)) {
        fail("disk status event not received");
    }
    if (event.data.disk_status.last_result != C64_DRIVE_STATUS_OK ||
        !event.data.disk_status.mounted) {
        fprintf(stderr,
            "disk mount failed: mounted=%u status=%d\n",
            event.data.disk_status.mounted,
            event.data.disk_status.last_result);
        exit(1);
    }

    expect_true("run runtime", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received");
    }

    if (!poll_basic_memory_loaded(client)) {
        fail("real 1541 autorun did not populate BASIC memory");
    }

    stop_runtime(rt, client);
}

int main(void) {
    test_real_1541_autorun_loads_first_program();
    return 0;
}
