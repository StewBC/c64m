#pragma once

/* Inspector recording (I0) + checkpoint ring / sealed replay to scratch (I1).
 * Identifiers are runtime_inspector_*; do not introduce tm_* names. */

#include "c64.h"
#include "runtime_history.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct runtime runtime;

enum {
    RUNTIME_INSPECTOR_DEFAULT_MEMORY_MB = 128,
    RUNTIME_INSPECTOR_MIN_MEMORY_MB = 16,
    RUNTIME_INSPECTOR_MAX_MEMORY_MB = 4096
};

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
 * cycle. dst is a scratch machine in I1 (must not be the live c64_t). */
bool runtime_inspector_materialize(runtime *rt, uint64_t cycle, c64_t *dst);
/* Load nearest checkpoint <= cycle into dst without re-execution. */
bool runtime_inspector_load_nearest_checkpoint(
    runtime *rt, uint64_t cycle, c64_t *dst);
void runtime_inspector_window_info(const runtime *rt, runtime_inspector_window *out);
void runtime_inspector_after_step(runtime *rt);
void runtime_inspector_on_media_event(runtime *rt, uint64_t cycle, int device);
void runtime_inspector_on_history_invalidate(runtime *rt);
void runtime_inspector_recorder_destroy(runtime *rt);

uint64_t runtime_inspector_checkpoint_count(const runtime *rt);
uint64_t runtime_inspector_checkpoints_dropped(const runtime *rt);
uint64_t runtime_inspector_media_truncations(const runtime *rt);
uint32_t runtime_inspector_cadence_cycles(const runtime *rt);
