#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_buffer.h"
#include "c64.h"

typedef struct runtime runtime;
typedef struct runtime_client runtime_client;

typedef struct runtime_config {
    const char *basic_rom_path;
    const char *char_rom_path;
    const char *kernal_rom_path;
    const char *system_rom_path;
    const char *rom1541_path;
    const char *ini_path;
    const char *symbol_files;
    bool use_ini;
    bool save_ini;
    c64_config machine_config;
    uint32_t turbo_speeds[16];
    uint8_t turbo_speed_count;
    uint32_t active_turbo_multiplier;
    /* Audio: pointer to the shared util audio buffer (not owned by runtime).
       Null and zero are valid — runtime runs silently if audio is unavailable. */
    audio_buffer *audio_out;
    int audio_sample_rate;
    const char *audio_record_path;
    double audio_record_start_seconds;
    double audio_record_duration_seconds;
    /* When non-zero, runtime emits a 440 Hz square-wave smoke tone instead of
       silence, proving the audio path without needing SID. */
    int audio_smoke;
    /* When true, automatically inject RUN after a PRG/BASIC load, or
       LOAD"*",8 + RUN after mounting D64/G64 media into an empty device 8.
       Replacing already-mounted media is a disk swap and does not autorun. */
    bool autorun;
} runtime_config;

void runtime_config_set_turbo_defaults(runtime_config *config);
bool runtime_config_set_turbo_csv(runtime_config *config, const char *csv);

bool runtime_init();
void runtime_shutdown();

runtime *runtime_create(const runtime_config *config);
void runtime_destroy(runtime *rt);

bool runtime_start(runtime *rt);
void runtime_stop(runtime *rt);
bool runtime_save_debug_ini(runtime *rt);

runtime_client *runtime_get_client(runtime *rt);
