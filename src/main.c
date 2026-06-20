#include "app_options.h"
#include "audio_buffer.h"
#include "frontend.h"
#include "frontend_input.h"
#include "platform.h"
#include "platform_audio.h"
#include "runtime.h"
#include "runtime_client.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    C64M_CONTROLLER_MAX = 2,
    C64M_CONTROLLER_AXIS_THRESHOLD = 16000
};

typedef struct sdl_c64_controller {
    SDL_GameController *controller;
    SDL_JoystickID instance_id;
    uint8_t inputs;
} sdl_c64_controller;

typedef struct sdl_c64_controller_state {
    sdl_c64_controller controllers[C64M_CONTROLLER_MAX];
    unsigned single_controller_port;
    bool swapped;
} sdl_c64_controller_state;

static bool choose_file_path(char *out_path, size_t out_size, const char *prompt, const char *types) {
#if defined(__APPLE__)
    FILE *pipe;
    char *newline;
    char command[512];

    if (out_path == NULL || out_size == 0) {
        return false;
    }

    out_path[0] = '\0';
    snprintf(
        command,
        sizeof(command),
        "osascript -e 'POSIX path of (choose file with prompt \"%s\"%s)'",
        prompt,
        types != NULL ? types : "");
    pipe = popen(command, "r");
    if (pipe == NULL) {
        return false;
    }

    if (fgets(out_path, (int)out_size, pipe) == NULL) {
        pclose(pipe);
        out_path[0] = '\0';
        return false;
    }
    pclose(pipe);

    newline = strchr(out_path, '\n');
    if (newline != NULL) {
        *newline = '\0';
    }

    return out_path[0] != '\0';
#else
    (void)out_path;
    (void)out_size;
    (void)prompt;
    (void)types;
    return false;
#endif
}

static bool choose_prg_path(char *out_path, size_t out_size) {
    return choose_file_path(out_path, out_size, "Load PRG/BAS", NULL);
}

static bool choose_disk_path(char *out_path, size_t out_size) {
    return choose_file_path(out_path, out_size, "Mount Disk Image", NULL);
}

static bool choose_ini_path(char *out_path, size_t out_size) {
    return choose_file_path(out_path, out_size, "Select INI File", " of type {\"ini\"}");
}

static bool choose_symbol_path(char *out_path, size_t out_size) {
    return choose_file_path(out_path, out_size, "Select Symbol File", NULL);
}

static bool choose_save_path(char *out_path, size_t out_size, const char *prompt) {
#if defined(__APPLE__)
    FILE *pipe;
    char *newline;
    char command[512];

    if (out_path == NULL || out_size == 0) {
        return false;
    }

    out_path[0] = '\0';
    snprintf(
        command,
        sizeof(command),
        "osascript -e 'POSIX path of (choose file name with prompt \"%s\")'",
        prompt);
    pipe = popen(command, "r");
    if (pipe == NULL) {
        return false;
    }

    if (fgets(out_path, (int)out_size, pipe) == NULL) {
        pclose(pipe);
        out_path[0] = '\0';
        return false;
    }
    pclose(pipe);

    newline = strchr(out_path, '\n');
    if (newline != NULL) {
        *newline = '\0';
    }

    return out_path[0] != '\0';
#else
    (void)out_path;
    (void)out_size;
    (void)prompt;
    return false;
#endif
}

static void make_relative_path(const char *abs_path, char *out, size_t out_size) {
    char cwd[1024];
    size_t cwd_len;

    if (abs_path == NULL || out == NULL || out_size == 0) {
        return;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(out, out_size, "%s", abs_path);
        return;
    }

    cwd_len = strlen(cwd);
    if (cwd_len > 0 &&
        strncmp(abs_path, cwd, cwd_len) == 0 &&
        abs_path[cwd_len] == '/') {
        snprintf(out, out_size, ".%s", abs_path + cwd_len);
    } else {
        snprintf(out, out_size, "%s", abs_path);
    }
}

static c64_config machine_config_from_options(const app_options *options) {
    c64_config config = {
        .video_standard = C64_VIDEO_STANDARD_NTSC,
    };

    if (options != NULL &&
        options->video_standard != NULL &&
        strcmp(options->video_standard, "PAL") == 0) {
        config.video_standard = C64_VIDEO_STANDARD_PAL;
    }
    return config;
}

static runtime_config runtime_config_from_options(const app_options *options) {
    runtime_config config = {0};

    runtime_config_set_turbo_defaults(&config);
    if (options != NULL) {
        runtime_config_set_turbo_csv(&config, options->turbo_multipliers);
    }
    return config;
}

