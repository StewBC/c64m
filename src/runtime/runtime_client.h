#pragma once

#include "runtime_event.h"

#include "c64_frame.h"
#include "keyboard.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct runtime_client runtime_client;

bool runtime_client_ping(runtime_client *client);
bool runtime_client_quit(runtime_client *client);
bool runtime_client_reset(runtime_client *client);
bool runtime_client_run(runtime_client *client);
bool runtime_client_pause(runtime_client *client);
bool runtime_client_step_cycle(runtime_client *client);
bool runtime_client_step_instruction(runtime_client *client);
bool runtime_client_run_cycles(runtime_client *client, size_t count);
bool runtime_client_run_instructions(runtime_client *client, size_t count);
bool runtime_client_request_cpu_state(runtime_client *client);
bool runtime_client_request_machine_state(runtime_client *client);
bool runtime_client_request_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode);
bool runtime_client_request_frame(runtime_client *client);
bool runtime_client_keyboard_key(runtime_client *client, c64_key key, bool pressed);
bool runtime_client_restore(runtime_client *client);
bool runtime_client_set_pc(runtime_client *client, uint16_t value);
bool runtime_client_set_sp(runtime_client *client, uint8_t value);
bool runtime_client_set_a(runtime_client *client, uint8_t value);
bool runtime_client_set_x(runtime_client *client, uint8_t value);
bool runtime_client_set_y(runtime_client *client, uint8_t value);
bool runtime_client_set_status(runtime_client *client, uint8_t value);
bool runtime_client_write_memory_byte(
    runtime_client *client,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode);
bool runtime_client_poll_frame(runtime_client *client, c64_frame *out_frame);

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event);
