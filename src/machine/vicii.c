#include "vicii.h"

#include "c64_bus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    VICII_REG_RASTER = 0x12,
    VICII_REG_CONTROL_1 = 0x11,
    VICII_REG_MEMORY_POINTER = 0x18,
    VICII_REG_BORDER_COLOR = 0x20,
    VICII_REG_BACKGROUND_COLOR_0 = 0x21,
    /* Phase C: background color register indices */
    VICII_REG_BACKGROUND_COLOR_1 = 0x22,
    VICII_REG_BACKGROUND_COLOR_2 = 0x23,
    VICII_REG_BACKGROUND_COLOR_3 = 0x24,

    /* Phase D: sprite register indices */
    VICII_REG_SPR_X_MSB      = 0x10,
    VICII_REG_SPR_ENABLE     = 0x15,
    VICII_REG_SPR_Y_EXPAND   = 0x17,
    VICII_REG_SPR_PRIORITY   = 0x1B,
    VICII_REG_SPR_MULTICOLOR = 0x1C,
    VICII_REG_SPR_X_EXPAND   = 0x1D,
    VICII_REG_SPR_SPR_COLL   = 0x1E,
    VICII_REG_SPR_BG_COLL    = 0x1F,
    VICII_REG_SPR_MM0        = 0x25,
    VICII_REG_SPR_MM1        = 0x26,
    VICII_DEFAULT_BORDER_COLOR = 6,
    VICII_DEFAULT_BACKGROUND_COLOR = 14,
    VICII_TEXT_COLUMNS = 40,
    VICII_CHARACTER_WIDTH = 8,
    VICII_CHARACTER_HEIGHT = 8,
    VICII_DEFAULT_YSCROLL = 3,
    VICII_DISPLAY_HEIGHT = 200,

    /* Phase A additions */
    VICII_BADLINE_FIRST        = 0x30,
    VICII_BADLINE_LAST         = 0xF7,
    VICII_CACCESS_FIRST_CYCLE  = 15,
    VICII_CACCESS_LAST_CYCLE   = 54,
    VICII_BA_ASSERT_CYCLE      = 12,
    VICII_VC_MAX               = 1023,
    VICII_RC_MAX               = 7,
    VICII_IRQ_RASTER           = 0x01,
    VICII_IRQ_IMBC             = 0x02,
    VICII_IRQ_IMMC             = 0x04,

    /* Phase H: sprite BA window width (cycles). Formula: ba_start = p_cycle - 3,
       ba_end = p_cycle + 2 (exclusive), so the window is always 5 cycles wide.
       Applies to both the early group (sprites 3-7) and late group (sprites 0-2). */
    VICII_SPRITE_BA_WINDOW     = 5,

    /* PAL border compare values (pixel/line units within 384×272 frame) */
    VICII_PAL_VBORDER_TOP_25    = 51,
    VICII_PAL_VBORDER_BOTTOM_25 = 251,
    VICII_PAL_VBORDER_TOP_24    = 55,
    VICII_PAL_VBORDER_BOTTOM_24 = 247,
    VICII_HBORDER_LEFT_40       = 24,
    VICII_HBORDER_RIGHT_40      = 344,
    VICII_HBORDER_LEFT_38       = 31,
    VICII_HBORDER_RIGHT_38      = 335,
};

/* c64_frame pixels are ARGB8888, so the palette values can be copied directly. */
static const uint32_t vicii_palette_argb[16] = {
    0xff000000u,
    0xffffffffu,
    0xff813338u,
    0xff75cec8u,
    0xff8e3c97u,
    0xff56ac4du,
    0xff2e2c9bu,
    0xffedf171u,
    0xff8e5029u,
    0xff553800u,
    0xffc46c71u,
    0xff4a4a4au,
    0xff7b7b7bu,
    0xffa9ff9fu,
    0xff706debu,
    0xffb2b2b2u,
};

/* Phase H: PAL 6569 sprite BA-assert cycle (0-based within the line where BA is
   asserted). Derived from Bauer: ba_start = p_cycle_1based - 1 - 3 = p_cycle_0based - 3.

   Sprites 0-2 and 5-7: BA is asserted during the SAME line as the sprite fetch.
   Sprites 3-4: their fetches occur on the NEXT line (p_cycle_0based 0 and 2), so
   BA must be asserted during the PREVIOUS line at 0-based cycles 60 and 62. */
static const uint32_t vicii_pal_sprite_ba_assert[8] = {
    54, 56, 58,   /* sprites 0-2: asserted in current line  (fetch at 0-based 57,59,61) */
    60, 62,       /* sprites 3-4: asserted in PREVIOUS line (fetch at 0-based  0, 2)    */
     1,  3,  5    /* sprites 5-7: asserted in current line  (fetch at 0-based  4, 6, 8) */
};

/* NTSC 6567R8 sprite BA-assert cycle (0-based). The emulator's NTSC mode uses
   65 cycles per line, matching the 6567R8 timing diagram. Sprite 2's late
   window reaches the end of the 65-cycle line; sprites 3-4 still assert during
   the previous line for next-line fetches. */
static const uint32_t vicii_ntsc_sprite_ba_assert[8] = {
    56, 58, 60,   /* sprites 0-2: asserted in current line  (fetch at 0-based 59,61,63) */
    62, 64,       /* sprites 3-4: asserted in PREVIOUS line (fetch at 0-based  0, 2)    */
     1,  3,  5    /* sprites 5-7: asserted in current line  (fetch at 0-based  4, 6, 8) */
};

static void vicii_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static void vicii_prepare_frame(c64_frame *frame, uint64_t frame_number, uint64_t machine_cycle, uint32_t fill_color) {
    assert(frame);

    frame->width = C64_FRAME_WIDTH;
    frame->height = C64_FRAME_HEIGHT;
    frame->stride_bytes = C64_FRAME_WIDTH * sizeof(frame->pixels[0]);
    frame->pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    frame->frame_number = frame_number;
    frame->machine_cycle = machine_cycle;

    for (uint32_t i = 0; i < C64_FRAME_WIDTH * C64_FRAME_HEIGHT; i++) {
        frame->pixels[i] = fill_color;
    }
}

