#include "app_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define c64m_chdir _chdir
#define c64m_getcwd _getcwd
#define c64m_mkdir(path, mode) _mkdir(path)
#define c64m_rmdir _rmdir
#else
#include <unistd.h>
#define c64m_chdir chdir
#define c64m_getcwd getcwd
#define c64m_mkdir(path, mode) mkdir(path, mode)
#define c64m_rmdir rmdir
#endif

/* Several tests below build "%s/literal/suffix" paths from a 1024-byte cwd
 * buffer into another 1024-byte buffer. GCC can't prove the real cwd stays
 * well under 1024 bytes, so it assumes the worst case and warns; the test
 * environment's cwd never comes close, so any truncation here can't happen
 * in practice. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

static void expect_string(const char *name, const char *expected, const char *actual) {
    if (!actual || strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n",
            name,
            expected,
            actual ? actual : "(null)");
        exit(1);
    }
}

static void normalize_path(char *path) {
    char *cursor;

    if (path == NULL) {
        return;
    }
    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
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

static void expect_bool(const char *name, int expected, int actual) {
    if ((expected != 0) != (actual != 0)) {
        fprintf(stderr, "%s: expected %s, got %s\n",
            name,
            expected ? "true" : "false",
            actual ? "true" : "false");
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
    fclose(file);
}

static void write_phase14_ini(const char *path) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fputs("[Video]\n", file);
    fputs("standard=PAL\n", file);
    fputs("true_aspect=yes\n", file);
    fputs("crt_scanlines=yes\n", file);
    fputs("crt_scanline_strength=47\n", file);
    fputs("crt_curvature=yes\n", file);
    fputs("crt_curvature_amount=23\n", file);
    fputs("\n[config]\n", file);
    fputs("Save=yes\n", file);
    fputs("scroll_wheel_lines=7\n", file);
    fputs("turbo_speeds=3,6,12\n", file);
    fputs("symbol_files=symbols/kernel.sym,symbols/basic.sym\n", file);
    fclose(file);
}

static void write_legacy_runtime_turbo_ini(const char *path) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fputs("[runtime]\n", file);
    fputs("turbo=251\n", file);
    fputs("\n[config]\n", file);
    fputs("turbo_speeds=1,2,4\n", file);
    fclose(file);
}

static bool file_contains(const char *path, const char *needle) {
    FILE *file = fopen(path, "r");
    char line[256];

    if (!file) {
        fprintf(stderr, "failed to open %s\n", path);
        exit(1);
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, needle) != NULL) {
            fclose(file);
            return true;
        }
    }

    fclose(file);
    return false;
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

    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }
    normalize_path(cwd);
    normalize_path(cwd);

    c64m_mkdir("test_noini_empty", 0777);
    if (c64m_chdir("test_noini_empty") != 0) {
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

    if (c64m_chdir(cwd) != 0) {
        fprintf(stderr, "failed to restore cwd\n");
        exit(1);
    }
    c64m_rmdir("test_noini_empty");
}

static void test_rom_paths_discovered_without_ini(void) {
    app_options options;
    char cwd[1024];
    char *argv[] = {
        "test_app_options",
        "--noini",
    };

    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }
    normalize_path(cwd);

    c64m_mkdir("test_noini_discovery", 0777);
    if (c64m_chdir("test_noini_discovery") != 0) {
        fprintf(stderr, "failed to enter test_noini_discovery\n");
        exit(1);
    }

    c64m_mkdir("roms", 0777);
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
    c64m_rmdir("roms");

    if (c64m_chdir(cwd) != 0) {
        fprintf(stderr, "failed to restore cwd\n");
        exit(1);
    }
    c64m_rmdir("test_noini_discovery");
}

static void test_rom_paths_discovered_when_default_ini_missing(void) {
    app_options options;
    char cwd[1024];
    char *argv[] = {
        "test_app_options",
    };

    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }
    normalize_path(cwd);

    c64m_mkdir("test_missing_ini_discovery", 0777);
    if (c64m_chdir("test_missing_ini_discovery") != 0) {
        fprintf(stderr, "failed to enter test_missing_ini_discovery\n");
        exit(1);
    }

    c64m_mkdir("roms", 0777);
    write_sized_file("roms/system.rom", 16384);
    write_sized_file("roms/character.rom", 4096);

    if (!app_options_load_startup(&options, 1, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string("discovered system rom path", "roms/system.rom", options.system_rom_path);
    expect_string("discovered char rom path", "roms/character.rom", options.char_rom_path);
    expect_null("basic rom path covered by system rom", options.basic_rom_path);
    expect_null("kernal rom path covered by system rom", options.kernal_rom_path);

    app_options_destroy(&options);

    remove("roms/system.rom");
    remove("roms/character.rom");
    c64m_rmdir("roms");

    if (c64m_chdir(cwd) != 0) {
        fprintf(stderr, "failed to restore cwd\n");
        exit(1);
    }
    c64m_rmdir("test_missing_ini_discovery");
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

    app_options_destroy(&options);
    remove("test_window_layout_save.ini");
}

static void test_phase14_config_from_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_phase14.ini",
    };

    write_phase14_ini("test_phase14.ini");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string("video standard", "PAL", options.video_standard);
    expect_bool("true aspect", 1, options.true_aspect);
    expect_bool("CRT scanlines", 1, options.crt_scanlines);
    expect_int("CRT scanline strength", 47, options.crt_scanline_strength);
    expect_bool("CRT curvature", 1, options.crt_curvature);
    expect_int("CRT curvature amount", 23, options.crt_curvature_amount);
    expect_bool("remember", 1, options.remember);
    expect_int("scroll wheel lines", 7, options.scroll_wheel_lines);
    expect_string("turbo speeds", "3,6,12", options.turbo_multipliers);
    expect_string("symbol files", "symbols/kernel.sym,symbols/basic.sym", options.symbol_files);

    app_options_destroy(&options);
    remove("test_phase14.ini");
}

static void test_video_standard_command_line_overrides(void) {
    app_options options;
    char *long_argv[] = {
        "test_app_options",
        "--inifile",
        "test_video_override.ini",
        "--video",
        "NTSC",
    };
    char *pal_argv[] = {
        "test_app_options",
        "--defaults",
        "-P",
    };
    char *ntsc_argv[] = {
        "test_app_options",
        "--inifile",
        "test_video_override.ini",
        "-N",
    };

    write_phase14_ini("test_video_override.ini");

    if (!app_options_load_startup(&options, 5, long_argv)) {
        fprintf(stderr, "app_options_load_startup with --video failed\n");
        exit(1);
    }
    expect_string("long video override", "NTSC", options.video_standard);
    app_options_destroy(&options);

    if (!app_options_load_startup(&options, 3, pal_argv)) {
        fprintf(stderr, "app_options_load_startup with -P failed\n");
        exit(1);
    }
    expect_string("short PAL override", "PAL", options.video_standard);
    app_options_destroy(&options);

    if (!app_options_load_startup(&options, 4, ntsc_argv)) {
        fprintf(stderr, "app_options_load_startup with -N failed\n");
        exit(1);
    }
    expect_string("short NTSC override", "NTSC", options.video_standard);
    app_options_destroy(&options);

    remove("test_video_override.ini");
}

static void test_config_turbo_speeds_ignores_runtime_turbo(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_turbo_precedence.ini",
    };

    write_legacy_runtime_turbo_ini("test_turbo_precedence.ini");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string("config turbo speeds wins", "1,2,4", options.turbo_multipliers);

    app_options_destroy(&options);
    remove("test_turbo_precedence.ini");
}

static void test_phase14_config_saved_to_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--noini",
        "--saveini",
        "--inifile",
        "test_phase14_save.ini",
    };
    char *load_argv[] = {
        "test_app_options",
        "--inifile",
        "test_phase14_save.ini",
    };

    write_legacy_runtime_turbo_ini("test_phase14_save.ini");
    if (!app_options_load_startup(&options, 5, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    app_options_set_string(&options.video_standard, "PAL");
    options.true_aspect = true;
    options.crt_scanlines = true;
    options.crt_scanline_strength = 62;
    options.crt_curvature = true;
    options.crt_curvature_amount = 41;
    options.remember = true;
    options.scroll_wheel_lines = 9;
    app_options_set_string(&options.turbo_multipliers, "5,10");
    /* browse_dirs[5] is the snapshot slot (see APP_BROWSE_DIR_SNAPSHOT); it also
       serves as the quicksave folder after unification. */
    app_options_set_string(&options.browse_dirs[5], "states");
    app_options_set_string(&options.symbol_files, "symbols/main.sym");

    if (!app_options_save_shutdown(&options)) {
        fprintf(stderr, "app_options_save_shutdown failed\n");
        exit(1);
    }
    app_options_destroy(&options);

    if (file_contains("test_phase14_save.ini", "turbo=251")) {
        fprintf(stderr, "legacy runtime turbo was not removed on save\n");
        exit(1);
    }

    if (!app_options_load_startup(&options, 3, load_argv)) {
        fprintf(stderr, "app_options_load_startup after save failed\n");
        exit(1);
    }

    expect_string("saved video standard", "PAL", options.video_standard);
    expect_bool("saved true aspect", 1, options.true_aspect);
    expect_bool("saved CRT scanlines", 1, options.crt_scanlines);
    expect_int("saved CRT scanline strength", 62, options.crt_scanline_strength);
    expect_bool("saved CRT curvature", 1, options.crt_curvature);
    expect_int("saved CRT curvature amount", 41, options.crt_curvature_amount);
    expect_bool("saved remember", 1, options.remember);
    expect_int("saved scroll wheel lines", 9, options.scroll_wheel_lines);
    expect_string("saved turbo speeds", "5,10", options.turbo_multipliers);
    expect_string("saved snapshot browse dir", "states", options.browse_dirs[5]);
    expect_string("saved symbol files", "symbols/main.sym", options.symbol_files);

    app_options_destroy(&options);
    remove("test_phase14_save.ini");
}

