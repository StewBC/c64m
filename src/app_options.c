#include "app_options.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "argparse.h"
#include "config.h"

#define C64M_DEFAULT_INI "c64m.ini"
#define C64M_DEFAULT_VIDEO_STANDARD "NTSC"
#define C64M_DEFAULT_VIDEO_FILTER "nearest"
#define C64M_DEFAULT_SCROLL_WHEEL_LINES 3
#define C64M_DEFAULT_LAYOUT_SPLIT_DISPLAY_RIGHT 0.62f
#define C64M_DEFAULT_LAYOUT_SPLIT_TOP_BOTTOM 0.58f
#define C64M_DEFAULT_LAYOUT_SPLIT_MEMORY_MISC 0.55f
#define C64M_SYSTEM_ROM_SIZE 16384
#define C64M_BASIC_ROM_SIZE 8192
#define C64M_KERNAL_ROM_SIZE 8192
#define C64M_CHARACTER_ROM_SIZE 4096

static char *copy_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    length = strlen(value);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static bool replace_string(char **target, const char *value)
{
    char *copy;

    copy = copy_string(value);
    if (copy == NULL && value != NULL) {
        return false;
    }

    free(*target);
    *target = copy;
    return true;
}

bool app_options_set_string(char **target, const char *value)
{
    return replace_string(target, value);
}

static bool string_equal_ignore_case(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

static bool rom_candidate_name_matches(const char *filename, const char *rom_name)
{
    char stem[256];
    const char *dot;
    size_t length;

    if (string_equal_ignore_case(filename, rom_name)) {
        return true;
    }

    dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        return false;
    }

    length = (size_t)(dot - filename);
    if (length >= sizeof(stem)) {
        return false;
    }

    memcpy(stem, filename, length);
    stem[length] = '\0';
    return string_equal_ignore_case(stem, rom_name);
}

static bool path_has_size(const char *path, size_t expected_size)
{
    struct stat st;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }

    return st.st_size >= 0 && (size_t)st.st_size == expected_size;
}

static bool join_path(char *out, size_t out_size, const char *dir, const char *filename)
{
    int written;

    if (strcmp(dir, ".") == 0) {
        written = snprintf(out, out_size, "%s", filename);
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, filename);
    }

    return written >= 0 && (size_t)written < out_size;
}

static bool discover_rom_path(
    const char *dir,
    const char *rom_name,
    size_t expected_size,
    char **target)
{
    DIR *handle;
    struct dirent *entry;
    char path[1024];

    if (*target != NULL) {
        return true;
    }

    handle = opendir(dir);
    if (handle == NULL) {
        return true;
    }

    while ((entry = readdir(handle)) != NULL) {
        if (!rom_candidate_name_matches(entry->d_name, rom_name)) {
            continue;
        }
        if (!join_path(path, sizeof(path), dir, entry->d_name)) {
            continue;
        }
        if (!path_has_size(path, expected_size)) {
            continue;
        }

        closedir(handle);
        return replace_string(target, path);
    }

    closedir(handle);
    return true;
}

static bool discover_default_rom_paths(app_options *options)
{
    static const char *const dirs[] = { ".", "rom", "roms" };
    size_t i;

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (!discover_rom_path(
                dirs[i],
                "system",
                C64M_SYSTEM_ROM_SIZE,
                &options->system_rom_path) ||
            !discover_rom_path(
                dirs[i],
                "basic",
                C64M_BASIC_ROM_SIZE,
                &options->basic_rom_path) ||
            !discover_rom_path(
                dirs[i],
                "character",
                C64M_CHARACTER_ROM_SIZE,
                &options->char_rom_path) ||
            !discover_rom_path(
                dirs[i],
                "kernal",
                C64M_KERNAL_ROM_SIZE,
                &options->kernal_rom_path)) {
            return false;
        }
    }

    return true;
}

static float config_get_float(config *cfg, const char *section, const char *key, float default_value)
{
    const char *value;
    char *end;
    float parsed;

    value = config_get(cfg, section, key);
    if (value == NULL) {
        return default_value;
    }

    parsed = strtof(value, &end);
    if (end == value || *end != '\0') {
        return default_value;
    }

    return parsed;
}

