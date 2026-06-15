#include "frontend_input.h"

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

static void expect_size(const char *name, size_t expected, size_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %zu, got %zu\n", name, expected, actual);
        exit(1);
    }
}

static void expect_action(
    const char *name,
    const frontend_input_action *action,
    frontend_input_action_type type,
    c64_key key,
    bool pressed) {
    if (action->type != type || action->key != key || action->pressed != pressed) {
        fprintf(
            stderr,
            "%s: expected type=%d key=%d pressed=%d, got type=%d key=%d pressed=%d\n",
            name,
            type,
            key,
            pressed,
            action->type,
            action->key,
            action->pressed);
        exit(1);
    }
}

static SDL_KeyboardEvent make_key_event(
    Uint32 type,
    SDL_Keycode sym,
    SDL_Scancode scancode,
    SDL_Keymod modifiers) {
    SDL_KeyboardEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.keysym.sym = sym;
    event.keysym.scancode = scancode;
    event.keysym.mod = modifiers;
    return event;
}

static void test_shifted_at_is_semantic_at(void) {
    frontend_input_mapper mapper;
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    SDL_KeyboardEvent event;
    size_t count;

    frontend_input_mapper_reset(&mapper);
    event = make_key_event(SDL_KEYDOWN, SDLK_2, SDL_SCANCODE_2, KMOD_SHIFT);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);

    expect_size("shift 2 action count", 1, count);
    expect_action("shift 2 maps to at", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_AT, true);
}

static void test_quote_chords_release_the_same_keys(void) {
    frontend_input_mapper mapper;
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    SDL_KeyboardEvent event;
    size_t count;

    frontend_input_mapper_reset(&mapper);
    event = make_key_event(SDL_KEYDOWN, SDLK_QUOTE, SDL_SCANCODE_APOSTROPHE, KMOD_NONE);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);

    expect_size("quote down count", 2, count);
    expect_action("quote down shift", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_RIGHT_SHIFT, true);
    expect_action("quote down key", &actions[1], FRONTEND_INPUT_ACTION_KEY, C64_KEY_7, true);

    event = make_key_event(SDL_KEYUP, SDLK_QUOTE, SDL_SCANCODE_APOSTROPHE, KMOD_SHIFT);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);

    expect_size("quote up count", 2, count);
    expect_action("quote up shift", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_RIGHT_SHIFT, false);
    expect_action("quote up key", &actions[1], FRONTEND_INPUT_ACTION_KEY, C64_KEY_7, false);
}

static void test_double_quote_maps_to_shift_2(void) {
    frontend_input_mapper mapper;
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    SDL_KeyboardEvent event;
    size_t count;

    frontend_input_mapper_reset(&mapper);
    event = make_key_event(SDL_KEYDOWN, SDLK_QUOTE, SDL_SCANCODE_APOSTROPHE, KMOD_SHIFT);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);

    expect_size("double quote count", 2, count);
    expect_action("double quote shift", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_RIGHT_SHIFT, true);
    expect_action("double quote key", &actions[1], FRONTEND_INPUT_ACTION_KEY, C64_KEY_2, true);
}

static void test_shifted_letters_remain_c64_graphics_chords(void) {
    frontend_input_mapper mapper;
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    SDL_KeyboardEvent event;
    size_t count;

    frontend_input_mapper_reset(&mapper);
    event = make_key_event(SDL_KEYDOWN, SDLK_e, SDL_SCANCODE_E, KMOD_SHIFT);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);

    expect_size("shift e count", 2, count);
    expect_action("shift e shift", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_RIGHT_SHIFT, true);
    expect_action("shift e key", &actions[1], FRONTEND_INPUT_ACTION_KEY, C64_KEY_E, true);
}

static void test_semantic_cursor_and_special_keys(void) {
    frontend_input_mapper mapper;
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    SDL_KeyboardEvent event;
    size_t count;

    frontend_input_mapper_reset(&mapper);
    event = make_key_event(SDL_KEYDOWN, SDLK_LEFT, SDL_SCANCODE_LEFT, KMOD_NONE);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    expect_size("left cursor count", 2, count);
    expect_action("left cursor shift", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_RIGHT_SHIFT, true);
    expect_action("left cursor key", &actions[1], FRONTEND_INPUT_ACTION_KEY, C64_KEY_CURSOR_RIGHT, true);

    event = make_key_event(SDL_KEYDOWN, SDLK_LCTRL, SDL_SCANCODE_LCTRL, KMOD_NONE);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    expect_size("control count", 1, count);
    expect_action("control key", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_CONTROL, true);

    event = make_key_event(SDL_KEYDOWN, SDLK_TAB, SDL_SCANCODE_TAB, KMOD_NONE);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    expect_size("commodore count", 1, count);
    expect_action("commodore key", &actions[0], FRONTEND_INPUT_ACTION_KEY, C64_KEY_COMMODORE, true);

    event = make_key_event(SDL_KEYDOWN, SDLK_DELETE, SDL_SCANCODE_DELETE, KMOD_NONE);
    count = frontend_input_map_keyboard_event(&mapper, &event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    expect_size("restore count", 1, count);
    expect_action("restore key", &actions[0], FRONTEND_INPUT_ACTION_RESTORE, C64_KEY_COUNT, true);
}

static void test_option_shortcut_detection(void) {
    SDL_KeyboardEvent event;

    event = make_key_event(SDL_KEYDOWN, SDLK_r, SDL_SCANCODE_R, KMOD_ALT);
    expect_true("option modifier", frontend_input_has_option_modifier(&event));
}

int main(void) {
    test_shifted_at_is_semantic_at();
    test_quote_chords_release_the_same_keys();
    test_double_quote_maps_to_shift_2();
    test_shifted_letters_remain_c64_graphics_chords();
    test_semantic_cursor_and_special_keys();
    test_option_shortcut_detection();
    return 0;
}