static void test_rom_single_system_flag(void) {
    app_options options;
    char *argv[] = { "test_app_options", "--inifile", "test_single_rom.ini" };
    FILE *file;

    /* No flag key, but basic+kernal+system all present: defaults to separate. */
    write_ini("test_single_rom.ini");
    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "load_startup failed\n");
        exit(1);
    }
    expect_bool("default single flag with basic+kernal", 0, options.rom_single_system);
    app_options_destroy(&options);

    /* Explicit flag overrides the derived default. */
    file = fopen("test_single_rom.ini", "w");
    if (!file) {
        fprintf(stderr, "failed to create ini\n");
        exit(1);
    }
    fputs("[roms]\nbasic=roms/basic.rom\nkernal=roms/kernal.rom\nsystem=roms/64c.rom\n"
          "single_system=1\n", file);
    fclose(file);
    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "load_startup failed\n");
        exit(1);
    }
    expect_bool("explicit single_system flag", 1, options.rom_single_system);

    /* Round-trip: flip it off and confirm it persists. */
    options.rom_single_system = false;
    if (!app_options_save_shutdown(&options)) {
        fprintf(stderr, "save_shutdown failed\n");
        exit(1);
    }
    app_options_destroy(&options);
    if (!file_contains("test_single_rom.ini", "single_system=false")) {
        fprintf(stderr, "single_system=false was not saved\n");
        exit(1);
    }
    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "reload failed\n");
        exit(1);
    }
    expect_bool("saved single_system flag", 0, options.rom_single_system);
    app_options_destroy(&options);
    remove("test_single_rom.ini");
}

