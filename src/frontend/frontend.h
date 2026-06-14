#pragma once

#include "platform.h"
#include "runtime_event.h"

#include "c64_frame.h"

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct frontend frontend;

typedef enum frontend_runtime_state {
    FRONTEND_RUNTIME_STATE_UNKNOWN = 0,
    FRONTEND_RUNTIME_STATE_RUNNING,
    FRONTEND_RUNTIME_STATE_PAUSED,
    FRONTEND_RUNTIME_STATE_ERROR
} frontend_runtime_state;

typedef struct frontend_debug_state {
    frontend_runtime_state runtime_state;
    runtime_cpu_snapshot cpu;
    uint64_t frame_number;
    uint64_t frame_cycle;
    uint64_t dropped_frames;
    bool has_frame;
    bool has_cpu;
} frontend_debug_state;

frontend *frontend_create(platform_window *window);
void frontend_destroy(frontend *ui);

void frontend_begin_input(frontend *ui);
void frontend_handle_event(frontend *ui, SDL_Event *event);
void frontend_end_input(frontend *ui);
bool frontend_submit_frame(frontend *ui, const c64_frame *frame);
void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state);
