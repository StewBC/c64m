#include "runtime_internal.h"

#include "message_queue.h"
#include "runtime_command.h"
#include "runtime_event.h"

#include <stdio.h>
#include <string.h>

static bool runtime_publish_event(
    runtime *rt,
    const runtime_event *event) {
    return message_queue_push(rt->event_queue, event);
}

static void runtime_publish_simple_event(
    runtime *rt,
    runtime_event_type type) {
    runtime_event event = {
        .type = type,
    };

    runtime_publish_event(rt, &event);
}

static void runtime_publish_error(
    runtime *rt,
    const char *message) {
    runtime_event event = {
        .type = RUNTIME_EVENT_ERROR,
    };

    snprintf(event.data.error.message, sizeof(event.data.error.message), "%s", message);
    runtime_publish_event(rt, &event);
}

static void runtime_publish_cpu_state(runtime *rt) {
    runtime_event event = {
        .type = RUNTIME_EVENT_CPU_STATE_RESPONSE,
    };
    c64_cpu_snapshot snapshot;

    c64_copy_cpu_snapshot(&rt->machine, &snapshot);
    event.data.cpu_state.pc = snapshot.pc;
    event.data.cpu_state.a = snapshot.a;
    event.data.cpu_state.x = snapshot.x;
    event.data.cpu_state.y = snapshot.y;
    event.data.cpu_state.sp = snapshot.sp;
    event.data.cpu_state.p = snapshot.p;
    event.data.cpu_state.cycles = snapshot.cycles;

    runtime_publish_event(rt, &event);
}

static bool runtime_load_rom(
    runtime *rt,
    const char *name,
    const char *path,
    bool (*load)(c64_rom_set *roms, const char *path, char *error, size_t error_size)) {
    char message[256];
    char error[256];

    if (!path || path[0] == '\0') {
        return true;
    }

    if (load(&rt->roms, path, error, sizeof(error))) {
        return true;
    }

    snprintf(message, sizeof(message), "failed to load %s ROM from %s: %s", name, path, error);
    runtime_publish_error(rt, message);
    return false;
}

static bool runtime_load_configured_roms(runtime *rt) {
    bool ok = true;

    c64_rom_set_init(&rt->roms);

    ok = runtime_load_rom(rt, "system", rt->system_rom_path, c64_rom_load_combined_64c) && ok;
    ok = runtime_load_rom(rt, "BASIC", rt->basic_rom_path, c64_rom_load_basic) && ok;
    ok = runtime_load_rom(rt, "character", rt->char_rom_path, c64_rom_load_character) && ok;
    ok = runtime_load_rom(rt, "KERNAL", rt->kernal_rom_path, c64_rom_load_kernal) && ok;

    return ok;
}

static bool runtime_reset_machine(runtime *rt) {
    char error[256];

    if (!c64_install_roms(&rt->machine, &rt->roms, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    if (!c64_reset(&rt->machine, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_RESET_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

static bool runtime_step_instruction(runtime *rt) {
    char error[256];

    if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

int runtime_thread_main(void *userdata) {
    runtime *rt = userdata;
    bool running = true;

    c64_init(&rt->machine);
    runtime_load_configured_roms(rt);
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STARTED);

    while (running) {
        runtime_command command;

        if (!message_queue_wait_pop(rt->command_queue, &command)) {
            continue;
        }

        switch (command.type) {
            case RUNTIME_COMMAND_PING:
                runtime_publish_simple_event(rt, RUNTIME_EVENT_PONG);
                break;

            case RUNTIME_COMMAND_QUIT:
                running = false;
                break;

            case RUNTIME_COMMAND_RESET:
                runtime_reset_machine(rt);
                break;

            case RUNTIME_COMMAND_STEP_INSTRUCTION:
                runtime_step_instruction(rt);
                break;

            case RUNTIME_COMMAND_REQUEST_CPU_STATE:
                runtime_publish_cpu_state(rt);
                break;

            case RUNTIME_COMMAND_NONE:
            default:
                runtime_publish_error(rt, "unsupported runtime command");
                break;
        }
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_STOPPED);
    return 0;
}
