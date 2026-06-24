#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum c64_key {
    C64_KEY_A = 0,
    C64_KEY_B,
    C64_KEY_C,
    C64_KEY_D,
    C64_KEY_E,
    C64_KEY_F,
    C64_KEY_G,
    C64_KEY_H,
    C64_KEY_I,
    C64_KEY_J,
    C64_KEY_K,
    C64_KEY_L,
    C64_KEY_M,
    C64_KEY_N,
    C64_KEY_O,
    C64_KEY_P,
    C64_KEY_Q,
    C64_KEY_R,
    C64_KEY_S,
    C64_KEY_T,
    C64_KEY_U,
    C64_KEY_V,
    C64_KEY_W,
    C64_KEY_X,
    C64_KEY_Y,
    C64_KEY_Z,
    C64_KEY_0,
    C64_KEY_1,
    C64_KEY_2,
    C64_KEY_3,
    C64_KEY_4,
    C64_KEY_5,
    C64_KEY_6,
    C64_KEY_7,
    C64_KEY_8,
    C64_KEY_9,
    C64_KEY_SPACE,
    C64_KEY_RETURN,
    C64_KEY_DELETE,
    C64_KEY_LEFT_SHIFT,
    C64_KEY_RIGHT_SHIFT,
    C64_KEY_PLUS,
    C64_KEY_MINUS,
    C64_KEY_ASTERISK,
    C64_KEY_EQUALS,
    C64_KEY_COLON,
    C64_KEY_SEMICOLON,
    C64_KEY_COMMA,
    C64_KEY_PERIOD,
    C64_KEY_SLASH,
    C64_KEY_AT,
    C64_KEY_CURSOR_RIGHT,
    C64_KEY_CURSOR_DOWN,
    C64_KEY_HOME,
    C64_KEY_RUN_STOP,
    C64_KEY_CONTROL,
    C64_KEY_COMMODORE,
    C64_KEY_LEFT_ARROW,
    C64_KEY_UP_ARROW,
    C64_KEY_POUND,
    C64_KEY_F1,
    C64_KEY_F3,
    C64_KEY_F5,
    C64_KEY_F7,
    C64_KEY_COUNT
} c64_key;

typedef struct c64_keyboard {
    uint8_t rows[8];
} c64_keyboard;

void c64_keyboard_reset(c64_keyboard *keyboard);
void c64_keyboard_set_key(c64_keyboard *keyboard, c64_key key, bool pressed);
void c64_keyboard_set_matrix(c64_keyboard *keyboard, uint8_t row, uint8_t col, bool pressed);
uint8_t c64_keyboard_read_columns(const c64_keyboard *keyboard, uint8_t selected_rows);
uint8_t c64_keyboard_read_rows(const c64_keyboard *keyboard, uint8_t selected_columns);
