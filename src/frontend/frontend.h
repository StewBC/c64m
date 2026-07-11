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
    runtime_memory_snapshot memory_view_snapshots[16];
    int memory_view_snapshot_count;
    runtime_debug_memory_snapshot debug_memory;
    runtime_breakpoint_snapshot breakpoints;
    runtime_memory_banking_snapshot memory_banking;
    c64_vicii_hardware_snapshot vicii_hardware;
    c64_cia_hardware_snapshot cia1_hardware;
    c64_cia_hardware_snapshot cia2_hardware;
    c64_sid_hardware_snapshot sid_hardware;
    c64_1541_hardware_snapshot drive8_hardware;
    c64_1541_hardware_snapshot drive9_hardware;
    runtime_disk_status_snapshot disk_status[2];
    runtime_call_stack_snapshot call_stack;
    uint64_t frame_number;
    uint64_t frame_cycle;
    uint64_t dropped_frames;
    uint64_t machine_cycle;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
    uint64_t step_cycle_start;
    uint64_t step_cpu_cycle_start;
    uint64_t screen_ram_writes;
    uint64_t color_ram_writes;
    uint64_t vic_register_writes;
    uint64_t cia1_register_writes;
    uint64_t cia2_register_writes;
    uint64_t sid_register_writes;
    uint64_t keyboard_events;
    uint64_t irq_entries;
    uint64_t cia1_icr_reads;
    uint64_t cia1_icr_writes;
    uint64_t cia1_interrupt_assertions;
    uint64_t nmi_entries;
    uint64_t restore_requests;
    runtime_stop_reason stop_reason;
    uint32_t active_turbo_multiplier;
    bool has_frame;
    bool has_cpu;
    bool has_memory;
    bool has_debug_memory;
    bool has_breakpoints;
    bool has_memory_banking;
    bool has_hardware;
    bool has_disk_status[2];
    bool has_call_stack;
    bool cartridge_attached;
} frontend_debug_state;

const char *frontend_runtime_state_name(frontend_runtime_state state);
const char *frontend_stop_reason_name(runtime_stop_reason reason);

typedef struct frontend_assembler_state {
    bool initialized;
    char file_path[1024];
    char address_buf[8];
    char run_address_buf[8];
    bool run_address_user_edited;
    bool auto_run;
    bool reset_first;
    bool rearm_oneshots;
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
    FRONTEND_DEBUGGER_INTENT_REQUEST_DEBUG_MEMORY,
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
    FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG,
    FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT,
    FRONTEND_DEBUGGER_INTENT_DISK_EJECT_ALL,
    FRONTEND_DEBUGGER_INTENT_DISK_SELECT,
    FRONTEND_DEBUGGER_INTENT_DISK_SET_WRITABLE,
    FRONTEND_DEBUGGER_INTENT_MACHINE_RESET,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY,
    FRONTEND_DEBUGGER_INTENT_SAVE_INI_NOW,
    FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE,
    FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN,
    FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE,
    FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE,
    FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE,
    FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG,
    FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG,
    FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK,
    FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG,
    FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_ROM_DIALOG,
    FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT
} frontend_debugger_intent_type;

/* File-browser "default folder" slots. Each remembers the last directory used by
   a family of callers so the browser reopens there. The order must match the
   [browse] key order in app_options.c. Load/Save-binary pick prg/basic/text by
   the dialog's Basic Program / Basic Text checkboxes; the plain PRG program load
   shares the PROGRAM slot; disk 8 & 9 share DISK; state save/load share SNAPSHOT. */
typedef enum frontend_browse_slot {
    FRONTEND_BROWSE_SLOT_ASSEMBLER = 0,
    FRONTEND_BROWSE_SLOT_DISK,
    FRONTEND_BROWSE_SLOT_PROGRAM,
    FRONTEND_BROWSE_SLOT_BASIC,
    FRONTEND_BROWSE_SLOT_TEXT,
    FRONTEND_BROWSE_SLOT_SNAPSHOT,
    FRONTEND_BROWSE_SLOT_COUNT
} frontend_browse_slot;

typedef struct frontend_config_apply_result {
    bool accepted;
    bool needs_reboot;
    bool symbols_changed;
    bool roms_changed;
} frontend_config_apply_result;

typedef struct frontend_debugger_intent {
    frontend_debugger_intent_type type;
    uint16_t address;
    uint16_t length;
    uint16_t value;
    uint32_t id;
    bool enabled;
    bool include_write_history;
    runtime_memory_mode memory_mode;
    runtime_breakpoint_definition breakpoint;
    app_options config;
    frontend_config_apply_result config_result;
    /* Assembler */
    char assemble_path[1024];
    uint16_t assemble_address;
    uint16_t assemble_run_address;
    bool assemble_auto_run;
    bool assemble_reset_first;
    bool assemble_rearm_oneshots;
    uint8_t disk_device;
    int disk_queue_index;
    bool disk_writable;
    /* Load */
    char load_bin_path[1024];
    uint16_t load_bin_address;
    bool load_bin_use_file_address;
    bool load_bin_reset_first;
    bool load_bin_is_basic;
    bool load_bin_is_basic_text;
    /* Save */
    char save_bin_path[1024];
    uint16_t save_bin_start;
    uint16_t save_bin_end;
    bool save_bin_write_file_address;
    bool save_bin_is_basic;
    bool save_bin_is_basic_text;
    /* Machine reset */
    bool machine_reset_detach_cartridge;
    bool machine_reset_resume_running;
    /* File browser result */
    frontend_debugger_intent_type file_browser_purpose;
    char file_browser_path[1024];
} frontend_debugger_intent;

