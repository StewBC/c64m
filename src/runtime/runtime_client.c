#include "runtime_client.h"

#include "runtime_command.h"
#include "runtime_internal.h"
#include "message_queue.h"
#include "mutex.h"

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

bool runtime_client_request_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_MEMORY,
    };

    if (!client) {
        return false;
    }

    command.data.request_memory.address = address;
    command.data.request_memory.length = length;
    command.data.request_memory.mode = (uint8_t)mode;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_request_frame(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_FRAME);
}

bool runtime_client_keyboard_key(runtime_client *client, c64_key key, bool pressed) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_KEYBOARD_KEY,
    };

    if (!client) {
        return false;
    }

    command.data.keyboard_key.key = key;
    command.data.keyboard_key.pressed = pressed ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_restore(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_RESTORE);
}

static bool runtime_client_set_cpu_register(
    runtime_client *client,
    runtime_cpu_register reg,
    uint16_t value) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_CPU_REGISTER,
    };

    if (!client) {
        return false;
    }

    command.data.set_cpu_register.reg = reg;
    command.data.set_cpu_register.value = value;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_set_pc(runtime_client *client, uint16_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_PC, value);
}

bool runtime_client_set_sp(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_SP, value);
}

bool runtime_client_set_a(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_A, value);
}

bool runtime_client_set_x(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_X, value);
}

bool runtime_client_set_y(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_Y, value);
}

bool runtime_client_set_status(runtime_client *client, uint8_t value) {
    return runtime_client_set_cpu_register(client, RUNTIME_CPU_REGISTER_STATUS, value);
}

bool runtime_client_write_memory_byte(
    runtime_client *client,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_WRITE_MEMORY_BYTE,
    };

    if (!client) {
        return false;
    }

    command.data.write_memory_byte.address = address;
    command.data.write_memory_byte.value = value;
    command.data.write_memory_byte.mode = (uint8_t)mode;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_set_execute_breakpoint(runtime_client *client, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_EXECUTE_BREAKPOINT,
    };

    if (!client) {
        return false;
    }

    command.data.set_execute_breakpoint.address = address;
    command.data.set_execute_breakpoint.enabled = 1u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_clear_breakpoint(runtime_client *client, uint32_t id) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_CLEAR_BREAKPOINT,
    };

    if (!client) {
        return false;
    }

    command.data.clear_breakpoint.id = id;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_clear_all_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_CLEAR_ALL_BREAKPOINTS);
}

bool runtime_client_set_breakpoint_enabled(runtime_client *client, uint32_t id, bool enabled) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_BREAKPOINT_ENABLED,
    };

    if (!client) {
        return false;
    }

    command.data.set_breakpoint_enabled.id = id;
    command.data.set_breakpoint_enabled.enabled = enabled ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_request_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_BREAKPOINTS);
}

bool runtime_client_poll_frame(runtime_client *client, c64_frame *out_frame) {
    runtime_frame_slot *slot;

    if (!client || !out_frame || !client->frame_slot) {
        return false;
    }

    slot = client->frame_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_frame) {
        mutex_unlock(slot->mutex);
        return false;
    }

    *out_frame = slot->frame;
    slot->has_frame = false;
    slot->consumed_frames++;
    mutex_unlock(slot->mutex);
    return true;
}

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event) {
    if (!client || !out_event) {
        return false;
    }

    return message_queue_try_pop(client->event_queue, out_event);
}
