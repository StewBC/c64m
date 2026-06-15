#include "runtime_internal.h"

#include "message_queue.h"
#include "runtime_command.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

enum {
    RUNTIME_RUN_BATCH_CYCLES = 1024,
    RUNTIME_TARGET_FPS = 60,
};

static void runtime_reset_pacer(runtime *rt) {
    uint64_t frequency;

    frequency = SDL_GetPerformanceFrequency();
    rt->frame_counter_step = frequency / RUNTIME_TARGET_FPS;
    if (rt->frame_counter_step == 0) {
        rt->frame_counter_step = 1;
    }
    rt->next_frame_counter = SDL_GetPerformanceCounter() + rt->frame_counter_step;
    rt->pace_initialized = true;
}

static void runtime_pace_after_frame(runtime *rt) {
    uint64_t now;
    uint64_t frequency;

    if (!rt->pace_initialized) {
        runtime_reset_pacer(rt);
        return;
    }

    now = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    if (now < rt->next_frame_counter) {
        uint64_t remaining = rt->next_frame_counter - now;
        uint32_t delay_ms = (uint32_t)((remaining * 1000u) / frequency);
        if (delay_ms > 0) {
            SDL_Delay(delay_ms);
        }
        while (SDL_GetPerformanceCounter() < rt->next_frame_counter) {
            SDL_Delay(0);
        }
    }

    rt->next_frame_counter += rt->frame_counter_step;
    now = SDL_GetPerformanceCounter();
    if (rt->next_frame_counter < now) {
        rt->next_frame_counter = now + rt->frame_counter_step;
    }
}

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
    event.data.machine_state.frame_number = rt->frame_slot.published_frames;
    event.data.machine_state.frame_cycle = rt->next_frame_cycle;
    event.data.machine_state.dropped_frames = rt->frame_slot.dropped_frames;
    event.data.machine_state.screen_ram_writes = snapshot.screen_ram_writes;
    event.data.machine_state.color_ram_writes = snapshot.color_ram_writes;
    event.data.machine_state.vic_register_writes = snapshot.vic_register_writes;
    event.data.machine_state.cia1_register_writes = snapshot.cia1_register_writes;
    event.data.machine_state.cia2_register_writes = snapshot.cia2_register_writes;
    event.data.machine_state.keyboard_events = snapshot.keyboard_events;
    event.data.machine_state.irq_entries = snapshot.irq_entries;
    event.data.machine_state.cia1_icr_reads = snapshot.cia1_icr_reads;
    event.data.machine_state.cia1_icr_writes = snapshot.cia1_icr_writes;
    event.data.machine_state.cia1_interrupt_assertions = snapshot.cia1_interrupt_assertions;
    event.data.machine_state.nmi_entries = snapshot.nmi_entries;
    event.data.machine_state.restore_requests = snapshot.restore_requests;
    event.data.machine_state.cia1_irq_pending = snapshot.cia1_irq_pending ? 1 : 0;
    event.data.machine_state.cia2_nmi_pending = snapshot.cia2_nmi_pending ? 1 : 0;

    runtime_publish_event(rt, &event);
}

static bool runtime_publish_frame(runtime *rt) {
    runtime_event event = {
        .type = RUNTIME_EVENT_FRAME_READY,
    };
    c64_frame frame;

    if (!c64_make_frame_snapshot(&rt->machine, &frame)) {
        runtime_publish_error(rt, "failed to generate frame");
        return false;
    }

    mutex_lock(rt->frame_slot.mutex);
    if (rt->frame_slot.has_frame) {
        rt->frame_slot.dropped_frames++;
    }
    rt->frame_slot.frame = frame;
    rt->frame_slot.has_frame = true;
    rt->frame_slot.published_frames++;
    event.data.frame_ready.frame_number = frame.frame_number;
    event.data.frame_ready.machine_cycle = frame.machine_cycle;
    event.data.frame_ready.dropped_frames = rt->frame_slot.dropped_frames;
    mutex_unlock(rt->frame_slot.mutex);

    runtime_publish_event(rt, &event);
    return true;
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

    mutex_lock(rt->frame_slot.mutex);
    rt->frame_slot.has_frame = false;
    rt->frame_slot.published_frames = 0;
    rt->frame_slot.consumed_frames = 0;
    rt->frame_slot.dropped_frames = 0;
    mutex_unlock(rt->frame_slot.mutex);

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->next_frame_cycle = 0;
    rt->pace_initialized = false;
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

static void runtime_set_cpu_register(runtime *rt, const runtime_command *command) {
    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_cpu_state(rt);
        return;
    }

    switch (command->data.set_cpu_register.reg) {
        case RUNTIME_CPU_REGISTER_PC:
            rt->machine.cpu.cpu.pc = command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_SP:
            rt->machine.cpu.cpu.sp = 0x0100u + (command->data.set_cpu_register.value & 0x00ffu);
            break;

        case RUNTIME_CPU_REGISTER_A:
            rt->machine.cpu.cpu.A = (uint8_t)command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_X:
            rt->machine.cpu.cpu.X = (uint8_t)command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_Y:
            rt->machine.cpu.cpu.Y = (uint8_t)command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_STATUS:
            rt->machine.cpu.cpu.flags = (uint8_t)command->data.set_cpu_register.value;
            break;

        default:
            runtime_publish_error(rt, "unsupported CPU register set command");
            return;
    }

    runtime_publish_cpu_state(rt);
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
            runtime_reset_pacer(rt);
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

        case RUNTIME_COMMAND_REQUEST_FRAME:
            runtime_publish_frame(rt);
            break;

        case RUNTIME_COMMAND_KEYBOARD_KEY:
            c64_set_key(&rt->machine, command->data.keyboard_key.key, command->data.keyboard_key.pressed != 0);
            break;

        case RUNTIME_COMMAND_RESTORE:
            c64_restore(&rt->machine);
            break;

        case RUNTIME_COMMAND_SET_CPU_REGISTER:
            runtime_set_cpu_register(rt, command);
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
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->next_frame_cycle = 0;
    rt->pace_initialized = false;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STARTED);
    if (runtime_load_configured_roms(rt)) {
        runtime_reset_machine(rt);
    }

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
                if (c64_consume_frame_complete(&rt->machine)) {
                    runtime_publish_frame(rt);
                    rt->next_frame_cycle = rt->machine.clock.cycle;
                    runtime_pace_after_frame(rt);
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
