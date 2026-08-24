/* I0: Inspector opt-in enable and film-arming. No checkpoint engine. */
#include "c64_bus.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_inspector.h"

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
    FILE *system = fopen("runtime_insp_64c.bin", "wb");
    FILE *character = fopen("runtime_insp_character.bin", "wb");
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

static void drain_commands(runtime_client *client) {
    runtime_event event;

    if (!runtime_client_ping(client) ||
        !poll_event(client, RUNTIME_EVENT_PONG, 0u, &event)) {
        fail("ping timeout after inspector command");
    }
}

static runtime_history_status request_history_status(runtime_client *client) {
    runtime_event event;
    uint64_t token = runtime_client_alloc_request_token(client);

    if (token == 0u || !runtime_client_history_info(client, token) ||
        !poll_event(
            client, RUNTIME_EVENT_HISTORY_STATUS_RESPONSE, token, &event)) {
        fail("history status timeout");
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

static void stop_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
}

static void fill_base_config(runtime_config *config) {
    memset(config, 0, sizeof(*config));
    config->system_rom_path = "runtime_insp_64c.bin";
    config->char_rom_path = "runtime_insp_character.bin";
    config->history_memory_mb = 16u;
    config->history_memory_mb_configured = true;
    config->frame_ring_memory_mb = 16u;
    config->frame_ring_memory_mb_configured = true;
    config->vic_ring_memory_mb = 0u;
    config->vic_ring_memory_mb_configured = true;
    config->inspector_memory_mb = 128u;
    config->inspector_memory_mb_configured = true;
}

int main(void) {
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_frame_ring_info film;
    runtime_history_status history;
    uint64_t token;

    write_test_roms();
    if (!runtime_init()) {
        fail("runtime_init failed");
    }

    fill_base_config(&config);
    rt = start_runtime(&config, &client);
    if (runtime_inspector_enabled(rt)) {
        fail("default Inspector is on");
    }
    if (runtime_inspector_memory_mb(rt) != 128u) {
        fail("default inspector_memory_mb is wrong");
    }
    runtime_client_get_frame_ring_info(client, &film);
    history = request_history_status(client);
    if (!film.recording) {
        fail("default play should already record film");
    }
    if (!history.recording) {
        fail("default play should already record HST1");
    }

    runtime_client_set_frame_ring_recording(client, false);
    runtime_client_get_frame_ring_info(client, &film);
    if (film.recording) {
        fail("standalone frame-ring-record off did not stop");
    }

    token = runtime_client_alloc_request_token(client);
    if (!runtime_client_inspector_set_enabled(client, false, token)) {
        fail("inspector_set_enabled off failed to queue");
    }
    drain_commands(client);
    if (runtime_inspector_enabled(rt)) {
        fail("Inspector off enabled the product");
    }
    runtime_client_get_frame_ring_info(client, &film);
    history = request_history_status(client);
    if (film.recording) {
        fail("Inspector off re-armed film");
    }
    if (!history.recording) {
        fail("Inspector off stopped HST1");
    }

    token = runtime_client_alloc_request_token(client);
    if (!runtime_client_inspector_set_enabled(client, true, token)) {
        fail("inspector_set_enabled on failed to queue");
    }
    drain_commands(client);
    if (!runtime_inspector_enabled(rt)) {
        fail("off->on did not enable Inspector");
    }
    runtime_client_get_frame_ring_info(client, &film);
    history = request_history_status(client);
    if (!film.recording) {
        fail("off->on did not re-arm film");
    }
    if (!history.recording) {
        fail("off->on changed HST1 recording");
    }

    runtime_client_set_frame_ring_recording(client, false);
    token = runtime_client_alloc_request_token(client);
    if (!runtime_client_inspector_set_enabled(client, true, token)) {
        fail("already-on set_enabled failed to queue");
    }
    drain_commands(client);
    runtime_client_get_frame_ring_info(client, &film);
    if (film.recording) {
        fail("already-on Inspector re-armed film after standalone off");
    }

    token = runtime_client_alloc_request_token(client);
    if (!runtime_client_inspector_set_enabled(client, false, token)) {
        fail("inspector_set_enabled leave failed to queue");
    }
    drain_commands(client);
    runtime_client_get_frame_ring_info(client, &film);
    history = request_history_status(client);
    if (film.recording) {
        fail("turning Inspector off started film");
    }
    if (!history.recording) {
        fail("turning Inspector off stopped HST1");
    }
    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector = true;
    rt = start_runtime(&config, &client);
    if (!runtime_inspector_enabled(rt)) {
        fail("startup inspector=1 did not enable");
    }
    runtime_client_get_frame_ring_info(client, &film);
    history = request_history_status(client);
    if (!film.recording) {
        fail("startup inspector=1 did not leave film recording");
    }
    if (!history.recording) {
        fail("startup inspector=1 changed HST1");
    }
    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector = true;
    config.history_memory_mb = 0u;
    rt = start_runtime(&config, &client);
    if (!runtime_inspector_enabled(rt)) {
        fail("inspector=1 with HST1 off did not enable");
    }
    history = request_history_status(client);
    if (history.recording) {
        fail("inspector=1 armed HST1");
    }
    runtime_client_get_frame_ring_info(client, &film);
    if (!film.recording) {
        fail("inspector=1 with HST1 off did not arm film");
    }
    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector = true;
    config.inspector_memory_mb = 0u;
    rt = start_runtime(&config, &client);
    if (!runtime_inspector_enabled(rt)) {
        fail("inspector=1 budget 0 refused enable");
    }
    if (runtime_inspector_memory_mb(rt) != 0u) {
        fail("typed 0 inspector_memory_mb was not honoured");
    }
    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector = true;
    config.frame_ring_memory_mb = 0u;
    rt = start_runtime(&config, &client);
    if (!runtime_inspector_enabled(rt)) {
        fail("inspector=1 with film budget 0 did not enable");
    }
    runtime_client_get_frame_ring_info(client, &film);
    if (film.recording || film.capacity != 0u) {
        fail("inspector=1 armed film with budget 0");
    }
    stop_runtime(rt, client);

    runtime_shutdown();
    remove("runtime_insp_64c.bin");
    remove("runtime_insp_character.bin");
    printf("test_runtime_inspector: ok\n");
    return 0;
}
