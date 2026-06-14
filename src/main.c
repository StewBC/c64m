#include "app_options.h"
#include "frontend.h"
#include "platform.h"
#include "runtime.h"
#include "runtime_client.h"

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdbool.h>

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

static void poll_runtime_events(runtime_client *client) {
    runtime_event event;

    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            SDL_Log("runtime error: %s", event.data.error.message);
        }
    }
}

static bool run_main_loop(platform_window *window, runtime_client *client, frontend *ui) {
    bool running = true;
    bool ui_visible = false;
    bool turbo_enabled = false;

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
                    turbo_enabled = !turbo_enabled;
                    SDL_Log("turbo_enabled=%s", turbo_enabled ? "true" : "false");
                }
            }

            if (ui_visible) {
                frontend_handle_event(ui, &event);
            }
        }
        frontend_end_input(ui);

        poll_runtime_events(client);

        if (!platform_window_clear(window)) {
            return false;
        }
        frontend_render(ui, ui_visible);
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
    poll_runtime_events(client);

    frontend_destroy(ui);
    platform_window_destroy(window);
    platform_shutdown();
    runtime_destroy(rt);
    runtime_shutdown();
    app_options_destroy(&options);
    return exit_code;
}
