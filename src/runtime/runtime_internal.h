#pragma once

#include "runtime.h"
#include "runtime_client.h"
#include "runtime_command.h"

#include "c64.h"
#include "c64_rom.h"
#include "mutex.h"

#include <stdbool.h>

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
};

typedef struct paste_state {
    char text[RUNTIME_PASTE_TEXT_MAX];
    size_t length;
    size_t position;
    uint64_t phase_end_cycle;
    bool shift_needed;
    bool in_gap;
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
    c64_t machine;
    c64_rom_set roms;
    char *basic_rom_path;
    char *char_rom_path;
    char *kernal_rom_path;
    char *system_rom_path;
    char *ini_path;
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
    runtime_breakpoint breakpoints[RUNTIME_BREAKPOINT_CAPACITY];
    size_t breakpoint_count;
    uint32_t next_breakpoint_id;
    bool breakpoint_hit_pending;
    uint64_t next_frame_cycle;
    uint64_t next_frame_counter;
    uint64_t frame_counter_step;
    bool pace_initialized;
    bool started;
    bool paste_active;
    paste_state paste;
    char *pending_prg_path;
};

int runtime_thread_main(void *userdata);