static void test_save_paths_only_roms_and_quicksave(void) {
    app_options options;
    char *argv[] = { "test_app_options", "--inifile", "test_paths_only.ini" };
    FILE *file;

    /* Legacy ini carrying the retired [state] quicksave_folder plus a system ROM. */
    file = fopen("test_paths_only.ini", "w");
    if (!file) {
        fprintf(stderr, "failed to create ini\n");
        exit(1);
    }
    fputs("[state]\nquicksave_folder=old_states\n"
          "[roms]\nsystem=roms/64c.rom\n", file);
    fclose(file);

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "load_startup failed\n");
        exit(1);
    }
    /* Edit ROM endpoints the way the Paths tab would, then Save Paths Only. */
    app_options_set_string(&options.char_rom_path, "roms/chars.rom");
    options.rom_single_system = true;
    if (!app_options_save_paths_only(&options)) {
        fprintf(stderr, "save_paths_only failed\n");
        exit(1);
    }
    app_options_destroy(&options);

    if (file_contains("test_paths_only.ini", "quicksave_folder")) {
        fprintf(stderr, "quicksave_folder was not stripped by save_paths_only\n");
        exit(1);
    }
    if (!file_contains("test_paths_only.ini", "character=roms/chars.rom")) {
        fprintf(stderr, "ROM path was not saved by save_paths_only\n");
        exit(1);
    }
    if (!file_contains("test_paths_only.ini", "single_system=true")) {
        fprintf(stderr, "single_system flag was not saved by save_paths_only\n");
        exit(1);
    }
    remove("test_paths_only.ini");
}

