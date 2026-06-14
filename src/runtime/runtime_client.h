#pragma once

#include "runtime_event.h"

#include <stdbool.h>

typedef struct runtime_client runtime_client;

bool runtime_client_ping(runtime_client *client);
bool runtime_client_quit(runtime_client *client);
bool runtime_client_reset(runtime_client *client);
bool runtime_client_step_instruction(runtime_client *client);
bool runtime_client_request_cpu_state(runtime_client *client);

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event);
