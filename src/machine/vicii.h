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
    VICII_ACTIVE_X = 32,
    VICII_ACTIVE_Y = 36,
    VICII_ACTIVE_W = 320,
    VICII_ACTIVE_H = 200,
    VICII_NTSC_CYCLES_PER_LINE = 65,
    VICII_NTSC_LINES_PER_FRAME = 263,
    VICII_PAL_CYCLES_PER_LINE = 63,
    VICII_PAL_LINES_PER_FRAME = 312,
};

typedef enum vicii_video_standard {
    VICII_VIDEO_STANDARD_NTSC = 0,
    VICII_VIDEO_STANDARD_PAL
} vicii_video_standard;

typedef struct vicii_timing {
    uint32_t cycles_per_line;
    uint32_t lines_per_frame;
    uint32_t cycle_in_line;
    uint32_t raster_line;
    uint64_t frame_number;
    bool frame_complete;
    vicii_video_standard standard;
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
};

bool vicii_init(vicii *v, char *error, size_t error_size);
void vicii_reset(vicii *v);
void vicii_set_video_standard(vicii *v, vicii_video_standard standard);
void vicii_step_cycle(vicii *v);
void vicii_destroy(vicii *v);

uint8_t vicii_read_register(vicii *v, uint16_t addr);
void vicii_write_register(vicii *v, uint16_t addr, uint8_t value);
bool vicii_consume_frame_complete(vicii *v);
bool vicii_make_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle);
void vicii_copy_snapshot(const vicii *v, c64_vicii_snapshot *out);
