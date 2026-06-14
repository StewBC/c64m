#include "app_options.h"
#include "frontend.h"
#include "platform.h"
#include "runtime.h"
#include "runtime_client.h"

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdbool.h>

static bool has_ctrl_modifier(const SDL_KeyboardEvent *key) {
    return key != NULL && (key->keysym.mod & KMOD_CTRL) != 0;
}

static bool is_host_quit_shortcut(const SDL_KeyboardEvent *key) {
    SDL_Keymod modifiers;

    if (key == NULL || key->keysym.sym != SDLK_q) {
        return false;
    }

    modifiers = key->keysym.mod;

#if defined(__APPLE__)
    return (modifiers & KMOD_GUI) != 0;
#else
    return (modifiers & KMOD_CTRL) != 0;
#endif
}

static void request_debug_state(runtime_client *client) {
    runtime_client_request_cpu_state(client);
    runtime_client_request_machine_state(client);
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
            debug_state->has_cpu = true;
            if (debug_state->runtime_state != FRONTEND_RUNTIME_STATE_ERROR) {
                debug_state->runtime_state = event->data.machine_state.running ?
                    FRONTEND_RUNTIME_STATE_RUNNING :
                    FRONTEND_RUNTIME_STATE_PAUSED;
            }
            break;

        case RUNTIME_EVENT_STARTED:
            if (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_UNKNOWN) {
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

static void poll_runtime_events(runtime_client *client, frontend_debug_state *debug_state) {
    runtime_event event;

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

static bool run_main_loop(platform_window *window, runtime_client *client, frontend *ui) {
    bool running = true;
    bool ui_visible = false;
    frontend_debug_state debug_state = {
        .runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN,
    };

    request_debug_state(client);

    while (running) {
        SDL_Event event;

        frontend_begin_input(ui);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (is_host_quit_shortcut(&event.key)) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_F9) {
                    ui_visible = !ui_visible;
                    SDL_Log("ui_visible=%s", ui_visible ? "true" : "false");
                } else if (event.key.keysym.sym == SDLK_F10) {
                    send_run_command(client);
                } else if (event.key.keysym.sym == SDLK_F11 ||
                           (event.key.keysym.sym == SDLK_s && has_ctrl_modifier(&event.key))) {
                    send_step_instruction_command(client);
                } else if (event.key.keysym.sym == SDLK_F12 ||
                           (event.key.keysym.sym == SDLK_c && has_ctrl_modifier(&event.key))) {
                    send_pause_command(client);
                }
            }

            if (ui_visible) {
                frontend_handle_event(ui, &event);
            }
        }
        frontend_end_input(ui);

        poll_runtime_events(client, &debug_state);

        if (!platform_window_clear(window)) {
            return false;
        }
        frontend_render(ui, ui_visible, &debug_state);
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

    if (!run_main_loop(window, client, ui)) {
        exit_code = 1;
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    poll_runtime_events(client, NULL);

    frontend_destroy(ui);
    platform_window_destroy(window);
    platform_shutdown();
    runtime_destroy(rt);
    runtime_shutdown();
    app_options_destroy(&options);
    return exit_code;
}
