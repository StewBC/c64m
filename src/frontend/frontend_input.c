#include "frontend_input.h"

#include <string.h>

static bool has_shift_modifier(const SDL_KeyboardEvent *key) {
    return key != NULL && (key->keysym.mod & KMOD_SHIFT) != 0;
}

bool frontend_input_has_option_modifier(const SDL_KeyboardEvent *key) {
    return key != NULL && (key->keysym.mod & KMOD_ALT) != 0;
}

bool frontend_input_has_shift_modifier(const SDL_KeyboardEvent *key) {
    return has_shift_modifier(key);
}

bool frontend_input_is_host_quit_shortcut(const SDL_KeyboardEvent *key) {
    SDL_Keymod modifiers;

    if (key == NULL || key->keysym.sym != SDLK_q) {
        return false;
    }

    modifiers = key->keysym.mod;

#if defined(__APPLE__)
    return (modifiers & KMOD_GUI) != 0;
#else
    return (modifiers & KMOD_ALT) != 0;
#endif
}

void frontend_input_mapper_reset(frontend_input_mapper *mapper) {
    if (mapper == NULL) {
        return;
    }

    memset(mapper, 0, sizeof(*mapper));
}

static void add_key(frontend_input_chord *chord, c64_key key, bool pressed) {
    frontend_input_action *action;

    if (chord == NULL || chord->count >= FRONTEND_INPUT_MAX_ACTIONS) {
        return;
    }

    action = &chord->actions[chord->count++];
    action->type = FRONTEND_INPUT_ACTION_KEY;
    action->key = key;
    action->pressed = pressed;
}

static void add_restore(frontend_input_chord *chord) {
    frontend_input_action *action;

    if (chord == NULL || chord->count >= FRONTEND_INPUT_MAX_ACTIONS) {
        return;
    }

    action = &chord->actions[chord->count++];
    action->type = FRONTEND_INPUT_ACTION_RESTORE;
    action->key = C64_KEY_COUNT;
    action->pressed = true;
}

static void add_shifted_key(frontend_input_chord *chord, c64_key key, bool pressed) {
    add_key(chord, C64_KEY_RIGHT_SHIFT, pressed);
    add_key(chord, key, pressed);
}

static bool map_letter(SDL_Keycode sym, c64_key *out_key) {
    if (out_key == NULL || sym < SDLK_a || sym > SDLK_z) {
        return false;
    }

    *out_key = (c64_key)(C64_KEY_A + (sym - SDLK_a));
    return true;
}

static bool map_digit(SDL_Keycode sym, c64_key *out_key) {
    if (out_key == NULL || sym < SDLK_0 || sym > SDLK_9) {
        return false;
    }

    *out_key = (c64_key)(C64_KEY_0 + (sym - SDLK_0));
    return true;
}