static void vicii_begin_live_frame(vicii *v) {
    uint8_t fill_index;
    uint32_t fill_color;

    assert(v);

    fill_index = (v->registers[VICII_REG_CONTROL_1] & 0x10u) != 0u ?
        (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu) :
        (uint8_t)(v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu);
    fill_color = vicii_palette_argb[fill_index];
    vicii_prepare_frame(&v->working_frame, v->timing.frame_number, 0, fill_color);
    v->vertical_border_active = true;
}

bool vicii_init(vicii *v, char *error, size_t error_size) {
    if (!v) {
        vicii_set_error(error, error_size, "VIC-II pointer is null");
        return false;
    }

    memset(v, 0, sizeof(*v));
    v->timing.standard = VICII_VIDEO_STANDARD_NTSC;
    vicii_reset(v);
    vicii_set_error(error, error_size, "");
    return true;
}

void vicii_reset(vicii *v) {
    vicii_video_standard standard;

    assert(v);

    standard = v->timing.standard;
    memset(v->registers, 0, sizeof(v->registers));
    memset(&v->timing, 0, sizeof(v->timing));
    memset(&v->working_frame, 0, sizeof(v->working_frame));
    memset(&v->completed_frame, 0, sizeof(v->completed_frame));

    v->timing.standard = standard;
    vicii_set_video_standard(v, standard);
    v->registers[VICII_REG_BORDER_COLOR] = VICII_DEFAULT_BORDER_COLOR;
    v->registers[VICII_REG_BACKGROUND_COLOR_0] = VICII_DEFAULT_BACKGROUND_COLOR;
    v->completed_frame_ready = false;

    v->vc                        = 0;
    v->vc_base                   = 0;
    v->rc                        = 0;
    v->display_state             = false;
    v->bad_line                  = false;
    v->timing.raster_compare          = 0;
    v->timing.ba_low_until_abs        = 0;
    v->timing.sprite_ba_low_until_abs = 0;
    v->irq_status                = 0;
    v->irq_enable                = 0;
    memset(v->video_matrix, 0, sizeof(v->video_matrix));
    memset(v->color_line,   0, sizeof(v->color_line));
    memset(v->sprite_mc,       0, sizeof(v->sprite_mc));
    memset(v->sprite_active,   0, sizeof(v->sprite_active));
    memset(v->sprite_visible,  0, sizeof(v->sprite_visible));
    memset(v->sprite_y_exp_ff, 0, sizeof(v->sprite_y_exp_ff));
    memset(v->sprite_data,         0, sizeof(v->sprite_data));
    memset(v->sprite_line_enabled,    0, sizeof(v->sprite_line_enabled));
    memset(v->sprite_line_x,          0, sizeof(v->sprite_line_x));
    memset(v->sprite_line_x_expand,   0, sizeof(v->sprite_line_x_expand));
    memset(v->sprite_line_multicolor, 0, sizeof(v->sprite_line_multicolor));
    memset(v->sprite_line_color,      0, sizeof(v->sprite_line_color));
    v->sprite_line_mm0 = 0;
    v->sprite_line_mm1 = 0;
    v->sprite_priority = 0;
    v->sprite_sprite_collision = 0;
    v->sprite_background_collision = 0;
    v->vertical_border_active = true;
    vicii_begin_live_frame(v);
}

void vicii_set_video_standard(vicii *v, vicii_video_standard standard) {
    assert(v);

    v->timing.standard = standard;
    if (standard == VICII_VIDEO_STANDARD_PAL) {
        v->timing.cycles_per_line = VICII_PAL_CYCLES_PER_LINE;
        v->timing.lines_per_frame = VICII_PAL_LINES_PER_FRAME;
        return;
    }

    v->timing.cycles_per_line = VICII_NTSC_CYCLES_PER_LINE;
    v->timing.lines_per_frame = VICII_NTSC_LINES_PER_FRAME;
}

static bool vicii_is_bad_line(const vicii *v) {
    uint32_t y       = v->timing.raster_line;
    uint8_t  yscroll = v->registers[0x11] & 0x07u;
    bool     den     = (v->registers[0x11] & 0x10u) != 0;

    if (!den) return false;
    if (y < VICII_BADLINE_FIRST || y > VICII_BADLINE_LAST) return false;
    return (uint8_t)(y & 0x07u) == yscroll;
}

typedef struct vicii_border_geometry {
    uint32_t top;
    uint32_t bottom;
    uint32_t left;
    uint32_t right;
} vicii_border_geometry;

typedef struct vicii_bg_pixel {
    uint32_t color;
    bool     foreground;
} vicii_bg_pixel;

typedef struct vicii_sprite_pixel {
    uint32_t color;
    bool     opaque;
} vicii_sprite_pixel;

static vicii_border_geometry vicii_get_border_geometry(const vicii *v) {
    vicii_border_geometry g;
    bool rsel = (v->registers[0x11] & 0x08u) != 0;
    bool csel = (v->registers[0x16] & 0x08u) != 0;

    g.top    = rsel ? (uint32_t)VICII_PAL_VBORDER_TOP_25    : (uint32_t)VICII_PAL_VBORDER_TOP_24;
    g.bottom = rsel ? (uint32_t)VICII_PAL_VBORDER_BOTTOM_25 : (uint32_t)VICII_PAL_VBORDER_BOTTOM_24;
    g.left   = csel ? (uint32_t)VICII_HBORDER_LEFT_40       : (uint32_t)VICII_HBORDER_LEFT_38;
    g.right  = csel ? (uint32_t)VICII_HBORDER_RIGHT_40      : (uint32_t)VICII_HBORDER_RIGHT_38;
    return g;
}

static int vicii_sprite_dx_wrapped(uint32_t frame_x, uint16_t spr_x) {
    int dx = (int)frame_x - (int)(spr_x & 0x01FFu);
    if (dx < 0) dx += 512;
    return dx;
}

