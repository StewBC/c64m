#include "runtime_inspector.h"

#include "c64_snapshot.h"
#include "runtime_frame_ring.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RUNTIME_INSPECTOR_INPUT_CAP_DEFAULT = 65536u };

typedef struct runtime_inspector_checkpoint {
    uint64_t cycle;
    size_t size;
    uint8_t *blob;
} runtime_inspector_checkpoint;

typedef struct runtime_inspector_input {
    uint64_t cycle;
    uint8_t kind;
    uint8_t a;
    uint8_t b;
    uint8_t c;
} runtime_inspector_input;

struct runtime_inspector_recorder {
    bool recording;
    uint32_t cadence_cycles;
    size_t memory_budget;
    size_t used;
    uint32_t slot_count;
    uint32_t count;
    uint32_t head;
    runtime_inspector_checkpoint *slots;
    uint64_t last_checkpoint_cycle;
    uint64_t dropped;
    uint64_t truncations;
    runtime_history_media_change_kind start_kind;
    uint32_t start_arg1;

    runtime_inspector_input *inputs;
    uint32_t input_cap;
    uint32_t input_count;
    uint32_t input_head;
};

static uint32_t inspector_cp_index(
    const struct runtime_inspector_recorder *rec, uint32_t logical)
{
    return (rec->head + rec->slot_count - rec->count + logical) % rec->slot_count;
}

static runtime_inspector_checkpoint *inspector_cp_at(
    struct runtime_inspector_recorder *rec, uint32_t logical)
{
    return &rec->slots[inspector_cp_index(rec, logical)];
}

static const runtime_inspector_checkpoint *inspector_cp_at_const(
    const struct runtime_inspector_recorder *rec, uint32_t logical)
{
    return &rec->slots[inspector_cp_index(rec, logical)];
}

static void inspector_drop_oldest(struct runtime_inspector_recorder *rec)
{
    runtime_inspector_checkpoint *cp;

    if (rec == NULL || rec->count == 0u) {
        return;
    }
    cp = inspector_cp_at(rec, 0u);
    rec->used -= cp->size;
    free(cp->blob);
    memset(cp, 0, sizeof(*cp));
    rec->count--;
    rec->dropped++;
}

static void inspector_input_drop_older_than(
    struct runtime_inspector_recorder *rec, uint64_t cycle)
{
    while (rec->input_count > 0u) {
        uint32_t tail =
            (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
        if (rec->inputs[tail].cycle >= cycle) {
            break;
        }
        rec->input_count--;
    }
}

static void inspector_recorder_free(struct runtime_inspector_recorder *rec)
{
    uint32_t i;

    if (rec == NULL) {
        return;
    }
    if (rec->slots != NULL) {
        for (i = 0u; i < rec->slot_count; ++i) {
            free(rec->slots[i].blob);
        }
        free(rec->slots);
    }
    free(rec->inputs);
    free(rec);
}

void runtime_inspector_recorder_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    inspector_recorder_free(rt->inspector_recorder);
    rt->inspector_recorder = NULL;
}

static struct runtime_inspector_recorder *inspector_recorder_ensure(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    uint64_t budget;
    uint32_t slots;

    if (rt == NULL) {
        return NULL;
    }
    if (rt->inspector_recorder != NULL) {
        return rt->inspector_recorder;
    }
    if (rt->inspector_memory_mb == 0u) {
        return NULL;
    }
    rec = (struct runtime_inspector_recorder *)calloc(1u, sizeof(*rec));
    if (rec == NULL) {
        return NULL;
    }
    budget = (uint64_t)rt->inspector_memory_mb * 1024ull * 1024ull;
    rec->memory_budget = (size_t)budget;
    rec->cadence_cycles = c64_config_cycles_per_frame(&rt->machine.config);
    if (rec->cadence_cycles == 0u) {
        rec->cadence_cycles = 19656u;
    }
    slots = (uint32_t)(budget / (64ull * 1024ull));
    if (slots < 2u) {
        slots = 2u;
    }
    if (slots > 4096u) {
        slots = 4096u;
    }
    rec->slots = (runtime_inspector_checkpoint *)calloc(slots, sizeof(*rec->slots));
    rec->slot_count = slots;
    rec->input_cap = RUNTIME_INSPECTOR_INPUT_CAP_DEFAULT;
    rec->inputs = (runtime_inspector_input *)calloc(
        rec->input_cap, sizeof(*rec->inputs));
    if (rec->slots == NULL || rec->inputs == NULL) {
        inspector_recorder_free(rec);
        return NULL;
    }
    rt->inspector_recorder = rec;
    return rec;
}

