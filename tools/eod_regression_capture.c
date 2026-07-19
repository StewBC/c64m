#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    EOD_SWAP_PC = 0x020c,
    EOD_DEPACK_PC = 0x028a,
    EOD_CHECKER_PC = 0xa3bd,
    EOD_PLASMA_PC = 0x1000,
    PAL_FRAME_CYCLES = 63 * 312
};

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static int wait_for_event(
    runtime_client *client,
    runtime_event_type wanted,
    runtime_event *out,
    int timeout_seconds)
{
    time_t deadline = time(NULL) + timeout_seconds;
    runtime_event event;

    while (time(NULL) <= deadline) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == wanted) {
                if (out != NULL) {
                    *out = event;
                }
                return 1;
            }
        }
        {
            const struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return 0;
}

static int run_until_pc(runtime_client *client, uint16_t address, int timeout_seconds) {
    time_t deadline = time(NULL) + timeout_seconds;
    runtime_event event;
    int pause_pending = 0;
    unsigned incidental_stops = 0;

    if (!runtime_client_set_execute_breakpoint(client, address) ||
        !runtime_client_run(client)) {
        return 0;
    }

    while (time(NULL) <= deadline) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_PAUSED) {
                pause_pending = 1;
            } else if (pause_pending &&
                       event.type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                if (event.data.machine_state.pc == address) {
                    return 1;
                }
                if (incidental_stops++ < 10u) {
                    fprintf(stderr, "resuming incidental stop at $%04x (reason %d)\n",
                        event.data.machine_state.pc,
                        event.data.machine_state.stop_reason);
                }
                pause_pending = 0;
                if (!runtime_client_run(client)) {
                    return 0;
                }
            }
        }
        {
            const struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    return 0;
}

static void write_ppm(const char *path, const c64_frame *frame) {
    FILE *file = fopen(path, "wb");
    uint32_t y;

    if (file == NULL) {
        fail("failed to create capture");
    }
    fprintf(file, "P6\n%u %u\n255\n", frame->width, frame->height);
    for (y = 0; y < frame->height; ++y) {
        uint32_t x;
        for (x = 0; x < frame->width; ++x) {
            uint32_t argb = frame->pixels[y * frame->width + x];
            fputc((int)((argb >> 16) & 0xffu), file);
            fputc((int)((argb >> 8) & 0xffu), file);
            fputc((int)(argb & 0xffu), file);
        }
    }
    fclose(file);
}

