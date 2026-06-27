#pragma once

#include <stdbool.h>
#include <stddef.h>

#define C64M_DRIVE_COUNT 16
#define C64M_DEFAULT_DISPLAY_WIDTH 384
#define C64M_DEFAULT_DISPLAY_HEIGHT 272

/* Ordered list of disk image paths for one drive slot. */
typedef struct {
    char **paths;
    int    count;
    int    current; /* index of the disk currently (or last) mounted; 0 by default */
} app_disk_slot;

typedef struct app_options {
    bool use_ini;
    bool save_ini;
    bool remember;
    bool defaults;
    bool no_save_ini;
    int scroll_wheel_lines;
    char *ini_path;
    app_disk_slot disk_slots[C64M_DRIVE_COUNT];
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
    char *rom1541_path;
    char *prg_path;
    char *basic_path;
    /* When true, runtime emits a 440 Hz square wave via the audio path to
       verify that samples reach the host audio device without needing SID. */
    bool audio_smoke;
    char *audio_record_path;
    float audio_record_start_seconds;
    float audio_record_duration_seconds;
    /* When true, automatically run after a PRG/BASIC/D64 load. */
    bool autorun;
    /* When true, disk I/O is routed through the genuine 1541 ROM (requires
       rom1541_path to be set); when false, KERNAL LOAD traps handle disk I/O. */
    bool emulate_1541;
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

/* Disk slot helpers used by callers that manage live mount state. */
bool app_disk_slot_set(app_disk_slot *slot, const char *path);
void app_disk_slot_clear(app_disk_slot *slot);
bool app_disk_slot_copy(app_disk_slot *dest, const app_disk_slot *src);

/*
 * Remove the current disk from the queue and advance to the next one
 * (round-robin).  Returns the path that should now be mounted, or NULL if the
 * queue is now empty.  The returned pointer is into slot->paths and is only
 * valid until the next mutation of slot.
 */
const char *app_disk_slot_eject_current(app_disk_slot *slot);

/*
 * Insert path into the queue immediately after the current disk.  If the queue
 * was empty the disk becomes the only entry (current=0).  Returns false on
 * allocation failure.
 */
bool app_disk_slot_add_after_current(app_disk_slot *slot, const char *path);

/*
 * Set current to index without modifying the queue.  Returns the path at that
 * index, or NULL if index is out of range.  The returned pointer is into
 * slot->paths.
 */
const char *app_disk_slot_select(app_disk_slot *slot, int index);
