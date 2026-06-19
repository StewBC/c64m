#pragma once

#include "platform.h"
#include "runtime_event.h"
#include "runtime_client.h"
#include "app_options.h"

#include "c64_frame.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

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
    runtime_memory_snapshot memory_view;
    runtime_breakpoint_snapshot breakpoints;
    runtime_disk_status_snapshot disk_status[2];
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
    bool has_memory_view;
    bool has_breakpoints;
    bool has_disk_status[2];
} frontend_debug_state;

typedef struct frontend_assembler_state {
    bool initialized;
    char file_path[1024];
    char address_buf[8];
    char run_address_buf[8];
    bool run_address_user_edited;
    bool auto_run;
    bool error_dialog_open;
    char error_text[4096];
    unsigned int error_scroll_x;
    unsigned int error_scroll_y;
} frontend_assembler_state;

typedef enum frontend_debugger_intent_type {
    FRONTEND_DEBUGGER_INTENT_NONE = 0,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y,
    FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS,
    FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY,
    FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW,
    FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE,
    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_REQUEST_SNAPSHOT,
    FRONTEND_DEBUGGER_INTENT_PROGRAM_LOAD_PRG_DIALOG,
    FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG,
    FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT,
    FRONTEND_DEBUGGER_INTENT_MACHINE_RESET,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY,
    FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE,
    FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN
} frontend_debugger_intent_type;

typedef struct frontend_config_apply_result {
    bool accepted;
    bool needs_reboot;
    bool save_ini_on_quit;
    bool symbols_changed;
} frontend_config_apply_result;

typedef struct frontend_debugger_intent {
    frontend_debugger_intent_type type;
    uint16_t address;
    uint16_t length;
    uint16_t value;
    uint32_t id;
    bool enabled;
    runtime_memory_mode memory_mode;
    runtime_breakpoint_definition breakpoint;
    app_options config;
    frontend_config_apply_result config_result;
    /* Assembler */
    char assemble_path[1024];
    uint16_t assemble_address;
    uint16_t assemble_run_address;
    bool assemble_auto_run;
    uint8_t disk_device;
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
bool frontend_routes_keyboard_to_c64(const frontend *ui);
bool frontend_submit_frame(frontend *ui, const c64_frame *frame);
void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state);
bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent);
void frontend_set_layout_state(frontend *ui, const frontend_layout_state *state);
void frontend_get_layout_state(frontend *ui, frontend_layout_state *out_state);
void frontend_set_config_state(frontend *ui, const app_options *options);
bool frontend_apply_selected_ini(frontend *ui, const app_options *options);
bool frontend_get_disassembly_cursor(const frontend *ui, uint16_t *out_address);
void frontend_append_symbol_file(frontend *ui, const char *path);
void frontend_set_assembler_path(frontend *ui, const char *path);
void frontend_show_assembler_errors(frontend *ui, const char *errors);
void frontend_update_symbols(frontend *ui, const runtime_symbol_snapshot *snapshot);
