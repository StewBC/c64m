#include "c64.h"
#include "keyboard.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
};

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void build_roms(c64_rom_set *roms) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffc] = (uint8_t)(TEST_RESET_VECTOR & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(TEST_RESET_VECTOR >> 8);
    roms->kernal[TEST_RESET_VECTOR - 0xe000] = 0xea;
}

static void reset_machine(c64_t *machine) {
    c64_rom_set roms;
    char error[256];

    build_roms(&roms);
    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, &roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void test_keyboard_matrix_press_release(void) {
    c64_keyboard keyboard;

    c64_keyboard_reset(&keyboard);
    expect_u8("no selected key columns", 0xff, c64_keyboard_read_columns(&keyboard, 0xff));

    c64_keyboard_set_key(&keyboard, C64_KEY_A, true);
    expect_u8("a pressed selected", 0xfb, c64_keyboard_read_columns(&keyboard, 0x02));
    expect_u8("a not selected", 0xff, c64_keyboard_read_columns(&keyboard, 0x01));

    c64_keyboard_set_key(&keyboard, C64_KEY_A, false);
    expect_u8("a released", 0xff, c64_keyboard_read_columns(&keyboard, 0x02));
}

static void test_cia_keyboard_scan(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_set_key(&machine, C64_KEY_A, true);

    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc03, 0x00);
    c64_bus_write(&machine.bus, 0xdc00, 0xfd);
    expect_u8("cia sees a key", 0xfb, c64_bus_read(&machine.bus, 0xdc01));

    c64_set_key(&machine, C64_KEY_A, false);
    expect_u8("cia sees released key", 0xff, c64_bus_read(&machine.bus, 0xdc01));
}

static void test_keyboard_reset_releases_keys(void) {
    c64_t machine;
    char error[256];

    reset_machine(&machine);
    c64_set_key(&machine, C64_KEY_RETURN, true);
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc00, 0xfe);
    expect_u8("return pressed", 0xfd, c64_bus_read(&machine.bus, 0xdc01));

    expect_true("reset machine again", c64_reset(&machine, error, sizeof(error)));
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc00, 0xfe);
    expect_u8("return released after reset", 0xff, c64_bus_read(&machine.bus, 0xdc01));
}

static void test_number_and_return_positions_are_not_transposed(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc03, 0x00);

    c64_set_key(&machine, C64_KEY_1, true);
    c64_bus_write(&machine.bus, 0xdc00, 0x7f);
    expect_u8("1 selected row reads column 0", 0xfe, c64_bus_read(&machine.bus, 0xdc01));
    c64_bus_write(&machine.bus, 0xdc00, 0xfe);
    expect_u8("1 is not cursor-down transpose", 0xff, c64_bus_read(&machine.bus, 0xdc01));
    c64_set_key(&machine, C64_KEY_1, false);

    c64_set_key(&machine, C64_KEY_3, true);
    c64_bus_write(&machine.bus, 0xdc00, 0xfd);
    expect_u8("3 selected row reads column 0", 0xfe, c64_bus_read(&machine.bus, 0xdc01));
    c64_set_key(&machine, C64_KEY_3, false);

    c64_set_key(&machine, C64_KEY_RETURN, true);
    c64_bus_write(&machine.bus, 0xdc00, 0xfe);
    expect_u8("return selected row reads column 1", 0xfd, c64_bus_read(&machine.bus, 0xdc01));
}

static void test_cursor_key_positions(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc03, 0x00);
    c64_bus_write(&machine.bus, 0xdc00, 0xfe);

    c64_set_key(&machine, C64_KEY_CURSOR_RIGHT, true);
    expect_u8("cursor right column", 0xfb, c64_bus_read(&machine.bus, 0xdc01));
    c64_set_key(&machine, C64_KEY_CURSOR_RIGHT, false);

    c64_set_key(&machine, C64_KEY_CURSOR_DOWN, true);
    expect_u8("cursor down column", 0x7f, c64_bus_read(&machine.bus, 0xdc01));
    c64_set_key(&machine, C64_KEY_CURSOR_DOWN, false);

    c64_set_key(&machine, C64_KEY_RIGHT_SHIFT, true);
    c64_set_key(&machine, C64_KEY_CURSOR_RIGHT, true);
    c64_bus_write(&machine.bus, 0xdc00, 0xbe);
    expect_u8("shift and cursor right columns", 0xeb, c64_bus_read(&machine.bus, 0xdc01));
}

static void test_home_and_shift_home_positions(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc03, 0x00);

    c64_set_key(&machine, C64_KEY_HOME, true);
    c64_bus_write(&machine.bus, 0xdc00, 0xbf);
    expect_u8("home column", 0xf7, c64_bus_read(&machine.bus, 0xdc01));

    c64_set_key(&machine, C64_KEY_RIGHT_SHIFT, true);
    c64_bus_write(&machine.bus, 0xdc00, 0xbf);
    expect_u8("shift home columns", 0xe7, c64_bus_read(&machine.bus, 0xdc01));
}

static void test_run_stop_position(void) {
    c64_t machine;

    reset_machine(&machine);
    c64_bus_write(&machine.bus, 0xdc02, 0xff);
    c64_bus_write(&machine.bus, 0xdc03, 0x00);

    c64_set_key(&machine, C64_KEY_RUN_STOP, true);
    c64_bus_write(&machine.bus, 0xdc00, 0x7f);
    expect_u8("run stop column", 0x7f, c64_bus_read(&machine.bus, 0xdc01));
}

int main(void) {
    test_keyboard_matrix_press_release();
    test_cia_keyboard_scan();
    test_keyboard_reset_releases_keys();
    test_number_and_return_positions_are_not_transposed();
    test_cursor_key_positions();
    test_home_and_shift_home_positions();
    test_run_stop_position();
    return 0;
}