static void inspector_log_input(
    void *user, uint64_t cycle, int kind, uint32_t a, uint32_t b, uint32_t c)
{
    runtime *rt = (runtime *)user;
    struct runtime_inspector_recorder *rec;
    runtime_inspector_input *ev;

    if (rt == NULL || rt->inspector_recorder == NULL ||
        !rt->inspector_recorder->recording) {
        return;
    }
    rec = rt->inspector_recorder;
    if (rec->input_count == rec->input_cap) {
        rec->input_count--;
    }
    ev = &rec->inputs[rec->input_head];
    ev->cycle = cycle;
    ev->kind = (uint8_t)kind;
    ev->a = (uint8_t)a;
    ev->b = (uint8_t)b;
    ev->c = (uint8_t)c;
    rec->input_head = (rec->input_head + 1u) % rec->input_cap;
    rec->input_count++;
}

static void inspector_on_media(void *user, uint64_t cycle, int device)
{
    runtime_inspector_on_media_event((runtime *)user, cycle, device);
}

bool runtime_inspector_checkpoint_take(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    runtime_inspector_checkpoint *cp;
    size_t size;
    uint8_t *blob;
    uint64_t cycle;

    if (rt == NULL) {
        return false;
    }
    rec = inspector_recorder_ensure(rt);
    if (rec == NULL || !rec->recording) {
        return false;
    }
    cycle = rt->machine.clock.cycle;
    if (rec->count > 0u && rec->last_checkpoint_cycle == cycle) {
        return true;
    }
    size = c64_snapshot_size(&rt->machine);
    if (size == 0u) {
        return false;
    }
    while (rec->count > 0u && rec->used + size > rec->memory_budget) {
        inspector_drop_oldest(rec);
    }
    if (rec->count == rec->slot_count) {
        inspector_drop_oldest(rec);
    }
    blob = (uint8_t *)malloc(size);
    if (blob == NULL) {
        return false;
    }
    if (c64_snapshot_save(&rt->machine, blob, size) != size) {
        free(blob);
        return false;
    }
    cp = &rec->slots[rec->head];
    free(cp->blob);
    memset(cp, 0, sizeof(*cp));
    cp->cycle = cycle;
    cp->size = size;
    cp->blob = blob;
    rec->head = (rec->head + 1u) % rec->slot_count;
    rec->count++;
    rec->used += size;
    rec->last_checkpoint_cycle = cycle;
    inspector_input_drop_older_than(
        rec, rec->count > 0u ? inspector_cp_at(rec, 0u)->cycle : cycle);
    return true;
}

void runtime_inspector_recorder_set_enabled(runtime *rt, bool enabled)
{
    struct runtime_inspector_recorder *rec;

    if (rt == NULL) {
        return;
    }
    if (!enabled) {
        if (rt->inspector_recorder != NULL) {
            rt->inspector_recorder->recording = false;
        }
        c64_set_input_event_callback(&rt->machine, NULL, NULL);
        c64_set_media_event_callback(&rt->machine, NULL, NULL);
        return;
    }
    rec = inspector_recorder_ensure(rt);
    if (rec == NULL) {
        return;
    }
    rec->recording = true;
    rec->cadence_cycles = c64_config_cycles_per_frame(&rt->machine.config);
    if (rec->cadence_cycles == 0u) {
        rec->cadence_cycles = 19656u;
    }
    c64_set_input_event_callback(&rt->machine, inspector_log_input, rt);
    c64_set_media_event_callback(&rt->machine, inspector_on_media, rt);
    (void)runtime_inspector_checkpoint_take(rt);
}

void runtime_inspector_set_enabled(runtime *rt, bool enabled)
{
    bool was_enabled;

    if (rt == NULL) {
        return;
    }

    was_enabled = rt->inspector_enabled;
    rt->inspector_enabled = enabled;

    if (!was_enabled && enabled) {
        if (rt->frame_ring_memory_mb > 0u) {
            runtime_frame_ring_set_recording(&rt->frame_ring, true);
        }
        if (rt->inspector_memory_mb == 0u && !rt->inspector_empty_tape_warned) {
            fprintf(
                stderr,
                "Inspector recording enabled with inspector_memory_mb=0; "
                "checkpoint tape is empty\n");
            rt->inspector_empty_tape_warned = true;
        }
        runtime_inspector_recorder_set_enabled(rt, true);
        return;
    }

    if (was_enabled && !enabled) {
        runtime_inspector_recorder_set_enabled(rt, false);
    }
}

