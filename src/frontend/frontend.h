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
    runtime_memory_snapshot memory;
    runtime_breakpoint_snapshot breakpoints;
    uint64_t frame_number;
    uint64_t frame_cycle;
    uint64_t dropped_frames;
    uint64_t machine_cycle;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
    runtime_stop_reason stop_reason;
    bool has_frame;
    bool has_cpu;
    bool has_memory;
    bool has_breakpoints;
} frontend_debug_state;

typedef enum frontend_debugger_intent_type {
    FRONTEND_DEBUGGER_INTENT_NONE = 0,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS,
    FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY,
    FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_REQUEST_SNAPSHOT,
    FRONTEND_DEBUGGER_INTENT_PROGRAM_LOAD_PRG_DIALOG
} frontend_debugger_intent_type;

typedef struct frontend_debugger_intent {
    frontend_debugger_intent_type type;
    uint16_t address;
    uint16_t length;
    uint16_t value;
    uint32_t id;
    bool enabled;
    runtime_memory_mode memory_mode;
} frontend_debugger_intent;

typedef struct frontend_layout_state {
    float split_display_right;
    float split_top_bottom;
    float split_memory_misc;
    int display_width;
    int display_height;
} frontend_layout_state;

frontend *frontend_create(platform_window *window);
void frontend_destroy(frontend *ui);

void frontend_begin_input(frontend *ui);
void frontend_handle_event(frontend *ui, SDL_Event *event);
void frontend_end_input(frontend *ui);
bool frontend_submit_frame(frontend *ui, const c64_frame *frame);
void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state);
bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent);
void frontend_set_layout_state(frontend *ui, const frontend_layout_state *state);
void frontend_get_layout_state(frontend *ui, frontend_layout_state *out_state);
