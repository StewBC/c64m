#include "runtime.h"
#include "runtime_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum {
    TEST_RESET_VECTOR = 0xe000
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

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_string(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n", name, expected, actual);
        exit(1);
    }
}

static void write_runtime_roms(void) {
    FILE *system = fopen("runtime_disk_64c.bin", "wb");
    FILE *character = fopen("runtime_disk_character.bin", "wb");
    size_t i;

    if (system == NULL || character == NULL) {
        fail("failed to create runtime disk test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }

    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc((uint8_t)(TEST_RESET_VECTOR & 0xff), system);
    fputc((uint8_t)(TEST_RESET_VECTOR >> 8), system);

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
        .system_rom_path = "runtime_disk_64c.bin",
        .char_rom_path = "runtime_disk_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

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

static void expect_disk_status(
    runtime_client *client,
    uint8_t device,
    uint8_t mounted,
    c64_drive_status_result result,
    const char *display_name,
    const char *disk_title) {
    runtime_event event;

    if (!poll_event(client, &event, RUNTIME_EVENT_DISK_STATUS_RESPONSE)) {
        fail("disk status event not received");
    }
    expect_u8("disk status device", device, event.data.disk_status.device);
    expect_u8("disk status mounted", mounted, event.data.disk_status.mounted);
    if (event.data.disk_status.last_result != result) {
        fprintf(stderr,
            "disk status result: expected %d, got %d\n",
            result,
            event.data.disk_status.last_result);
        exit(1);
    }
    if (display_name != NULL) {
        expect_string("disk display name", display_name, event.data.disk_status.display_name);
    }
    if (disk_title != NULL) {
        expect_string("disk title", disk_title, event.data.disk_status.disk_title);
    }
}

static void test_mount_replace_unmount_and_failure(void) {
    runtime *rt;
    runtime_client *client;
    char blank_path[512];
    char odell_path[512];
    char missing_path[512];

    snprintf(blank_path, sizeof(blank_path), "%s/assets/disks/blank.d64", C64M_SOURCE_DIR);
    snprintf(odell_path, sizeof(odell_path), "%s/assets/disks/ODELLLAK.D64", C64M_SOURCE_DIR);
    snprintf(missing_path, sizeof(missing_path), "%s/assets/disks/does-not-exist.d64", C64M_SOURCE_DIR);

    rt = start_runtime(&client);

    expect_true("request initial disk status", runtime_client_request_disk_status(client, 8));
    expect_disk_status(client, 8, 0, C64_DRIVE_STATUS_NOT_MOUNTED, "", "");

    expect_true("mount blank d64", runtime_client_mount_d64(client, 8, blank_path));
    expect_disk_status(client, 8, 1, C64_DRIVE_STATUS_OK, "blank.d64", "");

    expect_true("mount odell d64", runtime_client_mount_d64(client, 8, odell_path));
    expect_disk_status(client, 8, 1, C64_DRIVE_STATUS_OK, "ODELLLAK.D64", "ASS PRESENTS:");

    expect_true("mount odell d64 device 9", runtime_client_mount_d64(client, 9, odell_path));
    expect_disk_status(client, 9, 1, C64_DRIVE_STATUS_OK, "ODELLLAK.D64", "ASS PRESENTS:");

    expect_true("mount missing d64", runtime_client_mount_d64(client, 8, missing_path));
    expect_disk_status(client, 8, 1, C64_DRIVE_STATUS_IO_ERROR, "ODELLLAK.D64", "ASS PRESENTS:");

    expect_true("unmount d64 device 9", runtime_client_unmount_disk(client, 9));
    expect_disk_status(client, 9, 0, C64_DRIVE_STATUS_NOT_MOUNTED, "", "");

    expect_true("unmount d64", runtime_client_unmount_disk(client, 8));
    expect_disk_status(client, 8, 0, C64_DRIVE_STATUS_NOT_MOUNTED, "", "");

    stop_runtime(rt, client);
}

int main(void) {
    write_runtime_roms();
    test_mount_replace_unmount_and_failure();
    return 0;
}
