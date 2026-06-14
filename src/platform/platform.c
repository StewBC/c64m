#include "platform.h"

#include <SDL2/SDL.h>

struct platform_window {
    SDL_Window *window;
    SDL_Renderer *renderer;
};

bool platform_init(void)
{
    if (SDL_Init(
        SDL_INIT_VIDEO |
        SDL_INIT_AUDIO |
        SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void platform_shutdown(void)
{
    SDL_Quit();
}

platform_window *platform_window_create(const platform_window_config *config)
{
    platform_window *platform;
    int display_width;
    int display_height;
    int window_width;
    int window_height;

    if (config == NULL) {
        return NULL;
    }

    display_width = config->display_width > 0 ? config->display_width : 384;
    display_height = config->display_height > 0 ? config->display_height : 272;
    window_width = display_width * 2;
    window_height = display_height * 2;

    platform = (platform_window *)SDL_calloc(1, sizeof(*platform));
    if (platform == NULL) {
        SDL_Log("platform window allocation failed");
        return NULL;
    }

    platform->window = SDL_CreateWindow(
        "c64m",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        window_width,
        window_height,
        SDL_WINDOW_RESIZABLE);
    if (platform->window == NULL) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        platform_window_destroy(platform);
        return NULL;
    }

    platform->renderer = SDL_CreateRenderer(
        platform->window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (platform->renderer == NULL) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        platform_window_destroy(platform);
        return NULL;
    }

    return platform;
}

void platform_window_destroy(platform_window *window)
{
    if (window == NULL) {
        return;
    }

    if (window->renderer != NULL) {
        SDL_DestroyRenderer(window->renderer);
    }
    if (window->window != NULL) {
        SDL_DestroyWindow(window->window);
    }

    SDL_free(window);
}

bool platform_window_clear(platform_window *window)
{
    if (window == NULL || window->renderer == NULL) {
        return false;
    }

    if (SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, 255) != 0) {
        SDL_Log("SDL_SetRenderDrawColor failed: %s", SDL_GetError());
        return false;
    }

    if (SDL_RenderClear(window->renderer) != 0) {
        SDL_Log("SDL_RenderClear failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void platform_window_present(platform_window *window)
{
    if (window == NULL || window->renderer == NULL) {
        return;
    }

    SDL_RenderPresent(window->renderer);
}

void platform_window_get_size(platform_window *window, int *out_width, int *out_height)
{
    int width = 0;
    int height = 0;

    if (window != NULL && window->window != NULL) {
        SDL_GetWindowSize(window->window, &width, &height);
    }

    if (out_width != NULL) {
        *out_width = width;
    }
    if (out_height != NULL) {
        *out_height = height;
    }
}

SDL_Window *platform_window_get_sdl_window(platform_window *window)
{
    if (window == NULL) {
        return NULL;
    }

    return window->window;
}

SDL_Renderer *platform_window_get_sdl_renderer(platform_window *window)
{
    if (window == NULL) {
        return NULL;
    }

    return window->renderer;
}