bool runtime_inspector_enabled(const runtime *rt)
{
    return rt != NULL && rt->inspector_enabled;
}

uint32_t runtime_inspector_memory_mb(const runtime *rt)
{
    if (rt == NULL) {
        return 0u;
    }
    return rt->inspector_memory_mb;
}

bool runtime_inspector_recorder_is_recording(const runtime *rt)
{
    return rt != NULL && rt->inspector_recorder != NULL &&
        rt->inspector_recorder->recording;
}

void runtime_inspector_after_step(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    uint64_t cycle;

    if (rt == NULL || rt->inspector_recorder == NULL ||
        !rt->inspector_recorder->recording) {
        return;
    }
    rec = rt->inspector_recorder;
    cycle = rt->machine.clock.cycle;
    if (cycle - rec->last_checkpoint_cycle >= (uint64_t)rec->cadence_cycles) {
        (void)runtime_inspector_checkpoint_take(rt);
    }
}

static const runtime_inspector_checkpoint *inspector_nearest_cp(
    const struct runtime_inspector_recorder *rec, uint64_t cycle)
{
    uint32_t i;
    const runtime_inspector_checkpoint *best = NULL;

    if (rec == NULL) {
        return NULL;
    }
    for (i = 0u; i < rec->count; ++i) {
        const runtime_inspector_checkpoint *cp = inspector_cp_at_const(rec, i);
        if (cp->cycle <= cycle) {
            best = cp;
        }
    }
    return best;
}

static void inspector_prep_dst(const runtime *rt, c64_t *dst)
{
    memcpy(dst->bus.basic_rom, rt->machine.bus.basic_rom, sizeof(dst->bus.basic_rom));
    memcpy(dst->bus.kernal_rom, rt->machine.bus.kernal_rom, sizeof(dst->bus.kernal_rom));
    memcpy(dst->bus.char_rom, rt->machine.bus.char_rom, sizeof(dst->bus.char_rom));
    dst->has_basic_rom = rt->machine.has_basic_rom;
    dst->has_kernal_rom = rt->machine.has_kernal_rom;
    dst->has_character_rom = rt->machine.has_character_rom;
    dst->ready = rt->machine.ready;
    memcpy(dst->drive8.rom, rt->machine.drive8.rom, sizeof(dst->drive8.rom));
    memcpy(dst->drive9.rom, rt->machine.drive9.rom, sizeof(dst->drive9.rom));
    dst->drive8.rom_loaded = rt->machine.drive8.rom_loaded;
    dst->drive9.rom_loaded = rt->machine.drive9.rom_loaded;
}

static void inspector_apply_input(c64_t *dst, const runtime_inspector_input *ev)
{
    switch (ev->kind) {
    case C64_INPUT_EVENT_KEY:
        c64_set_key(dst, (c64_key)ev->a, ev->b != 0u);
        break;
    case C64_INPUT_EVENT_JOYSTICK:
        c64_set_joystick(dst, ev->a, ev->b);
        break;
    default:
        break;
    }
}

static bool inspector_step_to(c64_t *dst, uint64_t target)
{
    while (dst->clock.cycle < target) {
        uint64_t remain = target - dst->clock.cycle;
        uint32_t step = remain > 4096u ? 4096u : (uint32_t)remain;
        uint32_t ran = 0u;
        if (!c64_step_cycles_ex(dst, step, &ran, 0u, NULL, 0) || ran == 0u) {
            return false;
        }
    }
    return true;
}

