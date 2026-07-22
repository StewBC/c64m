#include "vicii.h"

#include "c64_bus.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    /* VICE PAL_CYCLE(n) is n-1: Phi2 c-accesses are cycles 14..53 and Phi1
       g-accesses are cycles 15..54 in c64m's zero-based raster coordinates. */
    VICII_CACCESS_FIRST_CYCLE  = 14,
    VICII_CACCESS_LAST_CYCLE   = 53,
    VICII_GACCESS_FIRST_CYCLE  = 15,
    VICII_GACCESS_LAST_CYCLE   = 54,
    VICII_VC_RC_CYCLE          = 13,
    VICII_UPDATE_RC_CYCLE      = 57,
    VICII_VC_MAX               = 1023,
    VICII_RC_MAX               = 7,
    VICII_IRQ_RASTER           = 0x01,
    VICII_IRQ_IMBC             = 0x02,
    VICII_IRQ_IMMC             = 0x04,

    /* VICE uses 3+1 for the BA-to-AEC acquisition countdown: the first BA
       cycle consumes the extra count immediately. */
    VICII_BA_LEAD_CYCLES       = 3,

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
    58, 60, 62, 64, 1, 3, 5, 7
};

/* VICE viciisc cycle_tab_{pal,ntsc}: sprite bits whose DMA makes BA low in
   each zero-based raster cycle.  These masks are deliberately explicit.  A
   generic look-ahead interval gets both the cross-line start and, critically,
   the release after sprite 7 wrong by one cycle. */
static const uint8_t vicii_pal_sprite_ba_mask[63] = {
    [0] = 0x18, [1] = 0x38, [2] = 0x30, [3] = 0x70, [4] = 0x60,
    [5] = 0xE0, [6] = 0xC0, [7] = 0xC0, [8] = 0x80, [9] = 0x80,
    [54] = 0x01, [55] = 0x01, [56] = 0x03, [57] = 0x03,
    [58] = 0x07, [59] = 0x06, [60] = 0x0E, [61] = 0x0C,
    [62] = 0x1C
};

static const uint8_t vicii_ntsc_sprite_ba_mask[65] = {
    [0] = 0x38, [1] = 0x30, [2] = 0x70, [3] = 0x60, [4] = 0xE0,
    [5] = 0xC0, [6] = 0xC0, [7] = 0x80, [8] = 0x80,
    [55] = 0x01, [56] = 0x01, [57] = 0x03, [58] = 0x03,
    [59] = 0x07, [60] = 0x06, [61] = 0x0E, [62] = 0x0C,
    [63] = 0x1C, [64] = 0x18
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

    /* Any pixel this frame does not paint lies outside the display window, which
       is border. That holds with DEN=0 too: the vertical border flip-flop never
       opens, so the border covers the whole screen - it does NOT become B0C.
       Selecting B0C on DEN=0 was inverted, and showed up on NTSC as a
       $D021-coloured band under a $D020-coloured boot screen. */
    fill_index = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    fill_color = vicii_palette_argb[fill_index];
    vicii_prepare_frame(&v->working_frame, vicii_frame_height(v), v->timing.frame_number, 0, fill_color);
    /* Do NOT reset the vertical border flip-flop here. On real hardware it is
       only toggled by the top/bottom RSEL compares, so a program that dodges the
       bottom compare (the classic "open the border" trick) keeps the border open
       across the frame boundary, which is what lets sprites multiplexed into the
       upper/lower border draw image data outside the normal display window.
       Forcing it closed every frame re-hid those border sprites. Power-on/reset
       still establishes the closed default via vicii_reset(). */

    /* The horizontal-border pixel pipeline holds at most the last two render
       cycles. Every visible line is fully flushed within two cycles (long before
       the last raster line), so nothing carries across the frame boundary; clear
       it anyway so a freshly begun frame starts empty. */
    v->hborder_pipe[0].n = 0;
    v->hborder_pipe[1].n = 0;
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
    v->vmli                      = 0;
    v->rc                        = 0;
    v->display_state             = false;
    v->bad_line                  = false;
    v->allow_bad_lines           = false;
    v->timing.raster_compare          = 0;
    v->timing.ba_low_until_abs        = 0;
    v->timing.sprite_ba_low_until_abs = 0;
    v->timing.aec_active              = true;
    v->timing.rdy_active              = true;
    v->timing.prefetch_cycles         = VICII_BA_LEAD_CYCLES + 1u;
    v->irq_status                = 0;
    v->irq_enable                = 0;
    memset(v->video_matrix, 0, sizeof(v->video_matrix));
    memset(v->color_line,   0, sizeof(v->color_line));
    memset(v->g_line,       0, sizeof(v->g_line));
    v->reg11_delay = 0;
    v->xscroll_pipe = 0;
    memset(v->sprite_mc,       0, sizeof(v->sprite_mc));
    memset(v->sprite_mcbase,   0, sizeof(v->sprite_mcbase));
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
    v->clear_collisions = 0;
    v->vertical_border_active = true;
    v->set_vborder            = true;
    v->main_border_ff         = true;
    /* Default on; runtime turbo policy re-applies after reset when needed. */
    v->pixel_output_enabled = true;
    v->color_pipe_d020 = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    v->color_pipe_d021 = (uint8_t)(v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu);
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
    uint8_t yscroll = v->registers[0x11] & 0x07u;

    /* Bad lines require the frame-level allow_bad_lines latch (DEN sampled on
       raster $30), not the live DEN bit. Live DEN still blanks graphics and
       gates vertical-border open at the top compare. */
    if (!v->allow_bad_lines) {
        return false;
    }
    if (y < VICII_BADLINE_FIRST || y > VICII_BADLINE_LAST) {
        return false;
    }
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
    /* Latched g-access bytes (live path). NULL → re-read RAM (snapshot path). */
    const uint8_t *g_latch;
    /* Phase 4: idle-vs-display selection. When true (live path) the idle state is
       driven by the sequencer: any line not in display state shows idle-state
       graphics, wherever it falls vertically. When false (snapshot/debug path)
       the legacy geometry is used: idle graphics outside the fixed display window
       and a B0C blank for inactive rows inside it. */
    bool idle_when_inactive;
} vicii_line_ctx;

static inline vicii_border_geometry vicii_get_border_geometry(const vicii *v) {
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

/* RSEL-only top/bottom compares — avoid full geometry on every cycle. */
static inline uint32_t vicii_vborder_top_line(const vicii *v) {
    return (v->registers[0x11] & 0x08u) != 0u
        ? (uint32_t)VICII_VBORDER_TOP_25
        : (uint32_t)VICII_VBORDER_TOP_24;
}

static inline uint32_t vicii_vborder_bottom_line(const vicii *v) {
    return (v->registers[0x11] & 0x08u) != 0u
        ? (uint32_t)VICII_VBORDER_BOTTOM_25
        : (uint32_t)VICII_VBORDER_BOTTOM_24;
}

/* Sprite X vs buffer X distance, with wrap across the end of the line.
   PAL is 63×8 = 504 dots (VICE VICII_PAL_SPRITE_WRAP_X); NTSC is 65×8 = 520.
   Using 512 (9-bit) was wrong: an X-expanded sprite at X=480 then only covered
   buffer columns 0..15 (dx 32..47) instead of the full 24-dot left border
   (dx 24..47 with wrap 504) — the incomplete left black column in lft-nine. */
static int vicii_sprite_dx_wrapped(const vicii *v, uint32_t frame_x, uint16_t spr_x) {
    int wrap = (int)v->timing.cycles_per_line * (int)VICII_CHARACTER_WIDTH;
    int dx = (int)frame_x - (int)(spr_x & 0x01FFu);
    if (dx < 0) {
        dx += wrap;
    }
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

#ifdef C64M_VIC_TRACE
static void vicii_trace_graphics_access(
    const vicii *v,
    const char *kind,
    uint32_t cycle,
    uint32_t col,
    uint16_t address,
    uint8_t value);
#endif

static vicii_line_ctx vicii_live_line_ctx(const vicii *v) {
    vicii_line_ctx c;

    /* The live renderer always follows the sequencer. DEN is sampled to arm bad
       lines and gate the top border, but clearing it later does not replace the
       running VC/RC state with a raster-derived screen address. */
    c.display_active     = v->display_state;
    c.cell_base          = v->vc_base;
    c.row_in_cell        = v->rc;
    c.vm_latch           = v->video_matrix;
    c.color_latch        = v->color_line;
    c.g_latch            = v->g_line;
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
    c.g_latch            = NULL;
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
    int n;

    memset(v->sprite_visible, 0, sizeof(v->sprite_visible));

    /* Renderer pre-latch of the three s-access data bytes, plus the per-line
       MC advance. This mirrors VICE's per-cycle sprite s-accesses collapsed to
       line start: MC was loaded from MCBASE at the display-check cycle of the
       previous line (see vicii_step_sprite_sequencer), so an expanded sprite --
       whose MCBASE
       is only latched every other line -- reads the same row twice. DMA
       lifetime, MCBASE and the Y-expand flip-flop are owned by the sequencer,
       not here. */
    for (n = 0; n < 8; n++) {
        if (!v->sprite_active[n]) {
            continue;
        }
        v->sprite_visible[n] = true;
        if (bus) {
            uint8_t  mc = v->sprite_mc[n];
            uint16_t vic_bank = c64_bus_vic_bank_base(bus);
            uint16_t screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0fu) * 0x0400u);
            uint16_t ptr_addr = (uint16_t)(vic_bank + screen_base + 0x03f8u + (uint16_t)n);
            uint16_t data_base;

            v->sprite_pointer[n] = c64_bus_vic_read_ram(bus, ptr_addr);
            data_base = (uint16_t)(vic_bank + (uint16_t)v->sprite_pointer[n] * 64u);
            v->sprite_data[n][0] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc));
            v->sprite_data[n][1] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc + 1u));
            v->sprite_data[n][2] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + mc + 2u));
        }
        v->sprite_mc[n] = (uint8_t)((v->sprite_mc[n] + 3u) & 0x3fu);
    }
}

#ifdef C64M_VIC_TRACE
/* Oracle companion to VICE_SPRDMA.  At the hardware's display check
   (Bauer cycle 58 / VICE raster_cycle 57), record the sequencer state that
   decides later sprite DMA/BA slots.  Deliberately compile- and runtime-gated
   like C64M_VICLOG: normal builds do not contain the file I/O path. */
