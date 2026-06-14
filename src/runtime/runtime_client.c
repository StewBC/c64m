#include "runtime_client.h"

#include "runtime_command.h"
#include "runtime_internal.h"
#include "message_queue.h"

static bool runtime_client_send_command(
    runtime_client *client,
    runtime_command_type type) {
    if (!client) {
        return false;
    }

    runtime_command command = {
        .type = type,
    };

    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_ping(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_PING);
}

bool runtime_client_quit(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_QUIT);
}

bool runtime_client_reset(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_RESET);
}

bool runtime_client_run(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_RUN);
}

bool runtime_client_pause(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_PAUSE);
}

bool runtime_client_step_cycle(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_CYCLE);
}

bool runtime_client_step_instruction(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_INSTRUCTION);
}

bool runtime_client_run_cycles(runtime_client *client, size_t count) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RUN_CYCLES,
    };

    if (!client) {
        return false;
    }

    command.data.run_cycles.count = count;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_run_instructions(runtime_client *client, size_t count) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RUN_INSTRUCTIONS,
    };

    if (!client) {
        return false;
    }

    command.data.run_instructions.count = count;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_request_cpu_state(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_CPU_STATE);
}

bool runtime_client_request_machine_state(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_MACHINE_STATE);
}

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event) {
    if (!client || !out_event) {
        return false;
    }

    return message_queue_try_pop(client->event_queue, out_event);
}
