#include "app_options.h"
#include "frontend.h"
#include "frontend_input.h"
#include "platform.h"
#include "runtime.h"
#include "runtime_client.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool choose_prg_path(char *out_path, size_t out_size) {
#if defined(__APPLE__)
    FILE *pipe;
    char *newline;

    if (out_path == NULL || out_size == 0) {
        return false;
    }

    out_path[0] = '\0';
    pipe = popen(
        "osascript -e 'POSIX path of (choose file with prompt \"Load PRG\" of type {\"prg\"})'",
        "r");
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
    return false;
#endif
}

static void request_debug_state(runtime_client *client) {
    runtime_client_request_cpu_state(client);
    runtime_client_request_machine_state(client);
    runtime_client_request_breakpoints(client);
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
            debug_state->frame_number = event->data.machine_state.frame_number;
            debug_state->frame_cycle = event->data.machine_state.frame_cycle;
            debug_state->dropped_frames = event->data.machine_state.dropped_frames;
            debug_state->has_cpu = true;
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

        case RUNTIME_EVENT_BREAKPOINTS_RESPONSE:
            debug_state->breakpoints = event->data.breakpoints;
            debug_state->has_breakpoints = true;
            break;

        case RUNTIME_EVENT_FRAME_READY:
            debug_state->frame_number = event->data.frame_ready.frame_number;
            debug_state->frame_cycle = event->data.frame_ready.machine_cycle;
            debug_state->dropped_frames = event->data.frame_ready.dropped_frames;
            debug_state->has_frame = true;
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
        } else if (event.type == RUNTIME_EVENT_STEP_COMPLETE &&
                   debug_state != NULL &&
                   debug_state->has_cpu) {
            SDL_Log(
                "STEP instruction PC=%04X CYCLES=%llu",
                debug_state->cpu.pc,
                (unsigned long long)debug_state->cpu.cycles);
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

static void dispatch_debugger_intents(runtime_client *client, frontend *ui) {
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

            case FRONTEND_DEBUGGER_INTENT_NONE:
            default:
                break;
        }

        if (sent) {
            request_debug_state(client);
        }
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

static bool run_main_loop(platform_window *window, runtime_client *client, frontend *ui) {
    bool running = true;
    bool ui_visible = false;
    frontend_input_mapper input_mapper;
    frontend_debug_state debug_state = {
        .runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN,
    };

    frontend_input_mapper_reset(&input_mapper);
    request_debug_state(client);
    runtime_client_request_frame(client);

    while (running) {
        SDL_Event event;

        frontend_begin_input(ui);
        while (SDL_PollEvent(&event)) {
            bool send_event_to_frontend = ui_visible;

            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (frontend_input_is_host_quit_shortcut(&event.key)) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F9) {
                    ui_visible = !ui_visible;
                    SDL_Log("ui_visible=%s", ui_visible ? "true" : "false");
                } else if (event.key.keysym.sym == SDLK_F10 ||
                           (event.key.keysym.sym == SDLK_r &&
                            frontend_input_has_option_modifier(&event.key))) {
                    send_run_command(client);
                } else if (event.key.keysym.sym == SDLK_F11 ||
                           (event.key.keysym.sym == SDLK_s &&
                            frontend_input_has_option_modifier(&event.key))) {
                    send_step_instruction_command(client);
                } else if (event.key.keysym.sym == SDLK_F12 ||
                           (event.key.keysym.sym == SDLK_p &&
                            frontend_input_has_option_modifier(&event.key))) {
                    send_pause_command(client);
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
        dispatch_debugger_intents(client, ui);
        platform_window_present(window);
    }

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
    int exit_code = 0;

    if (!app_options_load_startup(&options, argc, argv)) {
        return 1;
    }

    if (!runtime_init()) {
        app_options_destroy(&options);
        return 1;
    }

    runtime_cfg.basic_rom_path = options.basic_rom_path;
    runtime_cfg.char_rom_path = options.char_rom_path;
    runtime_cfg.kernal_rom_path = options.kernal_rom_path;
    runtime_cfg.system_rom_path = options.system_rom_path;
    runtime_cfg.ini_path = options.ini_path;
    runtime_cfg.use_ini = options.use_ini;
    runtime_cfg.save_ini = options.save_ini && !options.no_save_ini;

    rt = runtime_create(&runtime_cfg);
    if (rt == NULL) {
        runtime_shutdown();
        app_options_destroy(&options);
        return 1;
    }

    if (!runtime_start(rt)) {
        runtime_destroy(rt);
        runtime_shutdown();
        app_options_destroy(&options);
        return 1;
    }
    client = runtime_get_client(rt);

    if (!platform_init()) {
        runtime_destroy(rt);
        runtime_shutdown();
        app_options_destroy(&options);
        return 1;
    }

    window_config.display_width = options.display_width;
    window_config.display_height = options.display_height;
    window_config.window_width = options.window_width;
    window_config.window_height = options.window_height;

    window = platform_window_create(&window_config);
    if (window == NULL) {
        platform_shutdown();
        runtime_destroy(rt);
        runtime_shutdown();
        app_options_destroy(&options);
        return 1;
    }

    ui = frontend_create(window);
    if (ui == NULL) {
        platform_window_destroy(window);
        platform_shutdown();
        runtime_destroy(rt);
        runtime_shutdown();
        app_options_destroy(&options);
        return 1;
    }

    layout_state.split_display_right = options.layout_split_display_right;
    layout_state.split_top_bottom = options.layout_split_top_bottom;
    layout_state.split_memory_misc = options.layout_split_memory_misc;
    layout_state.display_width = options.layout_display_width;
    layout_state.display_height = options.layout_display_height;
    frontend_set_layout_state(ui, &layout_state);

    send_run_command(client);

    if (!run_main_loop(window, client, ui)) {
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
    if (!app_options_save_shutdown(&options)) {
        SDL_Log("failed to save ini file: %s", options.ini_path ? options.ini_path : "(null)");
    }

    frontend_destroy(ui);
    platform_window_destroy(window);
    platform_shutdown();
    runtime_destroy(rt);
    runtime_shutdown();
    app_options_destroy(&options);
    return exit_code;
}
