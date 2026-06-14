#pragma once

#include <stddef.h>

typedef enum runtime_command_type {
    RUNTIME_COMMAND_NONE = 0,
    RUNTIME_COMMAND_PING,
    RUNTIME_COMMAND_QUIT,
    RUNTIME_COMMAND_RESET,
    RUNTIME_COMMAND_RUN,
    RUNTIME_COMMAND_PAUSE,
    RUNTIME_COMMAND_STEP_CYCLE,
    RUNTIME_COMMAND_STEP_INSTRUCTION,
    RUNTIME_COMMAND_RUN_CYCLES,
    RUNTIME_COMMAND_RUN_INSTRUCTIONS,
    RUNTIME_COMMAND_REQUEST_CPU_STATE,
    RUNTIME_COMMAND_REQUEST_MACHINE_STATE,
    RUNTIME_COMMAND_REQUEST_FRAME
} runtime_command_type;

typedef struct runtime_command {
    runtime_command_type type;
    union {
        struct {
            size_t count;
        } run_cycles;

        struct {
            size_t count;
        } run_instructions;
    } data;
} runtime_command;
