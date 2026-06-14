#include "app_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_string(const char *name, const char *expected, const char *actual) {
    if (!actual || strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n",
            name,
            expected,
            actual ? actual : "(null)");
        exit(1);
    }
}

static void expect_null(const char *name, const char *actual) {
    if (actual) {
        fprintf(stderr, "%s: expected null, got `%s`\n", name, actual);
        exit(1);
    }
}

static void write_ini(const char *path) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fputs("[roms]\n", file);
    fputs("basic=roms/basic.rom\n", file);
    fputs("character=roms/characters.rom\n", file);
    fputs("kernal=roms/kernal.rom\n", file);
    fputs("system=roms/64c.rom\n", file);
    fclose(file);
}

static void test_rom_paths_from_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_app_options.ini",
    };

    write_ini("test_app_options.ini");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string("basic rom path", "roms/basic.rom", options.basic_rom_path);
    expect_string("char rom path", "roms/characters.rom", options.char_rom_path);
    expect_string("kernal rom path", "roms/kernal.rom", options.kernal_rom_path);
    expect_string("system rom path", "roms/64c.rom", options.system_rom_path);

    app_options_destroy(&options);
    remove("test_app_options.ini");
}

static void test_rom_paths_empty_without_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--noini",
    };

    if (!app_options_load_startup(&options, 2, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_null("basic rom path", options.basic_rom_path);
    expect_null("char rom path", options.char_rom_path);
    expect_null("kernal rom path", options.kernal_rom_path);
    expect_null("system rom path", options.system_rom_path);

    app_options_destroy(&options);
}

int main(void) {
    test_rom_paths_from_ini();
    test_rom_paths_empty_without_ini();
    return 0;
}
