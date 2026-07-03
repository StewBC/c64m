#include "runtime_client.h"

#include "runtime_command.h"
#include "runtime_internal.h"
#include "message_queue.h"
#include "mutex.h"

#include <stdio.h>
#include <string.h>

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

bool runtime_client_reset_ex(runtime_client *client, bool detach_cartridge) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RESET,
    };

    if (!client) {
        return false;
    }

    command.data.reset.detach_cartridge = detach_cartridge ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_reset(runtime_client *client) {
    return runtime_client_reset_ex(client, false);
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

bool runtime_client_request_memory_view(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_MEMORY_VIEW,
    };

    if (!client) {
        return false;
    }

    command.data.request_memory.address = address;
    command.data.request_memory.length = length;
    command.data.request_memory.mode = (uint8_t)mode;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_request_debug_memory(runtime_client *client, bool include_write_history) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_DEBUG_MEMORY,
    };

    if (!client) {
        return false;
    }

    command.data.request_debug_memory.include_write_history = include_write_history ? 1u : 0u;
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

bool runtime_client_set_joystick(runtime_client *client, unsigned port, uint8_t inputs) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SET_JOYSTICK,
    };

    if (!client || port < 1u || port > 2u) {
        return false;
    }

    command.data.set_joystick.port = (uint8_t)port;
    command.data.set_joystick.inputs = (uint8_t)(inputs & 0x1fu);
    return message_queue_push(client->command_queue, &command);
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

bool runtime_client_create_breakpoint(
    runtime_client *client,
    const runtime_breakpoint_definition *definition) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_CREATE_BREAKPOINT,
    };

    if (!client || !definition) {
        return false;
    }

    command.data.create_breakpoint.definition = *definition;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_update_breakpoint(
    runtime_client *client,
    uint32_t id,
    const runtime_breakpoint_definition *definition) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_UPDATE_BREAKPOINT,
    };

    if (!client || !definition) {
        return false;
    }

    command.data.update_breakpoint.id = id;
    command.data.update_breakpoint.definition = *definition;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_duplicate_breakpoint(runtime_client *client, uint32_t id) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_DUPLICATE_BREAKPOINT,
    };

    if (!client) {
        return false;
    }

    command.data.duplicate_breakpoint.id = id;
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

bool runtime_client_rearm_oneshot_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REARM_ONESHOT_BREAKPOINTS);
}

bool runtime_client_request_breakpoints(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_BREAKPOINTS);
}

bool runtime_client_load_prg(runtime_client *client, const char *path) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_LOAD_PRG,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.load_prg.path, sizeof(command.data.load_prg.path), "%s", path);
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_load_crt(runtime_client *client, const char *path) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_LOAD_CRT,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.load_crt.path, sizeof(command.data.load_crt.path), "%s", path);
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_mount_d64(runtime_client *client, uint8_t device, const char *path) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_MOUNT_D64,
    };

    if (!client || !path || path[0] == '\0' || !c64_drive_device_supported(device)) {
        return false;
    }

    command.data.mount_d64.device = device;
    snprintf(command.data.mount_d64.path, sizeof(command.data.mount_d64.path), "%s", path);
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_unmount_disk(runtime_client *client, uint8_t device) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_UNMOUNT_DISK,
    };

    if (!client || !c64_drive_device_supported(device)) {
        return false;
    }

    command.data.disk_device.device = device;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_request_disk_status(runtime_client *client, uint8_t device) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_REQUEST_DISK_STATUS,
    };

    if (!client || !c64_drive_device_supported(device)) {
        return false;
    }

    command.data.disk_device.device = device;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_assemble_file(runtime_client *client, const char *path, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_ASSEMBLE_FILE,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.assemble_file.path, sizeof(command.data.assemble_file.path), "%s", path);
    command.data.assemble_file.address = address;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_assemble_file_full(
    runtime_client *client,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool reset_first) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_ASSEMBLE_FILE,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.assemble_file.path, sizeof(command.data.assemble_file.path), "%s", path);
    command.data.assemble_file.address = address;
    command.data.assemble_file.run_address = run_address;
    command.data.assemble_file.auto_run = auto_run ? 1u : 0u;
    command.data.assemble_file.reset_first = reset_first ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_poll_symbols(runtime_client *client, runtime_symbol_snapshot *out) {
    runtime_symbol_slot *slot;

    if (!client || !out || !client->symbol_slot) {
        return false;
    }

    slot = client->symbol_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_symbols) {
        mutex_unlock(slot->mutex);
        return false;
    }

    *out = slot->snapshot;
    slot->has_symbols = false;
    mutex_unlock(slot->mutex);
    return true;
}

bool runtime_client_cycle_turbo_speed(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_CYCLE_TURBO_SPEED);
}

