#include "frontend.h"

#include "help_view.h"
#include "nuklear_config.h"
#include "nuklear_sdl.h"

#include "c64_layout.h"
#include "c64_pro_mono_font_data.h"
#include "disasm_6502.h"
#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define C64M_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#define C64M_STAT_ISREG(mode) S_ISREG(mode)
#endif

enum {
    FRONTEND_DEBUGGER_INTENT_CAPACITY = 32,
    FRONTEND_DISPLAY_CROP_X = 8,
    FRONTEND_DISPLAY_CROP_Y = 31,
    FRONTEND_DISPLAY_CROP_W = 352,
    FRONTEND_DISPLAY_CROP_H = 240
};

typedef enum frontend_register_field {
    FRONTEND_REGISTER_FIELD_NONE = 0,
    FRONTEND_REGISTER_FIELD_PC,
    FRONTEND_REGISTER_FIELD_SP,
    FRONTEND_REGISTER_FIELD_A,
    FRONTEND_REGISTER_FIELD_X,
    FRONTEND_REGISTER_FIELD_Y,
    FRONTEND_REGISTER_FIELD_STATUS_N,
    FRONTEND_REGISTER_FIELD_STATUS_V,
    FRONTEND_REGISTER_FIELD_STATUS_UNUSED,
    FRONTEND_REGISTER_FIELD_STATUS_B,
    FRONTEND_REGISTER_FIELD_STATUS_D,
    FRONTEND_REGISTER_FIELD_STATUS_I,
    FRONTEND_REGISTER_FIELD_STATUS_Z,
    FRONTEND_REGISTER_FIELD_STATUS_C
} frontend_register_field;

typedef struct frontend_register_view_state {
    frontend_register_field active_field;
    char pc[5];
    char sp[3];
    char a[3];
    char x[3];
    char y[3];
    char flags[8][2];
} frontend_register_view_state;

typedef enum frontend_memory_edit_field {
    FRONTEND_MEMORY_EDIT_HEX = 0,
    FRONTEND_MEMORY_EDIT_ASCII,
    FRONTEND_MEMORY_EDIT_ADDRESS
} frontend_memory_edit_field;

typedef struct frontend_memory_view_state {
    uint16_t view_address;
    uint16_t cursor_address;
    runtime_memory_mode mode;
    frontend_memory_edit_field edit_field;
    uint8_t active_nibble;
    uint8_t active_address_digit;
    uint8_t address_digits[4];
    uint16_t requested_address;
    uint16_t requested_length;
    runtime_memory_mode requested_mode;
    uint8_t columns;
    uint8_t rows;
    bool initialized;
    bool request_pending;
    bool active;
    bool scrollbar_dragging;
    float scrollbar_grab_offset;
} frontend_memory_view_state;

enum {
    FRONTEND_DISASM_MAX_ROWS = 128,
    FRONTEND_DISASM_FETCH_BYTES = RUNTIME_MEMORY_SNAPSHOT_MAX,
    FRONTEND_DISASM_CENTER_LOOKBACK = 32
};

typedef struct frontend_disassembly_view_state {
    uint16_t top_address;
    uint16_t cursor_address;
    uint16_t cursor_prev_address;
    uint16_t pc_lock_address;
    uint16_t requested_address;
    uint16_t requested_length;
    runtime_memory_mode mode;
    runtime_memory_mode requested_mode;
    uint8_t rows;
    uint8_t cursor_row;
    uint8_t cursor_length;
    uint8_t active_address_digit;
    uint8_t symbol_display_mode;
    uint16_t last_pc;
    frontend_runtime_state last_runtime_state;
    bool initialized;
    bool request_pending;
    bool active;
    bool address_entry;
    bool follow_pc;
    bool pc_lock_active;
    bool has_last_pc;
    bool has_user_cursor;
    bool has_snapshot;
    bool scrollbar_dragging;
    float scrollbar_grab_offset;
    runtime_memory_snapshot snapshot;
    disasm_6502_line lines[FRONTEND_DISASM_MAX_ROWS];
} frontend_disassembly_view_state;

typedef enum frontend_misc_tab {
    FRONTEND_MISC_TAB_PROGRAMS = 0,
    FRONTEND_MISC_TAB_DEBUGGER,
    FRONTEND_MISC_TAB_BREAKPOINTS,
    FRONTEND_MISC_TAB_HARDWARE,
    FRONTEND_MISC_TAB_ASSEMBLER
} frontend_misc_tab;

typedef struct frontend_misc_view_state {
    frontend_misc_tab active_tab;
    bool initialized;
    bool active;
} frontend_misc_view_state;

typedef enum frontend_active_view {
    FRONTEND_ACTIVE_VIEW_NONE = 0,
    FRONTEND_ACTIVE_VIEW_C64,
    FRONTEND_ACTIVE_VIEW_DISASSEMBLY,
    FRONTEND_ACTIVE_VIEW_MISC,
    FRONTEND_ACTIVE_VIEW_MEMORY
} frontend_active_view;

typedef enum frontend_config_tab {
    FRONTEND_CONFIG_TAB_MACHINE = 0,
    FRONTEND_CONFIG_TAB_EMULATOR
} frontend_config_tab;

typedef enum frontend_ini_prompt_state {
    FRONTEND_INI_PROMPT_NONE = 0,
    FRONTEND_INI_PROMPT_EXISTING
} frontend_ini_prompt_state;

typedef struct frontend_config_dialog_state {
    bool open;
    bool initialized;
    frontend_config_tab active_tab;
    frontend_ini_prompt_state prompt;
    app_options original;
    app_options edited;
    char previous_ini_path[1024];
    bool previous_save_ini;
    bool save_ini_on_quit;
    char error[160];
} frontend_config_dialog_state;

typedef enum frontend_breakpoint_dialog_mode {
    FRONTEND_BREAKPOINT_DIALOG_CREATE = 0,
    FRONTEND_BREAKPOINT_DIALOG_EDIT
} frontend_breakpoint_dialog_mode;

typedef struct frontend_breakpoint_dialog_state {
    bool open;
    frontend_breakpoint_dialog_mode mode;
    uint32_t id;
    bool enabled;
    bool execute;
    bool read;
    bool write;
    bool range;
    int mapping;
    bool use_counter;
    bool action_break;
    bool action_fast;
    bool action_slow;
    bool action_tron;
    bool action_troff;
    bool action_type;
    bool action_swap;
    char start_address[5];
    char end_address[5];
    char initial_count[11];
    char reset_count[11];
    char error[96];
} frontend_breakpoint_dialog_state;

struct frontend {
    platform_window *window;
    struct nk_context *ctx;
    SDL_Renderer *renderer;
    struct nk_font *help_font;
    SDL_Texture *display_texture;
    c64_frame current_frame;
    bool has_frame;
    c64_layout layout;
    c64_layout_limits limits;
    frontend_active_view active_view;
    bool c64_input_active;
    bool input_focus_initialized;
    frontend_register_view_state registers;
    frontend_memory_view_state memory;
    frontend_disassembly_view_state disassembly;
    frontend_misc_view_state misc;
    frontend_config_dialog_state config_dialog;
    frontend_breakpoint_dialog_state breakpoint_dialog;
    frontend_assembler_state assembler;
    frontend_load_bin_dialog_state load_bin_dialog;
    frontend_save_bin_dialog_state save_bin_dialog;
    frontend_help_state help;
    symbol_resolver symbols;
    symbol_table *symbol_table;
    frontend_debugger_intent intents[FRONTEND_DEBUGGER_INTENT_CAPACITY];
    size_t intent_read;
    size_t intent_write;
    bool cancel_register_edit_requested;
    SDL_KeyboardEvent pending_memory_key;
    bool has_pending_memory_key;
    SDL_KeyboardEvent pending_disassembly_key;
    bool has_pending_disassembly_key;
    struct nk_rect memory_scrollbar_bounds;
    bool has_memory_scrollbar_bounds;
    struct nk_rect disassembly_scrollbar_bounds;
    bool has_disassembly_scrollbar_bounds;
};

static const nk_flags pane_flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR;

static bool frontend_any_dialog_open(const frontend *ui)
{
    return ui->config_dialog.open
        || ui->breakpoint_dialog.open
        || ui->load_bin_dialog.open
        || ui->save_bin_dialog.open
        || ui->assembler.error_dialog_open;
}

static void frontend_set_active_view(frontend *ui, frontend_active_view view)
{
    if (ui == NULL) {
        return;
    }

    ui->active_view = view;
    ui->c64_input_active = view == FRONTEND_ACTIVE_VIEW_C64;
    ui->disassembly.active = view == FRONTEND_ACTIVE_VIEW_DISASSEMBLY;
    ui->misc.active = view == FRONTEND_ACTIVE_VIEW_MISC;
    ui->memory.active = view == FRONTEND_ACTIVE_VIEW_MEMORY;
}

static frontend_active_view frontend_next_active_view(frontend_active_view view, bool reverse)
{
    static const frontend_active_view cycle[] = {
        FRONTEND_ACTIVE_VIEW_C64,
        FRONTEND_ACTIVE_VIEW_DISASSEMBLY,
        FRONTEND_ACTIVE_VIEW_MISC,
        FRONTEND_ACTIVE_VIEW_MEMORY
    };
    size_t count = sizeof(cycle) / sizeof(cycle[0]);
    size_t i;

    for (i = 0; i < count; ++i) {
        if (cycle[i] == view) {
            return reverse ? cycle[(i + count - 1u) % count] : cycle[(i + 1u) % count];
        }
    }

    return reverse ? cycle[count - 1u] : cycle[0];
}

static void frontend_draw_view_border(struct nk_context *ctx, struct nk_color color, float inset, float thickness)
{
    struct nk_command_buffer *canvas;
    struct nk_rect content;

    if (ctx == NULL) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    content = nk_window_get_content_region(ctx);
    if (content.w <= inset * 2.0f || content.h <= inset * 2.0f) {
        return;
    }

    nk_stroke_rect(
        canvas,
        nk_rect(content.x + inset, content.y + inset, content.w - inset * 2.0f, content.h - inset * 2.0f),
        0.0f,
        thickness,
        color);
}

static void frontend_draw_active_view_border(struct nk_context *ctx)
{
    frontend_draw_view_border(ctx, nk_rgb(188, 198, 190), 1.0f, 2.0f);
}

static void frontend_draw_memory_mode_border(struct nk_context *ctx, runtime_memory_mode mode, bool view_active)
{
    struct nk_color border_color;

    if (mode == RUNTIME_MEMORY_MODE_CPU_MAP) {
        return;
    }

    border_color = mode == RUNTIME_MEMORY_MODE_ROM ?
        nk_rgb(200, 130, 40) : nk_rgb(60, 120, 200);
    frontend_draw_view_border(ctx, border_color, view_active ? 4.0f : 1.0f, 2.0f);
}

static SDL_Rect frontend_fit_rect(int area_x, int area_y, int area_w, int area_h, int source_w, int source_h)
{
    SDL_Rect out = {area_x, area_y, 0, 0};
    int width_from_height;
    int height_from_width;

    if (area_w <= 0 || area_h <= 0 || source_w <= 0 || source_h <= 0) {
        return out;
    }

    height_from_width = area_w * source_h / source_w;
    if (height_from_width <= area_h) {
        out.w = area_w;
        out.h = height_from_width;
    } else {
        width_from_height = area_h * source_w / source_h;
        out.w = width_from_height;
        out.h = area_h;
    }

    out.x = area_x + (area_w - out.w) / 2;
    out.y = area_y + (area_h - out.h) / 2;
    return out;
}

static struct nk_rect frontend_fit_nk_rect(
    struct nk_rect area,
    uint32_t source_w,
    uint32_t source_h)
{
    SDL_Rect fit = frontend_fit_rect(
        (int)area.x,
        (int)area.y,
        (int)area.w,
        (int)area.h,
        (int)source_w,
        (int)source_h);

    return nk_rect((float)fit.x, (float)fit.y, (float)fit.w, (float)fit.h);
}

static bool frontend_point_in_rect(float x, float y, struct nk_rect rect)
{
    return x >= rect.x && x < rect.x + rect.w &&
        y >= rect.y && y < rect.y + rect.h;
}

static bool frontend_click_in_any_dialog(const frontend *ui, float x, float y)
{
    struct nk_window *win;

    if (ui->config_dialog.open) {
        win = nk_window_find(ui->ctx, "Configure");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->breakpoint_dialog.open) {
        win = nk_window_find(ui->ctx, "Breakpoint Editor");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->load_bin_dialog.open) {
        win = nk_window_find(ui->ctx, "Load");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->save_bin_dialog.open) {
        win = nk_window_find(ui->ctx, "Save");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    if (ui->assembler.error_dialog_open) {
        win = nk_window_find(ui->ctx, "Assembly Errors");
        if (win && frontend_point_in_rect(x, y, win->bounds)) {
            return true;
        }
    }
    return false;
}

static const char *frontend_runtime_state_name(frontend_runtime_state state)
{
    switch (state) {
        case FRONTEND_RUNTIME_STATE_RUNNING:
            return "RUNNING";
        case FRONTEND_RUNTIME_STATE_PAUSED:
            return "PAUSED";
        case FRONTEND_RUNTIME_STATE_ERROR:
            return "ERROR";
        case FRONTEND_RUNTIME_STATE_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

static const char *frontend_stop_reason_name(runtime_stop_reason reason)
{
    switch (reason) {
        case RUNTIME_STOP_REASON_RESET:
            return "reset";
        case RUNTIME_STOP_REASON_PAUSE_COMMAND:
            return "pause";
        case RUNTIME_STOP_REASON_STEP:
            return "step";
        case RUNTIME_STOP_REASON_RUN_COMPLETE:
            return "run complete";
        case RUNTIME_STOP_REASON_BREAKPOINT:
            return "breakpoint";
        case RUNTIME_STOP_REASON_ERROR:
            return "error";
        case RUNTIME_STOP_REASON_NONE:
        default:
            return "none";
    }
}

static void frontend_push_debugger_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint16_t value);

static bool frontend_push_assemble_run_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool reset_first);

static void frontend_draw_assembler_error_dialog(frontend *ui, int width, int height);

static void frontend_push_breakpoint_id_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    bool enabled);

static void frontend_push_breakpoint_definition_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    const runtime_breakpoint_definition *definition);

static const runtime_breakpoint_snapshot_entry *frontend_find_execute_breakpoint(
    const frontend_debug_state *debug_state,
    uint16_t address)
{
    uint16_t i;

    if (debug_state == NULL || !debug_state->has_breakpoints) {
        return NULL;
    }

    for (i = 0; i < debug_state->breakpoints.count; ++i) {
        const runtime_breakpoint_snapshot_entry *entry = &debug_state->breakpoints.entries[i];
        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0 && entry->address == address) {
            return entry;
        }
    }

    return NULL;
}

static void frontend_toggle_execute_breakpoint_at_cursor(
    frontend *ui,
    const frontend_debug_state *debug_state)
{
    const runtime_breakpoint_snapshot_entry *entry;
    uint16_t address;

    if (ui == NULL || !ui->disassembly.has_user_cursor) {
        return;
    }

    address = ui->disassembly.cursor_address;
    entry = frontend_find_execute_breakpoint(debug_state, address);
    if (entry != NULL) {
        frontend_push_breakpoint_id_intent(
            ui,
            FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
            entry->id,
            false);
        return;
    }

    frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE, address);
}

static bool frontend_parse_hex16_text(const char *text, uint16_t *out)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return false;
    }

    value = strtoul(text, &end, 16);
    if (end == text || *end != '\0' || value > 0xfffful) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool frontend_parse_u32_text(const char *text, uint32_t *out)
{
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }

    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static void frontend_open_breakpoint_dialog_default(frontend *ui)
{
    frontend_breakpoint_dialog_state *dialog;

    if (ui == NULL) {
        return;
    }

    dialog = &ui->breakpoint_dialog;
    memset(dialog, 0, sizeof(*dialog));
    dialog->open = true;
    dialog->mode = FRONTEND_BREAKPOINT_DIALOG_CREATE;
    dialog->enabled = true;
    dialog->execute = true;
    dialog->mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
    dialog->action_break = true;
    snprintf(dialog->start_address, sizeof(dialog->start_address), "%04X", ui->disassembly.cursor_address);
    snprintf(dialog->end_address, sizeof(dialog->end_address), "%04X", ui->disassembly.cursor_address);
    snprintf(dialog->initial_count, sizeof(dialog->initial_count), "0");
    snprintf(dialog->reset_count, sizeof(dialog->reset_count), "0");
}

static void frontend_open_breakpoint_dialog_from_entry(
    frontend *ui,
    const runtime_breakpoint_snapshot_entry *entry,
    bool duplicate)
{
    frontend_breakpoint_dialog_state *dialog;

    if (ui == NULL || entry == NULL) {
        return;
    }

    dialog = &ui->breakpoint_dialog;
    memset(dialog, 0, sizeof(*dialog));
    dialog->open = true;
    dialog->mode = duplicate ? FRONTEND_BREAKPOINT_DIALOG_CREATE : FRONTEND_BREAKPOINT_DIALOG_EDIT;
    dialog->id = duplicate ? 0 : entry->id;
    dialog->enabled = entry->enabled != 0;
    dialog->execute = (entry->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0;
    dialog->read = (entry->access & RUNTIME_BREAKPOINT_ACCESS_READ) != 0;
    dialog->write = (entry->access & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0;
    dialog->range = entry->has_end_address != 0;
    dialog->mapping = entry->mapping;
    dialog->use_counter = entry->use_counter != 0;
    dialog->action_break = (entry->actions & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0;
    dialog->action_fast = (entry->actions & RUNTIME_BREAKPOINT_ACTION_FAST) != 0;
    dialog->action_slow = (entry->actions & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0;
    dialog->action_tron = (entry->actions & RUNTIME_BREAKPOINT_ACTION_TRON) != 0;
    dialog->action_troff = (entry->actions & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0;
    dialog->action_type = (entry->actions & RUNTIME_BREAKPOINT_ACTION_TYPE) != 0;
    dialog->action_swap = (entry->actions & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0;
    snprintf(dialog->start_address, sizeof(dialog->start_address), "%04X", entry->start_address);
    snprintf(dialog->end_address, sizeof(dialog->end_address), "%04X", entry->end_address);
    snprintf(dialog->initial_count, sizeof(dialog->initial_count), "%u", entry->initial_count);
    snprintf(dialog->reset_count, sizeof(dialog->reset_count), "%u", entry->reset_count);
}

static bool frontend_breakpoint_dialog_build_definition(
    frontend_breakpoint_dialog_state *dialog,
    runtime_breakpoint_definition *definition)
{
    uint16_t start;
    uint16_t end;

    memset(definition, 0, sizeof(*definition));
    if (!frontend_parse_hex16_text(dialog->start_address, &start)) {
        snprintf(dialog->error, sizeof(dialog->error), "Invalid start address");
        return false;
    }
    if (dialog->range && !frontend_parse_hex16_text(dialog->end_address, &end)) {
        snprintf(dialog->error, sizeof(dialog->error), "Invalid end address");
        return false;
    }
    if (!dialog->execute && !dialog->read && !dialog->write) {
        snprintf(dialog->error, sizeof(dialog->error), "Select at least one access type");
        return false;
    }
    if (!dialog->action_break && !dialog->action_fast && !dialog->action_slow &&
        !dialog->action_tron && !dialog->action_troff && !dialog->action_type && !dialog->action_swap) {
        snprintf(dialog->error, sizeof(dialog->error), "Select at least one action");
        return false;
    }

    definition->enabled = dialog->enabled ? 1u : 0u;
    definition->start_address = start;
    definition->end_address = dialog->range ? end : start;
    definition->has_end_address = dialog->range ? 1u : 0u;
    definition->mapping = (runtime_breakpoint_mapping)dialog->mapping;
    if (dialog->execute) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    }
    if (dialog->read) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_READ;
    }
    if (dialog->write) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_WRITE;
    }
    if (dialog->action_break) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_BREAK;
    }
    if (dialog->action_fast) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_FAST;
    }
    if (dialog->action_slow) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SLOW;
    }
    if (dialog->action_tron) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
    }
    if (dialog->action_troff) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TROFF;
    }
    if (dialog->action_type) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
    }
    if (dialog->action_swap) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
    }

    if (dialog->use_counter) {
        definition->use_counter = 1;
        if (!frontend_parse_u32_text(dialog->initial_count, &definition->initial_count) ||
            !frontend_parse_u32_text(dialog->reset_count, &definition->reset_count)) {
            snprintf(dialog->error, sizeof(dialog->error), "Invalid counter value");
            return false;
        }
    }

    dialog->error[0] = '\0';
    return true;
}

static void frontend_checkbox_bool(struct nk_context *ctx, const char *label, bool *value)
{
    nk_bool active = *value ? nk_true : nk_false;
    nk_checkbox_label(ctx, label, &active);
    *value = active != 0;
}

static bool frontend_file_exists(const char *path)
{
    struct stat st;

    return path != NULL && path[0] != '\0' && stat(path, &st) == 0 && C64M_STAT_ISREG(st.st_mode);
}

static bool frontend_string_equal(const char *a, const char *b)
{
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b) == 0;
}

static void frontend_copy_text(char *out, size_t out_size, const char *value)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", value != NULL ? value : "");
}

static bool frontend_config_reserve_string(char **target, size_t capacity)
{
    char *buffer;

    if (target == NULL || capacity == 0) {
        return false;
    }

    buffer = calloc(1, capacity);
    if (buffer == NULL) {
        return false;
    }

    if (*target != NULL) {
        snprintf(buffer, capacity, "%s", *target);
    }
    free(*target);
    *target = buffer;
    return true;
}

static bool frontend_config_prepare_edit_buffers(frontend_config_dialog_state *dialog)
{
    return
        frontend_config_reserve_string(&dialog->edited.ini_path, 1024) &&
        frontend_config_reserve_string(&dialog->edited.video_standard, 16) &&
        frontend_config_reserve_string(&dialog->edited.video_filter, 64) &&
        frontend_config_reserve_string(&dialog->edited.turbo_multipliers, 256) &&
        frontend_config_reserve_string(&dialog->edited.symbol_files, 1024);
}

