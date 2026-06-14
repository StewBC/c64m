#include "frontend.h"

#include "nuklear_config.h"
#include "nuklear_sdl.h"

#include "c64_layout.h"

#include <stdio.h>
#include <stdlib.h>

struct frontend {
    platform_window *window;
    struct nk_context *ctx;
    c64_layout layout;
    c64_layout_limits limits;
};

static const nk_flags pane_flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR;

static void frontend_label_value(struct nk_context *ctx, const char *label, const char *value)
{
    nk_layout_row_begin(ctx, NK_STATIC, 18.0f, 2);
    nk_layout_row_push(ctx, 42.0f);
    nk_label(ctx, label, NK_TEXT_LEFT);
    nk_layout_row_push(ctx, 90.0f);
    nk_label(ctx, value, NK_TEXT_LEFT);
    nk_layout_row_end(ctx);
}

static void frontend_draw_display_placeholder(struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_begin(ctx, "Commodore Display", bounds, pane_flags)) {
        struct nk_rect canvas_bounds;
        struct nk_command_buffer *canvas;

        nk_layout_row_dynamic(ctx, bounds.h - 52.0f, 1);
        canvas_bounds = nk_widget_bounds(ctx);
        canvas = nk_window_get_canvas(ctx);
        nk_fill_rect(canvas, canvas_bounds, 0.0f, nk_rgb(17, 22, 28));
        nk_stroke_rect(canvas, canvas_bounds, 0.0f, 1.0f, nk_rgb(75, 94, 112));
        nk_draw_text(
            canvas,
            nk_rect(canvas_bounds.x + 14.0f, canvas_bounds.y + 14.0f, canvas_bounds.w - 28.0f, 20.0f),
            "384 x 272 placeholder",
            21,
            ctx->style.font,
            nk_rgb(17, 22, 28),
            nk_rgb(196, 214, 228));
    }
    nk_end(ctx);
}

static void frontend_draw_registers(struct nk_context *ctx, struct nk_rect bounds)
{
    if (nk_begin(ctx, "CPU Registers", bounds, pane_flags)) {
        nk_layout_row_dynamic(ctx, 18.0f, 1);
        nk_label(ctx, "6510 placeholder", NK_TEXT_LEFT);
        frontend_label_value(ctx, "PC", "$0000");
        frontend_label_value(ctx, "A", "$00");
        frontend_label_value(ctx, "X", "$00");
        frontend_label_value(ctx, "Y", "$00");
        frontend_label_value(ctx, "SP", "$ff");
    }
    nk_end(ctx);
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
    ui->ctx = nk_sdl_init(sdl_window, sdl_renderer);
    if (ui->ctx == NULL) {
        free(ui);
        return NULL;
    }

    nk_sdl_font_stash_begin(&atlas);
    nk_sdl_font_stash_end();

    ui->limits.registers_h_px = 142;
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

    nk_sdl_handle_event(event);
}

void frontend_end_input(frontend *ui)
{
    if (ui == NULL || ui->ctx == NULL) {
        return;
    }

    nk_input_end(ui->ctx);
}

void frontend_render(frontend *ui, bool ui_visible)
{
    int width = 0;
    int height = 0;
    struct nk_rect parent;
    int split_display_active;
    int split_top_bottom_active;
    int split_memory_misc_active;
    int display_corner_active;

    if (ui == NULL || ui->ctx == NULL || !ui_visible) {
        return;
    }

    platform_window_get_size(ui->window, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    parent = nk_rect(0.0f, 0.0f, (float)width, (float)height);
    c64_layout_compute(&ui->layout, parent, &ui->limits);
    c64_layout_handle_drag(&ui->layout, &ui->ctx->input, parent, &ui->limits);

    frontend_draw_display_placeholder(ui->ctx, ui->layout.display);
    frontend_draw_registers(ui->ctx, ui->layout.registers);
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
