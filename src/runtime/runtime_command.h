#pragma once

#include "keyboard.h"

#include <stddef.h>
#include <stdint.h>

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
    RUNTIME_COMMAND_REQUEST_FRAME,
    RUNTIME_COMMAND_KEYBOARD_KEY,
    RUNTIME_COMMAND_RESTORE,
    RUNTIME_COMMAND_SET_CPU_REGISTER,
    RUNTIME_COMMAND_REQUEST_MEMORY,
    RUNTIME_COMMAND_WRITE_MEMORY_BYTE,
    RUNTIME_COMMAND_SET_EXECUTE_BREAKPOINT,
    RUNTIME_COMMAND_CLEAR_BREAKPOINT,
    RUNTIME_COMMAND_CLEAR_ALL_BREAKPOINTS,
    RUNTIME_COMMAND_SET_BREAKPOINT_ENABLED,
    RUNTIME_COMMAND_REQUEST_BREAKPOINTS,
    RUNTIME_COMMAND_LOAD_PRG
} runtime_command_type;

enum {
    RUNTIME_COMMAND_PATH_MAX = 1024
};

typedef enum runtime_cpu_register {
    RUNTIME_CPU_REGISTER_PC = 0,
    RUNTIME_CPU_REGISTER_SP,
    RUNTIME_CPU_REGISTER_A,
    RUNTIME_CPU_REGISTER_X,
    RUNTIME_CPU_REGISTER_Y,
    RUNTIME_CPU_REGISTER_STATUS
} runtime_cpu_register;

typedef struct runtime_command {
    runtime_command_type type;
    union {
        struct {
            size_t count;
        } run_cycles;

        struct {
            size_t count;
        } run_instructions;

        struct {
            c64_key key;
            uint8_t pressed;
        } keyboard_key;

        struct {
            runtime_cpu_register reg;
            uint16_t value;
        } set_cpu_register;

        struct {
            uint16_t address;
            uint16_t length;
            uint8_t mode;
        } request_memory;

        struct {
            uint16_t address;
            uint8_t value;
            uint8_t mode;
        } write_memory_byte;

        struct {
            uint16_t address;
            uint8_t enabled;
        } set_execute_breakpoint;

        struct {
            uint32_t id;
        } clear_breakpoint;

        struct {
            uint32_t id;
            uint8_t enabled;
        } set_breakpoint_enabled;

        struct {
            char path[RUNTIME_COMMAND_PATH_MAX];
        } load_prg;
    } data;
} runtime_command;
