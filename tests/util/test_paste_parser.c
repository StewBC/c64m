#include "paste_parser.h"

#include <stdio.h>
#include <stdlib.h>

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, int value) {
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

static void expect_u32(const char *name, uint32_t expected, uint32_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_type(const char *name, paste_event_type_t expected, paste_event_type_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected type %d, got %d\n", name, expected, actual);
        exit(1);
    }
}

static size_t parse_ok(const char *text, paste_event_t *events) {
    size_t count = 0;
    paste_parse_error_t err = { -1, NULL };

    if (!paste_parse(text, events, PASTE_EVENTS_MAX, &count, &err)) {
        fprintf(stderr, "parse failed at %d: %s\n", err.offset, err.message);
        exit(1);
    }
    return count;
}

static void expect_parse_error(const char *text) {
    size_t count = 0;
    paste_event_t events[PASTE_EVENTS_MAX];
    paste_parse_error_t err = { -1, NULL };

    if (paste_parse(text, events, PASTE_EVENTS_MAX, &count, &err)) {
        fail("expected parse error");
    }
    expect_true("parse error offset set", err.offset >= 0);
    expect_true("parse error message set", err.message != NULL);
}

static void test_bare_modifiers_are_oneshot(void) {
    paste_event_t events[PASTE_EVENTS_MAX];
    size_t count = parse_ok("\\[CTRL]\\[SHIFT]S", events);

    expect_size("oneshot modifier count", 3, count);
    expect_type("ctrl oneshot", PASTE_EV_KEY_ONESHOT, events[0].type);
    expect_type("shift oneshot", PASTE_EV_KEY_ONESHOT, events[1].type);
    expect_type("S keypress", PASTE_EV_KEY_PRESS, events[2].type);
}

static void test_explicit_modifiers_and_aliases(void) {
    paste_event_t events[PASTE_EVENTS_MAX];
    size_t count = parse_ok("\\[CTRL+]\\[CTRL]SA\\[CTRL-]\\[RS]\\[RE]", events);

    expect_size("explicit modifier count", 7, count);
    expect_type("ctrl assert", PASTE_EV_KEY_ASSERT, events[0].type);
    expect_type("redundant bare ctrl remains oneshot event", PASTE_EV_KEY_ONESHOT, events[1].type);
    expect_type("S key", PASTE_EV_KEY_PRESS, events[2].type);
    expect_type("A key", PASTE_EV_KEY_PRESS, events[3].type);
    expect_type("ctrl deassert", PASTE_EV_KEY_DEASSERT, events[4].type);
    expect_type("RS is RUNSTOP oneshot", PASTE_EV_KEY_ONESHOT, events[5].type);
    expect_type("RE is RESTORE NMI", PASTE_EV_NMI, events[6].type);
}

static void test_wait_tokens(void) {
    paste_event_t events[PASTE_EVENTS_MAX];
    size_t count = parse_ok("S\\[W:2]A\\[WAIT:0]", events);

    expect_size("wait count", 4, count);
    expect_type("first key", PASTE_EV_KEY_PRESS, events[0].type);
    expect_type("short wait", PASTE_EV_WAIT, events[1].type);
    expect_u32("short wait multiplier", 2u, events[1].wait.count);
    expect_type("second key", PASTE_EV_KEY_PRESS, events[2].type);
    expect_type("zero wait", PASTE_EV_WAIT, events[3].type);
    expect_u32("zero wait multiplier", 0u, events[3].wait.count);
}

static void test_invalid_wait_tokens(void) {
    expect_parse_error("\\[W:]");
    expect_parse_error("\\[WAIT:-1]");
    expect_parse_error("\\[W:nope]");
}

int main(void) {
    test_bare_modifiers_are_oneshot();
    test_explicit_modifiers_and_aliases();
    test_wait_tokens();
    test_invalid_wait_tokens();
    return 0;
}