static void config_set_float(config *cfg, const char *section, const char *key, float value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%.6g", value);
    config_set(cfg, section, key, buffer);
}

static int parse_bool_value(const char *value, bool *out)
{
    if (strcmp(value, "on") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out = true;
        return 1;
    }

    if (strcmp(value, "off") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = false;
        return 1;
    }

    return 0;
}

static bool apply_disk_spec(app_options *options, const char *spec)
{
    char *end;
    long drive;
    const char *image;

    drive = strtol(spec, &end, 10);
    if (end == spec || *end != '=' || drive < 0 || drive >= C64M_DRIVE_COUNT) {
        fprintf(stderr, "invalid disk spec `%s`; expected <drive>=<image>\n", spec);
        return false;
    }

    image = end + 1;
    if (*image == '\0') {
        fprintf(stderr, "invalid disk spec `%s`; image path is empty\n", spec);
        return false;
    }

    return replace_string(&options->disk_images[drive], image);
}

static void apply_config(app_options *options, config *cfg)
{
    const char *value;
    char key[8];
    int drive;

    if (cfg == NULL) {
        return;
    }

    options->show_leds = config_get_bool(cfg, "ui", "leds", options->show_leds);
    options->remember = config_get_bool(cfg, "config", "Save", options->remember);
    options->scroll_wheel_lines = config_get_int(
        cfg, "config", "scroll_wheel_lines", options->scroll_wheel_lines);
    if (options->scroll_wheel_lines < 1) {
        options->scroll_wheel_lines = 1;
    }
    value = config_get(cfg, "config", "symbol_files");
    if (value != NULL) {
        replace_string(&options->symbol_files, value);
    }
    value = config_get(cfg, "Video", "standard");
    if (value != NULL) {
        replace_string(&options->video_standard, value);
    }
    options->display_width = config_get_int(
        cfg, "Video", "display_width", options->display_width);
    options->display_height = config_get_int(
        cfg, "Video", "display_height", options->display_height);
    options->integer_scale = config_get_bool(
        cfg, "Video", "integer_scale", options->integer_scale);
    options->aspect_correct = config_get_bool(
        cfg, "Video", "aspect_correct", options->aspect_correct);
    value = config_get(cfg, "Video", "filter");
    if (value != NULL) {
        replace_string(&options->video_filter, value);
    }

    options->window_width = config_get_int(
        cfg, "Window", "width", options->window_width);
    options->window_height = config_get_int(
        cfg, "Window", "height", options->window_height);
    options->layout_split_display_right = config_get_float(
        cfg, "Layout", "split_display_right", options->layout_split_display_right);
    options->layout_split_top_bottom = config_get_float(
        cfg, "Layout", "split_top_bottom", options->layout_split_top_bottom);
    options->layout_split_memory_misc = config_get_float(
        cfg, "Layout", "split_memory_misc", options->layout_split_memory_misc);
    options->layout_display_width = config_get_int(
        cfg, "Layout", "display_width", options->layout_display_width);
    options->layout_display_height = config_get_int(
        cfg, "Layout", "display_height", options->layout_display_height);

    value = config_get(cfg, "config", "turbo_speeds");
    if (value != NULL) {
        replace_string(&options->turbo_multipliers, value);
    }

    value = config_get(cfg, "rom", "basic");
    if (value == NULL) {
        value = config_get(cfg, "roms", "basic");
    }
    if (value != NULL) {
        replace_string(&options->basic_rom_path, value);
    }
    value = config_get(cfg, "rom", "char");
    if (value == NULL) {
        value = config_get(cfg, "rom", "character");
    }
    if (value == NULL) {
        value = config_get(cfg, "roms", "char");
    }
    if (value == NULL) {
        value = config_get(cfg, "roms", "character");
    }
    if (value != NULL) {
        replace_string(&options->char_rom_path, value);
    }
    value = config_get(cfg, "rom", "kernal");
    if (value == NULL) {
        value = config_get(cfg, "roms", "kernal");
    }
    if (value != NULL) {
        replace_string(&options->kernal_rom_path, value);
    }
    value = config_get(cfg, "rom", "system");
    if (value == NULL) {
        value = config_get(cfg, "roms", "system");
    }
    if (value != NULL) {
        replace_string(&options->system_rom_path, value);
    }

    for (drive = 0; drive < C64M_DRIVE_COUNT; ++drive) {
        snprintf(key, sizeof(key), "%d", drive);
        value = config_get(cfg, "disk", key);
        if (value != NULL) {
            replace_string(&options->disk_images[drive], value);
        }
    }
}

