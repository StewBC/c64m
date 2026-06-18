#pragma once

#include <stdint.h>

typedef enum runtime_event_type {
    RUNTIME_EVENT_NONE = 0,
    RUNTIME_EVENT_PONG,
    RUNTIME_EVENT_STARTED,
    RUNTIME_EVENT_RUNNING,
    RUNTIME_EVENT_PAUSED,
    RUNTIME_EVENT_STOPPED,
    RUNTIME_EVENT_ERROR,
    RUNTIME_EVENT_RESET_COMPLETE,
    RUNTIME_EVENT_STEP_COMPLETE,
    RUNTIME_EVENT_RUN_COMPLETE,
    RUNTIME_EVENT_CPU_STATE_RESPONSE,
    RUNTIME_EVENT_MACHINE_STATE_RESPONSE,
    RUNTIME_EVENT_MEMORY_RESPONSE,
    RUNTIME_EVENT_MEMORY_VIEW_RESPONSE,
    RUNTIME_EVENT_BREAKPOINTS_RESPONSE,
    RUNTIME_EVENT_ASSEMBLE_COMPLETE,
    RUNTIME_EVENT_ASSEMBLE_ERROR,
    RUNTIME_EVENT_FRAME_READY
} runtime_event_type;

typedef enum runtime_memory_mode {
    RUNTIME_MEMORY_MODE_CPU_MAP = 0,
    RUNTIME_MEMORY_MODE_RAM
} runtime_memory_mode;

typedef enum runtime_stop_reason {
    RUNTIME_STOP_REASON_NONE = 0,
    RUNTIME_STOP_REASON_RESET,
    RUNTIME_STOP_REASON_PAUSE_COMMAND,
    RUNTIME_STOP_REASON_STEP,
    RUNTIME_STOP_REASON_RUN_COMPLETE,
    RUNTIME_STOP_REASON_BREAKPOINT,
    RUNTIME_STOP_REASON_ERROR
} runtime_stop_reason;

enum {
    RUNTIME_MEMORY_SNAPSHOT_MAX = 1024,
    RUNTIME_BREAKPOINT_SNAPSHOT_MAX = 64
};

typedef enum runtime_breakpoint_access {
    RUNTIME_BREAKPOINT_ACCESS_EXECUTE = 1u << 0,
    RUNTIME_BREAKPOINT_ACCESS_READ = 1u << 1,
    RUNTIME_BREAKPOINT_ACCESS_WRITE = 1u << 2
} runtime_breakpoint_access;

typedef enum runtime_breakpoint_mapping {
    RUNTIME_BREAKPOINT_MAPPING_MAP = 0,
    RUNTIME_BREAKPOINT_MAPPING_ROM,
    RUNTIME_BREAKPOINT_MAPPING_RAM
} runtime_breakpoint_mapping;

typedef enum runtime_breakpoint_action {
    RUNTIME_BREAKPOINT_ACTION_BREAK = 1u << 0,
    RUNTIME_BREAKPOINT_ACTION_FAST = 1u << 1,
    RUNTIME_BREAKPOINT_ACTION_SLOW = 1u << 2,
    RUNTIME_BREAKPOINT_ACTION_TRON = 1u << 3,
    RUNTIME_BREAKPOINT_ACTION_TROFF = 1u << 4,
    RUNTIME_BREAKPOINT_ACTION_TYPE = 1u << 5,
    RUNTIME_BREAKPOINT_ACTION_SWAP = 1u << 6
} runtime_breakpoint_action;

typedef struct runtime_breakpoint_definition {
    uint8_t enabled;
    uint16_t start_address;
    uint16_t end_address;
    uint8_t has_end_address;
    uint32_t access;
    runtime_breakpoint_mapping mapping;
    uint32_t actions;
    uint8_t use_counter;
    uint32_t initial_count;
    uint32_t reset_count;
} runtime_breakpoint_definition;

typedef struct runtime_cpu_snapshot {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint64_t cycles;
} runtime_cpu_snapshot;

typedef struct runtime_machine_snapshot {
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint8_t ready;
    uint8_t running;
    runtime_stop_reason stop_reason;
    uint64_t frame_number;
    uint64_t frame_cycle;
    uint64_t dropped_frames;
    uint64_t screen_ram_writes;
    uint64_t color_ram_writes;
    uint64_t vic_register_writes;
    uint64_t cia1_register_writes;
    uint64_t cia2_register_writes;
    uint64_t keyboard_events;
    uint64_t irq_entries;
    uint64_t cia1_icr_reads;
    uint64_t cia1_icr_writes;
    uint64_t cia1_interrupt_assertions;
    uint64_t nmi_entries;
    uint64_t restore_requests;
    uint32_t active_turbo_multiplier;
    uint8_t turbo_speed_count;
    uint8_t cia1_irq_pending;
    uint8_t cia2_nmi_pending;
} runtime_machine_snapshot;

typedef struct runtime_memory_snapshot {
    uint16_t address;
    runtime_memory_mode mode;
    uint16_t length;
    uint8_t bytes[RUNTIME_MEMORY_SNAPSHOT_MAX];
} runtime_memory_snapshot;

typedef struct runtime_breakpoint_snapshot_entry {
    uint32_t id;
    uint16_t start_address;
    uint16_t end_address;
    uint8_t has_end_address;
    runtime_breakpoint_access access;
    runtime_breakpoint_mapping mapping;
    uint32_t actions;
    uint8_t enabled;
    uint8_t use_counter;
    uint32_t current_hits;
    uint32_t initial_count;
    uint32_t reset_count;
    uint32_t counter;

    /* Phase 12 compatibility aliases. Prefer start_address and initial_count. */
    uint16_t address;
    uint32_t target_hits;
} runtime_breakpoint_snapshot_entry;

typedef struct runtime_breakpoint_snapshot {
    uint16_t count;
    runtime_breakpoint_snapshot_entry entries[RUNTIME_BREAKPOINT_SNAPSHOT_MAX];
} runtime_breakpoint_snapshot;

typedef struct runtime_event {
    runtime_event_type type;

    union {
        struct {
            int unused;
        } pong;

        struct {
            char message[1024];
        } error;

        struct {
            uint64_t frame_number;
            uint64_t machine_cycle;
            uint64_t dropped_frames;
        } frame_ready;

        runtime_cpu_snapshot cpu_state;
        runtime_machine_snapshot machine_state;
        runtime_memory_snapshot memory;
        runtime_breakpoint_snapshot breakpoints;
        struct {
            uint16_t address;
            char path[1024];
        } assemble;
    } data;
} runtime_event;