static int run_live_frames(runtime_client *client, unsigned count, c64_frame *out) {
    runtime_event event;

    while (runtime_client_poll_event(client, &event)) {
    }
    while (runtime_client_poll_frame(client, out)) {
    }
    if (!runtime_client_run(client)) {
        return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!wait_for_event(client, RUNTIME_EVENT_FRAME_READY, &event, 10) ||
            !runtime_client_poll_frame(client, out)) {
            return 0;
        }
    }
    if (!runtime_client_pause(client) ||
        !wait_for_event(client, RUNTIME_EVENT_PAUSED, &event, 10)) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    runtime_config config = {0};
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;
    unsigned advance_frames = 16u;
    unsigned sample_count = 1u;
    unsigned sample_interval = 50u;
    const char *scene = "checker";

    if (argc < 7 || argc > 11) {
        fprintf(stderr,
            "usage: %s SYSTEM CHAR 1541 DISK0 DISK1 OUTPUT.ppm "
            "[FRAMES] [checker|plasma|+RACE_FRAMES] [SAMPLES] [INTERVAL]\n",
            argv[0]);
        return 2;
    }
    if (argc >= 8) {
        advance_frames = (unsigned)strtoul(argv[7], NULL, 10);
    }
    if (argc >= 9) {
        scene = argv[8];
        if (strcmp(scene, "checker") != 0 && strcmp(scene, "plasma") != 0 &&
            scene[0] != '+') {
            fail("unknown scene");
        }
    }
    if (argc >= 10) {
        sample_count = (unsigned)strtoul(argv[9], NULL, 10);
        if (sample_count == 0u) fail("sample count must be nonzero");
    }
    if (argc == 11) {
        sample_interval = (unsigned)strtoul(argv[10], NULL, 10);
        if (sample_interval == 0u) fail("sample interval must be nonzero");
    }

    config.system_rom_path = argv[1];
    config.char_rom_path = argv[2];
    config.rom1541_path = argv[3];
    config.machine_config.video_standard = C64_VIDEO_STANDARD_PAL;
    config.machine_config.emulate_1541 = 1;
    config.machine_config.media_1541 = 1;
    config.active_turbo_multiplier = 256;
    config.autorun = true;

    if (!runtime_init()) {
        fail("runtime_init failed");
    }
    rt = runtime_create(&config);
    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime start failed");
    }
    client = runtime_get_client(rt);
    if (!wait_for_event(client, RUNTIME_EVENT_STARTED, NULL, 10) ||
        !wait_for_event(client, RUNTIME_EVENT_RESET_COMPLETE, NULL, 10)) {
        fail("runtime startup timed out");
    }

    if (!runtime_client_set_turbo_multiplier(client, 256) ||
        !runtime_client_mount_d64(client, 8, argv[4]) ||
        !run_until_pc(client, EOD_SWAP_PC, 300)) {
        fail("disk-swap marker timed out");
    }
    fprintf(stderr, "swap reached\n");

    if (!runtime_client_mount_d64(client, 8, argv[5]) ||
        !runtime_client_clear_all_breakpoints(client) ||
        !run_until_pc(client, EOD_DEPACK_PC, 300)) {
        fail("disk-1 depack marker timed out");
    }
    fprintf(stderr, "disk-1 depack reached\n");

    if (!runtime_client_clear_all_breakpoints(client) ||
        !runtime_client_set_turbo_multiplier(client, 7) ||
        !run_until_pc(client, EOD_CHECKER_PC, 300)) {
        fail("checker marker timed out");
    }
    fprintf(stderr, "checker marker reached\n");

    if (strcmp(scene, "plasma") == 0) {
        if (!runtime_client_clear_all_breakpoints(client) ||
            !runtime_client_set_turbo_multiplier(client, 7) ||
            !run_until_pc(client, EOD_PLASMA_PC, 300)) {
            fail("plasma marker timed out");
        }
        fprintf(stderr, "plasma marker reached\n");
    } else if (scene[0] == '+') {
        unsigned long race_frames = strtoul(scene + 1, NULL, 10);
        if (race_frames == 0ul ||
            !runtime_client_clear_all_breakpoints(client) ||
            !runtime_client_set_turbo_multiplier(client, 7) ||
            !runtime_client_run_cycles(client,
                (size_t)race_frames * (size_t)PAL_FRAME_CYCLES) ||
            !wait_for_event(client, RUNTIME_EVENT_RUN_COMPLETE, &event, 300)) {
            fail("scene race timed out");
        }
        fprintf(stderr, "scene race advanced %lu frames\n", race_frames);
    }

    if (!runtime_client_clear_all_breakpoints(client) ||
        !runtime_client_set_turbo_multiplier(client, 7)) {
        fail("live capture setup failed");
    }

    /* Turbo display uses a geometric debug reconstruction and the frame slot
       retains its oldest undrained frame.  Drain that slot, enable live video,
       then discard one full PAL frame so the cycle renderer starts from a clean
       frame boundary.  Keep draining FRAME_READY payloads while advancing so
       the final payload really is the requested live raster frame. */
    while (runtime_client_poll_frame(client, &frame)) {
    }
    if (!run_live_frames(client, 1u, &frame)) {
        fail("live-video warm-up frame timed out");
    }
    if (!run_live_frames(client, advance_frames, &frame)) {
        fail("live scene frame timed out");
    }
    for (unsigned sample = 0; sample < sample_count; ++sample) {
        char path[1024];

        if (sample != 0u &&
            !run_live_frames(client, sample_interval, &frame)) {
            fail("live sample interval timed out");
        }
        if (sample_count == 1u) {
            snprintf(path, sizeof(path), "%s", argv[6]);
        } else {
            snprintf(path, sizeof(path), "%s.%03u.ppm", argv[6], sample);
        }
        fprintf(stderr, "captured frame %llu: %s\n",
            (unsigned long long)frame.frame_number, path);
        write_ppm(path, &frame);
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
    return 0;
}
