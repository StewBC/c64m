#pragma once

#include "runtime_event.h"
#include "runtime.h"
#include "runtime_frame_ring.h"
#include "runtime_vic_ring.h"

#include "c64_frame.h"
#include "c64.h"
#include "keyboard.h"
#include "paste_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_SYMBOL_NAME_MAX    = 64,
    RUNTIME_SYMBOL_SNAPSHOT_MAX = 4096
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
bool runtime_client_reset_ex(runtime_client *client, bool detach_cartridge);
bool runtime_client_reset_ex_with_resume(
    runtime_client *client,
    bool detach_cartridge,
    bool resume_running);
bool runtime_client_run(runtime_client *client);
bool runtime_client_pause(runtime_client *client);
bool runtime_client_step_cycle(runtime_client *client);
bool runtime_client_step_instruction(runtime_client *client);
bool runtime_client_run_cycles(runtime_client *client, size_t count);
bool runtime_client_run_instructions(runtime_client *client, size_t count);
bool runtime_client_step_frame(runtime_client *client);
/* Allocate a non-zero request_token for solicited control/UI RPC. */
uint64_t runtime_client_alloc_request_token(runtime_client *client);

/* Stamp source session id onto subsequent commands (0 = unknown / internal). */
void runtime_client_set_command_session(runtime_client *client, uint32_t session_id);
uint32_t runtime_client_get_command_session(const runtime_client *client);

/* Token 0: unsolicited / UI telemetry (must not complete control deferred). */
bool runtime_client_request_cpu_state(runtime_client *client);
/* Solicited CPU read; runtime echoes request_token on CPU_STATE_RESPONSE. */
bool runtime_client_request_cpu_state_token(runtime_client *client, uint64_t request_token);
bool runtime_client_request_machine_state(runtime_client *client);
/* UI/legacy: token 0, inline snapshot up to RUNTIME_MEMORY_SNAPSHOT_MAX. */
bool runtime_client_request_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode);
/* Solicited bulk/control get-memory (token non-zero). length 1..65536. */
bool runtime_client_request_memory_token(
    runtime_client *client,
    uint16_t address,
    uint32_t length,
    runtime_memory_mode mode,
    uint64_t request_token);
/* Claim pool payload for token; caller owns *out_bytes (malloc). */
bool runtime_client_claim_memory_rpc(
    runtime_client *client,
    uint64_t request_token,
    uint8_t **out_bytes,
    uint32_t *out_length,
    uint16_t *out_address,
    runtime_memory_mode *out_mode);
bool runtime_client_claim_history_rpc(
    runtime_client *client,
    uint64_t request_token,
    uint8_t **out_bytes,
    uint32_t *out_length,
    runtime_history_rpc_meta *out_meta);
bool runtime_client_cancel_rpc(
    runtime_client *client,
    uint64_t request_token);
bool runtime_client_request_memory_view(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode);
bool runtime_client_request_debug_memory(runtime_client *client, bool include_write_history);
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
bool runtime_client_write_memory(
    runtime_client *client,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode,
    const uint8_t *bytes);
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
bool runtime_client_rearm_oneshot_breakpoints(runtime_client *client);
bool runtime_client_request_breakpoints(runtime_client *client);
bool runtime_client_load_prg(runtime_client *client, const char *path);
bool runtime_client_load_crt(runtime_client *client, const char *path);
bool runtime_client_save_state(runtime_client *client, const char *path);
bool runtime_client_load_state(runtime_client *client, const char *path);
bool runtime_client_mount_d64(runtime_client *client, uint8_t device, const char *path);
bool runtime_client_mount_d64_ex(
    runtime_client *client,
    uint8_t device,
    const char *path,
    bool writable);
bool runtime_client_set_disk_writable(runtime_client *client, uint8_t device, bool writable);
bool runtime_client_unmount_disk(runtime_client *client, uint8_t device);
/* Soft power-on for device 8 or 9 (sticky; no-op if already powered). */
bool runtime_client_power_on_drive(runtime_client *client, uint8_t device);
/* Soft power-off: ejects media if present, then powers the unit off. */
bool runtime_client_power_off_drive(runtime_client *client, uint8_t device);
bool runtime_client_request_disk_status(runtime_client *client, uint8_t device);
bool runtime_client_assemble_file(runtime_client *client, const char *path, uint16_t address);
bool runtime_client_assemble_file_full(
    runtime_client *client,
    const char *path,
    uint16_t address,
    uint16_t run_address,
    bool auto_run,
    bool basic_run,
    bool reset_first,
    bool auto_adjust_segments);
bool runtime_client_poll_symbols(runtime_client *client, runtime_symbol_snapshot *out);
bool runtime_client_paste_text(runtime_client *client, const char *text, size_t length);
bool runtime_client_paste_text_buffer(runtime_client *client, const char *text, size_t length);
bool runtime_client_paste_events(runtime_client *client, const paste_event_t *events, size_t count);
bool runtime_client_cycle_turbo_speed(runtime_client *client);
/* multiplier is a turbo mode ID: 1=normal, 2=max, 3=warp (RUNTIME_TURBO_MODE_*). */
bool runtime_client_set_turbo_multiplier(runtime_client *client, uint32_t multiplier);
/* rom_paths, when non-NULL, carries the effective ROM file paths (any member may
   be NULL/empty for "unset"); pass reload_roms=true to have the runtime re-read
   them as part of this apply (requires reset to take visible effect). */