static bool vicii_display_adjusted_y(uint32_t sy, uint8_t yscroll, uint32_t *out_adjusted) {
    int32_t adjusted;

    adjusted = (int32_t)sy + ((int32_t)VICII_DEFAULT_YSCROLL - (int32_t)(yscroll & 0x07u));
    if (adjusted < 0 || adjusted >= (int32_t)VICII_DISPLAY_HEIGHT) {
        return false;
    }

    *out_adjusted = (uint32_t)adjusted;
    return true;
}

static void vicii_fetch_sprites(vicii *v, const c64_bus_t *bus) {
    uint16_t vic_bank;
    uint16_t screen_base;
    uint8_t  enable;
    int      n;

    memset(v->sprite_visible, 0, sizeof(v->sprite_visible));

    if (!bus) return;

    vic_bank    = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0Fu) * 0x0400u);
    enable      = v->registers[VICII_REG_SPR_ENABLE];

    for (n = 0; n < 8; n++) {
        uint8_t  spr_y    = v->registers[1 + n * 2];
        uint32_t raster_y = v->timing.raster_line;
        bool     y_expand = (v->registers[VICII_REG_SPR_Y_EXPAND] >> n) & 1u;

        if ((enable >> n) & 1u) {
            if (raster_y == (uint32_t)spr_y) {
                v->sprite_mc[n]       = 0;
                v->sprite_active[n]   = true;
                v->sprite_y_exp_ff[n] = false;
            }
        } else {
            v->sprite_active[n]  = false;
            v->sprite_visible[n] = false;
        }

        if (v->sprite_active[n]) {
            uint8_t  mc = v->sprite_mc[n];
            bool     advance_mc;
            uint16_t ptr_addr;
            uint8_t  pointer;
            uint16_t data_base;

            if (y_expand) {
                advance_mc             = v->sprite_y_exp_ff[n];
                v->sprite_y_exp_ff[n] ^= 1u;
            } else {
                advance_mc = true;
            }

            ptr_addr  = (uint16_t)((vic_bank + screen_base + 0x03F8u + (uint16_t)n) & 0xFFFFu);
            pointer   = c64_bus_vic_read_ram(bus, ptr_addr);
            data_base = (uint16_t)((vic_bank + (uint16_t)pointer * 64u) & 0xFFFFu);
            v->sprite_data[n][0] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc)      & 0xFFFFu));
            v->sprite_data[n][1] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 1u) & 0xFFFFu));
            v->sprite_data[n][2] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 2u) & 0xFFFFu));
            v->sprite_visible[n] = true;

            if (advance_mc) {
                mc = (uint8_t)(mc + 3u);
                if (mc >= 63u) {
                    v->sprite_active[n] = false;
                } else {
                    v->sprite_mc[n] = mc;
                }
            }
        }
    }
}

static vicii_bg_pixel vicii_bg_pixel_make(uint32_t color, bool foreground) {
    vicii_bg_pixel pixel;

    pixel.color = color;
    pixel.foreground = foreground;
    return pixel;
}

static vicii_sprite_pixel vicii_sprite_pixel_make(uint32_t color, bool opaque) {
    vicii_sprite_pixel pixel;

    pixel.color = color;
    pixel.opaque = opaque;
    return pixel;
}

static vicii_sprite_pixel vicii_sprite_pixel_from_data(
    const vicii *v,
    const uint8_t data[3],
    int n,
    int dx)
{
    bool    x_expand   = (v->registers[VICII_REG_SPR_X_EXPAND]  >> n) & 1u;
    bool    multicolor = (v->registers[VICII_REG_SPR_MULTICOLOR] >> n) & 1u;
    int     spr_width  = x_expand ? 48 : 24;

    if (dx < 0 || dx >= spr_width) {
        return vicii_sprite_pixel_make(0, false);
    }

    if (multicolor) {
        int     pair_index = x_expand ? (dx / 4) : (dx / 2);
        int     bit_shift  = 6 - (pair_index * 2) % 8;
        uint8_t pair       = (data[(pair_index * 2) / 8] >> bit_shift) & 3u;
        switch (pair) {
        case 0u:
            return vicii_sprite_pixel_make(0, false);
        case 1u:
            return vicii_sprite_pixel_make(vicii_palette_argb[v->registers[VICII_REG_SPR_MM0] & 0x0Fu], true);
        case 2u:
            return vicii_sprite_pixel_make(vicii_palette_argb[v->registers[0x27u + (uint8_t)n] & 0x0Fu], true);
        default:
            return vicii_sprite_pixel_make(vicii_palette_argb[v->registers[VICII_REG_SPR_MM1] & 0x0Fu], true);
        }
    } else {
        int     bit_pos = x_expand ? (dx / 2) : dx;
        uint8_t bit     = (data[bit_pos / 8] >> (7 - bit_pos % 8)) & 1u;
        if (!bit) {
            return vicii_sprite_pixel_make(0, false);
        }
        return vicii_sprite_pixel_make(vicii_palette_argb[v->registers[0x27u + (uint8_t)n] & 0x0Fu], true);
    }
}

static vicii_sprite_pixel vicii_sprite_pixel_from_latched_data(
    const vicii *v,
    const uint8_t data[3],
    int n,
    int dx)
{
    bool    x_expand   = v->sprite_line_x_expand[n];
    bool    multicolor = v->sprite_line_multicolor[n];
    int     spr_width  = x_expand ? 48 : 24;

    if (dx < 0 || dx >= spr_width) {
        return vicii_sprite_pixel_make(0, false);
    }

    if (multicolor) {
        int     pair_index = x_expand ? (dx / 4) : (dx / 2);
        int     bit_shift  = 6 - (pair_index * 2) % 8;
        uint8_t pair       = (data[(pair_index * 2) / 8] >> bit_shift) & 3u;
        switch (pair) {
        case 0u:
            return vicii_sprite_pixel_make(0, false);
        case 1u:
            return vicii_sprite_pixel_make(vicii_palette_argb[v->sprite_line_mm0 & 0x0Fu], true);
        case 2u:
            return vicii_sprite_pixel_make(vicii_palette_argb[v->sprite_line_color[n] & 0x0Fu], true);
        default:
            return vicii_sprite_pixel_make(vicii_palette_argb[v->sprite_line_mm1 & 0x0Fu], true);
        }
    } else {
        int     bit_pos = x_expand ? (dx / 2) : dx;
        uint8_t bit     = (data[bit_pos / 8] >> (7 - bit_pos % 8)) & 1u;
        if (!bit) {
            return vicii_sprite_pixel_make(0, false);
        }
        return vicii_sprite_pixel_make(vicii_palette_argb[v->sprite_line_color[n] & 0x0Fu], true);
    }
}