static void frontend_config_dialog_reset(frontend_config_dialog_state *dialog)
{
    app_options_destroy(&dialog->original);
    app_options_destroy(&dialog->edited);
    memset(dialog, 0, sizeof(*dialog));
}

static bool frontend_config_dialog_open(frontend *ui)
{
    if (ui == NULL) {
        return false;
    }

    ui->config_dialog.open = true;
    ui->config_dialog.active_tab = FRONTEND_CONFIG_TAB_MACHINE;
    ui->config_dialog.error[0] = '\0';
    return true;
}

static bool frontend_push_config_apply_intent(
    frontend *ui,
    const app_options *options,
    const frontend_config_apply_result *result)
{
    size_t next;

    if (ui == NULL || options == NULL || result == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY;
    ui->intents[ui->intent_write].config_result = *result;
    if (!app_options_copy(&ui->intents[ui->intent_write].config, options)) {
        ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_NONE;
        return false;
    }
    ui->intent_write = next;
    return true;
}

static bool frontend_push_assemble_run_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool reset_first)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN;
    snprintf(
        ui->intents[ui->intent_write].assemble_path,
        sizeof(ui->intents[ui->intent_write].assemble_path),
        "%s", path);
    ui->intents[ui->intent_write].assemble_address = address;
    ui->intents[ui->intent_write].assemble_run_address = run_address;
    ui->intents[ui->intent_write].assemble_auto_run = auto_run;
    ui->intents[ui->intent_write].assemble_reset_first = reset_first;
    ui->intent_write = next;
    return true;
}

static bool frontend_push_simple_intent(frontend *ui, frontend_debugger_intent_type type)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intent_write = next;
    return true;
}

static bool frontend_push_disk_intent(frontend *ui, frontend_debugger_intent_type type, uint8_t device)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].disk_device = device;
    ui->intent_write = next;
    return true;
}

static nk_flags frontend_edit_replace(
    struct nk_context *ctx,
    nk_flags flags,
    char *buffer,
    int max,
    nk_plugin_filter filter)
{
    nk_flags result = nk_edit_string_zero_terminated(
        ctx,
        (flags & ~(nk_flags)NK_EDIT_ALWAYS_INSERT_MODE) | NK_EDIT_SIG_ENTER,
        buffer, max, filter);
    if (result & NK_EDIT_ACTIVE) {
        ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
    }
    if (result & NK_EDIT_COMMITED) {
        nk_edit_unfocus(ctx);
    }
    return result;
}

static void frontend_draw_breakpoint_editor(frontend *ui, int width, int height)
{
    frontend_breakpoint_dialog_state *dialog;
    runtime_breakpoint_definition definition;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_flags = NK_EDIT_FIELD;

    if (ui == NULL || !ui->breakpoint_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dialog = &ui->breakpoint_dialog;
    bounds = nk_rect((float)(width - 430) * 0.5f, (float)(height - 430) * 0.5f, 430.0f, 430.0f);
    if (bounds.x < 8.0f) {
        bounds.x = 8.0f;
    }
    if (bounds.y < 8.0f) {
        bounds.y = 8.0f;
    }

    if (nk_begin(
            ctx,
            "Breakpoint Editor",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Breakpoint Editor")) {
            nk_layout_row_dynamic(ctx, 20.0f, 1);
            frontend_checkbox_bool(ctx, "Enabled", &dialog->enabled);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Access", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 20.0f, 3);
            frontend_checkbox_bool(ctx, "Execute", &dialog->execute);
            frontend_checkbox_bool(ctx, "Read", &dialog->read);
            frontend_checkbox_bool(ctx, "Write", &dialog->write);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Address", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 4);
            nk_layout_row_push(ctx, 0.20f);
            nk_label(ctx, "Start", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.25f);
            frontend_edit_replace(ctx, edit_flags, dialog->start_address, sizeof(dialog->start_address), nk_filter_hex);
            nk_layout_row_push(ctx, 0.20f);
            frontend_checkbox_bool(ctx, "Range", &dialog->range);
            nk_layout_row_push(ctx, 0.25f);
            if (!dialog->range) {
                edit_flags |= NK_EDIT_READ_ONLY;
            }
            frontend_edit_replace(ctx, edit_flags, dialog->end_address, sizeof(dialog->end_address), nk_filter_hex);
            nk_layout_row_end(ctx);
            edit_flags = NK_EDIT_FIELD;

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Mapping", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 20.0f, 3);
            if (nk_option_label(ctx, "Map", dialog->mapping == RUNTIME_BREAKPOINT_MAPPING_MAP)) {
                dialog->mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
            }
            if (nk_option_label(ctx, "ROM", dialog->mapping == RUNTIME_BREAKPOINT_MAPPING_ROM)) {
                dialog->mapping = RUNTIME_BREAKPOINT_MAPPING_ROM;
            }
            if (nk_option_label(ctx, "RAM", dialog->mapping == RUNTIME_BREAKPOINT_MAPPING_RAM)) {
                dialog->mapping = RUNTIME_BREAKPOINT_MAPPING_RAM;
            }

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Counter", NK_TEXT_LEFT);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 5);
            nk_layout_row_push(ctx, 0.28f);
            frontend_checkbox_bool(ctx, "Use Counter", &dialog->use_counter);
            nk_layout_row_push(ctx, 0.16f);
            nk_label(ctx, "Initial", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.20f);
            frontend_edit_replace(
                ctx,
                dialog->use_counter ? NK_EDIT_FIELD : (NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->initial_count,
                sizeof(dialog->initial_count),
                nk_filter_decimal);
            nk_layout_row_push(ctx, 0.14f);
            nk_label(ctx, "Reset", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.20f);
            frontend_edit_replace(
                ctx,
                dialog->use_counter ? NK_EDIT_FIELD : (NK_EDIT_FIELD | NK_EDIT_READ_ONLY),
                dialog->reset_count,
                sizeof(dialog->reset_count),
                nk_filter_decimal);
            nk_layout_row_end(ctx);

            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "Actions", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 20.0f, 4);
            frontend_checkbox_bool(ctx, "Break", &dialog->action_break);
            frontend_checkbox_bool(ctx, "Fast", &dialog->action_fast);
            frontend_checkbox_bool(ctx, "Slow", &dialog->action_slow);
            frontend_checkbox_bool(ctx, "Tron", &dialog->action_tron);
            nk_layout_row_dynamic(ctx, 20.0f, 4);
            frontend_checkbox_bool(ctx, "Troff", &dialog->action_troff);
            frontend_checkbox_bool(ctx, "Swap", &dialog->action_swap);
            frontend_checkbox_bool(ctx, "Type", &dialog->action_type);
            nk_label(ctx, "", NK_TEXT_LEFT);

            if (dialog->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dialog->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dialog->open = false;
            }
            if (nk_button_label(ctx, "Apply") &&
                frontend_breakpoint_dialog_build_definition(dialog, &definition)) {
                if (dialog->mode == FRONTEND_BREAKPOINT_DIALOG_EDIT) {
                    frontend_push_breakpoint_definition_intent(
                        ui,
                        FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE,
                        dialog->id,
                        &definition);
                } else {
                    frontend_push_breakpoint_definition_intent(
                        ui,
                        FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE,
                        0,
                        &definition);
                }
                dialog->open = false;
            }
        } else {
            dialog->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Breakpoint Editor")) {
        dialog->open = false;
    }
    nk_end(ctx);
}

static bool frontend_config_validate(frontend_config_dialog_state *dialog)
{
    if (dialog->edited.display_width < 160 || dialog->edited.display_width > 2048) {
        snprintf(dialog->error, sizeof(dialog->error), "Display width must be 160..2048");
        return false;
    }
    if (dialog->edited.display_height < 120 || dialog->edited.display_height > 2048) {
        snprintf(dialog->error, sizeof(dialog->error), "Display height must be 120..2048");
        return false;
    }
    if (dialog->edited.scroll_wheel_lines < 1 || dialog->edited.scroll_wheel_lines > 100) {
        snprintf(dialog->error, sizeof(dialog->error), "Scroll wheel speed must be 1..100");
        return false;
    }
    if (dialog->edited.ini_path == NULL || dialog->edited.ini_path[0] == '\0') {
        snprintf(dialog->error, sizeof(dialog->error), "INI file path is required");
        return false;
    }
    dialog->error[0] = '\0';
    return true;
}

static bool frontend_config_standard_changed(const app_options *a, const app_options *b)
{
    return !frontend_string_equal(a->video_standard, b->video_standard);
}

static bool frontend_config_symbols_changed(const app_options *a, const app_options *b)
{
    return !frontend_string_equal(a->symbol_files, b->symbol_files);
}

static void frontend_config_commit_ini_path(frontend_config_dialog_state *dialog)
{
    const char *original;
    const char *edited;

    original = dialog->original.ini_path != NULL ? dialog->original.ini_path : "";
    edited = dialog->edited.ini_path != NULL ? dialog->edited.ini_path : "";
    if (strcmp(original, edited) != 0 && !dialog->edited.no_save_ini) {
        dialog->save_ini_on_quit = true;
    }
}

static void frontend_draw_config_tab_button(frontend *ui, frontend_config_tab tab, const char *label)
{
    struct nk_style_button saved_button = ui->ctx->style.button;

    if (ui->config_dialog.active_tab == tab) {
        ui->ctx->style.button.normal = nk_style_item_color(nk_rgb(49, 78, 94));
        ui->ctx->style.button.hover = nk_style_item_color(nk_rgb(58, 93, 112));
        ui->ctx->style.button.text_normal = nk_rgb(226, 246, 255);
    }

    if (nk_button_label(ui->ctx, label)) {
        ui->config_dialog.active_tab = tab;
    }

    ui->ctx->style.button = saved_button;
}

static void frontend_draw_config_machine_tab(frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    nk_layout_row_dynamic(ctx, 20.0f, 2);
    if (nk_option_label(ctx, "NTSC", frontend_string_equal(dialog->edited.video_standard, "NTSC"))) {
        app_options_set_string(&dialog->edited.video_standard, "NTSC");
    }
    if (nk_option_label(ctx, "PAL", frontend_string_equal(dialog->edited.video_standard, "PAL"))) {
        app_options_set_string(&dialog->edited.video_standard, "PAL");
    }

    nk_layout_row_dynamic(ctx, 22.0f, 2);
    nk_label(ctx, "Display Width", NK_TEXT_LEFT);
    nk_property_int(ctx, "#W", 160, &dialog->edited.display_width, 2048, 1, 8.0f);
    nk_label(ctx, "Display Height", NK_TEXT_LEFT);
    nk_property_int(ctx, "#H", 120, &dialog->edited.display_height, 2048, 1, 8.0f);

    nk_layout_row_dynamic(ctx, 20.0f, 2);
    frontend_checkbox_bool(ctx, "Integer Scale", &dialog->edited.integer_scale);
    frontend_checkbox_bool(ctx, "Aspect Correct", &dialog->edited.aspect_correct);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
    nk_layout_row_push(ctx, 0.30f);
    nk_label(ctx, "Filter", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.70f);
    if (dialog->edited.video_filter == NULL) {
        app_options_set_string(&dialog->edited.video_filter, "nearest");
    }
    frontend_edit_replace(
        ctx,
        NK_EDIT_FIELD,
        dialog->edited.video_filter,
        64,
        nk_filter_default);
    nk_layout_row_end(ctx);
}

static void frontend_draw_config_emulator_tab(frontend *ui, frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    if (dialog->edited.turbo_multipliers == NULL) {
        app_options_set_string(&dialog->edited.turbo_multipliers, "2,4,8,16");
    }
    if (dialog->edited.symbol_files == NULL) {
        app_options_set_string(&dialog->edited.symbol_files, "");
    }

    nk_layout_row_dynamic(ctx, 22.0f, 2);
    nk_label(ctx, "Scroll Wheel Speed", NK_TEXT_LEFT);
    nk_property_int(ctx, "#L", 1, &dialog->edited.scroll_wheel_lines, 100, 1, 4.0f);

    nk_layout_row_begin(ctx, NK_DYNAMIC, 22.0f, 2);
    nk_layout_row_push(ctx, 0.30f);
    nk_label(ctx, "Turbo Speeds", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.70f);
    frontend_edit_replace(
        ctx,
        NK_EDIT_FIELD,
        dialog->edited.turbo_multipliers,
        256,
        nk_filter_default);
    nk_layout_row_end(ctx);

    nk_layout_row_dynamic(ctx, 20.0f, 2);
    frontend_checkbox_bool(ctx, "Show Disk LEDs", &dialog->edited.show_leds);
    if (dialog->edited.no_save_ini) {
        nk_label(ctx, "Saving disabled", NK_TEXT_LEFT);
    } else {
        frontend_checkbox_bool(ctx, "Auto-save INI on Quit", &dialog->edited.remember);
    }

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Symbol Files", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 22.0f, 1);
    frontend_edit_replace(
        ctx,
        NK_EDIT_FIELD,
        dialog->edited.symbol_files,
        1024,
        nk_filter_default);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Add...")) {
        frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG);
    }
}

static void frontend_draw_config_existing_ini_prompt(frontend *ui, frontend_config_dialog_state *dialog, struct nk_context *ctx)
{
    if (dialog->prompt != FRONTEND_INI_PROMPT_EXISTING) {
        return;
    }

    if (nk_popup_begin(ctx, NK_POPUP_STATIC, "Parse selected INI file now?", NK_WINDOW_BORDER, nk_rect(220, 160, 360, 130))) {
        nk_layout_row_dynamic(ctx, 20.0f, 1);
        nk_label(ctx, "Parse selected INI file now?", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24.0f, 3);
        if (nk_button_label(ctx, "Yes")) {
            app_options parsed;
            memset(&parsed, 0, sizeof(parsed));
            if (app_options_copy(&parsed, &dialog->edited) &&
                app_options_apply_ini_file(&parsed, dialog->edited.ini_path)) {
                bool save_once = dialog->save_ini_on_quit;
                char path[1024];
                frontend_copy_text(path, sizeof(path), dialog->edited.ini_path);
                app_options_destroy(&dialog->edited);
                dialog->edited = parsed;
                app_options_set_string(&dialog->edited.ini_path, path);
                frontend_config_prepare_edit_buffers(dialog);
                dialog->save_ini_on_quit = save_once;
            } else {
                snprintf(dialog->error, sizeof(dialog->error), "Could not parse selected INI");
                app_options_destroy(&parsed);
            }
            dialog->prompt = FRONTEND_INI_PROMPT_NONE;
            nk_popup_close(ctx);
        }
        if (nk_button_label(ctx, "No")) {
            dialog->prompt = FRONTEND_INI_PROMPT_NONE;
            nk_popup_close(ctx);
        }
        if (nk_button_label(ctx, "Cancel")) {
            app_options_set_string(&dialog->edited.ini_path, dialog->previous_ini_path);
            frontend_config_reserve_string(&dialog->edited.ini_path, 1024);
            dialog->save_ini_on_quit = dialog->previous_save_ini;
            dialog->prompt = FRONTEND_INI_PROMPT_NONE;
            nk_popup_close(ctx);
        }
        nk_popup_end(ctx);
    }
    (void)ui;
}

static void frontend_draw_config_dialog(frontend *ui, int width, int height)
{
    frontend_config_dialog_state *dialog;
    struct nk_context *ctx;
    struct nk_rect bounds;

    if (ui == NULL || !ui->config_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dialog = &ui->config_dialog;
    bounds = nk_rect((float)(width - 560) * 0.5f, (float)(height - 500) * 0.5f, 560.0f, 500.0f);
    if (bounds.x < 8.0f) {
        bounds.x = 8.0f;
    }
    if (bounds.y < 8.0f) {
        bounds.y = 8.0f;
    }

    if (nk_begin(ctx, "Configure", bounds, NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Configure")) {
            nk_layout_row_dynamic(ctx, 24.0f, 2);
            frontend_draw_config_tab_button(ui, FRONTEND_CONFIG_TAB_MACHINE, "Machine");
            frontend_draw_config_tab_button(ui, FRONTEND_CONFIG_TAB_EMULATOR, "Emulator");

            nk_layout_row_dynamic(ctx, 250.0f, 1);
            if (nk_group_begin(ctx, "config-tab-body", NK_WINDOW_BORDER)) {
                if (dialog->active_tab == FRONTEND_CONFIG_TAB_MACHINE) {
                    frontend_draw_config_machine_tab(dialog, ctx);
                } else {
                    frontend_draw_config_emulator_tab(ui, dialog, ctx);
                }
                nk_group_end(ctx);
            }

            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "INI File", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.68f);
            if (dialog->edited.ini_path == NULL) {
                app_options_set_string(&dialog->edited.ini_path, "");
            }
            if (dialog->prompt == FRONTEND_INI_PROMPT_NONE) {
                frontend_copy_text(dialog->previous_ini_path, sizeof(dialog->previous_ini_path), dialog->edited.ini_path);
                dialog->previous_save_ini = dialog->save_ini_on_quit;
            }
            {
                nk_flags ini_result = frontend_edit_replace(
                    ctx,
                    NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                    dialog->edited.ini_path,
                    1024,
                    nk_filter_default);
                if (ini_result & NK_EDIT_COMMITED) {
                    frontend_config_commit_ini_path(dialog);
                    if (frontend_file_exists(dialog->edited.ini_path)) {
                        dialog->prompt = FRONTEND_INI_PROMPT_EXISTING;
                    }
                    nk_edit_unfocus(ctx);
                }
            }
            nk_layout_row_push(ctx, 0.14f);
            if (nk_button_label(ctx, "...")) {
                frontend_copy_text(dialog->previous_ini_path, sizeof(dialog->previous_ini_path), dialog->edited.ini_path);
                dialog->previous_save_ini = dialog->save_ini_on_quit;
                frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG);
            }
            nk_layout_row_end(ctx);

            nk_layout_row_dynamic(ctx, 20.0f, 1);
            if (dialog->edited.no_save_ini) {
                nk_label(ctx, "Save INI on Quit disabled by --nosaveini", NK_TEXT_LEFT);
            } else {
                frontend_checkbox_bool(ctx, "Save INI on Quit", &dialog->save_ini_on_quit);
            }

            if (dialog->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dialog->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "OK") && frontend_config_validate(dialog)) {
                frontend_config_apply_result result = {
                    .accepted = true,
                    .needs_reboot = frontend_config_standard_changed(&dialog->original, &dialog->edited),
                    .save_ini_on_quit = dialog->save_ini_on_quit,
                    .symbols_changed = frontend_config_symbols_changed(&dialog->original, &dialog->edited),
                };
                if (frontend_push_config_apply_intent(ui, &dialog->edited, &result)) {
                    dialog->open = false;
                }
            }
            if (nk_button_label(ctx, "Cancel")) {
                dialog->open = false;
            }
            frontend_draw_config_existing_ini_prompt(ui, dialog, ctx);
        } else {
            dialog->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Configure")) {
        dialog->open = false;
    }
    nk_end(ctx);
}

static void frontend_push_debugger_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint16_t value)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].value = value;
    ui->intent_write = next;
}

static void frontend_push_breakpoint_id_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    bool enabled)
{
    size_t next;

    if (ui == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].id = id;
    ui->intents[ui->intent_write].enabled = enabled;
    ui->intent_write = next;
}

static void frontend_push_breakpoint_definition_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    const runtime_breakpoint_definition *definition)
{
    size_t next;

    if (ui == NULL || definition == NULL || type == FRONTEND_DEBUGGER_INTENT_NONE) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].id = id;
    ui->intents[ui->intent_write].breakpoint = *definition;
    ui->intent_write = next;
}

static void frontend_push_memory_request(
    frontend *ui,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode)
{
    size_t next;

    if (ui == NULL || length == 0) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY;
    ui->intents[ui->intent_write].address = address;
    ui->intents[ui->intent_write].length = length;
    ui->intents[ui->intent_write].value = 0;
    ui->intents[ui->intent_write].memory_mode = mode;
    ui->intent_write = next;
}

static void frontend_push_memory_view_request(
    frontend *ui,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode)
{
    size_t next;

    if (ui == NULL || length == 0) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW;
    ui->intents[ui->intent_write].address = address;
    ui->intents[ui->intent_write].length = length;
    ui->intents[ui->intent_write].value = 0;
    ui->intents[ui->intent_write].memory_mode = mode;
    ui->intent_write = next;
}

static void frontend_push_memory_write_byte(
    frontend *ui,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode)
{
    size_t next;

    if (ui == NULL) {
        return;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return;
    }

    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE;
    ui->intents[ui->intent_write].address = address;
    ui->intents[ui->intent_write].length = 1;
    ui->intents[ui->intent_write].value = value;
    ui->intents[ui->intent_write].memory_mode = mode;
    ui->intent_write = next;
}

