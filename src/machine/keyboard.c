#include "keyboard.h"

#include <assert.h>
#include <string.h>

typedef struct c64_key_position {
    uint8_t row;
    uint8_t column;
} c64_key_position;

static const c64_key_position c64_key_positions[C64_KEY_COUNT] = {
    [C64_KEY_A] = {1, 2},
    [C64_KEY_B] = {3, 4},
    [C64_KEY_C] = {2, 4},
    [C64_KEY_D] = {2, 2},
    [C64_KEY_E] = {1, 6},
    [C64_KEY_F] = {2, 5},
    [C64_KEY_G] = {3, 2},
    [C64_KEY_H] = {3, 5},
    [C64_KEY_I] = {4, 1},
    [C64_KEY_J] = {4, 2},
    [C64_KEY_K] = {4, 5},
    [C64_KEY_L] = {5, 2},
    [C64_KEY_M] = {4, 4},
    [C64_KEY_N] = {4, 7},
    [C64_KEY_O] = {4, 6},
    [C64_KEY_P] = {5, 1},
    [C64_KEY_Q] = {7, 6},
    [C64_KEY_R] = {2, 1},
    [C64_KEY_S] = {1, 5},
    [C64_KEY_T] = {2, 6},
    [C64_KEY_U] = {3, 6},
    [C64_KEY_V] = {3, 7},
    [C64_KEY_W] = {1, 1},
    [C64_KEY_X] = {2, 7},
    [C64_KEY_Y] = {3, 1},
    [C64_KEY_Z] = {1, 4},
    [C64_KEY_0] = {4, 3},
    [C64_KEY_1] = {7, 0},
    [C64_KEY_2] = {7, 3},
    [C64_KEY_3] = {1, 0},
    [C64_KEY_4] = {1, 3},
    [C64_KEY_5] = {2, 0},
    [C64_KEY_6] = {2, 3},
    [C64_KEY_7] = {3, 0},
    [C64_KEY_8] = {3, 3},
    [C64_KEY_9] = {4, 0},
    [C64_KEY_SPACE] = {7, 4},
    [C64_KEY_RETURN] = {0, 1},
    [C64_KEY_DELETE] = {0, 0},
    [C64_KEY_LEFT_SHIFT] = {1, 7},
    [C64_KEY_RIGHT_SHIFT] = {6, 4},
    [C64_KEY_PLUS] = {5, 0},
    [C64_KEY_MINUS] = {5, 3},
    [C64_KEY_ASTERISK] = {6, 1},
    [C64_KEY_EQUALS] = {6, 5},
    [C64_KEY_COLON] = {5, 5},
    [C64_KEY_SEMICOLON] = {6, 2},
    [C64_KEY_COMMA] = {5, 7},
    [C64_KEY_PERIOD] = {5, 4},
    [C64_KEY_SLASH] = {6, 7},
    [C64_KEY_AT] = {5, 6},
    [C64_KEY_CURSOR_RIGHT] = {0, 2},
    [C64_KEY_CURSOR_DOWN] = {0, 7},
    [C64_KEY_HOME] = {6, 3},
    [C64_KEY_RUN_STOP] = {7, 7},
    [C64_KEY_CONTROL] = {7, 2},
    [C64_KEY_COMMODORE] = {7, 5},
    [C64_KEY_LEFT_ARROW] = {7, 1},
    [C64_KEY_UP_ARROW] = {6, 6},
    [C64_KEY_POUND] = {6, 0},
    [C64_KEY_F1] = {0, 4},
    [C64_KEY_F3] = {0, 5},
    [C64_KEY_F5] = {0, 6},
    [C64_KEY_F7] = {0, 3},
};

void c64_keyboard_reset(c64_keyboard *keyboard) {
    assert(keyboard);

    memset(keyboard->rows, 0, sizeof(keyboard->rows));
}

void c64_keyboard_set_matrix(c64_keyboard *keyboard, uint8_t row, uint8_t col, bool pressed) {
    uint8_t mask;
    assert(keyboard);
    if (row > 7 || col > 7) return;
    mask = (uint8_t)(1u << col);
    if (pressed) {
        keyboard->rows[row] |= mask;
    } else {
        keyboard->rows[row] &= (uint8_t)~mask;
    }
}

void c64_keyboard_set_key(c64_keyboard *keyboard, c64_key key, bool pressed) {
    c64_key_position position;
    uint8_t mask;

    assert(keyboard);

    if (key < 0 || key >= C64_KEY_COUNT) {
        return;
    }

    position = c64_key_positions[key];
    mask = (uint8_t)(1u << position.column);
    if (pressed) {
        keyboard->rows[position.row] |= mask;
    } else {
        keyboard->rows[position.row] &= (uint8_t)~mask;
    }
}

uint8_t c64_keyboard_read_columns(const c64_keyboard *keyboard, uint8_t selected_rows) {
    uint8_t value = 0xff;
    uint8_t row;

    assert(keyboard);

    for (row = 0; row < 8; row++) {
        if ((selected_rows & (1u << row)) != 0) {
            value &= (uint8_t)~keyboard->rows[row];
        }
    }

    return value;
}

uint8_t c64_keyboard_read_rows(const c64_keyboard *keyboard, uint8_t selected_columns) {
    uint8_t value = 0xff;
    uint8_t row;

    assert(keyboard);

    for (row = 0; row < 8; row++) {
        if ((keyboard->rows[row] & selected_columns) != 0) {
            value &= (uint8_t)~(1u << row);
        }
    }

    return value;
}