static bool apply_disk_args(app_options *options, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--disk") == 0 || strcmp(arg, "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a value\n", arg);
                return false;
            }
            if (!apply_disk_spec(options, argv[++i])) {
                return false;
            }
        } else if (strncmp(arg, "--disk=", 7) == 0) {
            if (!apply_disk_spec(options, arg + 7)) {
                return false;
            }
        } else if (strncmp(arg, "-d", 2) == 0 && arg[2] != '\0') {
            if (!apply_disk_spec(options, arg + 2)) {
                return false;
            }
        }
    }

    return true;
}

static bool preparse_ini_options(app_options *options, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--defaults") == 0 || strcmp(arg, "-f") == 0) {
            options->defaults = true;
            options->use_ini = false;
        } else if (strcmp(arg, "--noini") == 0 || strcmp(arg, "-n") == 0) {
            options->use_ini = false;
        } else if (strcmp(arg, "--inifile") == 0 || strcmp(arg, "-i") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a value\n", arg);
                return false;
            }
            if (!replace_string(&options->ini_path, argv[++i])) {
                return false;
            }
        } else if (strncmp(arg, "--inifile=", 10) == 0) {
            if (!replace_string(&options->ini_path, arg + 10)) {
                return false;
            }
        } else if (strncmp(arg, "-i", 2) == 0 && arg[2] != '\0') {
            if (!replace_string(&options->ini_path, arg + 2)) {
                return false;
            }
        }
    }

    return true;
}

