#include "app_options.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "argparse.h"
#include "config.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define C64M_DEFAULT_INI "c64m.ini"
#define C64M_DEFAULT_VIDEO_STANDARD "NTSC"
#define C64M_DEFAULT_KEYBOARD_JOYSTICK_LAYOUT "numpad"
#define C64M_DEFAULT_SCROLL_WHEEL_LINES 3
#define C64M_DEFAULT_LAYOUT_SPLIT_DISPLAY_RIGHT 0.62f
#define C64M_DEFAULT_LAYOUT_SPLIT_TOP_BOTTOM 0.58f
#define C64M_DEFAULT_LAYOUT_SPLIT_MEMORY_MISC 0.55f
#define C64M_SYSTEM_ROM_SIZE 16384
#define C64M_BASIC_ROM_SIZE 8192
#define C64M_KERNAL_ROM_SIZE 8192
#define C64M_CHARACTER_ROM_SIZE 4096

#if defined(_WIN32)
#define C64M_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#define c64m_getcwd _getcwd
#else
#define C64M_STAT_ISREG(mode) S_ISREG(mode)
#define c64m_getcwd getcwd
#endif

static void normalize_path_separators(char *path)
{
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

static bool path_is_absolute(const char *path)
{
    return path != NULL &&
        (path[0] == '/'
#if defined(_WIN32)
         || path[0] == '\\' ||
         (isalpha((unsigned char)path[0]) && path[1] == ':' &&
          (path[2] == '/' || path[2] == '\\'))
#endif
        );
}

static bool copy_path(char *out, size_t out_size, const char *path)
{
    int written;

    if (out == NULL || out_size == 0 || path == NULL) {
        return false;
    }

    written = snprintf(out, out_size, "%s", path);
    if (written < 0 || (size_t)written >= out_size) {
        return false;
    }
    normalize_path_separators(out);
    return true;
}

static bool join_path_buffer(char *out, size_t out_size, const char *dir, const char *path)
{
    int written;

    if (out == NULL || out_size == 0 || dir == NULL || path == NULL) {
        return false;
    }
    if (dir[0] == '\0' || strcmp(dir, ".") == 0) {
        written = snprintf(out, out_size, "%s", path);
    } else if (strcmp(dir, "/") == 0) {
        written = snprintf(out, out_size, "/%s", path);
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, path);
    }

    return written >= 0 && (size_t)written < out_size;
}

static bool copy_resolved_or_original(char *out, size_t out_size, const char *path)
{
    char resolved[PATH_MAX];

#if defined(_WIN32)
    if (_fullpath(resolved, path, sizeof(resolved)) != NULL) {
        return copy_path(out, out_size, resolved);
    }
#else
    if (realpath(path, resolved) != NULL) {
        return copy_path(out, out_size, resolved);
    }
#endif
    return copy_path(out, out_size, path);
}

static bool ini_directory_absolute(const app_options *options, char *out, size_t out_size)
{
    char cwd[PATH_MAX];
    char ini_copy[PATH_MAX];
    char joined[PATH_MAX];
    char *slash;

    if (out == NULL || out_size == 0) {
        return false;
    }
    if (c64m_getcwd(cwd, sizeof(cwd)) == NULL) {
        return copy_path(out, out_size, ".");
    }
    normalize_path_separators(cwd);
    if (options == NULL || options->ini_path == NULL || options->ini_path[0] == '\0') {
        return copy_path(out, out_size, cwd);
    }

    if (!copy_path(ini_copy, sizeof(ini_copy), options->ini_path)) {
        return false;
    }

    slash = strrchr(ini_copy, '/');
    if (slash == NULL) {
        return copy_path(out, out_size, cwd);
    }
    if (slash == ini_copy) {
        return copy_path(out, out_size, "/");
    }
    *slash = '\0';
    if (path_is_absolute(ini_copy)) {
        return copy_resolved_or_original(out, out_size, ini_copy);
    }
    if (!join_path_buffer(joined, sizeof(joined), cwd, ini_copy)) {
        return false;
    }
    return copy_resolved_or_original(out, out_size, joined);
}

static bool path_absolute_from_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size)
{
    char ini_dir[PATH_MAX];
    char joined[PATH_MAX];

    if (path == NULL || path[0] == '\0') {
        return copy_path(out, out_size, "");
    }
    if (path_is_absolute(path)) {
        return copy_resolved_or_original(out, out_size, path);
    }
    if (!ini_directory_absolute(options, ini_dir, sizeof(ini_dir))) {
        return false;
    }
    if (!join_path_buffer(joined, sizeof(joined), ini_dir, path)) {
        return false;
    }
    return copy_resolved_or_original(out, out_size, joined);
}

bool app_options_path_absolute_from_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size)
{
    return path_absolute_from_ini(options, path, out, out_size);
}

static size_t path_component_length(const char *path)
{
    const char *slash;

    slash = strchr(path, '/');
    return slash != NULL ? (size_t)(slash - path) : strlen(path);
}

