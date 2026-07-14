#pragma once

#include "c64_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef C64M_C64_BUS_TYPEDEF
#define C64M_C64_BUS_TYPEDEF
typedef struct c64_bus_t c64_bus_t;
#endif

#ifndef C64M_VICII_TYPEDEF
#define C64M_VICII_TYPEDEF
typedef struct vicii vicii;
#endif

enum {
    VICII_REGISTER_COUNT = 0x40,
    VICII_NTSC_CYCLES_PER_LINE = 65,
    VICII_NTSC_LINES_PER_FRAME = 263,
    VICII_PAL_CYCLES_PER_LINE = 63,
    VICII_PAL_LINES_PER_FRAME = 312,
};

typedef enum vicii_video_standard {
    VICII_VIDEO_STANDARD_NTSC = 0,
    VICII_VIDEO_STANDARD_PAL
} vicii_video_standard;

/* The VIC-II bus operation scheduled for the current Phi2 cycle. */
typedef enum vicii_bus_access_kind {
    VICII_BUS_ACCESS_NONE = 0,
    VICII_BUS_ACCESS_C,
    VICII_BUS_ACCESS_G,
    VICII_BUS_ACCESS_IDLE,
    VICII_BUS_ACCESS_SPRITE_POINTER,
    VICII_BUS_ACCESS_SPRITE_DATA,
    /* Compatibility spelling for callers which only distinguished the old
       coarse sprite fetch marker. Phi2 sprite work is sprite-data work. */
    VICII_BUS_ACCESS_SPRITE = VICII_BUS_ACCESS_SPRITE_DATA
} vicii_bus_access_kind;

typedef struct vicii_timing {
    uint32_t cycles_per_line;
    uint32_t lines_per_frame;
    uint32_t cycle_in_line;
    uint32_t raster_line;
    uint64_t frame_number;
    bool     frame_complete;
    vicii_video_standard standard;
    vicii_bus_access_kind bus_access;      /* Phi2: CPU-visible VIC work. */
    vicii_bus_access_kind bus_access_phi1; /* Phi1: VIC-private bus work. */
    bool aec_active;                       /* AEC high: CPU owns Phi2 bus. */
    bool rdy_active;                       /* RDY high: CPU reads may advance. */

    /* Phase A additions */
    uint16_t raster_compare;          /* 9-bit: 0-311 PAL, 0-262 NTSC */

    /* BA is one schedule-derived absolute expiry. The legacy sprite field is
       retained in the serialized layout but is no longer read by the live BA
       predicate. */
    uint64_t ba_low_until_abs;
    uint64_t sprite_ba_low_until_abs;
} vicii_timing;

typedef struct c64_vicii_snapshot {
    uint32_t raster_line;
    uint32_t cycle_in_line;
    uint64_t frame_number;
    uint8_t border_color;
    uint8_t background_color;
} c64_vicii_snapshot;

struct vicii {
    uint8_t registers[VICII_REGISTER_COUNT];
    vicii_timing timing;
    c64_frame working_frame;
    c64_frame completed_frame;
    bool completed_frame_ready;

    /* Phase A additions */
    uint16_t vc;               /* Video Counter, 10-bit, 0-1023 */
    uint16_t vc_base;          /* VCBASE: latched copy of vc at Bad Line row start */
    uint8_t  rc;               /* Row Counter, 3-bit, 0-7 */
    bool     display_state;    /* true while a character row is in progress */
    bool     bad_line;         /* true if the current raster line is a Bad Line */
    uint8_t  video_matrix[40]; /* character codes fetched on Bad Lines */
    uint8_t  color_line[40];   /* color nibbles fetched on Bad Lines */
    uint8_t  irq_status;       /* live shadow of $D019 low nibble */
    uint8_t  irq_enable;       /* live shadow of $D01A low nibble */

