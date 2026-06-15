#pragma once

#include <stdbool.h>

#define C64M_DRIVE_COUNT 16
#define C64M_DEFAULT_DISPLAY_WIDTH 384
#define C64M_DEFAULT_DISPLAY_HEIGHT 272

typedef struct app_options {
    bool use_ini;
    bool save_ini;
    bool remember;
    bool defaults;
    bool show_leds;
    bool no_save_ini;
    char *ini_path;
    char *disk_images[C64M_DRIVE_COUNT];
    char *breakpoint;
    char *turbo_multipliers;
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
} app_options;

void app_options_init(app_options *options);
bool app_options_load_startup(app_options *options, int argc, char **argv);
bool app_options_save_shutdown(const app_options *options);
void app_options_destroy(app_options *options);