static void vicii_latch_sprite_line_state(vicii *v) {
    uint8_t enable = v->registers[VICII_REG_SPR_ENABLE];
    uint8_t x_msb = v->registers[VICII_REG_SPR_X_MSB];
    uint8_t x_expand = v->registers[VICII_REG_SPR_X_EXPAND];
    uint8_t multicolor = v->registers[VICII_REG_SPR_MULTICOLOR];
    int n;

    v->sprite_line_mm0 = (uint8_t)(v->registers[VICII_REG_SPR_MM0] & 0x0Fu);
    v->sprite_line_mm1 = (uint8_t)(v->registers[VICII_REG_SPR_MM1] & 0x0Fu);

    for (n = 0; n < 8; n++) {
        v->sprite_line_enabled[n] = ((enable >> n) & 1u) != 0;
        v->sprite_line_x[n] = (uint16_t)(v->registers[(uint8_t)(n * 2)] |
            (uint16_t)(((x_msb >> n) & 1u) << 8));
        v->sprite_line_x_expand[n] = ((x_expand >> n) & 1u) != 0;
        v->sprite_line_multicolor[n] = ((multicolor >> n) & 1u) != 0;
        v->sprite_line_color[n] = (uint8_t)(v->registers[0x27u + (uint8_t)n] & 0x0Fu);
    }
}

static vicii_bg_pixel vicii_apply_den_blanking(bool den, uint32_t b0c, vicii_bg_pixel pixel) {
    if (!den) {
        pixel.color = b0c;
    }
    return pixel;
}

static vicii_bg_pixel vicii_background_pixel(
    const vicii *v,
    const c64_bus_t *bus,
    const vicii_border_geometry *g,
    uint32_t x,
    uint32_t y)
{
    uint32_t b0c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu];
    uint32_t b1c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_1] & 0x0fu];
    uint32_t b2c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_2] & 0x0fu];
    uint32_t b3c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_3] & 0x0fu];
    uint8_t mode;
    uint8_t xscroll;
    uint8_t yscroll;
    bool den;
    uint32_t sx_raw;
    uint32_t sy;
    uint32_t adjusted;
    uint32_t sx;
    uint32_t row_in_cell;
    uint32_t char_row;
    uint32_t col;
    uint16_t cell;
    uint16_t vic_bank;
    uint16_t screen_base;
    uint16_t char_base;
    uint16_t bitmap_base;

    if (x < g->left || x >= g->right || y < g->top || y >= g->bottom) {
        return vicii_bg_pixel_make(b0c, false);
    }

    mode = (uint8_t)(((v->registers[0x11] & 0x40u) ? 4u : 0u) |
                     ((v->registers[0x11] & 0x20u) ? 2u : 0u) |
                     ((v->registers[0x16] & 0x10u) ? 1u : 0u));
    xscroll = v->registers[0x16] & 0x07u;
    yscroll = v->registers[0x11] & 0x07u;
    den = (v->registers[0x11] & 0x10u) != 0u;
    sx_raw = x - g->left;
    sy = y - g->top;

    if (mode >= 5u) {
        return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[0], false));
    }

    if (sx_raw < (uint32_t)xscroll ||
        !vicii_display_adjusted_y(sy, yscroll, &adjusted)) {
        return vicii_bg_pixel_make(b0c, false);
    }

    sx = sx_raw - (uint32_t)xscroll;
    row_in_cell = adjusted & 7u;
    char_row = adjusted / 8u;
    col = sx / 8u;
    cell = (uint16_t)(char_row * 40u + col);
    vic_bank = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(vic_bank + (v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    char_base = (uint16_t)(vic_bank + ((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);
    bitmap_base = (uint16_t)(vic_bank + ((v->registers[VICII_REG_MEMORY_POINTER] >> 3) & 1u) * 0x2000u);

    switch (mode) {
    case 0u:
        {
            uint8_t code = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
            uint8_t glyph = c64_bus_vic_read_char_glyph_at(bus, char_base, code, (uint8_t)row_in_cell);
            uint8_t fg = c64_bus_vic_read_color(bus, cell);
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            if (glyph & bit) {
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[fg & 0x0fu], true));
            }
            return vicii_bg_pixel_make(b0c, false);
        }

    case 1u:
        {
            uint8_t code = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
            uint8_t color_nib = c64_bus_vic_read_color(bus, cell);
            uint8_t glyph = c64_bus_vic_read_char_glyph_at(bus, char_base, code, (uint8_t)row_in_cell);

            if ((color_nib & 0x08u) == 0u) {
                uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
                if (glyph & bit) {
                    return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[color_nib & 0x0fu], true));
                }
                return vicii_bg_pixel_make(b0c, false);
            } else {
                uint8_t pair = (uint8_t)((glyph >> (6u - (sx & 6u))) & 3u);
                switch (pair) {
                case 0u:
                    return vicii_bg_pixel_make(b0c, false);
                case 1u:
                    return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(b1c, true));
                case 2u:
                    return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(b2c, true));
                default:
                    return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[color_nib & 0x07u], true));
                }
            }
        }

    case 2u:
        {
            uint8_t vm_byte = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
            uint16_t baddr = (uint16_t)(bitmap_base + (uint32_t)cell * 8u + row_in_cell);
            uint8_t bdata = c64_bus_vic_read_ram(bus, baddr);
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            if (bdata & bit) {
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[(vm_byte >> 4) & 0x0fu], true));
            }
            return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[vm_byte & 0x0fu], false));
        }

    case 3u:
        {
            uint8_t vm_byte = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
            uint8_t color_nib = c64_bus_vic_read_color(bus, cell);
            uint16_t baddr = (uint16_t)(bitmap_base + (uint32_t)cell * 8u + row_in_cell);
            uint8_t bdata = c64_bus_vic_read_ram(bus, baddr);
            uint8_t pair = (uint8_t)((bdata >> (6u - (sx & 6u))) & 3u);
            switch (pair) {
            case 0u:
                return vicii_bg_pixel_make(b0c, false);
            case 1u:
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[(vm_byte >> 4) & 0x0fu], true));
            case 2u:
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[vm_byte & 0x0fu], true));
            default:
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[color_nib & 0x0fu], true));
            }
        }

    case 4u:
        {
            uint8_t code = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
            uint8_t ecm_sel = (code >> 6) & 3u;
            uint8_t glyph = c64_bus_vic_read_char_glyph_at(bus, char_base, code & 0x3fu, (uint8_t)row_in_cell);
            uint8_t fg_nib = c64_bus_vic_read_color(bus, cell);
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            uint32_t ecm_bg;

            if (glyph & bit) {
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[fg_nib & 0x0fu], true));
            }

            switch (ecm_sel) {
            case 0u:  ecm_bg = b0c; break;
            case 1u:  ecm_bg = b1c; break;
            case 2u:  ecm_bg = b2c; break;
            default:  ecm_bg = b3c; break;
            }
            return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(ecm_bg, false));
        }

    default:
        return vicii_bg_pixel_make(vicii_palette_argb[0], false);
    }
}