static void request_debug_state(runtime_client *client) {
    runtime_client_request_cpu_state(client);
    runtime_client_request_machine_state(client);
    runtime_client_request_breakpoints(client);
    runtime_client_request_disk_status(client, 8);
    runtime_client_request_disk_status(client, 9);
}

static void update_debug_state_from_event(
    frontend_debug_state *debug_state,
    const runtime_event *event) {
    if (debug_state == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
        case RUNTIME_EVENT_RUNNING:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_RUNNING;
            break;

        case RUNTIME_EVENT_PAUSED:
        case RUNTIME_EVENT_RESET_COMPLETE:
        case RUNTIME_EVENT_STEP_COMPLETE:
        case RUNTIME_EVENT_RUN_COMPLETE:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_PAUSED;
            break;

        case RUNTIME_EVENT_ERROR:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_ERROR;
            break;

        case RUNTIME_EVENT_CPU_STATE_RESPONSE:
            debug_state->cpu = event->data.cpu_state;
            debug_state->has_cpu = true;
            break;

        case RUNTIME_EVENT_MACHINE_STATE_RESPONSE:
            debug_state->cpu.pc = event->data.machine_state.pc;
            debug_state->cpu.a = event->data.machine_state.a;
            debug_state->cpu.x = event->data.machine_state.x;
            debug_state->cpu.y = event->data.machine_state.y;
            debug_state->cpu.sp = event->data.machine_state.sp;
            debug_state->cpu.p = event->data.machine_state.p;
            debug_state->cpu.cycles = event->data.machine_state.cpu_cycles;
            debug_state->machine_cycle = event->data.machine_state.cycle;
            debug_state->vic_cycles = event->data.machine_state.vic_cycles;
            debug_state->cia_cycles = event->data.machine_state.cia_cycles;
            debug_state->stop_reason = event->data.machine_state.stop_reason;
            debug_state->active_turbo_multiplier = event->data.machine_state.active_turbo_multiplier;
            debug_state->frame_number = event->data.machine_state.frame_number;
            debug_state->frame_cycle = event->data.machine_state.frame_cycle;
            debug_state->dropped_frames = event->data.machine_state.dropped_frames;
            debug_state->memory_banking = event->data.machine_state.memory_banking;
            debug_state->vicii_hardware = event->data.machine_state.vicii_hardware;
            debug_state->cia1_hardware = event->data.machine_state.cia1_hardware;
            debug_state->cia2_hardware = event->data.machine_state.cia2_hardware;
            debug_state->sid_hardware = event->data.machine_state.sid_hardware;
            debug_state->screen_ram_writes = event->data.machine_state.screen_ram_writes;
            debug_state->color_ram_writes = event->data.machine_state.color_ram_writes;
            debug_state->vic_register_writes = event->data.machine_state.vic_register_writes;
            debug_state->cia1_register_writes = event->data.machine_state.cia1_register_writes;
            debug_state->cia2_register_writes = event->data.machine_state.cia2_register_writes;
            debug_state->sid_register_writes = event->data.machine_state.sid_register_writes;
            debug_state->keyboard_events = event->data.machine_state.keyboard_events;
            debug_state->irq_entries = event->data.machine_state.irq_entries;
            debug_state->cia1_icr_reads = event->data.machine_state.cia1_icr_reads;
            debug_state->cia1_icr_writes = event->data.machine_state.cia1_icr_writes;
            debug_state->cia1_interrupt_assertions = event->data.machine_state.cia1_interrupt_assertions;
            debug_state->nmi_entries = event->data.machine_state.nmi_entries;
            debug_state->restore_requests = event->data.machine_state.restore_requests;
            debug_state->has_cpu = true;
            debug_state->has_memory_banking = true;
            debug_state->has_hardware = true;
            if (debug_state->runtime_state != FRONTEND_RUNTIME_STATE_ERROR) {
                debug_state->runtime_state = event->data.machine_state.running ?
                    FRONTEND_RUNTIME_STATE_RUNNING :
                    FRONTEND_RUNTIME_STATE_PAUSED;
            }
            break;

        case RUNTIME_EVENT_MEMORY_RESPONSE:
            debug_state->memory = event->data.memory;
            debug_state->has_memory = true;
            break;

        case RUNTIME_EVENT_MEMORY_VIEW_RESPONSE:
            debug_state->memory_view = event->data.memory;
            debug_state->has_memory_view = true;
            break;

        case RUNTIME_EVENT_BREAKPOINTS_RESPONSE:
            debug_state->breakpoints = event->data.breakpoints;
            debug_state->has_breakpoints = true;
            break;

        case RUNTIME_EVENT_DISK_STATUS_RESPONSE:
            if (event->data.disk_status.device >= 8 && event->data.disk_status.device <= 9) {
                size_t index = (size_t)(event->data.disk_status.device - 8u);
                debug_state->disk_status[index] = event->data.disk_status;
                debug_state->has_disk_status[index] = true;
            }
            break;

        case RUNTIME_EVENT_FRAME_READY:
            debug_state->frame_number = event->data.frame_ready.frame_number;
            debug_state->frame_cycle = event->data.frame_ready.machine_cycle;
            debug_state->dropped_frames = event->data.frame_ready.dropped_frames;
            debug_state->has_frame = true;
            break;

        case RUNTIME_EVENT_CALL_STACK_RESPONSE:
            debug_state->call_stack = event->data.call_stack;
            debug_state->has_call_stack = true;
            break;

        case RUNTIME_EVENT_STARTED:
            if (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_UNKNOWN ||
                debug_state->runtime_state == FRONTEND_RUNTIME_STATE_ERROR) {
                debug_state->runtime_state = FRONTEND_RUNTIME_STATE_PAUSED;
            }
            break;

        case RUNTIME_EVENT_STOPPED:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN;
            break;

        case RUNTIME_EVENT_NONE:
        case RUNTIME_EVENT_PONG:
        default:
            break;
    }
}

