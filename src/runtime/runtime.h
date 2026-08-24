#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "audio_buffer.h"
#include "c64.h"

typedef struct runtime runtime;
typedef struct runtime_client runtime_client;

/* Turbo mode IDs (not wall-clock multipliers). Stored in active_turbo_multiplier
   and turbo_speeds[] for historical field names / Opt+T list compatibility. */
enum {
    RUNTIME_TURBO_MODE_NORMAL = 1, /* real-time pace, live pixels */
    RUNTIME_TURBO_MODE_MAX = 2,    /* free-run, live pixels (full correctness) */
    RUNTIME_TURBO_MODE_WARP = 3,   /* free-run, paint off (debug frames only) */
    RUNTIME_TURBO_MODE_LAST = RUNTIME_TURBO_MODE_WARP
};

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
    /* Startup-only recorder budget. When history_memory_mb_configured is false,
       runtime_create uses RUNTIME_HISTORY_DEFAULT_MEMORY_MB. */
    uint32_t history_memory_mb;
    bool history_memory_mb_configured;
    /* Startup-only frame-ring budget. When frame_ring_memory_mb_configured is
       false, runtime_create uses RUNTIME_FRAME_RING_DEFAULT_MEMORY_MB. 0
       disables the ring. */
    uint32_t frame_ring_memory_mb;
    bool frame_ring_memory_mb_configured;
    /* Startup-only per-line VIC ring budget; 0 disables. */
    uint32_t vic_ring_memory_mb;
    bool vic_ring_memory_mb_configured;
    /* Inspector recording (opt-in, default off). Does not arm HST1.
       inspector_memory_mb is stored for I1; 0 is an empty tape. When
       inspector_memory_mb_configured is false, runtime_create uses
       RUNTIME_INSPECTOR_DEFAULT_MEMORY_MB. */
    bool inspector;
    uint32_t inspector_memory_mb;
    bool inspector_memory_mb_configured;
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
