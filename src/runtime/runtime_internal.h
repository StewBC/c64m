#pragma once

#include "runtime.h"
#include "runtime_client.h"

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

struct runtime_client {
    message_queue *command_queue;
    message_queue *event_queue;
    struct runtime_frame_slot *frame_slot;
};

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
    uint16_t address;
    bool enabled;
    uint32_t current_hits;
    uint32_t target_hits;
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
    runtime_exec_state exec_state;
    runtime_breakpoint breakpoints[RUNTIME_BREAKPOINT_CAPACITY];
    size_t breakpoint_count;
    uint32_t next_breakpoint_id;
    uint64_t next_frame_cycle;
    uint64_t next_frame_counter;
    uint64_t frame_counter_step;
    bool pace_initialized;
    bool started;
};

int runtime_thread_main(void *userdata);
