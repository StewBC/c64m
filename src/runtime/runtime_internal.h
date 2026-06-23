#pragma once

#include "runtime.h"
#include "runtime_client.h"
#include "runtime_command.h"

#include "audio_buffer.h"
#include "c64.h"
#include "c64_rom.h"
#include "mutex.h"
#include "symbol_table.h"

#include <stdbool.h>
#include <stdio.h>

#define RUNTIME_COMMAND_QUEUE_CAPACITY 256
#define RUNTIME_EVENT_QUEUE_CAPACITY 256
#define RUNTIME_BREAKPOINT_CAPACITY 64

typedef struct message_queue message_queue;
typedef struct thread thread;

typedef enum runtime_exec_state {
    RUNTIME_EXEC_STOPPED = 0,
    RUNTIME_EXEC_PAUSED,
    RUNTIME_EXEC_RUNNING
} runtime_exec_state;

typedef enum runtime_speed_mode {
    RUNTIME_SPEED_MODE_SLOW = 0,
    RUNTIME_SPEED_MODE_FAST
} runtime_speed_mode;

struct runtime_client {
    message_queue *command_queue;
    message_queue *event_queue;
    struct runtime_frame_slot *frame_slot;
    struct runtime_symbol_slot *symbol_slot;
};

typedef struct paste_state {
    char text[RUNTIME_PASTE_TEXT_MAX];
    size_t length;
    size_t position;
    uint64_t phase_end_cycle;
    bool shift_needed;
    bool in_gap;
    bool use_buffer;
} paste_state;

typedef struct runtime_frame_slot {
    mutex *mutex;
    /* Single copied handoff slot. The frontend consumes the latest complete frame. */
    c64_frame frame;
    bool has_frame;
    uint64_t published_frames;
    uint64_t consumed_frames;
    uint64_t dropped_frames;
} runtime_frame_slot;

typedef struct runtime_symbol_slot {
    mutex *mutex;
    runtime_symbol_snapshot snapshot;
    bool has_symbols;
} runtime_symbol_slot;

typedef struct runtime_breakpoint {
    uint32_t id;
    bool enabled;
    uint16_t start_address;
    uint16_t end_address;
    bool has_end_address;
    uint32_t access_mask;
    runtime_breakpoint_mapping mapping;
    uint32_t action_mask;
    bool use_counter;
    uint32_t initial_count;
    uint32_t reset_count;
    uint32_t counter;
    uint32_t current_hits;
} runtime_breakpoint;

struct runtime {
    thread *thread;
    message_queue *command_queue;
    message_queue *event_queue;
    runtime_client client;
    runtime_frame_slot frame_slot;
    c64_frame publish_frame;
    runtime_symbol_slot symbol_slot;
    symbol_table *symbols;
    c64_t machine;
    c64_rom_set roms;
    char *basic_rom_path;
    char *char_rom_path;
    char *kernal_rom_path;
    char *system_rom_path;
    char *ini_path;
    char *symbol_files;
    bool use_ini;
    bool save_ini;
    c64_config machine_config;
    uint32_t turbo_speeds[16];
    uint8_t turbo_speed_count;
    uint32_t active_turbo_multiplier;
    runtime_exec_state exec_state;
    runtime_stop_reason last_stop_reason;
    runtime_speed_mode speed_mode;
    bool trace_enabled;
    FILE *trace_file;
    runtime_breakpoint breakpoints[RUNTIME_BREAKPOINT_CAPACITY];
    size_t breakpoint_count;
    uint32_t next_breakpoint_id;
    bool breakpoint_hit_pending;
    bool suppress_execute_bp;
    bool temp_bp_active;
    uint16_t temp_bp_address;
    uint64_t next_frame_cycle;
    uint64_t next_frame_counter;
    uint64_t frame_counter_step;
    bool pace_initialized;
    bool started;
    /* Audio: shared buffer pointer (not owned). Null when audio is disabled. */
    audio_buffer *audio_out;
    int audio_sample_rate;
    char *audio_record_path;
    FILE *audio_record_file;
    double audio_record_start_seconds;
    double audio_record_duration_seconds;
    uint64_t audio_record_seen_samples;
    uint64_t audio_record_written_samples;
    uint64_t audio_record_target_samples;
    bool audio_record_failed;
    bool audio_record_finished;
    double audio_cycle_accum;   /* fractional cycle accumulator for sample timing */
    uint64_t audio_last_cycle;  /* machine cycle at last audio produce call */
    float audio_smoke_phase;    /* square-wave phase accumulator for smoke tone */
    int audio_smoke;            /* non-zero: emit smoke tone instead of silence */
    bool paste_active;
    paste_state paste;
    char *pending_prg_path;
    bool pending_prg_resume_running;
    char *pending_asm_path;
    uint16_t pending_asm_address;
    uint16_t pending_asm_run_address;
    bool pending_asm_auto_run;
    char *pending_bin_path;
    uint16_t pending_bin_address;
    uint8_t pending_bin_use_file_address;
    uint8_t pending_bin_is_basic;
    bool autorun;
    /* 0 = inactive; 1 = inject LOAD"*",8 on next $E38B; 2 = inject RUN on next $E38B */
    int autorun_d64_phase;
};

int runtime_thread_main(void *userdata);
