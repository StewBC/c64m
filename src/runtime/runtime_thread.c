#include "runtime_internal.h"

#include "message_queue.h"
#include "runtime_command.h"
#include "runtime_event.h"

#include <stdio.h>
#include <string.h>

enum {
    RUNTIME_RUN_BATCH_CYCLES = 1024,
};

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

static void runtime_publish_machine_state(runtime *rt) {
    runtime_event event = {
        .type = RUNTIME_EVENT_MACHINE_STATE_RESPONSE,
    };
    c64_machine_snapshot snapshot;

    c64_copy_machine_snapshot(&rt->machine, &snapshot);
    event.data.machine_state.cycle = snapshot.cycle;
    event.data.machine_state.cpu_cycles = snapshot.cpu_cycles;
    event.data.machine_state.vic_cycles = snapshot.vic_cycles;
    event.data.machine_state.cia_cycles = snapshot.cia_cycles;
    event.data.machine_state.pc = snapshot.pc;
    event.data.machine_state.a = snapshot.a;
    event.data.machine_state.x = snapshot.x;
    event.data.machine_state.y = snapshot.y;
    event.data.machine_state.sp = snapshot.sp;
    event.data.machine_state.p = snapshot.p;
    event.data.machine_state.ready = snapshot.ready ? 1 : 0;
    event.data.machine_state.running = rt->exec_state == RUNTIME_EXEC_RUNNING ? 1 : 0;

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

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RESET_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

static bool runtime_step_cycle(runtime *rt) {
    char error[256];

    if (!c64_step_cycle(&rt->machine, error, sizeof(error))) {
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        runtime_publish_error(rt, error);
        return false;
    }

    return true;
}

static bool runtime_step_cycle_command(runtime *rt) {
    if (!runtime_step_cycle(rt)) {
        return false;
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_machine_state(rt);
    return true;
}

static bool runtime_step_instruction(runtime *rt) {
    char error[256];
    c64_cpu_snapshot snapshot;

    if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    c64_copy_cpu_snapshot(&rt->machine, &snapshot);
    fprintf(
        stderr,
        "STEP instruction PC=%04X CYCLES=%llu\n",
        snapshot.pc,
        (unsigned long long)snapshot.cycles);
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

static bool runtime_run_instructions(runtime *rt, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; i++) {
        if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
            runtime_publish_error(rt, error);
            return false;
        }
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

static bool runtime_run_cycles(runtime *rt, size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (!runtime_step_cycle(rt)) {
            return false;
        }
    }

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RUN_COMPLETE);
    runtime_publish_machine_state(rt);
    return true;
}

static bool runtime_process_command(runtime *rt, const runtime_command *command, bool *alive) {
    switch (command->type) {
        case RUNTIME_COMMAND_PING:
            runtime_publish_simple_event(rt, RUNTIME_EVENT_PONG);
            break;

        case RUNTIME_COMMAND_QUIT:
            rt->exec_state = RUNTIME_EXEC_STOPPED;
            *alive = false;
            break;

        case RUNTIME_COMMAND_RESET:
            runtime_reset_machine(rt);
            break;

        case RUNTIME_COMMAND_RUN:
            fprintf(stderr, "RUN command received\n");
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
            break;

        case RUNTIME_COMMAND_PAUSE:
            fprintf(stderr, "PAUSE command received\n");
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
            runtime_publish_machine_state(rt);
            break;

        case RUNTIME_COMMAND_STEP_CYCLE:
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_step_cycle_command(rt);
            break;

        case RUNTIME_COMMAND_STEP_INSTRUCTION:
            fprintf(stderr, "STEP_INSTRUCTION command received\n");
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_step_instruction(rt);
            break;

        case RUNTIME_COMMAND_RUN_CYCLES:
            runtime_run_cycles(rt, command->data.run_cycles.count);
            break;

        case RUNTIME_COMMAND_RUN_INSTRUCTIONS:
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_run_instructions(rt, command->data.run_instructions.count);
            break;

        case RUNTIME_COMMAND_REQUEST_CPU_STATE:
            runtime_publish_cpu_state(rt);
            break;

        case RUNTIME_COMMAND_REQUEST_MACHINE_STATE:
            runtime_publish_machine_state(rt);
            break;

        case RUNTIME_COMMAND_NONE:
        default:
            runtime_publish_error(rt, "unsupported runtime command");
            break;
    }

    return *alive;
}

int runtime_thread_main(void *userdata) {
    runtime *rt = userdata;
    bool alive = true;

    c64_init(&rt->machine);
    runtime_load_configured_roms(rt);
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STARTED);

    while (alive) {
        runtime_command command;

        if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
            int i;

            while (message_queue_try_pop(rt->command_queue, &command)) {
                if (!runtime_process_command(rt, &command, &alive)) {
                    break;
                }
            }

            for (i = 0; alive && rt->exec_state == RUNTIME_EXEC_RUNNING && i < RUNTIME_RUN_BATCH_CYCLES; i++) {
                if (!runtime_step_cycle(rt)) {
                    break;
                }
            }

            continue;
        }

        if (!message_queue_wait_pop(rt->command_queue, &command)) {
            continue;
        }

        runtime_process_command(rt, &command, &alive);
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_STOPPED);
    return 0;
}