static bool inspector_replay_to(
    struct runtime_inspector_recorder *rec,
    c64_t *dst,
    uint64_t from_cycle,
    uint64_t to_cycle)
{
    uint32_t i;
    uint32_t tail;

    if (rec->input_count > 0u) {
        tail = (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
        for (i = 0u; i < rec->input_count; ++i) {
            const runtime_inspector_input *ev =
                &rec->inputs[(tail + i) % rec->input_cap];
            if (ev->cycle <= from_cycle) {
                continue;
            }
            if (ev->cycle > to_cycle) {
                break;
            }
            if (!inspector_step_to(dst, ev->cycle)) {
                return false;
            }
            inspector_apply_input(dst, ev);
        }
    }
    return inspector_step_to(dst, to_cycle);
}

static void inspector_enter_seal(c64_t *dst)
{
    c64_set_cpu_observer(dst, NULL, NULL);
    c64_set_memory_access_callback(dst, NULL, NULL);
    c64_set_vicii_line_observer(dst, NULL, NULL);
    c64_set_audio_output_enabled(dst, false);
    c64_set_input_event_callback(dst, NULL, NULL);
    c64_set_media_event_callback(dst, NULL, NULL);
    c64_set_replay_sealed(dst, true);
}

static void inspector_exit_seal(c64_t *dst)
{
    c64_set_replay_sealed(dst, false);
}

bool runtime_inspector_load_nearest_checkpoint(
    runtime *rt, uint64_t cycle, c64_t *dst)
{
    struct runtime_inspector_recorder *rec;
    const runtime_inspector_checkpoint *cp;

    if (rt == NULL || dst == NULL) {
        return false;
    }
    rec = rt->inspector_recorder;
    if (rec == NULL || rec->count == 0u) {
        return false;
    }
    cp = inspector_nearest_cp(rec, cycle);
    if (cp == NULL || cp->blob == NULL) {
        return false;
    }
    inspector_prep_dst(rt, dst);
    return c64_snapshot_load(dst, cp->blob, cp->size);
}

bool runtime_inspector_materialize(runtime *rt, uint64_t cycle, c64_t *dst)
{
    struct runtime_inspector_recorder *rec;
    const runtime_inspector_checkpoint *cp;
    uint64_t hst1_before = 0u;
    uint32_t frames_before = 0u;
    runtime_history_status st;
    runtime_frame_ring_info fi;
    uint64_t live;
    uint64_t oldest;

    if (rt == NULL || dst == NULL) {
        return false;
    }
    rec = rt->inspector_recorder;
    if (rec == NULL || rec->count == 0u) {
        return false;
    }
    oldest = inspector_cp_at(rec, 0u)->cycle;
    live = rt->machine.clock.cycle;
    if (cycle < oldest || cycle > live) {
        return false;
    }
    cp = inspector_nearest_cp(rec, cycle);
    if (cp == NULL || cp->blob == NULL) {
        return false;
    }

    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &st);
        hst1_before = st.record_count;
    }
    runtime_frame_ring_get_info(&rt->frame_ring, &fi);
    frames_before = fi.count;

    inspector_prep_dst(rt, dst);
    if (!c64_snapshot_load(dst, cp->blob, cp->size)) {
        return false;
    }
    inspector_enter_seal(dst);
    if (!inspector_replay_to(rec, dst, cp->cycle, cycle)) {
        inspector_exit_seal(dst);
        return false;
    }
    inspector_exit_seal(dst);

    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &st);
        if (st.record_count != hst1_before) {
            return false;
        }
    }
    runtime_frame_ring_get_info(&rt->frame_ring, &fi);
    if (fi.count != frames_before) {
        return false;
    }
    return dst->clock.cycle >= cp->cycle;
}

static void inspector_truncate_to_cycle(
    runtime *rt,
    uint64_t cycle,
    uint64_t marker_id,
    runtime_history_media_change_kind kind,
    uint32_t arg1)
{
    struct runtime_inspector_recorder *rec = rt->inspector_recorder;
    uint32_t kept = 0u;
    uint32_t i;

    if (rt->history != NULL && marker_id != 0u) {
        runtime_history_status st;
        runtime_history_get_status(rt->history, &st);
        (void)runtime_history_retain_from(rt->history, st.epoch, marker_id);
    }
    runtime_frame_ring_drop_older_than(&rt->frame_ring, cycle);
    if (rec == NULL) {
        return;
    }
    rec->start_kind = kind;
    rec->start_arg1 = arg1;
    rec->truncations++;
    for (i = 0u; i < rec->count; ++i) {
        runtime_inspector_checkpoint *cp = inspector_cp_at(rec, i);
        if (cp->cycle < cycle) {
            rec->used -= cp->size;
            free(cp->blob);
            memset(cp, 0, sizeof(*cp));
        } else {
            kept++;
        }
    }
    rec->dropped += (uint64_t)(rec->count - kept);
    rec->count = kept;
    if (kept > 0u) {
        runtime_inspector_checkpoint *tmp =
            (runtime_inspector_checkpoint *)calloc(rec->slot_count, sizeof(*tmp));
        uint32_t w = 0u;
        if (tmp != NULL) {
            for (i = 0u; i < rec->slot_count; ++i) {
                if (rec->slots[i].blob != NULL) {
                    tmp[w++] = rec->slots[i];
                }
            }
            free(rec->slots);
            rec->slots = tmp;
            rec->head = w % rec->slot_count;
            rec->count = w;
        }
    } else {
        rec->head = 0u;
        rec->used = 0u;
    }
    inspector_input_drop_older_than(rec, cycle);
}

