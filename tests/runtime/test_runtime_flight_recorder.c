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
    FILE *system = fopen("runtime_hist_64c.bin", "wb");
    FILE *character = fopen("runtime_hist_character.bin", "wb");
    size_t i;

    if (system == NULL || character == NULL) {
        fail("failed to create runtime test ROMs");
    }
    for (i = 0u; i < C64_BASIC_ROM_SIZE + C64_KERNAL_ROM_SIZE; ++i) {
        fputc(0xeau, system);
    }
    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffcu), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);
    for (i = 0u; i < C64_CHAR_ROM_SIZE; ++i) {
        fputc(0x00, character);
    }
    fclose(system);
    fclose(character);
}

static bool poll_event(
    runtime_client *client,
    runtime_event_type type,
    uint64_t token,
    runtime_event *out_event) {
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 3.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == type &&
                (token == 0u || event.request_token == token)) {
                if (out_event != NULL) {
                    *out_event = event;
                }
                return true;
            }
        }
    }
    return false;
}

static runtime_history_status request_status(runtime_client *client) {
    runtime_event event;
    uint64_t token = runtime_client_alloc_request_token(client);

    if (token == 0u || !runtime_client_history_info(client, token) ||
        !poll_event(
            client, RUNTIME_EVENT_HISTORY_STATUS_RESPONSE, token, &event)) {
        fail("history status timeout");
    }
    return event.data.history_status;
}

static runtime_history_status set_recording(
    runtime_client *client,
    bool enabled) {
    runtime_event event;
    uint64_t token = runtime_client_alloc_request_token(client);

    if (token == 0u ||
        !runtime_client_history_record(client, enabled, token) ||
        !poll_event(
            client, RUNTIME_EVENT_HISTORY_STATUS_RESPONSE, token, &event)) {
        fail("history record control timeout");
    }
    return event.data.history_status;
}

static runtime_history_status clear_history(runtime_client *client) {
    runtime_event event;
    uint64_t token = runtime_client_alloc_request_token(client);

    if (token == 0u ||
        !runtime_client_history_clear(client, token) ||
        !poll_event(
            client, RUNTIME_EVENT_HISTORY_STATUS_RESPONSE, token, &event)) {
        fail("history clear timeout");
    }
    return event.data.history_status;
}

static runtime *start_runtime(
    runtime_config *config,
    runtime_client **out_client) {
    runtime *rt = runtime_create(config);
    runtime_event event;

    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime start failed");
    }
    *out_client = runtime_get_client(rt);
    if (!poll_event(*out_client, RUNTIME_EVENT_STARTED, 0u, &event) ||
        !poll_event(*out_client, RUNTIME_EVENT_RESET_COMPLETE, 0u, &event)) {
        fail("runtime startup timeout");
    }
    return rt;
}

