#include "help_view.h"

#include <stdio.h>
#include <string.h>

#include "generated/help_content.inc"

#define HELP_INLINE_CODE_ON '\001'
#define HELP_INLINE_CODE_OFF '\002'

#define HELP_COLOR_BG nk_rgb(18, 24, 31)
#define HELP_COLOR_PANEL nk_rgb(27, 36, 45)
#define HELP_COLOR_HEADING nk_rgb(134, 209, 255)
#define HELP_COLOR_BODY nk_rgb(231, 238, 240)
#define HELP_COLOR_H3 nk_rgb(255, 212, 122)
#define HELP_COLOR_BULLET nk_rgb(156, 231, 169)
#define HELP_COLOR_NUMBER nk_rgb(178, 190, 255)
#define HELP_COLOR_CODE nk_rgb(238, 198, 156)
#define HELP_COLOR_TABLE nk_rgb(160, 216, 204)

void help_view_init(frontend_help_state *state)
{
    if (state == NULL) {
        return;
    }
    state->open = false;
    state->paused_by_help = false;
    state->section_index = 0;
    state->pending_scroll_y = 0;
    state->content_page_y = 400;
    state->pending_scroll_restore = false;
    memset(state->section_scroll_y, 0, sizeof(state->section_scroll_y));
}

void help_view_open(frontend_help_state *state, bool paused_by_help)
{
    if (state == NULL) {
        return;
    }
    state->open = true;
    state->paused_by_help = paused_by_help;
    if (state->section_index < 0 || state->section_index >= help_section_count) {
        state->section_index = 0;
    }
    if (state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        state->pending_scroll_y = state->section_scroll_y[state->section_index];
        state->pending_scroll_restore = true;
    }
}

void help_view_close(frontend_help_state *state)
{
    if (state == NULL) {
        return;
    }
    state->open = false;
    state->paused_by_help = false;
}

bool help_view_is_open(const frontend_help_state *state)
{
    return state != NULL && state->open;
}

bool help_view_paused_by_help(const frontend_help_state *state)
{
    return state != NULL && state->paused_by_help;
}

static int help_view_clamp_section_index(int section_index)
{
    if (help_section_count <= 0) {
        return 0;
    }
    if (section_index < 0) {
        return 0;
    }
    if (section_index >= help_section_count) {
        return help_section_count - 1;
    }
    return section_index;
}

static void help_view_store_current_scroll(struct nk_context *ctx, frontend_help_state *state)
{
    nk_uint x = 0;
    nk_uint y = 0;

    if (ctx == NULL || state == NULL ||
        state->section_index < 0 ||
        state->section_index >= FRONTEND_HELP_MAX_SECTIONS) {
        return;
    }

    nk_group_get_scroll(ctx, "HelpContent", &x, &y);
    state->section_scroll_y[state->section_index] = y;
}

static void help_view_request_restore(frontend_help_state *state)
{
    if (state == NULL ||
        state->section_index < 0 ||
        state->section_index >= FRONTEND_HELP_MAX_SECTIONS) {
        return;
    }

    state->pending_scroll_y = state->section_scroll_y[state->section_index];
    state->pending_scroll_restore = true;
}

bool help_view_select_section(struct nk_context *ctx, frontend_help_state *state, int section_index)
{
    int next;

    if (ctx == NULL || state == NULL || help_section_count <= 0) {
        return false;
    }

    next = help_view_clamp_section_index(section_index);
    if (next == state->section_index) {
        return true;
    }

    help_view_store_current_scroll(ctx, state);
    state->section_index = next;
    help_view_request_restore(state);
    return true;
}

bool help_view_scroll_content(struct nk_context *ctx, frontend_help_state *state, int delta_y)
{
    nk_uint y = 0;

    if (ctx == NULL || state == NULL) {
        return false;
    }

    (void)ctx;
    if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        y = state->section_scroll_y[state->section_index];
    }
    if (delta_y < 0) {
        nk_uint amount = (nk_uint)(-delta_y);
        y = amount > y ? 0 : y - amount;
    } else {
        y += (nk_uint)delta_y;
    }
    if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        state->section_scroll_y[state->section_index] = y;
    }
    state->pending_scroll_y = y;
    state->pending_scroll_restore = true;
    return true;
}

bool help_view_scroll_content_to(struct nk_context *ctx, frontend_help_state *state, nk_uint y)
{
    if (ctx == NULL || state == NULL) {
        return false;
    }

    (void)ctx;
    if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
        state->section_scroll_y[state->section_index] = y;
    }
    state->pending_scroll_y = y;
    state->pending_scroll_restore = true;
    return true;
}