void runtime_inspector_on_media_event(runtime *rt, uint64_t cycle, int device)
{
    uint64_t marker_id = 0u;
    runtime_history_record rec;

    if (rt == NULL || !rt->inspector_enabled) {
        return;
    }
    if (rt->inspector_recorder != NULL && !rt->inspector_recorder->recording) {
        return;
    }
    if (rt->history != NULL) {
        (void)runtime_history_force_new_block(rt->history, cycle);
        if (runtime_history_append_marker(
                rt->history,
                RUNTIME_HISTORY_MARKER_MEDIA_CHANGED,
                (uint32_t)RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE,
                (uint32_t)(device & 0xff),
                cycle) &&
            runtime_history_last(rt->history, &rec)) {
            marker_id = rec.id;
        }
    }
    inspector_truncate_to_cycle(
        rt,
        cycle,
        marker_id,
        RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE,
        (uint32_t)(device & 0xff));
    if (rt->inspector_recorder != NULL &&
        rt->inspector_recorder->recording &&
        rt->inspector_recorder->count == 0u) {
        (void)runtime_inspector_checkpoint_take(rt);
    }
}

void runtime_inspector_on_history_invalidate(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    uint32_t i;

    if (rt == NULL || rt->inspector_recorder == NULL) {
        return;
    }
    rec = rt->inspector_recorder;
    for (i = 0u; i < rec->slot_count; ++i) {
        free(rec->slots[i].blob);
        memset(&rec->slots[i], 0, sizeof(rec->slots[i]));
    }
    rec->count = 0u;
    rec->head = 0u;
    rec->used = 0u;
    rec->last_checkpoint_cycle = 0u;
    rec->input_count = 0u;
    rec->input_head = 0u;
    rec->start_kind = RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN;
    rec->start_arg1 = 0u;
    if (rec->recording) {
        (void)runtime_inspector_checkpoint_take(rt);
    }
}

void runtime_inspector_window_info(
    const runtime *rt, runtime_inspector_window *out)
{
    const struct runtime_inspector_recorder *rec;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (rt == NULL || rt->inspector_recorder == NULL ||
        rt->inspector_recorder->count == 0u) {
        return;
    }
    rec = rt->inspector_recorder;
    out->valid = true;
    out->oldest_cycle = inspector_cp_at_const(rec, 0u)->cycle;
    out->newest_cycle = inspector_cp_at_const(rec, rec->count - 1u)->cycle;
    out->checkpoint_count = rec->count;
    out->checkpoints_dropped = rec->dropped;
    out->media_truncations = rec->truncations;
    out->start_kind = rec->start_kind;
    out->start_arg1 = rec->start_arg1;
}

uint64_t runtime_inspector_checkpoint_count(const runtime *rt)
{
    return (rt != NULL && rt->inspector_recorder != NULL) ?
        rt->inspector_recorder->count : 0u;
}

uint64_t runtime_inspector_checkpoints_dropped(const runtime *rt)
{
    return (rt != NULL && rt->inspector_recorder != NULL) ?
        rt->inspector_recorder->dropped : 0u;
}

uint64_t runtime_inspector_media_truncations(const runtime *rt)
{
    return (rt != NULL && rt->inspector_recorder != NULL) ?
        rt->inspector_recorder->truncations : 0u;
}

uint32_t runtime_inspector_cadence_cycles(const runtime *rt)
{
    if (rt != NULL && rt->inspector_recorder != NULL &&
        rt->inspector_recorder->cadence_cycles != 0u) {
        return rt->inspector_recorder->cadence_cycles;
    }
    if (rt != NULL) {
        return c64_config_cycles_per_frame(&rt->machine.config);
    }
    return 19656u;
}