static void frontend_format_register_buffers(
    frontend_register_view_state *state,
    const runtime_cpu_snapshot *cpu,
    frontend_register_field except)
{
    static const uint8_t flag_bits[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    size_t i;

    if (state == NULL || cpu == NULL) {
        return;
    }

    if (except != FRONTEND_REGISTER_FIELD_PC) {
        snprintf(state->pc, sizeof(state->pc), "%04X", cpu->pc);
    }
    if (except != FRONTEND_REGISTER_FIELD_SP) {
        snprintf(state->sp, sizeof(state->sp), "%02X", cpu->sp);
    }
    if (except != FRONTEND_REGISTER_FIELD_A) {
        snprintf(state->a, sizeof(state->a), "%02X", cpu->a);
    }
    if (except != FRONTEND_REGISTER_FIELD_X) {
        snprintf(state->x, sizeof(state->x), "%02X", cpu->x);
    }
    if (except != FRONTEND_REGISTER_FIELD_Y) {
        snprintf(state->y, sizeof(state->y), "%02X", cpu->y);
    }

    for (i = 0; i < 8; ++i) {
        frontend_register_field field = (frontend_register_field)(
            FRONTEND_REGISTER_FIELD_STATUS_N + (int)i);

        if (except == field) {
            continue;
        }

        state->flags[i][0] = (cpu->p & (uint8_t)(1u << flag_bits[i])) ? '1' : '0';
        state->flags[i][1] = '\0';
    }
}

static int frontend_hex_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static float frontend_memory_char_width(frontend *ui);

static bool frontend_parse_hex(const char *text, size_t max_digits, uint16_t *out)
{
    uint16_t value = 0;
    size_t i;
    size_t length;

    if (text == NULL || out == NULL) {
        return false;
    }

    length = strlen(text);
    if (length == 0 || length > max_digits) {
        return false;
    }

    for (i = 0; i < length; ++i) {
        int digit = frontend_hex_digit_value(text[i]);

        if (digit < 0) {
            return false;
        }

        value = (uint16_t)((value << 4) | (uint16_t)digit);
    }

    *out = value;
    return true;
}

static bool frontend_parse_flag(const char *text, uint8_t *out)
{
    if (text == NULL || out == NULL || text[0] == '\0' || text[1] != '\0') {
        return false;
    }
    if (text[0] != '0' && text[0] != '1') {
        return false;
    }

    *out = (uint8_t)(text[0] - '0');
    return true;
}

static void frontend_commit_register_edit(
    frontend *ui,
    frontend_register_field field,
    const frontend_debug_state *debug_state)
{
    static const uint8_t flag_bits[8] = {7, 6, 5, 4, 3, 2, 1, 0};
    frontend_register_view_state *state;
    uint16_t value;
    uint8_t flag_value;

    if (ui == NULL || debug_state == NULL || !debug_state->has_cpu) {
        return;
    }

    state = &ui->registers;
    if (debug_state->runtime_state != FRONTEND_RUNTIME_STATE_PAUSED) {
        frontend_format_register_buffers(state, &debug_state->cpu, FRONTEND_REGISTER_FIELD_NONE);
        return;
    }

    switch (field) {
        case FRONTEND_REGISTER_FIELD_PC:
            if (frontend_parse_hex(state->pc, 4, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_SP:
            if (frontend_parse_hex(state->sp, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_A:
            if (frontend_parse_hex(state->a, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_X:
            if (frontend_parse_hex(state->x, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_Y:
            if (frontend_parse_hex(state->y, 2, &value)) {
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y, value);
                return;
            }
            break;

        case FRONTEND_REGISTER_FIELD_STATUS_N:
        case FRONTEND_REGISTER_FIELD_STATUS_V:
        case FRONTEND_REGISTER_FIELD_STATUS_UNUSED:
        case FRONTEND_REGISTER_FIELD_STATUS_B:
        case FRONTEND_REGISTER_FIELD_STATUS_D:
        case FRONTEND_REGISTER_FIELD_STATUS_I:
        case FRONTEND_REGISTER_FIELD_STATUS_Z:
        case FRONTEND_REGISTER_FIELD_STATUS_C: {
            size_t flag_index = (size_t)(field - FRONTEND_REGISTER_FIELD_STATUS_N);

            if (frontend_parse_flag(state->flags[flag_index], &flag_value)) {
                uint8_t mask = (uint8_t)(1u << flag_bits[flag_index]);
                uint8_t status = (uint8_t)(debug_state->cpu.p & (uint8_t)~mask);

                if (flag_value != 0) {
                    status = (uint8_t)(status | mask);
                }
                frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS, status);
                return;
            }
            break;
        }

        case FRONTEND_REGISTER_FIELD_NONE:
        default:
            break;
    }

    frontend_format_register_buffers(state, &debug_state->cpu, FRONTEND_REGISTER_FIELD_NONE);
}

static void frontend_draw_display_placeholder(frontend *ui, struct nk_rect bounds)
{
    if (nk_begin(ui->ctx, "Commodore Display", bounds, pane_flags)) {
        struct nk_rect canvas_bounds;
        struct nk_command_buffer *canvas;

        nk_layout_row_dynamic(ui->ctx, bounds.h - 52.0f, 1);
        canvas_bounds = nk_widget_bounds(ui->ctx);
        canvas = nk_window_get_canvas(ui->ctx);
        nk_fill_rect(canvas, canvas_bounds, 0.0f, nk_rgb(17, 22, 28));

        if (ui->has_frame && ui->display_texture != NULL) {
            struct nk_image image = nk_subimage_handle(
                nk_handle_ptr(ui->display_texture),
                (nk_ushort)ui->current_frame.width,
                (nk_ushort)ui->current_frame.height,
                nk_rect(
                    (float)FRONTEND_DISPLAY_CROP_X,
                    (float)FRONTEND_DISPLAY_CROP_Y,
                    (float)FRONTEND_DISPLAY_CROP_W,
                    (float)FRONTEND_DISPLAY_CROP_H));
            struct nk_rect image_bounds = frontend_fit_nk_rect(
                canvas_bounds,
                FRONTEND_DISPLAY_CROP_W,
                FRONTEND_DISPLAY_CROP_H);

            nk_draw_image(canvas, image_bounds, &image, nk_rgba(255, 255, 255, 255));
            nk_stroke_rect(canvas, image_bounds, 0.0f, 1.0f, nk_rgb(75, 94, 112));
        } else {
            nk_stroke_rect(canvas, canvas_bounds, 0.0f, 1.0f, nk_rgb(75, 94, 112));
            nk_draw_text(
                canvas,
                nk_rect(canvas_bounds.x + 14.0f, canvas_bounds.y + 14.0f, canvas_bounds.w - 28.0f, 20.0f),
                "waiting for frame",
                17,
                ui->ctx->style.font,
                nk_rgb(17, 22, 28),
                nk_rgb(196, 214, 228));
        }

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_C64) {
            frontend_draw_active_view_border(ui->ctx);
        }
    }
    nk_end(ui->ctx);
}

static void frontend_draw_register_edit(
    frontend *ui,
    frontend_register_field field,
    char *buffer,
    int max,
    nk_plugin_filter filter,
    const frontend_debug_state *debug_state,
    bool editable)
{
    /* NK_EDIT_FIELD includes NK_EDIT_ALWAYS_INSERT_MODE which forces insert
       mode every frame. Use the component flags directly so we can stay in
       replace (overwrite) mode. */
    nk_flags edit_flags = NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD | NK_EDIT_SIG_ENTER;
    nk_flags result;

    if (!editable) {
        edit_flags |= NK_EDIT_READ_ONLY;
    }

    result = nk_edit_string_zero_terminated(ui->ctx, edit_flags, buffer, max, filter);
    if ((result & NK_EDIT_ACTIVE) != 0 && editable) {
        /* Keep the widget in replace (overwrite) mode so the user can type a
           new value directly without deleting the old one first. */
        ui->ctx->current->edit.mode = NK_TEXT_EDIT_MODE_REPLACE;
    }
    if ((result & NK_EDIT_ACTIVATED) != 0 && editable) {
        ui->registers.active_field = field;
    }
    if ((result & NK_EDIT_COMMITED) != 0) {
        frontend_commit_register_edit(ui, field, debug_state);
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
        /* NK_EDIT_SIG_ENTER signals the commit but does not deactivate the
           widget; call unfocus so the field loses the cursor immediately. */
        nk_edit_unfocus(ui->ctx);
    }
}

static void frontend_draw_register_pair(
    frontend *ui,
    const char *label,
    frontend_register_field field,
    char *buffer,
    int max,
    float label_w,
    float edit_w,
    const frontend_debug_state *debug_state,
    bool editable)
{
    nk_layout_row_push(ui->ctx, label_w);
    nk_label(ui->ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ui->ctx, edit_w);
    frontend_draw_register_edit(ui, field, buffer, max, nk_filter_hex, debug_state, editable);
}

static void frontend_draw_flag_pair(
    frontend *ui,
    const char *label,
    size_t index,
    const frontend_debug_state *debug_state,
    bool editable)
{
    nk_layout_row_push(ui->ctx, 0.055f);
    nk_label(ui->ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ui->ctx, 0.070f);
    frontend_draw_register_edit(
        ui,
        (frontend_register_field)(FRONTEND_REGISTER_FIELD_STATUS_N + (int)index),
        ui->registers.flags[index],
        (int)sizeof(ui->registers.flags[index]),
        nk_filter_binary,
        debug_state,
        editable);
}

static void frontend_draw_registers(
    frontend *ui,
    struct nk_rect bounds,
    const frontend_debug_state *debug_state)
{
    bool editable;

    if (debug_state == NULL) {
        return;
    }

    editable = debug_state->has_cpu &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED;

    if (!editable) {
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
    }

    if (debug_state->has_cpu) {
        frontend_format_register_buffers(
            &ui->registers,
            &debug_state->cpu,
            ui->registers.active_field);
    }

    if (ui->cancel_register_edit_requested) {
        if (debug_state->has_cpu) {
            frontend_format_register_buffers(
                &ui->registers,
                &debug_state->cpu,
                FRONTEND_REGISTER_FIELD_NONE);
        }
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
        ui->cancel_register_edit_requested = false;
    }

    if (nk_begin(ui->ctx, "CPU", bounds, pane_flags)) {
        if (!debug_state->has_cpu) {
            nk_layout_row_dynamic(ui->ctx, 20.0f, 1);
            nk_label(ui->ctx, frontend_runtime_state_name(debug_state->runtime_state), NK_TEXT_LEFT);
            nk_label(ui->ctx, "PC ----  SP --  A --  X --  Y --", NK_TEXT_LEFT);
            nk_label(ui->ctx, "N -  V -  - -  B -  D -  I -  Z -  C -", NK_TEXT_LEFT);
        } else {
            nk_layout_row_begin(ui->ctx, NK_DYNAMIC, 22.0f, 10);
            frontend_draw_register_pair(
                ui,
                "PC",
                FRONTEND_REGISTER_FIELD_PC,
                ui->registers.pc,
                (int)sizeof(ui->registers.pc),
                0.07f,
                0.19f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "SP",
                FRONTEND_REGISTER_FIELD_SP,
                ui->registers.sp,
                (int)sizeof(ui->registers.sp),
                0.07f,
                0.13f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "A",
                FRONTEND_REGISTER_FIELD_A,
                ui->registers.a,
                (int)sizeof(ui->registers.a),
                0.05f,
                0.12f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "X",
                FRONTEND_REGISTER_FIELD_X,
                ui->registers.x,
                (int)sizeof(ui->registers.x),
                0.05f,
                0.12f,
                debug_state,
                editable);
            frontend_draw_register_pair(
                ui,
                "Y",
                FRONTEND_REGISTER_FIELD_Y,
                ui->registers.y,
                (int)sizeof(ui->registers.y),
                0.05f,
                0.12f,
                debug_state,
                editable);
            nk_layout_row_end(ui->ctx);

            nk_layout_row_begin(ui->ctx, NK_DYNAMIC, 22.0f, 16);
            frontend_draw_flag_pair(ui, "N", 0, debug_state, editable);
            frontend_draw_flag_pair(ui, "V", 1, debug_state, editable);
            frontend_draw_flag_pair(ui, "-", 2, debug_state, editable);
            frontend_draw_flag_pair(ui, "B", 3, debug_state, editable);
            frontend_draw_flag_pair(ui, "D", 4, debug_state, editable);
            frontend_draw_flag_pair(ui, "I", 5, debug_state, editable);
            frontend_draw_flag_pair(ui, "Z", 6, debug_state, editable);
            frontend_draw_flag_pair(ui, "C", 7, debug_state, editable);
            nk_layout_row_end(ui->ctx);
        }
    }
    nk_end(ui->ctx);
}

static int frontend_disassembly_snapshot_index(
    const frontend_disassembly_view_state *view,
    uint16_t address)
{
    uint16_t offset;

    if (view == NULL ||
        !view->has_snapshot ||
        view->snapshot.mode != view->mode) {
        return -1;
    }

    offset = (uint16_t)(address - view->snapshot.address);
    if (offset >= view->snapshot.length) {
        return -1;
    }

    return (int)offset;
}

static void frontend_disassembly_decode(frontend *ui)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    uint16_t address = view->top_address;
    uint8_t row;

    for (row = 0; row < view->rows && row < FRONTEND_DISASM_MAX_ROWS; ++row) {
        int index = frontend_disassembly_snapshot_index(view, address);
        const uint8_t *bytes = NULL;
        size_t available = 0;

        if (index >= 0) {
            bytes = &view->snapshot.bytes[index];
            available = view->snapshot.length - (size_t)index;
        }

        view->lines[row] = disasm_6502_decode_line(address, bytes, available, &ui->symbols);
        address = (uint16_t)(address + view->lines[row].length);
    }
}

static int frontend_disassembly_find_row(
    const frontend_disassembly_view_state *view,
    uint16_t address)
{
    uint8_t row;

    if (view == NULL) {
        return -1;
    }

    for (row = 0; row < view->rows && row < FRONTEND_DISASM_MAX_ROWS; ++row) {
        if (view->lines[row].address == address) {
            return (int)row;
        }
    }

    return -1;
}

static void frontend_disassembly_set_user_cursor(
    frontend_disassembly_view_state *view,
    uint16_t address,
    uint8_t row,
    uint8_t length)
{
    if (view == NULL) {
        return;
    }

    view->has_user_cursor = true;
    view->cursor_address = address;
    view->cursor_row = row;
    view->cursor_length = length == 0 ? 1 : length;
    if (row > 0 &&
        row < view->rows &&
        view->lines[row].address == address) {
        view->cursor_prev_address = view->lines[row - 1u].address;
    } else {
        view->cursor_prev_address = (uint16_t)(address - 1u);
    }
}

static uint16_t frontend_disassembly_center_top(uint16_t address, uint8_t rows)
{
    uint8_t back = rows > 0 ? rows / 2 : 0;
    uint8_t i;
    uint16_t top = address;

    if (back > FRONTEND_DISASM_CENTER_LOOKBACK) {
        back = FRONTEND_DISASM_CENTER_LOOKBACK;
    }

    for (i = 0; i < back; ++i) {
        top = (uint16_t)(top - 1u);
    }

    return top;
}

static bool frontend_disassembly_previous_address(
    const frontend_disassembly_view_state *view,
    uint16_t address,
    uint16_t *out_previous)
{
    int back;
    uint16_t invalid_candidate = 0;
    bool has_invalid_candidate = false;

    if (view == NULL || out_previous == NULL) {
        return false;
    }

    for (back = 3; back >= 1; --back) {
        uint16_t candidate = (uint16_t)(address - back);
        int index = frontend_disassembly_snapshot_index(view, candidate);
        uint8_t opcode;
        uint8_t length;

        if (index < 0) {
            continue;
        }

        opcode = view->snapshot.bytes[index];
        length = disasm_6502_instruction_length(opcode);
        if (length == back &&
            frontend_disassembly_snapshot_index(view, (uint16_t)(candidate + length - 1u)) >= 0) {
            if (disasm_6502_opcode_is_valid(opcode)) {
                *out_previous = candidate;
                return true;
            }
            if (!has_invalid_candidate) {
                invalid_candidate = candidate;
                has_invalid_candidate = true;
            }
        }
    }

    if (has_invalid_candidate) {
        *out_previous = invalid_candidate;
        return true;
    }

    if (frontend_disassembly_snapshot_index(view, (uint16_t)(address - 1u)) >= 0) {
        *out_previous = (uint16_t)(address - 1u);
        return true;
    }

    return false;
}

static bool frontend_disassembly_pc_locked_top(
    const frontend_disassembly_view_state *view,
    uint16_t pc,
    uint16_t *out_top)
{
    uint8_t target_row;
    uint8_t i;
    uint16_t address;

    if (view == NULL || out_top == NULL || view->rows == 0) {
        return false;
    }

    target_row = view->rows / 2;
    address = pc;
    for (i = 0; i < target_row; ++i) {
        uint16_t previous;

        if (!frontend_disassembly_previous_address(view, address, &previous)) {
            previous = (uint16_t)(address - 1u);
        }
        address = previous;
    }

    *out_top = address;
    return true;
}

static bool frontend_disassembly_snapshot_covers(
    const frontend_disassembly_view_state *view,
    uint16_t address,
    uint16_t length)
{
    uint16_t end_offset;

    if (view == NULL ||
        !view->has_snapshot ||
        view->snapshot.mode != view->mode) {
        return false;
    }

    end_offset = (uint16_t)(address - view->snapshot.address + length);
    return (uint16_t)(address - view->snapshot.address) < view->snapshot.length &&
        end_offset <= view->snapshot.length;
}

static uint16_t frontend_disassembly_visible_decode_length(const frontend_disassembly_view_state *view)
{
    uint16_t length;

    if (view == NULL) {
        return 16;
    }

    length = (uint16_t)((uint16_t)view->rows * 3u + 16u);
    if (length > FRONTEND_DISASM_FETCH_BYTES) {
        return FRONTEND_DISASM_FETCH_BYTES;
    }
    return length;
}

static void frontend_disassembly_request_if_needed(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    uint16_t length = FRONTEND_DISASM_FETCH_BYTES;
    uint16_t request_address = view->pc_lock_active ?
        (uint16_t)(view->pc_lock_address - (uint16_t)(view->rows * 3u + 8u)) :
        view->top_address;

    if (debug_state != NULL &&
        debug_state->has_memory &&
        debug_state->memory.mode == view->mode &&
        debug_state->memory.address == request_address) {
        view->snapshot = debug_state->memory;
        view->has_snapshot = true;
    }

    if (!view->request_pending ||
        view->requested_address != request_address ||
        view->requested_length != length ||
        view->requested_mode != view->mode ||
        !frontend_disassembly_snapshot_covers(view, request_address, length)) {
        frontend_push_memory_request(ui, request_address, length, view->mode);
        view->requested_address = request_address;
        view->requested_length = length;
        view->requested_mode = view->mode;
        view->request_pending = true;
    }
}

static void frontend_disassembly_follow_pc(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    if (debug_state == NULL || !debug_state->has_cpu) {
        return;
    }

    view->pc_lock_address = debug_state->cpu.pc;
    if (!frontend_disassembly_pc_locked_top(view, debug_state->cpu.pc, &view->top_address)) {
        view->top_address = frontend_disassembly_center_top(debug_state->cpu.pc, view->rows);
    }
    view->address_entry = false;
    view->follow_pc = true;
    view->pc_lock_active = true;
    view->has_user_cursor = false;
    view->request_pending = false;
}

static void frontend_disassembly_center_pc(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    if (debug_state == NULL || !debug_state->has_cpu) {
        return;
    }

    view->pc_lock_address = debug_state->cpu.pc;
    if (!frontend_disassembly_pc_locked_top(view, debug_state->cpu.pc, &view->top_address)) {
        view->top_address = frontend_disassembly_center_top(debug_state->cpu.pc, view->rows);
    }
    view->pc_lock_active = true;
    view->request_pending = false;
}

static void frontend_disassembly_center_cursor(frontend *ui)
{
    frontend_disassembly_view_state *view = &ui->disassembly;

    if (!view->has_user_cursor) {
        return;
    }

    view->top_address = frontend_disassembly_center_top(view->cursor_address, view->rows);
    view->cursor_row = view->rows / 2;
    view->request_pending = false;
}

static void frontend_disassembly_ensure_user_cursor(
    frontend *ui,
    const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    int pc_row;

    if (view->has_user_cursor) {
        return;
    }

    if (debug_state != NULL && debug_state->has_cpu) {
        pc_row = frontend_disassembly_find_row(view, debug_state->cpu.pc);
        if (pc_row >= 0) {
            frontend_disassembly_set_user_cursor(
                view,
                debug_state->cpu.pc,
                (uint8_t)pc_row,
                view->lines[pc_row].length);
        } else {
            frontend_disassembly_set_user_cursor(
                view,
                debug_state->cpu.pc,
                view->rows / 2,
                1);
        }
        return;
    }

    frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
}

static void frontend_disassembly_scroll_to_top(frontend *ui, uint16_t address)
{
    ui->disassembly.top_address = address;
    frontend_disassembly_set_user_cursor(&ui->disassembly, address, 0, 1);
    ui->disassembly.request_pending = false;
    ui->disassembly.follow_pc = false;
    ui->disassembly.pc_lock_active = false;
}

static void frontend_disassembly_apply_address_digit(frontend *ui, int digit)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    int shift;
    uint16_t mask;

    if (digit < 0 || digit > 15) {
        return;
    }

    shift = (3 - view->active_address_digit) * 4;
    mask = (uint16_t)(0x0fu << shift);
    view->cursor_address = (uint16_t)((view->cursor_address & (uint16_t)~mask) |
        (uint16_t)((uint16_t)digit << shift));
    view->top_address = frontend_disassembly_center_top(view->cursor_address, view->rows);
    view->request_pending = false;
    view->pc_lock_active = false;

    if (view->active_address_digit >= 3) {
        view->address_entry = false;
        view->active_address_digit = 0;
    } else {
        view->active_address_digit++;
    }
}

static void frontend_disassembly_handle_key(
    frontend *ui,
    const frontend_debug_state *debug_state,
    const SDL_KeyboardEvent *key)
{
    frontend_disassembly_view_state *view;
    SDL_Keycode sym;
    SDL_Keymod mod;
    bool alt;
    int row;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN) {
        return;
    }

    view = &ui->disassembly;
    if (debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        return;
    }

    sym = key->keysym.sym;
    mod = key->keysym.mod;
    alt = (mod & KMOD_ALT) != 0;

    if (alt && sym == SDLK_b) {
        frontend_disassembly_ensure_user_cursor(ui, debug_state);
        frontend_toggle_execute_breakpoint_at_cursor(ui, debug_state);
        return;
    }

    if (sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 || sym == SDLK_F12) {
        return;
    }

    if (alt && sym == SDLK_a) {
        frontend_disassembly_ensure_user_cursor(ui, debug_state);
        view->address_entry = !view->address_entry;
        view->active_address_digit = 0;
        return;
    }

    if (alt && sym == SDLK_s) {
        if (ui->symbols.enumerate != NULL) {
            (void)ui->symbols.enumerate(ui->symbols.userdata, NULL, 0);
        }
        return;
    }

    if (alt && sym == SDLK_m) {
        if (view->mode == RUNTIME_MEMORY_MODE_CPU_MAP) {
            view->mode = RUNTIME_MEMORY_MODE_ROM;
        } else if (view->mode == RUNTIME_MEMORY_MODE_ROM) {
            view->mode = RUNTIME_MEMORY_MODE_RAM;
        } else {
            view->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        }
        view->request_pending = false;
        return;
    }

    if (sym == SDLK_TAB) {
        if ((mod & KMOD_SHIFT) != 0) {
            view->symbol_display_mode = (uint8_t)((view->symbol_display_mode + 2u) % 3u);
        } else {
            view->symbol_display_mode = (uint8_t)((view->symbol_display_mode + 1u) % 3u);
        }
        return;
    }

    if (view->address_entry) {
        if (sym == SDLK_RETURN) {
            view->address_entry = false;
            return;
        }
        if (sym == SDLK_HOME) {
            view->active_address_digit = 0;
            return;
        }
        if (sym == SDLK_END) {
            view->active_address_digit = 3;
            return;
        }
        if (sym == SDLK_LEFT) {
            if (view->active_address_digit > 0) {
                view->active_address_digit--;
            }
            return;
        }
        if (sym == SDLK_RIGHT) {
            if (view->active_address_digit >= 3) {
                view->address_entry = false;
                view->active_address_digit = 0;
            } else {
                view->active_address_digit++;
            }
            return;
        }
        frontend_disassembly_apply_address_digit(ui, frontend_hex_digit_value((char)sym));
        return;
    }

    frontend_disassembly_ensure_user_cursor(ui, debug_state);
    row = frontend_disassembly_find_row(view, view->cursor_address);

    if (sym == SDLK_PAGEUP) {
        uint16_t old_top = view->lines[0].address;
        view->top_address = (uint16_t)(old_top - (uint16_t)(view->rows > 0 ? view->rows - 1u : 0u));
        frontend_disassembly_set_user_cursor(
            view,
            old_top,
            view->rows > 0 ? view->rows - 1u : 0,
            view->lines[0].length);
        view->request_pending = false;
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_PAGEDOWN) {
        uint8_t last = view->rows > 0 ? view->rows - 1u : 0;
        view->top_address = view->lines[last].address;
        frontend_disassembly_set_user_cursor(view, view->top_address, 0, view->lines[last].length);
        view->request_pending = false;
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_HOME) {
        if (alt) {
            frontend_disassembly_scroll_to_top(ui, 0x0000);
        } else if (view->rows > 0) {
            frontend_disassembly_set_user_cursor(view, view->lines[0].address, 0, view->lines[0].length);
        }
        return;
    }

    if (sym == SDLK_END) {
        if (alt) {
            view->top_address = 0xffff;
            frontend_disassembly_set_user_cursor(
                view,
                0xffff,
                view->rows > 0 ? view->rows - 1u : 0,
                1);
            view->request_pending = false;
            view->follow_pc = false;
            view->pc_lock_active = false;
        } else if (view->rows > 0) {
            uint8_t last = view->rows - 1u;
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[last].address,
                last,
                view->lines[last].length);
        }
        return;
    }

    if (sym == SDLK_UP) {
        if (row > 0) {
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[row - 1].address,
                (uint8_t)(row - 1),
                view->lines[row - 1].length);
        } else if (row < 0) {
            frontend_disassembly_set_user_cursor(view, view->cursor_prev_address, view->rows / 2, 1);
            frontend_disassembly_center_cursor(ui);
        } else {
            uint16_t previous = view->cursor_prev_address;
            view->top_address = previous;
            frontend_disassembly_set_user_cursor(view, previous, 0, 1);
            view->request_pending = false;
        }
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_DOWN) {
        if (row >= 0 && row + 1 < view->rows) {
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[row + 1].address,
                (uint8_t)(row + 1),
                view->lines[row + 1].length);
        } else if (row < 0) {
            uint16_t next = (uint16_t)(view->cursor_address + view->cursor_length);
            frontend_disassembly_set_user_cursor(view, next, view->rows / 2, 1);
            frontend_disassembly_center_cursor(ui);
        } else if (view->rows > 0) {
            view->top_address = view->rows > 1 ? view->lines[1].address :
                (uint16_t)(view->top_address + 1u);
            frontend_disassembly_set_user_cursor(
                view,
                view->lines[view->rows - 1u].address,
                view->rows - 1u,
                view->lines[view->rows - 1u].length);
            view->request_pending = false;
        }
        view->follow_pc = false;
        view->pc_lock_active = false;
        return;
    }

    if (sym == SDLK_LEFT) {
        if (alt && debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED) {
            frontend_push_debugger_intent(ui, FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC, view->cursor_address);
        } else if (row < 0) {
            frontend_disassembly_center_cursor(ui);
        }
        return;
    }

    if (sym == SDLK_RIGHT) {
        frontend_disassembly_follow_pc(ui, debug_state);
        return;
    }
}

