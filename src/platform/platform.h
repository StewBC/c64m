#pragma once

#include <stdbool.h>

typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Window SDL_Window;
typedef struct platform_window platform_window;

typedef struct platform_window_config {
    int display_width;
    int display_height;
} platform_window_config;

bool platform_init(void);
void platform_shutdown(void);

platform_window *platform_window_create(const platform_window_config *config);
void platform_window_destroy(platform_window *window);

bool platform_window_clear(platform_window *window);
void platform_window_present(platform_window *window);
void platform_window_get_size(platform_window *window, int *out_width, int *out_height);
SDL_Window *platform_window_get_sdl_window(platform_window *window);
SDL_Renderer *platform_window_get_sdl_renderer(platform_window *window);