static void test_symbol_files_are_relative_to_ini(void) {
    app_options options;
    char cwd[1024];
    char ini_path[1024];
    char symbol_path[1024];
    char expected_absolute[1024];
    char relative[1024];
    char absolute_list[1024];

    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }
    normalize_path(cwd);

    c64m_mkdir("test_symbol_ini", 0777);
    c64m_mkdir("test_symbol_ini/configs", 0777);
    c64m_mkdir("test_symbol_ini/symbols", 0777);
    write_sized_file("test_symbol_ini/symbols/main.sym", 1);

    snprintf(ini_path, sizeof(ini_path), "%s/test_symbol_ini/configs/c64m.ini", cwd);
    snprintf(symbol_path, sizeof(symbol_path), "%s/test_symbol_ini/symbols/main.sym", cwd);

    app_options_init(&options);
    app_options_set_string(&options.ini_path, ini_path);

    if (!app_options_path_relative_to_ini(&options, symbol_path, relative, sizeof(relative))) {
        fprintf(stderr, "app_options_path_relative_to_ini failed\n");
        exit(1);
    }
    expect_string("symbol path relative to ini", "../symbols/main.sym", relative);

    app_options_set_string(&options.symbol_files, relative);
    if (!app_options_symbol_files_absolute(&options, absolute_list, sizeof(absolute_list))) {
        fprintf(stderr, "app_options_symbol_files_absolute failed\n");
        exit(1);
    }
    snprintf(expected_absolute, sizeof(expected_absolute), "%s/test_symbol_ini/symbols/main.sym", cwd);
    expect_string("symbol path absolute for runtime", expected_absolute, absolute_list);

    app_options_set_string(&options.symbol_files, symbol_path);
    if (!app_options_save_shutdown(&options)) {
        fprintf(stderr, "app_options_save_shutdown failed\n");
        exit(1);
    }
    if (!file_contains(ini_path, "symbol_files=../symbols/main.sym")) {
        fprintf(stderr, "saved symbol_files was not relative to ini\n");
        exit(1);
    }

    app_options_destroy(&options);
    remove("test_symbol_ini/configs/c64m.ini");
    remove("test_symbol_ini/symbols/main.sym");
    c64m_rmdir("test_symbol_ini/configs");
    c64m_rmdir("test_symbol_ini/symbols");
    c64m_rmdir("test_symbol_ini");
}

static void write_disk_single_ini(const char *path, const char *disk_path) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fprintf(file, "[disk]\n8=%s\n", disk_path);
    fclose(file);
}

static void write_disk_multi_ini(const char *path, const char *disk_list) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fprintf(file, "[disk]\n8=%s\n", disk_list);
    fclose(file);
}

