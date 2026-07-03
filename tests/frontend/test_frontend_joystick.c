#include "frontend_joystick_input.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_false(const char *name, bool value) {
    if (value) {
        fprintf(stderr, "%s: expected false\n", name);
        exit(1);
    }
}

static void expect_mask(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected 0x%02X, got 0x%02X\n", name, expected, actual);
        exit(1);
    }
}

static SDL_KeyboardEvent key_event(Uint32 type, SDL_Keycode sym) {
    SDL_KeyboardEvent event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.keysym.sym = sym;
    return event;
}

static bool press(frontend_joystick_input *j, SDL_Keycode sym) {
    SDL_KeyboardEvent e = key_event(SDL_KEYDOWN, sym);
    return frontend_joystick_handle_key(j, &e);
}

static bool release(frontend_joystick_input *j, SDL_Keycode sym) {
    SDL_KeyboardEvent e = key_event(SDL_KEYUP, sym);
    return frontend_joystick_handle_key(j, &e);
}

/* Unassigned joystick consumes nothing and ignores key events. */
static void test_disabled_by_default(void) {
    frontend_joystick_input j;
    memset(&j, 0, sizeof(j));
    frontend_joystick_reset(&j);

    expect_true("layout defaults numpad",
                j.layout == FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    expect_true("port 0 unassigned", j.port == 0u);
    expect_false("no consume while unassigned",
                 frontend_joystick_consumes(&j, SDLK_KP_8));
    expect_false("no change while unassigned", press(&j, SDLK_KP_8));
    expect_mask("inputs stay 0", 0, j.inputs);
}

/* Numpad directions accumulate and clear correctly. */
static void test_numpad_directions(void) {
    frontend_joystick_input j;
    memset(&j, 0, sizeof(j));
    frontend_joystick_set_layout(&j, FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    frontend_joystick_set_port(&j, 2u);

    expect_true("consumes KP_8 when assigned",
                frontend_joystick_consumes(&j, SDLK_KP_8));

    expect_true("up changes", press(&j, SDLK_KP_8));
    expect_mask("up set", FRONTEND_JOYSTICK_UP, j.inputs);

    expect_true("left changes", press(&j, SDLK_KP_4));
    expect_mask("up+left set", FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_LEFT, j.inputs);

    expect_true("fire changes", press(&j, SDLK_KP_0));
    expect_mask("up+left+fire",
                FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_LEFT | FRONTEND_JOYSTICK_FIRE,
                j.inputs);

    expect_true("release up changes", release(&j, SDLK_KP_8));
    expect_mask("left+fire remain",
                FRONTEND_JOYSTICK_LEFT | FRONTEND_JOYSTICK_FIRE, j.inputs);

    /* Repeated identical keydown does not report a change. */
    expect_false("re-press left no change", press(&j, SDLK_KP_4));
}

/* A diagonal key and an overlapping cardinal key must not clobber each other:
 * releasing the diagonal keeps the direction still held by the cardinal. */
static void test_numpad_diagonal_no_clobber(void) {
    frontend_joystick_input j;
    memset(&j, 0, sizeof(j));
    frontend_joystick_set_layout(&j, FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    frontend_joystick_set_port(&j, 1u);

    press(&j, SDLK_KP_8);              /* up */
    press(&j, SDLK_KP_7);              /* up+left */
    expect_mask("up|left held",
                FRONTEND_JOYSTICK_UP | FRONTEND_JOYSTICK_LEFT, j.inputs);
    release(&j, SDLK_KP_7);            /* release diagonal */
    expect_mask("up still held via KP_8", FRONTEND_JOYSTICK_UP, j.inputs);
}

/* WASD layout maps letters + space, and only consumes when assigned. */
static void test_wasd_layout(void) {
    frontend_joystick_input j;
    memset(&j, 0, sizeof(j));
    frontend_joystick_set_layout(&j, FRONTEND_JOYSTICK_LAYOUT_WASD);

    /* Not assigned yet: WASD must reach the C64 keyboard (not consumed). */
    expect_false("wasd not consumed when off",
                 frontend_joystick_consumes(&j, SDLK_w));
    /* Numpad is not part of the WASD layout. */
    frontend_joystick_set_port(&j, 2u);
    expect_false("KP not in wasd layout",
                 frontend_joystick_consumes(&j, SDLK_KP_8));

    expect_true("wasd consumed when assigned",
                frontend_joystick_consumes(&j, SDLK_d));
    expect_true("d changes", press(&j, SDLK_d));
    expect_mask("right set", FRONTEND_JOYSTICK_RIGHT, j.inputs);
    expect_true("space fire", press(&j, SDLK_SPACE));
    expect_mask("right+fire",
                FRONTEND_JOYSTICK_RIGHT | FRONTEND_JOYSTICK_FIRE, j.inputs);
}

/* Disabling the port releases held keys and stops consuming. */
static void test_disable_releases(void) {
    frontend_joystick_input j;
    memset(&j, 0, sizeof(j));
    frontend_joystick_set_layout(&j, FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    frontend_joystick_set_port(&j, 2u);
    press(&j, SDLK_KP_6);
    expect_mask("right set", FRONTEND_JOYSTICK_RIGHT, j.inputs);

    frontend_joystick_set_port(&j, 0u);
    expect_mask("inputs cleared on disable", 0, j.inputs);
    expect_false("no consume after disable",
                 frontend_joystick_consumes(&j, SDLK_KP_6));
}

/* Changing layout releases everything and switches recognized keys. */
static void test_layout_switch_resets(void) {
    frontend_joystick_input j;
    memset(&j, 0, sizeof(j));
    frontend_joystick_set_layout(&j, FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    frontend_joystick_set_port(&j, 1u);
    press(&j, SDLK_KP_8);
    expect_mask("up set", FRONTEND_JOYSTICK_UP, j.inputs);

    frontend_joystick_set_layout(&j, FRONTEND_JOYSTICK_LAYOUT_WASD);
    expect_mask("inputs cleared on layout switch", 0, j.inputs);
    expect_false("numpad no longer consumed",
                 frontend_joystick_consumes(&j, SDLK_KP_8));
    expect_true("wasd now consumed", frontend_joystick_consumes(&j, SDLK_w));
}

static void test_layout_string_roundtrip(void) {
    expect_true("wasd parses",
                frontend_joystick_layout_from_string("wasd") ==
                    FRONTEND_JOYSTICK_LAYOUT_WASD);
    expect_true("WASD case-insensitive",
                frontend_joystick_layout_from_string("WASD") ==
                    FRONTEND_JOYSTICK_LAYOUT_WASD);
    expect_true("numpad parses",
                frontend_joystick_layout_from_string("numpad") ==
                    FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    expect_true("unknown falls back to numpad",
                frontend_joystick_layout_from_string("xyz") ==
                    FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    expect_true("NULL falls back to numpad",
                frontend_joystick_layout_from_string(NULL) ==
                    FRONTEND_JOYSTICK_LAYOUT_NUMPAD);
    expect_true("to_string numpad",
                strcmp(frontend_joystick_layout_to_string(
                           FRONTEND_JOYSTICK_LAYOUT_NUMPAD),
                       "numpad") == 0);
    expect_true("to_string wasd",
                strcmp(frontend_joystick_layout_to_string(
                           FRONTEND_JOYSTICK_LAYOUT_WASD),
                       "wasd") == 0);
}

int main(void) {
    test_disabled_by_default();
    test_numpad_directions();
    test_numpad_diagonal_no_clobber();
    test_wasd_layout();
    test_disable_releases();
    test_layout_switch_resets();
    test_layout_string_roundtrip();
    printf("all frontend joystick tests passed\n");
    return 0;
}