    /* Phase D: per-sprite state */
    uint8_t  sprite_mc[8];          /* next byte offset into 63-byte sprite block (0,3,6…60) */
    bool     sprite_active[8];      /* sprite sequencer remains active for future lines */
    uint8_t  sprite_mcbase[8];      /* MCBASE latch used by the line sequencer */
    bool     sprite_visible[8];     /* sprite has valid fetched data for the current line */
    bool     sprite_y_exp_ff[8];    /* Y-expand flip-flop; governs when mc advances */
    uint8_t  sprite_pointer[8];     /* pointer fetched in the sprite p-access */
    uint8_t  sprite_data[8][3];     /* current row: 3 fetched data bytes (committed to renderer) */
    bool     sprite_line_enabled[8]; /* $D015 latched at line start / DMA-on (DMA start only; paint uses sprite_visible) */
    uint16_t sprite_line_x[8];
    bool     sprite_line_x_expand[8];
    bool     sprite_line_multicolor[8];
    uint8_t  sprite_line_color[8];
    uint8_t  sprite_line_mm0;
    uint8_t  sprite_line_mm1;

    /* Phase E: sprite priority and collision latches */
    uint8_t  sprite_priority;              /* $D01B: 1 = sprite behind foreground graphics */
    uint8_t  sprite_sprite_collision;      /* $D01E: sprite-sprite collision latch */
    uint8_t  sprite_background_collision;  /* $D01F: sprite-background collision latch */
    /* VICE: $D01E/$D01F reads return the latch but defer the clear until the
       end of the current draw cycle. Two back-to-back LDA $D01F therefore see
       the same mask — lft-nine's VIC-type detect depends on that. 0 = idle;
       0x1E / 0x1F = clear that register after this cycle's pixels. */
    uint8_t  clear_collisions;

    /* Live vertical border-unit state. Snapshot rendering keeps using conventional
       geometry; completed live frames preserve mid-frame RSEL timing effects. */
    bool     vertical_border_active;

    /* Live main (horizontal) border flip-flop. Set at the right compare column
       and reset at the left compare column (Bauer 3.9 rules 1 & 6), evaluated per
       dot in the live renderer with the CSEL live for that cycle. Persists across
       cycles/lines/frames so a timed $D016 CSEL write can leave the side border
       open. Snapshot rendering keeps geometric side borders. */
    bool     main_border_ff;

    /* When false, raster/BA/IRQ/sprite-DMA timing still advances, but ARGB pixel
       fill, working-frame clears, and completed-frame copies are skipped. Used by
       the runtime under high turbo so free-run is not bound by display work.
       Sprite collision latches only update while pixel output is enabled. */
    bool     pixel_output_enabled;

    /* 6569 color_latency: $D020/$D021 take effect one pixel late (VICE
       draw_colors_6569 ring). lft-nine's six-write $D021 splits and the author's
       "one pixel delay to line up with XSCROLL=1" depend on this. Advanced once
       per live-rendered pixel after the sample. */
    uint8_t  color_pipe_d020;
    uint8_t  color_pipe_d021;
};

bool vicii_init(vicii *v, char *error, size_t error_size);
void vicii_reset(vicii *v);
void vicii_set_video_standard(vicii *v, vicii_video_standard standard);
void vicii_set_pixel_output_enabled(vicii *v, bool enabled);
bool vicii_pixel_output_enabled(const vicii *v);
void vicii_step_cycle(vicii *v, const c64_bus_t *bus, uint64_t abs_cycle);
bool vicii_ba_active(const vicii *v, uint64_t abs_cycle);
bool vicii_aec_active(const vicii *v);
bool vicii_rdy_active(const vicii *v, uint64_t abs_cycle);
vicii_bus_access_kind vicii_bus_access(const vicii *v);
vicii_bus_access_kind vicii_bus_access_phi1(const vicii *v);
void vicii_destroy(vicii *v);

uint8_t vicii_read_register(vicii *v, uint16_t addr);
uint8_t vicii_debug_read_register(const vicii *v, uint16_t addr);
void vicii_write_register(vicii *v, uint16_t addr, uint8_t value);
bool vicii_consume_frame_complete(vicii *v);
bool vicii_copy_completed_frame(vicii *v, c64_frame *out_frame, uint64_t machine_cycle);
bool vicii_make_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle);
bool vicii_make_current_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle);
void vicii_copy_snapshot(const vicii *v, c64_vicii_snapshot *out);
