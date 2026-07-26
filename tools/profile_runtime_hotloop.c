#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#define DEFAULT_SECONDS 12.0

static double monotonic_seconds(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

static double parse_seconds(int argc, char **argv) {
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        return DEFAULT_SECONDS;
    }
    return strtod(argv[1], NULL);
}

static bool wait_for_event(
    runtime_client *client,
    runtime_event_type type,
    uint64_t token,
    runtime_event *out,
    uint64_t *events) {
    double start = monotonic_seconds();
    runtime_event event;

    while (monotonic_seconds() - start < 5.0) {
        while (runtime_client_poll_event(client, &event)) {
            (*events)++;
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                return false;
            }
            if (event.type == type &&
                (token == 0u || event.request_token == token)) {
                if (out != NULL) {
                    *out = event;
                }
                return true;
            }
        }
    }
    return false;
}

int main(int argc, char **argv) {
    runtime_config config = {0};
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    double seconds = parse_seconds(argc, argv);
    double start;
    double elapsed;
    uint64_t events = 0;
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t token;
    runtime_history_status history_status;
    const char *history_mode =
        argc > 2 && argv[2] != NULL ? argv[2] : "full";

    config.system_rom_path = "roms/system.rom";
    config.char_rom_path = "roms/character.rom";
    config.machine_config.video_standard = C64_VIDEO_STANDARD_PAL;
    config.history_memory_mb_configured = true;
    config.history_memory_mb =
        strcmp(history_mode, "config-off") == 0 ?
            0u : RUNTIME_HISTORY_DEFAULT_MEMORY_MB;
    runtime_config_set_turbo_defaults(&config);
    config.turbo_speeds[0] = RUNTIME_TURBO_MODE_MAX;
    config.turbo_speed_count = 1u;
    config.active_turbo_multiplier = RUNTIME_TURBO_MODE_MAX;

    if (!runtime_init()) {
        fprintf(stderr, "runtime_init failed\n");
        return 1;
    }

    rt = runtime_create(&config);
    if (rt == NULL || !runtime_start(rt)) {
        fprintf(stderr, "runtime start failed\n");
        runtime_destroy(rt);
        runtime_shutdown();
        return 1;
    }
    client = runtime_get_client(rt);

    start = monotonic_seconds();
    while (monotonic_seconds() - start < 0.25) {
        while (runtime_client_poll_event(client, &event)) {
            events++;
        }
    }

    if (strcmp(history_mode, "record-off") == 0) {
        runtime_event status_event;
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_history_record(client, false, token) ||
            !wait_for_event(
                client,
                RUNTIME_EVENT_HISTORY_STATUS_RESPONSE,
                token,
                &status_event,
                &events)) {
            fprintf(stderr, "failed to stop history\n");
            runtime_destroy(rt);
            runtime_shutdown();
            return 1;
        }
    }
    if (!runtime_client_request_machine_state(client) ||
        !wait_for_event(
            client,
            RUNTIME_EVENT_MACHINE_STATE_RESPONSE,
            0u,
            &event,
            &events)) {
        fprintf(stderr, "initial machine state failed\n");
        runtime_destroy(rt);
        runtime_shutdown();
        return 1;
    }
    start_cycle = event.data.machine_state.cycle;

    if (!runtime_client_run(client)) {
        fprintf(stderr, "runtime run command failed\n");
        runtime_destroy(rt);
        runtime_shutdown();
        return 1;
    }

    start = monotonic_seconds();
    while (monotonic_seconds() - start < seconds) {
        while (runtime_client_poll_event(client, &event)) {
            events++;
        }
    }
    elapsed = monotonic_seconds() - start;

    if (!runtime_client_pause(client) ||
        !wait_for_event(
            client, RUNTIME_EVENT_PAUSED, 0u, &event, &events) ||
        !runtime_client_request_machine_state(client) ||
        !wait_for_event(
            client,
            RUNTIME_EVENT_MACHINE_STATE_RESPONSE,
            0u,
            &event,
            &events)) {
        fprintf(stderr, "final pause/state failed\n");
        runtime_destroy(rt);
        runtime_shutdown();
        return 1;
    }
    end_cycle = event.data.machine_state.cycle;

    token = runtime_client_alloc_request_token(client);
    if (!runtime_client_history_info(client, token) ||
        !wait_for_event(
            client,
            RUNTIME_EVENT_HISTORY_STATUS_RESPONSE,
            token,
            &event,
            &events)) {
        fprintf(stderr, "history status failed\n");
        runtime_destroy(rt);
        runtime_shutdown();
        return 1;
    }
    history_status = event.data.history_status;

    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();

    printf(
        "seconds=%.3f mhz=%.3f cycles=%llu events=%llu history_mode=%s "
        "available=%d recording=%d records=%llu used_bytes=%zu "
        "bytes_per_record=%.3f wraps=%llu\n",
        elapsed,
        elapsed > 0.0 ?
            (double)(end_cycle - start_cycle) / elapsed / 1000000.0 : 0.0,
        (unsigned long long)(end_cycle - start_cycle),
        (unsigned long long)events,
        history_mode,
        history_status.available ? 1 : 0,
        history_status.recording ? 1 : 0,
        (unsigned long long)history_status.record_count,
        history_status.used_bytes,
        history_status.record_count > 0u ?
            (double)history_status.used_bytes /
                (double)history_status.record_count : 0.0,
        (unsigned long long)history_status.wrap_count);
    return 0;
}
