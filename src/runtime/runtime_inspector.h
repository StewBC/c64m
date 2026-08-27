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
typedef struct mutex mutex;

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

/* Compact shared Record index for scrub join: (cycle, film_cycle) only.
 * Mutex-safe like the frame ring; mirrors retained CP slots across enter
 * disarm; cleared only with the tape. */
typedef struct runtime_inspector_cp_index_entry {
    uint64_t cycle;
    uint64_t film_cycle; /* 0 = no preferred still at birth */
} runtime_inspector_cp_index_entry;

typedef struct runtime_inspector_cp_index {
    mutex *mutex;
    runtime_inspector_cp_index_entry *entries;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
} runtime_inspector_cp_index;

/* Same slot formula as the recorder budget so the shared index capacity matches. */
uint32_t runtime_inspector_slot_count_for_budget(uint32_t memory_mb);

bool runtime_inspector_cp_index_init(
    runtime_inspector_cp_index *index, uint32_t capacity);
void runtime_inspector_cp_index_destroy(runtime_inspector_cp_index *index);
void runtime_inspector_cp_index_clear(runtime_inspector_cp_index *index);
/* Nearest retained cell with cycle <= preview. false if none or film_cycle 0. */
bool runtime_inspector_cp_index_lookup_film(
    runtime_inspector_cp_index *index,
    uint64_t preview_cycle,
    uint64_t *out_cell_cycle,
    uint64_t *out_film_cycle);
/* Record lattice neighbor for Inspector [+]/[-].
   direction < 0: greatest retained cycle < from_cycle.
   direction > 0: least retained cycle > from_cycle, else live_cycle when
   live_cycle > from_cycle (LIVE/NOW endpoint). */
bool runtime_inspector_cp_index_adjacent(
    runtime_inspector_cp_index *index,
    uint64_t from_cycle,
    int direction,
    uint64_t live_cycle,
    uint64_t *out_cycle);
/* Snapshot line helpers: navigable slots are retained CPs plus a distinct
   LIVE slot when live_cycle > newest CP (same idea as a2m NOW).
   out_ordinal is 0-based; out_exact is true when cycle is exactly a CP or LIVE. */
bool runtime_inspector_cp_index_snapshot_slot(
    runtime_inspector_cp_index *index,
    uint64_t cycle,
    uint64_t live_cycle,
    uint64_t *out_ordinal,
    uint64_t *out_count,
    bool *out_exact);

/* I0 master switch. Off->on arms film (if budget > 0) and starts the
 * checkpoint recorder. Never arms HST1. On->off stops Inspector recording
 * only; standalone HST1 / film are left alone.
 * While turbo is max/warp and inspector_off_on_max is on, enable does not
 * arm: it is remembered for leave-max. Disable clears that memory. */
void runtime_inspector_set_enabled(runtime *rt, bool enabled);
bool runtime_inspector_enabled(const runtime *rt);
uint32_t runtime_inspector_memory_mb(const runtime *rt);

void runtime_inspector_recorder_set_enabled(runtime *rt, bool enabled);
bool runtime_inspector_recorder_is_recording(const runtime *rt);
bool runtime_inspector_checkpoint_take(runtime *rt);
/* Frame-synced birth: preferred still key (0 = none / turbo-display). */
bool runtime_inspector_checkpoint_take_for_frame(runtime *rt, uint64_t film_cycle);
/* Load nearest checkpoint <= cycle into dst, then sealed-replay inputs to
 * cycle. I1 tests pass a scratch machine; I2 land uses load_nearest into the
 * live c64_t instead of this path. */
bool runtime_inspector_materialize(runtime *rt, uint64_t cycle, c64_t *dst);
/* Load nearest checkpoint <= cycle into dst without re-execution. */
bool runtime_inspector_load_nearest_checkpoint(
    runtime *rt, uint64_t cycle, c64_t *dst);
void runtime_inspector_window_info(const runtime *rt, runtime_inspector_window *out);
/* Idle for CP birth: lattice advances on frame publish, not cadence. */
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
/* Exact land: nearest checkpoint <= target then sealed reexecute_to(target).
   One helper / one publish at command end — do not split into two UI RPCs. */
bool runtime_inspector_land_to_cycle(runtime *rt, uint64_t target_cycle);
bool runtime_inspector_reexecute_to(runtime *rt, uint64_t target_cycle);
/* Walk Record lattice. direction < 0: greatest CP with cycle < focus;
   direction > 0: least CP with cycle > focus, else LIVE/NOW.
   Loads the target checkpoint into the live c64_t (quantized). Does not
   sealed-hunt frame_complete. */
bool runtime_inspector_checkpoint_step(runtime *rt, int direction);
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
/* Approx frame length (cycles_per_frame); not the Record clock. */
uint32_t runtime_inspector_cadence_cycles(const runtime *rt);
