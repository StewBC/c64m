#pragma once

#include <stdbool.h>
#include <stddef.h>

#define C64M_DRIVE_COUNT 16
#define C64M_DEFAULT_DISPLAY_WIDTH 384
#define C64M_DEFAULT_DISPLAY_HEIGHT 272

typedef struct app_options {
    bool use_ini;
    bool save_ini;
    bool remember;
    bool defaults;
    bool no_save_ini;
    int scroll_wheel_lines;
    char *ini_path;
    char *disk_images[C64M_DRIVE_COUNT];
    char *breakpoint;
    char *turbo_multipliers;
    char *symbol_files;
    char *video_standard;
    int display_width;
    int display_height;
    bool integer_scale;
    bool aspect_correct;
    char *video_filter;
    int window_width;
    int window_height;
    float layout_split_display_right;
    float layout_split_top_bottom;
    float layout_split_memory_misc;
    int layout_display_width;
    int layout_display_height;
    char *basic_rom_path;
    char *char_rom_path;
    char *kernal_rom_path;
    char *system_rom_path;
    char *prg_path;
    char *basic_path;
    /* When true, runtime emits a 440 Hz square wave via the audio path to
       verify that samples reach the host audio device without needing SID. */
    bool audio_smoke;
} app_options;

void app_options_init(app_options *options);
bool app_options_load_startup(app_options *options, int argc, char **argv);
bool app_options_save_shutdown(const app_options *options);
bool app_options_copy(app_options *dest, const app_options *src);
bool app_options_apply_ini_file(app_options *options, const char *path);
bool app_options_set_string(char **target, const char *value);
bool app_options_path_relative_to_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size);
bool app_options_symbol_files_absolute(
    const app_options *options,
    char *out,
    size_t out_size);
void app_options_destroy(app_options *options);