typedef struct runtime_client_rom_paths {
    const char *system_rom_path;
    const char *basic_rom_path;
    const char *char_rom_path;
    const char *kernal_rom_path;
    const char *rom1541_path;
} runtime_client_rom_paths;

bool runtime_client_apply_machine_config(
    runtime_client *client,
    const c64_config *config,
    const runtime_config *runtime_options,
    const char *ini_path,
    const char *symbol_files,
    bool reset,
    bool save_ini,
    bool resume_running,
    const runtime_client_rom_paths *rom_paths,
    bool reload_roms);
bool runtime_client_poll_frame(runtime_client *client, c64_frame *out_frame);
bool runtime_client_poll_debug_memory(runtime_client *client, runtime_debug_memory_snapshot *out_snapshot);
bool runtime_client_poll_breakpoints(
    runtime_client *client,
    runtime_breakpoint_snapshot *out_snapshot);

bool runtime_client_step_out(runtime_client *client);
bool runtime_client_step_over(runtime_client *client);
bool runtime_client_run_to_cursor(runtime_client *client, uint16_t address);
/* Run until VIC raster_line matches (and optional cycle_in_line). */
bool runtime_client_run_to_raster(
    runtime_client *client,
    uint16_t raster_line,
    bool has_cycle,
    uint16_t cycle_in_line);
bool runtime_client_history_info(
    runtime_client *client,
    uint64_t request_token);
bool runtime_client_history_record(
    runtime_client *client,
    bool enabled,
    uint64_t request_token);
bool runtime_client_history_clear(
    runtime_client *client,
    uint64_t request_token);
/* session_id 0 = default session (compat). */
bool runtime_client_history_find(
    runtime_client *client,
    uint32_t session_id,
    const runtime_history_query *query,
    runtime_history_from_kind from_kind,
    uint64_t from_id,
    uint16_t limit,
    uint64_t request_token);
bool runtime_client_history_next(
    runtime_client *client,
    uint32_t session_id,
    uint64_t cursor,
    uint16_t limit,
    uint64_t request_token);
bool runtime_client_history_read(
    runtime_client *client,
    uint32_t session_id,
    uint64_t epoch,
    uint64_t id,
    uint16_t before,
    uint16_t after,
    uint64_t request_token);
bool runtime_client_history_close(
    runtime_client *client,
    uint32_t session_id,
    uint64_t cursor,
    uint64_t request_token);
/* Worker-allocated session; reply via RUNTIME_EVENT_SESSION_RESPONSE.
   endpoint_epoch is stored for control binding (0 if unused). */
bool runtime_client_session_open(
    runtime_client *client,
    runtime_session_kind kind,
    uint64_t endpoint_epoch,
    uint64_t request_token);
bool runtime_client_session_close(
    runtime_client *client,
    uint32_t session_id,
    uint64_t request_token);
bool runtime_client_inspector_set_enabled(
    runtime_client *client,
    bool enabled,
    uint64_t request_token);
bool runtime_client_inspector_enter(runtime_client *client, uint64_t request_token);
bool runtime_client_inspector_leave(runtime_client *client, uint64_t request_token);
bool runtime_client_inspector_land(
    runtime_client *client, uint64_t cycle, uint64_t request_token);
bool runtime_client_inspector_land_to_cycle(
    runtime_client *client, uint64_t cycle, uint64_t request_token);
bool runtime_client_inspector_frame_step(
    runtime_client *client, int direction, uint64_t request_token);
bool runtime_client_request_call_stack(runtime_client *client);

bool runtime_client_load_bin(
    runtime_client *client,
    const char *path,
    uint16_t address,
    bool use_file_address,
    bool reset_first,
    bool is_basic,
    bool is_basic_text);
bool runtime_client_save_bin(
    runtime_client *client,
    const char *path,
    uint16_t start_address,
    uint16_t end_address,
    bool write_file_address,
    bool is_basic,
    bool is_basic_text);

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event);

/* Frame ring (rolling framebuffer black box). These read the runtime's ring
   directly; it carries its own mutex, so they are safe to call from the main
   thread while the runtime thread keeps pushing frames. */
void runtime_client_get_frame_ring_info(
    runtime_client *client,
    runtime_frame_ring_info *out_info);

/* Nearest-≤ lookup by frame number or machine_cycle. For exact machine_cycle
   match, see runtime_client_copy_frame_by_cycle_exact. */
bool runtime_client_copy_frame_at(
    runtime_client *client,
    uint64_t target,
    bool by_cycle,
    c64_frame *out_frame);

/* Exact machine_cycle match only; false on any miss (no nearest-≤ fallback). */
bool runtime_client_copy_frame_by_cycle_exact(
    runtime_client *client,
    uint64_t machine_cycle,
    c64_frame *out_frame);

void runtime_client_set_frame_ring_recording(runtime_client *client, bool recording);

void runtime_client_clear_frame_ring(runtime_client *client);

/* Per-line VIC ring. Same threading story as the frame ring: the ring owns a
   mutex, so these are safe from the main thread while the runtime records. */
void runtime_client_get_vic_ring_info(
    runtime_client *client,
    runtime_vic_ring_info *out_info);

uint32_t runtime_client_copy_vic_lines(
    runtime_client *client,
    bool has_frame,
    uint64_t frame_number,
    uint16_t raster_first,
    uint16_t raster_last,
    uint32_t limit,
    vicii_line_record *out);

void runtime_client_set_vic_ring_recording(runtime_client *client, bool recording);

void runtime_client_clear_vic_ring(runtime_client *client);