static bool append_relative_up(char *out, size_t out_size, size_t *used)
{
    int written;

    written = snprintf(out + *used, out_size - *used, "%s..", *used > 0 ? "/" : "");
    if (written < 0 || (size_t)written >= out_size - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool relative_path_from_dir(
    const char *base_dir,
    const char *abs_path,
    char *out,
    size_t out_size)
{
    const char *base_cursor;
    const char *path_cursor;
    const char *base_remainder;
    const char *path_remainder;
    size_t used = 0;

    if (!path_is_absolute(base_dir) || !path_is_absolute(abs_path)) {
        return copy_path(out, out_size, abs_path);
    }

    base_cursor = base_dir;
    path_cursor = abs_path;
    while (*base_cursor == '/' && *path_cursor == '/') {
        base_cursor++;
        path_cursor++;
    }

    base_remainder = base_cursor;
    path_remainder = path_cursor;
    while (*base_cursor != '\0' && *path_cursor != '\0') {
        size_t base_len = path_component_length(base_cursor);
        size_t path_len = path_component_length(path_cursor);

        if (base_len != path_len || strncmp(base_cursor, path_cursor, base_len) != 0) {
            break;
        }

        base_cursor += base_len;
        path_cursor += path_len;
        if (*base_cursor == '/') {
            base_cursor++;
        }
        if (*path_cursor == '/') {
            path_cursor++;
        }
        base_remainder = base_cursor;
        path_remainder = path_cursor;
    }

    while (*base_remainder != '\0') {
        size_t component_len = path_component_length(base_remainder);
        if (component_len > 0 && !append_relative_up(out, out_size, &used)) {
            return false;
        }
        base_remainder += component_len;
        if (*base_remainder == '/') {
            base_remainder++;
        }
    }

    if (*path_remainder != '\0') {
        int written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "/" : "", path_remainder);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
    } else if (used == 0) {
        return copy_path(out, out_size, ".");
    }

    return true;
}

bool app_options_path_relative_to_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size)
{
    char ini_dir[PATH_MAX];
    char resolved[PATH_MAX];

    if (path == NULL || path[0] == '\0') {
        return copy_path(out, out_size, "");
    }
    if (!path_is_absolute(path)) {
        return copy_path(out, out_size, path);
    }
    if (!ini_directory_absolute(options, ini_dir, sizeof(ini_dir))) {
        return false;
    }
#if defined(_WIN32)
    if (_fullpath(resolved, path, sizeof(resolved)) != NULL) {
        normalize_path_separators(resolved);
        return relative_path_from_dir(ini_dir, resolved, out, out_size);
    }
#else
    if (realpath(path, resolved) != NULL) {
        return relative_path_from_dir(ini_dir, resolved, out, out_size);
    }
#endif
    return relative_path_from_dir(ini_dir, path, out, out_size);
}

static bool transform_symbol_files(
    const app_options *options,
    const char *symbol_files,
    bool absolute,
    char *out,
    size_t out_size)
{
    const char *cursor;
    size_t used = 0;

    if (out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    cursor = symbol_files != NULL ? symbol_files : "";
    while (*cursor != '\0') {
        const char *start;
        const char *end;
        char path[PATH_MAX];
        char transformed[PATH_MAX];
        size_t length;
        int written;

        while (*cursor == ',' || isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }

        length = (size_t)(end - start);
        if (length == 0) {
            continue;
        }
        if (length >= sizeof(path)) {
            return false;
        }
        memcpy(path, start, length);
        path[length] = '\0';

        if (absolute) {
            if (!path_absolute_from_ini(options, path, transformed, sizeof(transformed))) {
                return false;
            }
        } else {
            if (!app_options_path_relative_to_ini(options, path, transformed, sizeof(transformed))) {
                return false;
            }
        }

        written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "," : "", transformed);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }

    return true;
}