static void write_disk_multi_writable_ini(
    const char *path,
    const char *disk_list,
    const char *writable_list) {
    FILE *file = fopen(path, "w");

    if (!file) {
        fprintf(stderr, "failed to create %s\n", path);
        exit(1);
    }

    fprintf(file, "[disk]\n8=%s\n8_writable=%s\n", disk_list, writable_list);
    fclose(file);
}

static void test_disk_single_from_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_disk_single.ini",
    };

    write_disk_single_ini("test_disk_single.ini", "/abs/path/game.d64");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_int("disk slot 8 count", 1, options.disk_slots[8].count);
    expect_string("disk slot 8 path 0", "/abs/path/game.d64", options.disk_slots[8].paths[0]);
    expect_int("disk slot 9 count", 0, options.disk_slots[9].count);

    app_options_destroy(&options);
    remove("test_disk_single.ini");
}

static void test_disk_multi_from_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_disk_multi.ini",
    };

    write_disk_multi_ini("test_disk_multi.ini",
        "/games/disk1.d64,/games/disk2.d64,/games/disk3.d64");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_int("disk slot 8 count", 3, options.disk_slots[8].count);
    expect_string("disk slot 8 path 0", "/games/disk1.d64", options.disk_slots[8].paths[0]);
    expect_string("disk slot 8 path 1", "/games/disk2.d64", options.disk_slots[8].paths[1]);
    expect_string("disk slot 8 path 2", "/games/disk3.d64", options.disk_slots[8].paths[2]);
    expect_bool("disk slot 8 writable default", 0, app_disk_slot_current_writable(&options.disk_slots[8]));

    app_options_destroy(&options);
    remove("test_disk_multi.ini");
}

static void test_disk_writable_from_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--inifile",
        "test_disk_writable.ini",
    };

    write_disk_multi_writable_ini(
        "test_disk_writable.ini",
        "/games/disk1.d64,/games/disk2.d64",
        "0,1");

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_int("disk writable count", 2, options.disk_slots[8].count);
    expect_bool("disk 0 read-only", 0, app_disk_slot_current_writable(&options.disk_slots[8]));
    app_disk_slot_select(&options.disk_slots[8], 1);
    expect_bool("disk 1 writable", 1, app_disk_slot_current_writable(&options.disk_slots[8]));

    app_options_destroy(&options);
    remove("test_disk_writable.ini");
}

static void test_disk_relative_path_from_ini(void) {
    app_options options;
    char cwd[1024];
    char ini_path[1024];
    char expected[1024];
    char *argv[3];

    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }
    normalize_path(cwd);

    c64m_mkdir("test_disk_rel_ini", 0777);
    c64m_mkdir("test_disk_rel_ini/configs", 0777);
    c64m_mkdir("test_disk_rel_ini/disks", 0777);
    write_sized_file("test_disk_rel_ini/disks/game.d64", 1);
    write_sized_file("test_disk_rel_ini/disks/game2.d64", 1);

    snprintf(ini_path, sizeof(ini_path), "%s/test_disk_rel_ini/configs/c64m.ini", cwd);
    snprintf(expected, sizeof(expected), "%s/test_disk_rel_ini/disks/game.d64", cwd);

    write_disk_multi_ini(ini_path, "../disks/game.d64,../disks/game2.d64");

    argv[0] = "test_app_options";
    argv[1] = "--inifile";
    argv[2] = ini_path;

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_int("disk slot 8 count from rel ini", 2, options.disk_slots[8].count);
    normalize_path(options.disk_slots[8].paths[0]);
    expect_string("disk slot 8 path 0 resolved", expected, options.disk_slots[8].paths[0]);

    app_options_destroy(&options);

    remove(ini_path);
    remove("test_disk_rel_ini/disks/game.d64");
    remove("test_disk_rel_ini/disks/game2.d64");
    c64m_rmdir("test_disk_rel_ini/configs");
    c64m_rmdir("test_disk_rel_ini/disks");
    c64m_rmdir("test_disk_rel_ini");
}