void runtime_inspector_get_focus(const runtime *rt, runtime_inspector_focus *out)
{
    if (out == NULL) {
        return;
    }
    if (rt == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = rt->inspector_focus;
}

runtime_inspector_mode runtime_inspector_current_mode(const runtime *rt)
{
    return (rt != NULL && rt->inspecting) ?
        RUNTIME_INSPECTOR_MODE_INSPECT : RUNTIME_INSPECTOR_MODE_LIVE;
}

bool runtime_inspector_in_inspect(const runtime *rt)
{
    return rt != NULL && rt->inspecting;
}

const char *runtime_inspector_window_start_name(runtime_history_media_change_kind kind)
{
    switch (kind) {
    case RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE:
        return "guest-write";
    case RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN:
    default:
        return "unknown";
    }
}

bool runtime_inspector_snapshot_machine(runtime *rt, uint8_t **blob, size_t *size)
{
    size_t need;
    uint8_t *bytes;
    size_t written;

    if (rt == NULL || blob == NULL || size == NULL) {
        return false;
    }
    need = c64_snapshot_size(&rt->machine);
    if (need == 0u) {
        return false;
    }
    bytes = (uint8_t *)malloc(need);
    if (bytes == NULL) {
        return false;
    }
    written = c64_snapshot_save(&rt->machine, bytes, need);
    if (written != need) {
        free(bytes);
        return false;
    }
    *blob = bytes;
    *size = written;
    return true;
}

bool runtime_inspector_restore_blob(runtime *rt, const uint8_t *blob, size_t size)
{
    if (rt == NULL || blob == NULL || size == 0u) {
        return false;
    }
    return c64_snapshot_load(&rt->machine, blob, size);
}

void runtime_inspector_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    free(rt->inspector_now_blob);
    rt->inspector_now_blob = NULL;
    rt->inspector_now_size = 0u;
    rt->inspector_now_cycle = 0u;
    rt->inspecting = false;
    memset(&rt->inspector_focus, 0, sizeof(rt->inspector_focus));
}

uint64_t runtime_inspector_live_cycle(const runtime *rt)
{
    uint64_t oldest = 0u;
    uint64_t newest_cp = 0u;
    uint64_t count = 0u;
    uint64_t live;

    if (rt == NULL) {
        return 0u;
    }
    if (rt->inspecting && rt->inspector_now_blob != NULL) {
        live = rt->inspector_now_cycle;
    } else {
        live = rt->machine.clock.cycle;
    }
    if (rt->inspector_recorder != NULL && rt->inspector_recorder->count > 0u) {
        newest_cp = inspector_cp_at_const(
            rt->inspector_recorder, rt->inspector_recorder->count - 1u)->cycle;
        oldest = inspector_cp_at_const(rt->inspector_recorder, 0u)->cycle;
        count = rt->inspector_recorder->count;
        (void)oldest;
        (void)count;
        if (newest_cp > live) {
            live = newest_cp;
        }
    }
    return live;
}

void runtime_inspector_timeline_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *live, uint64_t *count)
{
    uint64_t n = 0u;
    uint64_t cp_old = 0u;

    if (oldest != NULL) {
        *oldest = 0u;
    }
    if (live != NULL) {
        *live = 0u;
    }
    if (count != NULL) {
        *count = 0u;
    }
    if (rt == NULL || rt->inspector_recorder == NULL ||
        rt->inspector_recorder->count == 0u) {
        return;
    }
    n = rt->inspector_recorder->count;
    cp_old = inspector_cp_at_const(rt->inspector_recorder, 0u)->cycle;
    if (oldest != NULL) {
        *oldest = cp_old;
    }
    if (live != NULL) {
        *live = runtime_inspector_live_cycle(rt);
    }
    if (count != NULL) {
        *count = n;
    }
}

bool runtime_inspector_at_live(const runtime *rt)
{
    uint64_t live;

    if (rt == NULL || !rt->inspecting) {
        return false;
    }
    live = rt->inspector_now_cycle;
    if (live == 0u) {
        live = runtime_inspector_live_cycle(rt);
    }
    return rt->machine.clock.cycle >= live;
}

void runtime_inspector_sync_focus(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    memset(&rt->inspector_focus, 0, sizeof(rt->inspector_focus));
    rt->inspector_focus.valid = true;
    rt->inspector_focus.cycle = rt->machine.clock.cycle;
    rt->inspector_focus.pc = rt->machine.cpu.cpu.pc;
    rt->inspector_focus.a = rt->machine.cpu.cpu.A;
    rt->inspector_focus.x = rt->machine.cpu.cpu.X;
    rt->inspector_focus.y = rt->machine.cpu.cpu.Y;
    rt->inspector_focus.p = rt->machine.cpu.cpu.flags;
    rt->inspector_focus.sp = (uint8_t)(rt->machine.cpu.cpu.sp & 0xffu);
}