bool app_options_symbol_files_absolute(
    const app_options *options,
    char *out,
    size_t out_size)
{
    if (options == NULL) {
        return false;
    }
    return transform_symbol_files(options, options->symbol_files, true, out, out_size);
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

    if (stat(path, &st) != 0 || !C64M_STAT_ISREG(st.st_mode)) {
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
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search_path[1024];
    char path[1024];
#else
    DIR *handle;
    struct dirent *entry;
    char path[1024];
#endif

    if (*target != NULL) {
        return true;
    }

#if defined(_WIN32)
    if (!join_path(search_path, sizeof(search_path), dir, "*")) {
        return true;
    }
    handle = FindFirstFileA(search_path, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return true;
    }

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            !rom_candidate_name_matches(data.cFileName, rom_name)) {
            continue;
        }
        if (!join_path(path, sizeof(path), dir, data.cFileName)) {
            continue;
        }
        if (!path_has_size(path, expected_size)) {
            continue;
        }

        FindClose(handle);
        return replace_string(target, path);
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
#else
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
#endif
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
                &options->kernal_rom_path) ||
            !discover_rom_path(
                dirs[i],
                "1541",
                C64M_SYSTEM_ROM_SIZE,
                &options->rom1541_path)) {
            return false;
        }
    }

    {
        /* Match the ini-load default: a lone combined system ROM means single-ROM
           mode; a basic+kernal pair means separate. */
        bool have_system = options->system_rom_path != NULL && options->system_rom_path[0] != '\0';
        bool have_basic = options->basic_rom_path != NULL && options->basic_rom_path[0] != '\0';
        bool have_kernal = options->kernal_rom_path != NULL && options->kernal_rom_path[0] != '\0';
        options->rom_single_system = have_system && !(have_basic && have_kernal);
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

/* --- disk slot helpers --------------------------------------------------- */

static void disk_slot_free(app_disk_slot *slot)
{
    int i;

    for (i = 0; i < slot->count; ++i) {
        free(slot->paths[i]);
    }
    free(slot->paths);
    free(slot->writable);
    slot->paths = NULL;
    slot->writable = NULL;
    slot->count = 0;
    slot->current = 0;
}

static bool disk_slot_append(app_disk_slot *slot, const char *path)
{
    char **grown;
    bool *grown_writable;
    char *copy;

    copy = copy_string(path);
    if (copy == NULL) {
        return false;
    }

    grown = (char **)realloc(slot->paths, (size_t)(slot->count + 1) * sizeof(char *));
    if (grown == NULL) {
        free(copy);
        return false;
    }
    slot->paths = grown;

    grown_writable = (bool *)realloc(slot->writable, (size_t)(slot->count + 1) * sizeof(bool));
    if (grown_writable == NULL) {
        free(copy);
        return false;
    }

    grown[slot->count] = copy;
    slot->writable = grown_writable;
    slot->writable[slot->count] = false;
    slot->count++;
    return true;
}

static void disk_slot_parse_writable_list(app_disk_slot *slot, const char *spec)
{
    const char *cursor = spec;
    int index = 0;

    if (slot == NULL || spec == NULL) {
        return;
    }

    while (*cursor != '\0' && index < slot->count) {
        const char *start;
        const char *end;
        size_t len;

        while (*cursor == ' ') {
            cursor++;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        while (end > start && end[-1] == ' ') {
            end--;
        }
        len = (size_t)(end - start);
        slot->writable[index] =
            (len == 1 && start[0] == '1') ||
            (len == 4 && strncmp(start, "true", 4) == 0) ||
            (len == 2 && strncmp(start, "rw", 2) == 0);
        index++;
        if (*cursor == ',') {
            cursor++;
        }
    }
}

static bool disk_slot_format_writable_list(
    const app_disk_slot *slot,
    char *out,
    size_t out_size)
{
    size_t used = 0;
    int j;

    if (slot == NULL || out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    for (j = 0; j < slot->count; ++j) {
        int written = snprintf(
            out + used,
            out_size - used,
            "%s%d",
            used > 0 ? "," : "",
            slot->writable[j] ? 1 : 0);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }
    return true;
}

/*
 * Parse a comma-separated list of paths into slot (replacing any prior
 * contents).  When resolve_options is non-NULL each path is resolved
 * relative to the INI directory; otherwise paths are kept as-is.
 */
static bool disk_slot_parse_list(
    app_disk_slot *slot,
    const app_options *resolve_options,
    const char *spec)
{
    const char *cursor = spec;

    disk_slot_free(slot);

    while (*cursor != '\0') {
        const char *start;
        const char *end;
        char path[PATH_MAX];
        size_t len;

        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        while (end > start && end[-1] == ' ') {
            end--;
        }

        len = (size_t)(end - start);
        if (len == 0) {
            if (*cursor == ',') {
                cursor++;
            }
            continue;
        }
        if (len >= sizeof(path)) {
            return false;
        }
        memcpy(path, start, len);
        path[len] = '\0';

        if (resolve_options != NULL) {
            char abs_path[PATH_MAX];
            if (path_absolute_from_ini(resolve_options, path, abs_path, sizeof(abs_path))) {
                if (!disk_slot_append(slot, abs_path)) {
                    return false;
                }
            } else {
                if (!disk_slot_append(slot, path)) {
                    return false;
                }
            }
        } else {
            if (!disk_slot_append(slot, path)) {
                return false;
            }
        }

        if (*cursor == ',') {
            cursor++;
        }
    }

    return true;
}

/*
 * Write slot paths as a comma-separated string into out, converting each
 * absolute path to be relative to the INI directory.
 */
static bool disk_slot_format_list(
    const app_disk_slot *slot,
    const app_options *options,
    char *out,
    size_t out_size)
{
    size_t used = 0;
    int j;

    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    for (j = 0; j < slot->count; ++j) {
        char rel[PATH_MAX];
        int written;

        if (!app_options_path_relative_to_ini(options, slot->paths[j], rel, sizeof(rel))) {
            if (!copy_path(rel, sizeof(rel), slot->paths[j])) {
                return false;
            }
        }

        written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "," : "", rel);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }

    return true;
}

/* Public slot API ---------------------------------------------------------- */

bool app_disk_slot_set(app_disk_slot *slot, const char *path)
{
    disk_slot_free(slot);
    if (path == NULL || path[0] == '\0') {
        return true;
    }
    return disk_slot_append(slot, path);
}

void app_disk_slot_clear(app_disk_slot *slot)
{
    disk_slot_free(slot);
}

bool app_disk_slot_copy(app_disk_slot *dest, const app_disk_slot *src)
{
    int i;

    disk_slot_free(dest);
    for (i = 0; i < src->count; ++i) {
        if (!disk_slot_append(dest, src->paths[i])) {
            disk_slot_free(dest);
            return false;
        }
        dest->writable[i] = src->writable != NULL && src->writable[i];
    }
    dest->current = src->current;
    return true;
}

const char *app_disk_slot_eject_current(app_disk_slot *slot)
{
    int i;

    if (slot == NULL || slot->count == 0) {
        return NULL;
    }

    free(slot->paths[slot->current]);
    for (i = slot->current; i < slot->count - 1; ++i) {
        slot->paths[i] = slot->paths[i + 1];
        slot->writable[i] = slot->writable[i + 1];
    }
    slot->paths[slot->count - 1] = NULL;
    slot->count--;

    if (slot->count == 0) {
        free(slot->paths);
        free(slot->writable);
        slot->paths = NULL;
        slot->writable = NULL;
        slot->current = 0;
        return NULL;
    }

    if (slot->current >= slot->count) {
        slot->current = 0;
    }
    return slot->paths[slot->current];
}

bool app_disk_slot_add_after_current(app_disk_slot *slot, const char *path)
{
    char **grown;
    bool *grown_writable;
    char *copy;
    int insert_at;
    int i;

    if (slot == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    copy = copy_string(path);
    if (copy == NULL) {
        return false;
    }

    grown = (char **)realloc(slot->paths, (size_t)(slot->count + 1) * sizeof(char *));
    if (grown == NULL) {
        free(copy);
        return false;
    }
    slot->paths = grown;

    grown_writable = (bool *)realloc(slot->writable, (size_t)(slot->count + 1) * sizeof(bool));
    if (grown_writable == NULL) {
        free(copy);
        return false;
    }
    slot->writable = grown_writable;

    insert_at = slot->count == 0 ? 0 : slot->current + 1;
    for (i = slot->count; i > insert_at; --i) {
        slot->paths[i] = slot->paths[i - 1];
        slot->writable[i] = slot->writable[i - 1];
    }
    slot->paths[insert_at] = copy;
    slot->writable[insert_at] = false;
    slot->count++;
    return true;
}

const char *app_disk_slot_select(app_disk_slot *slot, int index)
{
    if (slot == NULL || index < 0 || index >= slot->count) {
        return NULL;
    }
    slot->current = index;
    return slot->paths[index];
}

bool app_disk_slot_current_writable(const app_disk_slot *slot)
{
    if (slot == NULL || slot->count == 0 || slot->current < 0 || slot->current >= slot->count ||
        slot->writable == NULL) {
        return false;
    }
    return slot->writable[slot->current];
}

bool app_disk_slot_set_current_writable(app_disk_slot *slot, bool writable)
{
    if (slot == NULL || slot->count == 0 || slot->current < 0 || slot->current >= slot->count ||
        slot->writable == NULL) {
        return false;
    }
    slot->writable[slot->current] = writable;
    return true;
}

/* --- disk spec parsing for --disk CLI arg --------------------------------- */

static bool apply_disk_spec(app_options *options, const char *spec)
{
    char *end;
    long drive;
    const char *images;

    drive = strtol(spec, &end, 10);
    if (end == spec || *end != '=' || drive < 0 || drive >= C64M_DRIVE_COUNT) {
        fprintf(stderr, "invalid disk spec `%s`; expected <drive>=<image>\n", spec);
        return false;
    }

    images = end + 1;
    if (*images == '\0') {
        fprintf(stderr, "invalid disk spec `%s`; image path is empty\n", spec);
        return false;
    }

    /* Command-line paths stay as-is (relative to CWD). */
    return disk_slot_parse_list(&options->disk_slots[drive], NULL, images);
}

/* Keys in the [browse] section, indexed by frontend_browse_slot / APP_BROWSE_DIR
   order. Keep in sync with frontend_browse_slot in frontend/frontend.h. */
static const char *const browse_dir_keys[APP_BROWSE_DIR_COUNT] = {
    "assembler", "disk", "program", "basic", "text", "snapshot"
};
/* Index of the "snapshot" slot within browse_dir_keys / browse_dirs. Doubles as
   the quicksave folder (see the frontend Paths tab). */
#define APP_BROWSE_DIR_SNAPSHOT 5

/* Write the ROM file paths and the single/separate-ROM flag into cfg. Empty or
   unset paths remove their key so a cleared field disappears from the INI. Shared
   by the full-shutdown save and the "Save Paths Only" save. */
static void config_write_rom_config(config *cfg, const app_options *options)
{
    struct {
        const char *key;
        const char *value;
    } roms[] = {
        { "basic", options->basic_rom_path },
        { "character", options->char_rom_path },
        { "kernal", options->kernal_rom_path },
        { "system", options->system_rom_path },
        { "1541", options->rom1541_path },
    };
    size_t i;

    for (i = 0; i < sizeof(roms) / sizeof(roms[0]); ++i) {
        if (roms[i].value != NULL && roms[i].value[0] != '\0') {
            config_set(cfg, "roms", roms[i].key, roms[i].value);
        } else {
            config_remove_prefix(cfg, "roms", roms[i].key);
        }
    }
    config_set_bool(cfg, "roms", "single_system", options->rom_single_system);
}

static void apply_config(app_options *options, config *cfg)
{
    const char *value;
    char key[32];
    int drive;
    int i;

    if (cfg == NULL) {
        return;
    }

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
    value = config_get(cfg, "input", "keyboard_joystick_layout");
    if (value != NULL) {
        replace_string(&options->keyboard_joystick_layout, value);
    }
    options->keyboard_joystick_port = config_get_int(
        cfg, "input", "keyboard_joystick_port", options->keyboard_joystick_port);
    if (options->keyboard_joystick_port < 0 || options->keyboard_joystick_port > 2) {
        options->keyboard_joystick_port = 0;
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
    value = config_get(cfg, "rom", "1541");
    if (value == NULL) {
        value = config_get(cfg, "roms", "1541");
    }
    if (value != NULL) {
        replace_string(&options->rom1541_path, value);
    }

    {
        /* When the flag is absent, derive it: a lone combined system ROM implies
           single-ROM mode; a basic+kernal pair (with or without system) implies
           the separate-ROM mode. */
        bool have_system = options->system_rom_path != NULL && options->system_rom_path[0] != '\0';
        bool have_basic = options->basic_rom_path != NULL && options->basic_rom_path[0] != '\0';
        bool have_kernal = options->kernal_rom_path != NULL && options->kernal_rom_path[0] != '\0';
        bool default_single = have_system && !(have_basic && have_kernal);
        options->rom_single_system = config_get_bool(
            cfg, "roms", "single_system",
            config_get_bool(cfg, "rom", "single_system", default_single));
    }

    options->emulate_1541 = config_get_bool(cfg, "disk", "emulate_1541", options->emulate_1541);

    value = config_get(cfg, "assembler", "file");
    if (value != NULL) {
        char abs_path[PATH_MAX];
        if (path_absolute_from_ini(options, value, abs_path, sizeof(abs_path))) {
            replace_string(&options->assembler_file, abs_path);
        } else {
            replace_string(&options->assembler_file, value);
        }
    }
    value = config_get(cfg, "assembler", "address");
    if (value != NULL) {
        replace_string(&options->assembler_address, value);
    }
    value = config_get(cfg, "assembler", "run_address");
    if (value != NULL) {
        replace_string(&options->assembler_run_address, value);
    }
    options->assembler_auto_run = config_get_bool(
        cfg, "assembler", "auto_run", options->assembler_auto_run);
    options->assembler_reset_first = config_get_bool(
        cfg, "assembler", "reset", options->assembler_reset_first);
    options->assembler_rearm_oneshots = config_get_bool(
        cfg, "assembler", "rearm_oneshots", options->assembler_rearm_oneshots);

    for (drive = 0; drive < C64M_DRIVE_COUNT; ++drive) {
        snprintf(key, sizeof(key), "%d", drive);
        value = config_get(cfg, "disk", key);
        if (value != NULL) {
            disk_slot_parse_list(&options->disk_slots[drive], options, value);
        }
        snprintf(key, sizeof(key), "%d_writable", drive);
        value = config_get(cfg, "disk", key);
        if (value != NULL) {
            disk_slot_parse_writable_list(&options->disk_slots[drive], value);
        }
    }

    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        value = config_get(cfg, "browse", browse_dir_keys[i]);
        if (value != NULL && value[0] != '\0') {
            replace_string(&options->browse_dirs[i], value);
        }
    }
    /* Migrate the pre-unification [state] quicksave_folder into the snapshot slot
       when no [browse] snapshot is present. */
    if (options->browse_dirs[APP_BROWSE_DIR_SNAPSHOT] == NULL ||
            options->browse_dirs[APP_BROWSE_DIR_SNAPSHOT][0] == '\0') {
        value = config_get(cfg, "state", "quicksave_folder");
        if (value != NULL && value[0] != '\0') {
            replace_string(&options->browse_dirs[APP_BROWSE_DIR_SNAPSHOT], value);
        }
    }
}

static bool apply_disk_args(app_options *options, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (arg == NULL) {
            break;
        }
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

static bool apply_video_standard_arg(app_options *options, const char *value)
{
    if (value == NULL) {
        return true;
    }
    if (strcmp(value, "PAL") == 0 || strcmp(value, "NTSC") == 0) {
        return replace_string(&options->video_standard, value);
    }

    fprintf(stderr, "invalid video standard `%s`; expected PAL or NTSC\n", value);
    return false;
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
    int audio_smoke = 0;
    int autorun = 0;
    int control_port = 0;
    int headless = 0;
    int video_pal = 0;
    int video_ntsc = 0;
    int kbdjoy_port = -1;
    const char *kbdjoy_layout = NULL;
    float audio_record_start = 0.0f;
    float audio_record_duration = 0.0f;
    const char *basic_path = NULL;
    const char *breakpoint = NULL;
    const char *crt_path = NULL;
    const char *disk = NULL;
    const char *ini_path = NULL;
    const char *prg_path = NULL;
    const char *audio_record_path = NULL;
    const char *turbo = NULL;
    const char *video_standard = NULL;
    struct argparse argparse;
    const char *const usages[] = {
        "c64m [options]",
        NULL,
    };
    struct argparse_option parse_options[] = {
        OPT_BOOLEAN('A', "audio-smoke", &audio_smoke, "emit 440 Hz tone to verify audio path", NULL, 0, OPT_NONEG),
        OPT_STRING('\0', "audio-record", &audio_record_path, "record runtime mono audio to WAV", NULL, 0, 0),
        OPT_FLOAT('\0', "audio-record-start", &audio_record_start, "recording start time in seconds", NULL, 0, 0),
        OPT_FLOAT('\0', "audio-record-duration", &audio_record_duration, "recording duration in seconds", NULL, 0, 0),
        OPT_BOOLEAN('a', "autorun", &autorun, "run automatically after load", NULL, 0, OPT_NONEG),
        OPT_STRING('B', "basic", &basic_path, "load file as BASIC program at startup", NULL, 0, 0),
        OPT_STRING('b', "break", &breakpoint, "install a breakpoint", NULL, 0, 0),
        OPT_STRING('\0', "crt", &crt_path, "load CRT cartridge at startup", NULL, 0, 0),
        OPT_INTEGER('\0', "control-port", &control_port, "enable localhost control server on port", NULL, 0, 0),
        OPT_BOOLEAN('\0', "headless", &headless, "run without creating a window; requires --control-port", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('f', "defaults", &defaults, "use default settings", NULL, 0, OPT_NONEG),
        OPT_STRING('d', "disk", &disk, "1541 drive image; format <drive>=<image>", NULL, 0, 0),
        OPT_STRING('i', "inifile", &ini_path, "path to an .ini file", NULL, 0, 0),
        OPT_BOOLEAN('n', "noini", &noini, "do not use an ini file", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('!', "nosaveini", &no_save_ini, "do not save the ini no matter what", NULL, 0, OPT_NONEG),
        OPT_STRING('p', "prg", &prg_path, "load file as PRG at startup", NULL, 0, 0),
        OPT_BOOLEAN('P', "pal", &video_pal, "use PAL video timing", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('N', "ntsc", &video_ntsc, "use NTSC video timing", NULL, 0, OPT_NONEG),
        OPT_STRING('\0', "video", &video_standard, "video standard: PAL or NTSC", NULL, 0, 0),
        OPT_INTEGER('\0', "kbdjoy", &kbdjoy_port, "drive keyboard joystick on C64 port: 0 off, 1 or 2", NULL, 0, 0),
        OPT_STRING('\0', "kbdjoy-layout", &kbdjoy_layout, "keyboard joystick layout: numpad or wasd", NULL, 0, 0),
        OPT_BOOLEAN('r', "remember", &remember, "add save at quit to ini file", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('v', "saveini", &save_ini, "save to ini file at quit", NULL, 0, OPT_NONEG),
        OPT_STRING('t', "turbo", &turbo, "comma separated set of turbo multipliers", NULL, 0, 0),
        OPT_HELP(),
        OPT_END(),
    };

    argparse_init(&argparse, parse_options, usages, 0);
    argparse_describe(&argparse, "Commodore 64 emulator written by Codex and Claude Code, produced by Stefan Wessels, 2026.", NULL);
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
    if (prg_path != NULL) {
        replace_string(&options->prg_path, prg_path);
    }
    if (crt_path != NULL) {
        replace_string(&options->crt_path, crt_path);
    }
    if (basic_path != NULL) {
        replace_string(&options->basic_path, basic_path);
    }
    if (turbo != NULL) {
        replace_string(&options->turbo_multipliers, turbo);
    }
    if (!apply_video_standard_arg(options, video_standard)) {
        return false;
    }
    if (video_pal && !apply_video_standard_arg(options, "PAL")) {
        return false;
    }
    if (video_ntsc && !apply_video_standard_arg(options, "NTSC")) {
        return false;
    }
    if (kbdjoy_port >= 0) {
        if (kbdjoy_port > 2) {
            fprintf(stderr, "--kbdjoy expects 0, 1, or 2\n");
            return false;
        }
        options->keyboard_joystick_port = kbdjoy_port;
    }
    if (kbdjoy_layout != NULL) {
        if (strcmp(kbdjoy_layout, "numpad") != 0 && strcmp(kbdjoy_layout, "wasd") != 0) {
            fprintf(stderr, "--kbdjoy-layout expects 'numpad' or 'wasd'\n");
            return false;
        }
        replace_string(&options->keyboard_joystick_layout, kbdjoy_layout);
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
    if (audio_smoke) {
        options->audio_smoke = true;
    }
    if (audio_record_path != NULL) {
        replace_string(&options->audio_record_path, audio_record_path);
    }
    if (audio_record_start > 0.0f) {
        options->audio_record_start_seconds = audio_record_start;
    }
    if (audio_record_duration > 0.0f) {
        options->audio_record_duration_seconds = audio_record_duration;
    }
    if (autorun) {
        options->autorun = true;
    }
    if (control_port < 0 || control_port > 65535) {
        fprintf(stderr, "invalid control port `%d`; expected 0..65535\n", control_port);
        return false;
    }
    if (control_port > 0) {
        options->control_port = control_port;
    }
    if (headless) {
        options->headless = true;
    }
    if (options->headless && options->control_port <= 0) {
        fprintf(stderr, "--headless requires --control-port PORT\n");
        return false;
    }

    return true;
}

void app_options_init(app_options *options)
{
    memset(options, 0, sizeof(*options));
    options->use_ini = true;
    replace_string(&options->ini_path, C64M_DEFAULT_INI);
    options->scroll_wheel_lines = C64M_DEFAULT_SCROLL_WHEEL_LINES;
    replace_string(&options->video_standard, C64M_DEFAULT_VIDEO_STANDARD);
    replace_string(&options->keyboard_joystick_layout,
                   C64M_DEFAULT_KEYBOARD_JOYSTICK_LAYOUT);
    options->keyboard_joystick_port = 0;
    options->window_width = 0;
    options->window_height = 0;
    options->layout_split_display_right = C64M_DEFAULT_LAYOUT_SPLIT_DISPLAY_RIGHT;
    options->layout_split_top_bottom = C64M_DEFAULT_LAYOUT_SPLIT_TOP_BOTTOM;
    options->layout_split_memory_misc = C64M_DEFAULT_LAYOUT_SPLIT_MEMORY_MISC;
    options->assembler_auto_run = false;
    options->assembler_reset_first = true;
    options->assembler_rearm_oneshots = false;
    options->control_port = 0;
    options->headless = false;
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
    dest->no_save_ini = src->no_save_ini;
    dest->autorun = src->autorun;
    dest->emulate_1541 = src->emulate_1541;
    dest->rom_single_system = src->rom_single_system;
    dest->audio_smoke = src->audio_smoke;
    dest->audio_record_start_seconds = src->audio_record_start_seconds;
    dest->audio_record_duration_seconds = src->audio_record_duration_seconds;
    dest->scroll_wheel_lines = src->scroll_wheel_lines;
    dest->window_width = src->window_width;
    dest->window_height = src->window_height;
    dest->layout_split_display_right = src->layout_split_display_right;
    dest->layout_split_top_bottom = src->layout_split_top_bottom;
    dest->layout_split_memory_misc = src->layout_split_memory_misc;

    dest->assembler_auto_run = src->assembler_auto_run;
    dest->assembler_reset_first = src->assembler_reset_first;
    dest->assembler_rearm_oneshots = src->assembler_rearm_oneshots;
    dest->control_port = src->control_port;
    dest->headless = src->headless;
    dest->keyboard_joystick_port = src->keyboard_joystick_port;

    if (!replace_string(&dest->keyboard_joystick_layout, src->keyboard_joystick_layout) ||
        !replace_string(&dest->ini_path, src->ini_path) ||
        !replace_string(&dest->breakpoint, src->breakpoint) ||
        !replace_string(&dest->turbo_multipliers, src->turbo_multipliers) ||
        !replace_string(&dest->symbol_files, src->symbol_files) ||
        !replace_string(&dest->video_standard, src->video_standard) ||
        !replace_string(&dest->basic_rom_path, src->basic_rom_path) ||
        !replace_string(&dest->char_rom_path, src->char_rom_path) ||
        !replace_string(&dest->kernal_rom_path, src->kernal_rom_path) ||
        !replace_string(&dest->system_rom_path, src->system_rom_path) ||
        !replace_string(&dest->rom1541_path, src->rom1541_path) ||
        !replace_string(&dest->crt_path, src->crt_path) ||
        !replace_string(&dest->prg_path, src->prg_path) ||
        !replace_string(&dest->basic_path, src->basic_path) ||
        !replace_string(&dest->audio_record_path, src->audio_record_path) ||
        !replace_string(&dest->assembler_file, src->assembler_file) ||
        !replace_string(&dest->assembler_address, src->assembler_address) ||
        !replace_string(&dest->assembler_run_address, src->assembler_run_address)) {
        app_options_destroy(dest);
        return false;
    }

    for (i = 0; i < C64M_DRIVE_COUNT; ++i) {
        if (!app_disk_slot_copy(&dest->disk_slots[i], &src->disk_slots[i])) {
            app_options_destroy(dest);
            return false;
        }
    }

    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        if (!replace_string(&dest->browse_dirs[i], src->browse_dirs[i])) {
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

    if (!options->defaults &&
        (!options->use_ini || cfg == NULL) &&
        !discover_default_rom_paths(options)) {
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
    char key[32];
    char relative_symbol_files[1024];

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
    config_remove_prefix(cfg, "Video", "display_width");
    config_remove_prefix(cfg, "Video", "display_height");
    if (options->keyboard_joystick_layout != NULL) {
        config_set(cfg, "input", "keyboard_joystick_layout",
                   options->keyboard_joystick_layout);
    }
    config_set_int(cfg, "input", "keyboard_joystick_port",
                   options->keyboard_joystick_port);

    config_remove_prefix(cfg, "runtime", "turbo");
    if (options->turbo_multipliers != NULL) {
        config_set(cfg, "config", "turbo_speeds", options->turbo_multipliers);
    }
    config_set_int(cfg, "config", "scroll_wheel_lines", options->scroll_wheel_lines);
    /* The snapshot folder is now [browse] snapshot; drop the legacy key. */
    config_remove_prefix(cfg, "state", "quicksave_folder");
    if (options->symbol_files != NULL &&
        transform_symbol_files(options, options->symbol_files, false, relative_symbol_files, sizeof(relative_symbol_files))) {
        config_set(cfg, "config", "symbol_files", relative_symbol_files);
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
    config_remove_prefix(cfg, "Layout", "display_width");
    config_remove_prefix(cfg, "Layout", "display_height");

    config_write_rom_config(cfg, options);

    config_remove_prefix(cfg, "disk", "");
    for (drive = 0; drive < C64M_DRIVE_COUNT; ++drive) {
        const app_disk_slot *slot = &options->disk_slots[drive];
        if (slot->count > 0) {
            char joined[4096];
            if (disk_slot_format_list(slot, options, joined, sizeof(joined))) {
                snprintf(key, sizeof(key), "%d", drive);
                config_set(cfg, "disk", key, joined);
            }
            if (disk_slot_format_writable_list(slot, joined, sizeof(joined))) {
                snprintf(key, sizeof(key), "%d_writable", drive);
                config_set(cfg, "disk", key, joined);
            }
        }
    }
    if (options->emulate_1541) {
        config_set_bool(cfg, "disk", "emulate_1541", true);
    } else {
        config_remove_prefix(cfg, "disk", "emulate_1541");
    }

    if (options->assembler_file != NULL && options->assembler_file[0] != '\0') {
        char rel_path[PATH_MAX];
        if (app_options_path_relative_to_ini(options, options->assembler_file, rel_path, sizeof(rel_path))) {
            config_set(cfg, "assembler", "file", rel_path);
        }
    }
    if (options->assembler_address != NULL && options->assembler_address[0] != '\0') {
        config_set(cfg, "assembler", "address", options->assembler_address);
    }
    if (options->assembler_run_address != NULL && options->assembler_run_address[0] != '\0') {
        config_set(cfg, "assembler", "run_address", options->assembler_run_address);
    }
    config_set_bool(cfg, "assembler", "auto_run", options->assembler_auto_run);
    config_set_bool(cfg, "assembler", "reset", options->assembler_reset_first);
    config_set_bool(cfg, "assembler", "rearm_oneshots", options->assembler_rearm_oneshots);

    {
        int i;
        for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
            if (options->browse_dirs[i] != NULL && options->browse_dirs[i][0] != '\0') {
                config_set(cfg, "browse", browse_dir_keys[i], options->browse_dirs[i]);
            }
        }
    }

    ok = config_save(cfg, options->ini_path);
    config_destroy(cfg);
    return ok;
}

bool app_options_save_paths_only(const app_options *options)
{
    config *cfg;
    bool ok;
    int i;

    if (options == NULL || options->no_save_ini ||
            options->ini_path == NULL || options->ini_path[0] == '\0') {
        return true; /* nothing named to write to: silent no-op */
    }

    /* Re-read the file so we preserve every other setting, then overwrite only
       the [browse] path keys. */
    cfg = config_load(options->ini_path);
    if (cfg == NULL) {
        cfg = config_load(NULL);
    }
    if (cfg == NULL) {
        return false;
    }

    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        if (options->browse_dirs[i] != NULL && options->browse_dirs[i][0] != '\0') {
            config_set(cfg, "browse", browse_dir_keys[i], options->browse_dirs[i]);
        } else {
            config_remove_prefix(cfg, "browse", browse_dir_keys[i]);
        }
    }

    /* ROM file paths are persisted here too, so "Save Paths Only" captures both
       the browse folders and the ROM endpoints. */
    config_write_rom_config(cfg, options);

    /* The snapshot folder is now [browse] snapshot; drop the legacy key. */
    config_remove_prefix(cfg, "state", "quicksave_folder");

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
    free(options->keyboard_joystick_layout);
    free(options->basic_rom_path);
    free(options->char_rom_path);
    free(options->kernal_rom_path);
    free(options->system_rom_path);
    free(options->rom1541_path);
    free(options->crt_path);
    free(options->prg_path);
    free(options->basic_path);
    free(options->audio_record_path);
    free(options->assembler_file);
    free(options->assembler_address);
    free(options->assembler_run_address);
    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        free(options->browse_dirs[i]);
    }
    for (i = 0; i < C64M_DRIVE_COUNT; ++i) {
        disk_slot_free(&options->disk_slots[i]);
    }

    memset(options, 0, sizeof(*options));
}
