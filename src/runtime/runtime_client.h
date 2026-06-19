#pragma once

#include "runtime_event.h"
#include "runtime.h"

#include "c64_frame.h"
#include "c64.h"
#include "keyboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_SYMBOL_NAME_MAX    = 64,
    RUNTIME_SYMBOL_SNAPSHOT_MAX = 256
};

typedef struct runtime_symbol_snapshot_entry {
    uint16_t address;
    char name[RUNTIME_SYMBOL_NAME_MAX];
} runtime_symbol_snapshot_entry;

typedef struct runtime_symbol_snapshot {
    size_t count;
    size_t total;
    runtime_symbol_snapshot_entry entries[RUNTIME_SYMBOL_SNAPSHOT_MAX];
} runtime_symbol_snapshot;

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
bool runtime_client_request_memory_view(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode);
bool runtime_client_request_frame(runtime_client *client);
bool runtime_client_keyboard_key(runtime_client *client, c64_key key, bool pressed);
bool runtime_client_restore(runtime_client *client);
bool runtime_client_set_joystick(runtime_client *client, unsigned port, uint8_t inputs);
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
bool runtime_client_set_execute_breakpoint(runtime_client *client, uint16_t address);
bool runtime_client_create_breakpoint(
    runtime_client *client,
    const runtime_breakpoint_definition *definition);
bool runtime_client_update_breakpoint(
    runtime_client *client,
    uint32_t id,
    const runtime_breakpoint_definition *definition);
bool runtime_client_duplicate_breakpoint(runtime_client *client, uint32_t id);
bool runtime_client_clear_breakpoint(runtime_client *client, uint32_t id);
bool runtime_client_clear_all_breakpoints(runtime_client *client);
bool runtime_client_set_breakpoint_enabled(runtime_client *client, uint32_t id, bool enabled);
bool runtime_client_request_breakpoints(runtime_client *client);
bool runtime_client_load_prg(runtime_client *client, const char *path);
bool runtime_client_mount_d64(runtime_client *client, uint8_t device, const char *path);
bool runtime_client_unmount_disk(runtime_client *client, uint8_t device);
bool runtime_client_request_disk_status(runtime_client *client, uint8_t device);
bool runtime_client_assemble_file(runtime_client *client, const char *path, uint16_t address);
bool runtime_client_assemble_file_full(
    runtime_client *client,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool reset_first);
bool runtime_client_poll_symbols(runtime_client *client, runtime_symbol_snapshot *out);
bool runtime_client_paste_text(runtime_client *client, const char *text, size_t length);
bool runtime_client_paste_text_buffer(runtime_client *client, const char *text, size_t length);
bool runtime_client_cycle_turbo_speed(runtime_client *client);
bool runtime_client_apply_machine_config(
    runtime_client *client,
    const c64_config *config,
    const runtime_config *runtime_options,
    const char *ini_path,
    const char *symbol_files,
    bool reset,
    bool save_ini);
bool runtime_client_poll_frame(runtime_client *client, c64_frame *out_frame);

bool runtime_client_step_out(runtime_client *client);
bool runtime_client_step_over(runtime_client *client);
bool runtime_client_run_to_cursor(runtime_client *client, uint16_t address);

bool runtime_client_load_bin(
    runtime_client *client,
    const char *path,
    uint16_t address,
    bool use_file_address,
    bool reset_first,
    bool is_basic);
bool runtime_client_save_bin(
    runtime_client *client,
    const char *path,
    uint16_t start_address,
    uint16_t end_address,
    bool write_file_address,
    bool is_basic);

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event);
