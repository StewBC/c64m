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
    VICII_DEFAULT_BORDER_COLOR = 6,
    VICII_DEFAULT_BACKGROUND_COLOR = 14,
    VICII_TEXT_COLUMNS = 40,
    VICII_CHARACTER_WIDTH = 8,
    VICII_CHARACTER_HEIGHT = 8,
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

static void vicii_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

bool vicii_init(vicii *v, char *error, size_t error_size) {
    if (!v) {
        vicii_set_error(error, error_size, "VIC-II pointer is null");
        return false;
    }

    memset(v, 0, sizeof(*v));
    vicii_reset(v);
    vicii_set_error(error, error_size, "");
    return true;
}

void vicii_reset(vicii *v) {
    assert(v);

    memset(v->registers, 0, sizeof(v->registers));
    memset(&v->timing, 0, sizeof(v->timing));
    memset(&v->working_frame, 0, sizeof(v->working_frame));

    v->timing.cycles_per_line = VICII_NTSC_CYCLES_PER_LINE;
    v->timing.lines_per_frame = VICII_NTSC_LINES_PER_FRAME;
    v->registers[VICII_REG_BORDER_COLOR] = VICII_DEFAULT_BORDER_COLOR;
    v->registers[VICII_REG_BACKGROUND_COLOR_0] = VICII_DEFAULT_BACKGROUND_COLOR;
}

void vicii_step_cycle(vicii *v) {
    assert(v);

    v->timing.cycle_in_line++;
    if (v->timing.cycle_in_line < v->timing.cycles_per_line) {
        return;
    }

    v->timing.cycle_in_line = 0;
    v->timing.raster_line++;

    if (v->timing.raster_line >= v->timing.lines_per_frame) {
        v->timing.raster_line = 0;
        v->timing.frame_number++;
        v->timing.frame_complete = true;
    }
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

    return v->registers[reg];
}

void vicii_write_register(vicii *v, uint16_t addr, uint8_t value) {
    assert(v);

    v->registers[addr & 0x3fu] = value;
}

bool vicii_consume_frame_complete(vicii *v) {
    bool complete;

    assert(v);

    complete = v->timing.frame_complete;
    v->timing.frame_complete = false;
    return complete;
}

bool vicii_make_frame_snapshot(vicii *v, const c64_bus_t *bus, c64_frame *out_frame, uint64_t machine_cycle) {
    uint8_t border_index;
    uint8_t background_index;
    uint32_t border_color;
    uint32_t background_color;
    uint32_t x;
    uint32_t y;
    uint16_t screen_base;
    uint16_t character_base;

    assert(v);
    assert(bus);
    assert(out_frame);

    border_index = (uint8_t)(v->registers[VICII_REG_BORDER_COLOR] & 0x0fu);
    background_index = (uint8_t)(v->registers[VICII_REG_BACKGROUND_COLOR_0] & 0x0fu);
    screen_base = (uint16_t)((v->registers[VICII_REG_MEMORY_POINTER] >> 4) * 0x0400u);
    character_base = (uint16_t)(((v->registers[VICII_REG_MEMORY_POINTER] >> 1) & 0x07u) * 0x0800u);
    border_color = vicii_palette_argb[border_index];
    background_color = vicii_palette_argb[background_index];

    v->working_frame.width = C64_FRAME_WIDTH;
    v->working_frame.height = C64_FRAME_HEIGHT;
    v->working_frame.stride_bytes = C64_FRAME_WIDTH * sizeof(v->working_frame.pixels[0]);
    v->working_frame.pixel_format = C64_FRAME_PIXEL_FORMAT_ARGB8888;
    v->working_frame.frame_number = v->timing.frame_number;
    v->working_frame.machine_cycle = machine_cycle;

    for (y = 0; y < C64_FRAME_HEIGHT; y++) {
        for (x = 0; x < C64_FRAME_WIDTH; x++) {
            bool active = x >= VICII_ACTIVE_X && x < VICII_ACTIVE_X + VICII_ACTIVE_W &&
                y >= VICII_ACTIVE_Y && y < VICII_ACTIVE_Y + VICII_ACTIVE_H;
            uint32_t pixel = border_color;

            if (active) {
                uint32_t active_x = x - VICII_ACTIVE_X;
                uint32_t active_y = y - VICII_ACTIVE_Y;
                uint32_t column = active_x / VICII_CHARACTER_WIDTH;
                uint32_t row = active_y / VICII_CHARACTER_HEIGHT;
                uint16_t cell = (uint16_t)(row * VICII_TEXT_COLUMNS + column);
                uint8_t character_code = c64_bus_vic_read_ram(bus, (uint16_t)(screen_base + cell));
                uint8_t glyph_row = c64_bus_vic_read_char_glyph_at(
                    bus,
                    character_base,
                    character_code,
                    (uint8_t)(active_y & 0x07u));
                uint8_t foreground_index = c64_bus_vic_read_color(bus, cell);
                uint8_t bit = (uint8_t)(0x80u >> (active_x & 0x07u));

                pixel = background_color;
                if ((glyph_row & bit) != 0) {
                    pixel = vicii_palette_argb[foreground_index & 0x0fu];
                }
            }

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