static void vicii_set_irq_flag(vicii *v, uint8_t flag) {
    v->irq_status |= flag;
    v->registers[0x19] = v->irq_status;
}

static void vicii_note_sprite_collisions(vicii *v, vicii_bg_pixel bg, const vicii_sprite_pixel sprites[8]) {
    uint8_t mask = 0;
    int n;

    for (n = 0; n < 8; n++) {
        if (sprites[n].opaque) {
            mask |= (uint8_t)(1u << n);
        }
    }

    if ((mask & (uint8_t)(mask - 1u)) != 0) {
        v->sprite_sprite_collision |= mask;
        if ((v->irq_status & VICII_IRQ_IMMC) == 0) {
            vicii_set_irq_flag(v, VICII_IRQ_IMMC);
        }
    }

    if (bg.foreground && mask != 0) {
        v->sprite_background_collision |= mask;
        if ((v->irq_status & VICII_IRQ_IMBC) == 0) {
            vicii_set_irq_flag(v, VICII_IRQ_IMBC);
        }
    }
}

static uint32_t vicii_compose_pixel(
    vicii *v,
    bool border_active,
    uint32_t border_color,
    vicii_bg_pixel bg,
    const vicii_sprite_pixel sprites[8])
{
    int n;

    vicii_note_sprite_collisions(v, bg, sprites);

    if (border_active) {
        if ((v->registers[VICII_REG_CONTROL_1] & 0x10u) == 0u) {
            return bg.color;
        }
        return border_color;
    }

    for (n = 0; n < 8; n++) {
        if (sprites[n].opaque && ((v->sprite_priority & (uint8_t)(1u << n)) == 0)) {
            return sprites[n].color;
        }
    }

    if (bg.foreground) {
        return bg.color;
    }

    for (n = 0; n < 8; n++) {
        if (sprites[n].opaque && ((v->sprite_priority & (uint8_t)(1u << n)) != 0)) {
            return sprites[n].color;
        }
    }

    return bg.color;
}

static uint32_t vicii_live_pixel(vicii *v, const c64_bus_t *bus, const vicii_border_geometry *g, uint32_t x, uint32_t y) {
    bool hborder = x < g->left || x >= g->right;
    uint32_t border_color = vicii_palette_argb[v->registers[VICII_REG_BORDER_COLOR] & 0x0fu];
    vicii_bg_pixel bg = vicii_background_pixel(v, bus, g, x, y);
    vicii_sprite_pixel sprites[8];
    bool any_sprite_enabled = false;
    int n;

    for (n = 0; n < 8; n++) {
        any_sprite_enabled = any_sprite_enabled || v->sprite_line_enabled[n];
    }

    if (!any_sprite_enabled) {
        if (v->vertical_border_active || hborder) {
            if ((v->registers[VICII_REG_CONTROL_1] & 0x10u) == 0u) {
                return bg.color;
            }
            return border_color;
        }
        return bg.color;
    }

    for (n = 0; n < 8; n++) {
        sprites[n] = vicii_sprite_pixel_make(0, false);
        if (!v->sprite_line_enabled[n]) continue;
        if (!v->sprite_visible[n]) continue;
        {
            int dx = vicii_sprite_dx_wrapped(x, v->sprite_line_x[n]);
            sprites[n] = vicii_sprite_pixel_from_latched_data(v, v->sprite_data[n], n, dx);
        }
    }

    return vicii_compose_pixel(v, v->vertical_border_active || hborder, border_color, bg, sprites);
}

static void vicii_update_live_vertical_border(vicii *v) {
    vicii_border_geometry g;

    g = vicii_get_border_geometry(v);
    if (v->timing.raster_line == g.top) {
        v->vertical_border_active = false;
    }
    if (v->timing.raster_line == g.bottom) {
        v->vertical_border_active = true;
    }
}

static void vicii_render_live_cycle(vicii *v, const c64_bus_t *bus) {
    vicii_border_geometry g;
    uint32_t y;
    uint32_t x0;
    uint32_t x1;
    uint32_t x;

    if (!bus) {
        return;
    }

    y = v->timing.raster_line;
    if (y >= C64_FRAME_HEIGHT) {
        return;
    }

    g = vicii_get_border_geometry(v);
    x0 = (v->timing.cycle_in_line * (uint32_t)C64_FRAME_WIDTH) / v->timing.cycles_per_line;
    x1 = ((v->timing.cycle_in_line + 1u) * (uint32_t)C64_FRAME_WIDTH) / v->timing.cycles_per_line;
    if (x1 <= x0) {
        x1 = x0 + 1u;
    }
    if (x1 > C64_FRAME_WIDTH) {
        x1 = C64_FRAME_WIDTH;
    }

    for (x = x0; x < x1; x++) {
        v->working_frame.pixels[y * C64_FRAME_WIDTH + x] = vicii_live_pixel(v, bus, &g, x, y);
    }
}