static char frontend_disassembly_line_char_at(const char *line, int index)
{
    size_t length;

    if (line == NULL || index < 0) {
        return ' ';
    }

    length = strlen(line);
    if ((size_t)index >= length) {
        return ' ';
    }

    return line[index];
}

static void frontend_disassembly_draw_address_cursor(
    frontend *ui,
    struct nk_rect row_bounds,
    const char *line)
{
    struct nk_command_buffer *canvas;
    float char_w;
    struct nk_rect cursor_rect;
    char text[2];
    int text_col;

    if (ui == NULL || line == NULL || !ui->disassembly.active || !ui->disassembly.address_entry) {
        return;
    }

    text_col = 3 + ui->disassembly.active_address_digit;
    char_w = frontend_memory_char_width(ui);
    cursor_rect = nk_rect(
        row_bounds.x + char_w * (float)text_col,
        row_bounds.y + 1.0f,
        char_w,
        row_bounds.h - 2.0f);
    text[0] = frontend_disassembly_line_char_at(line, text_col);
    text[1] = '\0';

    canvas = nk_window_get_canvas(ui->ctx);
    nk_fill_rect(canvas, cursor_rect, 0.0f, nk_rgb(255, 244, 120));
    nk_draw_text(
        canvas,
        cursor_rect,
        text,
        1,
        ui->ctx->style.font,
        nk_rgb(255, 244, 120),
        nk_rgb(20, 24, 28));
}

static void frontend_disassembly_handle_mouse_row(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect row_bounds,
    uint8_t row)
{
    float char_w;
    float rel_x;
    int text_col;

    if (!nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) ||
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, row_bounds)) {
        return;
    }

    if (debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        return;
    }

    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
    frontend_disassembly_set_user_cursor(
        &ui->disassembly,
        ui->disassembly.lines[row].address,
        row,
        ui->disassembly.lines[row].length);

    char_w = frontend_memory_char_width(ui);
    rel_x = ui->ctx->input.mouse.pos.x - row_bounds.x;
    if (rel_x < 0.0f) {
        rel_x = 0.0f;
    }
    text_col = (int)(rel_x / char_w);
    if (text_col >= 3 && text_col <= 6) {
        ui->disassembly.address_entry = true;
        ui->disassembly.active_address_digit = (uint8_t)(text_col - 3);
    } else {
        ui->disassembly.address_entry = false;
    }
}

static void frontend_disassembly_draw_scrollbar(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect bounds)
{
    frontend_disassembly_view_state *view;
    struct nk_command_buffer *canvas;
    struct nk_rect track;
    struct nk_rect thumb;
    const struct nk_mouse *mouse;
    float visible_fraction;
    float thumb_h;
    float thumb_y;
    float usable_h;

    if (ui == NULL || bounds.h <= 0.0f) {
        return;
    }

    ui->disassembly_scrollbar_bounds = bounds;
    ui->has_disassembly_scrollbar_bounds = true;

    view = &ui->disassembly;
    canvas = nk_window_get_canvas(ui->ctx);
    mouse = &ui->ctx->input.mouse;
    track = nk_rect(bounds.x + 2.0f, bounds.y + 2.0f, bounds.w - 4.0f, bounds.h - 4.0f);

    visible_fraction = ((float)view->rows * 2.0f) / 65536.0f;
    thumb_h = track.h * visible_fraction;
    if (thumb_h < 16.0f) {
        thumb_h = 16.0f;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }

    usable_h = track.h - thumb_h;
    thumb_y = track.y + usable_h * ((float)view->top_address / 65535.0f);
    thumb = nk_rect(track.x + 2.0f, thumb_y, track.w - 4.0f, thumb_h);

    if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) &&
        nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !view->scrollbar_dragging) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        view->scrollbar_dragging = true;
        view->scrollbar_grab_offset = mouse->pos.y - thumb.y;
    }

    if (!nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT)) {
        view->scrollbar_dragging = false;
    }

    if (view->scrollbar_dragging) {
        float y = mouse->pos.y - view->scrollbar_grab_offset;
        float relative;

        if (y < track.y) {
            y = track.y;
        }
        if (y > track.y + usable_h) {
            y = track.y + usable_h;
        }

        relative = usable_h > 0.0f ? (y - track.y) / usable_h : 0.0f;
        if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            view->top_address = (uint16_t)(relative * 65535.0f);
            frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
            view->follow_pc = false;
            view->pc_lock_active = false;
            view->request_pending = false;
        }
    } else if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, track) &&
        nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb)) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            if (mouse->pos.y < thumb.y) {
                view->top_address = (uint16_t)(view->top_address - (uint16_t)(view->rows * 2u));
            } else {
                view->top_address = (uint16_t)(view->top_address + (uint16_t)(view->rows * 2u));
            }
            frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
            view->follow_pc = false;
            view->pc_lock_active = false;
            view->request_pending = false;
        }
    }

    nk_fill_rect(canvas, track, 0.0f, nk_rgb(35, 41, 47));
    nk_fill_rect(
        canvas,
        thumb,
        2.0f,
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) || view->scrollbar_dragging ?
            nk_rgb(160, 174, 186) :
            nk_rgb(103, 124, 139));
}

static void frontend_draw_disassembly_view(
    frontend *ui,
    struct nk_rect bounds,
    const frontend_debug_state *debug_state)
{
    frontend_disassembly_view_state *view = &ui->disassembly;
    struct nk_style_window saved_window_style;
    uint8_t row;
    uint8_t rows;
    float row_h;
    float scrollbar_w = 24.0f;
    float scrollbar_margin = 8.0f;
    float content_h;

    if (!view->initialized) {
        view->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        view->follow_pc = true;
        view->initialized = true;
        if (debug_state != NULL && debug_state->has_cpu) {
            view->top_address = debug_state->cpu.pc;
        }
    }

    row_h = ui->ctx->style.font != NULL ? ui->ctx->style.font->height : 13.0f;
    content_h = bounds.h - 28.0f;
    rows = (uint8_t)((content_h > row_h) ? (content_h / row_h) : 1);
    if (rows > 1) {
        rows--;
    }
    if (rows == 0) {
        rows = 1;
    }
    if (rows > FRONTEND_DISASM_MAX_ROWS) {
        rows = FRONTEND_DISASM_MAX_ROWS;
    }
    view->rows = rows;

    if (debug_state != NULL && debug_state->has_cpu) {
        bool pc_changed = !view->has_last_pc || view->last_pc != debug_state->cpu.pc;

        if (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
            frontend_disassembly_follow_pc(ui, debug_state);
        } else if (pc_changed) {
            frontend_disassembly_center_pc(ui, debug_state);
        } else if (view->follow_pc) {
            int pc_row = frontend_disassembly_find_row(view, debug_state->cpu.pc);
            if (pc_row < 0 || pc_row != rows / 2) {
                frontend_disassembly_center_pc(ui, debug_state);
            }
        }

        view->last_pc = debug_state->cpu.pc;
        view->has_last_pc = true;
    }
    if (debug_state != NULL) {
        view->last_runtime_state = debug_state->runtime_state;
    }

    if (!frontend_any_dialog_open(ui) &&
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) &&
        ui->ctx->input.mouse.scroll_delta.y != 0.0f) {
        if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
            int32_t lines = ui->ctx->input.mouse.scroll_delta.y > 0.0f ? -3 : 3;
            view->top_address = (uint16_t)(view->top_address + lines);
            view->request_pending = false;
            view->follow_pc = false;
            view->pc_lock_active = false;
            frontend_disassembly_set_user_cursor(view, view->top_address, 0, 1);
        }
    }

    if (ui->has_pending_disassembly_key) {
        if (view->active) {
            frontend_disassembly_handle_key(ui, debug_state, &ui->pending_disassembly_key);
        }
        ui->has_pending_disassembly_key = false;
    }

    frontend_disassembly_request_if_needed(ui, debug_state);
    if (view->pc_lock_active) {
        uint16_t locked_top;
        if (frontend_disassembly_pc_locked_top(view, view->pc_lock_address, &locked_top) &&
            locked_top != view->top_address) {
            view->top_address = locked_top;
            view->request_pending = false;
        }
    }
    if (frontend_disassembly_snapshot_covers(
            view,
            view->top_address,
            frontend_disassembly_visible_decode_length(view))) {
        frontend_disassembly_decode(ui);
    }

    if (nk_begin(ui->ctx, "Disassembly", bounds, pane_flags)) {
        saved_window_style = ui->ctx->style.window;
        ui->ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.group_padding = nk_vec2(0.0f, 0.0f);

        nk_layout_row_begin(ui->ctx, NK_STATIC, row_h * (float)rows, 3);
        nk_layout_row_push(ui->ctx, bounds.w - scrollbar_w - scrollbar_margin);
        if (nk_group_begin(ui->ctx, "disassembly-rows", NK_WINDOW_NO_SCROLLBAR)) {
            for (row = 0; row < rows; ++row) {
                disasm_6502_line *line = &view->lines[row];
                char bytes[16];
                char address_label[32] = "";
                char rendered[128];
                struct nk_rect row_bounds;
                const runtime_breakpoint_snapshot_entry *breakpoint =
                    frontend_find_execute_breakpoint(debug_state, line->address);
                bool is_pc = debug_state != NULL && debug_state->has_cpu && line->address == debug_state->cpu.pc;
                bool is_cursor = view->has_user_cursor && line->address == view->cursor_address && !is_pc;
                bool is_breakpoint = breakpoint != NULL;
                bool is_enabled_breakpoint = is_breakpoint && breakpoint->enabled != 0;
                struct nk_style_selectable saved_selectable = ui->ctx->style.selectable;
                nk_bool selected = is_cursor ? nk_true : nk_false;

                snprintf(bytes, sizeof(bytes), "%02X %s%s",
                    line->bytes[0],
                    line->length > 1 ? "" : "  ",
                    line->length > 1 ? "" : "  ");
                if (line->length == 2) {
                    snprintf(bytes, sizeof(bytes), "%02X %02X   ", line->bytes[0], line->bytes[1]);
                } else if (line->length >= 3) {
                    snprintf(bytes, sizeof(bytes), "%02X %02X %02X", line->bytes[0], line->bytes[1], line->bytes[2]);
                } else {
                    snprintf(bytes, sizeof(bytes), "%02X      ", line->bytes[0]);
                }

                if (ui->symbols.address_to_label != NULL) {
                    (void)ui->symbols.address_to_label(
                        ui->symbols.userdata,
                        line->address,
                        address_label,
                        sizeof(address_label));
                }
                address_label[15] = '\0';

                snprintf(rendered, sizeof(rendered), "%c%c %04X %-15s %-8s %s",
                    is_pc ? '>' : ' ',
                    is_breakpoint ? (is_enabled_breakpoint ? 'X' : 'x') : ' ',
                    line->address,
                    address_label,
                    bytes,
                    line->text);

                if (line->forced_byte) {
                    ui->ctx->style.selectable.normal = nk_style_item_color(nk_rgb(30, 34, 38));
                    ui->ctx->style.selectable.hover = nk_style_item_color(nk_rgb(39, 45, 51));
                    ui->ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(49, 78, 94));
                    ui->ctx->style.selectable.text_normal = nk_rgb(125, 136, 145);
                }
                if (is_pc) {
                    ui->ctx->style.selectable.normal = nk_style_item_color(nk_rgb(83, 73, 24));
                    ui->ctx->style.selectable.text_normal = nk_rgb(255, 244, 120);
                }
                if (is_breakpoint && !is_pc) {
                    ui->ctx->style.selectable.text_normal = is_enabled_breakpoint ?
                        nk_rgb(255, 151, 122) :
                        nk_rgb(169, 126, 202);
                }
                if (is_cursor) {
                    ui->ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(21, 91, 116));
                    ui->ctx->style.selectable.text_normal_active = nk_rgb(226, 246, 255);
                    ui->ctx->style.selectable.text_hover_active = nk_rgb(226, 246, 255);
                    ui->ctx->style.selectable.text_pressed_active = nk_rgb(226, 246, 255);
                }

                nk_layout_row_dynamic(ui->ctx, row_h, 1);
                row_bounds = nk_widget_bounds(ui->ctx);
                frontend_disassembly_handle_mouse_row(ui, debug_state, row_bounds, row);
                if (nk_selectable_label(ui->ctx, rendered, NK_TEXT_LEFT, &selected)) {
                    if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_RUNNING) {
                        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
                        frontend_disassembly_set_user_cursor(view, line->address, row, line->length);
                        view->address_entry = false;
                        view->follow_pc = false;
                        view->pc_lock_active = false;
                    }
                }
                frontend_disassembly_draw_address_cursor(ui, row_bounds, rendered);
                ui->ctx->style.selectable = saved_selectable;
            }
            nk_group_end(ui->ctx);
        }
        nk_layout_row_push(ui->ctx, scrollbar_w);
        if (nk_group_begin(ui->ctx, "disassembly-scrollbar", NK_WINDOW_NO_SCROLLBAR)) {
            struct nk_rect scrollbar_bounds = nk_window_get_content_region(ui->ctx);
            frontend_disassembly_draw_scrollbar(ui, debug_state, scrollbar_bounds);
            nk_group_end(ui->ctx);
        }
        nk_layout_row_push(ui->ctx, scrollbar_margin);
        nk_spacing(ui->ctx, 1);
        nk_layout_row_end(ui->ctx);

        if (nk_contextual_begin(ui->ctx, 0, nk_vec2(120.0f, 90.0f), bounds)) {
            nk_layout_row_dynamic(ui->ctx, 22, 1);
            if (nk_contextual_item_symbol_label(ui->ctx,
                    view->mode == RUNTIME_MEMORY_MODE_CPU_MAP ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_NONE,
                    "Map", NK_TEXT_LEFT)) {
                view->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
                view->request_pending = false;
            }
            if (nk_contextual_item_symbol_label(ui->ctx,
                    view->mode == RUNTIME_MEMORY_MODE_ROM ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_NONE,
                    "ROM", NK_TEXT_LEFT)) {
                view->mode = RUNTIME_MEMORY_MODE_ROM;
                view->request_pending = false;
            }
            if (nk_contextual_item_symbol_label(ui->ctx,
                    view->mode == RUNTIME_MEMORY_MODE_RAM ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_NONE,
                    "RAM", NK_TEXT_LEFT)) {
                view->mode = RUNTIME_MEMORY_MODE_RAM;
                view->request_pending = false;
            }
            nk_contextual_end(ui->ctx);
        }

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_DISASSEMBLY) {
            frontend_draw_active_view_border(ui->ctx);
        }
        frontend_draw_memory_mode_border(
            ui->ctx,
            view->mode,
            !frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_DISASSEMBLY);

        ui->ctx->style.window = saved_window_style;
    }
    nk_end(ui->ctx);
}

static uint16_t frontend_memory_visible_count(const frontend_memory_view_state *memory)
{
    return (uint16_t)((uint16_t)memory->columns * (uint16_t)memory->rows);
}

static bool frontend_memory_cursor_visible(const frontend_memory_view_state *memory)
{
    uint16_t offset = (uint16_t)(memory->cursor_address - memory->view_address);
    return offset < frontend_memory_visible_count(memory);
}

static void frontend_memory_recenter_cursor(frontend_memory_view_state *memory)
{
    uint16_t visible = frontend_memory_visible_count(memory);
    uint16_t offset;

    if (visible == 0 || memory->columns == 0 || frontend_memory_cursor_visible(memory)) {
        return;
    }

    offset = (uint16_t)(memory->cursor_address - memory->view_address);
    if (offset >= 0x8000u) {
        memory->view_address = (uint16_t)(memory->cursor_address - (memory->cursor_address % memory->columns));
    } else {
        uint16_t cursor_col = (uint16_t)(memory->cursor_address % memory->columns);
        memory->view_address = (uint16_t)(memory->cursor_address - cursor_col -
            (uint16_t)((memory->rows - 1u) * memory->columns));
    }
}

static int frontend_memory_snapshot_index(
    const runtime_memory_snapshot *snapshot,
    uint16_t address)
{
    uint16_t offset;

    if (snapshot == NULL) {
        return -1;
    }

    offset = (uint16_t)(address - snapshot->address);
    if (offset >= snapshot->length) {
        return -1;
    }

    return (int)offset;
}

static uint8_t frontend_memory_byte_at(
    const frontend_debug_state *debug_state,
    uint16_t address)
{
    int index;

    if (debug_state == NULL || !debug_state->has_memory_view) {
        return 0;
    }

    index = frontend_memory_snapshot_index(&debug_state->memory_view, address);
    if (index < 0) {
        return 0;
    }

    return debug_state->memory_view.bytes[index];
}

static char frontend_memory_ascii(uint8_t value)
{
    if (value >= 32 && value <= 126) {
        return (char)value;
    }

    return '.';
}

static void frontend_memory_request_if_needed(frontend *ui, const frontend_debug_state *debug_state)
{
    frontend_memory_view_state *memory = &ui->memory;
    uint16_t length = frontend_memory_visible_count(memory);

    if (length == 0) {
        return;
    }

    if (memory->request_pending &&
        debug_state != NULL &&
        debug_state->has_memory_view &&
        debug_state->memory_view.address == memory->requested_address &&
        debug_state->memory_view.length == memory->requested_length &&
        debug_state->memory_view.mode == (runtime_memory_mode)memory->requested_mode) {
        memory->request_pending = false;
    }

    if (!memory->request_pending ||
        memory->requested_address != memory->view_address ||
        memory->requested_length != length ||
        memory->requested_mode != memory->mode) {
        frontend_push_memory_view_request(ui, memory->view_address, length, memory->mode);
        memory->requested_address = memory->view_address;
        memory->requested_length = length;
        memory->requested_mode = memory->mode;
        memory->request_pending = true;
    }
}

