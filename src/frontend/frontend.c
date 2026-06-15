#include "frontend.h"

#include "nuklear_config.h"
#include "nuklear_sdl.h"

#include "c64_layout.h"
#include "disasm_6502.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FRONTEND_DEBUGGER_INTENT_CAPACITY = 32
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

struct frontend {
    platform_window *window;
    struct nk_context *ctx;
    SDL_Renderer *renderer;
    SDL_Texture *display_texture;
    c64_frame current_frame;
    bool has_frame;
    c64_layout layout;
    c64_layout_limits limits;
    frontend_register_view_state registers;
    frontend_memory_view_state memory;
    frontend_disassembly_view_state disassembly;
    symbol_resolver symbols;
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

static void frontend_push_debugger_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint16_t value);

static void frontend_push_breakpoint_id_intent(
    frontend *ui,
    frontend_debugger_intent_type type,
    uint32_t id,
    bool enabled);

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
        if (entry->access == RUNTIME_BREAKPOINT_ACCESS_EXECUTE && entry->address == address) {
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

    ui->intents[ui->intent_write].type = type;
    ui->intents[ui->intent_write].id = id;
    ui->intents[ui->intent_write].enabled = enabled;
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
            struct nk_image image = nk_image_handle(nk_handle_ptr(ui->display_texture));
            struct nk_rect image_bounds = frontend_fit_nk_rect(
                canvas_bounds,
                ui->current_frame.width,
                ui->current_frame.height);

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
    nk_flags edit_flags = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER;
    nk_flags result;

    if (!editable) {
        edit_flags |= NK_EDIT_READ_ONLY;
    }

    result = nk_edit_string_zero_terminated(ui->ctx, edit_flags, buffer, max, filter);
    if ((result & NK_EDIT_ACTIVATED) != 0 && editable) {
        ui->registers.active_field = field;
    }
    if ((result & NK_EDIT_COMMITED) != 0) {
        frontend_commit_register_edit(ui, field, debug_state);
        ui->registers.active_field = FRONTEND_REGISTER_FIELD_NONE;
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
    bool ctrl;
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
    ctrl = (mod & KMOD_CTRL) != 0;

    if ((mod & KMOD_ALT) != 0 && sym == SDLK_b) {
        frontend_disassembly_ensure_user_cursor(ui, debug_state);
        frontend_toggle_execute_breakpoint_at_cursor(ui, debug_state);
        return;
    }

    if ((mod & KMOD_ALT) != 0 || sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 || sym == SDLK_F12) {
        return;
    }

    if (ctrl && sym == SDLK_a) {
        frontend_disassembly_ensure_user_cursor(ui, debug_state);
        view->address_entry = !view->address_entry;
        view->active_address_digit = 0;
        return;
    }

    if (ctrl && sym == SDLK_s) {
        if (ui->symbols.enumerate != NULL) {
            (void)ui->symbols.enumerate(ui->symbols.userdata, NULL, 0);
        }
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
        if (ctrl) {
            frontend_disassembly_scroll_to_top(ui, 0x0000);
        } else if (view->rows > 0) {
            frontend_disassembly_set_user_cursor(view, view->lines[0].address, 0, view->lines[0].length);
        }
        return;
    }

    if (sym == SDLK_END) {
        if (ctrl) {
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
        if (ctrl && debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED) {
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
        ui->disassembly.active = true;
        ui->memory.active = false;
        return;
    }

    ui->disassembly.active = true;
    ui->memory.active = false;
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
        view->active = true;
        ui->memory.active = false;
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
        view->active = true;
        ui->memory.active = false;
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
    float footer_h;
    float scrollbar_w = 24.0f;
    float scrollbar_margin = 8.0f;
    float content_h;

    if (!view->initialized) {
        view->mode = RUNTIME_MEMORY_MODE_CPU_MAP;
        view->follow_pc = true;
        view->active = false;
        view->initialized = true;
        if (debug_state != NULL && debug_state->has_cpu) {
            view->top_address = debug_state->cpu.pc;
        }
    }

    row_h = ui->ctx->style.font != NULL ? ui->ctx->style.font->height : 13.0f;
    footer_h = 22.0f;
    content_h = bounds.h - 28.0f - footer_h;
    rows = (uint8_t)((content_h > row_h) ? (content_h / row_h) : 1);
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

    if (nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT)) {
        view->active = nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) ? true : false;
        if (view->active) {
            ui->memory.active = false;
        }
    }

    if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) &&
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
                char rendered[96];
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

                snprintf(rendered, sizeof(rendered), "%c%c %04X  %-8s  %s",
                    is_pc ? '>' : ' ',
                    is_breakpoint ? (is_enabled_breakpoint ? 'X' : 'x') : ' ',
                    line->address,
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
                        view->active = true;
                        ui->memory.active = false;
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

        nk_layout_row_begin(ui->ctx, NK_DYNAMIC, footer_h, 4);
        nk_layout_row_push(ui->ctx, 0.30f);
        if (nk_button_label(ui->ctx, view->mode == RUNTIME_MEMORY_MODE_CPU_MAP ? "CPU map" : "RAM")) {
            view->mode = view->mode == RUNTIME_MEMORY_MODE_CPU_MAP ?
                RUNTIME_MEMORY_MODE_RAM :
                RUNTIME_MEMORY_MODE_CPU_MAP;
            view->request_pending = false;
        }
        nk_layout_row_push(ui->ctx, 0.25f);
        nk_label(ui->ctx, view->address_entry ? "Address" : "Cursor", NK_TEXT_LEFT);
        nk_layout_row_push(ui->ctx, 0.25f);
        {
            char label[32];
            snprintf(label, sizeof(label), "PC: %04X", debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.pc : 0);
            nk_label(ui->ctx, label, NK_TEXT_LEFT);
        }
        nk_layout_row_push(ui->ctx, 0.20f);
        nk_label(ui->ctx, view->symbol_display_mode == 0 ? "symbols: auto" :
            (view->symbol_display_mode == 1 ? "symbols: names" : "symbols: raw"), NK_TEXT_LEFT);
        nk_layout_row_end(ui->ctx);

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

    if (debug_state == NULL || !debug_state->has_memory) {
        return 0;
    }

    index = frontend_memory_snapshot_index(&debug_state->memory, address);
    if (index < 0) {
        return 0;
    }

    return debug_state->memory.bytes[index];
}

static char frontend_memory_ascii(uint8_t value)
{
    if (value >= 32 && value <= 126) {
        return (char)value;
    }

    return '.';
}

static void frontend_memory_request_if_needed(frontend *ui)
{
    frontend_memory_view_state *memory = &ui->memory;
    uint16_t length = frontend_memory_visible_count(memory);

    if (length == 0) {
        return;
    }

    if (!memory->request_pending ||
        memory->requested_address != memory->view_address ||
        memory->requested_length != length ||
        memory->requested_mode != memory->mode) {
        frontend_push_memory_request(ui, memory->view_address, length, memory->mode);
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
        memory->active = true;
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
        memory->active = true;
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

    ui->memory.active = true;
    ui->disassembly.active = false;
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
    bool ctrl;

    if (ui == NULL || key == NULL || key->type != SDL_KEYDOWN) {
        return;
    }

    sym = key->keysym.sym;
    mod = key->keysym.mod;
    ctrl = (mod & KMOD_CTRL) != 0;

    if ((mod & KMOD_ALT) != 0 || sym == SDLK_F9 || sym == SDLK_F10 || sym == SDLK_F11 || sym == SDLK_F12) {
        return;
    }

    if (ctrl && sym == SDLK_a) {
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

    if (ctrl && sym == SDLK_t) {
        ui->memory.edit_field = ui->memory.edit_field == FRONTEND_MEMORY_EDIT_ASCII ?
            FRONTEND_MEMORY_EDIT_HEX :
            FRONTEND_MEMORY_EDIT_ASCII;
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
        } else if (ctrl) {
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
        } else if (ctrl) {
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
            frontend_memory_write_byte(ui, debug_state, ui->memory.cursor_address, (uint8_t)sym);
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
        memory->active = true;
        memory->initialized = true;
    }

    row_h = ui->ctx->style.font != NULL ? ui->ctx->style.font->height : 13.0f;
    footer_h = row_h + 3.0f;

    if (nk_input_is_mouse_pressed(&ui->ctx->input, NK_BUTTON_LEFT)) {
        memory->active = nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) ? true : false;
    }

    rows = (uint8_t)((bounds.h > footer_h + 28.0f) ? ((bounds.h - footer_h - 28.0f) / row_h) : 1);
    if (rows == 0) {
        rows = 1;
    }
    if (rows > RUNTIME_MEMORY_SNAPSHOT_MAX / memory->columns) {
        rows = RUNTIME_MEMORY_SNAPSHOT_MAX / memory->columns;
    }
    memory->rows = rows;
    visible_count = frontend_memory_visible_count(memory);

    if (nk_input_is_mouse_hovering_rect(&ui->ctx->input, bounds) &&
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

    frontend_memory_request_if_needed(ui);

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

        nk_layout_row_begin(ui->ctx, NK_DYNAMIC, 22.0f, 4);
        nk_layout_row_push(ui->ctx, 0.30f);
        if (nk_button_label(ui->ctx, memory->mode == RUNTIME_MEMORY_MODE_CPU_MAP ? "CPU map" : "RAM")) {
            memory->mode = memory->mode == RUNTIME_MEMORY_MODE_CPU_MAP ?
                RUNTIME_MEMORY_MODE_RAM :
                RUNTIME_MEMORY_MODE_CPU_MAP;
            memory->request_pending = false;
        }
        nk_layout_row_push(ui->ctx, 0.25f);
        nk_label(ui->ctx, memory->edit_field == FRONTEND_MEMORY_EDIT_ASCII ? "ASCII" :
            (memory->edit_field == FRONTEND_MEMORY_EDIT_ADDRESS ? "Address" : "Hex"), NK_TEXT_LEFT);
        nk_layout_row_push(ui->ctx, 0.25f);
        {
            char label[32];
            snprintf(label, sizeof(label), "Address: %04X", memory->cursor_address);
            nk_label(ui->ctx, label, NK_TEXT_LEFT);
        }
        nk_layout_row_push(ui->ctx, 0.20f);
        nk_label(ui->ctx,
            debug_state != NULL && debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED ? "editable" : "read-only",
            NK_TEXT_LEFT);
        nk_layout_row_end(ui->ctx);

        ui->ctx->style.window = saved_window_style;
    }
    nk_end(ui->ctx);
}

static void frontend_draw_misc(frontend *ui, struct nk_rect bounds, const frontend_debug_state *debug_state)
{
    struct nk_context *ctx;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    ctx = ui->ctx;
    if (nk_begin(ctx, "Misc", bounds, NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        uint16_t i;
        uint16_t count = debug_state != NULL && debug_state->has_breakpoints ?
            debug_state->breakpoints.count :
            0;

        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "Debug Status", NK_TEXT_LEFT);
        {
            char status[96];
            snprintf(
                status,
                sizeof(status),
                "State: %s  PC: %04X  Cycles: %llu",
                debug_state != NULL ? frontend_runtime_state_name(debug_state->runtime_state) : "UNKNOWN",
                debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.pc : 0,
                (unsigned long long)(debug_state != NULL && debug_state->has_cpu ? debug_state->cpu.cycles : 0));
            nk_label(ctx, status, NK_TEXT_LEFT);
        }

        nk_layout_row_dynamic(ctx, 8.0f, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "Breakpoints", NK_TEXT_LEFT);

        if (count == 0) {
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, "No breakpoints set", NK_TEXT_LEFT);
        }

        for (i = 0; i < count; ++i) {
            const runtime_breakpoint_snapshot_entry *entry = &debug_state->breakpoints.entries[i];
            struct nk_style_button saved_button = ctx->style.button;
            char label[32];

            if (entry->enabled == 0) {
                ctx->style.button.text_normal = nk_rgb(180, 142, 210);
                ctx->style.button.normal = nk_style_item_color(nk_rgb(40, 34, 48));
            }

            snprintf(label, sizeof(label), "X Break [%04X]", entry->address);
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24.0f, 5);
            nk_layout_row_push(ctx, 0.30f);
            nk_label_colored(
                ctx,
                label,
                NK_TEXT_LEFT,
                entry->enabled != 0 ? nk_rgb(232, 235, 238) : nk_rgb(180, 142, 210));
            nk_layout_row_push(ctx, 0.15f);
            nk_button_label(ctx, "Edit");
            nk_layout_row_push(ctx, 0.17f);
            if (nk_button_label(ctx, entry->enabled != 0 ? "Disable" : "Enable")) {
                frontend_push_breakpoint_id_intent(
                    ui,
                    FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED,
                    entry->id,
                    entry->enabled == 0);
            }
            nk_layout_row_push(ctx, 0.18f);
            if (nk_button_label(ctx, "View PC")) {
                frontend_disassembly_scroll_to_top(ui, frontend_disassembly_center_top(entry->address, ui->disassembly.rows));
                frontend_disassembly_set_user_cursor(&ui->disassembly, entry->address, ui->disassembly.rows / 2, 1);
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

        nk_layout_row_dynamic(ctx, 8.0f, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "Call Stack placeholder", NK_TEXT_LEFT);
        nk_label(ctx, "C64 Hardware placeholder", NK_TEXT_LEFT);
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

    if (event->type == SDL_KEYDOWN &&
        event->key.repeat == 0 &&
        event->key.keysym.sym == SDLK_ESCAPE) {
        ui->cancel_register_edit_requested = true;
    }

    if (event->type == SDL_KEYDOWN) {
        ui->pending_memory_key = event->key;
        ui->has_pending_memory_key = true;
        ui->pending_disassembly_key = event->key;
        ui->has_pending_disassembly_key = true;
    }

    nk_sdl_handle_event(event);
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

static void frontend_render_display_only(frontend *ui)
{
    int width = 0;
    int height = 0;
    SDL_Rect dest;

    if (ui == NULL || ui->display_texture == NULL || !ui->has_frame) {
        return;
    }

    platform_window_get_size(ui->window, &width, &height);
    dest = frontend_fit_rect(0, 0, width, height, (int)ui->current_frame.width, (int)ui->current_frame.height);
    SDL_RenderCopy(ui->renderer, ui->display_texture, NULL, &dest);
}

void frontend_render(frontend *ui, bool ui_visible, const frontend_debug_state *debug_state)
{
    int width = 0;
    int height = 0;
    struct nk_rect parent;
    int split_display_active;
    int split_top_bottom_active;
    int split_memory_misc_active;
    int display_corner_active;
    bool debugger_scrollbar_active = false;

    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    if (!ui_visible) {
        frontend_render_display_only(ui);
        return;
    }

    if (debug_state == NULL) {
        return;
    }

    platform_window_get_size(ui->window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    parent = nk_rect(0.0f, 0.0f, (float)width, (float)height);
    c64_layout_compute(&ui->layout, parent, &ui->limits);
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

    frontend_draw_display_placeholder(ui, ui->layout.display);
    frontend_draw_registers(ui, ui->layout.registers, debug_state);
    frontend_draw_disassembly_view(ui, ui->layout.disassembly, debug_state);
    frontend_draw_memory(ui, ui->layout.memory, debug_state);
    frontend_draw_misc(ui, ui->layout.misc, debug_state);

    split_display_active = ui->layout.drag_active == C64_LAYOUT_DRAG_SPLIT_DISPLAY ||
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_display);
    split_top_bottom_active = ui->layout.drag_active == C64_LAYOUT_DRAG_SPLIT_TOP_BOTTOM ||
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_top_bottom);
    split_memory_misc_active = ui->layout.drag_active == C64_LAYOUT_DRAG_SPLIT_BOTTOM ||
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_split_memory_misc);
    display_corner_active = ui->layout.drag_active == C64_LAYOUT_DRAG_DISPLAY_CORNER ||
        nk_input_is_mouse_hovering_rect(&ui->ctx->input, ui->layout.hit_display_corner);

    frontend_draw_splitter(ui->ctx, "split-display", ui->layout.hit_split_display, split_display_active);
    frontend_draw_splitter(ui->ctx, "split-top-bottom", ui->layout.hit_split_top_bottom, split_top_bottom_active);
    frontend_draw_splitter(ui->ctx, "split-memory-misc", ui->layout.hit_split_memory_misc, split_memory_misc_active);
    frontend_draw_corner_handle(ui->ctx, ui->layout.hit_display_corner, display_corner_active);
    nk_sdl_render(NK_ANTI_ALIASING_ON);
}

bool frontend_poll_debugger_intent(frontend *ui, frontend_debugger_intent *out_intent)
{
    if (ui == NULL || out_intent == NULL || ui->intent_read == ui->intent_write) {
        return false;
    }

    *out_intent = ui->intents[ui->intent_read];
    ui->intent_read = (ui->intent_read + 1u) % FRONTEND_DEBUGGER_INTENT_CAPACITY;
    return true;
}