static void test_disk_saved_relative_to_ini(void) {
    app_options options;
    char cwd[1024];
    char ini_path[1024];
    char disk_path[1024];
    char *argv[3];

    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "failed to read cwd\n");
        exit(1);
    }
    normalize_path(cwd);

    c64m_mkdir("test_disk_save_rel", 0777);
    c64m_mkdir("test_disk_save_rel/configs", 0777);
    c64m_mkdir("test_disk_save_rel/disks", 0777);

    snprintf(ini_path, sizeof(ini_path), "%s/test_disk_save_rel/configs/c64m.ini", cwd);
    snprintf(disk_path, sizeof(disk_path), "%s/test_disk_save_rel/disks/game.d64", cwd);

    argv[0] = "test_app_options";
    argv[1] = "--inifile";
    argv[2] = ini_path;

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    app_disk_slot_set(&options.disk_slots[8], disk_path);
    app_disk_slot_set_current_writable(&options.disk_slots[8], true);
    app_disk_slot_set(&options.disk_slots[9], disk_path);

    if (!app_options_save_shutdown(&options)) {
        fprintf(stderr, "app_options_save_shutdown failed\n");
        exit(1);
    }

    if (!file_contains(ini_path, "../disks/game.d64")) {
        fprintf(stderr, "saved disk path was not relative to ini\n");
        exit(1);
    }
    if (!file_contains(ini_path, "8_writable=1")) {
        fprintf(stderr, "saved disk writable state was not persisted\n");
        exit(1);
    }

    app_options_destroy(&options);

    remove(ini_path);
    c64m_rmdir("test_disk_save_rel/configs");
    c64m_rmdir("test_disk_save_rel/disks");
    c64m_rmdir("test_disk_save_rel");
}

static void test_disk_slot_set_and_clear(void) {
    app_disk_slot slot = {0};

    if (!app_disk_slot_set(&slot, "/games/a.d64")) {
        fprintf(stderr, "app_disk_slot_set failed\n");
        exit(1);
    }
    expect_int("slot count after set", 1, slot.count);
    expect_string("slot path 0", "/games/a.d64", slot.paths[0]);

    app_disk_slot_clear(&slot);
    expect_int("slot count after clear", 0, slot.count);

    app_disk_slot_clear(&slot);
}

static void test_disk_slot_copy(void) {
    app_disk_slot src = {0};
    app_disk_slot dst = {0};

    app_disk_slot_set(&src, "/a.d64");
    if (!app_disk_slot_copy(&dst, &src)) {
        fprintf(stderr, "app_disk_slot_copy failed\n");
        exit(1);
    }
    expect_int("copy count", 1, dst.count);
    expect_string("copy path 0", "/a.d64", dst.paths[0]);

    app_disk_slot_set(&src, "/b.d64");
    expect_string("src unchanged path", "/b.d64", src.paths[0]);
    expect_string("dst still original", "/a.d64", dst.paths[0]);

    app_disk_slot_clear(&src);
    app_disk_slot_clear(&dst);
}

static void test_audio_record_options(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--noini",
        "--audio-record",
        "build/sid.wav",
        "--audio-record-start",
        "9.5",
        "--audio-record-duration",
        "4.0",
    };

    if (!app_options_load_startup(&options, 8, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string("audio record path", "build/sid.wav", options.audio_record_path);
    expect_float_near("audio record start", 9.5f, options.audio_record_start_seconds);
    expect_float_near("audio record duration", 4.0f, options.audio_record_duration_seconds);

    app_options_destroy(&options);
}

static void test_control_port_option(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--control-port",
        "6510",
    };

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_int("control port", 6510, options.control_port);

    app_options_destroy(&options);
}

static void test_headless_requires_control_port(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--headless",
    };

    if (app_options_load_startup(&options, 2, argv)) {
        fprintf(stderr, "headless without control port should fail\n");
        app_options_destroy(&options);
        exit(1);
    }
}

