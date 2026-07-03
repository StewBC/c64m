#include "frontend_joystick_input.h"

#include <string.h>

typedef struct joystick_binding {
    SDL_Keycode sym;
    uint8_t     mask;
} joystick_binding;

/* Numpad digits are not mapped to any C64 key (see frontend_input.c), so this
 * cluster can be consumed unconditionally while assigned without stealing a
 * C64 keystroke. */
static const joystick_binding s_numpad_bindings[] = {
    {SDLK_KP_8, FRONTEND_JOYSTICK_UP},
    {SDLK_KP_2, FRONTEND_JOYSTICK_DOWN},
    {SDLK_KP_4, FRONTEND_JOYSTICK_LEFT},
    {SDLK_KP_6, FRONTEND_JOYSTICK_RIGHT},
    {SDLK_KP_7, FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_LEFT},
    {SDLK_KP_9, FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_RIGHT},
    {SDLK_KP_1, FRONTEND_JOYSTICK_DOWN | FRONTEND_JOYSTICK_LEFT},
    {SDLK_KP_3, FRONTEND_JOYSTICK_DOWN | FRONTEND_JOYSTICK_RIGHT},
    {SDLK_KP_0, FRONTEND_JOYSTICK_FIRE},
};

/* W/A/S/D are C64 letter keys, so while assigned they are stolen from the C64
 * keyboard (the accepted trade-off for the WASD layout). */
static const joystick_binding s_wasd_bindings[] = {
    {SDLK_w, FRONTEND_JOYSTICK_UP},
    {SDLK_s, FRONTEND_JOYSTICK_DOWN},
    {SDLK_a, FRONTEND_JOYSTICK_LEFT},
    {SDLK_d, FRONTEND_JOYSTICK_RIGHT},
    {SDLK_SPACE, FRONTEND_JOYSTICK_FIRE},
};

static const joystick_binding *active_bindings(frontend_joystick_layout layout,
                                               size_t *count) {
    if (layout == FRONTEND_JOYSTICK_LAYOUT_WASD) {
        *count = sizeof(s_wasd_bindings) / sizeof(s_wasd_bindings[0]);
        return s_wasd_bindings;
    }
    *count = sizeof(s_numpad_bindings) / sizeof(s_numpad_bindings[0]);
    return s_numpad_bindings;
}

/* Index of sym in the active layout, or -1. */
static int binding_index(frontend_joystick_layout layout, SDL_Keycode sym) {
    const joystick_binding *bindings;
    size_t count;
    size_t i;

    bindings = active_bindings(layout, &count);
    for (i = 0; i < count; ++i) {
        if (bindings[i].sym == sym) {
            return (int)i;
        }
    }
    return -1;
}

/* Recompute the accumulated mask from currently held keys. Recomputing from
 * scratch (rather than OR/clear per event) avoids a diagonal key release
 * clearing a direction bit that a cardinal key still holds. */
static void recompute_inputs(frontend_joystick_input *joystick) {
    const joystick_binding *bindings;
    size_t count;
    size_t i;
    uint8_t mask = 0;

    bindings = active_bindings(joystick->layout, &count);
    for (i = 0; i < count; ++i) {
        if (joystick->key_down[i]) {
            mask |= bindings[i].mask;
        }
    }
    joystick->inputs = mask;
}

void frontend_joystick_reset(frontend_joystick_input *joystick) {
    if (joystick == NULL) {
        return;
    }
    joystick->inputs = 0;
    memset(joystick->key_down, 0, sizeof(joystick->key_down));
}

frontend_joystick_layout frontend_joystick_layout_from_string(const char *name) {
    if (name != NULL && SDL_strcasecmp(name, "wasd") == 0) {
        return FRONTEND_JOYSTICK_LAYOUT_WASD;
    }
    return FRONTEND_JOYSTICK_LAYOUT_NUMPAD;
}

const char *frontend_joystick_layout_to_string(frontend_joystick_layout layout) {
    return layout == FRONTEND_JOYSTICK_LAYOUT_WASD ? "wasd" : "numpad";
}

void frontend_joystick_set_layout(frontend_joystick_input *joystick,
                                  frontend_joystick_layout layout) {
    if (joystick == NULL) {
        return;
    }
    joystick->layout = layout;
    frontend_joystick_reset(joystick);
}

void frontend_joystick_set_port(frontend_joystick_input *joystick, unsigned port) {
    if (joystick == NULL) {
        return;
    }
    joystick->port = (port == 1u || port == 2u) ? port : 0u;
    if (joystick->port == 0u) {
        frontend_joystick_reset(joystick);
    }
}

bool frontend_joystick_consumes(const frontend_joystick_input *joystick,
                                SDL_Keycode sym) {
    if (joystick == NULL || joystick->port == 0u) {
        return false;
    }
    return binding_index(joystick->layout, sym) >= 0;
}

bool frontend_joystick_handle_key(frontend_joystick_input *joystick,
                                  const SDL_KeyboardEvent *event) {
    int index;
    uint8_t previous;

    if (joystick == NULL || event == NULL || joystick->port == 0u) {
        return false;
    }
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) {
        return false;
    }

    index = binding_index(joystick->layout, event->keysym.sym);
    if (index < 0) {
        return false;
    }

    joystick->key_down[index] = (event->type == SDL_KEYDOWN);
    previous = joystick->inputs;
    recompute_inputs(joystick);
    return joystick->inputs != previous;
}
