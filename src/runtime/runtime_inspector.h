#pragma once

/* Inspector recording (I0) + checkpoint ring / sealed replay to scratch (I1)
 * + Inspect mode into the live c64_t (I2).
 * Identifiers are runtime_inspector_*; do not introduce tm_* names. */

#include "c64.h"
#include "runtime_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct runtime runtime;

enum {
    RUNTIME_INSPECTOR_DEFAULT_MEMORY_MB = 128,
    RUNTIME_INSPECTOR_MIN_MEMORY_MB = 16,
    RUNTIME_INSPECTOR_MAX_MEMORY_MB = 4096
};

/* Landed C64 (machine cycles / regs). Not an HST1 tape head. */
typedef struct runtime_inspector_focus {
    bool valid;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t p;
    uint16_t pc;
    uint64_t cycle;
} runtime_inspector_focus;

typedef struct runtime_inspector_window {
    bool valid;
    uint64_t oldest_cycle;
    uint64_t newest_cycle;
    uint64_t checkpoint_count;
    uint64_t checkpoints_dropped;
    uint64_t media_truncations;
    runtime_history_media_change_kind start_kind;
    uint32_t start_arg1;
} runtime_inspector_window;

typedef enum runtime_inspector_mode {
    RUNTIME_INSPECTOR_MODE_LIVE = 0,
    RUNTIME_INSPECTOR_MODE_INSPECT
} runtime_inspector_mode;

typedef enum runtime_inspector_enter_status {
    RUNTIME_INSPECTOR_ENTER_OK = 0,
    RUNTIME_INSPECTOR_ENTER_UNAVAILABLE,
    RUNTIME_INSPECTOR_ENTER_EMPTY,
    RUNTIME_INSPECTOR_ENTER_FAILED
} runtime_inspector_enter_status;

/* I0 master switch. Off->on arms film (if budget > 0) and starts the
 * checkpoint recorder. Never arms HST1. On->off stops Inspector recording
 * only; standalone HST1 / film are left alone. */
void runtime_inspector_set_enabled(runtime *rt, bool enabled);
bool runtime_inspector_enabled(const runtime *rt);
uint32_t runtime_inspector_memory_mb(const runtime *rt);

void runtime_inspector_recorder_set_enabled(runtime *rt, bool enabled);
bool runtime_inspector_recorder_is_recording(const runtime *rt);
bool runtime_inspector_checkpoint_take(runtime *rt);
/* Load nearest checkpoint <= cycle into dst, then sealed-replay inputs to
 * cycle. I1 tests pass a scratch machine; I2 land uses load_nearest into the
 * live c64_t instead of this path. */
bool runtime_inspector_materialize(runtime *rt, uint64_t cycle, c64_t *dst);
/* Load nearest checkpoint <= cycle into dst without re-execution. */
bool runtime_inspector_load_nearest_checkpoint(
    runtime *rt, uint64_t cycle, c64_t *dst);
void runtime_inspector_window_info(const runtime *rt, runtime_inspector_window *out);
void runtime_inspector_after_step(runtime *rt);
void runtime_inspector_on_media_event(runtime *rt, uint64_t cycle, int device);
void runtime_inspector_on_history_invalidate(runtime *rt);
void runtime_inspector_recorder_destroy(runtime *rt);

void runtime_inspector_get_focus(const runtime *rt, runtime_inspector_focus *out);
void runtime_inspector_timeline_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *live, uint64_t *count);
uint64_t runtime_inspector_live_cycle(const runtime *rt);
bool runtime_inspector_at_live(const runtime *rt);
void runtime_inspector_sync_focus(runtime *rt);
void runtime_inspector_apply_logged_inputs(
    runtime *rt, c64_t *dst, uint64_t from_inclusive, uint64_t to_inclusive);
void runtime_inspector_apply_live_seal(runtime *rt);
bool runtime_inspector_restore_live(runtime *rt);
bool runtime_inspector_land(runtime *rt, uint64_t cycle);
bool runtime_inspector_reexecute_to(runtime *rt, uint64_t target_cycle);
bool runtime_inspector_frame_step(runtime *rt, int direction);
runtime_inspector_mode runtime_inspector_current_mode(const runtime *rt);
bool runtime_inspector_in_inspect(const runtime *rt);
runtime_inspector_enter_status runtime_inspector_can_enter(const runtime *rt);
runtime_inspector_enter_status runtime_inspector_enter(runtime *rt);
void runtime_inspector_leave(runtime *rt);
bool runtime_inspector_snapshot_machine(runtime *rt, uint8_t **blob, size_t *size);
bool runtime_inspector_restore_blob(runtime *rt, const uint8_t *blob, size_t size);
void runtime_inspector_destroy(runtime *rt);
const char *runtime_inspector_window_start_name(runtime_history_media_change_kind kind);

uint64_t runtime_inspector_checkpoint_count(const runtime *rt);
uint64_t runtime_inspector_checkpoints_dropped(const runtime *rt);
uint64_t runtime_inspector_media_truncations(const runtime *rt);
uint32_t runtime_inspector_cadence_cycles(const runtime *rt);
