#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char **argv) {
    runtime_config config = {0};
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    double seconds = parse_seconds(argc, argv);
    double start;
    uint64_t events = 0;

    config.system_rom_path = "roms/system.rom";
    config.char_rom_path = "roms/character.rom";
    config.machine_config.video_standard = C64_VIDEO_STANDARD_PAL;
    runtime_config_set_turbo_defaults(&config);

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

    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();

    printf("seconds=%.3f events=%llu\n", seconds, (unsigned long long)events);
    return 0;
}