runtime_inspector_enter_status runtime_inspector_can_enter(const runtime *rt)
{
    if (rt == NULL) {
        return RUNTIME_INSPECTOR_ENTER_UNAVAILABLE;
    }
    if (rt->inspecting) {
        return RUNTIME_INSPECTOR_ENTER_OK;
    }
    if (!rt->inspector_enabled) {
        return RUNTIME_INSPECTOR_ENTER_UNAVAILABLE;
    }
    if (runtime_inspector_checkpoint_count(rt) == 0u) {
        return RUNTIME_INSPECTOR_ENTER_EMPTY;
    }
    return RUNTIME_INSPECTOR_ENTER_OK;
}

void runtime_inspector_apply_live_seal(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    c64_set_cpu_observer(&rt->machine, NULL, NULL);
    c64_set_memory_access_callback(&rt->machine, NULL, NULL);
    c64_set_vicii_line_observer(&rt->machine, NULL, NULL);
    c64_set_audio_output_enabled(&rt->machine, false);
    c64_set_replay_sealed(&rt->machine, true);
}

void runtime_inspector_apply_logged_inputs(
    runtime *rt, c64_t *dst, uint64_t from_inclusive, uint64_t to_inclusive)
{
    struct runtime_inspector_recorder *rec;
    uint32_t i;
    uint32_t tail;

    if (rt == NULL || dst == NULL || rt->inspector_recorder == NULL) {
        return;
    }
    rec = rt->inspector_recorder;
    if (to_inclusive < from_inclusive || rec->input_count == 0u) {
        return;
    }
    tail = (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
    for (i = 0u; i < rec->input_count; ++i) {
        const runtime_inspector_input *ev =
            &rec->inputs[(tail + i) % rec->input_cap];
        if (ev->cycle < from_inclusive) {
            continue;
        }
        if (ev->cycle > to_inclusive) {
            break;
        }
        inspector_apply_input(dst, ev);
    }
}

bool runtime_inspector_restore_live(runtime *rt)
{
    if (rt == NULL || rt->inspector_now_blob == NULL || rt->inspector_now_size == 0u) {
        return false;
    }
    if (!runtime_inspector_restore_blob(rt, rt->inspector_now_blob, rt->inspector_now_size)) {
        return false;
    }
    runtime_inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    return true;
}

bool runtime_inspector_land(runtime *rt, uint64_t cycle)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;
    uint8_t *then_blob = NULL;
    size_t then_size = 0u;
    bool ok;

    if (rt == NULL || !rt->inspecting) {
        return false;
    }
    runtime_inspector_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    if (cycle < oldest) {
        return false;
    }
    if (cycle >= live) {
        return runtime_inspector_restore_live(rt);
    }
    if (!runtime_inspector_snapshot_machine(rt, &then_blob, &then_size)) {
        return false;
    }
    ok = runtime_inspector_load_nearest_checkpoint(rt, cycle, &rt->machine);
    if (!ok) {
        (void)runtime_inspector_restore_blob(rt, then_blob, then_size);
        runtime_inspector_apply_live_seal(rt);
        free(then_blob);
        return false;
    }
    free(then_blob);
    runtime_inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    return true;
}

bool runtime_inspector_reexecute_to(runtime *rt, uint64_t target_cycle)
{
    uint64_t live;

    if (rt == NULL || !rt->inspecting) {
        return false;
    }
    live = runtime_inspector_live_cycle(rt);
    if (target_cycle > live) {
        target_cycle = live;
    }
    runtime_inspector_apply_live_seal(rt);
    while (rt->machine.clock.cycle < target_cycle) {
        uint64_t c0 = rt->machine.clock.cycle;
        char error[256];
        if (!c64_step_cycle(&rt->machine, error, sizeof(error))) {
            break;
        }
        runtime_inspector_apply_logged_inputs(
            rt, &rt->machine, c0 + 1u, rt->machine.clock.cycle);
    }
    if (rt->machine.clock.cycle >= live) {
        return runtime_inspector_restore_live(rt);
    }
    runtime_inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    return true;
}

