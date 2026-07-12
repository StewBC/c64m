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
    VICII_VC_MAX               = 1023,
    VICII_RC_MAX               = 7,
    VICII_IRQ_RASTER           = 0x01,
    VICII_IRQ_IMBC             = 0x02,
    VICII_IRQ_IMMC             = 0x04,

    /* BA timing is derived from the Phi2 schedule: three-cycle lead and a
       two-cycle release margin after a contiguous VIC-owned run. */
    VICII_BA_LEAD_CYCLES       = 3,
    VICII_BA_RELEASE_CYCLES    = 2,

    /* Vertical border compare values (raster-line units within the internal
       frame). Identical for PAL (6569) and NTSC (6567); only the total line
       count differs, not the display-window position. */
    VICII_VBORDER_TOP_25    = 51,
    VICII_VBORDER_BOTTOM_25 = 251,
    VICII_VBORDER_TOP_24    = 55,
    VICII_VBORDER_BOTTOM_24 = 247,
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

static const uint32_t vicii_pal_sprite_fetch_cycle[8] = {
    57, 59, 61, 0, 2, 4, 6, 8
};

static const uint32_t vicii_ntsc_sprite_fetch_cycle[8] = {
    59, 61, 63, 0, 2, 4, 6, 8
};

static void vicii_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static uint32_t vicii_frame_height(const vicii *v) {
    assert(v);
    return v->timing.lines_per_frame < C64_FRAME_HEIGHT ?
        v->timing.lines_per_frame :
        (uint32_t)C64_FRAME_HEIGHT;
}

static void vicii_prepare_frame(c64_frame *frame, uint32_t height, uint64_t frame_number, uint64_t machine_cycle, uint32_t fill_color) {
    assert(frame);

    frame->width = C64_FRAME_WIDTH;
    frame->height = height;
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
    vicii_prepare_frame(&v->working_frame, vicii_frame_height(v), v->timing.frame_number, 0, fill_color);
    /* Do NOT reset the vertical border flip-flop here. On real hardware it is
       only toggled by the top/bottom RSEL compares, so a program that dodges the
       bottom compare (the classic "open the border" trick) keeps the border open
       across the frame boundary, which is what lets sprites multiplexed into the
       upper/lower border draw image data outside the normal display window.
       Forcing it closed every frame re-hid those border sprites. Power-on/reset
       still establishes the closed default via vicii_reset(). */
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
    v->timing.aec_active              = true;
    v->timing.rdy_active              = true;
    v->irq_status                = 0;
    v->irq_enable                = 0;
    memset(v->video_matrix, 0, sizeof(v->video_matrix));
    memset(v->color_line,   0, sizeof(v->color_line));
    memset(v->sprite_mc,       0, sizeof(v->sprite_mc));
    memset(v->sprite_active,   0, sizeof(v->sprite_active));
    memset(v->sprite_visible,  0, sizeof(v->sprite_visible));
    memset(v->sprite_y_exp_ff, 0, sizeof(v->sprite_y_exp_ff));
    memset(v->sprite_pointer,  0, sizeof(v->sprite_pointer));
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
    v->main_border_ff         = true;
    /* Default on; runtime turbo policy re-applies after reset when needed. */
    v->pixel_output_enabled = true;
    vicii_begin_live_frame(v);
}

void vicii_set_pixel_output_enabled(vicii *v, bool enabled) {
    assert(v);

    v->pixel_output_enabled = enabled;
    if (!enabled) {
        /* Stale live buffers must not be published as completed frames. */
        v->completed_frame_ready = false;
    }
}