static void test_headless_with_control_port(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--headless",
        "--control-port",
        "6510",
    };

    if (!app_options_load_startup(&options, 4, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_bool("headless", true, options.headless);
    expect_int("control port", 6510, options.control_port);

    app_options_destroy(&options);
}

static void test_crt_path_with_spaces(void) {
    app_options options;
    char *argv[] = {
        "test_app_options",
        "--crt",
        "assets/crt/International Soccer (1983)(Commodore).crt",
    };

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "app_options_load_startup failed\n");
        exit(1);
    }

    expect_string(
        "crt path",
        "assets/crt/International Soccer (1983)(Commodore).crt",
        options.crt_path);
    app_options_destroy(&options);
}

static void test_keyboard_joystick_defaults_and_overrides(void) {
    app_options options;
    char *default_argv[] = {"test_app_options", "--noini"};
    char *cli_argv[] = {
        "test_app_options", "--noini", "--kbdjoy", "1", "--kbdjoy-layout", "wasd",
    };

    if (!app_options_load_startup(&options, 2, default_argv)) {
        fprintf(stderr, "kbdjoy default load failed\n");
        exit(1);
    }
    expect_string("kbdjoy default layout", "numpad", options.keyboard_joystick_layout);
    expect_int("kbdjoy default port", 0, options.keyboard_joystick_port);
    app_options_destroy(&options);

    if (!app_options_load_startup(&options, 6, cli_argv)) {
        fprintf(stderr, "kbdjoy cli load failed\n");
        exit(1);
    }
    expect_string("kbdjoy cli layout", "wasd", options.keyboard_joystick_layout);
    expect_int("kbdjoy cli port", 1, options.keyboard_joystick_port);
    app_options_destroy(&options);
}

static void test_keyboard_joystick_saved_to_ini(void) {
    app_options options;
    char *argv[] = {
        "test_app_options", "--inifile", "test_kbdjoy_save.ini",
    };

    remove("test_kbdjoy_save.ini");
    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "kbdjoy save load failed\n");
        exit(1);
    }

    options.keyboard_joystick_port = 2;
    if (!app_options_set_string(&options.keyboard_joystick_layout, "wasd")) {
        fprintf(stderr, "kbdjoy set layout failed\n");
        exit(1);
    }

    if (!app_options_save_shutdown(&options)) {
        fprintf(stderr, "kbdjoy save_shutdown failed\n");
        exit(1);
    }
    app_options_destroy(&options);

    if (!app_options_load_startup(&options, 3, argv)) {
        fprintf(stderr, "kbdjoy reload failed\n");
        exit(1);
    }
    expect_string("saved kbdjoy layout", "wasd", options.keyboard_joystick_layout);
    expect_int("saved kbdjoy port", 2, options.keyboard_joystick_port);

    app_options_destroy(&options);
    remove("test_kbdjoy_save.ini");
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main(void) {
    test_rom_paths_from_ini();
    test_rom_paths_empty_without_ini();
    test_rom_paths_discovered_without_ini();
    test_rom_paths_discovered_when_default_ini_missing();
    test_window_layout_from_ini();
    test_window_layout_saved_to_ini();
    test_phase14_config_from_ini();
    test_video_standard_command_line_overrides();
    test_config_turbo_speeds_ignores_runtime_turbo();
    test_phase14_config_saved_to_ini();
    test_rom_single_system_flag();
    test_save_paths_only_roms_and_quicksave();
    test_symbol_files_are_relative_to_ini();
    test_audio_record_options();
    test_control_port_option();
    test_headless_requires_control_port();
    test_headless_with_control_port();
    test_crt_path_with_spaces();
    test_disk_single_from_ini();
    test_disk_multi_from_ini();
    test_disk_writable_from_ini();
    test_disk_relative_path_from_ini();
    test_disk_saved_relative_to_ini();
    test_disk_slot_set_and_clear();
    test_disk_slot_copy();
    test_keyboard_joystick_defaults_and_overrides();
    test_keyboard_joystick_saved_to_ini();
    return 0;
}
