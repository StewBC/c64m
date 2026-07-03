#pragma once

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host-keyboard driven C64 joystick.
 *
 * This is a frontend-only input source. It produces the same 5-bit joystick
 * mask that the SDL game-controller path produces and feeds it, per assigned
 * C64 port, into runtime_client_set_joystick() at the same choke point.
 *
 * The bit values below intentionally mirror c64_joystick_input in
 * machine/c64.h. They are duplicated here so the frontend does not take a
 * dependency on the machine layer (see AGENTS.md dependency rules). If the
 * machine enum ever changes, keep these in sync.
 */
enum {
    FRONTEND_JOYSTICK_UP    = 0x01,
    FRONTEND_JOYSTICK_DOWN  = 0x02,
    FRONTEND_JOYSTICK_LEFT  = 0x04,
    FRONTEND_JOYSTICK_RIGHT = 0x08,
    FRONTEND_JOYSTICK_FIRE  = 0x10
};

enum {
    /* Largest layout binding table (numpad: 4 cardinal + 4 diagonal + fire). */
    FRONTEND_JOYSTICK_MAX_BINDINGS = 9
};

typedef enum frontend_joystick_layout {
    FRONTEND_JOYSTICK_LAYOUT_NUMPAD = 0, /* KP_8/2/4/6 + diagonals, KP_0 fire */
    FRONTEND_JOYSTICK_LAYOUT_WASD        /* W/A/S/D + Space fire */
} frontend_joystick_layout;

typedef struct frontend_joystick_input {
    frontend_joystick_layout layout;
    unsigned port;    /* 0 = unassigned/disabled; 1 or 2 = live C64 port */
    uint8_t  inputs;  /* current accumulated FRONTEND_JOYSTICK_* mask */
    bool     key_down[FRONTEND_JOYSTICK_MAX_BINDINGS]; /* per active binding */
} frontend_joystick_input;

/* Release all keys (clears inputs and per-key state); layout and port kept. */
void frontend_joystick_reset(frontend_joystick_input *joystick);

/* Parse a layout name; unknown/NULL falls back to numpad. */
frontend_joystick_layout frontend_joystick_layout_from_string(const char *name);
const char *frontend_joystick_layout_to_string(frontend_joystick_layout layout);

/* Change the active layout; releases any held keys. */
void frontend_joystick_set_layout(frontend_joystick_input *joystick,
                                  frontend_joystick_layout layout);

/* Assign to a C64 port (0 disables). Disabling releases all held keys. */
void frontend_joystick_set_port(frontend_joystick_input *joystick, unsigned port);

/* True when the joystick is assigned to a port and this key belongs to the
 * active layout, i.e. the event loop should consume the key instead of routing
 * it to the C64 keyboard. */
bool frontend_joystick_consumes(const frontend_joystick_input *joystick,
                                SDL_Keycode sym);

/* Apply a key up/down event. Returns true if the accumulated joystick mask
 * changed (so the caller should re-publish the port state). Keys that are not
 * part of the active layout, or events while unassigned, return false. */
bool frontend_joystick_handle_key(frontend_joystick_input *joystick,
                                  const SDL_KeyboardEvent *event);