static bool parse_command_line_overrides(app_options *options, int argc, char **argv)
{
    int defaults = 0;
    int noini = 0;
    int no_save_ini = 0;
    int remember = 0;
    int save_ini = 0;
    const char *breakpoint = NULL;
    const char *disk = NULL;
    const char *ini_path = NULL;
    const char *leds = NULL;
    const char *turbo = NULL;
    struct argparse argparse;
    const char *const usages[] = {
        "c64m [options]",
        NULL,
    };
    struct argparse_option parse_options[] = {
        OPT_STRING('b', "break", &breakpoint, "install a breakpoint", NULL, 0, 0),
        OPT_BOOLEAN('f', "defaults", &defaults, "use default settings", NULL, 0, OPT_NONEG),
        OPT_STRING('d', "disk", &disk, "1541 drive contains image", NULL, 0, 0),
        OPT_STRING('i', "inifile", &ini_path, "path to an .ini file", NULL, 0, 0),
        OPT_STRING('l', "leds", &leds, "show disk activity LEDs in window based ui", NULL, 0, 0),
        OPT_BOOLEAN('n', "noini", &noini, "do not use an ini file", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('!', "nosaveini", &no_save_ini, "do not save the ini no matter what", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('r', "remember", &remember, "add save at quit to ini file", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('v', "saveini", &save_ini, "save to ini file at quit", NULL, 0, OPT_NONEG),
        OPT_STRING('t', "turbo", &turbo, "comma separated set of turbo multipliers", NULL, 0, 0),
        OPT_HELP(),
        OPT_END(),
    };

    argparse_init(&argparse, parse_options, usages, 0);
    argparse_describe(&argparse, "Commodore 64 emulator", NULL);
    argparse_parse(&argparse, argc, (const char **)argv);

    if (defaults) {
        options->defaults = true;
        options->use_ini = false;
    }
    if (ini_path != NULL) {
        replace_string(&options->ini_path, ini_path);
    }
    if (noini) {
        options->use_ini = false;
    }
    if (breakpoint != NULL) {
        replace_string(&options->breakpoint, breakpoint);
    }
    if (!apply_disk_args(options, argc, argv)) {
        return false;
    }
    if (leds != NULL && !parse_bool_value(leds, &options->show_leds)) {
        fprintf(stderr, "invalid leds value `%s`; expected on or off\n", leds);
        return false;
    }
    if (turbo != NULL) {
        replace_string(&options->turbo_multipliers, turbo);
    }

    if (remember) {
        options->remember = true;
        options->save_ini = true;
    }
    if (save_ini) {
        options->save_ini = true;
    }
    if (no_save_ini) {
        options->no_save_ini = true;
        options->save_ini = false;
    }

    return true;
}

void app_options_init(app_options *options)
{
    memset(options, 0, sizeof(*options));
    options->use_ini = true;
    replace_string(&options->ini_path, C64M_DEFAULT_INI);
    options->show_leds = true;
    options->scroll_wheel_lines = C64M_DEFAULT_SCROLL_WHEEL_LINES;
    replace_string(&options->video_standard, C64M_DEFAULT_VIDEO_STANDARD);
    options->display_width = C64M_DEFAULT_DISPLAY_WIDTH;
    options->display_height = C64M_DEFAULT_DISPLAY_HEIGHT;
    options->integer_scale = true;
    options->aspect_correct = true;
    replace_string(&options->video_filter, C64M_DEFAULT_VIDEO_FILTER);
    options->window_width = 0;
    options->window_height = 0;
    options->layout_split_display_right = C64M_DEFAULT_LAYOUT_SPLIT_DISPLAY_RIGHT;
    options->layout_split_top_bottom = C64M_DEFAULT_LAYOUT_SPLIT_TOP_BOTTOM;
    options->layout_split_memory_misc = C64M_DEFAULT_LAYOUT_SPLIT_MEMORY_MISC;
    options->layout_display_width = C64M_DEFAULT_DISPLAY_WIDTH;
    options->layout_display_height = C64M_DEFAULT_DISPLAY_HEIGHT;
}

bool app_options_apply_ini_file(app_options *options, const char *path)
{
    config *cfg;

    if (options == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    cfg = config_load(path);
    if (cfg == NULL) {
        return false;
    }

    apply_config(options, cfg);
    config_destroy(cfg);
    return true;
}

bool app_options_copy(app_options *dest, const app_options *src)
{
    int i;

    if (dest == NULL || src == NULL) {
        return false;
    }

    app_options_init(dest);
    dest->use_ini = src->use_ini;
    dest->save_ini = src->save_ini;
    dest->remember = src->remember;
    dest->defaults = src->defaults;
    dest->show_leds = src->show_leds;
    dest->no_save_ini = src->no_save_ini;
    dest->scroll_wheel_lines = src->scroll_wheel_lines;
    dest->display_width = src->display_width;
    dest->display_height = src->display_height;
    dest->integer_scale = src->integer_scale;
    dest->aspect_correct = src->aspect_correct;
    dest->window_width = src->window_width;
    dest->window_height = src->window_height;
    dest->layout_split_display_right = src->layout_split_display_right;
    dest->layout_split_top_bottom = src->layout_split_top_bottom;
    dest->layout_split_memory_misc = src->layout_split_memory_misc;
    dest->layout_display_width = src->layout_display_width;
    dest->layout_display_height = src->layout_display_height;

    if (!replace_string(&dest->ini_path, src->ini_path) ||
        !replace_string(&dest->breakpoint, src->breakpoint) ||
        !replace_string(&dest->turbo_multipliers, src->turbo_multipliers) ||
        !replace_string(&dest->symbol_files, src->symbol_files) ||
        !replace_string(&dest->video_standard, src->video_standard) ||
        !replace_string(&dest->video_filter, src->video_filter) ||
        !replace_string(&dest->basic_rom_path, src->basic_rom_path) ||
        !replace_string(&dest->char_rom_path, src->char_rom_path) ||
        !replace_string(&dest->kernal_rom_path, src->kernal_rom_path) ||
        !replace_string(&dest->system_rom_path, src->system_rom_path)) {
        app_options_destroy(dest);
        return false;
    }

    for (i = 0; i < C64M_DRIVE_COUNT; ++i) {
        if (!replace_string(&dest->disk_images[i], src->disk_images[i])) {
            app_options_destroy(dest);
            return false;
        }
    }

    return true;
}

bool app_options_load_startup(app_options *options, int argc, char **argv)
{
    config *cfg = NULL;

    app_options_init(options);

    if (!preparse_ini_options(options, argc, argv)) {
        return false;
    }

    if (options->use_ini) {
        cfg = config_load(options->ini_path);
        if (cfg != NULL) {
            apply_config(options, cfg);
        }
    }

    if (!parse_command_line_overrides(options, argc, argv)) {
        config_destroy(cfg);
        return false;
    }

    if (!options->use_ini && !options->defaults && !discover_default_rom_paths(options)) {
        config_destroy(cfg);
        return false;
    }

    config_destroy(cfg);
    return true;
}

bool app_options_save_shutdown(const app_options *options)
{
    config *cfg;
    bool ok;
    int drive;
    char key[8];

    if (options == NULL || options->no_save_ini || options->ini_path == NULL) {
        return true;
    }

    cfg = config_load(options->ini_path);
    if (cfg == NULL) {
        cfg = config_load(NULL);
    }
    if (cfg == NULL) {
        return false;
    }

    if (options->video_standard != NULL) {
        config_set(cfg, "Video", "standard", options->video_standard);
    }
    config_set_int(cfg, "Video", "display_width", options->display_width);
    config_set_int(cfg, "Video", "display_height", options->display_height);
    config_set_bool(cfg, "Video", "integer_scale", options->integer_scale);
    config_set_bool(cfg, "Video", "aspect_correct", options->aspect_correct);
    if (options->video_filter != NULL) {
        config_set(cfg, "Video", "filter", options->video_filter);
    }

    config_set_bool(cfg, "ui", "leds", options->show_leds);
    config_remove_prefix(cfg, "runtime", "turbo");
    if (options->turbo_multipliers != NULL) {
        config_set(cfg, "config", "turbo_speeds", options->turbo_multipliers);
    }
    config_set_int(cfg, "config", "scroll_wheel_lines", options->scroll_wheel_lines);
    if (options->symbol_files != NULL) {
        config_set(cfg, "config", "symbol_files", options->symbol_files);
    }
    if (options->remember) {
        config_set(cfg, "config", "Save", "yes");
    } else {
        config_remove_prefix(cfg, "config", "Save");
    }

    if (options->window_width > 0 && options->window_height > 0) {
        config_set_int(cfg, "Window", "width", options->window_width);
        config_set_int(cfg, "Window", "height", options->window_height);
    }

    config_set_float(cfg, "Layout", "split_display_right", options->layout_split_display_right);
    config_set_float(cfg, "Layout", "split_top_bottom", options->layout_split_top_bottom);
    config_set_float(cfg, "Layout", "split_memory_misc", options->layout_split_memory_misc);
    config_set_int(cfg, "Layout", "display_width", options->layout_display_width);
    config_set_int(cfg, "Layout", "display_height", options->layout_display_height);

    if (options->basic_rom_path != NULL) {
        config_set(cfg, "roms", "basic", options->basic_rom_path);
    }
    if (options->char_rom_path != NULL) {
        config_set(cfg, "roms", "character", options->char_rom_path);
    }
    if (options->kernal_rom_path != NULL) {
        config_set(cfg, "roms", "kernal", options->kernal_rom_path);
    }
    if (options->system_rom_path != NULL) {
        config_set(cfg, "roms", "system", options->system_rom_path);
    }

    for (drive = 0; drive < C64M_DRIVE_COUNT; ++drive) {
        if (options->disk_images[drive] != NULL) {
            snprintf(key, sizeof(key), "%d", drive);
            config_set(cfg, "disk", key, options->disk_images[drive]);
        }
    }

    ok = config_save(cfg, options->ini_path);
    config_destroy(cfg);
    return ok;
}

void app_options_destroy(app_options *options)
{
    int i;

    if (options == NULL) {
        return;
    }

    free(options->ini_path);
    free(options->breakpoint);
    free(options->turbo_multipliers);
    free(options->symbol_files);
    free(options->video_standard);
    free(options->video_filter);
    free(options->basic_rom_path);
    free(options->char_rom_path);
    free(options->kernal_rom_path);
    free(options->system_rom_path);
    for (i = 0; i < C64M_DRIVE_COUNT; ++i) {
        free(options->disk_images[i]);
    }

    memset(options, 0, sizeof(*options));
}