static void vicii_trace_sprite_dma(const vicii *v) {
    static FILE *trace = NULL;
    static int init = 0;
    static unsigned long f0 = 0, f1 = 0xffffffffUL;
    uint8_t active = 0;
    int n;

    if (!init) {
        const char *path = getenv("C64M_SPRDMA");
        const char *first = getenv("C64M_VICLOG_F0");
        const char *last = getenv("C64M_VICLOG_F1");

        if (path) trace = fopen(path, "wb");
        if (first) f0 = strtoul(first, NULL, 10);
        if (last) f1 = strtoul(last, NULL, 10);
        init = 1;
    }
    if (!trace) return;

    for (n = 0; n < 8; ++n) {
        if (v->sprite_active[n]) active |= (uint8_t)(1u << n);
    }

    if (v->timing.frame_number >= f0 && v->timing.frame_number <= f1) {
        fprintf(trace, "F%llu R%u dma=%02X en=%02X y=",
                (unsigned long long)v->timing.frame_number,
                v->timing.raster_line,
                (unsigned)active,
                (unsigned)v->registers[VICII_REG_SPR_ENABLE]);
        for (n = 0; n < 8; ++n) {
            fprintf(trace, "%02X", (unsigned)v->registers[1 + n * 2]);
        }
        fputs(" mcbase=", trace);
        for (n = 0; n < 8; ++n) {
            fprintf(trace, "%02X", (unsigned)v->sprite_mcbase[n]);
        }
        fputc('\n', trace);
    }
}
#else
#define vicii_trace_sprite_dma(v) ((void)0)
#endif

/* Per-cycle sprite DMA/MCBASE sequencer, ported from VICE's viciisc model
   (vicii-cycle.c) so the cycle-exact sprite crunch is reproduced.
   Cycle numbers are 0-based like VICE's raster_cycle / VICII_PAL_CYCLE(n)
   (= Bauer/doc cycle n, minus one). NTSC shares the logic. The Y-expand
   flip-flop matches VICE: set to 1 when DMA turns on, toggled on the
   expansion-check cycle for expanded sprites, and forced to 1 by a $D017
   clear (see the crunch handler in vicii_write_register). MC advances +3/line
   in vicii_prepare_sprite_line. */
static void vicii_step_sprite_sequencer(vicii *v, uint32_t cycle) {
    uint8_t  enable   = v->registers[VICII_REG_SPR_ENABLE];
    uint8_t  y_expand = v->registers[VICII_REG_SPR_Y_EXPAND];
    uint8_t  raster8  = (uint8_t)(v->timing.raster_line & 0xffu);
    int n;

    /* Bauer cycle 16 / VICE index 15: MCBASE update. If the expansion
       flip-flop is set, latch MCBASE from MC and turn DMA off when MCBASE
       reaches 63. */
    if (cycle == 15u) {
        for (n = 0; n < 8; ++n) {
            if (v->sprite_y_exp_ff[n]) {
                v->sprite_mcbase[n] = v->sprite_mc[n];
                if (v->sprite_mcbase[n] == 63u) {
                    v->sprite_active[n] = false;
                }
            }
        }
    }

    /* Bauer cycles 55 & 56 / VICE indices 54 & 55: DMA-on check. Turn DMA on
       for an enabled sprite whose Y matches the current raster and whose DMA
       is still off; reset MCBASE and set the expansion flip-flop. */
    if (cycle == 54u || cycle == 55u) {
        for (n = 0; n < 8; ++n) {
            uint8_t spr_y = v->registers[1 + n * 2];
            if ((enable & (uint8_t)(1u << n)) != 0u &&
                spr_y == raster8 &&
                !v->sprite_active[n]) {
                v->sprite_active[n]   = true;
                v->sprite_mcbase[n]   = 0u;
                v->sprite_y_exp_ff[n] = true;
                /* Mark for a same-line presentation fetch (see
                   vicii_prime_sprite_row_after_dma_on). cycle-0 prepare ran
                   before DMA-on so the Y-match line would otherwise stay
                   invisible for the rest of this raster. */
                v->sprite_visible[n] = true;
                v->sprite_mc[n] = 0u;
            }
        }
    }

    /* Bauer cycle 56 / VICE index 55: toggle the expansion flip-flop for
       DMA-active expanded sprites (same index as the second DMA-on check). */
    if (cycle == 55u) {
        for (n = 0; n < 8; ++n) {
            if (v->sprite_active[n] && (y_expand & (uint8_t)(1u << n)) != 0u) {
                v->sprite_y_exp_ff[n] = !v->sprite_y_exp_ff[n];
            }
        }
    }

    /* Bauer cycle 58 / VICE index 57: load MC from MCBASE for the upcoming
       display row. */
    if (cycle == 57u) {
        for (n = 0; n < 8; ++n) {
            v->sprite_mc[n] = v->sprite_mcbase[n];
        }
        vicii_trace_sprite_dma(v);
    }
}

static inline vicii_bg_pixel vicii_bg_pixel_make(uint32_t color, bool foreground) {
    vicii_bg_pixel pixel;

    pixel.color = color;
    pixel.foreground = foreground;
    return pixel;
}

