#pragma once

typedef enum runtime_command_type {
    RUNTIME_COMMAND_NONE = 0,
    RUNTIME_COMMAND_PING,
    RUNTIME_COMMAND_QUIT,
    RUNTIME_COMMAND_RESET,
    RUNTIME_COMMAND_STEP_INSTRUCTION,
    RUNTIME_COMMAND_REQUEST_CPU_STATE
} runtime_command_type;

typedef struct runtime_command {
    runtime_command_type type;
} runtime_command;
