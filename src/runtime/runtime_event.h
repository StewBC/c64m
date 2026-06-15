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
    RUNTIME_EVENT_FRAME_READY
} runtime_event_type;

typedef enum runtime_memory_mode {
    RUNTIME_MEMORY_MODE_CPU_MAP = 0,
    RUNTIME_MEMORY_MODE_RAM
} runtime_memory_mode;

enum {
    RUNTIME_MEMORY_SNAPSHOT_MAX = 1024
};

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
    uint8_t cia1_irq_pending;
    uint8_t cia2_nmi_pending;
} runtime_machine_snapshot;

typedef struct runtime_memory_snapshot {
    uint16_t address;
    runtime_memory_mode mode;
    uint16_t length;
    uint8_t bytes[RUNTIME_MEMORY_SNAPSHOT_MAX];
} runtime_memory_snapshot;

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
    } data;
} runtime_event;
