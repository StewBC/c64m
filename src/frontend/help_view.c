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
            state->section_index = i;
        }
        ctx->style.button = saved;
    }
}

void help_view_render(struct nk_context *ctx, frontend_help_state *state, int width, int height)
{
    struct nk_rect bounds;
    struct nk_style_window saved_window;
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
            help_render_section(ctx, &help_sections[state->section_index]);
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