static void vicii_assert_raster_irq(vicii *v) {
    vicii_set_irq_flag(v, VICII_IRQ_RASTER);
}

/* Returns true if sprite n will perform a DMA fetch on the NEXT raster line.
   Called during the previous line (at the sprite's BA-assert cycle) to determine
   whether to open a sprite BA window that spans the line boundary.
   Sprites 3-4 display on the line whose number equals spr_y (no +1 offset);
   the next_y == spr_y check mirrors the Y-match logic in vicii_fetch_sprites(). */
static bool vicii_sprite_dma_next_line(const vicii *v, int n) {
    uint8_t  enable   = v->registers[VICII_REG_SPR_ENABLE];
    uint8_t  spr_y    = v->registers[1 + n * 2];
    uint32_t next_y   = (v->timing.raster_line + 1u) % v->timing.lines_per_frame;

    if (v->sprite_active[n]) {
        return true;
    }
    if ((enable >> n) & 1u) {
        if (next_y == (uint32_t)spr_y) {
            return true;
        }
    }
    return false;
}

static const uint32_t *vicii_sprite_ba_assert_table(const vicii *v) {
    return v->timing.standard == VICII_VIDEO_STANDARD_PAL ?
        vicii_pal_sprite_ba_assert :
        vicii_ntsc_sprite_ba_assert;
}

void vicii_step_cycle(vicii *v, const c64_bus_t *bus, uint64_t abs_cycle) {
    uint32_t cycle;
    uint16_t screen_base;
    uint32_t col;
    uint16_t screen_addr;

    assert(v);

    cycle = v->timing.cycle_in_line;

    /* ------------------------------------------------------------------
     * Start-of-line events (cycle 0 of the current raster line)
     * ------------------------------------------------------------------ */
    if (cycle == 0) {
        if (v->timing.raster_line == v->timing.raster_compare) {
            vicii_assert_raster_irq(v);
        }

        vicii_update_live_vertical_border(v);

        v->bad_line = vicii_is_bad_line(v);

        if (v->bad_line) {
            v->rc            = 0;
            v->vc            = v->vc_base;
            v->display_state = true;
        }
    }

    if (cycle == 0) {
        vicii_fetch_sprites(v, bus);
        vicii_latch_sprite_line_state(v);
    }

    /* Bad-line BA: assert at cycle 12 for the 43-cycle c-access window (12-54
       inclusive). Window = [abs_cycle_at_12, abs_cycle_at_12 + 44). */
    if (v->bad_line && cycle == (uint32_t)VICII_BA_ASSERT_CYCLE) {
        uint64_t new_until = abs_cycle +
            (uint64_t)(VICII_CACCESS_LAST_CYCLE - VICII_BA_ASSERT_CYCLE + 2u);
        if (new_until > v->timing.ba_low_until_abs) {
            v->timing.ba_low_until_abs = new_until;
        }
    }

    /* ------------------------------------------------------------------
     * c-access window (cycles 15-54) on Bad Lines
     * ------------------------------------------------------------------ */
    if (bus &&
        v->bad_line &&
        cycle >= (uint32_t)VICII_CACCESS_FIRST_CYCLE &&
        cycle <= (uint32_t)VICII_CACCESS_LAST_CYCLE) {

        col         = cycle - (uint32_t)VICII_CACCESS_FIRST_CYCLE;
        screen_base = (uint16_t)(c64_bus_vic_bank_base(bus) + (v->registers[0x18] >> 4) * 0x0400u);
        screen_addr = (uint16_t)(screen_base + v->vc + col);

        v->video_matrix[col] = c64_bus_vic_read_ram(bus, screen_addr);
        v->color_line[col]   = c64_bus_vic_read_color(bus, (uint16_t)(v->vc + col));
    }

    /* ------------------------------------------------------------------
     * Phase H: Sprite BA windows.
     *
     * Each active sprite contributes a 5-cycle BA-low window starting at
     * the selected standard's BA assert table. Sprites 0-2 and 5-7 are asserted
     * during the same line as their fetch.  Sprites 3-4 are asserted
     * during the PREVIOUS line for the next line's
     * fetch, so vicii_sprite_dma_next_line() is used for those two.
     *
     * sprite_ba_low_until_abs is a running high-water mark: each new
     * assertion takes max(current, abs_cycle + WINDOW) so overlapping
     * windows within a group merge automatically without over-extending
     * into the gap between the early group (sprites 3-7) and the late
     * group (sprites 0-2).
     *
     * ------------------------------------------------------------------ */
    {
        const uint32_t *sprite_ba_assert = vicii_sprite_ba_assert_table(v);
        int sn;
        for (sn = 0; sn < 8; sn++) {
            if (cycle != sprite_ba_assert[sn]) {
                continue;
            }
            if (sn == 3 || sn == 4) {
                /* Cross-line: assert for the NEXT line's fetch. */
                if (vicii_sprite_dma_next_line(v, sn)) {
                    uint64_t new_until = abs_cycle + (uint64_t)VICII_SPRITE_BA_WINDOW;
                    if (new_until > v->timing.sprite_ba_low_until_abs) {
                        v->timing.sprite_ba_low_until_abs = new_until;
                    }
                }
            } else {
                /* Same-line: assert for the current line's fetch. */
                if (v->sprite_active[sn]) {
                    uint64_t new_until = abs_cycle + (uint64_t)VICII_SPRITE_BA_WINDOW;
                    if (new_until > v->timing.sprite_ba_low_until_abs) {
                        v->timing.sprite_ba_low_until_abs = new_until;
                    }
                }
            }
        }
    }

    vicii_render_live_cycle(v, bus);

    /* ------------------------------------------------------------------
     * Advance cycle counter
     * ------------------------------------------------------------------ */
    v->timing.cycle_in_line++;
    if (v->timing.cycle_in_line < v->timing.cycles_per_line) {
        return;
    }

    /* ------------------------------------------------------------------
     * End-of-line events
     * ------------------------------------------------------------------ */
    v->timing.cycle_in_line = 0;

    if (v->display_state) {
        v->vc = (uint16_t)((v->vc + VICII_TEXT_COLUMNS) & (uint16_t)VICII_VC_MAX);

        if (v->rc == VICII_RC_MAX) {
            v->vc_base       = v->vc;
            v->display_state = false;
        }
        v->rc = (uint8_t)((v->rc + 1u) & 0x07u);
    }

    v->timing.raster_line++;
    if (v->timing.raster_line < v->timing.lines_per_frame) {
        return;
    }

    /* ------------------------------------------------------------------
     * End-of-frame events
     * ------------------------------------------------------------------ */
    v->timing.raster_line  = 0;
    v->timing.frame_number++;
    v->working_frame.frame_number = v->timing.frame_number;
    memcpy(&v->completed_frame, &v->working_frame, sizeof(v->completed_frame));
    v->completed_frame_ready = true;
    v->timing.frame_complete = true;

    v->vc            = 0;
    v->vc_base       = 0;
    v->rc            = 0;
    v->display_state = false;
    v->bad_line      = false;
    vicii_begin_live_frame(v);
}

