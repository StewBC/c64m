/* S3: state-changed inform + cursor stale after mutation. */
#include "c64_bus.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_history.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void expect_true(const char *name, int v) {
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_state_changed_64c.bin", "wb");
    FILE *character = fopen("runtime_state_changed_character.bin", "wb");
    size_t i;

    if (system == NULL || character == NULL) {
        fprintf(stderr, "FAIL: create test ROMs\n");
        exit(1);
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

static void drain(runtime_client *client) {
    runtime_event event;
    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            fprintf(stderr, "runtime error: %s\n", event.data.error.message);
            exit(1);
        }
    }
}

static int wait_history_result(
    runtime_client *client,
    uint64_t token,
    runtime_history_rpc_meta *out_meta,
    uint8_t **out_bytes,
    uint32_t *out_len) {
    runtime_event event;

    if (!poll_event(
            client, RUNTIME_EVENT_HISTORY_RESULT_RESPONSE, token, &event)) {
        return 0;
    }
    if (out_meta != NULL) {
        *out_meta = event.data.history_rpc;
    }
    if (event.data.history_rpc.status != RUNTIME_HISTORY_RPC_OK) {
        if (out_bytes != NULL) {
            *out_bytes = NULL;
        }
        if (out_len != NULL) {
            *out_len = 0;
        }
        return 1;
    }
    if (event.data.history_rpc.byte_length == 0u) {
        if (out_bytes != NULL) {
            *out_bytes = NULL;
        }
        if (out_len != NULL) {
            *out_len = 0;
        }
        return 1;
    }
    if (!runtime_client_claim_history_rpc(
            client, token, out_bytes, out_len, out_meta)) {
        fprintf(stderr, "FAIL: claim history rpc\n");
        exit(1);
    }
    return 1;
}

static int wait_session_response(
    runtime_client *client,
    uint64_t token,
    uint32_t *out_session_id) {
    runtime_event event;

    if (!poll_event(client, RUNTIME_EVENT_SESSION_RESPONSE, token, &event)) {
        return 0;
    }
    expect_true("session ok", event.data.session.status == RUNTIME_SESSION_OK);
    if (out_session_id != NULL) {
        *out_session_id = event.data.session.session_id;
    }
    return 1;
}

static int wait_state_changed(
    runtime_client *client,
    runtime_state_changed_reason expect_reason,
    uint32_t expect_source) {
    runtime_event event;

    if (!poll_event(client, RUNTIME_EVENT_STATE_CHANGED, 0u, &event)) {
        return 0;
    }
    expect_true("reason", event.data.state_changed.reason == expect_reason);
    expect_true(
        "source",
        event.data.state_changed.source_session_id == expect_source);
    return 1;
}

int main(void) {
    runtime_config config = {
        .system_rom_path = "runtime_state_changed_64c.bin",
        .char_rom_path = "runtime_state_changed_character.bin",
        .history_memory_mb = 16,
        .history_memory_mb_configured = true,
        .frame_ring_memory_mb = 0,
        .frame_ring_memory_mb_configured = true,
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_history_query query;
    runtime_history_rpc_meta meta;
    uint8_t *bytes = NULL;
    uint32_t length = 0;
    uint64_t token;
    uint32_t session_a = 0;
    uint32_t session_b = 0;
    uint64_t cursor = 0;
    int i;

    write_test_roms();
    expect_true("runtime_init", runtime_init());

    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("started", poll_event(client, RUNTIME_EVENT_STARTED, 0u, &event));
    expect_true(
        "reset", poll_event(client, RUNTIME_EVENT_RESET_COMPLETE, 0u, &event));

    for (i = 0; i < 8; ++i) {
        expect_true("step seed", runtime_client_step_instruction(client));
        expect_true(
            "step seed done",
            poll_event(client, RUNTIME_EVENT_STEP_COMPLETE, 0u, &event));
        /* Drain state-changed from seed steps (source session 0). */
        drain(client);
    }

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "open a",
        runtime_client_session_open(
            client, RUNTIME_SESSION_KIND_UI, 0u, token));
    expect_true("open a resp", wait_session_response(client, token, &session_a));

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "open b",
        runtime_client_session_open(
            client, RUNTIME_SESSION_KIND_UI, 0u, token));
    expect_true("open b resp", wait_session_response(client, token, &session_b));
    expect_true("distinct", session_a != session_b);

    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_BACKWARD;
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "find b",
        runtime_client_history_find(
            client,
            session_b,
            &query,
            RUNTIME_HISTORY_FROM_NEWEST,
            0,
            2,
            token));
    expect_true(
        "find b ok", wait_history_result(client, token, &meta, &bytes, &length));
    expect_true("find more", meta.more == 1u && meta.cursor != 0u);
    cursor = meta.cursor;
    free(bytes);
    bytes = NULL;

    runtime_client_set_command_session(client, session_a);
    drain(client);
    expect_true("step", runtime_client_step_instruction(client));
    expect_true(
        "state-changed",
        wait_state_changed(client, RUNTIME_STATE_CHANGED_STEP, session_a));

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "next stale",
        runtime_client_history_next(client, session_b, cursor, 2, token));
    expect_true(
        "next stale resp",
        wait_history_result(client, token, &meta, &bytes, &length));
    expect_true("stale", meta.status == RUNTIME_HISTORY_RPC_CURSOR_STALE);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "refind",
        runtime_client_history_find(
            client,
            session_b,
            &query,
            RUNTIME_HISTORY_FROM_NEWEST,
            0,
            2,
            token));
    expect_true(
        "refind ok", wait_history_result(client, token, &meta, &bytes, &length));
    expect_true("refind status", meta.status == RUNTIME_HISTORY_RPC_OK);
    free(bytes);

    runtime_client_set_command_session(client, 0u);
    token = runtime_client_alloc_request_token(client);
    (void)runtime_client_session_close(client, session_a, token);
    token = runtime_client_alloc_request_token(client);
    (void)runtime_client_session_close(client, session_b, token);

    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
    printf("ok\n");
    return 0;
}