bool runtime_inspector_frame_step(runtime *rt, int direction)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;
    uint64_t here;

    if (rt == NULL || !rt->inspecting) {
        return false;
    }
    runtime_inspector_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    here = rt->machine.clock.cycle;
    if (direction > 0) {
        if (here >= live) {
            return runtime_inspector_restore_live(rt);
        }
        (void)c64_consume_frame_complete(&rt->machine);
        runtime_inspector_apply_live_seal(rt);
        c64_set_video_output_enabled(&rt->machine, true);
        while (rt->machine.clock.cycle < live) {
            uint64_t c0 = rt->machine.clock.cycle;
            char error[256];
            if (!c64_step_cycle(&rt->machine, error, sizeof(error))) {
                break;
            }
            runtime_inspector_apply_logged_inputs(
                rt, &rt->machine, c0 + 1u, rt->machine.clock.cycle);
            if (c64_consume_frame_complete(&rt->machine)) {
                break;
            }
        }
        if (rt->machine.clock.cycle >= live) {
            return runtime_inspector_restore_live(rt);
        }
        runtime_inspector_apply_live_seal(rt);
        runtime_inspector_sync_focus(rt);
        return true;
    }
    if (direction < 0) {
        uint64_t last_fr;
        uint8_t *then_blob = NULL;
        size_t then_size = 0u;

        if (here <= oldest) {
            return true;
        }
        if (!runtime_inspector_snapshot_machine(rt, &then_blob, &then_size)) {
            return false;
        }
        if (!runtime_inspector_load_nearest_checkpoint(rt, here - 1u, &rt->machine)) {
            (void)runtime_inspector_restore_blob(rt, then_blob, then_size);
            runtime_inspector_apply_live_seal(rt);
            free(then_blob);
            return false;
        }
        free(then_blob);
        last_fr = rt->machine.clock.cycle;
        (void)c64_consume_frame_complete(&rt->machine);
        runtime_inspector_apply_live_seal(rt);
        c64_set_video_output_enabled(&rt->machine, true);
        while (rt->machine.clock.cycle < here) {
            uint64_t c0 = rt->machine.clock.cycle;
            char error[256];
            if (!c64_step_cycle(&rt->machine, error, sizeof(error))) {
                break;
            }
            runtime_inspector_apply_logged_inputs(
                rt, &rt->machine, c0 + 1u, rt->machine.clock.cycle);
            if (c64_consume_frame_complete(&rt->machine)) {
                uint64_t c = rt->machine.clock.cycle;
                if (c < here) {
                    last_fr = c;
                } else {
                    break;
                }
            }
        }
        if (rt->machine.clock.cycle != last_fr) {
            if (!runtime_inspector_load_nearest_checkpoint(rt, last_fr, &rt->machine)) {
                return false;
            }
            if (!runtime_inspector_reexecute_to(rt, last_fr)) {
                return false;
            }
        }
        runtime_inspector_apply_live_seal(rt);
        runtime_inspector_sync_focus(rt);
        return true;
    }
    return true;
}

runtime_inspector_enter_status runtime_inspector_enter(runtime *rt)
{
    runtime_inspector_enter_status can;
    uint8_t *now = NULL;
    size_t now_size = 0u;

    if (rt == NULL) {
        return RUNTIME_INSPECTOR_ENTER_UNAVAILABLE;
    }
    if (rt->inspecting) {
        return RUNTIME_INSPECTOR_ENTER_OK;
    }
    can = runtime_inspector_can_enter(rt);
    if (can != RUNTIME_INSPECTOR_ENTER_OK) {
        return can;
    }

    (void)runtime_inspector_checkpoint_take(rt);
    if (runtime_inspector_checkpoint_count(rt) == 0u) {
        return RUNTIME_INSPECTOR_ENTER_EMPTY;
    }
    if (!runtime_inspector_snapshot_machine(rt, &now, &now_size)) {
        return RUNTIME_INSPECTOR_ENTER_FAILED;
    }

    runtime_inspector_recorder_set_enabled(rt, false);
    runtime_inspector_apply_live_seal(rt);

    rt->inspector_now_blob = now;
    rt->inspector_now_size = now_size;
    rt->inspector_now_cycle = rt->machine.clock.cycle;
    rt->inspecting = true;
    runtime_inspector_sync_focus(rt);
    return RUNTIME_INSPECTOR_ENTER_OK;
}

void runtime_inspector_leave(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    if (!rt->inspecting) {
        return;
    }
    if (rt->inspector_now_blob != NULL && rt->inspector_now_size > 0u) {
        (void)runtime_inspector_restore_blob(
            rt, rt->inspector_now_blob, rt->inspector_now_size);
    }
    c64_set_replay_sealed(&rt->machine, false);
    rt->inspecting = false;
    free(rt->inspector_now_blob);
    rt->inspector_now_blob = NULL;
    rt->inspector_now_size = 0u;
    rt->inspector_now_cycle = 0u;
    memset(&rt->inspector_focus, 0, sizeof(rt->inspector_focus));
    if (rt->inspector_enabled) {
        runtime_inspector_recorder_set_enabled(rt, true);
    }
}