static void frontend_memory_move_cursor(frontend *ui, int32_t delta)
{
    frontend_memory_view_state *memory = &ui->memory;

    memory->cursor_address = (uint16_t)(memory->cursor_address + delta);
    frontend_memory_recenter_cursor(memory);
    memory->request_pending = false;
}

static void frontend_memory_write_byte(
    frontend *ui,
    const frontend_debug_state *debug_state,
    uint16_t address,
    uint8_t value)
{
    if (debug_state == NULL || debug_state->runtime_state != FRONTEND_RUNTIME_STATE_PAUSED) {
        return;
    }

    frontend_push_memory_write_byte(ui, address, value, ui->memory.mode);
    ui->memory.request_pending = false;
}

static void frontend_memory_apply_hex_digit(
    frontend *ui,
    const frontend_debug_state *debug_state,
    int digit)
{
    uint8_t old_value;
    uint8_t new_value;

    if (digit < 0 || digit > 15) {
        return;
    }

    old_value = frontend_memory_byte_at(debug_state, ui->memory.cursor_address);
    if (ui->memory.active_nibble == 0) {
        new_value = (uint8_t)((old_value & 0x0fu) | (uint8_t)(digit << 4));
        frontend_memory_write_byte(ui, debug_state, ui->memory.cursor_address, new_value);
        ui->memory.active_nibble = 1;
    } else {
        new_value = (uint8_t)((old_value & 0xf0u) | (uint8_t)digit);
        frontend_memory_write_byte(ui, debug_state, ui->memory.cursor_address, new_value);
        ui->memory.active_nibble = 0;
        frontend_memory_move_cursor(ui, 1);
    }
}

static void frontend_memory_apply_address_digit(frontend *ui, int digit)
{
    int shift;
    uint16_t mask;

    if (digit < 0 || digit > 15) {
        return;
    }

    shift = (3 - ui->memory.active_address_digit) * 4;
    mask = (uint16_t)(0x0fu << shift);
    ui->memory.view_address = (uint16_t)((ui->memory.view_address & (uint16_t)~mask) |
        (uint16_t)((uint16_t)digit << shift));
    ui->memory.cursor_address = ui->memory.view_address;

    if (ui->memory.active_address_digit >= 3) {
        ui->memory.edit_field = FRONTEND_MEMORY_EDIT_HEX;
        ui->memory.active_address_digit = 0;
    } else {
        ui->memory.active_address_digit++;
    }
    ui->memory.request_pending = false;
}

static float frontend_memory_char_width(frontend *ui)
{
    const struct nk_user_font *font;

    if (ui == NULL || ui->ctx == NULL) {
        return 8.0f;
    }

    font = ui->ctx->style.font;
    if (font == NULL || font->width == NULL) {
        return 8.0f;
    }

    return font->width(font->userdata, font->height, "0", 1);
}

static int frontend_memory_cursor_text_col(const frontend_memory_view_state *memory)
{
    uint16_t offset;
    uint8_t col;

    if (memory == NULL || memory->columns == 0) {
        return 0;
    }

    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
        return memory->active_address_digit;
    }

    offset = (uint16_t)(memory->cursor_address - memory->view_address);
    col = (uint8_t)(offset % memory->columns);
    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
        return 5 + (int)memory->columns * 3 + col;
    }

    return 5 + (int)col * 3 + memory->active_nibble;
}

static char frontend_memory_line_char_at(const char *line, int index)
{
    size_t length;

    if (line == NULL || index < 0) {
        return ' ';
    }

    length = strlen(line);
    if ((size_t)index >= length) {
        return ' ';
    }

    return line[index];
}

static void frontend_memory_draw_cursor(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect row_bounds,
    const char *line,
    uint16_t row_addr)
{
    frontend_memory_view_state *memory;
    struct nk_command_buffer *canvas;
    uint16_t row_offset;
    int text_col;
    float char_w;
    struct nk_rect cursor_rect;
    char text[2];

    if (ui == NULL ||
        line == NULL ||
        !ui->memory.active ||
        debug_state == NULL ||
        debug_state->runtime_state != FRONTEND_RUNTIME_STATE_PAUSED) {
        return;
    }

    memory = &ui->memory;
    row_offset = (uint16_t)(memory->cursor_address - row_addr);
    if (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS &&
        (row_offset >= memory->columns || memory->cursor_address != row_addr)) {
        return;
    }

    if (memory->edit_field != FRONTEND_MEMORY_EDIT_ADDRESS && row_offset >= memory->columns) {
        return;
    }

    text_col = frontend_memory_cursor_text_col(memory);
    char_w = frontend_memory_char_width(ui);
    cursor_rect = nk_rect(
        row_bounds.x + char_w * (float)text_col,
        row_bounds.y + 1.0f,
        char_w,
        row_bounds.h - 2.0f);
    text[0] = frontend_memory_line_char_at(line, text_col);
    text[1] = '\0';

    canvas = nk_window_get_canvas(ui->ctx);
    nk_fill_rect(canvas, cursor_rect, 0.0f, nk_rgb(255, 244, 120));
    nk_draw_text(
        canvas,
        cursor_rect,
        text,
        1,
        ui->ctx->style.font,
        nk_rgb(255, 244, 120),
        nk_rgb(20, 24, 28));
}

static void frontend_memory_draw_scrollbar(
    frontend *ui,
    struct nk_rect bounds,
    uint16_t visible_count)
{
    frontend_memory_view_state *memory;
    struct nk_command_buffer *canvas;
    float visible_fraction;
    float thumb_h;
    float thumb_y;
    float usable_h;
    struct nk_rect track;
    struct nk_rect thumb;
    const struct nk_mouse *mouse;

    if (ui == NULL || bounds.h <= 0.0f) {
        return;
    }

    ui->memory_scrollbar_bounds = bounds;
    ui->has_memory_scrollbar_bounds = true;

    memory = &ui->memory;
    canvas = nk_window_get_canvas(ui->ctx);
    mouse = &ui->ctx->input.mouse;
    track = nk_rect(bounds.x + 2.0f, bounds.y + 2.0f, bounds.w - 4.0f, bounds.h - 4.0f);

    visible_fraction = (float)visible_count / 65536.0f;
    thumb_h = track.h * visible_fraction;
    if (thumb_h < 16.0f) {
        thumb_h = 16.0f;
    }
    if (thumb_h > track.h) {
        thumb_h = track.h;
    }

    usable_h = track.h - thumb_h;
    thumb_y = track.y + usable_h * ((float)memory->view_address / 65535.0f);
    thumb = nk_rect(track.x + 2.0f, thumb_y, track.w - 4.0f, thumb_h);

    if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) &&
        nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !memory->scrollbar_dragging) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
        memory->scrollbar_dragging = true;
        memory->scrollbar_grab_offset = mouse->pos.y - thumb.y;
    }

    if (!nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT)) {
        memory->scrollbar_dragging = false;
    }

    if (memory->scrollbar_dragging) {
        float y = mouse->pos.y - memory->scrollbar_grab_offset;
        float relative;

        if (y < track.y) {
            y = track.y;
        }
        if (y > track.y + usable_h) {
            y = track.y + usable_h;
        }

        relative = usable_h > 0.0f ? (y - track.y) / usable_h : 0.0f;
        memory->view_address = (uint16_t)(relative * 65535.0f);
        memory->request_pending = false;
    } else if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, track) &&
        nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) &&
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb)) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
        if (mouse->pos.y < thumb.y) {
            memory->view_address = (uint16_t)(memory->view_address - visible_count);
        } else {
            memory->view_address = (uint16_t)(memory->view_address + visible_count);
        }
        memory->request_pending = false;
    }

    nk_fill_rect(canvas, track, 0.0f, nk_rgb(35, 41, 47));
    nk_fill_rect(
        canvas,
        thumb,
        2.0f,
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, thumb) || memory->scrollbar_dragging ?
            nk_rgb(160, 174, 186) :
            nk_rgb(103, 124, 139));
}

static void frontend_memory_draw_status_footer(
    frontend *ui,
    const frontend_debug_state *debug_state,
    struct nk_rect content,
    float footer_h)
{
    struct nk_command_buffer *canvas;
    struct nk_rect footer;
    struct nk_rect field_rect;
    struct nk_rect address_rect;
    struct nk_rect edit_rect;
    const char *field;
    const char *editable;
    char address[32];

    if (ui == NULL || ui->ctx == NULL || content.w <= 8.0f || content.h <= footer_h) {
        return;
    }

    field = ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ASCII ? "ASCII" :
        (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS ? "Address" : "Hex");
    editable = debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED ?
        "editable" : "read-only";
    snprintf(address, sizeof(address), "Address: %04X", ui->memory.cursor_address);

    canvas = nk_window_get_canvas(ui->ctx);
    footer = nk_rect(content.x + 4.0f, content.y + content.h - footer_h, content.w - 8.0f, footer_h);
    field_rect = nk_rect(footer.x, footer.y + 2.0f, footer.w * 0.25f, footer.h - 4.0f);
    address_rect = nk_rect(footer.x + footer.w * 0.25f, footer.y + 2.0f, footer.w * 0.45f, footer.h - 4.0f);
    edit_rect = nk_rect(footer.x + footer.w * 0.70f, footer.y + 2.0f, footer.w * 0.30f, footer.h - 4.0f);

    nk_draw_text(
        canvas,
        field_rect,
        field,
        (int)strlen(field),
        ui->ctx->style.font,
        nk_rgb(30, 34, 38),
        nk_rgb(196, 214, 228));
    nk_draw_text(
        canvas,
        address_rect,
        address,
        (int)strlen(address),
        ui->ctx->style.font,
        nk_rgb(30, 34, 38),
        nk_rgb(196, 214, 228));
    nk_draw_text(
        canvas,
        edit_rect,
        editable,
        (int)strlen(editable),
        ui->ctx->style.font,
        nk_rgb(30, 34, 38),
        nk_rgb(196, 214, 228));
}

static void frontend_memory_handle_mouse_row(
    frontend *ui,
    struct nk_rect row_bounds,
    uint16_t row_addr)
{
    float char_w = frontend_memory_char_width(ui);
    float rel_x;
    int text_col;
    int hex_start = 5;
    int ascii_start = hex_start + ui->memory.columns * 3;
    int hex_end = hex_start + ui->memory.columns * 3;

    if (!nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT) ||
        !nk_input_is_mouse_hovering_rect(&ui->ctx->input, row_bounds)) {
        return;
    }

    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
    rel_x = ui->ctx->input.mouse.pos.x - row_bounds.x;
    if (rel_x < 0.0f) {
        rel_x = 0.0f;
    }
    text_col = (int)(rel_x / char_w);

    if (text_col < 4) {
        ui->memory.edit_field = FRONTEND_MEMORY_EDIT_ADDRESS;
        ui->memory.active_address_digit = (uint8_t)text_col;
        ui->memory.cursor_address = row_addr;
        return;
    }

    if (text_col >= hex_start && text_col < hex_end) {
        int cell = (text_col - hex_start) / 3;
        int cell_col = (text_col - hex_start) % 3;

        if (cell >= 0 && cell < ui->memory.columns) {
            ui->memory.edit_field = FRONTEND_MEMORY_EDIT_HEX;
            ui->memory.cursor_address = (uint16_t)(row_addr + cell);
            ui->memory.active_nibble = (uint8_t)(cell_col == 1 ? 1 : 0);
        }
        return;
    }

    if (text_col >= ascii_start && text_col < ascii_start + ui->memory.columns) {
        int cell = text_col - ascii_start;

        ui->memory.edit_field = FRONTEND_MEMORY_EDIT_ASCII;
        ui->memory.cursor_address = (uint16_t)(row_addr + cell);
    }
}

static void frontend_memory_handle_key(
    frontend *ui,
    const frontend_debug_state *debug_state,
    const SDL_KeyboardEvent *key)
{
    SDL_Keycode sym;
    SDL_Keymod mod;
    bool alt;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN) {
        return;
    }

    sym = key->keysym.sym;
    mod = key->keysym.mod;
    alt = (mod & KMOD_ALT) != 0;

    if (sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 || sym == SDLK_F12) {
        return;
    }

    if (alt && sym == SDLK_a) {
        if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            ui->memory.edit_field = FRONTEND_MEMORY_EDIT_HEX;
        } else {
            uint16_t offset = (uint16_t)(ui->memory.cursor_address - ui->memory.view_address);
            uint16_t row = (uint16_t)(offset / ui->memory.columns);

            ui->memory.edit_field = FRONTEND_MEMORY_EDIT_ADDRESS;
            ui->memory.cursor_address = (uint16_t)(ui->memory.view_address + row * ui->memory.columns);
        }
        ui->memory.active_address_digit = 0;
        return;
    }

    if (alt && sym == SDLK_x) {
        ui->memory.edit_field = ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ASCII ?
            FRONTEND_MEMORY_EDIT_HEX :
            FRONTEND_MEMORY_EDIT_ASCII;
        return;
    }

    if (alt && sym == SDLK_m) {
        if (ui->memory.mode == RUNTIME_MEMORY_MODE_CPU_MAP) {
            ui->memory.mode = RUNTIME_MEMORY_MODE_ROM;
        } else if (ui->memory.mode == RUNTIME_MEMORY_MODE_ROM) {
            ui->memory.mode = RUNTIME_MEMORY_MODE_RAM;
        } else {
            ui->memory.mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        }
        ui->memory.request_pending = false;
        return;
    }

    if (sym == SDLK_PAGEUP && ui->memory.edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        ui->memory.view_address = (uint16_t)(ui->memory.view_address - frontend_memory_visible_count(&ui->memory));
        ui->memory.request_pending = false;
        return;
    }

    if (sym == SDLK_PAGEDOWN && ui->memory.edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        ui->memory.view_address = (uint16_t)(ui->memory.view_address + frontend_memory_visible_count(&ui->memory));
        ui->memory.request_pending = false;
        return;
    }

    if (sym == SDLK_HOME) {
        if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            ui->memory.active_address_digit = 0;
        } else if (alt) {
            ui->memory.cursor_address = ui->memory.view_address;
        } else {
            uint16_t offset = (uint16_t)(ui->memory.cursor_address - ui->memory.view_address);
            uint16_t row = (uint16_t)(offset / ui->memory.columns);
            ui->memory.cursor_address = (uint16_t)(ui->memory.view_address + row * ui->memory.columns);
        }
        return;
    }

    if (sym == SDLK_END) {
        if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            ui->memory.active_address_digit = 3;
        } else if (alt) {
            ui->memory.cursor_address = (uint16_t)(ui->memory.view_address +
                frontend_memory_visible_count(&ui->memory) - 1u);
        } else {
            uint16_t offset = (uint16_t)(ui->memory.cursor_address - ui->memory.view_address);
            uint16_t row = (uint16_t)(offset / ui->memory.columns);
            ui->memory.cursor_address = (uint16_t)(ui->memory.view_address + row * ui->memory.columns +
                ui->memory.columns - 1u);
        }
        return;
    }

    if (sym == SDLK_UP && ui->memory.edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        frontend_memory_move_cursor(ui, -(int32_t)ui->memory.columns);
        return;
    }

    if (sym == SDLK_DOWN && ui->memory.edit_field != FRONTEND_MEMORY_EDIT_ADDRESS) {
        frontend_memory_move_cursor(ui, ui->memory.columns);
        return;
    }

    if (sym == SDLK_LEFT) {
        if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            if (ui->memory.active_address_digit > 0) {
                ui->memory.active_address_digit--;
            }
        } else if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
            frontend_memory_move_cursor(ui, -1);
        } else if (ui->memory.active_nibble == 0) {
            frontend_memory_move_cursor(ui, -1);
            ui->memory.active_nibble = 1;
        } else {
            ui->memory.active_nibble = 0;
        }
        return;
    }

    if (sym == SDLK_RIGHT) {
        if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            if (ui->memory.active_address_digit >= 3) {
                ui->memory.edit_field = FRONTEND_MEMORY_EDIT_HEX;
                ui->memory.active_address_digit = 0;
            } else {
                ui->memory.active_address_digit++;
            }
        } else if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
            frontend_memory_move_cursor(ui, 1);
        } else if (ui->memory.active_nibble == 0) {
            ui->memory.active_nibble = 1;
        } else {
            ui->memory.active_nibble = 0;
            frontend_memory_move_cursor(ui, 1);
        }
        return;
    }

    if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ASCII) {
        if (sym == SDLK_RETURN) {
            frontend_memory_write_byte(ui, debug_state, ui->memory.cursor_address, 0x0d);
            frontend_memory_move_cursor(ui, 1);
            return;
        }
        if (sym == SDLK_BACKSPACE) {
            frontend_memory_write_byte(ui, debug_state, ui->memory.cursor_address, 0x08);
            frontend_memory_move_cursor(ui, 1);
            return;
        }
        if (sym >= 32 && sym <= 126) {
            uint8_t byte = (uint8_t)sym;
            if (sym >= 'a' && sym <= 'z') {
                bool shift = (mod & KMOD_SHIFT) != 0;
                bool caps = (mod & KMOD_CAPS) != 0;
                if (shift ^ caps) {
                    byte = (uint8_t)(sym - ('a' - 'A'));
                }
            }
            frontend_memory_write_byte(ui, debug_state, ui->memory.cursor_address, byte);
            frontend_memory_move_cursor(ui, 1);
            return;
        }
    } else {
        int digit = frontend_hex_digit_value((char)sym);
        if (ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ADDRESS) {
            frontend_memory_apply_address_digit(ui, digit);
        } else {
            frontend_memory_apply_hex_digit(ui, debug_state, digit);
        }
    }
}

static void frontend_draw_memory(frontend *ui, struct nk_rect bounds, const frontend_debug_state *debug_state)
{
    frontend_memory_view_state *memory = &ui->memory;
    uint8_t row;
    uint8_t rows;
    float row_h;
    float footer_h;
    float scrollbar_w = 24.0f;
    float scrollbar_margin = 8.0f;
    uint16_t visible_count;
    struct nk_style_window saved_window_style;

    if (!memory->initialized) {
        memory->view_address = 0x0000;
        memory->cursor_address = 0x0000;
        memory->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        memory->edit_field = FRONTEND_MEMORY_EDIT_HEX;
        memory->columns = 16;
        memory->initialized = true;
    }

    row_h = ui->ctx->style.font != NULL ? ui->ctx->style.font->height : 13.0f;
    footer_h = 22.0f;

    rows = (uint8_t)((bounds.h > footer_h + 28.0f) ? ((bounds.h - footer_h - 28.0f) / row_h) : 1);
    if (rows > 1) {
        rows--;
    }
    if (rows == 0) {
        rows = 1;
    }
    if (rows > RUNTIME_MEMORY_SNAPSHOT_MAX / memory->columns) {
        rows = RUNTIME_MEMORY_SNAPSHOT_MAX / memory->columns;
    }
    memory->rows = rows;
    visible_count = frontend_memory_visible_count(memory);

    if (!frontend_any_dialog_open(ui) &&
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) &&
        ui->ctx->input.mouse.scroll_delta.y != 0.0f) {
        int32_t lines = ui->ctx->input.mouse.scroll_delta.y > 0.0f ? -3 : 3;
        memory->view_address = (uint16_t)(memory->view_address + lines * memory->columns);
        memory->request_pending = false;
    }

    if (ui->has_pending_memory_key) {
        if (memory->active) {
            frontend_memory_handle_key(ui, debug_state, &ui->pending_memory_key);
        }
        ui->has_pending_memory_key = false;
    }

    frontend_memory_request_if_needed(ui, debug_state);

    if (nk_begin(ui->ctx, "Memory", bounds, pane_flags)) {
        saved_window_style = ui->ctx->style.window;
        ui->ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
        ui->ctx->style.window.group_padding = nk_vec2(0.0f, 0.0f);

        nk_layout_row_begin(ui->ctx, NK_STATIC, row_h * (float)rows, 3);
        nk_layout_row_push(ui->ctx, bounds.w - scrollbar_w - scrollbar_margin);
        if (nk_group_begin(ui->ctx, "memory-rows", NK_WINDOW_NO_SCROLLBAR)) {
            for (row = 0; row < rows; ++row) {
                char line[96];
                char *cursor = line;
                size_t remaining = sizeof(line);
                uint8_t col;
                uint16_t row_addr = (uint16_t)(memory->view_address + (uint16_t)row * memory->columns);
                int written = snprintf(cursor, remaining, "%04X:", row_addr);
                struct nk_rect row_bounds;

                cursor += written;
                remaining -= (size_t)written;
                for (col = 0; col < memory->columns; ++col) {
                    uint16_t address = (uint16_t)(row_addr + col);
                    uint8_t value = frontend_memory_byte_at(debug_state, address);
                    written = snprintf(cursor, remaining, "%02X ", value);
                    cursor += written;
                    remaining -= (size_t)written;
                }
                for (col = 0; col < memory->columns; ++col) {
                    uint16_t address = (uint16_t)(row_addr + col);
                    uint8_t value = frontend_memory_byte_at(debug_state, address);
                    written = snprintf(cursor, remaining, "%c", frontend_memory_ascii(value));
                    cursor += written;
                    remaining -= (size_t)written;
                }

                nk_layout_row_dynamic(ui->ctx, row_h, 1);
                row_bounds = nk_widget_bounds(ui->ctx);
                frontend_memory_handle_mouse_row(ui, row_bounds, row_addr);
                nk_label(ui->ctx, line, NK_TEXT_LEFT);
                frontend_memory_draw_cursor(ui, debug_state, row_bounds, line, row_addr);
            }
            nk_group_end(ui->ctx);
        }

        nk_layout_row_push(ui->ctx, scrollbar_w);
        if (nk_group_begin(ui->ctx, "memory-scrollbar", NK_WINDOW_NO_SCROLLBAR)) {
            struct nk_rect scrollbar_bounds = nk_window_get_content_region(ui->ctx);
            frontend_memory_draw_scrollbar(ui, scrollbar_bounds, visible_count);
            nk_group_end(ui->ctx);
        }
        nk_layout_row_push(ui->ctx, scrollbar_margin);
        nk_spacing(ui->ctx, 1);
        nk_layout_row_end(ui->ctx);

        frontend_memory_draw_status_footer(
            ui,
            debug_state,
            nk_window_get_content_region(ui->ctx),
            footer_h);

        if (nk_contextual_begin(ui->ctx, 0, nk_vec2(120.0f, 90.0f), bounds)) {
            nk_layout_row_dynamic(ui->ctx, 22, 1);
            if (nk_contextual_item_symbol_label(ui->ctx,
                    memory->mode == RUNTIME_MEMORY_MODE_CPU_MAP ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_NONE,
                    "Map", NK_TEXT_LEFT)) {
                memory->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
                memory->request_pending = false;
            }
            if (nk_contextual_item_symbol_label(ui->ctx,
                    memory->mode == RUNTIME_MEMORY_MODE_ROM ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_NONE,
                    "ROM", NK_TEXT_LEFT)) {
                memory->mode = RUNTIME_MEMORY_MODE_ROM;
                memory->request_pending = false;
            }
            if (nk_contextual_item_symbol_label(ui->ctx,
                    memory->mode == RUNTIME_MEMORY_MODE_RAM ? NK_SYMBOL_CIRCLE_SOLID : NK_SYMBOL_NONE,
                    "RAM", NK_TEXT_LEFT)) {
                memory->mode = RUNTIME_MEMORY_MODE_RAM;
                memory->request_pending = false;
            }
            nk_contextual_end(ui->ctx);
        }

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_MEMORY) {
            frontend_draw_active_view_border(ui->ctx);
        }
        frontend_draw_memory_mode_border(
            ui->ctx,
            memory->mode,
            !frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_MEMORY);

        ui->ctx->style.window = saved_window_style;
    }
    nk_end(ui->ctx);
}