bool vicii_ba_active(const vicii *v, uint64_t abs_cycle) {
    assert(v);
    return abs_cycle < v->timing.ba_low_until_abs ||
           abs_cycle < v->timing.sprite_ba_low_until_abs;
}

void vicii_destroy(vicii *v) {
    (void)v;
}

uint8_t vicii_read_register(vicii *v, uint16_t addr) {
    uint8_t reg;

    assert(v);

    reg = (uint8_t)(addr & 0x3fu);
    if (reg == VICII_REG_RASTER) {
        return (uint8_t)(v->timing.raster_line & 0xffu);
    }
    if (reg == VICII_REG_CONTROL_1) {
        uint8_t value = v->registers[reg] & 0x7fu;
        if ((v->timing.raster_line & 0x100u) != 0) {
            value |= 0x80u;
        }
        return value;
    }

    if (reg == 0x19) {
        return (uint8_t)(((v->irq_status & v->irq_enable) ? 0x80u : 0x00u) |
                         0x70u |
                         (v->irq_status & 0x0Fu));
    }

    if (reg == 0x1A) {
        return (uint8_t)(v->irq_enable | 0xF0u);
    }

    if (reg == VICII_REG_SPR_PRIORITY) {
        return v->sprite_priority;
    }

    if (reg == VICII_REG_SPR_SPR_COLL) {
        uint8_t value = v->sprite_sprite_collision;
        v->sprite_sprite_collision = 0;
        return value;
    }

    if (reg == VICII_REG_SPR_BG_COLL) {
        uint8_t value = v->sprite_background_collision;
        v->sprite_background_collision = 0;
        return value;
    }

    /* Phase G: $D016 — bits 7:5 are unused and read as 1 */
    if (reg == 0x16u) {
        return (uint8_t)((v->registers[reg] & 0x1Fu) | 0xE0u);
    }

    /* Phase G: $D020–$D02E (color registers) — bits 7:4 are unused and read as 1 */
    if (reg >= 0x20u && reg <= 0x2Eu) {
        return (uint8_t)((v->registers[reg] & 0x0Fu) | 0xF0u);
    }

    /* Phase G: $D02F–$D03F (unused register block) — all reads return $FF */
    if (reg >= 0x2Fu) {
        return 0xFFu;
    }

    return v->registers[reg];
}

uint8_t vicii_debug_read_register(const vicii *v, uint16_t addr) {
    uint8_t reg;

    assert(v);

    reg = (uint8_t)(addr & 0x3fu);
    if (reg == VICII_REG_RASTER) {
        return (uint8_t)(v->timing.raster_line & 0xffu);
    }
    if (reg == VICII_REG_CONTROL_1) {
        uint8_t value = v->registers[reg] & 0x7fu;
        if ((v->timing.raster_line & 0x100u) != 0) {
            value |= 0x80u;
        }
        return value;
    }
    if (reg == 0x19u) {
        return (uint8_t)(((v->irq_status & v->irq_enable) ? 0x80u : 0x00u) |
                         0x70u |
                         (v->irq_status & 0x0Fu));
    }
    if (reg == 0x1Au) {
        return (uint8_t)(v->irq_enable | 0xF0u);
    }
    if (reg == VICII_REG_SPR_PRIORITY) {
        return v->sprite_priority;
    }
    if (reg == VICII_REG_SPR_SPR_COLL) {
        return v->sprite_sprite_collision;
    }
    if (reg == VICII_REG_SPR_BG_COLL) {
        return v->sprite_background_collision;
    }
    if (reg == 0x16u) {
        return (uint8_t)((v->registers[reg] & 0x1Fu) | 0xE0u);
    }
    if (reg >= 0x20u && reg <= 0x2Eu) {
        return (uint8_t)((v->registers[reg] & 0x0Fu) | 0xF0u);
    }
    if (reg >= 0x2Fu) {
        return 0xFFu;
    }
    return v->registers[reg];
}

void vicii_write_register(vicii *v, uint16_t addr, uint8_t value) {
    uint8_t reg;

    assert(v);
    reg = (uint8_t)(addr & 0x3Fu);

    switch (reg) {
    case 0x11: /* CONTROL_1: bit 7 is RST8, updates raster_compare bit 8 */
        v->registers[reg] = value & 0x7Fu;
        if (value & 0x80u) {
            v->timing.raster_compare |= 0x100u;
        } else {
            v->timing.raster_compare &= 0x00FFu;
        }
        break;

    case 0x12: /* RASTER compare low byte */
        v->timing.raster_compare =
            (v->timing.raster_compare & 0x100u) | (uint16_t)value;
        break;

    case 0x19: /* IRQ_STATUS: write-1-to-clear */
        v->irq_status &= (uint8_t)(~value & 0x0Fu);
        v->registers[reg] = v->irq_status;
        break;

    case 0x1A: /* IRQ_ENABLE */
        v->irq_enable     = value & 0x0Fu;
        v->registers[reg] = v->irq_enable;
        break;

    case VICII_REG_SPR_PRIORITY:
        v->sprite_priority = value;
        v->registers[reg] = value;
        break;

    case VICII_REG_SPR_SPR_COLL:
    case VICII_REG_SPR_BG_COLL:
        break;

    default:
        v->registers[reg] = value;
        break;
    }
}

