#include "app_options.h"
#include "audio_buffer.h"
#include "c64_snapshot.h"
#include "control_server.h"
#include "frontend.h"
#include "frontend_input.h"
#include "frontend_joystick_input.h"
#include "paste_parser.h"
#include "platform.h"
#include "platform_audio.h"
#include "runtime.h"
#include "runtime_client.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

enum {
    C64M_CONTROLLER_MAX = 2,
    C64M_CONTROLLER_AXIS_THRESHOLD = 16000,
    C64M_STATE_CHUNK_HEADER_SIZE = 8,
    C64M_STATE_HOST_CHUNK_SIZE = 8
};

#define C64M_STATE_TAG(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum {
    C64M_STATE_HOST_TAG = C64M_STATE_TAG('H', 'O', 'S', 'T'),
    C64M_STATE_HOST_VERSION = 1u
};

typedef struct sdl_c64_controller {
    SDL_GameController *controller;
    SDL_JoystickID instance_id;
    uint8_t inputs;
} sdl_c64_controller;

typedef struct sdl_c64_controller_state {
    sdl_c64_controller controllers[C64M_CONTROLLER_MAX];
    unsigned single_controller_port;
    bool swapped;
    /* Optional host-keyboard joystick source, OR'd into its assigned C64 port
       when the port states are published. NULL when not wired. */
    const frontend_joystick_input *kbd_joystick;
} sdl_c64_controller_state;

typedef struct deferred_control_response {
    bool active;
    uint32_t request_id;
    control_command_type command_type;
    uint64_t deadline_ms;
    uint16_t memory_address;
    uint16_t memory_length;
    runtime_memory_mode memory_mode;
    uint16_t expected_breakpoint_count;
    uint32_t expected_breakpoint_id;
    bool expected_breakpoint_enabled;
    uint16_t expected_breakpoint_start;
    bool has_expected_breakpoint_count;
    bool has_expected_breakpoint_enabled;
    bool has_expected_breakpoint_start;
    bool expect_breakpoint_absent;
    bool include_write_history;
    uint64_t start_frame_number;
    uint64_t frame_delta;
    char wait_event_name[32];
} deferred_control_response;

typedef struct control_cached_state {
    bool has_frame;
    c64_frame frame;
    bool has_symbols;
    runtime_symbol_snapshot symbols;
} control_cached_state;

static void sdl_c64_controller_send_ports(
    const sdl_c64_controller_state *state,
    runtime_client *client);

static bool path_has_extension(const char *path, const char *extension) {
    const char *dot;

    if (path == NULL || extension == NULL) {
        return false;
    }

    dot = strrchr(path, '.');
    return dot != NULL && SDL_strcasecmp(dot + 1, extension) == 0;
}

static void make_relative_path(const char *abs_path, char *out, size_t out_size) {
    char cwd[1024];
    size_t cwd_len;

    if (abs_path == NULL || out == NULL || out_size == 0) {
        return;
    }

    if (
#if defined(_WIN32)
        _getcwd(cwd, sizeof(cwd))
#else
        getcwd(cwd, sizeof(cwd))
#endif
        == NULL) {
        snprintf(out, out_size, "%s", abs_path);
        return;
    }

    cwd_len = strlen(cwd);
    if (cwd_len > 0 &&
        strncmp(abs_path, cwd, cwd_len) == 0 &&
        (abs_path[cwd_len] == '/' || abs_path[cwd_len] == '\\')) {
        snprintf(out, out_size, ".%s", abs_path + cwd_len);
    } else {
        snprintf(out, out_size, "%s", abs_path);
    }
}

static bool path_is_absolute_local(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#if defined(_WIN32)
    return path[0] == '/' || path[0] == '\\' ||
        (isalpha((unsigned char)path[0]) && path[1] == ':');
#else
    return path[0] == '/';
#endif
}

static bool join_path_local(char *out, size_t out_size, const char *dir, const char *name) {
    const char *separator = "/";
    size_t dir_len;

    if (out == NULL || out_size == 0 || name == NULL) {
        return false;
    }
    if (dir == NULL || dir[0] == '\0') {
        return snprintf(out, out_size, "%s", name) < (int)out_size;
    }
    dir_len = strlen(dir);
    if (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\')) {
        separator = "";
    }
    return snprintf(out, out_size, "%s%s%s", dir, separator, name) < (int)out_size;
}

static bool options_ini_dir(const app_options *options, char *out, size_t out_size) {
    char cwd[1024];
    const char *ini_path;
    const char *slash;
    size_t len;

    if (out == NULL || out_size == 0) {
        return false;
    }
    if (
#if defined(_WIN32)
        _getcwd(cwd, sizeof(cwd))
#else
        getcwd(cwd, sizeof(cwd))
#endif
        == NULL) {
        return false;
    }

    ini_path = (options != NULL && options->ini_path != NULL && options->ini_path[0] != '\0') ?
        options->ini_path : ".";
    slash = strrchr(ini_path, '/');
#if defined(_WIN32)
    {
        const char *backslash = strrchr(ini_path, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) {
            slash = backslash;
        }
    }
#endif
    if (slash == NULL) {
        return snprintf(out, out_size, "%s", cwd) < (int)out_size;
    }
    len = (size_t)(slash - ini_path);
    if (len == 0) {
        return snprintf(out, out_size, "%c", ini_path[0]) < (int)out_size;
    }
    if (path_is_absolute_local(ini_path)) {
        if (len >= out_size) {
            return false;
        }
        memcpy(out, ini_path, len);
        out[len] = '\0';
        return true;
    }
    if (len >= sizeof(cwd)) {
        return false;
    }
    {
        char rel_dir[1024];
        memcpy(rel_dir, ini_path, len);
        rel_dir[len] = '\0';
        return join_path_local(out, out_size, cwd, rel_dir);
    }
}

static bool quicksave_folder_absolute(
    const app_options *options,
    const char *folder,
    char *out,
    size_t out_size) {
    char ini_dir[1024];

    if (out == NULL || out_size == 0) {
        return false;
    }
    if (folder == NULL || folder[0] == '\0') {
        folder = ".";
    }
    if (path_is_absolute_local(folder)) {
        return snprintf(out, out_size, "%s", folder) < (int)out_size;
    }
    if (!options_ini_dir(options, ini_dir, sizeof(ini_dir))) {
        return snprintf(out, out_size, "%s", folder) < (int)out_size;
    }
    return join_path_local(out, out_size, ini_dir, folder);
}

static const char *path_basename_local(const char *path) {
    const char *slash;

    if (path == NULL) {
        return "";
    }
    slash = strrchr(path, '/');
#if defined(_WIN32)
    {
        const char *backslash = strrchr(path, '\\');
        if (backslash != NULL && (slash == NULL || backslash > slash)) {
            slash = backslash;
        }
    }
#endif
    return slash != NULL ? slash + 1 : path;
}

static void sanitize_snapshot_stem(const char *input, char *out, size_t out_size) {
    const char *base = path_basename_local(input);
    size_t i = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    if (base == NULL || base[0] == '\0') {
        base = "c64m";
    }
    while (base[i] != '\0' && i + 1 < out_size) {
        if (base[i] == '.') {
            break;
        }
        out[i] = (isalnum((unsigned char)base[i]) || base[i] == '-' || base[i] == '_') ?
            base[i] : '_';
        ++i;
    }
    if (i == 0) {
        snprintf(out, out_size, "c64m");
    } else {
        out[i] = '\0';
    }
}

static const char *active_content_path(const app_options *options) {
    const app_disk_slot *slot;

    if (options == NULL) {
        return NULL;
    }
    if (options->crt_path != NULL && options->crt_path[0] != '\0') {
        return options->crt_path;
    }
    slot = &options->disk_slots[8];
    if (slot->count > 0 && slot->current >= 0 && slot->current < slot->count) {
        return slot->paths[slot->current];
    }
    if (options->prg_path != NULL && options->prg_path[0] != '\0') {
        return options->prg_path;
    }
    if (options->basic_path != NULL && options->basic_path[0] != '\0') {
        return options->basic_path;
    }
    return NULL;
}

static void remember_loaded_content(app_options *options, const char *path, const char *kind) {
    if (options == NULL || path == NULL || kind == NULL) {
        return;
    }
    app_options_set_string(&options->crt_path, "");
    app_options_set_string(&options->prg_path, "");
    app_options_set_string(&options->basic_path, "");
    if (strcmp(kind, "crt") == 0) {
        app_options_set_string(&options->crt_path, path);
    } else if (strcmp(kind, "basic") == 0) {
        app_options_set_string(&options->basic_path, path);
    } else {
        app_options_set_string(&options->prg_path, path);
    }
}

static bool append_state_extension(char *path, size_t path_size) {
    size_t len;

    if (path == NULL || path_size == 0 || path[0] == '\0') {
        return false;
    }
    if (path_has_extension(path, "c64state")) {
        return true;
    }
    len = strlen(path);
    if (len + strlen(".c64state") + 1 > path_size) {
        return false;
    }
    strcat(path, ".c64state");
    return true;
}