static const char *frontend_disk_label(const frontend_debug_state *debug_state, uint8_t device)
{
    size_t index;
    const runtime_disk_status_snapshot *status;

    if (debug_state == NULL || device < 8 || device > 9) {
        return "No disk";
    }
    index = (size_t)(device - 8u);
    if (!debug_state->has_disk_status[index]) {
        return "No disk";
    }

    status = &debug_state->disk_status[index];
    if (!status->mounted) {
        if (status->last_result == C64_DRIVE_STATUS_IO_ERROR) {
            return "Mount failed";
        }
        if (status->last_result == C64_DRIVE_STATUS_PARSE_ERROR ||
            status->last_result == C64_DRIVE_STATUS_UNSUPPORTED_IMAGE) {
            return "Unsupported disk";
        }
        return "No disk";
    }
    if (status->disk_title[0] != '\0') {
        return status->disk_title;
    }
    if (status->display_name[0] != '\0') {
        return status->display_name;
    }
    return "Mounted";
}

static void frontend_draw_misc_programs(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;

    /* Disks */
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Disks", NK_TEXT_LEFT);
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
    nk_layout_row_push(ctx, 0.12f);
    if (nk_button_label(ctx, "8")) {
        frontend_push_disk_intent(ui, FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG, 8);
    }
    nk_layout_row_push(ctx, 0.20f);
    if (nk_button_label(ctx, "Eject")) {
        frontend_push_disk_intent(ui, FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT, 8);
    }
    nk_layout_row_push(ctx, 0.68f);
    nk_label(ctx, frontend_disk_label(debug_state, 8), NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
    nk_layout_row_push(ctx, 0.12f);
    if (nk_button_label(ctx, "9")) {
        frontend_push_disk_intent(ui, FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG, 9);
    }
    nk_layout_row_push(ctx, 0.20f);
    if (nk_button_label(ctx, "Eject")) {
        frontend_push_disk_intent(ui, FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT, 9);
    }
    nk_layout_row_push(ctx, 0.68f);
    nk_label(ctx, frontend_disk_label(debug_state, 9), NK_TEXT_LEFT);
    nk_layout_row_end(ctx);

    /* Programs */
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Programs", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 2);
    if (nk_button_label(ctx, "Load")) {
        frontend_load_bin_dialog_state *dlg = &ui->load_bin_dialog;
        if (!dlg->initialized) {
            memset(dlg, 0, sizeof(*dlg));
            dlg->use_file_address = true;
            snprintf(dlg->address_buf, sizeof(dlg->address_buf), "0000");
            dlg->initialized = true;
        }
        dlg->open = true;
    }
    if (nk_button_label(ctx, "Save")) {
        frontend_save_bin_dialog_state *dlg = &ui->save_bin_dialog;
        if (!dlg->initialized) {
            memset(dlg, 0, sizeof(*dlg));
            dlg->write_file_address = true;
            snprintf(dlg->start_address_buf, sizeof(dlg->start_address_buf), "0801");
            snprintf(dlg->end_address_buf, sizeof(dlg->end_address_buf), "0801");
            dlg->initialized = true;
        }
        dlg->open = true;
    }

    /* Emulator */
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Emulator", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Configure...")) {
        frontend_config_dialog_open(ui);
    }
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "Reset")) {
        frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_MACHINE_RESET);
    }
}

static void frontend_draw_misc_debugger(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Debug Status", NK_TEXT_LEFT);
    {
        char status[96];
        snprintf(
            status,
            sizeof(status),
            "State: %s  Turbo: %ux  PC: %04X  Stop: %s",
            debug_state != NULL ? frontend_runtime_state_name(debug_state->runtime_state) : "UNKNOWN",
            debug_state != NULL ? (debug_state->active_turbo_multiplier > 0 ? debug_state->active_turbo_multiplier : 1u) : 1u,
            debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.pc : 0,
            debug_state != NULL ? frontend_stop_reason_name(debug_state->stop_reason) : "none");
        nk_label(ctx, status, NK_TEXT_LEFT);
    }
    {
        char status[128];
        snprintf(
            status,
            sizeof(status),
            "CPU: %llu  Machine: %llu  VIC: %llu  CIA: %llu",
            (unsigned long long)(debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.cycles : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->machine_cycle : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->vic_cycles : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->cia_cycles : 0));
        nk_label(ctx, status, NK_TEXT_LEFT);
    }
    {
        char status[96];
        snprintf(
            status,
            sizeof(status),
            "Frame: %llu  Frame cycle: %llu  Dropped: %llu",
            (unsigned long long)(debug_state != NULL ? debug_state->frame_number : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->frame_cycle : 0),
            (unsigned long long)(debug_state != NULL ? debug_state->dropped_frames : 0));
        nk_label(ctx, status, NK_TEXT_LEFT);
    }

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Call Stack", NK_TEXT_LEFT);

    frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK);

    if (debug_state == NULL || !debug_state->has_call_stack ||
        debug_state->call_stack.count == 0) {
        nk_layout_row_dynamic(ctx, 15.0f, 1);
        nk_label(ctx, "No stack entries", NK_TEXT_LEFT);
    } else {
        uint8_t cs_i;
        for (cs_i = 0; cs_i < debug_state->call_stack.count; cs_i++) {
            const runtime_call_stack_entry *cs_entry = &debug_state->call_stack.entries[cs_i];
            frontend_disassembly_view_state *view = &ui->disassembly;
            char jsr_label[8];
            char dest_label[28];
            char sym[17];
            nk_bool selected;

            snprintf(jsr_label, sizeof(jsr_label), "%04X", cs_entry->jsr_address);

            sym[0] = '\0';
            if (ui->symbols.address_to_label != NULL) {
                (void)ui->symbols.address_to_label(
                    ui->symbols.userdata,
                    cs_entry->dest_address,
                    sym,
                    sizeof(sym));
                sym[15] = '\0';
            }
            if (sym[0] != '\0') {
                snprintf(dest_label, sizeof(dest_label), "JSR %s", sym);
            } else {
                snprintf(dest_label, sizeof(dest_label), "JSR %04X", cs_entry->dest_address);
            }

            nk_layout_row_begin(ctx, NK_DYNAMIC, 15.0f, 2);

            nk_layout_row_push(ctx, 0.12f);
            selected = nk_false;
            if (nk_selectable_label(ctx, jsr_label, NK_TEXT_LEFT, &selected) && selected) {
                view->cursor_address = cs_entry->jsr_address;
                view->top_address = frontend_disassembly_center_top(cs_entry->jsr_address, view->rows);
                view->cursor_row = view->rows / 2u;
                view->has_user_cursor = true;
                view->request_pending = false;
                view->follow_pc = false;
                view->pc_lock_active = false;
            }

            nk_layout_row_push(ctx, 0.88f);
            selected = nk_false;
            if (nk_selectable_label(ctx, dest_label, NK_TEXT_LEFT, &selected) && selected) {
                view->cursor_address = cs_entry->dest_address;
                view->top_address = frontend_disassembly_center_top(cs_entry->dest_address, view->rows);
                view->cursor_row = view->rows / 2u;
                view->has_user_cursor = true;
                view->request_pending = false;
                view->follow_pc = false;
                view->pc_lock_active = false;
            }

            nk_layout_row_end(ctx);
        }
    }
}

static void frontend_draw_misc_breakpoints(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;
    uint16_t i;
    uint16_t count;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    count = debug_state != NULL && debug_state->has_breakpoints ?
        debug_state->breakpoints.count :
        0;

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, "Breakpoints", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    if (nk_button_label(ctx, "New")) {
        frontend_open_breakpoint_dialog_default(ui);
    }

    if (count == 0) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No breakpoints set", NK_TEXT_LEFT);
    }

    for (i = 0; i < count; ++i) {
        const runtime_breakpoint_snapshot_entry *entry = &debug_state->breakpoints.entries[i];
        struct nk_style_button saved_button = ctx->style.button;
        char label[96];
        char access[4];
        size_t access_len = 0;

        if (entry->enabled == 0) {
            ctx->style.button.text_normal = nk_rgb(180, 142, 210);
            ctx->style.button.normal = nk_style_item_color(nk_rgb(40, 34, 48));
        }

        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0) {
            access[access_len++] = 'X';
        }
        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_READ) != 0) {
            access[access_len++] = 'R';
        }
        if ((entry->access & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0) {
            access[access_len++] = 'W';
        }
        access[access_len] = '\0';

        if (entry->use_counter != 0) {
            if (entry->has_end_address) {
                snprintf(
                    label,
                    sizeof(label),
                    "%s [%04X-%04X] (%u:%u)",
                    access,
                    entry->start_address,
                    entry->end_address,
                    entry->current_hits,
                    entry->counter);
            } else {
                snprintf(
                    label,
                    sizeof(label),
                    "%s [%04X] (%u:%u)",
                    access,
                    entry->start_address,
                    entry->current_hits,
                    entry->counter);
            }
        } else if (entry->has_end_address) {
            snprintf(label, sizeof(label), "%s [%04X-%04X] (%u)", access, entry->start_address, entry->end_address, entry->current_hits);
        } else {
            snprintf(label, sizeof(label), "%s [%04X] (%u)", access, entry->start_address, entry->current_hits);
        }
        nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 6);
        nk_layout_row_push(ctx, 0.28f);
        nk_label_colored(
            ctx,
            label,
            NK_TEXT_LEFT,
            entry->enabled != 0 ? nk_rgb(232, 235, 238) : nk_rgb(180, 142, 210));
        nk_layout_row_push(ctx, 0.13f);
        if (nk_button_label(ctx, "Edit")) {
            frontend_open_breakpoint_dialog_from_entry(ui, entry, false);
        }
        nk_layout_row_push(ctx, 0.16f);
        if (nk_button_label(ctx, "Duplicate")) {
            frontend_open_breakpoint_dialog_from_entry(ui, entry, true);
        }
        nk_layout_row_push(ctx, 0.14f);
        if (nk_button_label(ctx, entry->enabled != 0 ? "Disable" : "Enable")) {
            frontend_push_breakpoint_id_intent(
                ui,
                FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED,
                entry->id,
                entry->enabled == 0);
        }
        nk_layout_row_push(ctx, 0.15f);
        if (nk_button_label(ctx, "View PC")) {
            frontend_disassembly_scroll_to_top(ui, frontend_disassembly_center_top(entry->start_address, ui->disassembly.rows));
            frontend_disassembly_set_user_cursor(&ui->disassembly, entry->start_address, ui->disassembly.rows / 2, 1);
        }
        nk_layout_row_push(ctx, 0.14f);
        if (nk_button_label(ctx, "Clear")) {
            frontend_push_breakpoint_id_intent(
                ui,
                FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR,
                entry->id,
                false);
        }
        nk_layout_row_end(ctx);
        ctx->style.button = saved_button;
    }

    if (count >= 2) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        if (nk_button_label(ctx, "Clear All")) {
            frontend_push_debugger_intent(
                ui,
                FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL,
                0);
        }
    }
}

static const char *frontend_memory_visibility_name(
    c64_memory_visibility visibility,
    const char *rom_name)
{
    switch (visibility) {
        case C64_MEMORY_VISIBILITY_RAM:
            return "RAM";
        case C64_MEMORY_VISIBILITY_ROM:
            return rom_name;
        case C64_MEMORY_VISIBILITY_IO:
            return "I/O";
        default:
            return "?";
    }
}

static void frontend_hardware_label_value(
    struct nk_context *ctx,
    const char *label,
    const char *value)
{
    nk_layout_row_begin(ctx, NK_STATIC, 18.0f, 2);
    nk_layout_row_push(ctx, 150.0f);
    nk_label(ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 660.0f);
    nk_label(ctx, value, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
}

static const char *frontend_bool_text(bool value)
{
    return value ? "yes" : "no";
}

static const char *frontend_video_standard_text(c64_video_standard standard)
{
    return standard == C64_VIDEO_STANDARD_PAL ? "PAL" : "NTSC";
}

static const char *frontend_sid_env_state_text(sid_env_state state)
{
    switch (state) {
        case SID_ENV_ATTACK:
            return "attack";
        case SID_ENV_DECAY:
            return "decay";
        case SID_ENV_SUSTAIN:
            return "sustain";
        case SID_ENV_RELEASE:
        default:
            return "release";
    }
}

static void frontend_sid_waveform_text(uint8_t mask, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    if ((mask & 0xf0u) == 0) {
        snprintf(out, out_size, "none");
        return;
    }

    snprintf(
        out,
        out_size,
        "%s%s%s%s",
        (mask & 0x10u) != 0 ? "tri " : "",
        (mask & 0x20u) != 0 ? "saw " : "",
        (mask & 0x40u) != 0 ? "pulse " : "",
        (mask & 0x80u) != 0 ? "noise" : "");
}

static void frontend_draw_hardware_vicii(
    struct nk_context *ctx,
    const frontend_debug_state *debug_state)
{
    const c64_vicii_hardware_snapshot *vic;
    char value[160];

    if (debug_state == NULL || !debug_state->has_hardware) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No VIC-II snapshot", NK_TEXT_LEFT);
        return;
    }

    vic = &debug_state->vicii_hardware;
    snprintf(
        value,
        sizeof(value),
        "%s  line %u/%u  cycle %u/%u",
        frontend_video_standard_text(vic->standard),
        vic->raster_line,
        vic->lines_per_frame,
        vic->cycle_in_line,
        vic->cycles_per_line);
    frontend_hardware_label_value(ctx, "Raster", value);

    snprintf(value, sizeof(value), "%llu", (unsigned long long)vic->frame_number);
    frontend_hardware_label_value(ctx, "Frame", value);

    snprintf(
        value,
        sizeof(value),
        "$%03X  pending %s  status $%02X  enable $%02X",
        vic->raster_compare,
        frontend_bool_text(vic->irq_pending),
        vic->irq_status,
        vic->irq_enable);
    frontend_hardware_label_value(ctx, "IRQ", value);

    snprintf(value, sizeof(value), "$%02X  $D016 $%02X  $D018 $%02X",
        vic->control_1, vic->control_2, vic->memory_pointer);
    frontend_hardware_label_value(ctx, "Registers", value);

    snprintf(
        value,
        sizeof(value),
        "border %u  bg %u/%u/%u/%u",
        vic->border_color,
        vic->background_color[0],
        vic->background_color[1],
        vic->background_color[2],
        vic->background_color[3]);
    frontend_hardware_label_value(ctx, "Colors", value);

    snprintf(
        value,
        sizeof(value),
        "display %s  bad-line %s  BA %s",
        frontend_bool_text(vic->display_state),
        frontend_bool_text(vic->bad_line),
        frontend_bool_text(vic->ba_active));
    frontend_hardware_label_value(ctx, "State", value);

    snprintf(value, sizeof(value), "VC %u  VCBASE %u  RC %u", vic->vc, vic->vc_base, vic->rc);
    frontend_hardware_label_value(ctx, "Counters", value);

    snprintf(
        value,
        sizeof(value),
        "enable $%02X  xexp $%02X  yexp $%02X  mc $%02X",
        vic->sprite_enable,
        vic->sprite_x_expand,
        vic->sprite_y_expand,
        vic->sprite_multicolor);
    frontend_hardware_label_value(ctx, "Sprites", value);

    snprintf(
        value,
        sizeof(value),
        "priority $%02X  ss $%02X  sb $%02X",
        vic->sprite_priority,
        vic->sprite_sprite_collision,
        vic->sprite_background_collision);
    frontend_hardware_label_value(ctx, "Collisions", value);
}

static void frontend_draw_hardware_cia_one(
    struct nk_context *ctx,
    const char *name,
    const c64_cia_hardware_snapshot *cia_state)
{
    char value[160];

    nk_layout_row_dynamic(ctx, 18.0f, 1);
    nk_label(ctx, name, NK_TEXT_LEFT);

    snprintf(value, sizeof(value), "PA $%02X  PB $%02X  DDRA $%02X  DDRB $%02X",
        cia_state->port_a, cia_state->port_b, cia_state->ddra, cia_state->ddrb);
    frontend_hardware_label_value(ctx, "Ports", value);

    snprintf(
        value,
        sizeof(value),
        "counter $%04X  latch $%04X  ctrl $%02X  underflow %s",
        cia_state->timer_a_counter,
        cia_state->timer_a_latch,
        cia_state->timer_a_control,
        frontend_bool_text(cia_state->timer_a_underflow));
    frontend_hardware_label_value(ctx, "Timer A", value);

    snprintf(
        value,
        sizeof(value),
        "counter $%04X  latch $%04X  ctrl $%02X  underflow %s",
        cia_state->timer_b_counter,
        cia_state->timer_b_latch,
        cia_state->timer_b_control,
        frontend_bool_text(cia_state->timer_b_underflow));
    frontend_hardware_label_value(ctx, "Timer B", value);

    snprintf(
        value,
        sizeof(value),
        "flags $%02X  mask $%02X  pending %s",
        cia_state->interrupt_flags,
        cia_state->interrupt_mask,
        frontend_bool_text(cia_state->interrupt_pending));
    frontend_hardware_label_value(ctx, "ICR", value);

    snprintf(
        value,
        sizeof(value),
        "%02X:%02X:%02X.%X  alarm %02X:%02X:%02X.%X",
        cia_state->tod_hours,
        cia_state->tod_minutes,
        cia_state->tod_seconds,
        cia_state->tod_tenths,
        cia_state->alarm_hours,
        cia_state->alarm_minutes,
        cia_state->alarm_seconds,
        cia_state->alarm_tenths);
    frontend_hardware_label_value(ctx, "TOD", value);
}

static void frontend_draw_hardware_cia(
    struct nk_context *ctx,
    const frontend_debug_state *debug_state)
{
    if (debug_state == NULL || !debug_state->has_hardware) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No CIA snapshot", NK_TEXT_LEFT);
        return;
    }

    frontend_draw_hardware_cia_one(ctx, "CIA #1", &debug_state->cia1_hardware);
    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    frontend_draw_hardware_cia_one(ctx, "CIA #2", &debug_state->cia2_hardware);
}

static void frontend_draw_hardware_sid(
    struct nk_context *ctx,
    const frontend_debug_state *debug_state)
{
    const c64_sid_hardware_snapshot *sid_state;
    char value[160];
    int i;

    if (debug_state == NULL || !debug_state->has_hardware) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No SID snapshot", NK_TEXT_LEFT);
        return;
    }

    sid_state = &debug_state->sid_hardware;
    for (i = 0; i < 3; ++i) {
        const c64_sid_voice_hardware_snapshot *voice = &sid_state->voices[i];
        char label[32];
        char waveform[48];

        frontend_sid_waveform_text(voice->waveform_mask, waveform, sizeof(waveform));
        snprintf(label, sizeof(label), "Voice %d", i + 1);
        snprintf(
            value,
            sizeof(value),
            "freq $%04X  pw $%03X  ctrl $%02X  %s",
            voice->frequency,
            voice->pulse_width,
            voice->control,
            waveform);
        frontend_hardware_label_value(ctx, label, value);

        snprintf(
            value,
            sizeof(value),
            "env %u %s  AD $%02X  SR $%02X  phase $%02X",
            voice->envelope,
            frontend_sid_env_state_text(voice->envelope_state),
            voice->attack_decay,
            voice->sustain_release,
            voice->phase_hi);
        frontend_hardware_label_value(ctx, "Envelope", value);
    }

    snprintf(
        value,
        sizeof(value),
        "cutoff $%03X  res/route $%02X  mode/vol $%02X  volume %u",
        sid_state->filter_cutoff,
        sid_state->filter_res_route,
        sid_state->mode_volume,
        sid_state->volume);
    frontend_hardware_label_value(ctx, "Filter", value);

    snprintf(
        value,
        sizeof(value),
        "OSC3 $%02X  ENV3 $%02X  sample %.4f  output %s",
        sid_state->voice3_osc_read,
        sid_state->voice3_env_read,
        (double)sid_state->last_sample,
        frontend_bool_text(sid_state->sample_output_enabled));
    frontend_hardware_label_value(ctx, "Readback", value);
}