bool vicii_consume_frame_complete(vicii *v) {
    bool complete;

    assert(v);

    complete = v->timing.frame_complete;
    v->timing.frame_complete = false;
    return complete;
}

bool vicii_copy_completed_frame(vicii *v, c64_frame *out_frame, uint64_t machine_cycle) {
    assert(v);
    assert(out_frame);

    if (!v->completed_frame_ready) {
        return false;
    }

    memcpy(out_frame, &v->completed_frame, sizeof(*out_frame));
    out_frame->machine_cycle = machine_cycle;
    return true;
}

typedef struct {
    bool    active;
    uint8_t data[3];
} vicii_snapshot_sprite_row;

static void vicii_snapshot_sprite_line(
    const vicii               *v,
    const c64_bus_t           *bus,
    uint32_t                   raster_y,
    vicii_snapshot_sprite_row *rows)
{
    uint16_t vic_bank;
    uint16_t screen_base;
    uint8_t  enable;
    int      n;

    for (n = 0; n < 8; n++) rows[n].active = false;
    if (!bus) return;

    vic_bank    = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0Fu) * 0x0400u);
    enable      = v->registers[VICII_REG_SPR_ENABLE];

    for (n = 0; n < 8; n++) {
        uint8_t  spr_y;
        uint32_t sprite_y;
        bool     y_expand;
        int      dy;
        int      sprite_row;
        uint8_t  mc;
        uint16_t ptr_addr;
        uint8_t  pointer;
        uint16_t data_base;

        if (!((enable >> n) & 1u)) continue;

        spr_y    = v->registers[1 + n * 2];
        sprite_y = (uint32_t)spr_y;
        y_expand = (v->registers[VICII_REG_SPR_Y_EXPAND] >> n) & 1u;
        dy       = (int)raster_y - (int)sprite_y;

        if (dy < 0) continue;

        sprite_row = y_expand ? (dy / 2) : dy;
        if (sprite_row > 20) continue;

        mc        = (uint8_t)(sprite_row * 3u);
        ptr_addr  = (uint16_t)((vic_bank + screen_base + 0x03F8u + (uint16_t)n) & 0xFFFFu);
        pointer   = c64_bus_vic_read_ram(bus, ptr_addr);
        data_base = (uint16_t)((vic_bank + (uint16_t)pointer * 64u) & 0xFFFFu);

        rows[n].data[0] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc)      & 0xFFFFu));
        rows[n].data[1] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 1u) & 0xFFFFu));
        rows[n].data[2] = c64_bus_vic_read_ram(bus, (uint16_t)((data_base + mc + 2u) & 0xFFFFu));
        rows[n].active  = true;
    }
}

bool vicii_make_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle) {
    vicii_border_geometry g;
    bool     vborder, hborder;
    uint8_t  border_index;
    uint32_t border_color;
    uint32_t x, y;

    assert(v);
    assert(bus);
    assert(out_frame);

    if (vicii_copy_completed_frame(v, out_frame, machine_cycle)) {
        return true;
    }

    g = vicii_get_border_geometry(v);

    border_index = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    border_color = vicii_palette_argb[border_index];

    v->working_frame.width        = C64_FRAME_WIDTH;
    v->working_frame.height       = C64_FRAME_HEIGHT;
    v->working_frame.stride_bytes = C64_FRAME_WIDTH * sizeof(v->working_frame.pixels[0]);
    v->working_frame.pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    v->working_frame.frame_number = v->timing.frame_number;
    v->working_frame.machine_cycle = machine_cycle;

    vborder = true;   /* border active until the top compare fires */
    hborder = true;

    for (y = 0; y < C64_FRAME_HEIGHT; y++) {
        vicii_snapshot_sprite_row spr_rows[8];

        /* Vertical flip-flop transitions (Bauer §3.9) */
        if (y == g.top)    vborder = false;
        if (y == g.bottom) vborder = true;

        vicii_snapshot_sprite_line(v, bus, y, spr_rows);

        for (x = 0; x < C64_FRAME_WIDTH; x++) {
            vicii_bg_pixel bg;
            vicii_sprite_pixel sprites[8];
            uint32_t pixel;
            uint8_t enable_reg;
            int n;

            /* Horizontal flip-flop transitions (Bauer §3.9) */
            if (x == g.right) hborder = true;
            if (x == g.left && !vborder) hborder = false;

            bg = vicii_background_pixel(v, bus, &g, x, y);
            enable_reg = v->registers[VICII_REG_SPR_ENABLE];
            for (n = 0; n < 8; n++) {
                sprites[n] = vicii_sprite_pixel_make(0, false);
                if (!((enable_reg >> n) & 1u)) continue;
                if (!spr_rows[n].active) continue;
                {
                    uint16_t spr_x = (uint16_t)(v->registers[(uint8_t)(n * 2)] |
                        ((v->registers[VICII_REG_SPR_X_MSB] >> n & 1u) << 8));
                    int dx = vicii_sprite_dx_wrapped(x, spr_x);
                    sprites[n] = vicii_sprite_pixel_from_data(v, spr_rows[n].data, n, dx);
                }
            }

            pixel = vicii_compose_pixel(v, vborder || hborder, border_color, bg, sprites);
            v->working_frame.pixels[y * C64_FRAME_WIDTH + x] = pixel;
        }
    }

    memcpy(out_frame, &v->working_frame, sizeof(*out_frame));
    return true;
}

void vicii_copy_snapshot(const vicii *v, c64_vicii_snapshot *out) {
    assert(v);
    assert(out);

    out->raster_line = v->timing.raster_line;
    out->cycle_in_line = v->timing.cycle_in_line;
    out->frame_number = v->timing.frame_number;
    out->border_color = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    out->background_color = (uint8_t)(v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu);
}