bool vicii_pixel_output_enabled(const vicii *v) {
    assert(v);

    return v->pixel_output_enabled;
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

static bool vicii_is_bad_line_at(const vicii *v, uint32_t y) {
    uint8_t  yscroll = v->registers[0x11] & 0x07u;
    bool     den     = (v->registers[0x11] & 0x10u) != 0;

    if (!den) return false;
    if (y < VICII_BADLINE_FIRST || y > VICII_BADLINE_LAST) return false;
    return (uint8_t)(y & 0x07u) == yscroll;
}

static bool vicii_is_bad_line(const vicii *v) {
    return vicii_is_bad_line_at(v, v->timing.raster_line);
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

/* Per-line display addressing context. Phase 2 (C64MVICIIEXPHASES): the display
   address is generated from the VIC's video counters, not from raster geometry.
   - display_active: the character-row sequencer is producing graphics on this
     line (VIC display state). When false the display window blanks to B0C, which
     is exactly what the previous geometric out-of-range check produced.
   - cell_base: VCBASE for this line (video-matrix index of the leftmost column).
   - row_in_cell: RC for this line (0..7).
   The live renderer fills this from the running sequencer so per-line $D011
   changes that force/suppress bad lines shift the address the way hardware does.
   The snapshot/debug renderer fills it geometrically to preserve prior output. */
typedef struct vicii_line_ctx {
    bool     display_active;
    uint16_t cell_base;
    uint8_t  row_in_cell;
    /* Phase 3: video-matrix / colour line latch source, indexed by column 0..39.
       Non-NULL on the live path -- the character codes and colour nibbles come
       from the buffers latched at the last bad line, so a mid-frame change to
       screen/colour RAM does not alter the rest of the character row. NULL on the
       snapshot/debug path, which reads RAM live (it has no sequencer history). */
    const uint8_t *vm_latch;
    const uint8_t *color_latch;
    /* Phase 4: idle-vs-display selection. When true (live path) the idle state is
       driven by the sequencer: any line not in display state shows idle-state
       graphics, wherever it falls vertically. When false (snapshot/debug path)
       the legacy geometry is used: idle graphics outside the fixed display window
       and a B0C blank for inactive rows inside it. */
    bool idle_when_inactive;
} vicii_line_ctx;

static vicii_border_geometry vicii_get_border_geometry(const vicii *v) {
    vicii_border_geometry g;
    bool rsel = (v->registers[0x11] & 0x08u) != 0;
    bool csel = (v->registers[0x16] & 0x08u) != 0;

    /* The vertical border flip-flop compare values are raster-line numbers and
       are identical on the 6569 (PAL) and 6567 (NTSC): first/last display line
       51/251 for RSEL=1 and 55/247 for RSEL=0. Only the total line count and the
       per-line cycle count differ between the standards, not where the display
       window sits. Anchoring NTSC to different values desynchronises the
       background (which is mapped through g.top) from sprites (which are placed
       by absolute raster line), so both standards share these compares. */
    g.top    = rsel ? (uint32_t)VICII_VBORDER_TOP_25    : (uint32_t)VICII_VBORDER_TOP_24;
    g.bottom = rsel ? (uint32_t)VICII_VBORDER_BOTTOM_25 : (uint32_t)VICII_VBORDER_BOTTOM_24;
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

/* Live per-line context taken straight from the running sequencer. During a line
   v->vc_base holds this line's VCBASE (it only advances at the end of RC==7) and
   v->rc holds this line's RC, so reading them at render time is per-line correct,
   including when a mid-line $D011 write has changed the bad-line progression. */
static vicii_line_ctx vicii_snapshot_line_ctx(const vicii *v, uint32_t y);

static vicii_line_ctx vicii_live_line_ctx(const vicii *v) {
    vicii_line_ctx c;

    /* DEN=0 produces no bad lines, so the sequencer never enters display state.
       The emulator still evaluates background graphics in that case to preserve
       the documented DEN=0 collision/foreground behaviour (VICII.md); the visible
       output is blanked to B0C downstream by the DEN handling. Fall back to the
       geometric mapping there, matching the pre-counter renderer exactly. FLI
       needs DEN=1, so this fallback never affects the bad-line-driven path. */
    if ((v->registers[0x11] & 0x10u) == 0u) {
        return vicii_snapshot_line_ctx(v, v->timing.raster_line);
    }

    c.display_active     = v->display_state;
    c.cell_base          = v->vc_base;
    c.row_in_cell        = v->rc;
    c.vm_latch           = v->video_matrix;
    c.color_latch        = v->color_line;
    c.idle_when_inactive = true;
    return c;
}

/* Snapshot/debug per-line context derived from raster geometry. Reproduces the
   previous positional mapping exactly: the display is active where the
   YSCROLL-adjusted row is in [0,200); cell base and row-in-cell decompose that
   same adjusted row. Used only when no completed live frame exists. */
static vicii_line_ctx vicii_snapshot_line_ctx(const vicii *v, uint32_t y) {
    vicii_line_ctx c;
    uint8_t  yscroll = v->registers[0x11] & 0x07u;
    uint32_t sy      = y - (uint32_t)VICII_VBORDER_TOP_25;
    uint32_t adjusted;

    /* Snapshot/debug path has no sequencer history: read RAM live (NULL latch)
       and use the legacy fixed-window idle geometry. */
    c.vm_latch           = NULL;
    c.color_latch        = NULL;
    c.idle_when_inactive = false;

    if (y < (uint32_t)VICII_VBORDER_TOP_25 ||
        !vicii_display_adjusted_y(sy, yscroll, &adjusted)) {
        c.display_active = false;
        c.cell_base      = 0;
        c.row_in_cell    = 0;
        return c;
    }

    c.display_active = true;
    c.row_in_cell    = (uint8_t)(adjusted & 7u);
    c.cell_base      = (uint16_t)((adjusted / 8u) * (uint32_t)VICII_TEXT_COLUMNS);
    return c;
}

/* The line sequencer decides which sprites need DMA at cycle 0.  Their pointer
   and three data bytes are fetched later at their scheduled Phi1/Phi2 slots;
   do not batch those bus reads here. */
static void vicii_prepare_sprite_line(vicii *v, const c64_bus_t *bus) {
    uint8_t  enable;
    int      n;

    memset(v->sprite_visible, 0, sizeof(v->sprite_visible));
    enable      = v->registers[VICII_REG_SPR_ENABLE];

    for (n = 0; n < 8; n++) {
        uint8_t  spr_y    = v->registers[1 + n * 2];
        uint32_t raster_y = v->timing.raster_line;
        if ((enable >> n) & 1u) {
            if (raster_y == ((uint32_t)spr_y + 1u) % v->timing.lines_per_frame) {
                v->sprite_mc[n]       = 0;
                v->sprite_active[n]   = true;
                v->sprite_y_exp_ff[n] = false;
            }
        } else {
            v->sprite_active[n]  = false;
            v->sprite_visible[n] = false;
        }

        /* The live renderer consumes a complete row before the first display
           pixels of this line are emitted. Keep that established latch timing
           while the schedule below records the real split pointer/data slots;
           a later renderer-pipeline change can retire this pre-latch without
           changing the schedule or BA interface. */
        if (bus && v->sprite_active[n]) {
            uint8_t mc = v->sprite_mc[n];
            bool advance_mc;
            uint16_t vic_bank = c64_bus_vic_bank_base(bus);
            uint16_t screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0fu) * 0x0400u);
            uint16_t ptr_addr = (uint16_t)(vic_bank + screen_base + 0x03f8u + (uint16_t)n);
            uint16_t data_base;

            if ((v->registers[VICII_REG_SPR_Y_EXPAND] & (uint8_t)(1u << n)) != 0u) {
                advance_mc = v->sprite_y_exp_ff[n];
                v->sprite_y_exp_ff[n] ^= 1u;
            } else {
                advance_mc = true;
            }
            v->sprite_pointer[n] = c64_bus_vic_read_ram(bus, ptr_addr);
            data_base = (uint16_t)(vic_bank + (uint16_t)v->sprite_pointer[n] * 64u);
            v->sprite_data[n][0] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc));
            v->sprite_data[n][1] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc + 1u));
            v->sprite_data[n][2] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc + 2u));
            v->sprite_visible[n] = true;
            if (advance_mc) {
                mc = (uint8_t)(mc + 3u);
                if (mc >= 63u) v->sprite_active[n] = false;
                else v->sprite_mc[n] = mc;
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

/* Idle-state background pixel. Vertically outside the display window the VIC is
   in idle state: g-accesses read a fixed byte from $3FFF (or $39FF when ECM=1)
   and are displayed with the c-access data forced to 0. This is what shows
   through when a program opens the vertical border (the classic sprites-over-
   border title-screen technique), so the un-sprited gaps are idle graphics --
   usually black -- not the live background colour. Returning the live b0c here
   is what made c64m paint the opened border with the background colour instead
   of the near-black idle output VICE/hardware produce. */
static vicii_bg_pixel vicii_idle_pixel(const vicii *v, const c64_bus_t *bus, uint32_t x) {
    uint32_t b0c   = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu];
    uint32_t black = vicii_palette_argb[0];
    bool ecm = (v->registers[0x11] & 0x40u) != 0u;
    bool bmm = (v->registers[0x11] & 0x20u) != 0u;
    bool mcm = (v->registers[0x16] & 0x10u) != 0u;
    uint16_t vic_bank = c64_bus_vic_bank_base(bus);
    uint16_t addr = (uint16_t)(vic_bank + (ecm ? 0x39ffu : 0x3fffu));
    uint8_t g = c64_bus_vic_read_ram(bus, addr);
    uint32_t sx_raw = x - (uint32_t)VICII_HBORDER_LEFT_40;

    if (mcm) {
        uint8_t pair = (uint8_t)((g >> (6u - (sx_raw & 6u))) & 3u);
        if (pair == 0u) {
            return vicii_bg_pixel_make(b0c, false);
        }
        /* 01/10/11 all resolve through the zero c-data / colour-RAM to black. */
        return vicii_bg_pixel_make(black, pair >= 2u);
    } else {
        uint8_t bit = (uint8_t)(0x80u >> (sx_raw & 7u));
        if (bmm) {
            /* Standard bitmap idle: both nibbles of the zero c-data are black. */
            return vicii_bg_pixel_make(black, (g & bit) != 0u);
        }
        /* Standard/ECM text idle: set bits take foreground colour 0 (black). */
        if (g & bit) {
            return vicii_bg_pixel_make(black, true);
        }
        return vicii_bg_pixel_make(b0c, false);
    }
}