static void poll_runtime_events(runtime_client *client, frontend *ui, frontend_debug_state *debug_state) {
    runtime_event event;
    c64_frame frame;
    bool consumed_frame = false;

    while (runtime_client_poll_event(client, &event)) {
        update_debug_state_from_event(debug_state, &event);
        if (event.type == RUNTIME_EVENT_ERROR) {
            SDL_Log("runtime error: %s", event.data.error.message);
        } else if (event.type == RUNTIME_EVENT_ASSEMBLE_ERROR) {
            if (ui != NULL) {
                frontend_show_assembler_errors(ui, event.data.error.message);
            }
        } else if (event.type == RUNTIME_EVENT_ASSEMBLE_COMPLETE) {
            if (ui != NULL) {
                runtime_symbol_snapshot symbols;
                if (runtime_client_poll_symbols(client, &symbols)) {
                    frontend_update_symbols(ui, &symbols);
                }
            }
        } else if (event.type == RUNTIME_EVENT_STEP_COMPLETE &&
                   debug_state != NULL &&
                   debug_state->has_cpu) {
            SDL_Log(
                "STEP instruction PC=%04X CYCLES=%llu",
                debug_state->cpu.pc,
                (unsigned long long)debug_state->cpu.cycles);
        }
    }

    if (ui != NULL) {
        runtime_symbol_snapshot symbols;
        if (runtime_client_poll_symbols(client, &symbols)) {
            frontend_update_symbols(ui, &symbols);
        }
    }

    while (ui != NULL && runtime_client_poll_frame(client, &frame)) {
        if (frontend_submit_frame(ui, &frame) && debug_state != NULL) {
            debug_state->frame_number = frame.frame_number;
            debug_state->frame_cycle = frame.machine_cycle;
            debug_state->has_frame = true;
            consumed_frame = true;
        }
    }

    if (consumed_frame && debug_state != NULL &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        request_debug_state(client);
    }
}

static void send_run_command(runtime_client *client) {
    SDL_Log("RUN command requested");
    if (runtime_client_run(client)) {
        request_debug_state(client);
    }
}

static void send_pause_command(runtime_client *client) {
    SDL_Log("PAUSE command requested");
    if (runtime_client_pause(client)) {
        request_debug_state(client);
    }
}

static void send_step_instruction_command(runtime_client *client) {
    SDL_Log("STEP instruction requested");
    if (runtime_client_step_instruction(client)) {
        request_debug_state(client);
    }
}

static void send_step_out_command(runtime_client *client) {
    SDL_Log("STEP OUT requested");
    if (runtime_client_step_out(client)) {
        request_debug_state(client);
    }
}

static void send_step_over_command(runtime_client *client) {
    SDL_Log("STEP OVER requested");
    if (runtime_client_step_over(client)) {
        request_debug_state(client);
    }
}

static void send_run_to_cursor_command(runtime_client *client, frontend *ui) {
    uint16_t addr;
    if (!frontend_get_disassembly_cursor(ui, &addr)) {
        return;
    }
    SDL_Log("RUN TO CURSOR requested: $%04X", addr);
    if (runtime_client_run_to_cursor(client, addr)) {
        request_debug_state(client);
    }
}

