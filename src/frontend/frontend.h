#pragma once

#include "platform.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct frontend frontend;

frontend *frontend_create(platform_window *window);
void frontend_destroy(frontend *ui);

void frontend_begin_input(frontend *ui);
void frontend_handle_event(frontend *ui, SDL_Event *event);
void frontend_end_input(frontend *ui);
void frontend_render(frontend *ui, bool ui_visible);