typedef struct frontend_load_bin_dialog_state {
    bool open;
    bool initialized;
    char path[1024];
    bool use_file_address;
    char address_buf[5];
    bool reset_first;
    bool basic_program;
    bool basic_text;
    char error[128];
} frontend_load_bin_dialog_state;

typedef struct frontend_save_bin_dialog_state {
    bool open;
    bool initialized;
    char path[1024];
    bool basic_program;
    bool basic_text;
    bool write_file_address;
    char start_address_buf[5];
    char end_address_buf[5];
    char error[128];
} frontend_save_bin_dialog_state;

typedef struct frontend_layout_state {
    float split_display_right;
    float split_top_bottom;
    float split_memory_misc;
} frontend_layout_state;

typedef struct frontend_assembler_options {
    char file[1024];
    char address[8];
    char run_address[8];
    bool auto_run;
    bool reset_first;
    bool rearm_oneshots;
} frontend_assembler_options;

frontend *frontend_create(platform_window *window);
void frontend_destroy(frontend *ui);

void frontend_begin_input(frontend *ui);
void frontend_handle_event(frontend *ui, SDL_Event *event);
void frontend_end_input(frontend *ui);
bool frontend_routes_keyboard_to_c64(const frontend *ui);
bool frontend_wants_text_input(const frontend *ui);
bool frontend_handle_view_cycle_key(frontend *ui, const SDL_KeyboardEvent *key);
bool frontend_submit_frame(frontend *ui, const c64_frame *frame);
void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state);
/* Force disk activity LEDs off (e.g. machine reset). */
void frontend_clear_disk_activity_leds(frontend *ui);
void frontend_open_help(frontend *ui, bool paused_by_help);
bool frontend_close_help(frontend *ui);
bool frontend_help_is_open(const frontend *ui);
bool frontend_help_paused_by_help(const frontend *ui);
bool frontend_handle_help_key(frontend *ui, const SDL_KeyboardEvent *key, int scroll_wheel_lines);
bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent);
void frontend_set_layout_state(frontend *ui, const frontend_layout_state *state);
void frontend_get_layout_state(frontend *ui, frontend_layout_state *out_state);
void frontend_debug_min_window_size(const frontend *ui, int *out_min_w, int *out_min_h);
void frontend_set_config_state(frontend *ui, const app_options *options);
bool frontend_config_dialog_is_open(const frontend *ui);
bool frontend_trigger_assembler(frontend *ui);
void frontend_set_disk_queue(frontend *ui, uint8_t device, const app_disk_slot *slot);
bool frontend_apply_selected_ini(frontend *ui, const app_options *options);
bool frontend_get_disassembly_cursor(const frontend *ui, uint16_t *out_address);
void frontend_append_symbol_file(frontend *ui, const char *path);
void frontend_set_assembler_path(frontend *ui, const char *path);
void frontend_show_assembler_errors(frontend *ui, const char *errors);
void frontend_update_symbols(frontend *ui, const runtime_symbol_snapshot *snapshot);
void frontend_set_load_bin_path(frontend *ui, const char *path);
void frontend_set_save_bin_path(frontend *ui, const char *path);
void frontend_invalidate_disassembly_cache(frontend *ui);
void frontend_set_assembler_options(frontend *ui, const frontend_assembler_options *opts);
void frontend_get_assembler_options(frontend *ui, frontend_assembler_options *out);
void frontend_open_file_browser(
    frontend *ui,
    frontend_debugger_intent_type purpose,
    const char *title,
    bool save_mode,
    const char *filter_extension,
    const char *default_extension,
    uint8_t disk_device);

/* Seed / read the remembered default folder for a browse slot. main.c uses these
   to bridge the slots to the INI file: seed from options at startup, read back
   before saving. get returns "" (never NULL) when the slot is unset. */
void frontend_set_browse_dir(frontend *ui, frontend_browse_slot slot, const char *dir);
const char *frontend_get_browse_dir(const frontend *ui, frontend_browse_slot slot);
/* Stores a folder picked via a Paths-tab [...] button into its pending slot. */
void frontend_set_picked_browse_dir(frontend *ui, const char *path);
void frontend_set_picked_rom_path(frontend *ui, const char *path);
void frontend_config_export_rom_paths(const frontend *ui, app_options *options);
