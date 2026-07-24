/*
 * Phase 7: run-to-raster stops with RUN_COMPLETE on the target VIC line.
 */

#include "c64_bus.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_r2r_64c.bin", "wb");
    FILE *character = fopen("runtime_r2r_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create runtime test ROMs");
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

static int poll_event_type(
    runtime_client *client,
    runtime_event *event,
    runtime_event_type type)
{
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 5.0) {
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

int main(void) {
    runtime_config config = {
        .system_rom_path = "runtime_r2r_64c.bin",
        .char_rom_path = "runtime_r2r_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t line;

    write_test_roms();
    if (!runtime_init()) {
        fail("runtime_init failed");
    }
    rt = runtime_create(&config);
    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime start failed");
    }
    client = runtime_get_client(rt);

    if (!poll_event_type(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED timeout");
    }
    if (!runtime_client_reset(client)) {
        fail("reset rejected");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("RESET_COMPLETE timeout");
    }
    /* Drain residual. */
    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.2) {
            if (!runtime_client_poll_event(client, &event)) {
                break;
            }
        }
    }

    if (!runtime_client_run_to_raster(client, 100u, false, 0u)) {
        fail("run_to_raster rejected");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_RUN_COMPLETE)) {
        fail("RUN_COMPLETE timeout");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("machine state after run-to-raster timeout");
    }
    line = event.data.machine_state.vicii_hardware.raster_line;
    if (line != 100u) {
        fprintf(stderr, "FAIL: expected raster 100, got %u\n", line);
        exit(1);
    }
    if (event.data.machine_state.running != 0) {
        fail("expected paused after run-to-raster");
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
    remove("runtime_r2r_64c.bin");
    remove("runtime_r2r_character.bin");
    printf("test_runtime_run_to_raster: ok\n");
    return 0;
}