static void frontend_draw_hardware_counters(
    struct nk_context *ctx,
    const frontend_debug_state *debug_state)
{
    char value[192];

    if (debug_state == NULL || !debug_state->has_hardware) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No counter snapshot", NK_TEXT_LEFT);
        return;
    }

    snprintf(
        value,
        sizeof(value),
        "machine %llu  CPU %llu  VIC %llu  CIA %llu",
        (unsigned long long)debug_state->machine_cycle,
        (unsigned long long)(debug_state->has_cpu ? debug_state->cpu.cycles : 0),
        (unsigned long long)debug_state->vic_cycles,
        (unsigned long long)debug_state->cia_cycles);
    frontend_hardware_label_value(ctx, "Cycles", value);

    snprintf(
        value,
        sizeof(value),
        "frame %llu  frame cycle %llu  dropped %llu",
        (unsigned long long)debug_state->frame_number,
        (unsigned long long)debug_state->frame_cycle,
        (unsigned long long)debug_state->dropped_frames);
    frontend_hardware_label_value(ctx, "Frames", value);

    snprintf(
        value,
        sizeof(value),
        "screen %llu  color %llu",
        (unsigned long long)debug_state->screen_ram_writes,
        (unsigned long long)debug_state->color_ram_writes);
    frontend_hardware_label_value(ctx, "RAM writes", value);

    snprintf(
        value,
        sizeof(value),
        "VIC %llu  CIA1 %llu  CIA2 %llu  SID %llu",
        (unsigned long long)debug_state->vic_register_writes,
        (unsigned long long)debug_state->cia1_register_writes,
        (unsigned long long)debug_state->cia2_register_writes,
        (unsigned long long)debug_state->sid_register_writes);
    frontend_hardware_label_value(ctx, "I/O writes", value);

    snprintf(
        value,
        sizeof(value),
        "IRQ entries %llu  NMI entries %llu  RESTORE %llu",
        (unsigned long long)debug_state->irq_entries,
        (unsigned long long)debug_state->nmi_entries,
        (unsigned long long)debug_state->restore_requests);
    frontend_hardware_label_value(ctx, "Interrupts", value);

    snprintf(
        value,
        sizeof(value),
        "reads %llu  writes %llu  assertions %llu",
        (unsigned long long)debug_state->cia1_icr_reads,
        (unsigned long long)debug_state->cia1_icr_writes,
        (unsigned long long)debug_state->cia1_interrupt_assertions);
    frontend_hardware_label_value(ctx, "CIA1 ICR", value);

    snprintf(value, sizeof(value), "%llu", (unsigned long long)debug_state->keyboard_events);
    frontend_hardware_label_value(ctx, "Keyboard events", value);
}

static void frontend_draw_hardware_memory_banking(
    struct nk_context *ctx,
    const frontend_debug_state *debug_state)
{
    const runtime_memory_banking_snapshot *banking;
    char value[128];

    if (debug_state == NULL || !debug_state->has_memory_banking) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "No memory banking snapshot", NK_TEXT_LEFT);
        return;
    }

    banking = &debug_state->memory_banking;

    snprintf(value, sizeof(value), "$%02X", banking->cpu_port_direction);
    frontend_hardware_label_value(ctx, "$0000 DDR", value);

    snprintf(value, sizeof(value), "$%02X", banking->cpu_port_data);
    frontend_hardware_label_value(ctx, "$0001 DATA", value);

    snprintf(
        value,
        sizeof(value),
        "LORAM %s  HIRAM %s  CHAREN %s",
        banking->loram ? "on" : "off",
        banking->hiram ? "on" : "off",
        banking->charen ? "on" : "off");
    frontend_hardware_label_value(ctx, "Bank bits", value);

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_begin(ctx, NK_STATIC, 18.0f, 2);
    nk_layout_row_push(ctx, 150.0f);
    nk_label(ctx, "Range", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 660.0f);
    nk_label(ctx, "Visible", NK_TEXT_LEFT);
    nk_layout_row_end(ctx);

    frontend_hardware_label_value(
        ctx,
        "$A000-$BFFF",
        frontend_memory_visibility_name(banking->basic_visibility, "BASIC ROM"));
    frontend_hardware_label_value(
        ctx,
        "$D000-$DFFF",
        frontend_memory_visibility_name(banking->io_visibility, "CHAR ROM"));
    frontend_hardware_label_value(
        ctx,
        "$E000-$FFFF",
        frontend_memory_visibility_name(banking->kernal_visibility, "KERNAL ROM"));

    nk_layout_row_dynamic(ctx, 8.0f, 1);
    nk_spacing(ctx, 1);

    snprintf(
        value,
        sizeof(value),
        "$%04X  bank %u  CIA2 PA low bits %u",
        banking->vic_bank_base,
        banking->vic_bank_select,
        (unsigned)(banking->cia2_port_a_pins & 0x03u));
    frontend_hardware_label_value(ctx, "VIC bank base", value);

    snprintf(value, sizeof(value), "$%02X", banking->vic_memory_pointer);
    frontend_hardware_label_value(ctx, "$D018", value);

    snprintf(value, sizeof(value), "$%04X", banking->vic_screen_base);
    frontend_hardware_label_value(ctx, "Screen base", value);

    snprintf(value, sizeof(value), "$%04X", banking->vic_character_base);
    frontend_hardware_label_value(ctx, "Character base", value);

    snprintf(value, sizeof(value), "$%04X", banking->vic_bitmap_base);
    frontend_hardware_label_value(ctx, "Bitmap base", value);
}

static void frontend_draw_misc_hardware(frontend *ui, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    if (nk_tree_push_id(ctx, NK_TREE_TAB, "Memory / Banks", NK_MAXIMIZED, 1)) {
        frontend_draw_hardware_memory_banking(ctx, debug_state);
        nk_tree_pop(ctx);
    }
    if (nk_tree_push_id(ctx, NK_TREE_TAB, "VIC-II", NK_MAXIMIZED, 2)) {
        frontend_draw_hardware_vicii(ctx, debug_state);
        nk_tree_pop(ctx);
    }
    if (nk_tree_push_id(ctx, NK_TREE_TAB, "CIA", NK_MAXIMIZED, 3)) {
        frontend_draw_hardware_cia(ctx, debug_state);
        nk_tree_pop(ctx);
    }
    if (nk_tree_push_id(ctx, NK_TREE_TAB, "SID", NK_MAXIMIZED, 4)) {
        frontend_draw_hardware_sid(ctx, debug_state);
        nk_tree_pop(ctx);
    }
    if (nk_tree_push_id(ctx, NK_TREE_TAB, "Counters", NK_MAXIMIZED, 5)) {
        frontend_draw_hardware_counters(ctx, debug_state);
        nk_tree_pop(ctx);
    }
}

static void frontend_draw_misc_assembler(frontend *ui)
{
    struct nk_context *ctx;
    frontend_assembler_state *asm_state;
    static const nk_flags edit_flags = NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    asm_state = &ui->assembler;

    if (!asm_state->initialized) {
        snprintf(asm_state->address_buf, sizeof(asm_state->address_buf), "8000");
        snprintf(asm_state->run_address_buf, sizeof(asm_state->run_address_buf), "8000");
        asm_state->run_address_user_edited = false;
        asm_state->auto_run = false;
        asm_state->reset_first = true;
        asm_state->initialized = true;
    }

    /* File Name row */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
    nk_layout_row_push(ctx, 0.22f);
    nk_label(ctx, "File Name", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.56f);
    frontend_edit_replace(ctx, edit_flags, asm_state->file_path, (int)sizeof(asm_state->file_path), nk_filter_default);
    nk_layout_row_push(ctx, 0.22f);
    if (nk_button_label(ctx, "Browse...")) {
        frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE);
    }
    nk_layout_row_end(ctx);

    /* Address row */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
    nk_layout_row_push(ctx, 0.35f);
    nk_label(ctx, "Address", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.65f);
    if (frontend_edit_replace(ctx, edit_flags, asm_state->address_buf, (int)sizeof(asm_state->address_buf), nk_filter_hex) & NK_EDIT_DEACTIVATED) {
        /* Keep run_address in sync if user hasn't edited it independently */
        if (!asm_state->run_address_user_edited) {
            snprintf(asm_state->run_address_buf, sizeof(asm_state->run_address_buf), "%s", asm_state->address_buf);
        }
    }
    nk_layout_row_end(ctx);

    /* Run Address row */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
    nk_layout_row_push(ctx, 0.35f);
    nk_label(ctx, "Run Address", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.65f);
    if (frontend_edit_replace(ctx, edit_flags, asm_state->run_address_buf, (int)sizeof(asm_state->run_address_buf), nk_filter_hex) & NK_EDIT_ACTIVE) {
        asm_state->run_address_user_edited = true;
    }
    nk_layout_row_end(ctx);

    /* Auto Run row */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
    nk_layout_row_push(ctx, 0.35f);
    nk_label(ctx, "Auto Run", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.65f);
    {
        nk_bool auto_run_nk = asm_state->auto_run ? nk_true : nk_false;
        nk_checkbox_label(ctx, "", &auto_run_nk);
        asm_state->auto_run = (auto_run_nk != nk_false);
    }
    nk_layout_row_end(ctx);

    /* Reset checkbox */
    nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 2);
    nk_layout_row_push(ctx, 0.35f);
    nk_label(ctx, "Reset", NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 0.65f);
    {
        nk_bool reset_nk = asm_state->reset_first ? nk_true : nk_false;
        nk_checkbox_label(ctx, "", &reset_nk);
        asm_state->reset_first = (reset_nk != nk_false);
    }
    nk_layout_row_end(ctx);

    /* Assemble button */
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    if (nk_button_label(ctx, "Assemble")) {
        if (asm_state->file_path[0] != '\0') {
            frontend_push_assemble_run_intent(
                ui,
                asm_state->file_path,
                (uint16_t)strtoul(asm_state->address_buf, NULL, 16),
                (uint16_t)strtoul(asm_state->run_address_buf, NULL, 16),
                asm_state->auto_run,
                asm_state->reset_first);
        }
    }
}

static void frontend_draw_misc_tab_button(
    frontend *ui,
    frontend_misc_tab tab,
    const char *label)
{
    struct nk_style_button saved_button = ui->ctx->style.button;

    if (ui->misc.active_tab == tab) {
        ui->ctx->style.button.normal = nk_style_item_color(nk_rgb(49, 78, 94));
        ui->ctx->style.button.hover = nk_style_item_color(nk_rgb(58, 93, 112));
        ui->ctx->style.button.text_normal = nk_rgb(226, 246, 255);
    }

    if (nk_button_label(ui->ctx, label)) {
        ui->misc.active_tab = tab;
    }

    ui->ctx->style.button = saved_button;
}

static void frontend_draw_misc(frontend *ui, struct nk_rect bounds, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;
    float tab_h = 24.0f;
    float content_h;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    if (!ui->misc.initialized) {
        ui->misc.active_tab = FRONTEND_MISC_TAB_PROGRAMS;
        ui->misc.initialized = true;
    }

    ctx = ui->ctx;
    if (nk_begin(ctx, "Misc", bounds, NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, tab_h, 5);
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_PROGRAMS, "Machine");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_DEBUGGER, "Debugger");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_BREAKPOINTS, "Breakpoints");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_HARDWARE, "Hardware");
        frontend_draw_misc_tab_button(ui, FRONTEND_MISC_TAB_ASSEMBLER, "Assembler");

        content_h = bounds.h - tab_h - 44.0f;
        if (content_h < 24.0f) {
            content_h = 24.0f;
        }

        nk_layout_row_dynamic(ctx, content_h, 1);
        if (nk_group_begin(ctx, "misc-tab-content", NK_WINDOW_BORDER)) {
            switch (ui->misc.active_tab) {
                case FRONTEND_MISC_TAB_PROGRAMS:
                    frontend_draw_misc_programs(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_DEBUGGER:
                    frontend_draw_misc_debugger(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_BREAKPOINTS:
                    frontend_draw_misc_breakpoints(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_HARDWARE:
                    frontend_draw_misc_hardware(ui, debug_state);
                    break;

                case FRONTEND_MISC_TAB_ASSEMBLER:
                default:
                    frontend_draw_misc_assembler(ui);
                    break;
            }
            nk_group_end(ctx);
        }

        if (!frontend_any_dialog_open(ui) && ui->active_view == FRONTEND_ACTIVE_VIEW_MISC) {
            frontend_draw_active_view_border(ctx);
        }
    }
    nk_end(ctx);
}

static void frontend_draw_splitter(struct nk_context *ctx, const char *name, struct nk_rect bounds, int active)
{
    struct nk_style_window saved;
    struct nk_color color = active ? nk_rgb(114, 164, 204) : nk_rgb(74, 88, 100);

    saved = ctx->style.window;
    ctx->style.window.fixed_background = nk_style_item_color(color);
    ctx->style.window.border = 0.0f;
    ctx->style.window.rounding = 0.0f;
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, name, bounds, NK_WINDOW_NO_SCROLLBAR)) {
    }
    nk_end(ctx);

    ctx->style.window = saved;
}

static void frontend_draw_corner_handle(struct nk_context *ctx, struct nk_rect bounds, int active)
{
    struct nk_color color = active ? nk_rgb(176, 214, 241) : nk_rgb(105, 126, 143);
    struct nk_style_window saved;

    saved = ctx->style.window;
    ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    ctx->style.window.border = 0.0f;
    ctx->style.window.rounding = 0.0f;
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "display-corner", bounds, NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        float x = bounds.x + bounds.w - 2.0f;
        float y = bounds.y + bounds.h - 2.0f;

        nk_stroke_line(canvas, x - 13.0f, y, x, y - 13.0f, 2.0f, color);
        nk_stroke_line(canvas, x - 8.0f, y, x, y - 8.0f, 2.0f, color);
        nk_stroke_line(canvas, x - 3.0f, y, x, y - 3.0f, 2.0f, color);
    }
    nk_end(ctx);

    ctx->style.window = saved;
}

frontend *frontend_create(platform_window *window)
{
    frontend *ui;
    struct nk_font_atlas *atlas;
    struct nk_font *font;
    SDL_Window *sdl_window;
    SDL_Renderer *sdl_renderer;

    if (window == NULL) {
        return NULL;
    }

    sdl_window = platform_window_get_sdl_window(window);
    sdl_renderer = platform_window_get_sdl_renderer(window);
    if (sdl_window == NULL || sdl_renderer == NULL) {
        return NULL;
    }

    ui = calloc(1, sizeof(*ui));
    if (ui == NULL) {
        return NULL;
    }

    ui->window = window;
    ui->renderer = sdl_renderer;
    ui->ctx = nk_sdl_init(sdl_window, sdl_renderer);
    if (ui->ctx == NULL) {
        free(ui);
        return NULL;
    }

    nk_sdl_font_stash_begin(&atlas);
    font = nk_font_atlas_add_default(atlas, 13.0f, NULL);
    ui->help_font = nk_font_atlas_add_from_memory(
        atlas,
        (void *)c64_pro_mono_font_data,
        (nk_size)c64_pro_mono_font_data_len,
        10.0f,
        NULL);
    nk_sdl_font_stash_end();
    if (font != NULL) {
        nk_style_set_font(ui->ctx, &font->handle);
    }
    symbol_resolver_null(&ui->symbols);

    ui->limits.registers_h_px = 88;
    ui->limits.min_display_w_px = 220;
    ui->limits.min_right_w_px = 220;
    ui->limits.min_disassembly_h_px = 120;
    ui->limits.min_bottom_h_px = 150;
    ui->limits.min_memory_w_px = 260;
    ui->limits.min_misc_w_px = 180;
    ui->limits.gutter_px = 5;
    ui->limits.corner_px = 22;
    c64_layout_init(&ui->layout);
    help_view_init(&ui->help);

    return ui;
}

void frontend_set_layout_state(frontend *ui, const frontend_layout_state *state)
{
    if (ui == NULL || state == NULL) {
        return;
    }

    if (state->split_display_right > 0.0f) {
        ui->layout.split_display_right = state->split_display_right;
    }
    if (state->split_top_bottom > 0.0f) {
        ui->layout.split_top_bottom = state->split_top_bottom;
    }
    if (state->split_memory_misc > 0.0f) {
        ui->layout.split_memory_misc = state->split_memory_misc;
    }
    if (state->display_width > 0) {
        ui->layout.display_px_w = state->display_width;
    }
    if (state->display_height > 0) {
        ui->layout.display_px_h = state->display_height;
    }
}

void frontend_get_layout_state(frontend *ui, frontend_layout_state *out_state)
{
    if (ui == NULL || out_state == NULL) {
        return;
    }

    out_state->split_display_right = ui->layout.split_display_right;
    out_state->split_top_bottom = ui->layout.split_top_bottom;
    out_state->split_memory_misc = ui->layout.split_memory_misc;
    out_state->display_width = ui->layout.display_px_w;
    out_state->display_height = ui->layout.display_px_h;
}

void frontend_set_config_state(frontend *ui, const app_options *options)
{
    if (ui == NULL || options == NULL) {
        return;
    }

    frontend_config_dialog_reset(&ui->config_dialog);
    if (!app_options_copy(&ui->config_dialog.original, options) ||
        !app_options_copy(&ui->config_dialog.edited, options) ||
        !frontend_config_prepare_edit_buffers(&ui->config_dialog)) {
        frontend_config_dialog_reset(&ui->config_dialog);
        return;
    }

    ui->config_dialog.initialized = true;
}

bool frontend_apply_selected_ini(frontend *ui, const app_options *options)
{
    frontend_config_dialog_state *dialog;

    if (ui == NULL || options == NULL) {
        return false;
    }

    dialog = &ui->config_dialog;
    if (!dialog->initialized) {
        return false;
    }

    app_options_destroy(&dialog->edited);
    if (!app_options_copy(&dialog->edited, options) ||
        !frontend_config_prepare_edit_buffers(dialog)) {
        frontend_config_dialog_reset(dialog);
        return false;
    }

    frontend_config_commit_ini_path(dialog);
    if (frontend_file_exists(dialog->edited.ini_path)) {
        dialog->prompt = FRONTEND_INI_PROMPT_EXISTING;
    }
    return true;
}

void frontend_append_symbol_file(frontend *ui, const char *path)
{
    frontend_config_dialog_state *dialog;
    char display_path[1024];
    char merged[1024];

    if (ui == NULL || path == NULL || path[0] == '\0') {
        return;
    }

    dialog = &ui->config_dialog;
    if (!dialog->initialized) {
        return;
    }

    if (!app_options_path_relative_to_ini(&dialog->edited, path, display_path, sizeof(display_path))) {
        snprintf(display_path, sizeof(display_path), "%s", path);
    }

    if (dialog->edited.symbol_files != NULL && dialog->edited.symbol_files[0] != '\0') {
        snprintf(merged, sizeof(merged), "%s,%s", dialog->edited.symbol_files, display_path);
    } else {
        snprintf(merged, sizeof(merged), "%s", display_path);
    }
    frontend_config_reserve_string(&dialog->edited.symbol_files, 1024);
    snprintf(dialog->edited.symbol_files, 1024, "%s", merged);
}

void frontend_set_assembler_path(frontend *ui, const char *path)
{
    if (ui == NULL || path == NULL) {
        return;
    }

    snprintf(ui->assembler.file_path, sizeof(ui->assembler.file_path), "%s", path);
}

void frontend_show_assembler_errors(frontend *ui, const char *errors)
{
    if (ui == NULL) {
        return;
    }

    snprintf(ui->assembler.error_text, sizeof(ui->assembler.error_text), "%s",
        errors != NULL ? errors : "");
    ui->assembler.error_scroll_x = 0;
    ui->assembler.error_scroll_y = 0;
    ui->assembler.error_dialog_open = true;
    ui->misc.active_tab = FRONTEND_MISC_TAB_ASSEMBLER;
}

void frontend_set_load_bin_path(frontend *ui, const char *path)
{
    if (ui == NULL || path == NULL) {
        return;
    }

    snprintf(ui->load_bin_dialog.path, sizeof(ui->load_bin_dialog.path), "%s", path);
}

void frontend_set_save_bin_path(frontend *ui, const char *path)
{
    if (ui == NULL || path == NULL) {
        return;
    }

    snprintf(ui->save_bin_dialog.path, sizeof(ui->save_bin_dialog.path), "%s", path);
}

void frontend_update_symbols(frontend *ui, const runtime_symbol_snapshot *snapshot)
{
    size_t i;

    if (ui == NULL || snapshot == NULL) {
        return;
    }

    if (ui->symbol_table == NULL) {
        ui->symbol_table = symbol_table_create();
    }
    if (ui->symbol_table == NULL) {
        return;
    }

    symbol_table_clear(ui->symbol_table);

    for (i = 0; i < snapshot->count; ++i) {
        symbol_table_add(
            ui->symbol_table,
            snapshot->entries[i].address,
            snapshot->entries[i].name,
            SYMBOL_SOURCE_ASSEMBLER,
            "assembler",
            true);
    }

    symbol_table_make_resolver(ui->symbol_table, &ui->symbols);
}

void frontend_destroy(frontend *ui)
{
    if (ui == NULL) {
        return;
    }

    if (ui->ctx != NULL) {
        nk_sdl_shutdown();
    }

    if (ui->display_texture != NULL) {
        SDL_DestroyTexture(ui->display_texture);
    }

    symbol_table_destroy(ui->symbol_table);
    frontend_config_dialog_reset(&ui->config_dialog);
    free(ui);
}

void frontend_begin_input(frontend *ui)
{
    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    nk_input_begin(ui->ctx);
}