int main(void) {
    runtime_config config = {
        .system_rom_path = "runtime_hist_64c.bin",
        .char_rom_path = "runtime_hist_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_history_status status;
    uint64_t frozen_records;
    uint64_t epoch;
    uint32_t timeline;
    int i;

    write_test_roms();
    if (!runtime_init()) {
        fail("runtime_init failed");
    }

    rt = start_runtime(&config, &client);
    status = request_status(client);
    if (!status.available || !status.recording ||
        status.requested_bytes != 256u * 1024u * 1024u ||
        status.epoch != 1u || status.timeline != 1u ||
        status.record_count < 2u) {
        fail("default recorder startup status is incorrect");
    }

    for (i = 0; i < 5; ++i) {
        if (!runtime_client_step_instruction(client) ||
            !poll_event(client, RUNTIME_EVENT_STEP_COMPLETE, 0u, &event)) {
            fail("step instruction timeout");
        }
    }
    status = request_status(client);
    if (status.record_count < 7u || status.partial_records != 0u) {
        fail("stepped instructions were not recorded");
    }

    {
        runtime_history_query query;
        runtime_history_rpc_meta meta;
        uint64_t token = runtime_client_alloc_request_token(client);
        uint64_t busy_token;
        uint64_t stale_token;
        uint64_t cursor;
        uint8_t *payload = NULL;
        uint32_t payload_length = 0u;

        memset(&query, 0, sizeof(query));
        if (!runtime_client_history_find(
                client, &query, RUNTIME_HISTORY_FROM_DEFAULT,
                0u, 2u, token) ||
            !poll_event(
                client, RUNTIME_EVENT_HISTORY_RESULT_RESPONSE,
                token, &event) ||
            event.data.history_rpc.status != RUNTIME_HISTORY_RPC_OK ||
            event.data.history_rpc.count != 2u ||
            event.data.history_rpc.cursor == 0u) {
            fail("history-find completion is incorrect");
        }
        cursor = event.data.history_rpc.cursor;
        if (runtime_client_claim_memory_rpc(
                client, token, &payload, NULL, NULL, NULL) ||
            runtime_client_claim_history_rpc(
                client, token + 1000u, &payload, NULL, NULL)) {
            fail("payload kind/token isolation failed");
        }
        busy_token = runtime_client_alloc_request_token(client);
        if (!runtime_client_history_read(
                client, 0u, status.newest_id, 0u, 0u, busy_token) ||
            !poll_event(
                client, RUNTIME_EVENT_HISTORY_RESULT_RESPONSE,
                busy_token, &event) ||
            event.data.history_rpc.status !=
                RUNTIME_HISTORY_RPC_REQUEST_ACTIVE) {
            fail("second history payload request was not busy");
        }
        if (!runtime_client_claim_history_rpc(
                client, token, &payload, &payload_length, &meta) ||
            payload_length < 24u || memcmp(payload, "HST1", 4u) != 0 ||
            meta.cursor != cursor || meta.count != 2u) {
            fail("history payload claim is incorrect");
        }
        free(payload);
        payload = NULL;
        if (runtime_client_claim_history_rpc(
                client, token, &payload, NULL, NULL)) {
            free(payload);
            fail("history payload could be claimed twice");
        }

        if (!runtime_client_step_instruction(client) ||
            !poll_event(client, RUNTIME_EVENT_STEP_COMPLETE, 0u, &event)) {
            fail("step for cursor invalidation timeout");
        }
        stale_token = runtime_client_alloc_request_token(client);
        if (!runtime_client_history_next(
                client, cursor, 2u, stale_token) ||
            !poll_event(
                client, RUNTIME_EVENT_HISTORY_RESULT_RESPONSE,
                stale_token, &event) ||
            event.data.history_rpc.status !=
                RUNTIME_HISTORY_RPC_CURSOR_STALE) {
            fail("mutated history cursor was not stale");
        }

        status = request_status(client);
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_history_read(
                client, status.epoch, status.newest_id,
                1u, 0u, token) ||
            !poll_event(
                client, RUNTIME_EVENT_HISTORY_RESULT_RESPONSE,
                token, &event) ||
            event.data.history_rpc.status != RUNTIME_HISTORY_RPC_OK ||
            event.data.history_rpc.cursor != 0u ||
            event.data.history_rpc.count != 2u ||
            !runtime_client_claim_history_rpc(
                client, token, &payload, &payload_length, &meta)) {
            fail("history-read completion is incorrect");
        }
        free(payload);
    }

    status = set_recording(client, false);
    if (status.recording) {
        fail("history-record off did not stop");
    }
    frozen_records = status.record_count;
    for (i = 0; i < 3; ++i) {
        if (!runtime_client_step_instruction(client) ||
            !poll_event(client, RUNTIME_EVENT_STEP_COMPLETE, 0u, &event)) {
            fail("step while history stopped timeout");
        }
    }
    status = request_status(client);
    if (status.record_count != frozen_records) {
        fail("history changed while stopped");
    }

    status = set_recording(client, true);
    if (!status.recording || status.record_count != frozen_records + 1u) {
        fail("history-record on did not append one resume marker");
    }

    epoch = status.epoch;
    status = clear_history(client);
    if (status.epoch != epoch + 1u || status.record_count != 1u ||
        !status.recording) {
        fail("history-clear lifecycle is incorrect");
    }

    timeline = status.timeline;
    if (!runtime_client_reset(client) ||
        !poll_event(client, RUNTIME_EVENT_RESET_COMPLETE, 0u, &event)) {
        fail("explicit reset timeout");
    }
    status = request_status(client);
    if (status.timeline != timeline + 1u || status.record_count < 2u) {
        fail("reset did not retain history and advance timeline");
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);

    config.history_memory_mb = 0u;
    config.history_memory_mb_configured = true;
    rt = start_runtime(&config, &client);
    status = request_status(client);
    if (status.available || status.recording ||
        status.unavailable_reason !=
            RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG) {
        fail("history_memory_mb=0 status is incorrect");
    }
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);

    runtime_shutdown();
    remove("runtime_hist_64c.bin");
    remove("runtime_hist_character.bin");
    printf("test_runtime_flight_recorder: ok\n");
    return 0;
}