static float help_row_height(struct nk_context *ctx)
{
    if (ctx != NULL && ctx->style.font != NULL) {
        return ctx->style.font->height + 2.0f;
    }
    return 15.0f;
}

static float help_text_width(struct nk_context *ctx, const char *text, int len)
{
    const struct nk_user_font *font;

    if (ctx == NULL || text == NULL || len <= 0 || ctx->style.font == NULL) {
        return 0.0f;
    }

    font = ctx->style.font;
    return font->width(font->userdata, font->height, text, len);
}

static void help_draw_inline_at(
    struct nk_context *ctx,
    struct nk_command_buffer *canvas,
    struct nk_rect bounds,
    const char *text,
    struct nk_color base_color)
{
    const struct nk_user_font *font;
    const char *run;
    const char *p;
    float x;
    bool code = false;

    if (ctx == NULL || canvas == NULL || text == NULL || ctx->style.font == NULL) {
        return;
    }

    font = ctx->style.font;
    x = bounds.x;
    p = text;
    run = p;
    while (true) {
        if (*p == HELP_INLINE_CODE_ON || *p == HELP_INLINE_CODE_OFF || *p == '\0') {
            int len = (int)(p - run);
            if (len > 0) {
                float w = help_text_width(ctx, run, len);
                struct nk_rect segment = nk_rect(x, bounds.y, w + 1.0f, bounds.h);
                nk_draw_text(
                    canvas,
                    segment,
                    run,
                    len,
                    font,
                    HELP_COLOR_PANEL,
                    code ? HELP_COLOR_CODE : base_color);
                x += w;
            }
            if (*p == '\0') {
                break;
            }
            code = *p == HELP_INLINE_CODE_ON;
            ++p;
            run = p;
        } else {
            ++p;
        }
    }
}

static void help_inline_row(struct nk_context *ctx, const char *text, struct nk_color color)
{
    struct nk_rect bounds;

    if (ctx == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, help_row_height(ctx), 1);
    if (!nk_widget(&bounds, ctx)) {
        return;
    }

    help_draw_inline_at(ctx, nk_window_get_canvas(ctx), bounds, text != NULL ? text : "", color);
}

static void help_bullet_row(struct nk_context *ctx, const char *text)
{
    struct nk_rect bounds;
    struct nk_command_buffer *canvas;
    float marker_w;

    if (ctx == NULL) {
        return;
    }

    nk_layout_row_dynamic(ctx, help_row_height(ctx), 1);
    if (!nk_widget(&bounds, ctx)) {
        return;
    }

    canvas = nk_window_get_canvas(ctx);
    marker_w = help_text_width(ctx, "- ", 2);
    nk_draw_text(
        canvas,
        nk_rect(bounds.x, bounds.y, marker_w + 1.0f, bounds.h),
        "- ",
        2,
        ctx->style.font,
        HELP_COLOR_PANEL,
        HELP_COLOR_BULLET);
    bounds.x += marker_w;
    bounds.w -= marker_w;
    help_draw_inline_at(ctx, canvas, bounds, text != NULL ? text : "", HELP_COLOR_BODY);
}

static void help_render_span(struct nk_context *ctx, const help_span *span)
{
    if (ctx == NULL || span == NULL) {
        return;
    }

    switch (span->kind) {
        case HELP_SPAN_BLANK:
            nk_layout_row_dynamic(ctx, 4.0f, 1);
            nk_spacing(ctx, 1);
            break;

        case HELP_SPAN_H3:
            help_inline_row(ctx, span->text, HELP_COLOR_H3);
            break;

        case HELP_SPAN_BULLET:
            help_bullet_row(ctx, span->text);
            break;

        case HELP_SPAN_NUMBER:
            help_inline_row(ctx, span->text, HELP_COLOR_NUMBER);
            break;

        case HELP_SPAN_CODE_BLOCK:
            help_inline_row(ctx, span->text, HELP_COLOR_CODE);
            break;

        case HELP_SPAN_TABLE:
            help_inline_row(ctx, span->text, HELP_COLOR_TABLE);
            break;

        case HELP_SPAN_TEXT:
        default:
            help_inline_row(ctx, span->text, HELP_COLOR_BODY);
            break;
    }
}