static void dispatch_input_actions(
    runtime_client *client,
    const frontend_input_action *actions,
    size_t count) {
    size_t i;

    if (client == NULL || actions == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        switch (actions[i].type) {
            case FRONTEND_INPUT_ACTION_KEY:
                runtime_client_keyboard_key(client, actions[i].key, actions[i].pressed);
                break;

            case FRONTEND_INPUT_ACTION_RESTORE:
                runtime_client_restore(client);
                break;

            case FRONTEND_INPUT_ACTION_NONE:
            default:
                break;
        }
    }
}

static void sdl_c64_controllers_reset(sdl_c64_controller_state *state) {
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->single_controller_port = 2u;
}

static size_t sdl_c64_controller_count(const sdl_c64_controller_state *state) {
    size_t i;
    size_t count = 0;

    if (state == NULL) {
        return 0;
    }

    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL) {
            count++;
        }
    }
    return count;
}

static int sdl_c64_controller_find_slot(
    const sdl_c64_controller_state *state,
    SDL_JoystickID instance_id) {
    size_t i;

    if (state == NULL) {
        return -1;
    }

    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL &&
            state->controllers[i].instance_id == instance_id) {
            return (int)i;
        }
    }
    return -1;
}

static unsigned sdl_c64_controller_slot_port(
    const sdl_c64_controller_state *state,
    size_t slot,
    size_t connected_count) {
    if (state == NULL || connected_count == 0) {
        return 0;
    }

    if (connected_count == 1) {
        return state->single_controller_port;
    }

    if (slot == 0) {
        return state->swapped ? 2u : 1u;
    }
    if (slot == 1) {
        return state->swapped ? 1u : 2u;
    }
    return 0;
}

static void sdl_c64_controller_send_ports(
    const sdl_c64_controller_state *state,
    runtime_client *client) {
    uint8_t ports[3] = {0, 0, 0};
    size_t connected_count;
    size_t i;

    if (state == NULL || client == NULL) {
        return;
    }

    connected_count = sdl_c64_controller_count(state);
    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        unsigned port;

        if (state->controllers[i].controller == NULL) {
            continue;
        }
        port = sdl_c64_controller_slot_port(state, i, connected_count);
        if (port >= 1u && port <= 2u) {
            ports[port] = state->controllers[i].inputs;
        }
    }

    runtime_client_set_joystick(client, 1u, ports[1]);
    runtime_client_set_joystick(client, 2u, ports[2]);
}

static uint8_t sdl_c64_controller_read_inputs(SDL_GameController *controller) {
    Sint16 x;
    Sint16 y;
    uint8_t inputs = 0;

    if (controller == NULL) {
        return 0;
    }

    x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

    if (x <= -C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        inputs |= C64_JOYSTICK_LEFT;
    }
    if (x >= C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        inputs |= C64_JOYSTICK_RIGHT;
    }
    if (y <= -C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        inputs |= C64_JOYSTICK_UP;
    }
    if (y >= C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        inputs |= C64_JOYSTICK_DOWN;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) {
        inputs |= C64_JOYSTICK_FIRE;
    }

    return inputs;
}

static void sdl_c64_controller_refresh_slot(
    sdl_c64_controller_state *state,
    size_t slot,
    runtime_client *client) {
    uint8_t inputs;

    if (state == NULL || slot >= C64M_CONTROLLER_MAX ||
        state->controllers[slot].controller == NULL) {
        return;
    }

    inputs = sdl_c64_controller_read_inputs(state->controllers[slot].controller);
    if (inputs != state->controllers[slot].inputs) {
        state->controllers[slot].inputs = inputs;
        sdl_c64_controller_send_ports(state, client);
    }
}

static void sdl_c64_controller_add(
    sdl_c64_controller_state *state,
    runtime_client *client,
    int device_index) {
    SDL_GameController *controller;
    SDL_Joystick *joystick;
    SDL_JoystickID instance_id;
    size_t slot;

    if (state == NULL || !SDL_IsGameController(device_index)) {
        return;
    }

    for (slot = 0; slot < C64M_CONTROLLER_MAX; slot++) {
        if (state->controllers[slot].controller == NULL) {
            break;
        }
    }
    if (slot >= C64M_CONTROLLER_MAX) {
        SDL_Log("ignoring extra controller: %s", SDL_GameControllerNameForIndex(device_index));
        return;
    }

    controller = SDL_GameControllerOpen(device_index);
    if (controller == NULL) {
        SDL_Log("SDL_GameControllerOpen failed: %s", SDL_GetError());
        return;
    }

    joystick = SDL_GameControllerGetJoystick(controller);
    instance_id = joystick != NULL ? SDL_JoystickInstanceID(joystick) : -1;
    if (instance_id < 0) {
        SDL_Log("SDL_JoystickInstanceID failed: %s", SDL_GetError());
        SDL_GameControllerClose(controller);
        return;
    }
    if (sdl_c64_controller_find_slot(state, instance_id) >= 0) {
        SDL_GameControllerClose(controller);
        return;
    }

    state->controllers[slot].controller = controller;
    state->controllers[slot].instance_id = instance_id;
    state->controllers[slot].inputs = sdl_c64_controller_read_inputs(controller);
    SDL_Log("controller connected: %s", SDL_GameControllerName(controller));
    sdl_c64_controller_send_ports(state, client);
}