static vicii_bg_pixel vicii_background_pixel(
    const vicii *v,
    const c64_bus_t *bus,
    const vicii_border_geometry *g,
    const vicii_line_ctx *lc,
    uint32_t x,
    uint32_t y)
{
    uint32_t b0c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu];
    uint32_t b1c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_1] & 0x0fu];
    uint32_t b2c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_2] & 0x0fu];
    uint32_t b3c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_3] & 0x0fu];
    uint8_t mode;
    uint8_t xscroll;
    bool den;
    uint32_t sx_raw;
    uint32_t sx;
    uint32_t row_in_cell;
    uint32_t col;
    uint16_t cell;
    uint16_t vic_bank;
    uint16_t screen_base;
    uint16_t char_base;
    uint16_t bitmap_base;

    if (x < g->left || x >= g->right) {
        /* Side border. Overridden by the border colour in vicii_compose_pixel,
           so the exact value here only matters when the caller does not treat it
           as border; b0c keeps prior behaviour. */
        return vicii_bg_pixel_make(b0c, false);
    }

    if (lc->idle_when_inactive) {
        /* Live path: idle-state graphics are shown whenever the sequencer is not
           in display state, wherever that occurs vertically. For an ordinary
           screen this is exactly the region outside the fixed display window
           (display state spans 51..250 at YSCROLL=3), so normal output is
           unchanged; per-line $D011 bad-line forcing can now open or close the
           display state mid-window, which is what the expose reveal needs. The
           badline/display range is driven by YSCROLL, not RSEL, so RSEL still
           does not split the picture. */
        if (!lc->display_active) {
            return vicii_idle_pixel(v, bus, x);
        }
    } else if (y < (uint32_t)VICII_VBORDER_TOP_25 || y >= (uint32_t)VICII_VBORDER_BOTTOM_25) {
        /* Snapshot/debug path: idle-state graphics outside the fixed 25-row
           window. Inactive rows inside the window fall through to the B0C blank
           below, preserving the legacy geometric reconstruction. */
        return vicii_idle_pixel(v, bus, x);
    }

    mode = (uint8_t)(((v->registers[0x11] & 0x40u) ? 4u : 0u) |
                     ((v->registers[0x11] & 0x20u) ? 2u : 0u) |
                     ((v->registers[0x16] & 0x10u) ? 1u : 0u));
    xscroll = v->registers[0x16] & 0x07u;
    den = (v->registers[0x11] & 0x10u) != 0u;
    sx_raw = x - (uint32_t)VICII_HBORDER_LEFT_40;

    if (mode >= 5u) {
        return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[0], false));
    }

    /* Outside the active character-row sequence this line is blank (B0C). This
       replaces the previous YSCROLL-adjusted range check; for a static screen the
       active span is identical, but it now also tracks bad-line forcing. */
    if (!lc->display_active) {
        return vicii_bg_pixel_make(b0c, false);
    }

    if (sx_raw < (uint32_t)xscroll) {
        return vicii_bg_pixel_make(b0c, false);
    }

    sx = sx_raw - (uint32_t)xscroll;
    col = sx / 8u;
    if (col >= 40u) {
        return vicii_bg_pixel_make(b0c, false);
    }

    /* Address generation is now counter-driven: the row within the character
       cell is RC, and the cell index is VCBASE + column. */
    row_in_cell = lc->row_in_cell;
    cell = (uint16_t)(lc->cell_base + col);
    vic_bank = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(vic_bank + (v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    char_base = (uint16_t)(vic_bank + ((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);
    bitmap_base = (uint16_t)(vic_bank + ((v->registers[VICII_REG_MEMORY_POINTER] >> 3) & 1u) * 0x2000u);

    /* Character code and colour come from the line latch on the live path (filled
       at the last bad line) or live RAM on the snapshot path. The g-access (glyph
       / bitmap byte) is a per-line fetch and is always read live below. */
    {
    uint8_t vm_byte   = lc->vm_latch    ? lc->vm_latch[col]
                                        : c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
    uint8_t color_reg = lc->color_latch ? lc->color_latch[col]
                                        : c64_bus_vic_read_color(bus, cell);

    switch (mode) {
    case 0u:
        {
            uint8_t code = vm_byte;
            uint8_t glyph = c64_bus_vic_read_char_glyph_at(bus, char_base, code, (uint8_t)row_in_cell);
            uint8_t fg = color_reg;
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            if (glyph & bit) {
                return vicii_apply_den_blanking(den, b0c, vicii_bg_pixel_make(vicii_palette_argb[fg & 0x0fu], true));
            }
            return vicii_bg_pixel_make(b0c, false);
        }

    case 1u:
        {
            uint8_t code = vm_byte;
            uint8_t color_nib = color_reg;
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
            uint8_t color_nib = color_reg;
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
            uint8_t code = vm_byte;
            uint8_t ecm_sel = (code >> 6) & 3u;
            uint8_t glyph = c64_bus_vic_read_char_glyph_at(bus, char_base, code & 0x3fu, (uint8_t)row_in_cell);
            uint8_t fg_nib = color_reg;
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

static uint32_t vicii_live_pixel(vicii *v, const c64_bus_t *bus, const vicii_border_geometry *g, const vicii_line_ctx *lc, uint32_t x, uint32_t y, bool border_active) {
    uint32_t border_color = vicii_palette_argb[v->registers[VICII_REG_BORDER_COLOR] & 0x0fu];
    vicii_bg_pixel bg = vicii_background_pixel(v, bus, g, lc, x, y);
    vicii_sprite_pixel sprites[8];
    bool any_sprite_enabled = false;
    int n;

    for (n = 0; n < 8; n++) {
        any_sprite_enabled = any_sprite_enabled || v->sprite_line_enabled[n];
    }

    if (!any_sprite_enabled) {
        if (border_active) {
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

    return vicii_compose_pixel(v, border_active, border_color, bg, sprites);
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
    vicii_line_ctx lc;
    uint32_t y;
    uint32_t x0;
    uint32_t x1;
    uint32_t x;

    if (!bus) {
        return;
    }

    y = v->timing.raster_line;
    if (y >= v->working_frame.height) {
        return;
    }

    g = vicii_get_border_geometry(v);
    lc = vicii_live_line_ctx(v);

    /* Anchored dot mapping (C64MVICII_SIDEBORDER.md §2.2): each cycle paints its
       true 8 VIC dots, so buffer_x == VIC X-coordinate and the paint cycle for
       every column matches hardware. Display column 0 (X=24) is drawn at the
       first c-access cycle (15); each cycle advances X by 8. This replaces the
       scaled cycle*width/cycles_per_line mapping, which was not dot-anchored and
       smeared a mid-line $D016 CSEL write across the border-compare columns.
       Cycles whose dots fall outside the 384-px crop (line start/end H-blank)
       produce an empty span and paint nothing; every column 0..383 is still
       painted exactly once per line (PAL cycles 12..59, NTSC 12..59). */
    {
        int32_t xs = (int32_t)VICII_HBORDER_LEFT_40 +
            ((int32_t)v->timing.cycle_in_line - (int32_t)VICII_CACCESS_FIRST_CYCLE) *
            (int32_t)VICII_CHARACTER_WIDTH;
        int32_t xe = xs + (int32_t)VICII_CHARACTER_WIDTH;
        if (xs < 0) xs = 0;
        if (xe > (int32_t)C64_FRAME_WIDTH) xe = (int32_t)C64_FRAME_WIDTH;
        if (xe < xs) xe = xs;
        x0 = (uint32_t)xs;
        x1 = (uint32_t)xe;
    }

    for (x = x0; x < x1; x++) {
        /* Main border flip-flop (Bauer 3.9 rules 1 & 6), advanced per dot in
           increasing X with the CSEL live for this cycle (g is per-cycle). Rule 1:
           set at the right compare. Rule 6: reset at the left compare when the
           vertical border is not active. Both are applied before the dot is drawn,
           so the compare column itself takes the new state. A timed $D016 CSEL
           change moves g.left/g.right and can leave the FF set (side border open)
           for the rest of the line. */
        if (x == g.right) {
            v->main_border_ff = true;
        }
        if (x == g.left && !v->vertical_border_active) {
            v->main_border_ff = false;
        }
        v->working_frame.pixels[y * C64_FRAME_WIDTH + x] =
            vicii_live_pixel(v, bus, &g, &lc, x, y,
                             v->main_border_ff || v->vertical_border_active);
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
        if (next_y == ((uint32_t)spr_y + 1u) % v->timing.lines_per_frame) {
            return true;
        }
    }
    return false;
}

static bool vicii_sprite_dma_current_line(const vicii *v, int n) {
    uint8_t enable = v->registers[VICII_REG_SPR_ENABLE];
    uint8_t spr_y = v->registers[1 + n * 2];

    if (v->sprite_visible[n] || v->sprite_active[n]) {
        return true;
    }
    return ((enable >> n) & 1u) != 0u &&
        v->timing.raster_line == ((uint32_t)spr_y + 1u) % v->timing.lines_per_frame;
}

static const uint32_t *vicii_sprite_fetch_cycle_table(const vicii *v) {
    return v->timing.standard == VICII_VIDEO_STANDARD_PAL ?
        vicii_pal_sprite_fetch_cycle :
        vicii_ntsc_sprite_fetch_cycle;
}

static bool vicii_sprite_slot(const vicii *v, uint32_t cycle, int *out_sprite, bool *out_second_cycle) {
    const uint32_t *fetch_cycle = vicii_sprite_fetch_cycle_table(v);
    int n;

    for (n = 0; n < 8; n++) {
        if (cycle == fetch_cycle[n]) {
            *out_sprite = n;
            *out_second_cycle = false;
            return true;
        }
        if (cycle == fetch_cycle[n] + 1u) {
            *out_sprite = n;
            *out_second_cycle = true;
            return true;
        }
    }
    return false;
}

static void vicii_schedule_bus_access(vicii *v, uint32_t cycle) {
    int sprite;
    bool second_cycle;

    /* Phi1 is always VIC-owned. In display state it performs g-accesses; in
       idle state it performs the documented fixed idle access. Sprite slots
       replace those accesses with pointer/data work. Phi2 is CPU-visible only
       for bad-line c-accesses and the first/third sprite data bytes. */
    v->timing.bus_access = VICII_BUS_ACCESS_NONE;
    v->timing.bus_access_phi1 = v->display_state ?
        VICII_BUS_ACCESS_G : VICII_BUS_ACCESS_IDLE;

    if (vicii_sprite_slot(v, cycle, &sprite, &second_cycle)) {
        v->timing.bus_access_phi1 = second_cycle ?
            VICII_BUS_ACCESS_SPRITE_DATA : VICII_BUS_ACCESS_SPRITE_POINTER;
        if (vicii_sprite_dma_current_line(v, sprite)) {
            v->timing.bus_access = VICII_BUS_ACCESS_SPRITE_DATA;
        }
        return;
    }

    if (v->bad_line && cycle >= VICII_CACCESS_FIRST_CYCLE &&
        cycle <= VICII_CACCESS_LAST_CYCLE) {
        v->timing.bus_access = VICII_BUS_ACCESS_C;
    }
}

/* Ask whether a CPU-visible Phi2 VIC access is scheduled offset cycles from the
   current cycle. This is the sole source for BA: it also handles the sprite
   3/4 cross-line look-ahead without a separate BA-assert table. */
static bool vicii_phi2_access_scheduled_after(
    const vicii *v,
    uint32_t cycle,
    uint32_t offset)
{
    uint32_t future_cycle = cycle + offset;
    bool next_line = false;
    uint32_t future_line = v->timing.raster_line;
    int sprite;
    bool second_cycle;

    if (future_cycle >= v->timing.cycles_per_line) {
        future_cycle -= v->timing.cycles_per_line;
        next_line = true;
        future_line = (future_line + 1u) % v->timing.lines_per_frame;
    }
    if (vicii_is_bad_line_at(v, future_line) &&
        future_cycle >= VICII_CACCESS_FIRST_CYCLE &&
        future_cycle <= VICII_CACCESS_LAST_CYCLE) {
        return true;
    }
    if (!vicii_sprite_slot(v, future_cycle, &sprite, &second_cycle)) {
        return false;
    }
    (void)second_cycle;
    return next_line ? vicii_sprite_dma_next_line(v, sprite) :
        vicii_sprite_dma_current_line(v, sprite);
}

static bool vicii_phi2_access_scheduled_now(const vicii *v) {
    uint32_t cycle = v->timing.cycle_in_line;
    int sprite;
    bool second_cycle;

    if ((v->bad_line || vicii_is_bad_line(v)) &&
        cycle >= VICII_CACCESS_FIRST_CYCLE && cycle <= VICII_CACCESS_LAST_CYCLE) {
        return true;
    }
    return vicii_sprite_slot(v, cycle, &sprite, &second_cycle) &&
        vicii_sprite_dma_current_line(v, sprite);
}

static void vicii_derive_ba_from_schedule(vicii *v, uint32_t cycle, uint64_t abs_cycle) {
    uint32_t last_offset;
    uint32_t release;
    uint32_t end_cycle;
    uint32_t end_line;
    int sprite;
    bool second_cycle;
    bool badline_window;

    if (!vicii_phi2_access_scheduled_after(v, cycle, VICII_BA_LEAD_CYCLES)) {
        return;
    }
    last_offset = VICII_BA_LEAD_CYCLES;
    while (vicii_phi2_access_scheduled_after(v, cycle, last_offset + 1u)) {
        last_offset++;
    }

    /* Badline character fetches need a slightly longer post-steal BA hold so
       dual-bit IEC samples that resume after RDY do not land on a released bus.
       Sprite BA windows keep the classic RELEASE=2 length (unit matrix). */
    end_cycle = cycle + last_offset;
    end_line = v->timing.raster_line;
    if (end_cycle >= v->timing.cycles_per_line) {
        end_cycle -= v->timing.cycles_per_line;
        end_line = (end_line + 1u) % v->timing.lines_per_frame;
    }
    badline_window =
        vicii_is_bad_line_at(v, end_line) &&
        end_cycle >= VICII_CACCESS_FIRST_CYCLE &&
        end_cycle <= VICII_CACCESS_LAST_CYCLE &&
        !vicii_sprite_slot(v, end_cycle, &sprite, &second_cycle);
    release = badline_window ? 3u : (uint32_t)VICII_BA_RELEASE_CYCLES;

    if (abs_cycle + last_offset + release > v->timing.ba_low_until_abs) {
        v->timing.ba_low_until_abs = abs_cycle + last_offset + release;
    }
}

static void vicii_fetch_c_access(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    uint32_t col = cycle - VICII_CACCESS_FIRST_CYCLE;
    uint16_t screen_base;
    uint16_t vc_col;

    if (!bus || col >= VICII_TEXT_COLUMNS) return;
    screen_base = (uint16_t)(c64_bus_vic_bank_base(bus) +
        (v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    vc_col = (uint16_t)((v->vc + col) & VICII_VC_MAX);
    v->video_matrix[col] = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + vc_col));
    v->color_line[col] = c64_bus_vic_read_color(bus, vc_col);
}

static void vicii_fetch_g_or_idle_access(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    uint16_t vic_bank;
    uint16_t address;
    uint32_t col;

    if (!bus) return;
    vic_bank = c64_bus_vic_bank_base(bus);
    if (!v->display_state || cycle < VICII_CACCESS_FIRST_CYCLE ||
        cycle > VICII_CACCESS_LAST_CYCLE) {
        address = (uint16_t)(vic_bank +
            ((v->registers[VICII_REG_CONTROL_1] & 0x40u) ? 0x39ffu : 0x3fffu));
        (void)c64_bus_vic_read_ram(bus, address);
        return;
    }

    col = cycle - VICII_CACCESS_FIRST_CYCLE;
    if ((v->registers[VICII_REG_CONTROL_1] & 0x20u) != 0u) {
        uint16_t bitmap_base = (uint16_t)(vic_bank +
            ((v->registers[VICII_REG_MEMORY_POINTER] >> 3) & 1u) * 0x2000u);
        address = (uint16_t)(bitmap_base + (((uint16_t)(v->vc_base + col) & 0x03ffu) << 3) + v->rc);
        (void)c64_bus_vic_read_ram(bus, address);
    } else {
        uint16_t char_base = (uint16_t)(vic_bank +
            ((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);
        uint8_t code = v->video_matrix[col];
        if ((v->registers[VICII_REG_CONTROL_1] & 0x40u) != 0u) code &= 0x3fu;
        (void)c64_bus_vic_read_char_glyph_at(bus, char_base, code, v->rc);
    }
}

static void vicii_fetch_sprite_slot(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    int n;
    bool second_cycle;
    uint16_t vic_bank;
    uint16_t screen_base;
    uint16_t data_base;
    uint8_t mc;

    if (!bus || !vicii_sprite_slot(v, cycle, &n, &second_cycle)) return;
    vic_bank = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(((v->registers[VICII_REG_MEMORY_POINTER] >> 4) & 0x0fu) * 0x0400u);

    if (!second_cycle) {
        uint16_t ptr = (uint16_t)(vic_bank + screen_base + 0x03f8u + (uint16_t)n);
        v->sprite_pointer[n] = c64_bus_vic_read_ram(bus, ptr);
        if (!v->sprite_visible[n]) return;
        mc = v->sprite_mc[n];
        data_base = (uint16_t)(vic_bank + (uint16_t)v->sprite_pointer[n] * 64u);
        (void)c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc));
        return;
    }

    if (!v->sprite_visible[n]) return;
    mc = v->sprite_mc[n];
    data_base = (uint16_t)(vic_bank + (uint16_t)v->sprite_pointer[n] * 64u);
    (void)c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc + 1u));
    (void)c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc + 2u));
}

static void vicii_execute_scheduled_fetches(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    if (v->timing.bus_access_phi1 == VICII_BUS_ACCESS_SPRITE_POINTER ||
        v->timing.bus_access_phi1 == VICII_BUS_ACCESS_SPRITE_DATA ||
        v->timing.bus_access == VICII_BUS_ACCESS_SPRITE_DATA) {
        vicii_fetch_sprite_slot(v, bus, cycle);
        return;
    }
    vicii_fetch_g_or_idle_access(v, bus, cycle);
    if (v->timing.bus_access == VICII_BUS_ACCESS_C) {
        vicii_fetch_c_access(v, bus, cycle);
    }
}

/* Fill the video-matrix and colour-line latches for the whole line (all 40
   columns) from screen/colour RAM at VC. On hardware the c-accesses are spread
   across cycles 15-54, but nothing observes the partially-filled buffers -- only
   the renderer reads them, and the cycle->pixel mapping draws the leftmost
   columns before cycle 15. Filling the complete line latch at the bad line's
   start guarantees every column is drawn from a consistent, correct latch, and
   yields identical values to the per-cycle fill (same RAM, same VC). BA stalling
   is modelled separately (cycle 12), so it is unaffected. */
static void vicii_fill_line_latch(vicii *v, const c64_bus_t *bus) {
    uint16_t screen_base = (uint16_t)(c64_bus_vic_bank_base(bus) +
        (v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    uint32_t col;

    for (col = 0; col < (uint32_t)VICII_TEXT_COLUMNS; col++) {
        uint16_t vc_col = (uint16_t)(v->vc + col);
        v->video_matrix[col] = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + vc_col));
        v->color_line[col]   = c64_bus_vic_read_color(bus, vc_col);
    }
}

void vicii_step_cycle(vicii *v, const c64_bus_t *bus, uint64_t abs_cycle) {
    uint32_t cycle;

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

        /* VC is reloaded from VCBASE at the start of every line (the hardware
           cycle-14 load), not only on bad lines. Within a character row VCBASE is
           unchanged, so all 8 rows re-address the same 40 video-matrix cells; the
           +40 advance happens once per row at the RC==7 line end below. */
        v->vc = v->vc_base;

        /* Clear the per-line bad-line latch; the condition is (re)evaluated every
           cycle below so a mid-line $D011 write can still commit it this line. */
        v->bad_line = false;
    }

    /* Bad Line Condition (Bauer 3.5): present whenever RASTER is in [$30,$f7],
       (RASTER & 7) == YSCROLL and DEN is set. It can turn true at ANY cycle if a
       $D011 write changes YSCROLL mid-line -- this is the core of FLI/expose. The
       first cycle it holds on a line, commit the bad line once: enter display
       state, reset RC, and latch the row. An ordinary bad line commits at cycle 0,
       reproducing the previous behaviour exactly. */
    if (!v->bad_line && vicii_is_bad_line(v)) {
        v->bad_line      = true;
        v->display_state = true;
        v->rc            = 0;
        if (bus) {
            vicii_fill_line_latch(v, bus);
        }
    }

    if (cycle == 0) {
        vicii_prepare_sprite_line(v, bus);
        vicii_latch_sprite_line_state(v);
    }

    vicii_schedule_bus_access(v, cycle);
    vicii_derive_ba_from_schedule(v, cycle, abs_cycle);
    v->timing.aec_active = !vicii_phi2_access_scheduled_now(v);
    v->timing.rdy_active = !vicii_ba_active(v, abs_cycle);
    vicii_execute_scheduled_fetches(v, bus, cycle);

    if (v->pixel_output_enabled) {
        vicii_render_live_cycle(v, bus);
    }

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
        /* This display line advanced VC by one character row's worth of cells.
           At RC==7 the character row is finished: latch VCBASE forward by 40 and
           leave display state (idle) until the next bad line re-enters it. Bauer
           only increments RC while it is below 7, so RC is not wrapped here; the
           next bad line resets it to 0. */
        v->vc = (uint16_t)((v->vc + VICII_TEXT_COLUMNS) & (uint16_t)VICII_VC_MAX);

        if (v->rc == VICII_RC_MAX) {
            v->vc_base       = v->vc;
            v->display_state = false;
        } else {
            v->rc = (uint8_t)(v->rc + 1u);
        }
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
    v->timing.frame_complete = true;

    if (v->pixel_output_enabled) {
        v->working_frame.frame_number = v->timing.frame_number;
        memcpy(&v->completed_frame, &v->working_frame, sizeof(v->completed_frame));
        v->completed_frame_ready = true;
        vicii_begin_live_frame(v);
    } else {
        /* Timing still advances; no ARGB buffers to copy or re-clear. */
        v->completed_frame_ready = false;
    }

    v->vc            = 0;
    v->vc_base       = 0;
    v->rc            = 0;
    v->display_state = false;
    v->bad_line      = false;
}

bool vicii_ba_active(const vicii *v, uint64_t abs_cycle) {
    assert(v);
    return abs_cycle < v->timing.ba_low_until_abs;
}

bool vicii_aec_active(const vicii *v) {
    assert(v);
    return !vicii_phi2_access_scheduled_now(v);
}

bool vicii_rdy_active(const vicii *v, uint64_t abs_cycle) {
    assert(v);
    return !vicii_ba_active(v, abs_cycle);
}

vicii_bus_access_kind vicii_bus_access(const vicii *v) {
    assert(v);
    return v->timing.bus_access;
}

vicii_bus_access_kind vicii_bus_access_phi1(const vicii *v) {
    assert(v);
    return v->timing.bus_access_phi1;
}

void vicii_destroy(vicii *v) {
    (void)v;
}

uint8_t vicii_read_register(vicii *v, uint16_t addr) {
    uint8_t reg;

    assert(v);

    reg = (uint8_t)(addr & 0x3fu);
    if (reg == VICII_REG_RASTER) {
        /* Live raster counter — same phase as badline/BA (not a +1 fudge). */
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

/* After $D011/$D012 update the 9-bit raster compare: if it now equals the
   current raster line, raise the raster IRQ immediately. Hardware does this
   on the write (not only at the start of a matching line). Galencia NTSC's
   bottom-border IRQ chain writes compare == current line mid-line to chain
   the next slice; without re-trigger the cleanup IRQ is skipped, RST8 is left
   set, and every other frame misses the top-of-frame multiplex (flashing HUD,
   half-rate music). */
static void vicii_raster_compare_maybe_trigger(vicii *v) {
    if (v->timing.raster_line == (uint32_t)v->timing.raster_compare) {
        vicii_assert_raster_irq(v);
    }
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
        vicii_raster_compare_maybe_trigger(v);
        break;

    case 0x12: /* RASTER compare low byte */
        v->timing.raster_compare =
            (v->timing.raster_compare & 0x100u) | (uint16_t)value;
        vicii_raster_compare_maybe_trigger(v);
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

static bool vicii_make_frame_snapshot_internal(
    vicii *v,
    const c64_bus_t *bus,
    c64_frame *out_frame,
    uint64_t machine_cycle,
    bool prefer_completed_frame) {
    vicii_border_geometry g;
    bool     vborder, hborder;
    uint8_t  border_index;
    uint32_t border_color;
    uint32_t x, y;

    assert(v);
    assert(bus);
    assert(out_frame);

    if (prefer_completed_frame && vicii_copy_completed_frame(v, out_frame, machine_cycle)) {
        return true;
    }

    g = vicii_get_border_geometry(v);

    border_index = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    border_color = vicii_palette_argb[border_index];

    out_frame->width        = C64_FRAME_WIDTH;
    out_frame->height       = vicii_frame_height(v);
    out_frame->stride_bytes = C64_FRAME_WIDTH * sizeof(out_frame->pixels[0]);
    out_frame->pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    out_frame->frame_number = v->timing.frame_number;
    out_frame->machine_cycle = machine_cycle;

    vborder = true;   /* border active until the top compare fires */
    hborder = true;

    for (y = 0; y < out_frame->height; y++) {
        vicii_snapshot_sprite_row spr_rows[8];
        vicii_line_ctx lc = vicii_snapshot_line_ctx(v, y);

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

            bg = vicii_background_pixel(v, bus, &g, &lc, x, y);
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
            out_frame->pixels[y * C64_FRAME_WIDTH + x] = pixel;
        }
    }

    return true;
}

bool vicii_make_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle) {
    return vicii_make_frame_snapshot_internal(v, bus, out_frame, machine_cycle, true);
}

bool vicii_make_current_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle) {
    return vicii_make_frame_snapshot_internal(v, bus, out_frame, machine_cycle, false);
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
