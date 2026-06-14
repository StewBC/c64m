#include "runtime.h"
#include "runtime_client.h"

#include "c64_bus.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_64c.bin", "wb");
    FILE *character = fopen("runtime_character.bin", "wb");
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

int main(void) {
    runtime_config config = {
        .system_rom_path = "runtime_64c.bin",
        .char_rom_path = "runtime_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint16_t reset_pc;
    uint16_t stepped_pc;
    uint64_t reset_cycles;
    uint64_t stepped_cycles;

    write_test_roms();

    if (!runtime_init()) {
        fail("runtime_init failed");
    }

    rt = runtime_create(&config);
    if (!rt) {
        fail("runtime_create failed");
    }

    if (!runtime_start(rt)) {
        fail("runtime_start failed");
    }

    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }

    if (!runtime_client_reset(client)) {
        fail("reset command failed");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("RESET_COMPLETE event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state after reset not received");
    }

    reset_pc = event.data.cpu_state.pc;
    reset_cycles = event.data.cpu_state.cycles;
    if (reset_pc != 0xe000) {
        fprintf(stderr, "expected reset PC e000, got %04x\n", reset_pc);
        exit(1);
    }

    if (!runtime_client_step_instruction(client)) {
        fail("step command failed");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE)) {
        fail("STEP_COMPLETE event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU state after step not received");
    }

    stepped_pc = event.data.cpu_state.pc;
    stepped_cycles = event.data.cpu_state.cycles;
    if (stepped_pc == reset_pc) {
        fail("PC did not advance after step");
    }
    if (stepped_cycles <= reset_cycles) {
        fail("cycles did not advance after step");
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();

    remove("runtime_64c.bin");
    remove("runtime_character.bin");
    return 0;
}