static inline vicii_sprite_pixel vicii_sprite_pixel_make(uint32_t color, bool opaque) {
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
    /* Geometry is line-latched; colours are live (VICE colour pipeline is
       near-live). Multiplex reassigns $D027+n as digits move — a line-start
       colour latch left the first rows of a new assignment on the previous
       digit's palette (wrong-colour top-border digits in lft-nine). */
    uint8_t color = (uint8_t)(v->registers[0x27u + (uint8_t)n] & 0x0Fu);
    uint8_t mm0 = (uint8_t)(v->registers[VICII_REG_SPR_MM0] & 0x0Fu);
    uint8_t mm1 = (uint8_t)(v->registers[VICII_REG_SPR_MM1] & 0x0Fu);

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
            return vicii_sprite_pixel_make(vicii_palette_argb[mm0], true);
        case 2u:
            return vicii_sprite_pixel_make(vicii_palette_argb[color], true);
        default:
            return vicii_sprite_pixel_make(vicii_palette_argb[mm1], true);
        }
    } else {
        int     bit_pos = x_expand ? (dx / 2) : dx;
        uint8_t bit     = (data[bit_pos / 8] >> (7 - bit_pos % 8)) & 1u;
        if (!bit) {
            return vicii_sprite_pixel_make(0, false);
        }
        return vicii_sprite_pixel_make(vicii_palette_argb[color], true);
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

/* Per-cycle paint constants for the live 8-dot span. Registers and bus bank are
   fixed for the duration of begin_cycle's paint (CPU Phi2 stores land after), so
   mode / ghost-byte / palette extras are computed once instead of per pixel. */
typedef struct vicii_paint_prep {
    vicii_line_ctx lc;
    uint8_t mode;
    uint8_t xscroll;
    uint32_t b1c;
    uint32_t b2c;
    uint32_t b3c;
    uint8_t idle_g;
    bool idle_ecm;
    bool idle_bmm;
    bool idle_mcm;
    bool idle_invalid;
    bool any_sprite;
} vicii_paint_prep;

/* Idle-state background pixel. Vertically outside the display window the VIC is
   in idle state: g-accesses read a fixed byte from $3FFF (or $39FF when ECM=1)
   and are displayed with the c-access data forced to 0. This is what shows
   through when a program opens the vertical border (the classic sprites-over-
   border title-screen technique), so the un-sprited gaps are idle graphics --
   usually black -- not the live background colour. Returning the live b0c here
   is what made c64m paint the opened border with the background colour instead
   of the near-black idle output VICE/hardware produce.

   ghost_g / mode flags are cycle-constant; callers that paint many pixels pass
   the pre-fetched ghost so each pixel only does the XSCROLL phase shift. */
static inline vicii_bg_pixel vicii_idle_pixel_decoded(
    const vicii *v,
    uint32_t x,
    uint8_t xscroll,
    uint8_t g,
    bool ecm,
    bool bmm,
    bool mcm,
    bool invalid_mode)
{
    /* Use 1-pixel-delayed B0C (color_pipe_d021) so mid-line $D021 splits in the
       opened border line up with XSCROLL the same way as VICE 6569. */
    uint32_t b0c   = vicii_palette_argb[v->color_pipe_d021 & 0x0fu];
    uint32_t black = vicii_palette_argb[0];
    /* Idle g-accesses emit the ghost byte every cycle; XSCROLL phase-shifts
       that repeating 8-dot pattern the same way it delays display-window
       graphics. lft-nine sets XSCROLL (often 1..7) specifically to line the
       ghostbyte up with the digit sprites — ignoring it left the device frame
       misaligned with the top-border digits. Unsigned wrap keeps x<24 valid. */
    uint32_t sx_raw = x - (uint32_t)VICII_HBORDER_LEFT_40;
    uint32_t phase = (sx_raw - (uint32_t)(xscroll & 0x07u));

    (void)ecm;

    /* Invalid modes (ECM combined with BMM and/or MCM) output black in every
       pixel slot — same as VICE COL_NONE. Forcing the colour black is what keeps
       EoD plasma's post-FLI $D011=$71 (ECM+BMM)+MCM idle bottom frame a solid
       black line instead of the ghost-byte MCM-bitmap stipple (pair 0 would
       otherwise paint B0C).

       The colour is forced black, but the graphics-derived foreground/priority
       bit is NOT: the VIC still clocks the MC flip-flop from the sequencer in
       invalid modes, so idle ghost-byte pixels still occlude behind-priority
       (MDP=1) sprites and still raise sprite/background collisions. Returning
       foreground=false here (the original fc5a58b fix) dropped that bit, which
       let dkarcade2016's venetian-reveal sprites leak through the still-black
       top/bottom border before their scanline had been uncovered. Foreground is
       pair-based for multicolor *bitmap* (MCM+BMM — the dkarcade case, matching
       the valid path below) and the hires bit otherwise (text idle stays hires
       because idle cbuf=0). */
    if (invalid_mode) {
        bool fg;
        if (mcm && bmm) {
            uint8_t pair = (uint8_t)((g >> (6u - (phase & 6u))) & 3u);
            fg = pair >= 2u;
        } else {
            uint8_t bit = (uint8_t)(0x80u >> (phase & 7u));
            fg = (g & bit) != 0u;
        }
        return vicii_bg_pixel_make(black, fg);
    }

    /* Idle forces c-access data to 0 (VICE vbuf/cbuf = 0). Multicolor *text*
       only uses 2-bit pixels when colour-RAM bit 3 is set; with cbuf=0 that
       never holds, so MCM text idle is hires with fg colour 0 — same as VICE
       draw_graphics when cbuf_reg==0 && !BMM. Treating MCM idle as always
       multicolor made the ghost byte render as 2-dot pairs (stippled open-
       border lines in Edge of Disgrace). Multicolor *bitmap* idle stays
       paired: vbuf=0 makes pairs 01/10/11 all colour 0 (black). */
    if (mcm && bmm) {
        uint8_t pair = (uint8_t)((g >> (6u - (phase & 6u))) & 3u);
        if (pair == 0u) {
            return vicii_bg_pixel_make(b0c, false);
        }
        /* 01/10/11 → black via zero vbuf/cbuf; pairs 10/11 are "fg" for coll. */
        return vicii_bg_pixel_make(black, pair >= 2u);
    }

    {
        uint8_t bit = (uint8_t)(0x80u >> (phase & 7u));
        if (bmm) {
            /* Standard bitmap idle: both nibbles of the zero c-data are black. */
            return vicii_bg_pixel_make(black, (g & bit) != 0u);
        }
        /* Standard/ECM text idle (including MCM=1): set bits → colour 0. */
        if (g & bit) {
            return vicii_bg_pixel_make(black, true);
        }
        return vicii_bg_pixel_make(b0c, false);
    }
}

/* Graphics output in the horizontal over-border region (outside the 40-column
   g-access window).  VICE viciisc forces the graphics shift register to zero
   there -- `vicii-draw-cycle.c`: `if (vis_en && vicii.vborder == 0)
   gbuf_pipe0_reg = vicii.gbuf; else gbuf_pipe0_reg = 0;` -- because no g-access
   loads the sequencer outside cycles 15..54.  `vicii_fetch_idle()` reads $3FFF
   for the bus/AEC but, unlike `vicii_fetch_idle_gfx()`, never assigns gbuf.

   With gbuf zero every pixel pair is 00, so VICE's `colors[]` table reduces to
   the pair-0 entry of the current mode.  vbuf/cbuf are *not* zeroed: they retain
   the last display column, which only matters for the two modes whose pair-0
   colour comes from vbuf.  Emitting the $3FFF ghost byte here instead paints its
   set bits in colour 0 -- the pure-black blocks in Edge of Disgrace's opened
   side border, where the demo's multicolor sprites expect flat B0C underneath. */
static inline vicii_bg_pixel vicii_border_gfx_pixel(
    const vicii_line_ctx *lc,
    uint32_t b0c,
    uint32_t b1c,
    uint32_t b2c,
    uint32_t b3c,
    bool ecm,
    bool bmm,
    bool mcm,
    bool invalid_mode)
{
    uint8_t vbuf;

    /* Invalid modes are COL_NONE in every pair slot. */
    if (invalid_mode) {
        return vicii_bg_pixel_make(vicii_palette_argb[0], false);
    }

    /* Retained c-data from the last display column (VICE keeps vbuf/cbuf). */
    vbuf = (lc != NULL && lc->vm_latch != NULL) ? lc->vm_latch[39] : 0u;

    if (bmm && !mcm) {
        /* ECM=0 BMM=1 MCM=0 → COL_VBUF_L. */
        return vicii_bg_pixel_make(vicii_palette_argb[vbuf & 0x0fu], false);
    }
    if (ecm && !bmm && !mcm) {
        /* ECM=1 BMM=0 MCM=0 → COL_D02X_EXT = D021 + (vbuf >> 6). */
        switch ((vbuf >> 6) & 3u) {
        case 1u:  return vicii_bg_pixel_make(b1c, false);
        case 2u:  return vicii_bg_pixel_make(b2c, false);
        case 3u:  return vicii_bg_pixel_make(b3c, false);
        default:  break;
        }
        return vicii_bg_pixel_make(b0c, false);
    }
    /* Remaining valid modes (hires/MCM text, MCM bitmap) → COL_D021. */
    return vicii_bg_pixel_make(b0c, false);
}

static vicii_bg_pixel vicii_idle_pixel(
    const vicii *v, const c64_bus_t *bus, uint32_t x, uint8_t xscroll)
{
    bool ecm = (v->registers[0x11] & 0x40u) != 0u;
    bool bmm = (v->registers[0x11] & 0x20u) != 0u;
    bool mcm = (v->registers[0x16] & 0x10u) != 0u;
    uint16_t vic_bank = c64_bus_vic_bank_base(bus);
    uint16_t addr = (uint16_t)(vic_bank + (ecm ? 0x39ffu : 0x3fffu));
    uint8_t g = c64_bus_vic_read_ram(bus, addr);

    return vicii_idle_pixel_decoded(
        v, x, xscroll, g, ecm, bmm, mcm, ecm && (bmm || mcm));
}

/* Decode one background pixel. prep may be NULL (snapshot path): mode/colors and
   idle ghost are derived from registers/bus. When prep is non-NULL (live path)
   those cycle-constant values are reused and the idle ghost is not re-fetched. */
static vicii_bg_pixel vicii_background_pixel_ex(
    const vicii *v,
    const c64_bus_t *bus,
    const vicii_border_geometry *g,
    const vicii_line_ctx *lc,
    const vicii_paint_prep *prep,
    uint32_t x,
    uint32_t y)
{
    uint32_t b0c = vicii_palette_argb[v->color_pipe_d021 & 0x0fu];
    uint32_t b1c;
    uint32_t b2c;
    uint32_t b3c;
    uint8_t mode;
    uint8_t xscroll;
    uint32_t sx_raw;
    uint32_t sx;
    uint32_t row_in_cell;
    uint32_t col;
    uint16_t cell;
    uint8_t vm_byte;
    uint8_t color_reg;
    uint8_t gdata;

    /* Border geometry is no longer needed here: the display span is the fixed
       40-column window and the CSEL-narrowed border is applied by the main
       flip-flop in the renderer, not by clamping the background value. */
    (void)g;

    if (prep != NULL) {
        mode = prep->mode;
        xscroll = prep->xscroll;
        b1c = prep->b1c;
        b2c = prep->b2c;
        b3c = prep->b3c;
    } else {
        /* Live path: VICE xscroll_pipe (sampled at end of g-access paint cycles).
           Snapshot path has no pipe history — use live $D016. */
        xscroll = (lc->g_latch != NULL)
            ? (uint8_t)(v->xscroll_pipe & 0x07u)
            : (uint8_t)(v->registers[0x16] & 0x07u);
        mode = (uint8_t)(((v->registers[0x11] & 0x40u) ? 4u : 0u) |
                         ((v->registers[0x11] & 0x20u) ? 2u : 0u) |
                         ((v->registers[0x16] & 0x10u) ? 1u : 0u));
        b1c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_1] & 0x0fu];
        b2c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_2] & 0x0fu];
        b3c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_3] & 0x0fu];
    }

    if (x < (uint32_t)VICII_HBORDER_LEFT_40 || x >= (uint32_t)VICII_HBORDER_RIGHT_40) {
        /* Horizontal over-border region: outside the fixed 40-column display span
           [24,344), independent of CSEL. No g-access loads the sequencer here, so
           the graphics data is zero (VICE gbuf_pipe0_reg) and every pair is 00.
           This is what an opened side border reveals; while the main border
           flip-flop is closed vicii_compose_pixel overrides it, so ordinary
           screens are unaffected. See vicii_border_gfx_pixel. */
        uint32_t bd0c = vicii_palette_argb[v->color_pipe_d021 & 0x0fu];
        if (prep != NULL) {
            return vicii_border_gfx_pixel(
                lc, bd0c, b1c, b2c, b3c,
                prep->idle_ecm, prep->idle_bmm, prep->idle_mcm, prep->idle_invalid);
        }
        {
            bool becm = (v->registers[0x11] & 0x40u) != 0u;
            bool bbmm = (v->registers[0x11] & 0x20u) != 0u;
            bool bmcm = (v->registers[0x16] & 0x10u) != 0u;
            return vicii_border_gfx_pixel(
                lc, bd0c, b1c, b2c, b3c,
                becm, bbmm, bmcm, becm && (bbmm || bmcm));
        }
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
            if (prep != NULL) {
                return vicii_idle_pixel_decoded(
                    v, x, xscroll, prep->idle_g,
                    prep->idle_ecm, prep->idle_bmm, prep->idle_mcm, prep->idle_invalid);
            }
            return vicii_idle_pixel(v, bus, x, xscroll);
        }
    } else if (y < (uint32_t)VICII_VBORDER_TOP_25 || y >= (uint32_t)VICII_VBORDER_BOTTOM_25) {
        /* Snapshot/debug path: idle-state graphics outside the fixed 25-row
           window. Inactive rows inside the window fall through to the B0C blank
           below, preserving the legacy geometric reconstruction. */
        return vicii_idle_pixel(v, bus, x, xscroll);
    }

    sx_raw = x - (uint32_t)VICII_HBORDER_LEFT_40;

    if (mode >= 5u) {
        return vicii_bg_pixel_make(vicii_palette_argb[0], false);
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

    /* Character code and colour come from the per-cycle c-access latches on the
       live path or live RAM on the snapshot path. g-access bytes come from
       g_line (latched at Phi1 with reg11_delay addressing). Live path always
       has all three latches, so skip bank/base address math entirely. */
    if (lc->vm_latch != NULL && lc->color_latch != NULL && lc->g_latch != NULL) {
        vm_byte = lc->vm_latch[col];
        color_reg = lc->color_latch[col];
        gdata = lc->g_latch[col];
    } else {
        uint16_t vic_bank = c64_bus_vic_bank_base(bus);
        uint16_t screen_base =
            (uint16_t)(vic_bank + (v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
        uint16_t char_base =
            (uint16_t)(vic_bank + ((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);
        uint16_t bitmap_base =
            (uint16_t)(vic_bank + ((v->registers[VICII_REG_MEMORY_POINTER] >> 3) & 1u) * 0x2000u);

        vm_byte = lc->vm_latch
            ? lc->vm_latch[col]
            : c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
        color_reg = lc->color_latch
            ? lc->color_latch[col]
            : c64_bus_vic_read_color(bus, cell);
        if (lc->g_latch) {
            gdata = lc->g_latch[col];
        } else if (mode == 2u || mode == 3u) {
            uint16_t baddr = (uint16_t)(bitmap_base + (uint32_t)cell * 8u + row_in_cell);
            gdata = c64_bus_vic_read_ram(bus, baddr);
        } else {
            uint8_t code = (mode == 4u) ? (uint8_t)(vm_byte & 0x3fu) : vm_byte;
            gdata = c64_bus_vic_read_char_glyph_at(bus, char_base, code, (uint8_t)row_in_cell);
        }
    }

    switch (mode) {
    case 0u:
        {
            uint8_t glyph = gdata;
            uint8_t fg = color_reg;
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            if (glyph & bit) {
                return vicii_bg_pixel_make(vicii_palette_argb[fg & 0x0fu], true);
            }
            return vicii_bg_pixel_make(b0c, false);
        }

    case 1u:
        {
            uint8_t color_nib = color_reg;
            uint8_t glyph = gdata;

            if ((color_nib & 0x08u) == 0u) {
                uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
                if (glyph & bit) {
                    return vicii_bg_pixel_make(vicii_palette_argb[color_nib & 0x0fu], true);
                }
                return vicii_bg_pixel_make(b0c, false);
            } else {
                uint8_t pair = (uint8_t)((glyph >> (6u - (sx & 6u))) & 3u);
                switch (pair) {
                case 0u:
                    return vicii_bg_pixel_make(b0c, false);
                case 1u:
                    return vicii_bg_pixel_make(b1c, true);
                case 2u:
                    return vicii_bg_pixel_make(b2c, true);
                default:
                    return vicii_bg_pixel_make(vicii_palette_argb[color_nib & 0x07u], true);
                }
            }
        }

    case 2u:
        {
            uint8_t bdata = gdata;
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            if (bdata & bit) {
                return vicii_bg_pixel_make(vicii_palette_argb[(vm_byte >> 4) & 0x0fu], true);
            }
            return vicii_bg_pixel_make(vicii_palette_argb[vm_byte & 0x0fu], false);
        }

    case 3u:
        {
            uint8_t color_nib = color_reg;
            uint8_t bdata = gdata;
            uint8_t pair = (uint8_t)((bdata >> (6u - (sx & 6u))) & 3u);
            switch (pair) {
            case 0u:
                return vicii_bg_pixel_make(b0c, false);
            case 1u:
                return vicii_bg_pixel_make(vicii_palette_argb[(vm_byte >> 4) & 0x0fu], true);
            case 2u:
                return vicii_bg_pixel_make(vicii_palette_argb[vm_byte & 0x0fu], true);
            default:
                return vicii_bg_pixel_make(vicii_palette_argb[color_nib & 0x0fu], true);
            }
        }

    case 4u:
        {
            uint8_t code = vm_byte;
            uint8_t ecm_sel = (code >> 6) & 3u;
            uint8_t glyph = gdata;
            uint8_t fg_nib = color_reg;
            uint8_t bit = (uint8_t)(0x80u >> (sx & 7u));
            uint32_t ecm_bg;

            if (glyph & bit) {
                return vicii_bg_pixel_make(vicii_palette_argb[fg_nib & 0x0fu], true);
            }

            switch (ecm_sel) {
            case 0u:  ecm_bg = b0c; break;
            case 1u:  ecm_bg = b1c; break;
            case 2u:  ecm_bg = b2c; break;
            default:  ecm_bg = b3c; break;
            }
            return vicii_bg_pixel_make(ecm_bg, false);
        }

    default:
        return vicii_bg_pixel_make(vicii_palette_argb[0], false);
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
    return vicii_background_pixel_ex(v, bus, g, lc, NULL, x, y);
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

    /* Bauer / VICE: collision IRQs edge-trigger only when the latch goes from
       zero to non-zero. Acking $D019 while $D01E/$D01F stays latched must not
       re-assert IRQ on later overlap pixels (Potty Pigeon title: $D01A=$05
       enables IMMC, IRQ path acks only $01; sticky re-assert starved the main
       loop so music and animation froze while the multi-raster colour bars
       still ran). */
    if ((mask & (uint8_t)(mask - 1u)) != 0) {
        uint8_t prev = v->sprite_sprite_collision;
        v->sprite_sprite_collision |= mask;
        if (prev == 0u && v->sprite_sprite_collision != 0u) {
            vicii_set_irq_flag(v, VICII_IRQ_IMMC);
        }
    }

    if (bg.foreground && mask != 0) {
        uint8_t prev = v->sprite_background_collision;
        v->sprite_background_collision |= mask;
        if (prev == 0u && v->sprite_background_collision != 0u) {
            vicii_set_irq_flag(v, VICII_IRQ_IMBC);
        }
    }
}

/* Compose one pixel. Bauer 3.9: the *main* border flip-flop alone decides
   whether $D020 covers everything (including sprites). The vertical border
   flip-flop only (a) prevents the main FF from clearing at the left compare
   and (b) forces the graphics sequencer to output background colour — it does
   *not* blank sprites. Open upper/lower border therefore shows sprites over
   the (graphics) background.

   DEN controls bad-line eligibility and the top vertical-border compare; it
   does not directly blank an already-open graphics pipeline. */
static uint32_t vicii_compose_pixel(
    vicii *v,
    bool main_border,
    bool vertical_border,
    uint32_t border_color,
    vicii_bg_pixel bg,
    const vicii_sprite_pixel sprites[8])
{
    int n;
    vicii_note_sprite_collisions(v, bg, sprites);

    if (main_border) {
        return border_color;
    }

    /* Vertical border forces graphics to B0C; sprites still mux. */
    if (vertical_border) {
        bg.color = vicii_palette_argb[v->color_pipe_d021 & 0x0fu];
        bg.foreground = false;
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

static uint32_t vicii_live_pixel(
    vicii *v,
    const c64_bus_t *bus,
    const vicii_border_geometry *g,
    const vicii_paint_prep *prep,
    uint32_t x,
    uint32_t y,
    bool main_border,
    bool vertical_border)
{
    uint32_t border_color = vicii_palette_argb[v->color_pipe_d020 & 0x0fu];
    uint32_t b0c = vicii_palette_argb[v->color_pipe_d021 & 0x0fu];
    vicii_bg_pixel bg;
    vicii_sprite_pixel sprites[8];
    int n;

    /* Paint from the line-latched row (sprite_visible), not from $D015.
       VICE's sprite_display_bits is sticky once set at the Y-match/display
       check while DMA is on; clearing $D015 only prevents a new DMA start,
       it does not blank an already-active sprite for the rest of its life.
       Re-gating paint on latched enable each line was clipping lft-nine's
       bottom digits after $D015←0 @ R251 while DMA still had rows left.

       any_sprite is cycle-constant (set once in the paint prep). When no
       sprites are visible, skip background decode for vertical-border spans
       (content is forced to B0C) and avoid the compose path entirely. */
    if (!prep->any_sprite) {
        if (main_border) {
            return border_color;
        }
        if (vertical_border) {
            return b0c;
        }
        return vicii_background_pixel_ex(v, bus, g, &prep->lc, prep, x, y).color;
    }

    bg = vicii_background_pixel_ex(v, bus, g, &prep->lc, prep, x, y);

    for (n = 0; n < 8; n++) {
        sprites[n] = vicii_sprite_pixel_make(0, false);
        if (!v->sprite_visible[n]) continue;
        {
            /* X is near-live (VICE pipes it by one cycle). Line-latching X left
               multiplexed digits one line behind their new position when the
               demo rewrote $D000+ mid-lifetime. Expand/MC mode stay latched. */
            uint16_t spr_x = (uint16_t)(v->registers[(uint8_t)(n * 2)] |
                (uint16_t)(((v->registers[VICII_REG_SPR_X_MSB] >> n) & 1u) << 8));
            int dx = vicii_sprite_dx_wrapped(v, x, spr_x);
            sprites[n] = vicii_sprite_pixel_from_latched_data(v, v->sprite_data[n], n, dx);
        }
    }

    return vicii_compose_pixel(v, main_border, vertical_border, border_color, bg, sprites);
}

/* VICE check_vborder_top: top compare + DEN clears both the latch and the
   live vertical border flag immediately (not deferred to cycle 0). */
static void vicii_check_vborder_top(vicii *v) {
    bool den = (v->registers[0x11] & 0x10u) != 0u;

    if (v->timing.raster_line == vicii_vborder_top_line(v) && den) {
        v->vertical_border_active = false;
        v->set_vborder            = false;
    }
}

/* VICE check_vborder_bottom: bottom compare only *sets* the latch. It never
   clears it — only top+DEN does. That is what makes the RSEL lower-border open
   stick once the 24-row bottom has been missed. */
static void vicii_check_vborder_bottom(vicii *v) {
    if (v->timing.raster_line == vicii_vborder_bottom_line(v)) {
        v->set_vborder = true;
    }
}

/* Apply latched set_vborder → vertical_border_active (VICE: cycle 1 = our 0,
   and again at the left border compare). */
static void vicii_apply_vborder_latch(vicii *v) {
    v->vertical_border_active = v->set_vborder;
}

/* Flush the oldest buffered render span (painted two cycles ago) into the frame,
   choosing border colour vs. content per pixel from the main border flip-flop as
   it stands *now* — this is the 2-cycle border-decision delay that lets a timed
   $D016 CSEL write dodge the right-border compare while keeping normal edges. */
static void vicii_hborder_flush(vicii *v, bool main_border) {
    uint8_t i;
    for (i = 0; i < v->hborder_pipe[0].n; i++) {
        v->working_frame.pixels[v->hborder_pipe[0].idx[i]] =
            main_border ? v->hborder_pipe[0].border[i]
                        : v->hborder_pipe[0].content[i];
    }
}

static void vicii_render_live_cycle(vicii *v, const c64_bus_t *bus) {
    vicii_border_geometry g;
    vicii_paint_prep prep;
    uint32_t y;
    uint32_t x;
    uint32_t cyc;
    bool     check_csel;
    bool     check_prev_csel;
    int      n;
    uint16_t idle_bank;
    uint16_t idle_addr;

    if (!bus) {
        return;
    }

    y = v->timing.raster_line;
    if (y >= v->working_frame.height) {
        return;
    }

    /* Geometry is unused by background decode (border is the main flip-flop);
       only kept for the shared API with the snapshot path. */
    g = vicii_get_border_geometry(v);
    prep.lc = vicii_live_line_ctx(v);
    prep.xscroll = (uint8_t)(v->xscroll_pipe & 0x07u);
    prep.mode = (uint8_t)(((v->registers[0x11] & 0x40u) ? 4u : 0u) |
                          ((v->registers[0x11] & 0x20u) ? 2u : 0u) |
                          ((v->registers[0x16] & 0x10u) ? 1u : 0u));
    prep.b1c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_1] & 0x0fu];
    prep.b2c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_2] & 0x0fu];
    prep.b3c = vicii_palette_argb[v->registers[VICII_REG_BACKGROUND_COLOR_3] & 0x0fu];
    prep.idle_ecm = (v->registers[0x11] & 0x40u) != 0u;
    prep.idle_bmm = (v->registers[0x11] & 0x20u) != 0u;
    prep.idle_mcm = (v->registers[0x16] & 0x10u) != 0u;
    prep.idle_invalid = prep.idle_ecm && (prep.idle_bmm || prep.idle_mcm);
    idle_bank = c64_bus_vic_bank_base(bus);
    idle_addr = (uint16_t)(idle_bank + (prep.idle_ecm ? 0x39ffu : 0x3fffu));
    prep.idle_g = c64_bus_vic_read_ram(bus, idle_addr);
    prep.any_sprite = false;
    for (n = 0; n < 8; n++) {
        if (v->sprite_visible[n]) {
            prep.any_sprite = true;
            break;
        }
    }

    cyc = v->timing.cycle_in_line;

    /* CSEL used for this cycle's border check is the value latched at the end of
       the previous cycle. c64m applies the CPU store before the VIC step, so this
       reproduces VICE's check_hborder, which samples CSEL *before* the store. */
    check_csel = v->hborder_prev_csel;
    check_prev_csel = v->hborder_prev2_csel;

    /* VICE viciisc check_hborder (PAL/NTSC identical cycles). Right compare sets
       the main flip-flop at cycle 57 (csel=1) or 56 (csel=0); left compare at
       cycle 17 (csel=1) or 18 (csel=0) applies the vertical-border latch and
       clears main when the vertical border is inactive. */
    /* c64m's CPU/VIC projection places the VICE cycle-56 CSEL fall at cycle 56
       in EoD but at cycle 55 in lft-nine's CIA-synchronised stable raster.  In
       the latter case CSEL has been low for one render cycle when the CSEL=0
       compare is reached, while it was still high at the preceding sample.
       Preserve that in-flight transition rather than mistaking it for a stable
       38-column compare.  A CSEL=0 value stable for two samples still closes at
       cycle 56, and the normal CSEL=1 compare remains cycle 57. */
    if (cyc == (check_csel ? 57u : 56u) &&
        !(!check_csel && check_prev_csel)) {
        v->main_border_ff = true;
    }
    if (cyc == (check_csel ? 17u : 18u)) {
        vicii_check_vborder_bottom(v);
        vicii_check_vborder_top(v);
        vicii_apply_vborder_latch(v);
        if (!v->vertical_border_active) {
            v->main_border_ff = false;
        }
    }

    /* Apply the (now-updated) flip-flop to the span painted two cycles ago, then
       shift the one-cycle-old span into the flush slot. */
    vicii_hborder_flush(v, v->main_border_ff);
    v->hborder_pipe[0] = v->hborder_pipe[1];

    /* Anchored dot mapping (C64MVICII_SIDEBORDER.md §2.2): each cycle owns its
       true 8 VIC dots, so buffer_x == VIC X-coordinate and the paint cycle for
       every column matches hardware. Display column 0 (X=24) is drawn at the
       first g-access cycle (15); each cycle advances X by 8. Dots outside the
       384-px crop (line start/end H-blank) are not written to the frame, but
       still advance the 6569 color pipes — VICE's draw_colors ring runs for
       every cycle, including HBLANK. Advancing only on painted pixels left a
       1px $D020/$D021 delay stuck across the line edge (EoD top/bottom black
       bar: x=0 still previous border colour). Every column 0..383 is painted
       exactly once per line (PAL cycles 12..59, NTSC 12..59). */
    {
        int32_t raw_xs = (int32_t)VICII_HBORDER_LEFT_40 +
            ((int32_t)cyc - (int32_t)VICII_GACCESS_FIRST_CYCLE) *
            (int32_t)VICII_CHARACTER_WIDTH;
        int      i;

        /* Compute this cycle's content pixels (border decision applied later, at
           flush) and buffer them. Content is the composed graphics/sprites/
           vertical-border pixel with main_border=false; the border colour is
           captured at paint time so its own colour_latency is unchanged. */
        v->hborder_pipe[1].n = 0;
        for (i = 0; i < (int)VICII_CHARACTER_WIDTH; i++) {
            int32_t x_i = raw_xs + i;

            if (x_i >= 0 && x_i < (int32_t)C64_FRAME_WIDTH) {
                uint8_t k = v->hborder_pipe[1].n;
                x = (uint32_t)x_i;
                v->hborder_pipe[1].idx[k]     = y * C64_FRAME_WIDTH + x;
                v->hborder_pipe[1].content[k] =
                    vicii_live_pixel(v, bus, &g, &prep, x, y, false,
                        v->vertical_border_active);
#ifdef C64M_VIC_TRACE
                if ((x == 24u || x == 48u || x == 144u || x == 240u || x == 336u) &&
                    y >= 40u && y <= 250u) {
                    uint32_t pixel = v->hborder_pipe[1].content[k];
                    unsigned palette = 16u;
                    for (unsigned pi = 0; pi < 16u; ++pi) {
                        if (pixel == vicii_palette_argb[pi]) {
                            palette = pi;
                            break;
                        }
                    }
                    vicii_trace_graphics_access(v, "p", cyc, x, 0,
                        (uint8_t)palette);
                }
#endif
                v->hborder_pipe[1].border[k] =
                    vicii_palette_argb[v->color_pipe_d020 & 0x0fu];
                v->hborder_pipe[1].n = (uint8_t)(k + 1u);
            }
            /* VICE 6569: advance color pipes after each dot (visible or not) so
               a mid-line $D020/$D021 write is visible one pixel later, and a
               write during HBLANK is fully drained before x=0. */
            v->color_pipe_d020 =
                (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
            v->color_pipe_d021 =
                (uint8_t)(v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu);
        }
    }

    /* Latch CSEL for the next cycle's pre-store border check. */
    v->hborder_prev2_csel = v->hborder_prev_csel;
    v->hborder_prev_csel = (v->registers[0x16] & 0x08u) != 0;

#ifdef C64M_VIC_TRACE
    {
        static FILE *brdlog = NULL; static int brd_init = 0;
        static unsigned long bf0 = 0, bf1 = 0; static unsigned r0 = 0, r1 = 0;
        if (!brd_init) {
            const char *p = getenv("C64M_BRDLOG");
            const char *a = getenv("C64M_BRDLOG_F0"); const char *b = getenv("C64M_BRDLOG_F1");
            const char *ra = getenv("C64M_BRDLOG_R0"); const char *rb = getenv("C64M_BRDLOG_R1");
            if (p) brdlog = fopen(p, "wb");
            if (a) bf0 = strtoul(a, NULL, 10); if (b) bf1 = strtoul(b, NULL, 10);
            if (ra) r0 = (unsigned)strtoul(ra, NULL, 10); if (rb) r1 = (unsigned)strtoul(rb, NULL, 10);
            brd_init = 1;
        }
        if (brdlog) {
            unsigned long fr = (unsigned long)v->timing.frame_number;
            if (fr >= bf0 && fr <= bf1 && y >= r0 && y <= r1) {
                uint32_t paint_x0 = (v->hborder_pipe[1].n > 0u)
                    ? (v->hborder_pipe[1].idx[0] % C64_FRAME_WIDTH)
                    : 0u;
                fprintf(brdlog, "F%lu R%u C%u x0=%u csel=%d chk=%d chk2=%d "
                        "mbff=%d vb=%d svb=%d d11=%02x d16=%02x\n",
                        fr, y, cyc, paint_x0, (v->registers[0x16] & 8) ? 1 : 0,
                        check_csel ? 1 : 0, check_prev_csel ? 1 : 0,
                        v->main_border_ff ? 1 : 0,
                        v->vertical_border_active ? 1 : 0,
                        v->set_vborder ? 1 : 0,
                        (unsigned)v->registers[0x11],
                        (unsigned)v->registers[0x16]);
            }
        }
    }
#endif
}


static void vicii_assert_raster_irq(vicii *v) {
    vicii_set_irq_flag(v, VICII_IRQ_RASTER);
}

/* Bus arbitration follows the live DMA flag only (VICE sprite_dma), not the
   renderer latch and not a pure Y-match. DMA turns on at the cycle-54/55
   sequencer checks; from that moment sprite_active is true so the usual
   3-cycle BA lead still covers the first p/s slots. A Y-match-before-DMA
   predicate was stealing Phi2 on the activation line earlier than VICE (which
   only sets sprite_dma at the DMA-on checks). */
static bool vicii_sprite_dma_current_line(const vicii *v, int n) {
    return v->sprite_active[n];
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

static void vicii_prepare_phi1_bus_access(vicii *v, uint32_t cycle) {
    int sprite;
    bool second_cycle;

    /* Phi1 is sampled before this cycle's Phi2 sequencer transitions. */
    v->timing.bus_access_phi1 = v->display_state ?
        VICII_BUS_ACCESS_G : VICII_BUS_ACCESS_IDLE;

    if (vicii_sprite_slot(v, cycle, &sprite, &second_cycle)) {
        v->timing.bus_access_phi1 = second_cycle ?
            VICII_BUS_ACCESS_SPRITE_DATA : VICII_BUS_ACCESS_SPRITE_POINTER;
    }
}

static void vicii_schedule_phi2_bus_access(vicii *v, uint32_t cycle) {
    int sprite;
    bool second_cycle;

    /* Phi2 is CPU-visible only for bad-line c-accesses and sprite data. */
    v->timing.bus_access = VICII_BUS_ACCESS_NONE;

    if (vicii_sprite_slot(v, cycle, &sprite, &second_cycle)) {
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

static bool vicii_update_ba(vicii *v, uint32_t cycle, uint64_t abs_cycle) {
    const uint8_t *table = v->timing.standard == VICII_VIDEO_STANDARD_PAL ?
        vicii_pal_sprite_ba_mask : vicii_ntsc_sprite_ba_mask;
    uint8_t active = 0;
    bool pending_ba = abs_cycle < v->timing.ba_low_until_abs;
    bool sprite_ba;
    bool matrix_ba;
    int n;

    for (n = 0; n < 8; ++n) {
        if (v->sprite_active[n]) active |= (uint8_t)(1u << n);
    }
    sprite_ba = (active & table[cycle]) != 0u;
    matrix_ba = v->bad_line && cycle >= 11u &&
        cycle <= VICII_CACCESS_LAST_CYCLE;

    /* Keep the absolute endpoints for debugger/snapshot compatibility, but
       extend them by one current cycle only.  VICE recomputes BA from the live
       badline and sprite masks every cycle; persisting a cancelled badline to
       cycle 53 is observably wrong for timed $D011 loops. */
    if (sprite_ba && abs_cycle + 1u > v->timing.sprite_ba_low_until_abs) {
        v->timing.sprite_ba_low_until_abs = abs_cycle + 1u;
    }
    if ((sprite_ba || matrix_ba) &&
        abs_cycle + 1u > v->timing.ba_low_until_abs) {
        v->timing.ba_low_until_abs = abs_cycle + 1u;
    }
    return pending_ba || sprite_ba || matrix_ba;
}

#ifdef C64M_VIC_TRACE
static void vicii_trace_graphics_access(
    const vicii *v,
    const char *kind,
    uint32_t cycle,
    uint32_t col,
    uint16_t address,
    uint8_t value)
{
    static FILE *gfxlog = NULL;
    static int init = 0;
    static unsigned long f0 = 0, f1 = 0xffffffffUL;
    static unsigned r0 = 0, r1 = 0xffffffffU;
    unsigned long frame;
    unsigned raster;

    if (!init) {
        const char *path = getenv("C64M_GFXLOG");
        const char *first = getenv("C64M_VICLOG_F0");
        const char *last = getenv("C64M_VICLOG_F1");
        const char *raster_first = getenv("C64M_GFXLOG_R0");
        const char *raster_last = getenv("C64M_GFXLOG_R1");

        if (path) gfxlog = fopen(path, "wb");
        if (first) f0 = strtoul(first, NULL, 10);
        if (last) f1 = strtoul(last, NULL, 10);
        if (raster_first) r0 = (unsigned)strtoul(raster_first, NULL, 10);
        if (raster_last) r1 = (unsigned)strtoul(raster_last, NULL, 10);
        init = 1;
    }
    if (!gfxlog) return;

    frame = (unsigned long)v->timing.frame_number;
    raster = (unsigned)v->timing.raster_line;
    if (frame < f0 || frame > f1 || raster < r0 || raster > r1) return;

    fprintf(gfxlog,
        "F%lu R%u C%u %s%u a=%04X v=%02X d011=%02X d016=%02X d018=%02X "
        "disp=%u bad=%u vb=%u svb=%u mb=%u vc=%03X vcb=%03X rc=%u "
        "vmli=%u vm=%02X cl=%02X pf=%u\n",
        frame, raster, cycle, kind, (unsigned)col, (unsigned)address,
        (unsigned)value,
        (unsigned)v->registers[VICII_REG_CONTROL_1],
        (unsigned)v->registers[0x16],
        (unsigned)v->registers[VICII_REG_MEMORY_POINTER],
        v->display_state ? 1u : 0u, v->bad_line ? 1u : 0u,
        v->vertical_border_active ? 1u : 0u, v->set_vborder ? 1u : 0u,
        v->main_border_ff ? 1u : 0u,
        (unsigned)v->vc, (unsigned)v->vc_base, (unsigned)v->rc,
        (unsigned)v->vmli,
        col < VICII_TEXT_COLUMNS ? (unsigned)v->video_matrix[col] : 0xffu,
        col < VICII_TEXT_COLUMNS ? (unsigned)v->color_line[col] : 0xffu,
        (unsigned)v->timing.prefetch_cycles);
}
#else
#define vicii_trace_graphics_access(v, kind, cycle, col, address, value) ((void)0)
#endif

static void vicii_fetch_g_or_idle_access(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    uint16_t vic_bank;
    uint16_t address;
    uint32_t col;
    /* VICE color_latency: g-fetch address uses prior-cycle $D011 for BMM/ECM. */
    uint8_t reg11 = v->reg11_delay;
    bool bmm = (reg11 & 0x20u) != 0u;
    bool ecm = (reg11 & 0x40u) != 0u;

    if (!v->display_state || cycle < VICII_GACCESS_FIRST_CYCLE ||
        cycle > VICII_GACCESS_LAST_CYCLE) {
        if (bus) {
            vic_bank = c64_bus_vic_bank_base(bus);
            address = (uint16_t)(vic_bank + (ecm ? 0x39ffu : 0x3fffu));
            uint8_t value = c64_bus_vic_read_ram(bus, address);
            if (cycle == VICII_GACCESS_FIRST_CYCLE ||
                cycle == VICII_GACCESS_LAST_CYCLE) {
                vicii_trace_graphics_access(v, "i", cycle, 0, address, value);
            }
        }
        return;
    }

    col = cycle - VICII_GACCESS_FIRST_CYCLE;
    if (col >= (uint32_t)VICII_TEXT_COLUMNS) {
        return;
    }
    if (bus) {
        vic_bank = c64_bus_vic_bank_base(bus);
        if (bmm) {
            uint16_t bitmap_base = (uint16_t)(vic_bank +
                ((v->registers[VICII_REG_MEMORY_POINTER] >> 3) & 1u) * 0x2000u);
            address = (uint16_t)(bitmap_base + ((v->vc & 0x03ffu) << 3) + v->rc);
            v->g_line[col] = c64_bus_vic_read_ram(bus, address);
        } else {
            uint16_t char_base = (uint16_t)(vic_bank +
                ((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);
            uint8_t code = v->video_matrix[v->vmli];
            if (ecm) {
                code &= 0x3fu;
            }
            address = (uint16_t)(char_base + (uint16_t)code * 8u + v->rc);
            v->g_line[col] = c64_bus_vic_read_char_glyph_at(bus, char_base, code, v->rc);
        }
        if (col == 0u || col == 1u || col == 2u || col == 20u || col == 39u) {
            vicii_trace_graphics_access(v, "g", cycle, col, address, v->g_line[col]);
        }
    }

    /* VICE/Bauer: every display-state g-access advances the 10-bit video
       counter and matrix-line index. The following Phi2 c-access therefore
       fills the next vbuf/cbuf slot, except at cycle 14 where slot zero is
       fetched before the first g-access. */
    v->vc = (uint16_t)((v->vc + 1u) & (uint16_t)VICII_VC_MAX);
    if (v->vmli < (uint8_t)VICII_TEXT_COLUMNS) {
        v->vmli++;
    }
}

static void vicii_fetch_c_access(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    uint16_t vic_bank;
    uint16_t screen_base;

    if (!bus || !v->bad_line || cycle < VICII_CACCESS_FIRST_CYCLE ||
        cycle > VICII_CACCESS_LAST_CYCLE ||
        v->vmli >= (uint8_t)VICII_TEXT_COLUMNS) {
        return;
    }

    vic_bank = c64_bus_vic_bank_base(bus);
    screen_base = (uint16_t)(vic_bank +
        (v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    v->video_matrix[v->vmli] = c64_bus_vic_read_ram(
        bus, (uint16_t)(screen_base + v->vc));
    v->color_line[v->vmli] = c64_bus_vic_read_color(bus, v->vc);
    if (v->vmli == 0u || v->vmli == 1u || v->vmli == 2u ||
        v->vmli == 20u || v->vmli == 39u) {
        vicii_trace_graphics_access(v, "c", cycle, v->vmli,
            (uint16_t)(screen_base + v->vc), v->video_matrix[v->vmli]);
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

static void vicii_execute_phi1_fetch(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    if (v->timing.bus_access_phi1 == VICII_BUS_ACCESS_SPRITE_POINTER ||
        v->timing.bus_access_phi1 == VICII_BUS_ACCESS_SPRITE_DATA) {
        vicii_fetch_sprite_slot(v, bus, cycle);
        return;
    }
    vicii_fetch_g_or_idle_access(v, bus, cycle);
}

static void vicii_execute_phi2_fetch(vicii *v, const c64_bus_t *bus, uint32_t cycle) {
    if (v->timing.bus_access != VICII_BUS_ACCESS_C) {
        return;
    }
    if (v->timing.prefetch_cycles != 0u) {
        /* Until BA's three-cycle lead has elapsed, AEC is still high. VICE
           vicii_fetch_matrix records vbuf=$ff and cbuf = ram[PC] & 0x0f (the
           Phi2 open-bus byte the 6510 is driving). A fixed $0f made EoD's late
           FLI badlines paint light-gray stripes in the left margin where VICE
           shows open-bus blue matching the intentional colour RAM. */
        if (v->vmli < (uint8_t)VICII_TEXT_COLUMNS) {
            uint8_t open_cbuf = 0x0fu;
            if (bus != NULL) {
                open_cbuf = (uint8_t)(bus->ram[bus->cpu_open_bus_pc] & 0x0fu);
            }
            v->video_matrix[v->vmli] = 0xffu;
            v->color_line[v->vmli] = open_cbuf;
        }
        return;
    }
    vicii_fetch_c_access(v, bus, cycle);
}

void vicii_begin_cycle(vicii *v, const c64_bus_t *bus, uint64_t abs_cycle) {
    uint32_t cycle;
    bool ba_low;

    assert(v);

    cycle = v->timing.cycle_in_line;

    /* VICE's cycle order is Phi1 fetch, horizontal-border/draw work, then the
       internal Phi2 sequencer and c-access. In particular, drawing must see the
       matrix/color buffer as it existed before this cycle's c-access. */
    vicii_prepare_phi1_bus_access(v, cycle);
    vicii_execute_phi1_fetch(v, bus, cycle);

    if (v->pixel_output_enabled) {
        vicii_render_live_cycle(v, bus);
    }

    /* Deferred $D01E/$D01F clear follows this cycle's draw/collision sample. */
    if (v->clear_collisions == VICII_REG_SPR_SPR_COLL) {
        v->sprite_sprite_collision = 0;
        v->clear_collisions = 0;
    } else if (v->clear_collisions == VICII_REG_SPR_BG_COLL) {
        v->sprite_background_collision = 0;
        v->clear_collisions = 0;
    }

    /* ------------------------------------------------------------------
     * Start-of-line events (cycle 0 of the current raster line)
     * ------------------------------------------------------------------ */
    if (cycle == 0) {
        v->bad_line = false;
        if (v->timing.raster_line == v->timing.raster_compare) {
            vicii_assert_raster_irq(v);
        }
    }

    /* VICE vertical border unit (every cycle): top may clear latch+flag with
       DEN; bottom only sets the latch. At cycle 0 (= VICE PAL cycle 1) copy
       latch → live vertical flag. Mid-line $D011 RSEL/DEN changes are seen on
       the next check this cycle or at the left border apply. */
    vicii_check_vborder_top(v);
    vicii_check_vborder_bottom(v);
    if (cycle == 0u) {
        vicii_apply_vborder_latch(v);
    }

    /* Bauer/VICE allow_bad_lines: arm on first DMA line ($30) if DEN is set
       during any cycle of that line; disarm after last DMA line ($F7). */
    if (v->timing.raster_line == (uint32_t)VICII_BADLINE_FIRST &&
        (v->registers[0x11] & 0x10u) != 0u) {
        v->allow_bad_lines = true;
    }
    if (v->timing.raster_line == (uint32_t)VICII_BADLINE_LAST && cycle == 0u) {
        /* VICE's start-of-line hook runs before incrementing raster_line, so
           its LAST_DMA_LINE clear occurs after line $f7 has completed. */
    }
    if (v->timing.raster_line > (uint32_t)VICII_BADLINE_LAST) {
        v->allow_bad_lines = false;
    }

    /* Bad Line Condition (Bauer 3.5 / VICE check_badline): once allow_bad_lines
       is armed, each cycle tests (RASTER&7)==YSCROLL in $30–$F7. Mid-line
       YSCROLL writes assert/drop immediately. DEN is not re-tested here. */
    if (vicii_is_bad_line(v)) {
        v->bad_line      = true;
        v->display_state = true;
    } else {
        v->bad_line = false;
    }

    /* Bauer 3.7.2 UpdateVc at VICE Phi2(14), zero-based cycle 13.
       VC ← VCBASE and VMLI ← 0 every line; a live badline clears RC. */
    if (cycle == (uint32_t)VICII_VC_RC_CYCLE) {
        v->vc = v->vc_base;
        v->vmli = 0;
        if (v->bad_line) {
            v->rc            = 0;
            v->display_state = true;
        }

        /* Optional per-line sequencer dump for eod-3 diagnosis.
           C64M_LINELOG=<path> C64M_LINELOG_R0=190 C64M_LINELOG_R1=220
           Optional C64M_LINELOG_F0/F1 frame window. */
        {
            static FILE *linelog;
            static int line_init;
            static unsigned long lf0, lf1;
            static unsigned lr0, lr1;
            if (!line_init) {
                const char *p = getenv("C64M_LINELOG");
                const char *a = getenv("C64M_LINELOG_F0");
                const char *b = getenv("C64M_LINELOG_F1");
                const char *r0 = getenv("C64M_LINELOG_R0");
                const char *r1 = getenv("C64M_LINELOG_R1");
                lf0 = 0;
                lf1 = 0xffffffffUL;
                lr0 = 0;
                lr1 = 0xffffffffU;
                if (p) {
                    linelog = fopen(p, "wb");
                }
                if (a) {
                    lf0 = strtoul(a, NULL, 10);
                }
                if (b) {
                    lf1 = strtoul(b, NULL, 10);
                }
                if (r0) {
                    lr0 = (unsigned)strtoul(r0, NULL, 10);
                }
                if (r1) {
                    lr1 = (unsigned)strtoul(r1, NULL, 10);
                }
                line_init = 1;
            }
            if (linelog) {
                unsigned long fr = (unsigned long)v->timing.frame_number;
                unsigned rl = (unsigned)v->timing.raster_line;
                if (fr >= lf0 && fr <= lf1 && rl >= lr0 && rl <= lr1) {
                    uint8_t d011 = v->registers[VICII_REG_CONTROL_1];
                    uint8_t d018 = v->registers[VICII_REG_MEMORY_POINTER];
                    uint8_t d016 = v->registers[0x16];
                    uint16_t bank = bus ? c64_bus_vic_bank_base(bus) : 0;
                    uint16_t scr =
                        (uint16_t)(bank + ((d018 >> 4) * 0x0400u));
                    uint16_t bmp =
                        (uint16_t)(bank + ((d018 >> 3) & 1u) * 0x2000u);
                    uint16_t ch =
                        (uint16_t)(bank + ((d018 >> 1) & 7u) * 0x0800u);
                    uint16_t gaddr = 0;
                    if ((d011 & 0x20u) != 0u) {
                        gaddr = (uint16_t)(bmp +
                            (((v->vc_base + 2u) & 0x03ffu) << 3) + v->rc);
                    } else {
                        uint8_t code = v->video_matrix[2];
                        if ((d011 & 0x40u) != 0u) {
                            code &= 0x3fu;
                        }
                        gaddr = (uint16_t)(ch + (uint16_t)code * 8u + v->rc);
                    }
                    fprintf(linelog,
                            "F%lu R%u d011=%02X d016=%02X d018=%02X "
                            "disp=%d bad=%d idle=%d vc=%03X vcb=%03X rc=%u "
                            "vm2=%02X cl2=%02X g2=%04X scr=%04X bmp=%04X ch=%04X\n",
                            fr, rl, d011, d016, d018,
                            v->display_state ? 1 : 0,
                            v->bad_line ? 1 : 0,
                            v->display_state ? 0 : 1,
                            (unsigned)v->vc, (unsigned)v->vc_base,
                            (unsigned)v->rc,
                            (unsigned)v->video_matrix[2],
                            (unsigned)v->color_line[2],
                            (unsigned)gaddr,
                            (unsigned)scr, (unsigned)bmp, (unsigned)ch);
                    if (getenv("C64M_LINELOG_FULL") != NULL) {
                        int q;
                        fprintf(linelog, "  cl:");
                        for (q = 0; q < 40; ++q) {
                            fprintf(linelog, "%X", v->color_line[q] & 0x0fu);
                        }
                        fprintf(linelog, "\n  vm:");
                        for (q = 0; q < 40; ++q) {
                            fprintf(linelog, "%02X", v->video_matrix[q]);
                        }
                        fprintf(linelog, "\n  g_:");
                        for (q = 0; q < 40; ++q) {
                            fprintf(linelog, "%02X", v->g_line[q]);
                        }
                        fprintf(linelog, "\n");
                    }
                }
            }
        }
    }

    /* VICE Phi2(58), zero-based cycle 57. This runs well before line wrap;
       VC has already been advanced by the forty cycle-15..54 g-accesses. */
    if (cycle == (uint32_t)VICII_UPDATE_RC_CYCLE) {
        if (v->rc == (uint8_t)VICII_RC_MAX) {
            v->display_state = false;
            v->vc_base = v->vc;
        }
        if (v->display_state || v->bad_line) {
            v->rc = (uint8_t)((v->rc + 1u) & (uint8_t)VICII_RC_MAX);
            v->display_state = true;
        }
    }

    {
        uint8_t active_before = 0;
        int n;
        for (n = 0; n < 8; ++n) {
            if (v->sprite_active[n]) {
                active_before |= (uint8_t)(1u << n);
            }
        }
        vicii_step_sprite_sequencer(v, cycle);

        if (cycle == 0) {
            vicii_prepare_sprite_line(v, bus);
            vicii_latch_sprite_line_state(v);
        } else if ((cycle == 54u || cycle == 55u) && bus != NULL) {
            /* Same-line presentation fetch for sprites that just turned DMA on.
               cycle-0 prepare ran before DMA-on, so without this the Y-match
               line never latches a row for the renderer. */
            for (n = 0; n < 8; ++n) {
                uint8_t bit = (uint8_t)(1u << n);
                uint16_t vic_bank;
                uint16_t screen_base;
                uint16_t ptr_addr;
                uint16_t data_base;
                uint8_t enable;
                uint8_t x_msb;

                if ((active_before & bit) != 0u || !v->sprite_active[n]) {
                    continue;
                }

                vic_bank = c64_bus_vic_bank_base(bus);
                screen_base = (uint16_t)(((v->registers[0x18] >> 4) & 0x0fu) * 0x0400u);
                ptr_addr = (uint16_t)(vic_bank + screen_base + 0x03f8u + (uint16_t)n);
                v->sprite_pointer[n] = c64_bus_vic_read_ram(bus, ptr_addr);
                data_base = (uint16_t)(vic_bank + (uint16_t)v->sprite_pointer[n] * 64u);
                v->sprite_data[n][0] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + 0u));
                v->sprite_data[n][1] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + 1u));
                v->sprite_data[n][2] = c64_bus_vic_read_ram(bus, (uint16_t)(data_base + 2u));

                enable = v->registers[VICII_REG_SPR_ENABLE];
                x_msb = v->registers[VICII_REG_SPR_X_MSB];
                v->sprite_line_enabled[n] = ((enable >> n) & 1u) != 0;
                v->sprite_line_x[n] = (uint16_t)(v->registers[(uint8_t)(n * 2)] |
                    (uint16_t)(((x_msb >> n) & 1u) << 8));
                v->sprite_line_x_expand[n] =
                    ((v->registers[VICII_REG_SPR_X_EXPAND] >> n) & 1u) != 0;
                v->sprite_line_multicolor[n] =
                    ((v->registers[VICII_REG_SPR_MULTICOLOR] >> n) & 1u) != 0;
                v->sprite_line_color[n] =
                    (uint8_t)(v->registers[0x27u + (uint8_t)n] & 0x0Fu);
            }
        }
    }

    vicii_schedule_phi2_bus_access(v, cycle);
    ba_low = vicii_update_ba(v, cycle, abs_cycle);
    v->timing.rdy_active = !ba_low;
    if (ba_low) {
        if (v->timing.prefetch_cycles != 0u) {
            v->timing.prefetch_cycles--;
        }
    } else {
        v->timing.prefetch_cycles = VICII_BA_LEAD_CYCLES + 1u;
    }
    v->timing.aec_active =
        !vicii_phi2_access_scheduled_now(v) || v->timing.prefetch_cycles != 0u;
    vicii_execute_phi2_fetch(v, bus, cycle);

    /* VICE updates reg11_delay during VIC Phi2, before a CPU-owned Phi2 write.
       A CPU $D011 store in this cycle therefore cannot affect the next cycle's
       Phi1 g-address; it reaches the delayed latch one cycle later. */
    v->reg11_delay = v->registers[VICII_REG_CONTROL_1];
}

void vicii_finish_cycle(vicii *v) {
    uint32_t cyc;

    assert(v);

    /* XSCROLL pipe sample runs *after* this cycle's CPU Phi2 store (begin_cycle
       paints first, then the CPU, then finish_cycle).
       Only g-access cycles 15..54: never 0..14 (would pick up a still-live
       previous-line $62 before the first matrix cell) and never 55+ (EoD's
       right-border dodge STA $D016,$62). Either mistake pads x=24 with B0C
       and draws the solid vertical checker line. */
    cyc = v->timing.cycle_in_line;
    if (!v->vertical_border_active &&
        cyc >= (uint32_t)VICII_GACCESS_FIRST_CYCLE &&
        cyc <= (uint32_t)VICII_GACCESS_LAST_CYCLE) {
        v->xscroll_pipe = (uint8_t)(v->registers[0x16] & 0x07u);
    }

    v->timing.cycle_in_line++;
    if (v->timing.cycle_in_line < v->timing.cycles_per_line) {
        return;
    }

    /* ------------------------------------------------------------------
     * End-of-line: vertical border is handled every cycle (VICE set_vborder
     * model above); no Bauer cycle-63 dual-write needed.
     * ------------------------------------------------------------------ */
    v->timing.cycle_in_line = 0;

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
    v->allow_bad_lines = false;

    if (v->pixel_output_enabled) {
        v->working_frame.frame_number = v->timing.frame_number;
        memcpy(&v->completed_frame, &v->working_frame, sizeof(v->completed_frame));
        v->completed_frame_ready = true;
        vicii_begin_live_frame(v);
    } else {
        /* Timing still advances; no ARGB buffers to copy or re-clear. */
        v->completed_frame_ready = false;
    }

    /* VICE vicii_cycle_start_of_frame resets only vcbase and vc; it carries rc,
       vmli and idle_state (display_state) across the frame boundary. Matching
       that is required for idle-region VSP/AGSP: a partial bad line induced
       above the first natural bad line advances VC by <40, and UpdateRc only
       captures the shifted VCBASE while rc==7 (the value left in the bottom
       border). Resetting rc=0 here discarded that offset, so EoD's geometric
       object never scrolled horizontally. Normal frames are unaffected: the
       first real bad line clears rc at UpdateVc before any display g-access.
       bad_line is re-cleared at cycle 0 of every line, so it needs no reset. */
    v->vc            = 0;
    v->vc_base       = 0;
}

void vicii_step_cycle(vicii *v, const c64_bus_t *bus, uint64_t abs_cycle) {
    vicii_begin_cycle(v, bus, abs_cycle);
    vicii_finish_cycle(v);
}

bool vicii_ba_active(const vicii *v, uint64_t abs_cycle) {
    assert(v);
    return abs_cycle < v->timing.ba_low_until_abs;
}

bool vicii_aec_active(const vicii *v) {
    assert(v);
    return v->timing.aec_active;
}

bool vicii_rdy_active(const vicii *v, uint64_t abs_cycle) {
    assert(v);
    (void)abs_cycle;
    return v->timing.rdy_active;
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
        /* Return live latch; clear at end of this VIC cycle (VICE viciisc). */
        v->clear_collisions = VICII_REG_SPR_SPR_COLL;
        return v->sprite_sprite_collision;
    }

    if (reg == VICII_REG_SPR_BG_COLL) {
        v->clear_collisions = VICII_REG_SPR_BG_COLL;
        return v->sprite_background_collision;
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

/* After a $D011/$D012 write that *changes* the 9-bit raster compare: if the
   new compare equals the current raster line, raise the raster IRQ immediately.
   Hardware (and VICE viciisc) only edge-triggers on non-match → match. Writing
   $D011 YSCROLL/mode bits while the compare still matches the current line must
   NOT re-assert IRQ — Arkanoid's soft-scroll handler STA $D011 on the same
   raster that just IRQed; re-triggering ran the next chain handler ~9 lines
   early (ECM clear at 104 instead of 113) and destroyed dual-zone YSCROLL.
   Galencia still works: its chain writes a *new* D012 equal to the current
   line (compare changes → edge → trigger). */
static void vicii_raster_compare_maybe_trigger(vicii *v) {
    if (v->timing.raster_line == (uint32_t)v->timing.raster_compare) {
        vicii_assert_raster_irq(v);
    }
}

void vicii_write_register(vicii *v, uint16_t addr, uint8_t value) {
    uint8_t reg;

    assert(v);
    reg = (uint8_t)(addr & 0x3Fu);

    /* DEBUG oracle harness (compile-gated behind C64M_VIC_TRACE; inert in normal
       builds). VIC write trace for the lft-nine oracle diff. C64M_VICLOG=<path>
       enables at runtime; optional C64M_VICLOG_F0/F1 bound the frame window
       (inclusive). Record: "F<frame> R<raster> C<cycle> r<reg> v<val>".
       See md-files/lft-nine.md for the capture recipe. */
#ifdef C64M_VIC_TRACE
    {
        static FILE *viclog = NULL;
        static int   vic_init = 0;
        static unsigned long f0 = 0, f1 = 0xffffffffUL;
        if (!vic_init) {
            const char *p = getenv("C64M_VICLOG");
            const char *a = getenv("C64M_VICLOG_F0");
            const char *b = getenv("C64M_VICLOG_F1");
            if (p) { viclog = fopen(p, "wb"); }
            if (a) { f0 = strtoul(a, NULL, 10); }
            if (b) { f1 = strtoul(b, NULL, 10); }
            vic_init = 1;
        }
        if (viclog) {
            unsigned long fr = (unsigned long)v->timing.frame_number;
            if (fr >= f0 && fr <= f1) {
                fprintf(viclog, "F%lu R%u C%u r%02X v%02X\n",
                        fr, v->timing.raster_line, v->timing.cycle_in_line,
                        (unsigned)reg, (unsigned)value);
            } else if (fr > f1) {
                /* Past the capture window: flush and (optionally) exit fast. */
                fflush(viclog);
                if (getenv("C64M_VICLOG_EXIT")) {
                    exit(0);
                }
            }
        }
    }
#endif /* C64M_VIC_TRACE */

    switch (reg) {
    case 0x11: /* CONTROL_1: bit 7 is RST8, updates raster_compare bit 8 */
    {
        uint16_t old_compare = v->timing.raster_compare;
        v->registers[reg] = value & 0x7Fu;
        if (value & 0x80u) {
            v->timing.raster_compare |= 0x100u;
        } else {
            v->timing.raster_compare &= 0x00FFu;
        }
        /* Only edge-trigger when the 9-bit compare actually changed. */
        if (v->timing.raster_compare != old_compare) {
            vicii_raster_compare_maybe_trigger(v);
        }
        /* Immediate vertical-border re-check: VICE evaluates top/bottom every
           cycle from the live $D011, so a mid-line DEN/RSEL write can open or
           arm the border unit before the next left-edge apply. */
        vicii_check_vborder_top(v);
        vicii_check_vborder_bottom(v);
        break;
    }

    case 0x12: /* RASTER compare low byte */
    {
        uint16_t old_compare = v->timing.raster_compare;
        uint16_t new_compare =
            (uint16_t)((old_compare & 0x100u) | (uint16_t)value);
        if (new_compare == old_compare) {
            break; /* VICE d012_store: ignore same value */
        }
        v->timing.raster_compare = new_compare;
        vicii_raster_compare_maybe_trigger(v);
        break;
    }

    case 0x19: /* IRQ_STATUS: write-1-to-clear */
        v->irq_status &= (uint8_t)(~value & 0x0Fu);
        v->registers[reg] = v->irq_status;
        break;

    case 0x1A: /* IRQ_ENABLE */
        v->irq_enable     = value & 0x0Fu;
        v->registers[reg] = v->irq_enable;
        break;

    case VICII_REG_SPR_Y_EXPAND: /* $D017: sprite crunch on a Y-expand clear */
        if (value != v->registers[reg]) {
            uint32_t cyc = v->timing.cycle_in_line;
            int n;
            for (n = 0; n < 8; ++n) {
                uint8_t b = (uint8_t)(1u << n);
                /* A sprite whose expand bit is being cleared while its
                   expansion flip-flop is clear: force the flip-flop set, and
                   if this store lands on the crunch cycle mangle MC from
                   MC/MCBASE exactly as the hardware does. Ported from VICE
                   viciisc d017_store(). VICE fires ChkSprCrunch on
                   VICII_PAL_CYCLE(15) == raster_cycle 14 (0-based); the
                   lft-nine $D017<-$00 write lands on that cycle. */
                if (!(value & b) && !v->sprite_y_exp_ff[n]) {
                    if (cyc == 14u) {
                        uint8_t mc     = v->sprite_mc[n];
                        uint8_t mcbase = v->sprite_mcbase[n];
                        v->sprite_mc[n] = (uint8_t)((0x2au & (mcbase & mc)) |
                                                    (0x15u & (mcbase | mc)));
                    }
                    v->sprite_y_exp_ff[n] = true;
                }
            }
        }
        v->registers[reg] = value;
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

    /* Snapshot is not cycle-timed: prime color pipes from the live registers so
       geometric frames see current $D020/$D021 without the 6569 1-pixel lag
       (that lag only matters for mid-line timed writes in the live renderer). */
    v->color_pipe_d020 = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    v->color_pipe_d021 = (uint8_t)(v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu);

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

        /* Vertical flip-flop transitions (Bauer §3.9; DEN required to clear). */
        {
            bool den = (v->registers[VICII_REG_CONTROL_1] & 0x10u) != 0u;
            if (y == g.top && den) vborder = false;
            if (y == g.bottom) vborder = true;
        }

        vicii_snapshot_sprite_line(v, bus, y, spr_rows);

        for (x = 0; x < C64_FRAME_WIDTH; x++) {
            vicii_bg_pixel bg;
            vicii_sprite_pixel sprites[8];
            uint32_t pixel;
            uint8_t enable_reg;
            int n;

            /* Horizontal flip-flop transitions (Bauer §3.9) */
            if (x == g.right) hborder = true;
            if (x == g.left) {
                bool den = (v->registers[VICII_REG_CONTROL_1] & 0x10u) != 0u;
                if (y == g.bottom) vborder = true;
                if (y == g.top && den) vborder = false;
                if (!vborder) hborder = false;
            }

            bg = vicii_background_pixel(v, bus, &g, &lc, x, y);
            enable_reg = v->registers[VICII_REG_SPR_ENABLE];
            for (n = 0; n < 8; n++) {
                sprites[n] = vicii_sprite_pixel_make(0, false);
                if (!((enable_reg >> n) & 1u)) continue;
                if (!spr_rows[n].active) continue;
                {
                    uint16_t spr_x = (uint16_t)(v->registers[(uint8_t)(n * 2)] |
                        ((v->registers[VICII_REG_SPR_X_MSB] >> n & 1u) << 8));
                    int dx = vicii_sprite_dx_wrapped(v, x, spr_x);
                    sprites[n] = vicii_sprite_pixel_from_data(v, spr_rows[n].data, n, dx);
                }
            }

            /* Snapshot path: hborder stands in for main_border_ff. */
            pixel = vicii_compose_pixel(v, hborder, vborder, border_color, bg, sprites);
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