bool runtime_client_apply_machine_config(
    runtime_client *client,
    const c64_config *config,
    const runtime_config *runtime_options,
    const char *ini_path,
    const char *symbol_files,
    bool reset,
    bool save_ini) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_APPLY_MACHINE_CONFIG,
    };

    if (!client || !config) {
        return false;
    }

    command.data.apply_machine_config.config = *config;
    if (runtime_options != NULL) {
        memcpy(
            command.data.apply_machine_config.turbo_speeds,
            runtime_options->turbo_speeds,
            sizeof(command.data.apply_machine_config.turbo_speeds));
        command.data.apply_machine_config.turbo_speed_count = runtime_options->turbo_speed_count;
        command.data.apply_machine_config.active_turbo_multiplier =
            runtime_options->active_turbo_multiplier;
    }
    if (ini_path != NULL) {
        snprintf(command.data.apply_machine_config.ini_path, sizeof(command.data.apply_machine_config.ini_path), "%s", ini_path);
    }
    if (symbol_files != NULL) {
        snprintf(
            command.data.apply_machine_config.symbol_files,
            sizeof(command.data.apply_machine_config.symbol_files),
            "%s",
            symbol_files);
    }
    command.data.apply_machine_config.reset = reset ? 1u : 0u;
    command.data.apply_machine_config.save_ini = save_ini ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
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

bool runtime_client_poll_debug_memory(runtime_client *client, runtime_debug_memory_snapshot *out_snapshot) {
    runtime_debug_memory_slot *slot;

    if (!client || !out_snapshot || !client->debug_memory_slot) {
        return false;
    }

    slot = client->debug_memory_slot;
    mutex_lock(slot->mutex);
    if (!slot->has_snapshot) {
        mutex_unlock(slot->mutex);
        return false;
    }

    *out_snapshot = slot->snapshot;
    slot->has_snapshot = false;
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

bool runtime_client_step_out(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_OUT);
}

bool runtime_client_step_over(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_STEP_OVER);
}

bool runtime_client_run_to_cursor(runtime_client *client, uint16_t address) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_RUN_TO_CURSOR,
    };

    if (!client) {
        return false;
    }

    command.data.run_to_cursor.address = address;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_paste_text(runtime_client *client, const char *text, size_t length) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_PASTE_TEXT,
    };

    if (!client || !text || length == 0) {
        return false;
    }

    if (length > RUNTIME_PASTE_TEXT_MAX) {
        length = RUNTIME_PASTE_TEXT_MAX;
    }

    memcpy(command.data.paste_text.text, text, length);
    command.data.paste_text.length = length;
    command.data.paste_text.use_buffer = 0;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_load_bin(
    runtime_client *client,
    const char *path,
    uint16_t address,
    bool use_file_address,
    bool reset_first,
    bool is_basic) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_LOAD_BIN,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.load_bin.path, sizeof(command.data.load_bin.path), "%s", path);
    command.data.load_bin.address = address;
    command.data.load_bin.use_file_address = use_file_address ? 1u : 0u;
    command.data.load_bin.reset_first = reset_first ? 1u : 0u;
    command.data.load_bin.is_basic = is_basic ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_save_bin(
    runtime_client *client,
    const char *path,
    uint16_t start_address,
    uint16_t end_address,
    bool write_file_address,
    bool is_basic) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_SAVE_BIN,
    };

    if (!client || !path || path[0] == '\0') {
        return false;
    }

    snprintf(command.data.save_bin.path, sizeof(command.data.save_bin.path), "%s", path);
    command.data.save_bin.start_address = start_address;
    command.data.save_bin.end_address = end_address;
    command.data.save_bin.write_file_address = write_file_address ? 1u : 0u;
    command.data.save_bin.is_basic = is_basic ? 1u : 0u;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_paste_text_buffer(runtime_client *client, const char *text, size_t length) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_PASTE_TEXT,
    };

    if (!client || !text || length == 0) {
        return false;
    }

    if (length > RUNTIME_PASTE_TEXT_MAX) {
        length = RUNTIME_PASTE_TEXT_MAX;
    }

    memcpy(command.data.paste_text.text, text, length);
    command.data.paste_text.length = length;
    command.data.paste_text.use_buffer = 1;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_paste_events(runtime_client *client, const paste_event_t *events, size_t count) {
    runtime_command command = {
        .type = RUNTIME_COMMAND_PASTE_EVENTS,
    };

    if (!client || !events || count == 0) {
        return false;
    }

    if (count > PASTE_EVENTS_MAX) {
        count = PASTE_EVENTS_MAX;
    }

    memcpy(command.data.paste_events.events, events, count * sizeof(paste_event_t));
    command.data.paste_events.count = count;
    return message_queue_push(client->command_queue, &command);
}

bool runtime_client_request_call_stack(runtime_client *client) {
    return runtime_client_send_command(client, RUNTIME_COMMAND_REQUEST_CALL_STACK);
}
