#include "frontend.h"

#include "nuklear_config.h"
#include "nuklear_sdl.h"

#include "c64_layout.h"

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
    frontend_debugger_intent intents[FRONTEND_DEBUGGER_INTENT_CAPACITY];
    size_t intent_read;
    size_t intent_write;
    bool cancel_register_edit_requested;
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

static void frontend_draw_disassembly(struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_begin(ctx, "Disassembly", bounds, pane_flags)) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "> $0000  -- -- --   pending runtime state", NK_TEXT_LEFT);
        nk_label(ctx, "  $0001  -- -- --", NK_TEXT_LEFT);
        nk_label(ctx, "  $0002  -- -- --", NK_TEXT_LEFT);
        nk_label(ctx, "  $0003  -- -- --", NK_TEXT_LEFT);
    }
    nk_end(ctx);
}

static void frontend_draw_memory(struct nk_context *ctx, struct nk_rect bounds)
{
    int row;

    if (nk_begin(ctx, "Memory View", bounds, pane_flags)) {
        for (row = 0; row < 10; ++row) {
            char line[96];
            snprintf(line, sizeof(line), "$%04x  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00", row * 16);
            nk_layout_row_dynamic(ctx, 18.0f, 1);
            nk_label(ctx, line, NK_TEXT_LEFT);
        }
    }
    nk_end(ctx);
}

static void frontend_draw_misc(struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_begin(ctx, "Misc", bounds, NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "SID registers placeholder", NK_TEXT_LEFT);
        nk_label(ctx, "VIC-II registers placeholder", NK_TEXT_LEFT);
        nk_label(ctx, "CIA state placeholder", NK_TEXT_LEFT);
        nk_label(ctx, "Disk images placeholder", NK_TEXT_LEFT);
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
    nk_sdl_font_stash_end();

    ui->limits.registers_h_px = 88;
    ui->limits.min_display_w_px = 220;
    ui->limits.min_right_w_px = 220;
    ui->limits.min_disassembly_h_px = 120;
    ui->limits.min_bottom_h_px = 150;
    ui->limits.min_memory_w_px = 260;
    ui->limits.min_misc_w_px = 180;
    ui->limits.gutter_px = 9;
    ui->limits.corner_px = 22;
    c64_layout_init(&ui->layout);

    return ui;
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
    c64_layout_handle_drag(&ui->layout, &ui->ctx->input, parent, &ui->limits);

    frontend_draw_display_placeholder(ui, ui->layout.display);
    frontend_draw_registers(ui, ui->layout.registers, debug_state);
    frontend_draw_disassembly(ui->ctx, ui->layout.disassembly);
    frontend_draw_memory(ui->ctx, ui->layout.memory);
    frontend_draw_misc(ui->ctx, ui->layout.misc);

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
