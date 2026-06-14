#pragma once

#include "nuklear_config.h"

typedef enum c64_layout_drag_kind {
    C64_LAYOUT_DRAG_NONE = 0,
    C64_LAYOUT_DRAG_SPLIT_DISPLAY,
    C64_LAYOUT_DRAG_SPLIT_TOP_BOTTOM,
    C64_LAYOUT_DRAG_SPLIT_BOTTOM,
    C64_LAYOUT_DRAG_DISPLAY_CORNER
} c64_layout_drag_kind;

typedef struct c64_layout_limits {
    int registers_h_px;
    int min_display_w_px;
    int min_right_w_px;
    int min_disassembly_h_px;
    int min_bottom_h_px;
    int min_memory_w_px;
    int min_misc_w_px;
    int gutter_px;
    int corner_px;
} c64_layout_limits;

typedef struct c64_layout {
    float split_display_right;
    float split_top_bottom;
    float split_memory_misc;
    int display_px_w;
    int display_px_h;
    float display_aspect;

    struct nk_rect display;
    struct nk_rect registers;
    struct nk_rect disassembly;
    struct nk_rect memory;
    struct nk_rect misc;

    struct nk_rect hit_split_display;
    struct nk_rect hit_split_top_bottom;
    struct nk_rect hit_split_memory_misc;
    struct nk_rect hit_display_corner;

    c64_layout_drag_kind drag_active;
    float drag_last_x;
    float drag_last_y;
    int drag_moved;
} c64_layout;

void c64_layout_init(c64_layout *layout);
void c64_layout_compute(c64_layout *layout, struct nk_rect parent, const c64_layout_limits *limits);
int c64_layout_handle_drag(c64_layout *layout, const struct nk_input *input, struct nk_rect parent, const c64_layout_limits *limits);