static void map_keydown(const SDL_KeyboardEvent *event, frontend_input_chord *chord) {
    SDL_Keycode sym;
    bool shifted;
    c64_key key;

    if (event == NULL || chord == NULL) {
        return;
    }

    sym = event->keysym.sym;
    shifted = has_shift_modifier(event);

    if (map_letter(sym, &key)) {
        if (shifted) {
            add_shifted_key(chord, key, true);
        } else {
            add_key(chord, key, true);
        }
        return;
    }

    if (!shifted && map_digit(sym, &key)) {
        add_key(chord, key, true);
        return;
    }

    switch (sym) {
        case SDLK_1:
            add_shifted_key(chord, C64_KEY_1, true);
            break;
        case SDLK_2:
            add_key(chord, C64_KEY_AT, true);
            break;
        case SDLK_3:
            add_shifted_key(chord, C64_KEY_3, true);
            break;
        case SDLK_4:
            add_shifted_key(chord, C64_KEY_4, true);
            break;
        case SDLK_5:
            add_shifted_key(chord, C64_KEY_5, true);
            break;
        case SDLK_6:
            add_key(chord, C64_KEY_UP_ARROW, true);
            break;
        case SDLK_7:
            add_shifted_key(chord, C64_KEY_6, true);
            break;
        case SDLK_8:
        case SDLK_RIGHTBRACKET:
            add_key(chord, C64_KEY_ASTERISK, true);
            break;
        case SDLK_9:
            add_shifted_key(chord, C64_KEY_8, true);
            break;
        case SDLK_0:
            add_shifted_key(chord, C64_KEY_9, true);
            break;
        case SDLK_SPACE:
            add_key(chord, C64_KEY_SPACE, true);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            add_key(chord, C64_KEY_RETURN, true);
            break;
        case SDLK_BACKSPACE:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_DELETE, true);
            } else {
                add_key(chord, C64_KEY_DELETE, true);
            }
            break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            add_key(chord, C64_KEY_CONTROL, true);
            break;
        case SDLK_TAB:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_COMMODORE, true);
            } else {
                add_key(chord, C64_KEY_COMMODORE, true);
            }
            break;
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            add_key(chord, C64_KEY_PLUS, true);
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            add_key(chord, C64_KEY_MINUS, true);
            break;
        case SDLK_ASTERISK:
        case SDLK_KP_MULTIPLY:
            add_key(chord, C64_KEY_ASTERISK, true);
            break;
        case SDLK_EQUALS:
            add_key(chord, shifted ? C64_KEY_PLUS : C64_KEY_EQUALS, true);
            break;
        case SDLK_SEMICOLON:
            add_key(chord, shifted ? C64_KEY_COLON : C64_KEY_SEMICOLON, true);
            break;
        case SDLK_COMMA:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_COMMA, true);
            } else {
                add_key(chord, C64_KEY_COMMA, true);
            }
            break;
        case SDLK_PERIOD:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_PERIOD, true);
            } else {
                add_key(chord, C64_KEY_PERIOD, true);
            }
            break;
        case SDLK_SLASH:
        case SDLK_KP_DIVIDE:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_SLASH, true);
            } else {
                add_key(chord, C64_KEY_SLASH, true);
            }
            break;
        case SDLK_QUOTE:
            add_shifted_key(chord, shifted ? C64_KEY_2 : C64_KEY_7, true);
            break;
        case SDLK_BACKQUOTE:
            add_key(chord, C64_KEY_LEFT_ARROW, true);
            break;
        case SDLK_BACKSLASH:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_UP_ARROW, true);
            } else {
                add_key(chord, C64_KEY_UP_ARROW, true);
            }
            break;
        case SDLK_LEFTBRACKET:
            add_key(chord, C64_KEY_AT, true);
            break;
        case SDLK_RIGHT:
            add_key(chord, C64_KEY_CURSOR_RIGHT, true);
            break;
        case SDLK_DOWN:
            add_key(chord, C64_KEY_CURSOR_DOWN, true);
            break;
        case SDLK_LEFT:
            add_shifted_key(chord, C64_KEY_CURSOR_RIGHT, true);
            break;
        case SDLK_UP:
            add_shifted_key(chord, C64_KEY_CURSOR_DOWN, true);
            break;
        case SDLK_HOME:
            if (shifted) {
                add_shifted_key(chord, C64_KEY_HOME, true);
            } else {
                add_key(chord, C64_KEY_HOME, true);
            }
            break;
        case SDLK_ESCAPE:
            add_key(chord, C64_KEY_RUN_STOP, true);
            break;
        case SDLK_DELETE:
            add_restore(chord);
            break;
        case SDLK_F1:
            add_key(chord, C64_KEY_F1, true);
            break;
        case SDLK_F2:
            add_shifted_key(chord, C64_KEY_F1, true);
            break;
        case SDLK_F3:
            add_key(chord, C64_KEY_F3, true);
            break;
        case SDLK_F4:
            add_shifted_key(chord, C64_KEY_F3, true);
            break;
        case SDLK_F5:
            add_key(chord, C64_KEY_F5, true);
            break;
        case SDLK_F6:
            add_shifted_key(chord, C64_KEY_F5, true);
            break;
        case SDLK_F7:
            add_key(chord, C64_KEY_F7, true);
            break;
        case SDLK_F8:
            add_shifted_key(chord, C64_KEY_F7, true);
            break;
        default:
            break;
    }
}

static void copy_actions(
    frontend_input_action *actions,
    size_t capacity,
    const frontend_input_chord *chord) {
    size_t i;
    size_t count;

    if (actions == NULL || chord == NULL || capacity == 0) {
        return;
    }

    count = chord->count < capacity ? chord->count : capacity;
    for (i = 0; i < count; i++) {
        actions[i] = chord->actions[i];
    }
}

static void copy_release_actions(
    frontend_input_action *actions,
    size_t capacity,
    const frontend_input_chord *chord) {
    size_t i;
    size_t count;

    if (actions == NULL || chord == NULL || capacity == 0) {
        return;
    }

    count = chord->count < capacity ? chord->count : capacity;
    for (i = 0; i < count; i++) {
        actions[i] = chord->actions[i];
        if (actions[i].type == FRONTEND_INPUT_ACTION_KEY) {
            actions[i].pressed = false;
        }
    }
}

size_t frontend_input_map_keyboard_event(
    frontend_input_mapper *mapper,
    const SDL_KeyboardEvent *event,
    frontend_input_action *actions,
    size_t capacity) {
    frontend_input_chord chord = {0};
    SDL_Scancode scancode;
    size_t count;

    if (mapper == NULL || event == NULL || actions == NULL || capacity == 0) {
        return 0;
    }

    if (event->repeat != 0) {
        return 0;
    }

    scancode = event->keysym.scancode;
    if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) {
        return 0;
    }

    if (event->type == SDL_KEYDOWN) {
        map_keydown(event, &chord);
        mapper->active[scancode] = chord;
        copy_actions(actions, capacity, &chord);
        return chord.count < capacity ? chord.count : capacity;
    }

    if (event->type == SDL_KEYUP) {
        count = mapper->active[scancode].count < capacity ? mapper->active[scancode].count : capacity;
        copy_release_actions(actions, capacity, &mapper->active[scancode]);
        mapper->active[scancode].count = 0;
        return count;
    }

    return 0;
}
