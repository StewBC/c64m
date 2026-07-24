/*
 * Phase 8: optional CPU history records instruction-start states when enabled.
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
    FILE *system = fopen("runtime_hist_64c.bin", "wb");
    FILE *character = fopen("runtime_hist_character.bin", "wb");
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

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 3.0) {
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
        .system_rom_path = "runtime_hist_64c.bin",
        .char_rom_path = "runtime_hist_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    int i;

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
    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.2) {
            if (!runtime_client_poll_event(client, &event)) {
                break;
            }
        }
    }

    /* Disabled by default: empty history. */
    if (!runtime_client_request_cpu_history(client, 16u)) {
        fail("request history rejected");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_CPU_HISTORY_RESPONSE)) {
        fail("history response timeout");
    }
    if (event.data.cpu_history.enabled != 0 || event.data.cpu_history.count != 0) {
        fail("history should be empty and disabled by default");
    }

    if (!runtime_client_set_cpu_history(client, true)) {
        fail("enable history rejected");
    }
    /* Allow command to process. */
    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.1) {
            (void)runtime_client_poll_event(client, &event);
        }
    }

    for (i = 0; i < 5; ++i) {
        if (!runtime_client_step_instruction(client)) {
            fail("step rejected");
        }
        if (!poll_event_type(client, &event, RUNTIME_EVENT_STEP_COMPLETE)) {
            fail("step complete timeout");
        }
    }

    if (!runtime_client_request_cpu_history(client, 16u)) {
        fail("request history after steps rejected");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_CPU_HISTORY_RESPONSE)) {
        fail("history after steps timeout");
    }
    if (event.data.cpu_history.enabled == 0) {
        fail("history should be enabled");
    }
    if (event.data.cpu_history.count < 5u) {
        fprintf(
            stderr,
            "FAIL: expected at least 5 history entries, got %u\n",
            (unsigned)event.data.cpu_history.count);
        exit(1);
    }
    /* NOP sled at $E000: first entries should be EA. */
    if (event.data.cpu_history.entries[0].opcode != 0xeau) {
        fprintf(
            stderr,
            "FAIL: expected opcode EA, got %02X\n",
            event.data.cpu_history.entries[0].opcode);
        exit(1);
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
    remove("runtime_hist_64c.bin");
    remove("runtime_hist_character.bin");
    printf("test_runtime_cpu_history: ok\n");
    return 0;
}