static void sdl_c64_controller_remove(
    sdl_c64_controller_state *state,
    runtime_client *client,
    SDL_JoystickID instance_id) {
    int slot;

    slot = sdl_c64_controller_find_slot(state, instance_id);
    if (slot < 0) {
        return;
    }

    SDL_Log("controller disconnected: %s", SDL_GameControllerName(state->controllers[slot].controller));
    SDL_GameControllerClose(state->controllers[slot].controller);
    memset(&state->controllers[slot], 0, sizeof(state->controllers[slot]));
    sdl_c64_controller_send_ports(state, client);
}

static void sdl_c64_controller_handle_event(
    sdl_c64_controller_state *state,
    runtime_client *client,
    const SDL_Event *event) {
    int slot;

    if (state == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
        case SDL_CONTROLLERDEVICEADDED:
            sdl_c64_controller_add(state, client, event->cdevice.which);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            sdl_c64_controller_remove(state, client, event->cdevice.which);
            break;

        case SDL_CONTROLLERAXISMOTION:
            if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                slot = sdl_c64_controller_find_slot(state, event->caxis.which);
                if (slot >= 0) {
                    sdl_c64_controller_refresh_slot(state, (size_t)slot, client);
                }
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            switch (event->cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    slot = sdl_c64_controller_find_slot(state, event->cbutton.which);
                    if (slot >= 0) {
                        sdl_c64_controller_refresh_slot(state, (size_t)slot, client);
                    }
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void sdl_c64_controller_switch_mapping(
    sdl_c64_controller_state *state,
    runtime_client *client,
    unsigned port) {
    size_t connected_count;

    if (state == NULL || (port != 1u && port != 2u)) {
        return;
    }

    connected_count = sdl_c64_controller_count(state);
    if (connected_count >= 2) {
        state->swapped = !state->swapped;
        SDL_Log("controller ports swapped");
    } else {
        state->single_controller_port = port;
        SDL_Log("single controller mapped to C64 joystick port %u", port);
    }
    sdl_c64_controller_send_ports(state, client);
}

static void sdl_c64_controllers_open_existing(
    sdl_c64_controller_state *state,
    runtime_client *client) {
    int i;
    int count;

    count = SDL_NumJoysticks();
    for (i = 0; i < count; i++) {
        sdl_c64_controller_add(state, client, i);
    }
}

static void sdl_c64_controllers_close(sdl_c64_controller_state *state, runtime_client *client) {
    size_t i;

    if (state == NULL) {
        return;
    }

    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL) {
            SDL_GameControllerClose(state->controllers[i].controller);
            memset(&state->controllers[i], 0, sizeof(state->controllers[i]));
        }
    }
    sdl_c64_controller_send_ports(state, client);
}

static void dispatch_debugger_intents(runtime_client *client, frontend *ui, app_options *options) {
    frontend_debugger_intent intent;

    if (client == NULL || ui == NULL) {
        return;
    }

    while (frontend_poll_debugger_intent(ui, &intent)) {
        bool sent = false;

        switch (intent.type) {
            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC:
                sent = runtime_client_set_pc(client, intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP:
                sent = runtime_client_set_sp(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A:
                sent = runtime_client_set_a(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X:
                sent = runtime_client_set_x(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y:
                sent = runtime_client_set_y(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS:
                sent = runtime_client_set_status(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY:
                sent = runtime_client_request_memory(
                    client,
                    intent.address,
                    intent.length,
                    intent.memory_mode);
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW:
                sent = runtime_client_request_memory_view(
                    client,
                    intent.address,
                    intent.length,
                    intent.memory_mode);
                break;

            case FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE:
                sent = runtime_client_write_memory_byte(
                    client,
                    intent.address,
                    (uint8_t)intent.value,
                    intent.memory_mode);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE:
                sent = runtime_client_set_execute_breakpoint(client, intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR:
                sent = runtime_client_clear_breakpoint(client, intent.id);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL:
                sent = runtime_client_clear_all_breakpoints(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED:
                sent = runtime_client_set_breakpoint_enabled(client, intent.id, intent.enabled);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE:
                sent = runtime_client_create_breakpoint(client, &intent.breakpoint);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE:
                sent = runtime_client_update_breakpoint(client, intent.id, &intent.breakpoint);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_REQUEST_SNAPSHOT:
                sent = runtime_client_request_breakpoints(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_PROGRAM_LOAD_PRG_DIALOG:
                {
                    char path[1024];
                    if (choose_prg_path(path, sizeof(path))) {
                        sent = runtime_client_load_prg(client, path);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
                {
                    char path[1024];
                    if ((intent.disk_device == 8 || intent.disk_device == 9) &&
                        choose_disk_path(path, sizeof(path))) {
                        sent = runtime_client_mount_d64(client, intent.disk_device, path);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    sent = runtime_client_unmount_disk(client, intent.disk_device);
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_MACHINE_RESET:
                sent = runtime_client_reset(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG:
                {
                    char path[1024];
                    app_options selected;
                    if (choose_ini_path(path, sizeof(path)) && app_options_copy(&selected, options)) {
                        app_options_set_string(&selected.ini_path, path);
                        frontend_apply_selected_ini(ui, &selected);
                        app_options_destroy(&selected);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG:
                {
                    char path[1024];
                    if (choose_symbol_path(path, sizeof(path))) {
                        frontend_append_symbol_file(ui, path);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY:
                {
                    c64_config machine_config = machine_config_from_options(&intent.config);
                    runtime_config runtime_options = runtime_config_from_options(&intent.config);
                    app_options_destroy(options);
                    *options = intent.config;
                    memset(&intent.config, 0, sizeof(intent.config));
                    options->save_ini = intent.config_result.save_ini_on_quit || options->remember;
                    sent = runtime_client_apply_machine_config(
                        client,
                        &machine_config,
                        &runtime_options,
                        options->ini_path,
                        options->symbol_files,
                        intent.config_result.needs_reboot,
                        options->save_ini && !options->no_save_ini);
                    frontend_set_config_state(ui, options);
                    if (intent.config_result.symbols_changed) {
                        runtime_client_request_memory(client, 0, 1, RUNTIME_MEMORY_MODE_CPU_MAP);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE:
                {
                    char path[1024];
                    if (choose_file_path(path, sizeof(path), "Select Assembler Source", NULL)) {
                        frontend_set_assembler_path(ui, path);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN:
                sent = runtime_client_assemble_file_full(
                    client,
                    intent.assemble_path,
                    intent.assemble_address,
                    intent.assemble_run_address,
                    intent.assemble_auto_run,
                    intent.assemble_reset_first);
                break;

            case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE:
                {
                    char abs_path[1024];
                    char rel_path[1024];
                    if (choose_file_path(abs_path, sizeof(abs_path), "Select Binary File", NULL)) {
                        make_relative_path(abs_path, rel_path, sizeof(rel_path));
                        frontend_set_load_bin_path(ui, rel_path);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE:
                sent = runtime_client_load_bin(
                    client,
                    intent.load_bin_path,
                    intent.load_bin_address,
                    intent.load_bin_use_file_address,
                    intent.load_bin_reset_first,
                    intent.load_bin_is_basic);
                break;

            case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE:
                {
                    char abs_path[1024];
                    char rel_path[1024];
                    if (choose_save_path(abs_path, sizeof(abs_path), "Save File")) {
                        make_relative_path(abs_path, rel_path, sizeof(rel_path));
                        frontend_set_save_bin_path(ui, rel_path);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE:
                sent = runtime_client_save_bin(
                    client,
                    intent.save_bin_path,
                    intent.save_bin_start,
                    intent.save_bin_end,
                    intent.save_bin_write_file_address,
                    intent.save_bin_is_basic);
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK:
                sent = runtime_client_request_call_stack(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_NONE:
            default:
                break;
        }

        if (sent) {
            request_debug_state(client);
        }
        app_options_destroy(&intent.config);
    }
}

static void handle_keyboard_input(
    frontend_input_mapper *mapper,
    runtime_client *client,
    const SDL_KeyboardEvent *event) {
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    size_t count;

    count = frontend_input_map_keyboard_event(mapper, event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    dispatch_input_actions(client, actions, count);
}

static bool run_main_loop(platform_window *window, runtime_client *client, frontend *ui, app_options *options) {
    bool running = true;
    bool ui_visible = false;
    frontend_input_mapper input_mapper;
    sdl_c64_controller_state controller_state;
    frontend_debug_state debug_state = {
        .runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN,
    };

    frontend_input_mapper_reset(&input_mapper);
    sdl_c64_controllers_reset(&controller_state);
    sdl_c64_controllers_open_existing(&controller_state, client);
    request_debug_state(client);
    runtime_client_request_frame(client);

    while (running) {
        SDL_Event event;

        frontend_begin_input(ui);
        while (SDL_PollEvent(&event)) {
            bool send_event_to_frontend = ui_visible;

            sdl_c64_controller_handle_event(&controller_state, client, &event);

            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (frontend_input_is_host_quit_shortcut(&event.key)) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F9) {
                    ui_visible = !ui_visible;
                    SDL_Log("ui_visible=%s", ui_visible ? "true" : "false");
                } else if (event.key.keysym.sym == SDLK_F10 &&
                           !frontend_input_has_shift_modifier(&event.key)) {
                    /* F10: pause if running, step if paused */
                    if (debug_state.runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
                        send_pause_command(client);
                    } else {
                        send_step_instruction_command(client);
                    }
                } else if (event.key.keysym.sym == SDLK_F10 &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    send_step_out_command(client);
                } else if (event.key.keysym.sym == SDLK_F11 ||
                           (event.key.keysym.sym == SDLK_s &&
                            frontend_input_has_option_modifier(&event.key))) {
                    send_step_over_command(client);
                } else if (event.key.keysym.sym == SDLK_F12 &&
                           !frontend_input_has_shift_modifier(&event.key)) {
                    send_run_command(client);
                } else if (event.key.keysym.sym == SDLK_F12 &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    send_run_to_cursor_command(client, ui);
                } else if (event.key.keysym.sym == SDLK_t &&
                           frontend_input_has_option_modifier(&event.key)) {
                    runtime_client_cycle_turbo_speed(client);
                } else if ((event.key.keysym.sym == SDLK_1 || event.key.keysym.sym == SDLK_2) &&
                           frontend_input_has_option_modifier(&event.key)) {
                    sdl_c64_controller_switch_mapping(
                        &controller_state,
                        client,
                        event.key.keysym.sym == SDLK_1 ? 1u : 2u);
                } else if (event.key.keysym.sym == SDLK_INSERT &&
                           frontend_input_has_option_modifier(&event.key) &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    char *text = SDL_GetClipboardText();
                    if (text && text[0] != '\0') {
                        runtime_client_paste_text(client, text, strlen(text));
                    }
                    SDL_free(text);
                } else if (event.key.keysym.sym == SDLK_INSERT &&
                           frontend_input_has_option_modifier(&event.key) &&
                           !frontend_input_has_shift_modifier(&event.key)) {
                    char *text = SDL_GetClipboardText();
                    if (text && text[0] != '\0') {
                        runtime_client_paste_text_buffer(client, text, strlen(text));
                    }
                    SDL_free(text);
                } else if (!ui_visible || frontend_routes_keyboard_to_c64(ui)) {
                    handle_keyboard_input(&input_mapper, client, &event.key);
                    send_event_to_frontend = false;
                }
            } else if (event.type == SDL_KEYUP &&
                       (!ui_visible || frontend_routes_keyboard_to_c64(ui))) {
                handle_keyboard_input(&input_mapper, client, &event.key);
                send_event_to_frontend = false;
            }

            if (send_event_to_frontend) {
                frontend_handle_event(ui, &event);
            }
        }
        frontend_end_input(ui);

        poll_runtime_events(client, ui, &debug_state);

        if (!platform_window_clear(window)) {
            return false;
        }
        frontend_render(ui, ui_visible, &debug_state);
        dispatch_debugger_intents(client, ui, options);
        platform_window_present(window);
    }
    sdl_c64_controllers_close(&controller_state, client);

    return true;
}

int main(int argc, char **argv) {
    app_options options;
    runtime *rt = NULL;
    runtime_config runtime_cfg;
    runtime_client *client = NULL;
    frontend *ui = NULL;
    platform_window *window;
    platform_window_config window_config;
    frontend_layout_state layout_state;
    audio_buffer *abuf = NULL;
    platform_audio *paudio = NULL;
    int exit_code = 0;

    if (!app_options_load_startup(&options, argc, argv)) {
        return 1;
    }

    /* Create the shared audio buffer and open the SDL audio device before
       starting the runtime thread so the actual sample rate is known at
       runtime_create time.  platform_audio_init initialises SDL_INIT_AUDIO
       internally, so this may safely precede platform_init(). */
    abuf = audio_buffer_create(8192);
    if (abuf != NULL) {
        platform_audio_desc audio_desc;
        audio_desc.requested_rate             = 48000;
        audio_desc.requested_channels         = 2;
        audio_desc.requested_callback_samples = 512;
        audio_desc.buffer                     = abuf;
        paudio = platform_audio_create(&audio_desc);
        if (paudio == NULL) {
            SDL_Log("audio: failed to open device, running without audio");
            audio_buffer_destroy(abuf);
            abuf = NULL;
        }
    } else {
        SDL_Log("audio: failed to allocate buffer, running without audio");
    }

    if (!runtime_init()) {
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    runtime_cfg.basic_rom_path = options.basic_rom_path;
    runtime_cfg.char_rom_path = options.char_rom_path;
    runtime_cfg.kernal_rom_path = options.kernal_rom_path;
    runtime_cfg.system_rom_path = options.system_rom_path;
    runtime_cfg.ini_path = options.ini_path;
    runtime_cfg.symbol_files = options.symbol_files;
    runtime_cfg.use_ini = options.use_ini;
    runtime_cfg.save_ini = (options.save_ini || options.remember) && !options.no_save_ini;
    runtime_cfg.machine_config = machine_config_from_options(&options);
    runtime_cfg.audio_out         = abuf;
    runtime_cfg.audio_sample_rate = platform_audio_actual_rate(paudio);
    runtime_cfg.audio_smoke       = options.audio_smoke ? 1 : 0;
    {
        runtime_config turbo_cfg = runtime_config_from_options(&options);
        memcpy(runtime_cfg.turbo_speeds, turbo_cfg.turbo_speeds, sizeof(runtime_cfg.turbo_speeds));
        runtime_cfg.turbo_speed_count = turbo_cfg.turbo_speed_count;
        runtime_cfg.active_turbo_multiplier = turbo_cfg.active_turbo_multiplier;
    }

    rt = runtime_create(&runtime_cfg);
    if (rt == NULL) {
        runtime_shutdown();
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    if (!runtime_start(rt)) {
        runtime_destroy(rt);
        runtime_shutdown();
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }
    client = runtime_get_client(rt);

    /* Start audio playback now that the runtime thread is producing samples. */
    platform_audio_start(paudio);

    if (!platform_init()) {
        runtime_destroy(rt);
        runtime_shutdown();
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    window_config.display_width = options.display_width;
    window_config.display_height = options.display_height;
    window_config.window_width = options.window_width;
    window_config.window_height = options.window_height;

    window = platform_window_create(&window_config);
    if (window == NULL) {
        platform_audio_destroy(paudio);
        platform_shutdown();
        runtime_destroy(rt);
        runtime_shutdown();
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    ui = frontend_create(window);
    if (ui == NULL) {
        platform_window_destroy(window);
        platform_audio_destroy(paudio);
        platform_shutdown();
        runtime_destroy(rt);
        runtime_shutdown();
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    layout_state.split_display_right = options.layout_split_display_right;
    layout_state.split_top_bottom = options.layout_split_top_bottom;
    layout_state.split_memory_misc = options.layout_split_memory_misc;
    layout_state.display_width = options.layout_display_width;
    layout_state.display_height = options.layout_display_height;
    frontend_set_layout_state(ui, &layout_state);
    frontend_set_config_state(ui, &options);

    send_run_command(client);

    if (!run_main_loop(window, client, ui, &options)) {
        exit_code = 1;
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    poll_runtime_events(client, NULL, NULL);
    if (!runtime_save_debug_ini(rt)) {
        SDL_Log("failed to save debug ini data: %s", options.ini_path ? options.ini_path : "(null)");
    }

    platform_window_get_size(window, &options.window_width, &options.window_height);
    frontend_get_layout_state(ui, &layout_state);
    options.layout_split_display_right = layout_state.split_display_right;
    options.layout_split_top_bottom = layout_state.split_top_bottom;
    options.layout_split_memory_misc = layout_state.split_memory_misc;
    options.layout_display_width = layout_state.display_width;
    options.layout_display_height = layout_state.display_height;
    if ((options.save_ini || options.remember) && !app_options_save_shutdown(&options)) {
        SDL_Log("failed to save ini file: %s", options.ini_path ? options.ini_path : "(null)");
    }

    frontend_destroy(ui);
    platform_window_destroy(window);
    /* Stop and destroy the audio device before SDL_Quit so the device handle
       remains valid.  Runtime thread is already joined at this point. */
    platform_audio_destroy(paudio);
    platform_shutdown();
    runtime_destroy(rt);
    runtime_shutdown();
    audio_buffer_destroy(abuf);
    app_options_destroy(&options);
    return exit_code;
}
