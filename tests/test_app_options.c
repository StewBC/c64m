#include "app_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        exit(1);
    }
}

static void expect_float_near(const char *name, float expected, float actual) {
    float diff = expected - actual;

    if (diff < 0.0f) {
        diff = -diff;
    }
    if (diff > 0.0001f) {
        fprintf(stderr, "%s: expected %.4f, got %.4f\n", name, expected, actual);
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

static void write_sized_file(const char *path, size_t size) {
    FILE *file = fopen(path, "wb");
    size_t i;

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    for (i = 0; i < size; ++i) {
        fputc((int)(i & 0xff), file);
    }
    fclose(file);
}

static void write_window_layout_ini(const char *path) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fputs("[Window]\n", file);
    fputs("width=1234\n", file);
    fputs("height=876\n", file);
    fputs("\n[Layout]\n", file);
    fputs("split_display_right=0.7\n", file);
    fputs("split_top_bottom=0.6\n", file);
    fputs("split_memory_misc=0.4\n", file);
    fputs("display_width=500\n", file);
    fputs("display_height=360\n", file);
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
    char cwd[1024];
    char *argv[] = {
        "test_app_options",
        "--noini",
    };

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }

    mkdir("test_noini_empty", 0777);
    if (chdir("test_noini_empty") != 0) {
        fprintf(stderr, "failed to enter test_noini_empty\n");
        exit(1);
    }

    if (!app_options_load_startup(&options, 2, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_null("basic rom path", options.basic_rom_path);
    expect_null("char rom path", options.char_rom_path);
    expect_null("kernal rom path", options.kernal_rom_path);
    expect_null("system rom path", options.system_rom_path);

    app_options_destroy(&options);

    if (chdir(cwd) != 0) {
        fprintf(stderr, "failed to restore cwd\n");
        exit(1);
    }
    rmdir("test_noini_empty");
}

static void test_rom_paths_discovered_without_ini(void) {
    app_options options;
    char cwd[1024];
    char *argv[] = {
        "test_app_options",
        "--noini",
    };

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }

    mkdir("test_noini_discovery", 0777);
    if (chdir("test_noini_discovery") != 0) {
        fprintf(stderr, "failed to enter test_noini_discovery\n");
        exit(1);
    }

    mkdir("roms", 0777);
    write_sized_file("roms/SYSTEM.rom", 16384);
    write_sized_file("roms/basic.bin", 8192);
    write_sized_file("roms/Character", 4096);
    write_sized_file("roms/KERNAL.BIN", 8192);
    write_sized_file("roms/system.bad", 1);

    if (!app_options_load_startup(&options, 2, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string("discovered system rom path", "roms/SYSTEM.rom", options.system_rom_path);
    expect_string("discovered basic rom path", "roms/basic.bin", options.basic_rom_path);
    expect_string("discovered char rom path", "roms/Character", options.char_rom_path);
    expect_string("discovered kernal rom path", "roms/KERNAL.BIN", options.kernal_rom_path);

    app_options_destroy(&options);

    remove("roms/SYSTEM.rom");
    remove("roms/basic.bin");
    remove("roms/Character");
    remove("roms/KERNAL.BIN");
    remove("roms/system.bad");
    rmdir("roms");

    if (chdir(cwd) != 0) {
        fprintf(stderr, "failed to restore cwd\n");
        exit(1);
    }
    rmdir("test_noini_discovery");
}

static void test_window_layout_from_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_window_layout.ini",
    };

    write_window_layout_ini("test_window_layout.ini");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_int("window width", 1234, options.window_width);
    expect_int("window height", 876, options.window_height);
    expect_float_near("split display right", 0.7f, options.layout_split_display_right);
    expect_float_near("split top bottom", 0.6f, options.layout_split_top_bottom);
    expect_float_near("split memory misc", 0.4f, options.layout_split_memory_misc);
    expect_int("layout display width", 500, options.layout_display_width);
    expect_int("layout display height", 360, options.layout_display_height);

    app_options_destroy(&options);
    remove("test_window_layout.ini");
}

static void test_window_layout_saved_to_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_window_layout_save.ini",
    };

    remove("test_window_layout_save.ini");
    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    options.window_width = 1111;
    options.window_height = 777;
    options.layout_split_display_right = 0.65f;
    options.layout_split_top_bottom = 0.52f;
    options.layout_split_memory_misc = 0.48f;
    options.layout_display_width = 420;
    options.layout_display_height = 300;

    if (!app_options_save_shutdown(&options)) {
        fprintf(stderr, "app_options_save_shutdown failed\n");
        exit(1);
    }
    app_options_destroy(&options);

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup after save failed\n");
        exit(1);
    }

    expect_int("saved window width", 1111, options.window_width);
    expect_int("saved window height", 777, options.window_height);
    expect_float_near("saved split display right", 0.65f, options.layout_split_display_right);
    expect_float_near("saved split top bottom", 0.52f, options.layout_split_top_bottom);
    expect_float_near("saved split memory misc", 0.48f, options.layout_split_memory_misc);
    expect_int("saved layout display width", 420, options.layout_display_width);
    expect_int("saved layout display height", 300, options.layout_display_height);

    app_options_destroy(&options);
    remove("test_window_layout_save.ini");
}

int main(void) {
    test_rom_paths_from_ini();
    test_rom_paths_empty_without_ini();
    test_rom_paths_discovered_without_ini();
    test_window_layout_from_ini();
    test_window_layout_saved_to_ini();
    return 0;
}