static void help_render_section(struct nk_context *ctx, const help_section *section)
{
    int i;

    if (ctx == NULL || section == NULL) {
        return;
    }

    for (i = 0; i < section->span_count; ++i) {
        help_render_span(ctx, &section->spans[i]);
    }
}

static void help_render_section_buttons(struct nk_context *ctx, frontend_help_state *state, float width)
{
    int i;
    int columns;
    float button_w = 120.0f;

    if (ctx == NULL || state == NULL || help_section_count <= 0) {
        return;
    }

    columns = (int)(width / button_w);
    if (columns < 1) {
        columns = 1;
    }
    if (columns > help_section_count) {
        columns = help_section_count;
    }

    for (i = 0; i < help_section_count; ++i) {
        struct nk_style_button saved = ctx->style.button;
        if (i == state->section_index) {
            ctx->style.button.normal = nk_style_item_color(nk_rgb(49, 83, 106));
            ctx->style.button.hover = nk_style_item_color(nk_rgb(58, 96, 122));
            ctx->style.button.active = nk_style_item_color(nk_rgb(72, 119, 150));
            ctx->style.button.text_normal = nk_rgb(239, 247, 255);
        }
        if ((i % columns) == 0) {
            nk_layout_row_dynamic(ctx, 24.0f, columns);
        }
        if (nk_button_label(ctx, help_sections[i].title)) {
            help_view_select_section(ctx, state, i);
        }
        ctx->style.button = saved;
    }
}

void help_view_render(struct nk_context *ctx, frontend_help_state *state, int width, int height)
{
    struct nk_rect bounds;
    struct nk_style_window saved_window;
    nk_uint content_scroll_x = 0;
    nk_uint content_scroll_y = 0;
    float margin;
    float heading_h = 34.0f;
    float footer_h = 78.0f;
    float content_h;

    if (ctx == NULL || state == NULL || !state->open || width <= 0 || height <= 0) {
        return;
    }

    if (state->section_index < 0 || state->section_index >= help_section_count) {
        state->section_index = 0;
    }

    margin = width < 760 ? 12.0f : 36.0f;
    bounds = nk_rect(
        margin,
        margin,
        (float)width - margin * 2.0f,
        (float)height - margin * 2.0f);
    if (bounds.w < 100.0f || bounds.h < 100.0f) {
        return;
    }
    if (bounds.h < 320.0f) {
        footer_h = 54.0f;
    }
    content_h = bounds.h - heading_h - footer_h - 34.0f;
    if (content_h < 80.0f) {
        content_h = 80.0f;
    }
    state->content_page_y = (nk_uint)(content_h > 40.0f ? content_h - 24.0f : 80.0f);

    saved_window = ctx->style.window;
    ctx->style.window.fixed_background = nk_style_item_color(HELP_COLOR_BG);
    ctx->style.window.padding = nk_vec2(14.0f, 10.0f);
    ctx->style.window.spacing = nk_vec2(8.0f, 2.0f);
    ctx->style.window.group_padding = nk_vec2(10.0f, 4.0f);

    if (nk_begin(
            ctx,
            "Help",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_style_window group_window = ctx->style.window;

        nk_layout_row_dynamic(ctx, heading_h, 1);
        nk_label_colored(ctx, help_heading, NK_TEXT_LEFT, HELP_COLOR_HEADING);

        ctx->style.window.fixed_background = nk_style_item_color(HELP_COLOR_PANEL);
        nk_layout_row_dynamic(ctx, content_h, 1);
        if (nk_group_begin(ctx, "HelpContent", NK_WINDOW_BORDER)) {
            if (state->pending_scroll_restore) {
                nk_group_set_scroll(ctx, "HelpContent", 0, state->pending_scroll_y);
                state->pending_scroll_restore = false;
            }
            help_render_section(ctx, &help_sections[state->section_index]);
            nk_group_get_scroll(ctx, "HelpContent", &content_scroll_x, &content_scroll_y);
            if (state->section_index >= 0 && state->section_index < FRONTEND_HELP_MAX_SECTIONS) {
                state->section_scroll_y[state->section_index] = content_scroll_y;
            }
            nk_group_end(ctx);
        }

        ctx->style.window = group_window;
        nk_layout_row_dynamic(ctx, footer_h, 1);
        if (nk_group_begin(ctx, "HelpSections", NK_WINDOW_NO_SCROLLBAR)) {
            help_render_section_buttons(ctx, state, bounds.w - 28.0f);
            nk_group_end(ctx);
        }
    }
    nk_end(ctx);
    ctx->style.window = saved_window;
}