void frontend_handle_event(frontend *ui, SDL_Event *event)
{
    if (ui == NULL || ui->ctx == NULL || event == NULL) {
        return;
    }

    if (help_view_is_open(&ui->help)) {
        nk_sdl_handle_event(event);
        return;
    }

    if (frontend_any_dialog_open(ui) && event->type == SDL_MOUSEBUTTONDOWN) {
        float x = (float)event->button.x;
        float y = (float)event->button.y;
        if (!frontend_click_in_any_dialog(ui, x, y)) {
            return;
        }
    }

    if (!frontend_any_dialog_open(ui) &&
        event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        float x = (float)event->button.x;
        float y = (float)event->button.y;

        if (frontend_point_in_rect(x, y, ui->layout.display)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_C64);
        } else if (frontend_point_in_rect(x, y, ui->layout.disassembly)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_DISASSEMBLY);
        } else if (frontend_point_in_rect(x, y, ui->layout.memory)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MEMORY);
        } else if (frontend_point_in_rect(x, y, ui->layout.misc)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_MISC);
        } else if (frontend_point_in_rect(x, y, ui->layout.registers)) {
            frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_NONE);
        }
    }

    if (event->type == SDL_KEYDOWN &&
        event->key.repeat == 0 &&
        event->key.keysym.sym == SDLK_ESCAPE) {
        ui->cancel_register_edit_requested = true;
    }

    if (event->type == SDL_KEYDOWN && !ui->c64_input_active && !frontend_any_dialog_open(ui)) {
        ui->pending_memory_key = event->key;
        ui->has_pending_memory_key = true;
        ui->pending_disassembly_key = event->key;
        ui->has_pending_disassembly_key = true;
    }

    nk_sdl_handle_event(event);
}

bool frontend_routes_keyboard_to_c64(const frontend *ui)
{
    return ui != NULL && ui->c64_input_active
        && !help_view_is_open(&ui->help)
        && !frontend_any_dialog_open(ui);
}

bool frontend_handle_view_cycle_key(frontend *ui, const SDL_KeyboardEvent *key)
{
    bool reverse;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN || key->repeat != 0) {
        return false;
    }
    if (help_view_is_open(&ui->help) || frontend_any_dialog_open(ui)) {
        return false;
    }
    if (key->keysym.sym != SDLK_TAB || (key->keysym.mod & KMOD_ALT) == 0) {
        return false;
    }

    reverse = (key->keysym.mod & KMOD_SHIFT) != 0;
    frontend_set_active_view(ui, frontend_next_active_view(ui->active_view, reverse));
    return true;
}

void frontend_end_input(frontend *ui)
{
    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    nk_input_end(ui->ctx);
}

bool frontend_submit_frame(frontend *ui, const c64_frame *frame)
{
    if (ui == NULL || frame == NULL || ui->renderer == NULL) {
        return false;
    }

    if (frame->width != C64_FRAME_WIDTH ||
        frame->height != C64_FRAME_HEIGHT ||
        frame->stride_bytes != C64_FRAME_WIDTH * sizeof(frame->pixels[0]) ||
        frame->pixel_format != C64_FRAME_PIXEL_FORMAT_ARGB8888) {
        SDL_Log("unexpected frame format: %ux%u stride=%u format=%u",
            frame->width,
            frame->height,
            frame->stride_bytes,
            frame->pixel_format);
        return false;
    }

    if (ui->display_texture == NULL) {
        ui->display_texture = SDL_CreateTexture(
            ui->renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            (int)frame->width,
            (int)frame->height);
        if (ui->display_texture == NULL) {
            SDL_Log("SDL_CreateTexture failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(ui->display_texture, SDL_BLENDMODE_NONE);
    }

    if (SDL_UpdateTexture(ui->display_texture, NULL, frame->pixels, (int)frame->stride_bytes) != 0) {
        SDL_Log("SDL_UpdateTexture failed: %s", SDL_GetError());
        return false;
    }

    ui->current_frame = *frame;
    ui->has_frame = true;
    return true;
}

static bool frontend_push_load_bin_execute_intent(
    frontend *ui,
    const char *path,
    uint16_t address,
    bool use_file_address,
    bool reset_first,
    bool is_basic)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE;
    snprintf(
        ui->intents[ui->intent_write].load_bin_path,
        sizeof(ui->intents[ui->intent_write].load_bin_path),
        "%s", path);
    ui->intents[ui->intent_write].load_bin_address = address;
    ui->intents[ui->intent_write].load_bin_use_file_address = use_file_address;
    ui->intents[ui->intent_write].load_bin_reset_first = reset_first;
    ui->intents[ui->intent_write].load_bin_is_basic = is_basic;
    ui->intent_write = next;
    return true;
}

static bool frontend_push_save_bin_execute_intent(
    frontend *ui,
    const char *path,
    uint16_t start,
    uint16_t end,
    bool write_file_address,
    bool is_basic)
{
    size_t next;

    if (ui == NULL || path == NULL) {
        return false;
    }

    next = (ui->intent_write + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    if (next == ui->intent_read) {
        return false;
    }

    memset(&ui->intents[ui->intent_write], 0, sizeof(ui->intents[ui->intent_write]));
    ui->intents[ui->intent_write].type = FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE;
    snprintf(
        ui->intents[ui->intent_write].save_bin_path,
        sizeof(ui->intents[ui->intent_write].save_bin_path),
        "%s", path);
    ui->intents[ui->intent_write].save_bin_start = start;
    ui->intents[ui->intent_write].save_bin_end = end;
    ui->intents[ui->intent_write].save_bin_write_file_address = write_file_address;
    ui->intents[ui->intent_write].save_bin_is_basic = is_basic;
    ui->intent_write = next;
    return true;
}

static void frontend_draw_load_bin_dialog(frontend *ui, int width, int height)
{
    frontend_load_bin_dialog_state *dlg;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_flags = NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD;
    uint16_t address;

    if (ui == NULL || !ui->load_bin_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dlg = &ui->load_bin_dialog;
    bounds = nk_rect((float)(width - 420) * 0.5f, (float)(height - 240) * 0.5f, 420.0f, 240.0f);
    if (bounds.x < 8.0f) bounds.x = 8.0f;
    if (bounds.y < 8.0f) bounds.y = 8.0f;

    if (nk_begin(ctx, "Load", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Load")) {
            /* Name / Browse row */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.60f);
            frontend_edit_replace(ctx, edit_flags, dlg->path, (int)sizeof(dlg->path), nk_filter_default);
            nk_layout_row_push(ctx, 0.22f);
            if (nk_button_label(ctx, "Browse...")) {
                frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE);
            }
            nk_layout_row_end(ctx);

            /* File address checkbox + address box row */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Address", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.38f);
            frontend_checkbox_bool(ctx, "From file", &dlg->use_file_address);
            nk_layout_row_push(ctx, 0.44f);
            frontend_edit_replace(
                ctx, dlg->use_file_address ? (edit_flags | NK_EDIT_READ_ONLY) : edit_flags,
                dlg->address_buf, (int)sizeof(dlg->address_buf), nk_filter_hex);
            nk_layout_row_end(ctx);

            /* Reset and Basic Program checkboxes */
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            frontend_checkbox_bool(ctx, "Reset before load", &dlg->reset_first);
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            frontend_checkbox_bool(ctx, "Basic Program (fix BASIC pointers)", &dlg->basic_program);

            /* Error / spacer */
            if (dlg->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dlg->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            /* Buttons */
            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dlg->open = false;
            }
            if (nk_button_label(ctx, "OK")) {
                if (dlg->path[0] == '\0') {
                    snprintf(dlg->error, sizeof(dlg->error), "No file selected");
                } else if (!dlg->use_file_address &&
                           !frontend_parse_hex16_text(dlg->address_buf, &address)) {
                    snprintf(dlg->error, sizeof(dlg->error), "Invalid address (XXXX hex required)");
                } else {
                    if (dlg->use_file_address) {
                        address = 0;
                    }
                    frontend_push_load_bin_execute_intent(
                        ui, dlg->path, address,
                        dlg->use_file_address, dlg->reset_first, dlg->basic_program);
                    dlg->open = false;
                }
            }
        } else {
            dlg->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Load")) {
        dlg->open = false;
    }
    nk_end(ctx);
}

static void frontend_draw_save_bin_dialog(frontend *ui, int width, int height)
{
    frontend_save_bin_dialog_state *dlg;
    struct nk_context *ctx;
    struct nk_rect bounds;
    nk_flags edit_flags = NK_EDIT_FIELD | NK_EDIT_SELECTABLE | NK_EDIT_CLIPBOARD;
    nk_flags ro_flags   = edit_flags | NK_EDIT_READ_ONLY;
    uint16_t start_addr;
    uint16_t end_addr;

    if (ui == NULL || !ui->save_bin_dialog.open || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    dlg = &ui->save_bin_dialog;
    bounds = nk_rect((float)(width - 420) * 0.5f, (float)(height - 260) * 0.5f, 420.0f, 260.0f);
    if (bounds.x < 8.0f) bounds.x = 8.0f;
    if (bounds.y < 8.0f) bounds.y = 8.0f;

    if (nk_begin(ctx, "Save", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_CLOSABLE)) {
        if (!nk_window_is_closed(ctx, "Save")) {
            /* Name / Browse row */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 3);
            nk_layout_row_push(ctx, 0.18f);
            nk_label(ctx, "Name", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.60f);
            frontend_edit_replace(ctx, edit_flags, dlg->path, (int)sizeof(dlg->path), nk_filter_default);
            nk_layout_row_push(ctx, 0.22f);
            if (nk_button_label(ctx, "Browse...")) {
                frontend_push_simple_intent(ui, FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE);
            }
            nk_layout_row_end(ctx);

            /* Basic Program checkbox — when on, forces header and disables Start/End */
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            frontend_checkbox_bool(ctx, "Basic Program (use BASIC pointers)", &dlg->basic_program);
            if (dlg->basic_program) {
                dlg->write_file_address = true;
            }

            /* Write address header checkbox (locked on when basic_program) */
            nk_layout_row_dynamic(ctx, 24.0f, 1);
            if (dlg->basic_program) {
                nk_label(ctx, "  Write address header (on)", NK_TEXT_LEFT);
            } else {
                frontend_checkbox_bool(ctx, "Write address header", &dlg->write_file_address);
            }

            /* Start / End address row (read-only when basic_program) */
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 4);
            nk_layout_row_push(ctx, 0.22f);
            nk_label(ctx, "Start", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.28f);
            frontend_edit_replace(
                ctx, dlg->basic_program ? ro_flags : edit_flags,
                dlg->start_address_buf, (int)sizeof(dlg->start_address_buf), nk_filter_hex);
            nk_layout_row_push(ctx, 0.22f);
            nk_label(ctx, "End", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.28f);
            frontend_edit_replace(
                ctx, dlg->basic_program ? ro_flags : edit_flags,
                dlg->end_address_buf, (int)sizeof(dlg->end_address_buf), nk_filter_hex);
            nk_layout_row_end(ctx);

            /* Error / spacer */
            if (dlg->error[0] != '\0') {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(ctx, dlg->error, NK_TEXT_LEFT, nk_rgb(255, 128, 118));
            } else {
                nk_layout_row_dynamic(ctx, 8.0f, 1);
                nk_spacing(ctx, 1);
            }

            /* Buttons */
            nk_layout_row_dynamic(ctx, 24.0f, 2);
            if (nk_button_label(ctx, "Cancel")) {
                dlg->open = false;
            }
            if (nk_button_label(ctx, "OK")) {
                if (dlg->path[0] == '\0') {
                    snprintf(dlg->error, sizeof(dlg->error), "No file selected");
                } else if (!dlg->basic_program &&
                           !frontend_parse_hex16_text(dlg->start_address_buf, &start_addr)) {
                    snprintf(dlg->error, sizeof(dlg->error), "Invalid start address (XXXX hex required)");
                } else if (!dlg->basic_program &&
                           !frontend_parse_hex16_text(dlg->end_address_buf, &end_addr)) {
                    snprintf(dlg->error, sizeof(dlg->error), "Invalid end address (XXXX hex required)");
                } else if (!dlg->basic_program && start_addr > end_addr) {
                    snprintf(dlg->error, sizeof(dlg->error), "Start must be <= end");
                } else {
                    if (dlg->basic_program) {
                        start_addr = 0;
                        end_addr   = 0;
                    }
                    frontend_push_save_bin_execute_intent(
                        ui, dlg->path, start_addr, end_addr,
                        dlg->write_file_address, dlg->basic_program);
                    dlg->open = false;
                }
            }
        } else {
            dlg->open = false;
        }
    } else if (nk_window_is_closed(ctx, "Save")) {
        dlg->open = false;
    }
    nk_end(ctx);
}

static void frontend_draw_assembler_error_dialog(frontend *ui, int width, int height)
{
    struct nk_context *ctx;
    frontend_assembler_state *asm_state;
    struct nk_rect bounds;
    float dialog_w, dialog_h;

    if (ui == NULL || ui->ctx == NULL || !ui->assembler.error_dialog_open) {
        return;
    }

    ctx = ui->ctx;
    asm_state = &ui->assembler;
    dialog_w = (float)width * 0.7f;
    if (dialog_w < 400.0f) dialog_w = 400.0f;
    dialog_h = (float)height * 0.6f;
    if (dialog_h < 200.0f) dialog_h = 200.0f;
    bounds = nk_rect(
        ((float)width - dialog_w) * 0.5f,
        ((float)height - dialog_h) * 0.5f,
        dialog_w, dialog_h);

    if (nk_begin(ctx, "Assembly Errors", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        float content_h = dialog_h - 80.0f;
        if (content_h < 60.0f) content_h = 60.0f;

        nk_layout_row_dynamic(ctx, content_h, 1);
        {
            const struct nk_user_font *font = ctx->style.font;
            float max_row_w = dialog_w;
            {
                const char *sp = asm_state->error_text;
                const char *se = sp + strlen(sp);
                while (sp < se) {
                    const char *nl = strchr(sp, '\n');
                    int line_chars = nl ? (int)(nl - sp) : (int)(se - sp);
                    float w = font->width(font->userdata, font->height, sp, line_chars) + 20.0f;
                    if (w > max_row_w) max_row_w = w;
                    sp = nl ? nl + 1 : se;
                }
            }
            struct nk_scroll scroll = { asm_state->error_scroll_x, asm_state->error_scroll_y };
            if (nk_group_scrolled_begin(ctx, &scroll, "asm-error-scroll", NK_WINDOW_BORDER)) {
                const char *p = asm_state->error_text;
                const char *end = p + strlen(p);
                while (p < end) {
                    const char *nl = strchr(p, '\n');
                    size_t len = nl != NULL ? (size_t)(nl - p) : (size_t)(end - p);
                    char line[256];
                    if (len >= sizeof(line)) len = sizeof(line) - 1u;
                    memcpy(line, p, len);
                    line[len] = '\0';
                    nk_layout_row_static(ctx, 16.0f, (int)max_row_w, 1);
                    nk_label(ctx, line, NK_TEXT_LEFT);
                    p = nl != NULL ? nl + 1 : end;
                }
                nk_group_scrolled_end(ctx);
            }
            asm_state->error_scroll_x = scroll.x;
            asm_state->error_scroll_y = scroll.y;
        }

        nk_layout_row_dynamic(ctx, 28.0f, 3);
        nk_spacing(ctx, 1);
        if (nk_button_label(ctx, "OK")) {
            asm_state->error_dialog_open = false;
        }
        nk_spacing(ctx, 1);
    }
    nk_end(ctx);
}

static void frontend_render_display_only(frontend *ui)
{
    int width = 0;
    int height = 0;
    SDL_Rect dest;

    if (ui == NULL || ui->display_texture == NULL || !ui->has_frame) {
        return;
    }

    platform_window_get_size(ui->window, &width, &height);
    dest = frontend_fit_rect(0, 0, width, height, FRONTEND_DISPLAY_CROP_W, FRONTEND_DISPLAY_CROP_H);
    {
        SDL_Rect src = {
            FRONTEND_DISPLAY_CROP_X,
            FRONTEND_DISPLAY_CROP_Y,
            FRONTEND_DISPLAY_CROP_W,
            FRONTEND_DISPLAY_CROP_H,
        };
        SDL_RenderCopy(ui->renderer, ui->display_texture, &src, &dest);
    }
}

void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state)
{
    int width = 0;
    int height = 0;
    struct nk_rect parent;
    int split_display_active = 0;
    int split_top_bottom_active = 0;
    int split_memory_misc_active = 0;
    int display_corner_active = 0;
    bool debugger_scrollbar_active = false;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    if (!ui->input_focus_initialized) {
        frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_C64);
        ui->input_focus_initialized = true;
    }

    if (!ui_visible && !help_view_is_open(&ui->help)) {
        frontend_render_display_only(ui);
        return;
    }

    if (debug_state == NULL && ui_visible && !help_view_is_open(&ui->help)) {
        return;
    }

    platform_window_get_size(ui->window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    if (help_view_is_open(&ui->help)) {
        frontend_render_display_only(ui);
        help_view_render(ui->ctx, &ui->help, ui->help_font, width, height);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        return;
    }

    if (ui_visible && debug_state != NULL) {
        parent = nk_rect(0.0f, 0.0f, (float)width, (float)height);
        c64_layout_compute(&ui->layout, parent, &ui->limits);
        if (!frontend_any_dialog_open(ui)) {
            debugger_scrollbar_active = ui->memory.scrollbar_dragging ||
                ui->disassembly.scrollbar_dragging ||
                (ui->has_memory_scrollbar_bounds &&
                 nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
                 nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->memory_scrollbar_bounds)) ||
                (ui->has_disassembly_scrollbar_bounds &&
                 nk_input_is_mouse_down(&ui->ctx->input, NK_BUTTON_LEFT) &&
                 nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->disassembly_scrollbar_bounds));
            if (!debugger_scrollbar_active) {
                c64_layout_handle_drag(&ui->layout, &ui->ctx->input, parent, &ui->limits);
            } else {
                ui->layout.drag_active = C64_LAYOUT_DRAG_NONE;
            }
        } else {
            ui->layout.drag_active = C64_LAYOUT_DRAG_NONE;
        }

        frontend_draw_display_placeholder(ui, ui->layout.display);
        frontend_draw_registers(ui, ui->layout.registers, debug_state);
        frontend_draw_disassembly_view(ui, ui->layout.disassembly, debug_state);
        frontend_draw_memory(ui, ui->layout.memory, debug_state);
        frontend_draw_misc(ui, ui->layout.misc, debug_state);

        if (!frontend_any_dialog_open(ui)) {
            split_display_active = ui->layout.drag_active == C64_LAYOUT_DRAG_SPLIT_DISPLAY ||
                nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_display);
            split_top_bottom_active = ui->layout.drag_active == C64_LAYOUT_DRAG_SPLIT_TOP_BOTTOM ||
                nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_top_bottom);
            split_memory_misc_active = ui->layout.drag_active == C64_LAYOUT_DRAG_SPLIT_BOTTOM ||
                nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_memory_misc);
            display_corner_active = ui->layout.drag_active == C64_LAYOUT_DRAG_DISPLAY_CORNER ||
                nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_display_corner);
        }

        frontend_draw_splitter(ui->ctx, "split-display", ui->layout.hit_split_display, split_display_active);
        frontend_draw_splitter(ui->ctx, "split-top-bottom", ui->layout.hit_split_top_bottom, split_top_bottom_active);
        frontend_draw_splitter(ui->ctx, "split-memory-misc", ui->layout.hit_split_memory_misc, split_memory_misc_active);
        frontend_draw_corner_handle(ui->ctx, ui->layout.hit_display_corner, display_corner_active);
        frontend_draw_config_dialog(ui, width, height);
        frontend_draw_breakpoint_editor(ui, width, height);
        frontend_draw_load_bin_dialog(ui, width, height);
        frontend_draw_save_bin_dialog(ui, width, height);
        frontend_draw_assembler_error_dialog(ui, width, height);
    } else {
        frontend_render_display_only(ui);
    }

    nk_sdl_render(NK_ANTI_ALIASING_ON);
}

void frontend_open_help(frontend *ui, bool paused_by_help)
{
    if (ui == NULL) {
        return;
    }
    help_view_open(&ui->help, paused_by_help);
    frontend_set_active_view(ui, FRONTEND_ACTIVE_VIEW_NONE);
}

bool frontend_close_help(frontend *ui)
{
    bool paused_by_help;

    if (ui == NULL || !help_view_is_open(&ui->help)) {
        return false;
    }

    paused_by_help = help_view_paused_by_help(&ui->help);
    help_view_close(&ui->help);
    return paused_by_help;
}

bool frontend_help_is_open(const frontend *ui)
{
    return ui != NULL && help_view_is_open(&ui->help);
}

bool frontend_help_paused_by_help(const frontend *ui)
{
    return ui != NULL && help_view_paused_by_help(&ui->help);
}

bool frontend_handle_help_key(frontend *ui, const SDL_KeyboardEvent *key)
{
    nk_uint page;

    if (ui == NULL || ui->ctx == NULL || key == NULL || !help_view_is_open(&ui->help)) {
        return false;
    }

    page = ui->help.content_page_y > 0 ? ui->help.content_page_y : 400u;
    switch (key->keysym.sym) {
        case SDLK_PAGEUP:
            return help_view_scroll_content(ui->ctx, &ui->help, -(int)page);
        case SDLK_PAGEDOWN:
            return help_view_scroll_content(ui->ctx, &ui->help, (int)page);
        case SDLK_HOME:
            return help_view_scroll_content_to(ui->ctx, &ui->help, 0);
        case SDLK_END:
            return help_view_scroll_content_to(ui->ctx, &ui->help, 0x3fffffffu);
        case SDLK_LEFT:
            return help_view_select_section(ui->ctx, &ui->help, ui->help.section_index - 1);
        case SDLK_RIGHT:
            return help_view_select_section(ui->ctx, &ui->help, ui->help.section_index + 1);
        default:
            break;
    }

    return false;
}

bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent)
{
    if (ui == NULL || out_intent == NULL || ui->intent_read == ui->intent_write) {
        return false;
    }

    *out_intent = ui->intents[ui->intent_read];
    memset(&ui->intents[ui->intent_read], 0, sizeof(ui->intents[ui->intent_read]));
    ui->intent_read = (ui->intent_read + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    return true;
}

bool frontend_get_disassembly_cursor(const frontend *ui, uint16_t *out_address) {
    if (ui == NULL || out_address == NULL || !ui->disassembly.has_user_cursor) {
        return false;
    }
    *out_address = ui->disassembly.cursor_address;
    return true;
}
