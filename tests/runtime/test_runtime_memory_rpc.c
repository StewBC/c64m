/*
 * Phase 1: bulk get-memory via token-keyed RPC pool.
 * Full 64K dump in one solicited request; payload not in event union.
 */

#include "c64_bus.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_memrpc_64c.bin", "wb");
    FILE *character = fopen("runtime_memrpc_character.bin", "wb");
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
        .system_rom_path = "runtime_memrpc_64c.bin",
        .char_rom_path = "runtime_memrpc_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint64_t token;
    uint8_t *payload = NULL;
    uint32_t length = 0;
    uint16_t address = 0;
    runtime_memory_mode mode = RUNTIME_MEMORY_MODE_CPU_MAP;

    write_test_roms();

    if (!runtime_init()) {
        fail("runtime_init failed");
    }
    rt = runtime_create(&config);
    if (rt == NULL) {
        fail("runtime_create failed");
    }
    if (!runtime_start(rt)) {
        fail("runtime_start failed");
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

    /* Reject wrap at client. */
    if (runtime_client_request_memory_token(
            client, 0xFFFF, 2u, RUNTIME_MEMORY_MODE_RAM, 1u)) {
        fail("FFFF+2 should be rejected");
    }
    if (runtime_client_request_memory_token(
            client, 0, 65537u, RUNTIME_MEMORY_MODE_RAM, 1u)) {
        fail("length 65537 should be rejected");
    }

    token = runtime_client_alloc_request_token(client);
    if (token == 0u) {
        fail("token alloc failed");
    }
    if (!runtime_client_request_memory_token(
            client, 0, 65536u, RUNTIME_MEMORY_MODE_RAM, token)) {
        fail("full dump request rejected");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_MEMORY_RPC_COMPLETE)) {
        fail("MEMORY_RPC_COMPLETE timeout");
    }
    if (event.request_token != token) {
        fail("token mismatch on memory rpc complete");
    }
    if (event.data.memory_rpc.status != RUNTIME_MEMORY_RPC_OK) {
        fprintf(stderr, "status=%u\n", (unsigned)event.data.memory_rpc.status);
        fail("memory rpc status not OK");
    }
    if (event.data.memory_rpc.length != 65536u) {
        fail("meta length not 65536");
    }
    if (!runtime_client_claim_memory_rpc(
            client, token, &payload, &length, &address, &mode)) {
        fail("claim memory rpc failed");
    }
    if (payload == NULL || length != 65536u || address != 0u) {
        fail("claim payload meta wrong");
    }
    if (mode != RUNTIME_MEMORY_MODE_RAM) {
        fail("claim mode wrong");
    }
    /* Second claim of same token must fail. */
    {
        uint8_t *again = NULL;
        if (runtime_client_claim_memory_rpc(
                client, token, &again, NULL, NULL, NULL)) {
            free(again);
            fail("second claim should fail");
        }
    }

    free(payload);

    /* Small range still works. */
    token = runtime_client_alloc_request_token(client);
    if (!runtime_client_request_memory_token(
            client, 0x0400, 16u, RUNTIME_MEMORY_MODE_RAM, token)) {
        fail("small request rejected");
    }
    if (!poll_event_type(client, &event, RUNTIME_EVENT_MEMORY_RPC_COMPLETE)) {
        fail("small MEMORY_RPC_COMPLETE timeout");
    }
    if (!runtime_client_claim_memory_rpc(
            client, token, &payload, &length, &address, &mode)) {
        fail("small claim failed");
    }
    if (length != 16u || address != 0x0400u) {
        fail("small claim meta wrong");
    }
    free(payload);

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
    remove("runtime_memrpc_64c.bin");
    remove("runtime_memrpc_character.bin");
    printf("test_runtime_memory_rpc: ok\n");
    return 0;
}