static bool make_quicksave_path(
    const app_options *options, const char *snapshot_dir, char *out, size_t out_size) {
    char folder[1024];
    char stem[256];
    char filename[512];
    time_t now;
    struct tm tm_value;
    int suffix;

    if (!quicksave_folder_absolute(options, snapshot_dir, folder, sizeof(folder))) {
        return false;
    }
    sanitize_snapshot_stem(active_content_path(options), stem, sizeof(stem));
    now = time(NULL);
#if defined(_WIN32)
    localtime_s(&tm_value, &now);
#else
    {
        struct tm *tmp = localtime(&now);
        if (tmp == NULL) {
            return false;
        }
        tm_value = *tmp;
    }
#endif
    for (suffix = 0; suffix < 1000; ++suffix) {
        int written;
        if (suffix == 0) {
            written = snprintf(
                filename,
                sizeof(filename),
                "%s-%04d%02d%02d-%02d%02d%02d.c64state",
                stem,
                tm_value.tm_year + 1900,
                tm_value.tm_mon + 1,
                tm_value.tm_mday,
                tm_value.tm_hour,
                tm_value.tm_min,
                tm_value.tm_sec);
        } else {
            written = snprintf(
                filename,
                sizeof(filename),
                "%s-%04d%02d%02d-%02d%02d%02d-%d.c64state",
                stem,
                tm_value.tm_year + 1900,
                tm_value.tm_mon + 1,
                tm_value.tm_mday,
                tm_value.tm_hour,
                tm_value.tm_min,
                tm_value.tm_sec,
                suffix);
        }
        if (written < 0 || (size_t)written >= sizeof(filename)) {
            return false;
        }
        if (!join_path_local(out, out_size, folder, filename)) {
            return false;
        }
#if defined(_WIN32)
        if (_access(out, 0) != 0) {
#else
        if (access(out, 0) != 0) {
#endif
            return true;
        }
    }
    return false;
}

static bool find_newest_state_file(
    const app_options *options, const char *snapshot_dir, char *out, size_t out_size) {
    char folder[1024];
    bool found = false;

    if (!quicksave_folder_absolute(options, snapshot_dir, folder, sizeof(folder))) {
        return false;
    }
#if defined(_WIN32)
    {
        char pattern[1200];
        HANDLE handle;
        WIN32_FIND_DATAA data;
        FILETIME newest_time = {0, 0};

        if (!join_path_local(pattern, sizeof(pattern), folder, "*.c64state")) {
            return false;
        }
        handle = FindFirstFileA(pattern, &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                (!found || CompareFileTime(&data.ftLastWriteTime, &newest_time) > 0)) {
                if (join_path_local(out, out_size, folder, data.cFileName)) {
                    newest_time = data.ftLastWriteTime;
                    found = true;
                }
            }
        } while (FindNextFileA(handle, &data));
        FindClose(handle);
    }
#else
    {
        DIR *dir = opendir(folder);
        struct dirent *entry;
        time_t newest_mtime = 0;

        if (dir == NULL) {
            return false;
        }
        while ((entry = readdir(dir)) != NULL) {
            char candidate[1200];
            struct stat st;
            if (!path_has_extension(entry->d_name, "c64state")) {
                continue;
            }
            if (!join_path_local(candidate, sizeof(candidate), folder, entry->d_name)) {
                continue;
            }
            if (stat(candidate, &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            if (!found || st.st_mtime > newest_mtime ||
                (st.st_mtime == newest_mtime && strcmp(candidate, out) > 0)) {
                snprintf(out, out_size, "%s", candidate);
                newest_mtime = st.st_mtime;
                found = true;
            }
        }
        closedir(dir);
    }
#endif
    return found;
}

static bool key_is_quicksave_shortcut(const SDL_KeyboardEvent *key) {
    if (!frontend_input_has_option_modifier(key) ||
        !frontend_input_has_shift_modifier(key)) {
        return false;
    }
    return key->keysym.sym == SDLK_GREATER || key->keysym.sym == SDLK_PERIOD;
}

static bool key_is_quickload_shortcut(const SDL_KeyboardEvent *key) {
    if (!frontend_input_has_option_modifier(key) ||
        !frontend_input_has_shift_modifier(key)) {
        return false;
    }
    return key->keysym.sym == SDLK_LESS || key->keysym.sym == SDLK_COMMA;
}

static bool send_quicksave(runtime_client *client, const app_options *options, const frontend *ui) {
    char path[1200];
    const char *snapshot_dir = frontend_get_browse_dir(ui, FRONTEND_BROWSE_SLOT_SNAPSHOT);

    if (!make_quicksave_path(options, snapshot_dir, path, sizeof(path))) {
        SDL_Log("quicksave: failed to build snapshot path");
        return false;
    }
    SDL_Log("quicksave: %s", path);
    return runtime_client_save_state(client, path);
}

static bool send_quickload(runtime_client *client, const app_options *options, const frontend *ui) {
    char path[1200];
    const char *snapshot_dir = frontend_get_browse_dir(ui, FRONTEND_BROWSE_SLOT_SNAPSHOT);

    if (!find_newest_state_file(options, snapshot_dir, path, sizeof(path))) {
        SDL_Log("quickload: no .c64state files found");
        return false;
    }
    SDL_Log("quickload: %s", path);
    return runtime_client_load_state(client, path);
}

static uint32_t read_le32_local(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static void write_le32_local(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool append_host_state_chunk(
    const char *path,
    const app_options *options,
    const frontend_joystick_input *kbd_joystick) {
    FILE *file;
    uint8_t bytes[C64M_STATE_CHUNK_HEADER_SIZE + C64M_STATE_HOST_CHUNK_SIZE];
    uint8_t port;
    uint8_t layout;

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    port = kbd_joystick != NULL ? (uint8_t)kbd_joystick->port :
        (uint8_t)(options != NULL ? options->keyboard_joystick_port : 0);
    if (port > 2u) {
        port = 0;
    }
    layout = kbd_joystick != NULL ? (uint8_t)kbd_joystick->layout :
        (uint8_t)frontend_joystick_layout_from_string(
            options != NULL ? options->keyboard_joystick_layout : NULL);
    if (layout > (uint8_t)FRONTEND_JOYSTICK_LAYOUT_WASD) {
        layout = (uint8_t)FRONTEND_JOYSTICK_LAYOUT_NUMPAD;
    }

    write_le32_local(bytes, C64M_STATE_HOST_TAG);
    write_le32_local(bytes + 4, C64M_STATE_HOST_CHUNK_SIZE);
    write_le32_local(bytes + 8, C64M_STATE_HOST_VERSION);
    bytes[12] = port;
    bytes[13] = layout;
    bytes[14] = 0;
    bytes[15] = 0;

    file = fopen(path, "ab");
    if (file == NULL) {
        return false;
    }
    if (fwrite(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

static bool read_host_state_chunk(
    const char *path,
    uint8_t *out_port,
    frontend_joystick_layout *out_layout) {
    FILE *file;
    uint8_t *bytes = NULL;
    long length;
    size_t pos;
    bool found = false;

    if (path == NULL || out_port == NULL || out_layout == NULL) {
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    length = ftell(file);
    if (length < 32 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return false;
    }
    fclose(file);

    if (read_le32_local(bytes) != C64_SNAPSHOT_MAGIC ||
        read_le32_local(bytes + 4) != C64_SNAPSHOT_VERSION) {
        free(bytes);
        return false;
    }
    pos = read_le32_local(bytes + 8);
    while (pos + C64M_STATE_CHUNK_HEADER_SIZE <= (size_t)length) {
        uint32_t tag = read_le32_local(bytes + pos);
        uint32_t chunk_len = read_le32_local(bytes + pos + 4);
        const uint8_t *payload;

        pos += C64M_STATE_CHUNK_HEADER_SIZE;
        if (chunk_len > (uint32_t)((size_t)length - pos)) {
            break;
        }
        payload = bytes + pos;
        if (tag == C64M_STATE_HOST_TAG &&
            chunk_len >= C64M_STATE_HOST_CHUNK_SIZE &&
            read_le32_local(payload) == C64M_STATE_HOST_VERSION) {
            uint8_t port = payload[4];
            uint8_t layout = payload[5];
            if (port <= 2u && layout <= (uint8_t)FRONTEND_JOYSTICK_LAYOUT_WASD) {
                *out_port = port;
                *out_layout = (frontend_joystick_layout)layout;
                found = true;
            }
        }
        pos += chunk_len;
    }
    free(bytes);
    return found;
}

static void apply_loaded_host_state(
    const char *path,
    app_options *options,
    frontend *ui,
    runtime_client *client,
    sdl_c64_controller_state *controller_state,
    frontend_joystick_input *kbd_joystick) {
    uint8_t port = 0;
    frontend_joystick_layout layout = FRONTEND_JOYSTICK_LAYOUT_NUMPAD;

    if (kbd_joystick == NULL ||
        !read_host_state_chunk(path, &port, &layout)) {
        return;
    }
    frontend_joystick_set_layout(kbd_joystick, layout);
    frontend_joystick_set_port(kbd_joystick, port);
    if (options != NULL) {
        options->keyboard_joystick_port = (int)port;
        app_options_set_string(
            &options->keyboard_joystick_layout,
            frontend_joystick_layout_to_string(layout));
    }
    if (controller_state != NULL) {
        sdl_c64_controller_send_ports(controller_state, client);
    }
    if (ui != NULL && options != NULL && !frontend_config_dialog_is_open(ui)) {
        frontend_set_config_state(ui, options);
    }
    SDL_Log(
        "loaded host keyboard joystick: port %u (%s)",
        (unsigned)port,
        frontend_joystick_layout_to_string(layout));
}

static c64_config machine_config_from_options(const app_options *options) {
    c64_config config = {
        .video_standard = C64_VIDEO_STANDARD_NTSC,
    };

    if (options != NULL &&
        options->video_standard != NULL &&
        strcmp(options->video_standard, "PAL") == 0) {
        config.video_standard = C64_VIDEO_STANDARD_PAL;
    }
    if (options != NULL) {
        config.emulate_1541 = options->emulate_1541 ? 1 : 0;
    }
    return config;
}

static runtime_config runtime_config_from_options(const app_options *options) {
    runtime_config config = {0};

    runtime_config_set_turbo_defaults(&config);
    if (options != NULL) {
        runtime_config_set_turbo_csv(&config, options->turbo_multipliers);
    }
    return config;
}

static void request_debug_state(runtime_client *client) {
    runtime_client_request_cpu_state(client);
    runtime_client_request_machine_state(client);
    runtime_client_request_breakpoints(client);
    runtime_client_request_disk_status(client, 8);
    runtime_client_request_disk_status(client, 9);
}

static void update_debug_state_from_event(
    frontend_debug_state *debug_state,
    const runtime_event *event) {
    if (debug_state == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
        case RUNTIME_EVENT_RUNNING:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_RUNNING;
            break;

        case RUNTIME_EVENT_PAUSED:
        case RUNTIME_EVENT_RESET_COMPLETE:
        case RUNTIME_EVENT_STEP_COMPLETE:
        case RUNTIME_EVENT_RUN_COMPLETE:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_PAUSED;
            break;

        case RUNTIME_EVENT_ERROR:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_ERROR;
            break;

        case RUNTIME_EVENT_CPU_STATE_RESPONSE:
            debug_state->cpu = event->data.cpu_state;
            debug_state->has_cpu = true;
            break;

        case RUNTIME_EVENT_MACHINE_STATE_RESPONSE:
            debug_state->cpu.pc = event->data.machine_state.pc;
            debug_state->cpu.a = event->data.machine_state.a;
            debug_state->cpu.x = event->data.machine_state.x;
            debug_state->cpu.y = event->data.machine_state.y;
            debug_state->cpu.sp = event->data.machine_state.sp;
            debug_state->cpu.p = event->data.machine_state.p;
            debug_state->cpu.cycles = event->data.machine_state.cpu_cycles;
            debug_state->machine_cycle = event->data.machine_state.cycle;
            debug_state->vic_cycles = event->data.machine_state.vic_cycles;
            debug_state->cia_cycles = event->data.machine_state.cia_cycles;
            debug_state->stop_reason = event->data.machine_state.stop_reason;
            debug_state->active_turbo_multiplier = event->data.machine_state.active_turbo_multiplier;
            debug_state->frame_number = event->data.machine_state.frame_number;
            debug_state->frame_cycle = event->data.machine_state.frame_cycle;
            debug_state->dropped_frames = event->data.machine_state.dropped_frames;
            debug_state->memory_banking = event->data.machine_state.memory_banking;
            debug_state->vicii_hardware = event->data.machine_state.vicii_hardware;
            debug_state->cia1_hardware = event->data.machine_state.cia1_hardware;
            debug_state->cia2_hardware = event->data.machine_state.cia2_hardware;
            debug_state->sid_hardware = event->data.machine_state.sid_hardware;
            debug_state->screen_ram_writes = event->data.machine_state.screen_ram_writes;
            debug_state->color_ram_writes = event->data.machine_state.color_ram_writes;
            debug_state->vic_register_writes = event->data.machine_state.vic_register_writes;
            debug_state->cia1_register_writes = event->data.machine_state.cia1_register_writes;
            debug_state->cia2_register_writes = event->data.machine_state.cia2_register_writes;
            debug_state->sid_register_writes = event->data.machine_state.sid_register_writes;
            debug_state->keyboard_events = event->data.machine_state.keyboard_events;
            debug_state->irq_entries = event->data.machine_state.irq_entries;
            debug_state->cia1_icr_reads = event->data.machine_state.cia1_icr_reads;
            debug_state->cia1_icr_writes = event->data.machine_state.cia1_icr_writes;
            debug_state->cia1_interrupt_assertions = event->data.machine_state.cia1_interrupt_assertions;
            debug_state->nmi_entries = event->data.machine_state.nmi_entries;
            debug_state->restore_requests = event->data.machine_state.restore_requests;
            debug_state->cartridge_attached = event->data.machine_state.cartridge_attached != 0;
            debug_state->has_cpu = true;
            debug_state->has_memory_banking = true;
            debug_state->has_hardware = true;
            if (debug_state->runtime_state != FRONTEND_RUNTIME_STATE_ERROR) {
                debug_state->runtime_state = event->data.machine_state.running ?
                    FRONTEND_RUNTIME_STATE_RUNNING :
                    FRONTEND_RUNTIME_STATE_PAUSED;
            }
            break;

        case RUNTIME_EVENT_MEMORY_RESPONSE:
            debug_state->memory = event->data.memory;
            debug_state->has_memory = true;
            break;

        case RUNTIME_EVENT_MEMORY_VIEW_RESPONSE: {
            int mv_i;
            bool mv_found = false;
            for (mv_i = 0; mv_i < debug_state->memory_view_snapshot_count; mv_i++) {
                runtime_memory_snapshot *slot = &debug_state->memory_view_snapshots[mv_i];
                if (slot->address == event->data.memory.address &&
                    slot->length == event->data.memory.length &&
                    slot->mode == event->data.memory.mode) {
                    *slot = event->data.memory;
                    mv_found = true;
                    break;
                }
            }
            if (!mv_found && debug_state->memory_view_snapshot_count < 16) {
                debug_state->memory_view_snapshots[debug_state->memory_view_snapshot_count++] =
                    event->data.memory;
            }
            break;
        }

        case RUNTIME_EVENT_BREAKPOINTS_RESPONSE:
            debug_state->breakpoints = event->data.breakpoints;
            debug_state->has_breakpoints = true;
            break;

        case RUNTIME_EVENT_DISK_STATUS_RESPONSE:
            if (event->data.disk_status.device >= 8 && event->data.disk_status.device <= 9) {
                size_t index = (size_t)(event->data.disk_status.device - 8u);
                debug_state->disk_status[index] = event->data.disk_status;
                debug_state->has_disk_status[index] = true;
            }
            break;

        case RUNTIME_EVENT_FRAME_READY:
            debug_state->frame_number = event->data.frame_ready.frame_number;
            debug_state->frame_cycle = event->data.frame_ready.machine_cycle;
            debug_state->dropped_frames = event->data.frame_ready.dropped_frames;
            debug_state->has_frame = true;
            break;

        case RUNTIME_EVENT_CALL_STACK_RESPONSE:
            debug_state->call_stack = event->data.call_stack;
            debug_state->has_call_stack = true;
            break;

        case RUNTIME_EVENT_DEBUG_MEMORY_READY:
            break;

        case RUNTIME_EVENT_STARTED:
            if (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_UNKNOWN ||
                debug_state->runtime_state == FRONTEND_RUNTIME_STATE_ERROR) {
                debug_state->runtime_state = FRONTEND_RUNTIME_STATE_PAUSED;
            }
            break;

        case RUNTIME_EVENT_STOPPED:
            debug_state->runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN;
            break;

        case RUNTIME_EVENT_NONE:
        case RUNTIME_EVENT_PONG:
        default:
            break;
    }
}

static const char *control_runtime_state_name(frontend_runtime_state state)
{
    switch (state) {
        case FRONTEND_RUNTIME_STATE_RUNNING:
            return "running";
        case FRONTEND_RUNTIME_STATE_PAUSED:
            return "paused";
        case FRONTEND_RUNTIME_STATE_ERROR:
            return "error";
        case FRONTEND_RUNTIME_STATE_UNKNOWN:
        default:
            return "unknown";
    }
}

static const char *control_stop_reason_name(runtime_stop_reason reason)
{
    switch (reason) {
        case RUNTIME_STOP_REASON_RESET:
            return "reset";
        case RUNTIME_STOP_REASON_PAUSE_COMMAND:
            return "pause-command";
        case RUNTIME_STOP_REASON_STEP:
            return "step";
        case RUNTIME_STOP_REASON_RUN_COMPLETE:
            return "run-complete";
        case RUNTIME_STOP_REASON_BREAKPOINT:
            return "breakpoint";
        case RUNTIME_STOP_REASON_BRK:
            return "brk";
        case RUNTIME_STOP_REASON_ERROR:
            return "error";
        case RUNTIME_STOP_REASON_NONE:
        default:
            return "none";
    }
}

static const char *control_runtime_event_name(runtime_event_type type)
{
    switch (type) {
        case RUNTIME_EVENT_PONG:
            return "pong";
        case RUNTIME_EVENT_STARTED:
            return "started";
        case RUNTIME_EVENT_RUNNING:
            return "running";
        case RUNTIME_EVENT_PAUSED:
            return "paused";
        case RUNTIME_EVENT_STOPPED:
            return "stopped";
        case RUNTIME_EVENT_ERROR:
            return "error";
        case RUNTIME_EVENT_RESET_COMPLETE:
            return "reset-complete";
        case RUNTIME_EVENT_STEP_COMPLETE:
            return "step-complete";
        case RUNTIME_EVENT_RUN_COMPLETE:
            return "run-complete";
        case RUNTIME_EVENT_CPU_STATE_RESPONSE:
            return "cpu-state";
        case RUNTIME_EVENT_MACHINE_STATE_RESPONSE:
            return "machine-state";
        case RUNTIME_EVENT_MEMORY_RESPONSE:
            return "memory";
        case RUNTIME_EVENT_MEMORY_VIEW_RESPONSE:
            return "memory-view";
        case RUNTIME_EVENT_BREAKPOINTS_RESPONSE:
            return "breakpoints";
        case RUNTIME_EVENT_DISK_STATUS_RESPONSE:
            return "disk-status";
        case RUNTIME_EVENT_ASSEMBLE_COMPLETE:
            return "assemble-complete";
        case RUNTIME_EVENT_ASSEMBLE_ERROR:
            return "assemble-error";
        case RUNTIME_EVENT_FRAME_READY:
            return "frame";
        case RUNTIME_EVENT_CALL_STACK_RESPONSE:
            return "call-stack";
        case RUNTIME_EVENT_DISK_SWAP:
            return "disk-swap";
        case RUNTIME_EVENT_DEBUG_MEMORY_READY:
            return "debug-memory";
        case RUNTIME_EVENT_SAVE_STATE_COMPLETE:
            return "save-state-complete";
        case RUNTIME_EVENT_LOAD_STATE_COMPLETE:
            return "load-state-complete";
        case RUNTIME_EVENT_NONE:
        default:
            return "none";
    }
}

static void control_format_cpu_response(
    control_response *response,
    uint32_t request_id,
    const runtime_cpu_snapshot *cpu)
{
    char text[CONTROL_RESPONSE_TEXT_MAX];

    if (response == NULL || cpu == NULL) {
        return;
    }
    snprintf(
        text,
        sizeof(text),
        "pc=%04X a=%02X x=%02X y=%02X sp=%02X p=%02X cycles=%llu",
        cpu->pc,
        cpu->a,
        cpu->x,
        cpu->y,
        cpu->sp,
        cpu->p,
        (unsigned long long)cpu->cycles);
    control_protocol_format_ok(response, request_id, text, false);
}

static void control_format_memory_response(
    control_response *response,
    uint32_t request_id,
    const runtime_memory_snapshot *memory)
{
    uint8_t *payload;
    char metadata[CONTROL_RESPONSE_TEXT_MAX];

    if (response == NULL || memory == NULL || memory->length == 0) {
        return;
    }
    payload = (uint8_t *)malloc(memory->length);
    if (payload == NULL) {
        control_protocol_format_error(response, request_id, "memory", "allocation failed", false);
        return;
    }
    memcpy(payload, memory->bytes, memory->length);
    snprintf(
        metadata,
        sizeof(metadata),
        "addr=%04X length=%u mode=%u",
        memory->address,
        memory->length,
        (unsigned)memory->mode);
    control_protocol_format_data(
        response,
        request_id,
        "memory",
        payload,
        memory->length,
        metadata,
        false);
}

static void control_format_frame_response(
    control_response *response,
    uint32_t request_id,
    const c64_frame *frame)
{
    uint8_t *payload;
    size_t payload_size;
    char metadata[CONTROL_RESPONSE_TEXT_MAX];

    if (response == NULL || frame == NULL) {
        return;
    }
    payload_size = (size_t)frame->height * (size_t)frame->stride_bytes;
    payload = (uint8_t *)malloc(payload_size);
    if (payload == NULL) {
        control_protocol_format_error(response, request_id, "memory", "allocation failed", false);
        return;
    }
    memcpy(payload, frame->pixels, payload_size);
    snprintf(
        metadata,
        sizeof(metadata),
        "width=%u height=%u stride=%u format=argb8888 frame=%llu cycle=%llu",
        frame->width,
        frame->height,
        frame->stride_bytes,
        (unsigned long long)frame->frame_number,
        (unsigned long long)frame->machine_cycle);
    control_protocol_format_data(
        response,
        request_id,
        "frame",
        payload,
        payload_size,
        metadata,
        false);
}

static void control_format_debug_memory_response(
    control_response *response,
    uint32_t request_id,
    const runtime_debug_memory_snapshot *debug_memory,
    bool include_write_history)
{
    uint8_t *payload;
    uint8_t *cursor;
    size_t payload_size = (size_t)C64_RAM_SIZE * 3u;
    char metadata[CONTROL_RESPONSE_TEXT_MAX];

    if (response == NULL || debug_memory == NULL) {
        return;
    }
    if (include_write_history) {
        payload_size += (size_t)C64_RAM_SIZE * sizeof(debug_memory->write_history[0]);
    }
    payload = (uint8_t *)malloc(payload_size);
    if (payload == NULL) {
        control_protocol_format_error(response, request_id, "memory", "allocation failed", false);
        return;
    }
    cursor = payload;
    memcpy(cursor, debug_memory->map, C64_RAM_SIZE);
    cursor += C64_RAM_SIZE;
    memcpy(cursor, debug_memory->ram, C64_RAM_SIZE);
    cursor += C64_RAM_SIZE;
    memcpy(cursor, debug_memory->rom, C64_RAM_SIZE);
    cursor += C64_RAM_SIZE;
    if (include_write_history) {
        memcpy(cursor, debug_memory->write_history,
            (size_t)C64_RAM_SIZE * sizeof(debug_memory->write_history[0]));
    }
    snprintf(
        metadata,
        sizeof(metadata),
        "generation=%llu map=%u ram=%u rom=%u write_history=%u",
        (unsigned long long)debug_memory->generation,
        (unsigned)C64_RAM_SIZE,
        (unsigned)C64_RAM_SIZE,
        (unsigned)C64_RAM_SIZE,
        include_write_history ? 1u : 0u);
    control_protocol_format_data(
        response,
        request_id,
        "debug-memory",
        payload,
        payload_size,
        metadata,
        false);
}

static void control_format_call_stack_response(
    control_response *response,
    uint32_t request_id,
    const runtime_call_stack_snapshot *call_stack)
{
    char text[CONTROL_RESPONSE_TEXT_MAX];
    size_t used = 0;
    int written;
    uint8_t i;

    if (response == NULL || call_stack == NULL) {
        return;
    }
    written = snprintf(text, sizeof(text), "sp=%02X count=%u", call_stack->sp, call_stack->count);
    if (written < 0 || (size_t)written >= sizeof(text)) {
        control_protocol_format_error(response, request_id, "internal", "format failed", false);
        return;
    }
    used = (size_t)written;
    for (i = 0; i < call_stack->count && i < RUNTIME_CALL_STACK_MAX; i++) {
        written = snprintf(
            text + used,
            sizeof(text) - used,
            " frame%u=%04X:%04X",
            (unsigned)i,
            call_stack->entries[i].jsr_address,
            call_stack->entries[i].dest_address);
        if (written < 0 || (size_t)written >= sizeof(text) - used) {
            break;
        }
        used += (size_t)written;
    }
    control_protocol_format_ok(response, request_id, text, false);
}

static void control_format_disk_status_response(
    control_response *response,
    uint32_t request_id,
    const runtime_disk_status_snapshot *disk_status)
{
    char text[CONTROL_RESPONSE_TEXT_MAX];

    if (response == NULL || disk_status == NULL) {
        return;
    }
    snprintf(
        text,
        sizeof(text),
        "device=%u mounted=%u writable=%u dirty=%u kind=%u result=%u name=%s title=%s",
        disk_status->device,
        disk_status->mounted,
        disk_status->writable,
        disk_status->dirty,
        (unsigned)disk_status->image_kind,
        (unsigned)disk_status->last_result,
        disk_status->display_name,
        disk_status->disk_title);
    control_protocol_format_ok(response, request_id, text, false);
}

static void control_format_breakpoints_response(
    control_response *response,
    uint32_t request_id,
    const runtime_breakpoint_snapshot *breakpoints)
{
    uint8_t *payload;
    char *cursor;
    size_t payload_size = 1u + (size_t)RUNTIME_BREAKPOINT_SNAPSHOT_MAX * 192u;
    size_t used = 0;
    char metadata[64];
    uint16_t i;

    if (response == NULL || breakpoints == NULL) {
        return;
    }
    payload = (uint8_t *)malloc(payload_size);
    if (payload == NULL) {
        control_protocol_format_error(response, request_id, "memory", "allocation failed", false);
        return;
    }
    cursor = (char *)payload;
    for (i = 0; i < breakpoints->count && i < RUNTIME_BREAKPOINT_SNAPSHOT_MAX; i++) {
        const runtime_breakpoint_snapshot_entry *entry = &breakpoints->entries[i];
        int written = snprintf(
            cursor + used,
            payload_size - used,
            "id=%u enabled=%u start=%04X end=%04X has_end=%u access=%u mapping=%u actions=%u use_counter=%u hits=%u initial=%u reset=%u counter=%u\n",
            entry->id,
            entry->enabled,
            entry->start_address,
            entry->end_address,
            entry->has_end_address,
            (unsigned)entry->access,
            (unsigned)entry->mapping,
            entry->actions,
            entry->use_counter,
            entry->current_hits,
            entry->initial_count,
            entry->reset_count,
            entry->counter);
        if (written < 0 || (size_t)written >= payload_size - used) {
            break;
        }
        used += (size_t)written;
    }
    snprintf(metadata, sizeof(metadata), "count=%u", breakpoints->count);
    control_protocol_format_data(
        response,
        request_id,
        "breakpoints",
        payload,
        used,
        metadata,
        false);
}

static bool control_breakpoint_snapshot_find(
    const runtime_breakpoint_snapshot *breakpoints,
    uint32_t id,
    const runtime_breakpoint_snapshot_entry **out_entry)
{
    uint16_t i;

    if (breakpoints == NULL) {
        return false;
    }
    for (i = 0; i < breakpoints->count && i < RUNTIME_BREAKPOINT_SNAPSHOT_MAX; i++) {
        if (breakpoints->entries[i].id == id) {
            if (out_entry != NULL) {
                *out_entry = &breakpoints->entries[i];
            }
            return true;
        }
    }
    return false;
}

static bool control_parse_breakpoint_actions(const char *value, uint32_t *out_actions)
{
    uint32_t actions = 0;
    const char *cursor = value;

    if (value == NULL || value[0] == '\0' || out_actions == NULL) {
        return false;
    }
    while (*cursor != '\0') {
        const char *start = cursor;
        size_t length;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        length = (size_t)(cursor - start);
        if (length == 5 && strncmp(start, "break", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_BREAK;
        } else if (length == 4 && strncmp(start, "fast", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_FAST;
        } else if (length == 4 && strncmp(start, "slow", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_SLOW;
        } else if (length == 4 && strncmp(start, "tron", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
        } else if (length == 5 && strncmp(start, "troff", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_TROFF;
        } else if (length == 4 && strncmp(start, "type", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
        } else if (length == 4 && strncmp(start, "swap", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
        } else {
            return false;
        }
        if (*cursor == ',') {
            cursor++;
        }
    }
    *out_actions = actions;
    return actions != 0;
}

static bool control_parse_u32_field(const char *value, uint32_t *out)
{
    char *end;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0' || out == NULL) {
        return false;
    }
    parsed = strtoul(value, &end, 0);
    if (end == value || *end != '\0' || parsed > 0xfffffffful) {
        return false;
    }
    *out = (uint32_t)parsed;
    return true;
}

static bool control_parse_u16_field(const char *value, uint16_t *out)
{
    char *end;
    unsigned long parsed;
    int base = 0;

    if (value == NULL || value[0] == '\0' || out == NULL) {
        return false;
    }
    if (value[0] == '$') {
        value++;
        base = 16;
    }
    parsed = strtoul(value, &end, base);
    if (end == value || *end != '\0' || parsed > 0xfffful) {
        return false;
    }
    *out = (uint16_t)parsed;
    return true;
}

static bool control_parse_bool_field(const char *value, uint8_t *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
        *out = 1u;
        return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0) {
        *out = 0u;
        return true;
    }
    return false;
}

static bool control_parse_breakpoint_definition(
    const char *text,
    runtime_breakpoint_definition *definition)
{
    char buffer[1024];
    char *token;

    if (text == NULL || definition == NULL) {
        return false;
    }
    snprintf(buffer, sizeof(buffer), "%s", text);
    memset(definition, 0, sizeof(*definition));
    definition->enabled = 1u;
    definition->access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition->mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
    definition->actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition->reset_count = 1u;

    token = strtok(buffer, " \t");
    if (token == NULL || strcmp(token, "exec") != 0) {
        return false;
    }
    token = strtok(NULL, " \t");
    if (token == NULL || !control_parse_u16_field(token, &definition->start_address)) {
        return false;
    }
    definition->end_address = definition->start_address;

    while ((token = strtok(NULL, " \t")) != NULL) {
        char *eq = strchr(token, '=');
        char *key;
        char *value;
        if (eq == NULL) {
            return false;
        }
        *eq = '\0';
        key = token;
        value = eq + 1;
        if (strcmp(key, "enabled") == 0) {
            if (!control_parse_bool_field(value, &definition->enabled)) {
                return false;
            }
        } else if (strcmp(key, "end") == 0) {
            if (!control_parse_u16_field(value, &definition->end_address)) {
                return false;
            }
            definition->has_end_address = 1u;
        } else if (strcmp(key, "actions") == 0) {
            if (!control_parse_breakpoint_actions(value, &definition->actions)) {
                return false;
            }
        } else if (strcmp(key, "counter") == 0) {
            if (!control_parse_u32_field(value, &definition->initial_count)) {
                return false;
            }
            definition->use_counter = 1u;
        } else if (strcmp(key, "reset") == 0) {
            if (!control_parse_u32_field(value, &definition->reset_count)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

static void complete_deferred_control_response(
    control_server *control,
    deferred_control_response *deferred,
    const runtime_event *event)
{
    control_response response;

    if (control == NULL || deferred == NULL || !deferred->active || event == NULL) {
        return;
    }

    if (deferred->command_type == CONTROL_COMMAND_GET_CPU &&
        event->type == RUNTIME_EVENT_CPU_STATE_RESPONSE) {
        control_format_cpu_response(&response, deferred->request_id, &event->data.cpu_state);
        if (control_server_post_response(control, &response)) {
            deferred->active = false;
        } else {
            control_response_release(&response);
            SDL_Log("control: response queue full");
        }
    } else if (deferred->command_type == CONTROL_COMMAND_GET_MEMORY &&
               event->type == RUNTIME_EVENT_MEMORY_RESPONSE &&
               event->data.memory.address == deferred->memory_address &&
               event->data.memory.length == deferred->memory_length &&
               event->data.memory.mode == deferred->memory_mode) {
        control_format_memory_response(&response, deferred->request_id, &event->data.memory);
        if (control_server_post_response(control, &response)) {
            deferred->active = false;
        } else {
            control_response_release(&response);
            SDL_Log("control: response queue full");
        }
    } else if (deferred->command_type == CONTROL_COMMAND_GET_CALL_STACK &&
               event->type == RUNTIME_EVENT_CALL_STACK_RESPONSE) {
        control_format_call_stack_response(&response, deferred->request_id, &event->data.call_stack);
        if (control_server_post_response(control, &response)) {
            deferred->active = false;
        } else {
            control_response_release(&response);
            SDL_Log("control: response queue full");
        }
    } else if (deferred->command_type == CONTROL_COMMAND_GET_DISK_STATUS &&
               event->type == RUNTIME_EVENT_DISK_STATUS_RESPONSE &&
               event->data.disk_status.device == deferred->memory_address) {
        control_format_disk_status_response(&response, deferred->request_id, &event->data.disk_status);
        if (control_server_post_response(control, &response)) {
            deferred->active = false;
        } else {
            control_response_release(&response);
            SDL_Log("control: response queue full");
        }
    } else if ((deferred->command_type == CONTROL_COMMAND_BREAK_EXEC ||
                deferred->command_type == CONTROL_COMMAND_BREAK_CLEAR ||
                deferred->command_type == CONTROL_COMMAND_BREAK_ENABLE ||
                deferred->command_type == CONTROL_COMMAND_BREAK_LIST ||
                deferred->command_type == CONTROL_COMMAND_BREAK_CLEAR_ALL ||
                deferred->command_type == CONTROL_COMMAND_BREAK_CREATE ||
                deferred->command_type == CONTROL_COMMAND_BREAK_UPDATE ||
                deferred->command_type == CONTROL_COMMAND_REARM_ONESHOTS) &&
               event->type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE) {
        if (deferred->has_expected_breakpoint_count &&
            event->data.breakpoints.count < deferred->expected_breakpoint_count) {
            return;
        }
        if (deferred->has_expected_breakpoint_enabled) {
            const runtime_breakpoint_snapshot_entry *entry = NULL;
            if (!control_breakpoint_snapshot_find(
                    &event->data.breakpoints,
                    deferred->expected_breakpoint_id,
                    &entry) ||
                ((entry->enabled != 0) != deferred->expected_breakpoint_enabled)) {
                return;
            }
        }
        if (deferred->has_expected_breakpoint_start) {
            const runtime_breakpoint_snapshot_entry *entry = NULL;
            if (!control_breakpoint_snapshot_find(
                    &event->data.breakpoints,
                    deferred->expected_breakpoint_id,
                    &entry) ||
                entry->start_address != deferred->expected_breakpoint_start) {
                return;
            }
        }
        if (deferred->expect_breakpoint_absent &&
            control_breakpoint_snapshot_find(
                &event->data.breakpoints,
                deferred->expected_breakpoint_id,
                NULL)) {
            return;
        }
        control_format_breakpoints_response(&response, deferred->request_id, &event->data.breakpoints);
        if (control_server_post_response(control, &response)) {
            deferred->active = false;
        } else {
            control_response_release(&response);
            SDL_Log("control: response queue full");
        }
    } else if (deferred->command_type == CONTROL_COMMAND_ASSEMBLE &&
               (event->type == RUNTIME_EVENT_ASSEMBLE_COMPLETE ||
                event->type == RUNTIME_EVENT_ASSEMBLE_ERROR)) {
        if (event->type == RUNTIME_EVENT_ASSEMBLE_COMPLETE) {
            char metadata[CONTROL_RESPONSE_TEXT_MAX];
            snprintf(
                metadata,
                sizeof(metadata),
                "address=$%04X",
                (unsigned)event->data.assemble.address);
            control_protocol_format_ok(&response, deferred->request_id, metadata, false);
        } else {
            control_protocol_format_error(
                &response,
                deferred->request_id,
                "assemble-error",
                event->data.error.message[0] != '\0'
                    ? event->data.error.message
                    : "assembly failed",
                false);
        }
        if (control_server_post_response(control, &response)) {
            deferred->active = false;
        } else {
            control_response_release(&response);
            SDL_Log("control: response queue full");
        }
    }
}

static void complete_deferred_debug_memory_response(
    control_server *control,
    deferred_control_response *deferred,
    const runtime_debug_memory_snapshot *debug_memory)
{
    control_response response;

    if (control == NULL || deferred == NULL || !deferred->active ||
        debug_memory == NULL ||
        deferred->command_type != CONTROL_COMMAND_GET_DEBUG_MEMORY) {
        return;
    }

    control_format_debug_memory_response(
        &response,
        deferred->request_id,
        debug_memory,
        deferred->include_write_history);
    if (control_server_post_response(control, &response)) {
        deferred->active = false;
    } else {
        control_response_release(&response);
        SDL_Log("control: response queue full");
    }
}

static void complete_deferred_frame_response(
    control_server *control,
    deferred_control_response *deferred,
    const c64_frame *frame)
{
    control_response response;

    if (control == NULL || deferred == NULL || !deferred->active ||
        frame == NULL ||
        deferred->command_type != CONTROL_COMMAND_GET_FRAME) {
        return;
    }

    control_format_frame_response(&response, deferred->request_id, frame);
    if (control_server_post_response(control, &response)) {
        deferred->active = false;
    } else {
        control_response_release(&response);
        SDL_Log("control: response queue full");
    }
}

static uint32_t control_timeout_or_default(uint32_t timeout_ms)
{
    return timeout_ms != 0 ? timeout_ms : 2000u;
}

static void complete_deferred_wait_response(
    control_server *control,
    deferred_control_response *deferred,
    const char *metadata)
{
    control_response response;

    if (control == NULL || deferred == NULL || !deferred->active) {
        return;
    }
    control_protocol_format_ok(
        &response,
        deferred->request_id,
        metadata,
        false);
    if (control_server_post_response(control, &response)) {
        deferred->active = false;
    } else {
        control_response_release(&response);
        SDL_Log("control: response queue full");
    }
}

static void check_deferred_state_wait(
    control_server *control,
    deferred_control_response *deferred,
    const frontend_debug_state *debug_state)
{
    char metadata[CONTROL_RESPONSE_TEXT_MAX];

    if (control == NULL || deferred == NULL || !deferred->active ||
        debug_state == NULL) {
        return;
    }
    if (deferred->command_type == CONTROL_COMMAND_WAIT_PAUSED &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED) {
        snprintf(
            metadata,
            sizeof(metadata),
            "state=paused frame=%llu stop=%s",
            (unsigned long long)debug_state->frame_number,
            control_stop_reason_name(debug_state->stop_reason));
        complete_deferred_wait_response(control, deferred, metadata);
    } else if (deferred->command_type == CONTROL_COMMAND_WAIT_RUNNING &&
               debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        snprintf(
            metadata,
            sizeof(metadata),
            "state=running frame=%llu",
            (unsigned long long)debug_state->frame_number);
        complete_deferred_wait_response(control, deferred, metadata);
    }
}

static void check_deferred_frame_wait(
    control_server *control,
    deferred_control_response *deferred,
    uint64_t frame_number)
{
    char metadata[CONTROL_RESPONSE_TEXT_MAX];

    if (control == NULL || deferred == NULL || !deferred->active ||
        deferred->command_type != CONTROL_COMMAND_WAIT_FRAME) {
        return;
    }
    if (frame_number < deferred->start_frame_number + deferred->frame_delta) {
        return;
    }
    snprintf(
        metadata,
        sizeof(metadata),
        "frame=%llu delta=%llu",
        (unsigned long long)frame_number,
        (unsigned long long)(frame_number - deferred->start_frame_number));
    complete_deferred_wait_response(control, deferred, metadata);
}

static void check_deferred_event_wait(
    control_server *control,
    deferred_control_response *deferred,
    const runtime_event *event)
{
    char metadata[CONTROL_RESPONSE_TEXT_MAX];
    const char *event_name;

    if (control == NULL || deferred == NULL || !deferred->active ||
        deferred->command_type != CONTROL_COMMAND_WAIT_EVENT ||
        event == NULL) {
        return;
    }
    event_name = control_runtime_event_name(event->type);
    if (strcmp(event_name, deferred->wait_event_name) != 0) {
        return;
    }
    snprintf(metadata, sizeof(metadata), "event=%s", event_name);
    complete_deferred_wait_response(control, deferred, metadata);
}

static void check_deferred_control_timeout(
    control_server *control,
    deferred_control_response *deferred)
{
    control_response response;

    if (control == NULL || deferred == NULL || !deferred->active) {
        return;
    }
    if (SDL_GetTicks64() < deferred->deadline_ms) {
        return;
    }

    control_protocol_format_error(
        &response,
        deferred->request_id,
        "timeout",
        "deferred response timed out",
        false);
    if (control_server_post_response(control, &response)) {
        deferred->active = false;
    } else {
        control_response_release(&response);
        SDL_Log("control: response queue full");
    }
}

static bool control_parse_and_send_paste_events(
    runtime_client *client,
    const char *text,
    size_t length)
{
    paste_event_t events[PASTE_EVENTS_MAX];
    paste_parse_error_t parse_error = { -1, NULL };
    size_t count = 0;
    char buffer[4097];

    if (client == NULL || text == NULL || length == 0 || length >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    if (!paste_parse(buffer, events, PASTE_EVENTS_MAX, &count, &parse_error) || count == 0) {
        return false;
    }
    return runtime_client_paste_events(client, events, count);
}

/* Consume any freshly published symbol snapshot once and distribute it to the
   frontend (for the debugger views) and/or the control cache (so a control
   client can resolve labels via find-symbol). The runtime symbol slot is a
   single-consumer handoff, so this must be the only poll site. */
static void poll_symbols_into(
    runtime_client *client,
    frontend *ui,
    control_cached_state *control_cache) {
    runtime_symbol_snapshot *symbols = malloc(sizeof(*symbols));

    if (symbols == NULL) {
        return;
    }
    if (runtime_client_poll_symbols(client, symbols)) {
        if (ui != NULL) {
            frontend_update_symbols(ui, symbols);
        }
        if (control_cache != NULL) {
            control_cache->symbols = *symbols;
            control_cache->has_symbols = true;
        }
    }
    free(symbols);
}

static void poll_runtime_events(
    runtime_client *client,
    frontend *ui,
    frontend_debug_state *debug_state,
    app_options *options,
    sdl_c64_controller_state *controller_state,
    frontend_joystick_input *kbd_joystick,
    control_server *control,
    deferred_control_response *deferred,
    control_cached_state *control_cache) {
    runtime_event event;
    c64_frame frame;
    bool consumed_frame = false;

    while (runtime_client_poll_event(client, &event)) {
        update_debug_state_from_event(debug_state, &event);
        complete_deferred_control_response(control, deferred, &event);
        check_deferred_event_wait(control, deferred, &event);
        check_deferred_state_wait(control, deferred, debug_state);
        if (debug_state != NULL && debug_state->has_frame) {
            check_deferred_frame_wait(control, deferred, debug_state->frame_number);
        }
        if (event.type == RUNTIME_EVENT_ERROR) {
            SDL_Log("runtime error: %s", event.data.error.message);
        } else if (event.type == RUNTIME_EVENT_ASSEMBLE_ERROR) {
            if (ui != NULL) {
                frontend_show_assembler_errors(ui, event.data.error.message);
            }
        } else if (event.type == RUNTIME_EVENT_ASSEMBLE_COMPLETE) {
            poll_symbols_into(client, ui, control_cache);
            if (ui != NULL) {
                frontend_invalidate_disassembly_cache(ui);
            }
        } else if (event.type == RUNTIME_EVENT_SAVE_STATE_COMPLETE) {
            if (!append_host_state_chunk(event.data.state_file.path, options, kbd_joystick)) {
                SDL_Log("save state host settings append failed: %s", event.data.state_file.path);
            }
            SDL_Log("save state complete: %s", event.data.state_file.path);
        } else if (event.type == RUNTIME_EVENT_LOAD_STATE_COMPLETE) {
            apply_loaded_host_state(
                event.data.state_file.path,
                options,
                ui,
                client,
                controller_state,
                kbd_joystick);
            SDL_Log("load state complete: %s", event.data.state_file.path);
        } else if (event.type == RUNTIME_EVENT_STEP_COMPLETE &&
                   debug_state != NULL &&
                   debug_state->has_cpu) {
            SDL_Log(
                "STEP instruction PC=%04X CYCLES=%llu",
                debug_state->cpu.pc,
                (unsigned long long)debug_state->cpu.cycles);
        } else if (event.type == RUNTIME_EVENT_DISK_SWAP && options != NULL) {
            uint8_t device = event.data.disk_swap.device;
            int32_t param = event.data.disk_swap.swap_param;
            uint8_t relative = event.data.disk_swap.swap_relative;
            app_disk_slot *slot = &options->disk_slots[device];
            const char *path = NULL;

            if (param != 0 && slot->count > 0) {
                int new_index;
                if (relative) {
                    new_index = (slot->current + param % slot->count + slot->count) % slot->count;
                } else {
                    /* absolute 1-based, wrap */
                    new_index = ((param - 1) % slot->count + slot->count) % slot->count;
                }
                path = app_disk_slot_select(slot, new_index);
            }
            if (path != NULL) {
                runtime_client_mount_d64_ex(
                    client,
                    device,
                    path,
                    app_disk_slot_current_writable(slot));
                if (ui != NULL) {
                    frontend_set_disk_queue(ui, device, slot);
                }
            }
        } else if (event.type == RUNTIME_EVENT_DEBUG_MEMORY_READY &&
                   debug_state != NULL) {
            if (runtime_client_poll_debug_memory(client, &debug_state->debug_memory)) {
                debug_state->has_debug_memory = true;
                complete_deferred_debug_memory_response(
                    control,
                    deferred,
                    &debug_state->debug_memory);
            }
        }
    }

    if (ui != NULL || control_cache != NULL) {
        poll_symbols_into(client, ui, control_cache);
    }

    while ((ui != NULL || control != NULL) && runtime_client_poll_frame(client, &frame)) {
        if (ui != NULL && frontend_submit_frame(ui, &frame) && debug_state != NULL) {
            debug_state->frame_number = frame.frame_number;
            debug_state->frame_cycle = frame.machine_cycle;
            debug_state->has_frame = true;
            consumed_frame = true;
        } else if (ui == NULL && debug_state != NULL) {
            debug_state->frame_number = frame.frame_number;
            debug_state->frame_cycle = frame.machine_cycle;
            debug_state->has_frame = true;
        }
        if (control_cache != NULL) {
            control_cache->frame = frame;
            control_cache->has_frame = true;
            complete_deferred_frame_response(control, deferred, &control_cache->frame);
            check_deferred_frame_wait(control, deferred, frame.frame_number);
        }
    }

    if (consumed_frame && debug_state != NULL &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
        request_debug_state(client);
    }
}

static void send_run_command(runtime_client *client) {
    SDL_Log("RUN command requested");
    if (runtime_client_run(client)) {
        request_debug_state(client);
    }
}

static void send_pause_command(runtime_client *client) {
    SDL_Log("PAUSE command requested");
    if (runtime_client_pause(client)) {
        request_debug_state(client);
    }
}

static void open_help(frontend *ui, runtime_client *client, const frontend_debug_state *debug_state) {
    bool was_running = debug_state != NULL &&
        debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING;

    if (was_running) {
        send_pause_command(client);
    }
    frontend_open_help(ui, was_running);
}

static void close_help(frontend *ui, runtime_client *client, const frontend_debug_state *debug_state) {
    bool paused_by_help = frontend_close_help(ui);

    if (paused_by_help &&
        debug_state != NULL &&
        debug_state->runtime_state != FRONTEND_RUNTIME_STATE_ERROR) {
        send_run_command(client);
    }
}

static void send_step_instruction_command(runtime_client *client) {
    SDL_Log("STEP instruction requested");
    if (runtime_client_step_instruction(client)) {
        request_debug_state(client);
    }
}

static void send_step_out_command(runtime_client *client) {
    SDL_Log("STEP OUT requested");
    if (runtime_client_step_out(client)) {
        request_debug_state(client);
    }
}

static void send_step_over_command(runtime_client *client) {
    SDL_Log("STEP OVER requested");
    if (runtime_client_step_over(client)) {
        request_debug_state(client);
    }
}

static bool handle_step_key_event(
    runtime_client *client,
    frontend_debug_state *debug_state,
    const SDL_KeyboardEvent *key,
    bool allow_pause)
{
    if (client == NULL || debug_state == NULL || key == NULL) {
        return false;
    }

    if (key->keysym.sym == SDLK_F10 && !frontend_input_has_shift_modifier(key)) {
        if (debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
            if (allow_pause) {
                send_pause_command(client);
            }
        } else {
            debug_state->step_cycle_start = debug_state->machine_cycle;
            debug_state->step_cpu_cycle_start = debug_state->cpu.cycles;
            send_step_instruction_command(client);
        }
        return true;
    }

    if (key->keysym.sym == SDLK_F11) {
        debug_state->step_cycle_start = debug_state->machine_cycle;
        debug_state->step_cpu_cycle_start = debug_state->cpu.cycles;
        send_step_over_command(client);
        return true;
    }

    return false;
}

static void send_run_to_cursor_command(
    runtime_client *client,
    frontend *ui,
    const frontend_debug_state *debug_state) {
    uint16_t addr;
    if (!frontend_get_disassembly_cursor(ui, &addr)) {
        if (debug_state == NULL || !debug_state->has_cpu) {
            return;
        }
        addr = debug_state->cpu.pc;
    }
    SDL_Log("RUN TO CURSOR requested: $%04X", addr);
    if (runtime_client_run_to_cursor(client, addr)) {
        request_debug_state(client);
    }
}

static void dispatch_input_actions(
    runtime_client *client,
    const frontend_input_action *actions,
    size_t count) {
    size_t i;

    if (client == NULL || actions == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        switch (actions[i].type) {
            case FRONTEND_INPUT_ACTION_KEY:
                runtime_client_keyboard_key(client, actions[i].key, actions[i].pressed);
                break;

            case FRONTEND_INPUT_ACTION_RESTORE:
                runtime_client_restore(client);
                break;

            case FRONTEND_INPUT_ACTION_NONE:
            default:
                break;
        }
    }
}

static void sdl_c64_controllers_reset(sdl_c64_controller_state *state) {
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->single_controller_port = 2u;
}

static size_t sdl_c64_controller_count(const sdl_c64_controller_state *state) {
    size_t i;
    size_t count = 0;

    if (state == NULL) {
        return 0;
    }

    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL) {
            count++;
        }
    }
    return count;
}

static int sdl_c64_controller_find_slot(
    const sdl_c64_controller_state *state,
    SDL_JoystickID instance_id) {
    size_t i;

    if (state == NULL) {
        return -1;
    }

    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL &&
            state->controllers[i].instance_id == instance_id) {
            return (int)i;
        }
    }
    return -1;
}

static unsigned sdl_c64_controller_slot_port(
    const sdl_c64_controller_state *state,
    size_t slot,
    size_t connected_count) {
    if (state == NULL || connected_count == 0) {
        return 0;
    }

    if (connected_count == 1) {
        return state->single_controller_port;
    }

    if (slot == 0) {
        return state->swapped ? 2u : 1u;
    }
    if (slot == 1) {
        return state->swapped ? 1u : 2u;
    }
    return 0;
}

static void sdl_c64_controller_send_ports(
    const sdl_c64_controller_state *state,
    runtime_client *client) {
    uint8_t ports[3] = {0, 0, 0};
    size_t connected_count;
    size_t i;

    if (state == NULL || client == NULL) {
        return;
    }

    connected_count = sdl_c64_controller_count(state);
    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        unsigned port;

        if (state->controllers[i].controller == NULL) {
            continue;
        }
        port = sdl_c64_controller_slot_port(state, i, connected_count);
        if (port >= 1u && port <= 2u) {
            ports[port] = state->controllers[i].inputs;
        }
    }

    /* The keyboard joystick is just another source assigned to a port; OR it in
       so it coexists with any real controller mapped to the same port. */
    if (state->kbd_joystick != NULL &&
        state->kbd_joystick->port >= 1u && state->kbd_joystick->port <= 2u) {
        ports[state->kbd_joystick->port] |= state->kbd_joystick->inputs;
    }

    runtime_client_set_joystick(client, 1u, ports[1]);
    runtime_client_set_joystick(client, 2u, ports[2]);
}

static uint8_t sdl_c64_controller_read_inputs(SDL_GameController *controller) {
    Sint16 x;
    Sint16 y;
    uint8_t inputs = 0;

    if (controller == NULL) {
        return 0;
    }

    x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

    if (x <= -C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        inputs |= C64_JOYSTICK_LEFT;
    }
    if (x >= C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        inputs |= C64_JOYSTICK_RIGHT;
    }
    if (y <= -C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        inputs |= C64_JOYSTICK_UP;
    }
    if (y >= C64M_CONTROLLER_AXIS_THRESHOLD ||
        SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        inputs |= C64_JOYSTICK_DOWN;
    }
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) {
        inputs |= C64_JOYSTICK_FIRE;
    }

    return inputs;
}

static void sdl_c64_controller_refresh_slot(
    sdl_c64_controller_state *state,
    size_t slot,
    runtime_client *client) {
    uint8_t inputs;

    if (state == NULL || slot >= C64M_CONTROLLER_MAX ||
        state->controllers[slot].controller == NULL) {
        return;
    }

    inputs = sdl_c64_controller_read_inputs(state->controllers[slot].controller);
    if (inputs != state->controllers[slot].inputs) {
        state->controllers[slot].inputs = inputs;
        sdl_c64_controller_send_ports(state, client);
    }
}

static void sdl_c64_controller_add(
    sdl_c64_controller_state *state,
    runtime_client *client,
    int device_index) {
    SDL_GameController *controller;
    SDL_Joystick *joystick;
    SDL_JoystickID instance_id;
    size_t slot;

    if (state == NULL || !SDL_IsGameController(device_index)) {
        return;
    }

    for (slot = 0; slot < C64M_CONTROLLER_MAX; slot++) {
        if (state->controllers[slot].controller == NULL) {
            break;
        }
    }
    if (slot >= C64M_CONTROLLER_MAX) {
        SDL_Log("ignoring extra controller: %s", SDL_GameControllerNameForIndex(device_index));
        return;
    }

    controller = SDL_GameControllerOpen(device_index);
    if (controller == NULL) {
        SDL_Log("SDL_GameControllerOpen failed: %s", SDL_GetError());
        return;
    }

    joystick = SDL_GameControllerGetJoystick(controller);
    instance_id = joystick != NULL ? SDL_JoystickInstanceID(joystick) : -1;
    if (instance_id < 0) {
        SDL_Log("SDL_JoystickInstanceID failed: %s", SDL_GetError());
        SDL_GameControllerClose(controller);
        return;
    }
    if (sdl_c64_controller_find_slot(state, instance_id) >= 0) {
        SDL_GameControllerClose(controller);
        return;
    }

    state->controllers[slot].controller = controller;
    state->controllers[slot].instance_id = instance_id;
    state->controllers[slot].inputs = sdl_c64_controller_read_inputs(controller);
    SDL_Log("controller connected: %s", SDL_GameControllerName(controller));
    sdl_c64_controller_send_ports(state, client);
}

static void sdl_c64_controller_remove(
    sdl_c64_controller_state *state,
    runtime_client *client,
    SDL_JoystickID instance_id) {
    int slot;

    slot = sdl_c64_controller_find_slot(state, instance_id);
    if (slot < 0) {
        return;
    }

    SDL_Log("controller disconnected: %s", SDL_GameControllerName(state->controllers[slot].controller));
    SDL_GameControllerClose(state->controllers[slot].controller);
    memset(&state->controllers[slot], 0, sizeof(state->controllers[slot]));
    sdl_c64_controller_send_ports(state, client);
}

static void sdl_c64_controller_handle_event(
    sdl_c64_controller_state *state,
    runtime_client *client,
    const SDL_Event *event) {
    int slot;

    if (state == NULL || event == NULL) {
        return;
    }

    switch (event->type) {
        case SDL_CONTROLLERDEVICEADDED:
            sdl_c64_controller_add(state, client, event->cdevice.which);
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            sdl_c64_controller_remove(state, client, event->cdevice.which);
            break;

        case SDL_CONTROLLERAXISMOTION:
            if (event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                slot = sdl_c64_controller_find_slot(state, event->caxis.which);
                if (slot >= 0) {
                    sdl_c64_controller_refresh_slot(state, (size_t)slot, client);
                }
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            switch (event->cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    slot = sdl_c64_controller_find_slot(state, event->cbutton.which);
                    if (slot >= 0) {
                        sdl_c64_controller_refresh_slot(state, (size_t)slot, client);
                    }
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void sdl_c64_controller_switch_mapping(
    sdl_c64_controller_state *state,
    runtime_client *client,
    unsigned port) {
    size_t connected_count;

    if (state == NULL || (port != 1u && port != 2u)) {
        return;
    }

    connected_count = sdl_c64_controller_count(state);
    if (connected_count >= 2) {
        state->swapped = !state->swapped;
        SDL_Log("controller ports swapped");
    } else {
        state->single_controller_port = port;
        SDL_Log("single controller mapped to C64 joystick port %u", port);
    }
    sdl_c64_controller_send_ports(state, client);
}

static void sdl_c64_controllers_open_existing(
    sdl_c64_controller_state *state,
    runtime_client *client) {
    int i;
    int count;

    count = SDL_NumJoysticks();
    for (i = 0; i < count; i++) {
        sdl_c64_controller_add(state, client, i);
    }
}

static void sdl_c64_controllers_close(sdl_c64_controller_state *state, runtime_client *client) {
    size_t i;

    if (state == NULL) {
        return;
    }

    for (i = 0; i < C64M_CONTROLLER_MAX; i++) {
        if (state->controllers[i].controller != NULL) {
            SDL_GameControllerClose(state->controllers[i].controller);
            memset(&state->controllers[i], 0, sizeof(state->controllers[i]));
        }
    }
    sdl_c64_controller_send_ports(state, client);
}

static void dispatch_debugger_intents(
    runtime_client *client,
    frontend *ui,
    app_options *options,
    sdl_c64_controller_state *controller_state,
    frontend_joystick_input *kbd_joystick) {
    frontend_debugger_intent intent;

    if (client == NULL || ui == NULL) {
        return;
    }

    while (frontend_poll_debugger_intent(ui, &intent)) {
        bool sent = false;

        switch (intent.type) {
            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_PC:
                sent = runtime_client_set_pc(client, intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_SP:
                sent = runtime_client_set_sp(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_A:
                sent = runtime_client_set_a(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_X:
                sent = runtime_client_set_x(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_Y:
                sent = runtime_client_set_y(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REGISTER_SET_STATUS:
                sent = runtime_client_set_status(client, (uint8_t)intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY:
                sent = runtime_client_request_memory(
                    client,
                    intent.address,
                    intent.length,
                    intent.memory_mode);
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_MEMORY_VIEW:
                sent = runtime_client_request_memory_view(
                    client,
                    intent.address,
                    intent.length,
                    intent.memory_mode);
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_DEBUG_MEMORY:
                sent = runtime_client_request_debug_memory(client, intent.include_write_history);
                break;

            case FRONTEND_DEBUGGER_INTENT_MEMORY_WRITE_BYTE:
                sent = runtime_client_write_memory_byte(
                    client,
                    intent.address,
                    (uint8_t)intent.value,
                    intent.memory_mode);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_EXECUTE:
                sent = runtime_client_set_execute_breakpoint(client, intent.value);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR:
                sent = runtime_client_clear_breakpoint(client, intent.id);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CLEAR_ALL:
                sent = runtime_client_clear_all_breakpoints(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_SET_ENABLED:
                sent = runtime_client_set_breakpoint_enabled(client, intent.id, intent.enabled);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_CREATE:
                sent = runtime_client_create_breakpoint(client, &intent.breakpoint);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_UPDATE:
                sent = runtime_client_update_breakpoint(client, intent.id, &intent.breakpoint);
                break;

            case FRONTEND_DEBUGGER_INTENT_BREAKPOINT_REQUEST_SNAPSHOT:
                sent = runtime_client_request_breakpoints(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_PROGRAM_LOAD_PRG_DIALOG:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_PROGRAM_LOAD_PRG_DIALOG,
                    "Load PRG/BAS", false, NULL, NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG,
                        "Mount Disk Image", false, "d64", NULL, intent.disk_device);
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG,
                        "Add Disk Image", false, "d64", NULL, intent.disk_device);
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_UNMOUNT:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    app_disk_slot *slot = &options->disk_slots[intent.disk_device];
                    const char *next_path = app_disk_slot_eject_current(slot);
                    if (next_path != NULL) {
                        sent = runtime_client_mount_d64_ex(
                            client,
                            intent.disk_device,
                            next_path,
                            app_disk_slot_current_writable(slot));
                    } else {
                        sent = runtime_client_unmount_disk(client, intent.disk_device);
                    }
                    frontend_set_disk_queue(ui, intent.disk_device, slot);
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_EJECT_ALL:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    sent = runtime_client_unmount_disk(client, intent.disk_device);
                    if (sent) {
                        app_disk_slot_clear(&options->disk_slots[intent.disk_device]);
                        frontend_set_disk_queue(ui, intent.disk_device,
                            &options->disk_slots[intent.disk_device]);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_SELECT:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    app_disk_slot *slot = &options->disk_slots[intent.disk_device];
                    const char *path = app_disk_slot_select(slot, intent.disk_queue_index);
                    if (path != NULL) {
                        sent = runtime_client_mount_d64_ex(
                            client,
                            intent.disk_device,
                            path,
                            app_disk_slot_current_writable(slot));
                        frontend_set_disk_queue(ui, intent.disk_device, slot);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_DISK_SET_WRITABLE:
                if (intent.disk_device == 8 || intent.disk_device == 9) {
                    app_disk_slot *slot = &options->disk_slots[intent.disk_device];
                    if (app_disk_slot_set_current_writable(slot, intent.disk_writable)) {
                        sent = runtime_client_set_disk_writable(
                            client,
                            intent.disk_device,
                            intent.disk_writable);
                        frontend_set_disk_queue(ui, intent.disk_device, slot);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_MACHINE_RESET:
                sent = runtime_client_reset_ex(client, intent.machine_reset_detach_cartridge);
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG,
                    "Select INI File", false, "ini", NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG,
                    "Select Folder", false, NULL, NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG,
                    "Select Symbol File", false, NULL, NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_CONFIG_APPLY:
                {
                    int d;
                    c64_config machine_config = machine_config_from_options(&intent.config);
                    runtime_config runtime_options = runtime_config_from_options(&intent.config);
                    char absolute_symbol_files[1024];
                    for (d = 0; d < C64M_DRIVE_COUNT; ++d) {
                        app_disk_slot_copy(
                            &intent.config.disk_slots[d], &options->disk_slots[d]);
                    }
                    app_options_destroy(options);
                    *options = intent.config;
                    memset(&intent.config, 0, sizeof(intent.config));
                    options->save_ini = intent.config_result.save_ini_on_quit || options->remember;
                    if (!app_options_symbol_files_absolute(options, absolute_symbol_files, sizeof(absolute_symbol_files))) {
                        snprintf(absolute_symbol_files, sizeof(absolute_symbol_files), "%s", options->symbol_files ? options->symbol_files : "");
                    }
                    sent = runtime_client_apply_machine_config(
                        client,
                        &machine_config,
                        &runtime_options,
                        options->ini_path,
                        absolute_symbol_files,
                        intent.config_result.needs_reboot,
                        options->save_ini && !options->no_save_ini);
                    frontend_set_config_state(ui, options);
                    frontend_set_disk_queue(ui, 8, &options->disk_slots[8]);
                    frontend_set_disk_queue(ui, 9, &options->disk_slots[9]);
                    /* Apply keyboard-joystick config to the live input source. */
                    if (kbd_joystick != NULL) {
                        frontend_joystick_set_layout(
                            kbd_joystick,
                            frontend_joystick_layout_from_string(
                                options->keyboard_joystick_layout));
                        frontend_joystick_set_port(
                            kbd_joystick, (unsigned)options->keyboard_joystick_port);
                        if (controller_state != NULL) {
                            sdl_c64_controller_send_ports(controller_state, client);
                        }
                    }
                    if (intent.config_result.symbols_changed) {
                        runtime_client_request_memory(client, 0, 1, RUNTIME_MEMORY_MODE_CPU_MAP);
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE,
                    "Select Assembler Source", false, NULL, NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_SAVE_PATHS_ONLY:
                /* Pull the live folders from the frontend, then rewrite only the
                   [browse] keys in the named INI (leaving everything else). */
                if (options != NULL) {
                    int slot;
                    for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT &&
                             slot < APP_BROWSE_DIR_COUNT; ++slot) {
                        const char *dir = frontend_get_browse_dir(ui, (frontend_browse_slot)slot);
                        app_options_set_string(&options->browse_dirs[slot], dir[0] ? dir : NULL);
                    }
                    app_options_save_paths_only(options);
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN:
                if (intent.assemble_rearm_oneshots) {
                    runtime_client_rearm_oneshot_breakpoints(client);
                }
                {
                    char assemble_path[1024];
                    const char *source_path = intent.assemble_path;

                    if (options != NULL &&
                        app_options_path_absolute_from_ini(
                            options, intent.assemble_path,
                            assemble_path, sizeof(assemble_path))) {
                        source_path = assemble_path;
                    }
                sent = runtime_client_assemble_file_full(
                    client,
                    source_path,
                    intent.assemble_address,
                    intent.assemble_run_address,
                    intent.assemble_auto_run,
                    intent.assemble_reset_first);
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE,
                    "Select Binary File", false, NULL, NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_EXECUTE:
                if (path_has_extension(intent.load_bin_path, "crt")) {
                    sent = runtime_client_load_crt(client, intent.load_bin_path);
                    if (sent) {
                        remember_loaded_content(options, intent.load_bin_path, "crt");
                    }
                } else if (path_has_extension(intent.load_bin_path, "t64")) {
                    sent = runtime_client_load_prg(client, intent.load_bin_path);
                    if (sent) {
                        remember_loaded_content(options, intent.load_bin_path, "prg");
                    }
                } else {
                    sent = runtime_client_load_bin(
                        client,
                        intent.load_bin_path,
                        intent.load_bin_address,
                        intent.load_bin_use_file_address,
                        intent.load_bin_reset_first,
                        intent.load_bin_is_basic,
                        intent.load_bin_is_basic_text);
                    if (sent) {
                        remember_loaded_content(
                            options,
                            intent.load_bin_path,
                            (intent.load_bin_is_basic || intent.load_bin_is_basic_text)
                                ? "basic" : "prg");
                    }
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE,
                    "Save File", true, NULL, NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_EXECUTE:
                sent = runtime_client_save_bin(
                    client,
                    intent.save_bin_path,
                    intent.save_bin_start,
                    intent.save_bin_end,
                    intent.save_bin_write_file_address,
                    intent.save_bin_is_basic,
                    intent.save_bin_is_basic_text);
                break;

            case FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG,
                    "Save State", true, "c64state", "c64state", 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG:
                frontend_open_file_browser(ui, FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG,
                    "Load State", false, "c64state", NULL, 0);
                break;

            case FRONTEND_DEBUGGER_INTENT_FILE_BROWSER_RESULT:
                switch (intent.file_browser_purpose) {
                    case FRONTEND_DEBUGGER_INTENT_PROGRAM_LOAD_PRG_DIALOG:
                        if (path_has_extension(intent.file_browser_path, "crt")) {
                            sent = runtime_client_load_crt(client, intent.file_browser_path);
                            if (sent) {
                                remember_loaded_content(options, intent.file_browser_path, "crt");
                            }
                        } else {
                            sent = runtime_client_load_prg(client, intent.file_browser_path);
                            if (sent) {
                                remember_loaded_content(options, intent.file_browser_path, "prg");
                            }
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_DISK_MOUNT_DIALOG:
                        sent = runtime_client_mount_d64_ex(
                            client, intent.disk_device, intent.file_browser_path, false);
                        if (sent) {
                            app_disk_slot_set(&options->disk_slots[intent.disk_device], intent.file_browser_path);
                            frontend_set_disk_queue(ui, intent.disk_device,
                                &options->disk_slots[intent.disk_device]);
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_DISK_ADD_DIALOG:
                        {
                            app_disk_slot *slot = &options->disk_slots[intent.disk_device];
                            bool was_empty = slot->count == 0;
                            if (app_disk_slot_add_after_current(slot, intent.file_browser_path)) {
                                if (was_empty) {
                                    sent = runtime_client_mount_d64_ex(
                                        client,
                                        intent.disk_device,
                                        slot->paths[0],
                                        app_disk_slot_current_writable(slot));
                                } else {
                                    sent = true;
                                }
                                frontend_set_disk_queue(ui, intent.disk_device, slot);
                            }
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_INI_DIALOG:
                        {
                            app_options selected;
                            if (app_options_copy(&selected, options)) {
                                app_options_set_string(&selected.ini_path, intent.file_browser_path);
                                frontend_apply_selected_ini(ui, &selected);
                                app_options_destroy(&selected);
                            }
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_PATH_DIALOG:
                        frontend_set_picked_browse_dir(ui, intent.file_browser_path);
                        break;

                    case FRONTEND_DEBUGGER_INTENT_CONFIG_PICK_SYMBOL_DIALOG:
                        frontend_append_symbol_file(ui, intent.file_browser_path);
                        break;

                    case FRONTEND_DEBUGGER_INTENT_ASSEMBLE_BROWSE:
                        frontend_set_assembler_path(ui, intent.file_browser_path);
                        break;

                    case FRONTEND_DEBUGGER_INTENT_LOAD_BIN_BROWSE:
                        {
                            char rel_path[1024];
                            make_relative_path(intent.file_browser_path, rel_path, sizeof(rel_path));
                            frontend_set_load_bin_path(ui, rel_path);
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_SAVE_BIN_BROWSE:
                        {
                            char rel_path[1024];
                            make_relative_path(intent.file_browser_path, rel_path, sizeof(rel_path));
                            frontend_set_save_bin_path(ui, rel_path);
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_STATE_SAVE_AS_DIALOG:
                        {
                            char path[1024];
                            snprintf(path, sizeof(path), "%s", intent.file_browser_path);
                            if (append_state_extension(path, sizeof(path))) {
                                sent = runtime_client_save_state(client, path);
                            }
                        }
                        break;

                    case FRONTEND_DEBUGGER_INTENT_STATE_LOAD_DIALOG:
                        sent = runtime_client_load_state(client, intent.file_browser_path);
                        break;

                    default:
                        break;
                }
                break;

            case FRONTEND_DEBUGGER_INTENT_REQUEST_CALL_STACK:
                sent = runtime_client_request_call_stack(client);
                break;

            case FRONTEND_DEBUGGER_INTENT_NONE:
            default:
                break;
        }

        if (sent) {
            request_debug_state(client);
        }
        app_options_destroy(&intent.config);
    }
}

static void handle_keyboard_input(
    frontend_input_mapper *mapper,
    runtime_client *client,
    const SDL_KeyboardEvent *event) {
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    size_t count;

    count = frontend_input_map_keyboard_event(mapper, event, actions, FRONTEND_INPUT_MAX_ACTIONS);
    dispatch_input_actions(client, actions, count);
}

static void handle_drop_file(runtime_client *client, app_options *options, char *path) {
    if (path_has_extension(path, "d64")) {
        if (runtime_client_mount_d64_ex(client, 8, path, false) && options != NULL) {
            app_disk_slot_set(&options->disk_slots[8], path);
        }
    } else if (path_has_extension(path, "c64state")) {
        runtime_client_load_state(client, path);
    } else if (path_has_extension(path, "crt")) {
        if (runtime_client_load_crt(client, path)) {
            remember_loaded_content(options, path, "crt");
        }
    } else if (path_has_extension(path, "bas")) {
        if (runtime_client_load_bin(client, path, 0, true, true, true, false)) {
            remember_loaded_content(options, path, "basic");
        }
    } else {
        if (runtime_client_load_prg(client, path)) {
            remember_loaded_content(options, path, "prg");
        }
    }

    SDL_free(path);
}

static void dispatch_control_request(
    control_server *control,
    runtime_client *client,
    const frontend_debug_state *debug_state,
    const control_cached_state *control_cache,
    deferred_control_response *deferred,
    const control_request *request)
{
    control_response response;
    bool accepted = false;

    if (control == NULL || client == NULL || request == NULL) {
        return;
    }

    memset(&response, 0, sizeof(response));
    if (deferred != NULL && !deferred->active) {
        deferred->has_expected_breakpoint_count = false;
        deferred->expected_breakpoint_count = 0;
        deferred->has_expected_breakpoint_enabled = false;
        deferred->has_expected_breakpoint_start = false;
        deferred->expect_breakpoint_absent = false;
        deferred->expected_breakpoint_id = 0;
        deferred->start_frame_number = 0;
        deferred->frame_delta = 0;
        deferred->wait_event_name[0] = '\0';
    }

    switch (request->type) {
        case CONTROL_COMMAND_HELLO:
            control_protocol_format_ok(
                &response,
                request->id,
                "name=c64m protocol=C64M/1",
                false);
            break;

        case CONTROL_COMMAND_VERSION:
            control_protocol_format_ok(
                &response,
                request->id,
                "protocol=C64M/1 app=0.1.0",
                false);
            break;

        case CONTROL_COMMAND_CAPABILITIES:
            control_protocol_format_ok(
                &response,
                request->id,
                "connection introspection execution state step frame memory debug-memory call-stack input disk file breakpoints wait assemble symbols",
                false);
            break;

        case CONTROL_COMMAND_PING:
            control_protocol_format_ok(&response, request->id, NULL, false);
            break;

        case CONTROL_COMMAND_QUIT_CLIENT:
            control_protocol_format_ok(&response, request->id, NULL, true);
            break;

        case CONTROL_COMMAND_RESET:
            accepted = runtime_client_reset(client);
            break;

        case CONTROL_COMMAND_RUN:
            accepted = runtime_client_run(client);
            break;

        case CONTROL_COMMAND_PAUSE:
            accepted = runtime_client_pause(client);
            break;

        case CONTROL_COMMAND_STEP_CYCLE:
            accepted = runtime_client_step_cycle(client);
            break;

        case CONTROL_COMMAND_STEP_INSTRUCTION:
            accepted = runtime_client_step_instruction(client);
            break;

        case CONTROL_COMMAND_STEP_OVER:
            accepted = runtime_client_step_over(client);
            break;

        case CONTROL_COMMAND_STEP_OUT:
            accepted = runtime_client_step_out(client);
            break;

        case CONTROL_COMMAND_RUN_CYCLES:
            if (request->args.count > (uint64_t)((size_t)-1)) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "bad-args",
                    "count too large",
                    false);
            } else {
                accepted = runtime_client_run_cycles(client, (size_t)request->args.count);
            }
            break;

        case CONTROL_COMMAND_RUN_INSTRUCTIONS:
            if (request->args.count > (uint64_t)((size_t)-1)) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "bad-args",
                    "count too large",
                    false);
            } else {
                accepted = runtime_client_run_instructions(client, (size_t)request->args.count);
            }
            break;

        case CONTROL_COMMAND_RUN_TO:
            accepted = runtime_client_run_to_cursor(client, request->args.address);
            break;

        case CONTROL_COMMAND_GET_STATE: {
            char text[CONTROL_RESPONSE_TEXT_MAX];
            const bool has_state = debug_state != NULL;
            snprintf(
                text,
                sizeof(text),
                "state=%s has_cpu=%u frame=%llu cycle=%llu stop=%s turbo=%u",
                has_state ? control_runtime_state_name(debug_state->runtime_state) : "unknown",
                has_state && debug_state->has_cpu ? 1u : 0u,
                has_state ? (unsigned long long)debug_state->frame_number : 0ull,
                has_state ? (unsigned long long)debug_state->machine_cycle : 0ull,
                has_state ? control_stop_reason_name(debug_state->stop_reason) : "none",
                has_state ? debug_state->active_turbo_multiplier : 0u);
            control_protocol_format_ok(&response, request->id, text, false);
            break;
        }

        case CONTROL_COMMAND_GET_CPU:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_request_cpu_state(client)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    return;
                }
                control_protocol_format_error(
                    &response,
                    request->id,
                    "internal",
                    "deferred state unavailable",
                    false);
            } else {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;

        case CONTROL_COMMAND_GET_FRAME:
            if (control_cache != NULL && control_cache->has_frame) {
                control_format_frame_response(&response, request->id, &control_cache->frame);
            } else if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_request_frame(client)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    return;
                }
                control_protocol_format_error(
                    &response,
                    request->id,
                    "internal",
                    "deferred state unavailable",
                    false);
            } else {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;

        case CONTROL_COMMAND_GET_MEMORY:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_request_memory(
                           client,
                           request->args.address,
                           request->args.length,
                           (runtime_memory_mode)request->args.memory_mode)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->memory_address = request->args.address;
                    deferred->memory_length = request->args.length;
                    deferred->memory_mode = (runtime_memory_mode)request->args.memory_mode;
                    return;
                }
                control_protocol_format_error(
                    &response,
                    request->id,
                    "internal",
                    "deferred state unavailable",
                    false);
            } else {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;

        case CONTROL_COMMAND_GET_DEBUG_MEMORY:
            if (debug_state != NULL && debug_state->has_debug_memory &&
                (!request->args.include_write_history ||
                 debug_state->debug_memory.has_write_history)) {
                control_format_debug_memory_response(
                    &response,
                    request->id,
                    &debug_state->debug_memory,
                    request->args.include_write_history);
            } else if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_request_debug_memory(
                           client,
                           request->args.include_write_history)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->include_write_history = request->args.include_write_history;
                    return;
                }
                control_protocol_format_error(
                    &response,
                    request->id,
                    "internal",
                    "deferred state unavailable",
                    false);
            } else {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;

        case CONTROL_COMMAND_GET_CALL_STACK:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_request_call_stack(client)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    return;
                }
                control_protocol_format_error(
                    &response,
                    request->id,
                    "internal",
                    "deferred state unavailable",
                    false);
            } else {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;

        case CONTROL_COMMAND_KEY_DOWN:
            accepted = runtime_client_keyboard_key(client, (c64_key)request->args.key, true);
            break;

        case CONTROL_COMMAND_KEY_UP:
            accepted = runtime_client_keyboard_key(client, (c64_key)request->args.key, false);
            break;

        case CONTROL_COMMAND_RESTORE:
            accepted = runtime_client_restore(client);
            break;

        case CONTROL_COMMAND_JOYSTICK:
            accepted = runtime_client_set_joystick(
                client,
                request->args.port,
                request->args.mask);
            break;

        case CONTROL_COMMAND_PASTE_TEXT:
            accepted = runtime_client_paste_text_buffer(
                client,
                request->args.text,
                strlen(request->args.text));
            break;

        case CONTROL_COMMAND_PASTE_EVENTS:
            accepted = control_parse_and_send_paste_events(
                client,
                request->args.text,
                strlen(request->args.text));
            break;

        case CONTROL_COMMAND_PASTE_TEXT_DATA:
            accepted = request->payload != NULL &&
                runtime_client_paste_text_buffer(
                    client,
                    (const char *)request->payload,
                    request->payload_size);
            break;

        case CONTROL_COMMAND_PASTE_EVENTS_DATA:
            accepted = request->payload != NULL &&
                control_parse_and_send_paste_events(
                    client,
                    (const char *)request->payload,
                    request->payload_size);
            break;

        case CONTROL_COMMAND_LOAD_PRG:
            accepted = runtime_client_load_prg(client, request->args.text);
            break;

        case CONTROL_COMMAND_LOAD_BIN:
            accepted = runtime_client_load_bin(
                client,
                request->args.text,
                request->args.address,
                request->args.use_file_address,
                request->args.reset_first,
                request->args.is_basic,
                false);
            break;

        case CONTROL_COMMAND_SAVE_BIN:
            accepted = runtime_client_save_bin(
                client,
                request->args.text,
                request->args.start_address,
                request->args.end_address,
                request->args.write_file_address,
                request->args.is_basic,
                false);
            break;

        case CONTROL_COMMAND_MOUNT_D64:
            accepted = runtime_client_mount_d64(client, request->args.device, request->args.text);
            break;

        case CONTROL_COMMAND_UNMOUNT_DISK:
            accepted = runtime_client_unmount_disk(client, request->args.device);
            break;

        case CONTROL_COMMAND_GET_DISK_STATUS:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_request_disk_status(client, request->args.device)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->memory_address = request->args.device;
                    return;
                }
                control_protocol_format_error(
                    &response,
                    request->id,
                    "internal",
                    "deferred state unavailable",
                    false);
            } else {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;

        case CONTROL_COMMAND_BREAK_EXEC:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "busy",
                    "deferred-response-active",
                    false);
            } else if (runtime_client_set_execute_breakpoint(client, request->args.address)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->expected_breakpoint_count =
                        debug_state != NULL && debug_state->has_breakpoints ?
                        (uint16_t)(debug_state->breakpoints.count + 1u) :
                        1u;
                    deferred->has_expected_breakpoint_count = true;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;

        case CONTROL_COMMAND_BREAK_CLEAR:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_clear_breakpoint(client, request->args.id)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->expected_breakpoint_id = request->args.id;
                    deferred->expect_breakpoint_absent = true;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;

        case CONTROL_COMMAND_BREAK_ENABLE:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_set_breakpoint_enabled(
                           client,
                           request->args.id,
                           request->args.include_write_history)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->expected_breakpoint_id = request->args.id;
                    deferred->expected_breakpoint_enabled = request->args.include_write_history;
                    deferred->has_expected_breakpoint_enabled = true;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;

        case CONTROL_COMMAND_BREAK_LIST:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_request_breakpoints(client)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;

        case CONTROL_COMMAND_BREAK_CLEAR_ALL:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_clear_all_breakpoints(client)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;

        case CONTROL_COMMAND_BREAK_CREATE: {
            runtime_breakpoint_definition definition;
            if (!control_parse_breakpoint_definition(request->args.text, &definition)) {
                control_protocol_format_error(&response, request->id, "bad-args", "invalid breakpoint definition", false);
            } else if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_create_breakpoint(client, &definition)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->expected_breakpoint_count =
                        debug_state != NULL && debug_state->has_breakpoints ?
                        (uint16_t)(debug_state->breakpoints.count + 1u) :
                        1u;
                    deferred->has_expected_breakpoint_count = true;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;
        }

        case CONTROL_COMMAND_BREAK_UPDATE: {
            runtime_breakpoint_definition definition;
            if (!control_parse_breakpoint_definition(request->args.text, &definition)) {
                control_protocol_format_error(&response, request->id, "bad-args", "invalid breakpoint definition", false);
            } else if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_update_breakpoint(client, request->args.id, &definition)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    deferred->expected_breakpoint_id = request->args.id;
                    deferred->expected_breakpoint_start = definition.start_address;
                    deferred->has_expected_breakpoint_start = true;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;
        }

        case CONTROL_COMMAND_REARM_ONESHOTS:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (runtime_client_rearm_oneshot_breakpoints(client)) {
                if (deferred != NULL) {
                    deferred->active = true;
                    deferred->request_id = request->id;
                    deferred->command_type = request->type;
                    deferred->deadline_ms = SDL_GetTicks64() + 2000u;
                    return;
                }
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            } else {
                control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
            }
            break;

        case CONTROL_COMMAND_WAIT_PAUSED:
            if (debug_state != NULL &&
                debug_state->runtime_state == FRONTEND_RUNTIME_STATE_PAUSED) {
                char text[CONTROL_RESPONSE_TEXT_MAX];
                snprintf(
                    text,
                    sizeof(text),
                    "state=paused frame=%llu stop=%s",
                    (unsigned long long)debug_state->frame_number,
                    control_stop_reason_name(debug_state->stop_reason));
                control_protocol_format_ok(&response, request->id, text, false);
            } else if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (deferred != NULL) {
                deferred->active = true;
                deferred->request_id = request->id;
                deferred->command_type = request->type;
                deferred->deadline_ms =
                    SDL_GetTicks64() + control_timeout_or_default(request->args.timeout_ms);
                return;
            } else {
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            }
            break;

        case CONTROL_COMMAND_WAIT_RUNNING:
            if (debug_state != NULL &&
                debug_state->runtime_state == FRONTEND_RUNTIME_STATE_RUNNING) {
                char text[CONTROL_RESPONSE_TEXT_MAX];
                snprintf(
                    text,
                    sizeof(text),
                    "state=running frame=%llu",
                    (unsigned long long)debug_state->frame_number);
                control_protocol_format_ok(&response, request->id, text, false);
            } else if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (deferred != NULL) {
                deferred->active = true;
                deferred->request_id = request->id;
                deferred->command_type = request->type;
                deferred->deadline_ms =
                    SDL_GetTicks64() + control_timeout_or_default(request->args.timeout_ms);
                return;
            } else {
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            }
            break;

        case CONTROL_COMMAND_WAIT_FRAME:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (deferred != NULL) {
                deferred->active = true;
                deferred->request_id = request->id;
                deferred->command_type = request->type;
                deferred->deadline_ms =
                    SDL_GetTicks64() + control_timeout_or_default(request->args.timeout_ms);
                deferred->start_frame_number =
                    debug_state != NULL ? debug_state->frame_number : 0u;
                deferred->frame_delta = request->args.count;
                return;
            } else {
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            }
            break;

        case CONTROL_COMMAND_WAIT_EVENT:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (deferred != NULL) {
                deferred->active = true;
                deferred->request_id = request->id;
                deferred->command_type = request->type;
                deferred->deadline_ms =
                    SDL_GetTicks64() + control_timeout_or_default(request->args.timeout_ms);
                /* wait_event_name only ever needs to hold short identifiers
                 * like "step-complete" (see control_runtime_event_name());
                 * an oversized client-supplied name is truncated and simply
                 * won't match any real event, which is a safe fail-closed
                 * outcome, not a bug. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
                snprintf(
                    deferred->wait_event_name,
                    sizeof(deferred->wait_event_name),
                    "%s",
                    request->args.text);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
                return;
            } else {
                control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
            }
            break;

        case CONTROL_COMMAND_ASSEMBLE:
            if (deferred != NULL && deferred->active) {
                control_protocol_format_error(&response, request->id, "busy", "deferred-response-active", false);
            } else if (request->args.text[0] == '\0') {
                control_protocol_format_error(&response, request->id, "bad-args", "expected source path", false);
            } else {
                /* Auto-pause so assembly lands in a defined machine state. The
                   runtime's own reset/auto-run handling then applies. */
                runtime_client_pause(client);
                if (runtime_client_assemble_file_full(
                        client,
                        request->args.text,
                        request->args.address,
                        request->args.run_address,
                        request->args.auto_run,
                        request->args.reset_first)) {
                    if (deferred != NULL) {
                        deferred->active = true;
                        deferred->request_id = request->id;
                        deferred->command_type = request->type;
                        deferred->deadline_ms = SDL_GetTicks64() + 10000u;
                        return;
                    }
                    control_protocol_format_error(&response, request->id, "internal", "deferred state unavailable", false);
                } else {
                    control_protocol_format_error(&response, request->id, "runtime", "command rejected", false);
                }
            }
            break;

        case CONTROL_COMMAND_FIND_SYMBOL:
            if (control_cache == NULL || !control_cache->has_symbols) {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "not-ready",
                    "no symbols available; assemble or load symbols first",
                    false);
            } else {
                const runtime_symbol_snapshot *syms = &control_cache->symbols;
                bool found = false;
                size_t i;
                for (i = 0; i < syms->count; i++) {
                    if (strncmp(syms->entries[i].name, request->args.text, RUNTIME_SYMBOL_NAME_MAX) == 0) {
                        char text[CONTROL_RESPONSE_TEXT_MAX];
                        snprintf(
                            text,
                            sizeof(text),
                            "address=$%04X name=%s",
                            (unsigned)syms->entries[i].address,
                            syms->entries[i].name);
                        control_protocol_format_ok(&response, request->id, text, false);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    control_protocol_format_error(&response, request->id, "not-found", "symbol not found", false);
                }
            }
            break;

        case CONTROL_COMMAND_NONE:
        default:
            control_protocol_format_error(
                &response,
                request->id,
                "unknown-command",
                "unknown command",
                false);
            break;
    }

    switch (request->type) {
        case CONTROL_COMMAND_RESET:
        case CONTROL_COMMAND_RUN:
        case CONTROL_COMMAND_PAUSE:
        case CONTROL_COMMAND_STEP_CYCLE:
        case CONTROL_COMMAND_STEP_INSTRUCTION:
        case CONTROL_COMMAND_STEP_OVER:
        case CONTROL_COMMAND_STEP_OUT:
        case CONTROL_COMMAND_RUN_CYCLES:
        case CONTROL_COMMAND_RUN_INSTRUCTIONS:
        case CONTROL_COMMAND_RUN_TO:
        case CONTROL_COMMAND_KEY_DOWN:
        case CONTROL_COMMAND_KEY_UP:
        case CONTROL_COMMAND_RESTORE:
        case CONTROL_COMMAND_JOYSTICK:
        case CONTROL_COMMAND_PASTE_TEXT:
        case CONTROL_COMMAND_PASTE_EVENTS:
        case CONTROL_COMMAND_PASTE_TEXT_DATA:
        case CONTROL_COMMAND_PASTE_EVENTS_DATA:
        case CONTROL_COMMAND_LOAD_PRG:
        case CONTROL_COMMAND_LOAD_BIN:
        case CONTROL_COMMAND_SAVE_BIN:
        case CONTROL_COMMAND_MOUNT_D64:
        case CONTROL_COMMAND_UNMOUNT_DISK:
            if (accepted) {
                control_protocol_format_ok(&response, request->id, "accepted=1", false);
                request_debug_state(client);
            } else if (response.text[0] == '\0') {
                control_protocol_format_error(
                    &response,
                    request->id,
                    "runtime",
                    "command rejected",
                    false);
            }
            break;
        default:
            break;
    }

    if (!control_server_post_response(control, &response)) {
        control_response_release(&response);
        SDL_Log("control: response queue full");
    }
}

static void dispatch_control_requests(
    control_server *control,
    runtime_client *client,
    const frontend_debug_state *debug_state,
    const control_cached_state *control_cache,
    deferred_control_response *deferred)
{
    control_request request;

    if (control == NULL) {
        return;
    }

    while (control_server_poll_request(control, &request)) {
        dispatch_control_request(control, client, debug_state, control_cache, deferred, &request);
        control_request_release(&request);
    }
}

static void update_window_title(
    platform_window *window,
    frontend_runtime_state state,
    runtime_stop_reason stop_reason) {
    char title[64];

    switch (state) {
        case FRONTEND_RUNTIME_STATE_RUNNING:
            snprintf(title, sizeof(title), "c64m - Running");
            break;
        case FRONTEND_RUNTIME_STATE_PAUSED:
            snprintf(title, sizeof(title), "c64m - Paused (%s)",
                frontend_stop_reason_name(stop_reason));
            break;
        case FRONTEND_RUNTIME_STATE_ERROR:
            snprintf(title, sizeof(title), "c64m - Error");
            break;
        case FRONTEND_RUNTIME_STATE_UNKNOWN:
        default:
            snprintf(title, sizeof(title), "c64m");
            break;
    }
    platform_window_set_title(window, title);
}

static bool run_main_loop(
    platform_window *window,
    runtime_client *client,
    frontend *ui,
    app_options *options,
    control_server *control) {
    bool running = true;
    bool ui_visible = false;
    bool title_set = false;
    /* Keep SDL text input off unless a UI text field is focused, so holding a
       key for joystick/keyboard emulation never triggers the macOS
       press-and-hold accent popup. Seed from SDL's current state so the first
       frame only calls SDL when the desired state actually differs. */
    bool text_input_active = SDL_IsTextInputActive() == SDL_TRUE;
    frontend_runtime_state last_title_state = FRONTEND_RUNTIME_STATE_UNKNOWN;
    runtime_stop_reason last_title_stop_reason = RUNTIME_STOP_REASON_NONE;
    frontend_input_mapper input_mapper;
    sdl_c64_controller_state controller_state;
    frontend_joystick_input kbd_joystick;
    deferred_control_response deferred_control = {0};
    control_cached_state control_cache = {0};
    frontend_debug_state debug_state = {
        .runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN,
    };

    frontend_input_mapper_reset(&input_mapper);
    sdl_c64_controllers_reset(&controller_state);
    frontend_joystick_reset(&kbd_joystick);
    frontend_joystick_set_layout(
        &kbd_joystick,
        frontend_joystick_layout_from_string(options->keyboard_joystick_layout));
    frontend_joystick_set_port(&kbd_joystick, (unsigned)options->keyboard_joystick_port);
    controller_state.kbd_joystick = &kbd_joystick;
    sdl_c64_controllers_open_existing(&controller_state, client);
    sdl_c64_controller_send_ports(&controller_state, client);
    request_debug_state(client);
    runtime_client_request_frame(client);

    while (running) {
        SDL_Event event;

        frontend_begin_input(ui);
        while (SDL_PollEvent(&event)) {
            bool send_event_to_frontend = ui_visible || frontend_help_is_open(ui);

            sdl_c64_controller_handle_event(&controller_state, client, &event);

            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat != 0 &&
                       frontend_handle_help_key(ui, &event.key, options->scroll_wheel_lines)) {
                send_event_to_frontend = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat != 0 &&
                       !frontend_help_is_open(ui) &&
                       handle_step_key_event(client, &debug_state, &event.key, false)) {
                send_event_to_frontend = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (frontend_input_is_host_quit_shortcut(&event.key)) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_h &&
                           frontend_input_has_option_modifier(&event.key)) {
                    if (frontend_help_is_open(ui)) {
                        close_help(ui, client, &debug_state);
                    } else {
                        open_help(ui, client, &debug_state);
                    }
                    send_event_to_frontend = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && frontend_help_is_open(ui)) {
                    close_help(ui, client, &debug_state);
                    send_event_to_frontend = false;
                } else if (frontend_handle_help_key(ui, &event.key, options->scroll_wheel_lines)) {
                    send_event_to_frontend = false;
                } else if (frontend_help_is_open(ui)) {
                    send_event_to_frontend = true;
                } else if (event.key.keysym.sym == SDLK_F9) {
                    ui_visible = !ui_visible;
                    SDL_Log("ui_visible=%s", ui_visible ? "true" : "false");
                    {
                        int min_w = 0;
                        int min_h = 0;
                        if (ui_visible) {
                            frontend_debug_min_window_size(ui, &min_w, &min_h);
                        }
                        platform_window_set_minimum_size(window, min_w, min_h);
                    }
                } else if (handle_step_key_event(client, &debug_state, &event.key, true)) {
                    send_event_to_frontend = false;
                } else if (event.key.keysym.sym == SDLK_F10 &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    debug_state.step_cycle_start = debug_state.machine_cycle;
                    debug_state.step_cpu_cycle_start = debug_state.cpu.cycles;
                    send_step_out_command(client);
                } else if (event.key.keysym.sym == SDLK_F12 &&
                           !frontend_input_has_shift_modifier(&event.key)) {
                    debug_state.step_cycle_start = debug_state.machine_cycle;
                    debug_state.step_cpu_cycle_start = debug_state.cpu.cycles;
                    send_run_command(client);
                } else if (event.key.keysym.sym == SDLK_F12 &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    debug_state.step_cycle_start = debug_state.machine_cycle;
                    debug_state.step_cpu_cycle_start = debug_state.cpu.cycles;
                    send_run_to_cursor_command(client, ui, &debug_state);
                } else if (event.key.keysym.sym == SDLK_t &&
                           frontend_input_has_option_modifier(&event.key)) {
                    runtime_client_cycle_turbo_speed(client);
                } else if (key_is_quicksave_shortcut(&event.key)) {
                    send_quicksave(client, options, ui);
                    send_event_to_frontend = false;
                } else if (key_is_quickload_shortcut(&event.key)) {
                    send_quickload(client, options, ui);
                    send_event_to_frontend = false;
                } else if ((event.key.keysym.sym == SDLK_0 ||
                            event.key.keysym.sym == SDLK_1 ||
                            event.key.keysym.sym == SDLK_2) &&
                           frontend_input_has_option_modifier(&event.key) &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    unsigned requested = event.key.keysym.sym == SDLK_1 ? 1u :
                                         event.key.keysym.sym == SDLK_2 ? 2u : 0u;
                    /* Port 0 explicitly disables; re-pressing the assigned port
                       also toggles it back off. */
                    unsigned next = kbd_joystick.port == requested ? 0u : requested;
                    frontend_joystick_set_port(&kbd_joystick, next);
                    options->keyboard_joystick_port = (int)next;
                    sdl_c64_controller_send_ports(&controller_state, client);
                    /* Keep the (closed) config dialog seed consistent with the
                       live port so it does not later overwrite this choice. */
                    if (!frontend_config_dialog_is_open(ui)) {
                        frontend_set_config_state(ui, options);
                    }
                    if (next == 0u) {
                        SDL_Log("keyboard joystick disabled");
                    } else {
                        SDL_Log("keyboard joystick assigned to port %u (%s)",
                                next,
                                frontend_joystick_layout_to_string(kbd_joystick.layout));
                    }
                } else if ((event.key.keysym.sym == SDLK_1 || event.key.keysym.sym == SDLK_2) &&
                           frontend_input_has_option_modifier(&event.key)) {
                    sdl_c64_controller_switch_mapping(
                        &controller_state,
                        client,
                        event.key.keysym.sym == SDLK_1 ? 1u : 2u);
                } else if (event.key.keysym.sym == SDLK_INSERT &&
                           frontend_input_has_option_modifier(&event.key) &&
                           frontend_input_has_shift_modifier(&event.key)) {
                    char *text = SDL_GetClipboardText();
                    if (text && text[0] != '\0') {
                        paste_event_t       events[PASTE_EVENTS_MAX];
                        size_t              count = 0;
                        paste_parse_error_t perr  = { -1, NULL };
                        if (paste_parse(text, events, PASTE_EVENTS_MAX, &count, &perr)
                                && count > 0) {
                            runtime_client_paste_events(client, events, count);
                        } else if (perr.offset >= 0) {
                            SDL_Log("paste parse error at offset %d: %s",
                                    perr.offset, perr.message ? perr.message : "");
                        }
                    }
                    SDL_free(text);
                } else if (event.key.keysym.sym == SDLK_INSERT &&
                           frontend_input_has_option_modifier(&event.key) &&
                           !frontend_input_has_shift_modifier(&event.key)) {
                    char *text = SDL_GetClipboardText();
                    if (text && text[0] != '\0') {
                        runtime_client_paste_text_buffer(client, text, strlen(text));
                    }
                    SDL_free(text);
                } else if (ui_visible && frontend_handle_view_cycle_key(ui, &event.key)) {
                    send_event_to_frontend = false;
                } else if (!ui_visible || frontend_routes_keyboard_to_c64(ui)) {
                    if (frontend_joystick_consumes(&kbd_joystick, event.key.keysym.sym)) {
                        if (frontend_joystick_handle_key(&kbd_joystick, &event.key)) {
                            sdl_c64_controller_send_ports(&controller_state, client);
                        }
                    } else {
                        handle_keyboard_input(&input_mapper, client, &event.key);
                    }
                    send_event_to_frontend = false;
                }
            } else if (event.type == SDL_KEYUP &&
                       !frontend_help_is_open(ui) &&
                       (!ui_visible || frontend_routes_keyboard_to_c64(ui))) {
                if (frontend_joystick_consumes(&kbd_joystick, event.key.keysym.sym)) {
                    if (frontend_joystick_handle_key(&kbd_joystick, &event.key)) {
                        sdl_c64_controller_send_ports(&controller_state, client);
                    }
                } else {
                    handle_keyboard_input(&input_mapper, client, &event.key);
                }
                send_event_to_frontend = false;
            } else if (event.type == SDL_DROPFILE) {
                handle_drop_file(client, options, event.drop.file);
                send_event_to_frontend = false;
            }

            if (send_event_to_frontend) {
                frontend_handle_event(ui, &event);
            }
        }
        frontend_end_input(ui);

        poll_runtime_events(
            client,
            ui,
            &debug_state,
            options,
            &controller_state,
            &kbd_joystick,
            control,
            &deferred_control,
            &control_cache);
        check_deferred_control_timeout(control, &deferred_control);
        dispatch_control_requests(
            control,
            client,
            &debug_state,
            &control_cache,
            &deferred_control);

        if (!title_set ||
            debug_state.runtime_state != last_title_state ||
            debug_state.stop_reason != last_title_stop_reason) {
            update_window_title(window, debug_state.runtime_state, debug_state.stop_reason);
            last_title_state = debug_state.runtime_state;
            last_title_stop_reason = debug_state.stop_reason;
            title_set = true;
        }

        if (!platform_window_clear(window)) {
            return false;
        }
        frontend_render(ui, ui_visible, &debug_state);
        {
            /* frontend_render has just built this frame's UI, so the edit-focus
               state is now current. Sync SDL text input to it. */
            bool want_text_input = frontend_wants_text_input(ui);
            if (want_text_input != text_input_active) {
                if (want_text_input) {
                    SDL_StartTextInput();
                } else {
                    SDL_StopTextInput();
                }
                text_input_active = want_text_input;
            }
        }
        dispatch_debugger_intents(client, ui, options, &controller_state, &kbd_joystick);
        platform_window_present(window);
    }
    sdl_c64_controllers_close(&controller_state, client);

    return true;
}

static bool run_headless_loop(
    runtime_client *client,
    app_options *options,
    control_server *control)
{
    bool running = true;
    deferred_control_response deferred_control = {0};
    control_cached_state control_cache = {0};
    frontend_debug_state debug_state = {
        .runtime_state = FRONTEND_RUNTIME_STATE_UNKNOWN,
    };

    request_debug_state(client);
    runtime_client_request_frame(client);

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        poll_runtime_events(
            client,
            NULL,
            &debug_state,
            options,
            NULL,
            NULL,
            control,
            &deferred_control,
            &control_cache);
        check_deferred_control_timeout(control, &deferred_control);
        dispatch_control_requests(
            control,
            client,
            &debug_state,
            &control_cache,
            &deferred_control);
        SDL_Delay(1);
    }

    return true;
}

int main(int argc, char **argv) {
    app_options options;
    runtime *rt = NULL;
    runtime_config runtime_cfg;
    runtime_client *client = NULL;
    frontend *ui = NULL;
    platform_window *window = NULL;
    control_server *control = NULL;
    platform_window_config window_config;
    frontend_layout_state layout_state;
    audio_buffer *abuf = NULL;
    platform_audio *paudio = NULL;
    char runtime_symbol_files[1024];
    int exit_code = 0;
    bool platform_started = false;

    if (!app_options_load_startup(&options, argc, argv)) {
        return 1;
    }

    if (options.headless) {
        if (!platform_init_headless()) {
            app_options_destroy(&options);
            return 1;
        }
        platform_started = true;
    }

    /* Create the shared audio buffer and open the SDL audio device before
       starting the runtime thread so the actual sample rate is known at
       runtime_create time.  platform_audio_init initialises SDL_INIT_AUDIO
       internally, so this may safely precede platform_init(). */
    if (!options.headless) {
        abuf = audio_buffer_create(8192);
    }
    if (abuf != NULL) {
        platform_audio_desc audio_desc;
        audio_desc.requested_rate             = 48000;
        audio_desc.requested_channels         = 2;
        audio_desc.requested_callback_samples = 512;
        audio_desc.buffer                     = abuf;
        paudio = platform_audio_create(&audio_desc);
        if (paudio == NULL) {
            SDL_Log("audio: failed to open device, running without audio");
            audio_buffer_destroy(abuf);
            abuf = NULL;
        }
    } else {
        SDL_Log("audio: failed to allocate buffer, running without audio");
    }

    if (!runtime_init()) {
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        if (platform_started) {
            platform_shutdown();
        }
        app_options_destroy(&options);
        return 1;
    }

    runtime_cfg.basic_rom_path = options.basic_rom_path;
    runtime_cfg.char_rom_path = options.char_rom_path;
    runtime_cfg.kernal_rom_path = options.kernal_rom_path;
    runtime_cfg.system_rom_path = options.system_rom_path;
    runtime_cfg.rom1541_path = options.rom1541_path;
    runtime_cfg.ini_path = options.ini_path;
    if (app_options_symbol_files_absolute(&options, runtime_symbol_files, sizeof(runtime_symbol_files))) {
        runtime_cfg.symbol_files = runtime_symbol_files;
    } else {
        runtime_cfg.symbol_files = options.symbol_files;
    }
    runtime_cfg.use_ini = options.use_ini;
    runtime_cfg.save_ini = (options.save_ini || options.remember) && !options.no_save_ini;
    runtime_cfg.machine_config = machine_config_from_options(&options);
    runtime_cfg.audio_out         = abuf;
    runtime_cfg.audio_sample_rate = platform_audio_actual_rate(paudio);
    if (runtime_cfg.audio_sample_rate <= 0 && options.audio_record_path != NULL) {
        runtime_cfg.audio_sample_rate = 48000;
    }
    runtime_cfg.audio_record_path = options.audio_record_path;
    runtime_cfg.audio_record_start_seconds = options.audio_record_start_seconds;
    runtime_cfg.audio_record_duration_seconds = options.audio_record_duration_seconds;
    runtime_cfg.audio_smoke       = options.audio_smoke ? 1 : 0;
    runtime_cfg.autorun           = options.autorun;
    {
        runtime_config turbo_cfg = runtime_config_from_options(&options);
        memcpy(runtime_cfg.turbo_speeds, turbo_cfg.turbo_speeds, sizeof(runtime_cfg.turbo_speeds));
        runtime_cfg.turbo_speed_count = turbo_cfg.turbo_speed_count;
        runtime_cfg.active_turbo_multiplier = turbo_cfg.active_turbo_multiplier;
    }

    rt = runtime_create(&runtime_cfg);
    if (rt == NULL) {
        runtime_shutdown();
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        if (platform_started) {
            platform_shutdown();
        }
        app_options_destroy(&options);
        return 1;
    }

    if (!runtime_start(rt)) {
        runtime_destroy(rt);
        runtime_shutdown();
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        if (platform_started) {
            platform_shutdown();
        }
        app_options_destroy(&options);
        return 1;
    }
    client = runtime_get_client(rt);

    {
        int i;
        for (i = 0; i < C64M_DRIVE_COUNT; ++i) {
            if (options.disk_slots[i].count > 0) {
                runtime_client_mount_d64_ex(
                    client,
                    (uint8_t)i,
                    options.disk_slots[i].paths[0],
                    app_disk_slot_current_writable(&options.disk_slots[i]));
            }
        }
    }

    if (options.headless) {
        if (options.control_port > 0) {
            control = control_server_create((uint16_t)options.control_port);
            if (control == NULL || !control_server_start(control)) {
                SDL_Log("control: failed to listen on 127.0.0.1:%d", options.control_port);
                control_server_destroy(control);
                runtime_destroy(rt);
                runtime_shutdown();
                audio_buffer_destroy(abuf);
                platform_shutdown();
                app_options_destroy(&options);
                return 1;
            }
        }

        send_run_command(client);

        if (options.crt_path != NULL) {
            runtime_client_load_crt(client, options.crt_path);
        } else if (options.prg_path != NULL) {
            runtime_client_load_prg(client, options.prg_path);
        } else if (options.basic_path != NULL) {
            runtime_client_load_bin(client, options.basic_path, 0, true, true, true, false);
        }

        if (!run_headless_loop(client, &options, control)) {
            exit_code = 1;
        }

        control_server_stop(control);
        runtime_client_quit(client);
        runtime_stop(rt);
        poll_runtime_events(client, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        if (!runtime_save_debug_ini(rt)) {
            SDL_Log("failed to save debug ini data: %s", options.ini_path ? options.ini_path : "(null)");
        }
        control_server_destroy(control);
        runtime_destroy(rt);
        runtime_shutdown();
        audio_buffer_destroy(abuf);
        platform_shutdown();
        app_options_destroy(&options);
        return exit_code;
    }

    /* Start audio playback now that the runtime thread is producing samples. */
    platform_audio_start(paudio);

    if (!platform_init()) {
        runtime_destroy(rt);
        runtime_shutdown();
        platform_audio_destroy(paudio);
        audio_buffer_destroy(abuf);
        if (platform_started) {
            platform_shutdown();
        }
        app_options_destroy(&options);
        return 1;
    }
    platform_started = true;

    window_config.window_width = options.window_width;
    window_config.window_height = options.window_height;

    window = platform_window_create(&window_config);
    if (window == NULL) {
        platform_audio_destroy(paudio);
        platform_shutdown();
        runtime_destroy(rt);
        runtime_shutdown();
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    ui = frontend_create(window);
    if (ui == NULL) {
        platform_window_destroy(window);
        platform_audio_destroy(paudio);
        platform_shutdown();
        runtime_destroy(rt);
        runtime_shutdown();
        audio_buffer_destroy(abuf);
        app_options_destroy(&options);
        return 1;
    }

    layout_state.split_display_right = options.layout_split_display_right;
    layout_state.split_top_bottom = options.layout_split_top_bottom;
    layout_state.split_memory_misc = options.layout_split_memory_misc;
    frontend_set_layout_state(ui, &layout_state);
    frontend_set_config_state(ui, &options);
    frontend_set_disk_queue(ui, 8, &options.disk_slots[8]);
    frontend_set_disk_queue(ui, 9, &options.disk_slots[9]);
    /* Seed the file browser's remembered folders from the INI. The slot enum and
       options.browse_dirs share the same order (see frontend_browse_slot). */
    {
        int slot;
        for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT && slot < APP_BROWSE_DIR_COUNT; ++slot) {
            frontend_set_browse_dir(ui, (frontend_browse_slot)slot, options.browse_dirs[slot]);
        }
    }
    {
        frontend_assembler_options asm_opts;
        memset(&asm_opts, 0, sizeof(asm_opts));
        if (options.assembler_file != NULL) {
            snprintf(asm_opts.file, sizeof(asm_opts.file), "%s", options.assembler_file);
        }
        if (options.assembler_address != NULL) {
            snprintf(asm_opts.address, sizeof(asm_opts.address), "%s", options.assembler_address);
        }
        if (options.assembler_run_address != NULL) {
            snprintf(asm_opts.run_address, sizeof(asm_opts.run_address), "%s", options.assembler_run_address);
        }
        asm_opts.auto_run = options.assembler_auto_run;
        asm_opts.reset_first = options.assembler_reset_first;
        asm_opts.rearm_oneshots = options.assembler_rearm_oneshots;
        frontend_set_assembler_options(ui, &asm_opts);
    }

    if (options.control_port > 0) {
        control = control_server_create((uint16_t)options.control_port);
        if (control == NULL || !control_server_start(control)) {
            SDL_Log("control: failed to listen on 127.0.0.1:%d", options.control_port);
            control_server_destroy(control);
            frontend_destroy(ui);
            platform_window_destroy(window);
            platform_audio_destroy(paudio);
            platform_shutdown();
            runtime_destroy(rt);
            runtime_shutdown();
            audio_buffer_destroy(abuf);
            app_options_destroy(&options);
            return 1;
        }
    }

    send_run_command(client);

    if (options.crt_path != NULL) {
        runtime_client_load_crt(client, options.crt_path);
    } else if (options.prg_path != NULL) {
        runtime_client_load_prg(client, options.prg_path);
    } else if (options.basic_path != NULL) {
        runtime_client_load_bin(client, options.basic_path, 0, true, true, true, false);
    }

    if (!run_main_loop(window, client, ui, &options, control)) {
        exit_code = 1;
    }

    control_server_stop(control);
    runtime_client_quit(client);
    runtime_stop(rt);
    poll_runtime_events(client, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (!runtime_save_debug_ini(rt)) {
        SDL_Log("failed to save debug ini data: %s", options.ini_path ? options.ini_path : "(null)");
    }

    platform_window_get_size(window, &options.window_width, &options.window_height);
    frontend_get_layout_state(ui, &layout_state);
    options.layout_split_display_right = layout_state.split_display_right;
    options.layout_split_top_bottom = layout_state.split_top_bottom;
    options.layout_split_memory_misc = layout_state.split_memory_misc;
    {
        frontend_assembler_options asm_opts;
        char assembler_path[1024];
        frontend_get_assembler_options(ui, &asm_opts);
        if (asm_opts.file[0] != '\0' &&
            app_options_path_absolute_from_ini(
                &options, asm_opts.file, assembler_path, sizeof(assembler_path))) {
            app_options_set_string(&options.assembler_file, assembler_path);
        } else {
            app_options_set_string(&options.assembler_file,
                asm_opts.file[0] ? asm_opts.file : NULL);
        }
        app_options_set_string(&options.assembler_address,
            asm_opts.address[0] ? asm_opts.address : NULL);
        app_options_set_string(&options.assembler_run_address,
            asm_opts.run_address[0] ? asm_opts.run_address : NULL);
        options.assembler_auto_run = asm_opts.auto_run;
        options.assembler_reset_first = asm_opts.reset_first;
        options.assembler_rearm_oneshots = asm_opts.rearm_oneshots;
    }
    /* Pull the file browser's remembered folders back into options so they are
       written to the INI (same slot order as frontend_browse_slot). */
    {
        int slot;
        for (slot = 0; slot < FRONTEND_BROWSE_SLOT_COUNT && slot < APP_BROWSE_DIR_COUNT; ++slot) {
            const char *dir = frontend_get_browse_dir(ui, (frontend_browse_slot)slot);
            app_options_set_string(&options.browse_dirs[slot], dir[0] ? dir : NULL);
        }
    }
    if ((options.save_ini || options.remember) && !app_options_save_shutdown(&options)) {
        SDL_Log("failed to save ini file: %s", options.ini_path ? options.ini_path : "(null)");
    }

    frontend_destroy(ui);
    control_server_destroy(control);
    platform_window_destroy(window);
    /* Stop and destroy the audio device before SDL_Quit so the device handle
       remains valid.  Runtime thread is already joined at this point. */
    platform_audio_destroy(paudio);
    platform_shutdown();
    runtime_destroy(rt);
    runtime_shutdown();
    audio_buffer_destroy(abuf);
    app_options_destroy(&options);
    return exit_code;
}
