#include "runtime_internal.h"

#include "message_queue.h"
#include "runtime_breakpoint_ini.h"
#include "runtime_command.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RUNTIME_RUN_BATCH_CYCLES = 1024,
    RUNTIME_TARGET_FPS = 60,
    PASTE_HOLD_CYCLES = 39400,  /* ~40ms at PAL 985248 Hz */
    PASTE_GAP_CYCLES  =  9852,  /* ~10ms at PAL 985248 Hz */
};

static void runtime_reset_pacer(runtime *rt) {
    uint64_t frequency;
    uint32_t multiplier;

    frequency = SDL_GetPerformanceFrequency();
    multiplier = rt->active_turbo_multiplier > 0 ? rt->active_turbo_multiplier : 1u;
    rt->frame_counter_step = frequency / ((uint64_t)RUNTIME_TARGET_FPS * multiplier);
    if (rt->frame_counter_step == 0) {
        rt->frame_counter_step = 1;
    }
    rt->next_frame_counter = SDL_GetPerformanceCounter() + rt->frame_counter_step;
    rt->pace_initialized = true;
}

static void runtime_pace_after_frame(runtime *rt) {
    uint64_t now;
    uint64_t frequency;

    if (rt->speed_mode == RUNTIME_SPEED_MODE_FAST) {
        return;
    }

    if (!rt->pace_initialized) {
        runtime_reset_pacer(rt);
        return;
    }

    now = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    if (now < rt->next_frame_counter) {
        uint64_t remaining = rt->next_frame_counter - now;
        uint32_t delay_ms = (uint32_t)((remaining * 1000u) / frequency);
        if (delay_ms > 0) {
            SDL_Delay(delay_ms);
        }
        while (SDL_GetPerformanceCounter() < rt->next_frame_counter) {
            SDL_Delay(0);
        }
    }

    rt->next_frame_counter += rt->frame_counter_step;
    now = SDL_GetPerformanceCounter();
    if (rt->next_frame_counter < now) {
        rt->next_frame_counter = now + rt->frame_counter_step;
    }
}

static bool runtime_publish_event(
    runtime *rt,
    const runtime_event *event) {
    return message_queue_push(rt->event_queue, event);
}

static void runtime_publish_simple_event(
    runtime *rt,
    runtime_event_type type) {
    runtime_event event = {
        .type = type,
    };

    runtime_publish_event(rt, &event);
}

static void runtime_publish_error(
    runtime *rt,
    const char *message) {
    runtime_event event = {
        .type = RUNTIME_EVENT_ERROR,
    };

    snprintf(event.data.error.message, sizeof(event.data.error.message), "%s", message);
    rt->last_stop_reason = RUNTIME_STOP_REASON_ERROR;
    runtime_publish_event(rt, &event);
}

static void runtime_publish_cpu_state(runtime *rt) {
    runtime_event event = {
        .type = RUNTIME_EVENT_CPU_STATE_RESPONSE,
    };
    c64_cpu_snapshot snapshot;

    c64_copy_cpu_snapshot(&rt->machine, &snapshot);
    event.data.cpu_state.pc = snapshot.pc;
    event.data.cpu_state.a = snapshot.a;
    event.data.cpu_state.x = snapshot.x;
    event.data.cpu_state.y = snapshot.y;
    event.data.cpu_state.sp = snapshot.sp;
    event.data.cpu_state.p = snapshot.p;
    event.data.cpu_state.cycles = snapshot.cycles;

    runtime_publish_event(rt, &event);
}

static void runtime_publish_machine_state(runtime *rt) {
    runtime_event event = {
        .type = RUNTIME_EVENT_MACHINE_STATE_RESPONSE,
    };
    c64_machine_snapshot snapshot;

    c64_copy_machine_snapshot(&rt->machine, &snapshot);
    event.data.machine_state.cycle = snapshot.cycle;
    event.data.machine_state.cpu_cycles = snapshot.cpu_cycles;
    event.data.machine_state.vic_cycles = snapshot.vic_cycles;
    event.data.machine_state.cia_cycles = snapshot.cia_cycles;
    event.data.machine_state.pc = snapshot.pc;
    event.data.machine_state.a = snapshot.a;
    event.data.machine_state.x = snapshot.x;
    event.data.machine_state.y = snapshot.y;
    event.data.machine_state.sp = snapshot.sp;
    event.data.machine_state.p = snapshot.p;
    event.data.machine_state.ready = snapshot.ready ? 1 : 0;
    event.data.machine_state.running = rt->exec_state == RUNTIME_EXEC_RUNNING ? 1 : 0;
    event.data.machine_state.stop_reason = rt->last_stop_reason;
    event.data.machine_state.frame_number = rt->frame_slot.published_frames;
    event.data.machine_state.frame_cycle = rt->next_frame_cycle;
    event.data.machine_state.dropped_frames = rt->frame_slot.dropped_frames;
    event.data.machine_state.screen_ram_writes = snapshot.screen_ram_writes;
    event.data.machine_state.color_ram_writes = snapshot.color_ram_writes;
    event.data.machine_state.vic_register_writes = snapshot.vic_register_writes;
    event.data.machine_state.cia1_register_writes = snapshot.cia1_register_writes;
    event.data.machine_state.cia2_register_writes = snapshot.cia2_register_writes;
    event.data.machine_state.keyboard_events = snapshot.keyboard_events;
    event.data.machine_state.irq_entries = snapshot.irq_entries;
    event.data.machine_state.cia1_icr_reads = snapshot.cia1_icr_reads;
    event.data.machine_state.cia1_icr_writes = snapshot.cia1_icr_writes;
    event.data.machine_state.cia1_interrupt_assertions = snapshot.cia1_interrupt_assertions;
    event.data.machine_state.nmi_entries = snapshot.nmi_entries;
    event.data.machine_state.restore_requests = snapshot.restore_requests;
    event.data.machine_state.cia1_irq_pending = snapshot.cia1_irq_pending ? 1 : 0;
    event.data.machine_state.cia2_nmi_pending = snapshot.cia2_nmi_pending ? 1 : 0;

    runtime_publish_event(rt, &event);
}

static bool runtime_memory_mode_is_valid(uint8_t mode) {
    return mode == RUNTIME_MEMORY_MODE_CPU_MAP || mode == RUNTIME_MEMORY_MODE_RAM;
}

static void runtime_publish_memory(
    runtime *rt,
    uint16_t address,
    uint16_t length,
    runtime_memory_mode mode) {
    runtime_event event = {
        .type = RUNTIME_EVENT_MEMORY_RESPONSE,
    };
    uint16_t i;

    if (length > RUNTIME_MEMORY_SNAPSHOT_MAX) {
        length = RUNTIME_MEMORY_SNAPSHOT_MAX;
    }

    event.data.memory.address = address;
    event.data.memory.mode = mode;
    event.data.memory.length = length;

    for (i = 0; i < length; ++i) {
        uint16_t current = (uint16_t)(address + i);
        event.data.memory.bytes[i] = mode == RUNTIME_MEMORY_MODE_RAM ?
            c64_debug_read_ram(&rt->machine, current) :
            c64_debug_read_cpu_map(&rt->machine, current);
    }

    runtime_publish_event(rt, &event);
}

static void runtime_publish_breakpoints(runtime *rt) {
    runtime_event event = {
        .type = RUNTIME_EVENT_BREAKPOINTS_RESPONSE,
    };
    size_t i;

    event.data.breakpoints.count = (uint16_t)rt->breakpoint_count;
    for (i = 0; i < rt->breakpoint_count && i < RUNTIME_BREAKPOINT_SNAPSHOT_MAX; ++i) {
        event.data.breakpoints.entries[i].id = rt->breakpoints[i].id;
        event.data.breakpoints.entries[i].start_address = rt->breakpoints[i].start_address;
        event.data.breakpoints.entries[i].end_address = rt->breakpoints[i].end_address;
        event.data.breakpoints.entries[i].has_end_address = rt->breakpoints[i].has_end_address ? 1u : 0u;
        event.data.breakpoints.entries[i].access = (runtime_breakpoint_access)rt->breakpoints[i].access_mask;
        event.data.breakpoints.entries[i].mapping = rt->breakpoints[i].mapping;
        event.data.breakpoints.entries[i].actions = rt->breakpoints[i].action_mask;
        event.data.breakpoints.entries[i].enabled = rt->breakpoints[i].enabled ? 1u : 0u;
        event.data.breakpoints.entries[i].use_counter = rt->breakpoints[i].use_counter ? 1u : 0u;
        event.data.breakpoints.entries[i].current_hits = rt->breakpoints[i].current_hits;
        event.data.breakpoints.entries[i].initial_count = rt->breakpoints[i].initial_count;
        event.data.breakpoints.entries[i].reset_count = rt->breakpoints[i].reset_count;
        event.data.breakpoints.entries[i].counter = rt->breakpoints[i].counter;
        event.data.breakpoints.entries[i].address = rt->breakpoints[i].start_address;
        event.data.breakpoints.entries[i].target_hits = rt->breakpoints[i].initial_count;
    }

    runtime_publish_event(rt, &event);
}

static bool runtime_publish_frame_copy(runtime *rt, const c64_frame *frame) {
    runtime_event event = {
        .type = RUNTIME_EVENT_FRAME_READY,
    };

    mutex_lock(rt->frame_slot.mutex);
    if (rt->frame_slot.has_frame) {
        rt->frame_slot.dropped_frames++;
    }
    rt->frame_slot.frame = *frame;
    rt->frame_slot.has_frame = true;
    rt->frame_slot.published_frames++;
    event.data.frame_ready.frame_number = frame->frame_number;
    event.data.frame_ready.machine_cycle = frame->machine_cycle;
    event.data.frame_ready.dropped_frames = rt->frame_slot.dropped_frames;
    mutex_unlock(rt->frame_slot.mutex);

    runtime_publish_event(rt, &event);
    return true;
}

static bool runtime_publish_debug_frame(runtime *rt) {
    c64_frame frame;

    if (!c64_make_frame_snapshot(&rt->machine, &frame)) {
        runtime_publish_error(rt, "failed to generate frame");
        return false;
    }

    return runtime_publish_frame_copy(rt, &frame);
}

static bool runtime_publish_completed_frame(runtime *rt) {
    c64_frame frame;

    if (!c64_copy_completed_frame(&rt->machine, &frame)) {
        runtime_publish_error(rt, "no completed live frame available");
        return false;
    }

    return runtime_publish_frame_copy(rt, &frame);
}

static bool runtime_load_rom(
    runtime *rt,
    const char *name,
    const char *path,
    bool (*load)(c64_rom_set *roms, const char *path, char *error, size_t error_size)) {
    char message[256];
    char error[256];

    if (!path || path[0] == '\0') {
        return true;
    }

    if (load(&rt->roms, path, error, sizeof(error))) {
        return true;
    }

    snprintf(message, sizeof(message), "failed to load %s ROM from %s: %s", name, path, error);
    runtime_publish_error(rt, message);
    return false;
}

static bool runtime_load_configured_roms(runtime *rt) {
    bool ok = true;

    c64_rom_set_init(&rt->roms);

    ok = runtime_load_rom(rt, "system", rt->system_rom_path, c64_rom_load_combined_64c) && ok;
    ok = runtime_load_rom(rt, "BASIC", rt->basic_rom_path, c64_rom_load_basic) && ok;
    ok = runtime_load_rom(rt, "character", rt->char_rom_path, c64_rom_load_character) && ok;
    ok = runtime_load_rom(rt, "KERNAL", rt->kernal_rom_path, c64_rom_load_kernal) && ok;

    return ok;
}

static bool runtime_reset_machine(runtime *rt) {
    char error[256];

    if (!c64_install_roms(&rt->machine, &rt->roms, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    if (!c64_reset(&rt->machine, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    mutex_lock(rt->frame_slot.mutex);
    rt->frame_slot.has_frame = false;
    rt->frame_slot.published_frames = 0;
    rt->frame_slot.consumed_frames = 0;
    rt->frame_slot.dropped_frames = 0;
    mutex_unlock(rt->frame_slot.mutex);

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_RESET;
    rt->breakpoint_hit_pending = false;
    rt->next_frame_cycle = 0;
    rt->pace_initialized = false;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RESET_COMPLETE);
    runtime_publish_cpu_state(rt);
    runtime_publish_breakpoints(rt);
    return true;
}

static bool runtime_replace_string(char **target, const char *value) {
    char *copy = NULL;
    size_t length;

    if (value != NULL && value[0] != '\0') {
        length = strlen(value);
        copy = malloc(length + 1u);
        if (copy == NULL) {
            return false;
        }
        memcpy(copy, value, length + 1u);
    }

    free(*target);
    *target = copy;
    return true;
}

static void runtime_apply_machine_config(runtime *rt, const runtime_command *command) {
    rt->machine_config = command->data.apply_machine_config.config;
    rt->save_ini = command->data.apply_machine_config.save_ini != 0;
    memcpy(rt->turbo_speeds, command->data.apply_machine_config.turbo_speeds, sizeof(rt->turbo_speeds));
    rt->turbo_speed_count = command->data.apply_machine_config.turbo_speed_count;
    rt->active_turbo_multiplier = command->data.apply_machine_config.active_turbo_multiplier;
    if (rt->turbo_speed_count == 0 || rt->active_turbo_multiplier == 0) {
        runtime_config defaults = {0};
        runtime_config_set_turbo_defaults(&defaults);
        memcpy(rt->turbo_speeds, defaults.turbo_speeds, sizeof(rt->turbo_speeds));
        rt->turbo_speed_count = defaults.turbo_speed_count;
        rt->active_turbo_multiplier = defaults.active_turbo_multiplier;
    }
    rt->pace_initialized = false;
    if (!runtime_replace_string(&rt->ini_path, command->data.apply_machine_config.ini_path)) {
        runtime_publish_error(rt, "failed to update runtime INI path");
        return;
    }
    c64_set_config(&rt->machine, &rt->machine_config);
    if (command->data.apply_machine_config.reset != 0) {
        runtime_reset_machine(rt);
    } else {
        runtime_publish_machine_state(rt);
    }
}

static int runtime_find_breakpoint_by_id(const runtime *rt, uint32_t id) {
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        if (rt->breakpoints[i].id == id) {
            return (int)i;
        }
    }

    return -1;
}

static bool runtime_breakpoint_mapping_is_valid(runtime_breakpoint_mapping mapping) {
    return mapping == RUNTIME_BREAKPOINT_MAPPING_MAP ||
        mapping == RUNTIME_BREAKPOINT_MAPPING_ROM ||
        mapping == RUNTIME_BREAKPOINT_MAPPING_RAM;
}

static bool runtime_breakpoint_definition_is_valid(const runtime_breakpoint_definition *definition) {
    uint32_t supported_access =
        RUNTIME_BREAKPOINT_ACCESS_EXECUTE |
        RUNTIME_BREAKPOINT_ACCESS_READ |
        RUNTIME_BREAKPOINT_ACCESS_WRITE;
    uint32_t supported_actions =
        RUNTIME_BREAKPOINT_ACTION_BREAK |
        RUNTIME_BREAKPOINT_ACTION_FAST |
        RUNTIME_BREAKPOINT_ACTION_SLOW |
        RUNTIME_BREAKPOINT_ACTION_TRON |
        RUNTIME_BREAKPOINT_ACTION_TROFF |
        RUNTIME_BREAKPOINT_ACTION_TYPE |
        RUNTIME_BREAKPOINT_ACTION_SWAP;

    if (definition == NULL) {
        return false;
    }

    if ((definition->access & supported_access) == 0 ||
        (definition->access & ~supported_access) != 0) {
        return false;
    }

    if (!runtime_breakpoint_mapping_is_valid(definition->mapping)) {
        return false;
    }

    if ((definition->actions & supported_actions) == 0 ||
        (definition->actions & ~supported_actions) != 0) {
        return false;
    }

    return true;
}

static void runtime_breakpoint_apply_definition(
    runtime_breakpoint *breakpoint,
    const runtime_breakpoint_definition *definition,
    bool reset_hits) {
    breakpoint->enabled = definition->enabled != 0;
    breakpoint->start_address = definition->start_address;
    breakpoint->end_address = definition->has_end_address ?
        definition->end_address :
        definition->start_address;
    breakpoint->has_end_address = definition->has_end_address != 0;
    breakpoint->access_mask = definition->access;
    breakpoint->mapping = definition->mapping;
    breakpoint->action_mask = definition->actions;
    breakpoint->use_counter = definition->use_counter != 0;
    breakpoint->initial_count = definition->initial_count;
    breakpoint->reset_count = definition->reset_count;
    breakpoint->counter = definition->initial_count;
    if (reset_hits) {
        breakpoint->current_hits = 0;
    }
}

static bool runtime_add_breakpoint(
    runtime *rt,
    const runtime_breakpoint_definition *definition,
    uint32_t *out_id) {
    runtime_breakpoint *breakpoint;

    if (!runtime_breakpoint_definition_is_valid(definition)) {
        runtime_publish_error(rt, "invalid breakpoint definition");
        runtime_publish_breakpoints(rt);
        return false;
    }

    if (rt->breakpoint_count >= RUNTIME_BREAKPOINT_CAPACITY) {
        runtime_publish_error(rt, "breakpoint table is full");
        runtime_publish_breakpoints(rt);
        return false;
    }

    if (rt->next_breakpoint_id == 0) {
        rt->next_breakpoint_id = 1;
    }

    breakpoint = &rt->breakpoints[rt->breakpoint_count];
    breakpoint->id = rt->next_breakpoint_id++;
    runtime_breakpoint_apply_definition(breakpoint, definition, true);
    rt->breakpoint_count++;

    if (out_id != NULL) {
        *out_id = breakpoint->id;
    }

    runtime_publish_breakpoints(rt);
    return true;
}

static int runtime_find_execute_breakpoint_by_address(const runtime *rt, uint16_t address) {
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        if (rt->breakpoints[i].start_address == address &&
            !rt->breakpoints[i].has_end_address &&
            (rt->breakpoints[i].access_mask & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0 &&
            rt->breakpoints[i].mapping == RUNTIME_BREAKPOINT_MAPPING_MAP &&
            (rt->breakpoints[i].action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
            return (int)i;
        }
    }

    return -1;
}

static bool runtime_breakpoint_address_matches(
    const runtime_breakpoint *breakpoint,
    uint16_t address) {
    if (!breakpoint->has_end_address) {
        return breakpoint->start_address == address;
    }

    if (breakpoint->start_address <= breakpoint->end_address) {
        return address >= breakpoint->start_address && address <= breakpoint->end_address;
    }

    return address >= breakpoint->start_address || address <= breakpoint->end_address;
}

static bool runtime_breakpoint_mapping_matches(
    runtime *rt,
    const runtime_breakpoint *breakpoint,
    uint16_t address) {
    c64_memory_visibility visibility;

    if (breakpoint->mapping == RUNTIME_BREAKPOINT_MAPPING_MAP) {
        return true;
    }

    visibility = c64_memory_visibility_at(&rt->machine, address);
    if (breakpoint->mapping == RUNTIME_BREAKPOINT_MAPPING_ROM) {
        return visibility == C64_MEMORY_VISIBILITY_ROM;
    }

    if (breakpoint->mapping == RUNTIME_BREAKPOINT_MAPPING_RAM) {
        return visibility == C64_MEMORY_VISIBILITY_RAM;
    }

    return false;
}

static bool runtime_breakpoint_record_match(runtime_breakpoint *breakpoint) {
    breakpoint->current_hits++;

    if (!breakpoint->use_counter) {
        return true;
    }

    if (breakpoint->counter > 0) {
        breakpoint->counter--;
    }

    if (breakpoint->counter > 0) {
        return false;
    }

    breakpoint->counter = breakpoint->reset_count;
    return true;
}

static bool runtime_execute_breakpoint_actions(runtime *rt, const runtime_breakpoint *breakpoint) {
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
        return true;
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_FAST) != 0) {
        rt->speed_mode = RUNTIME_SPEED_MODE_FAST;
        rt->pace_initialized = false;
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0) {
        rt->speed_mode = RUNTIME_SPEED_MODE_SLOW;
        rt->pace_initialized = false;
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TRON) != 0) {
        rt->trace_enabled = true;
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0) {
        rt->trace_enabled = false;
    }

    /*
     * Type and Swap are intentional Phase 13 no-ops. They are ordered and kept
     * in the runtime-owned action set for later implementation.
     */

    return false;
}

static bool runtime_breakpoint_matches_access(
    runtime *rt,
    runtime_breakpoint_access access,
    uint16_t address) {
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        runtime_breakpoint *breakpoint = &rt->breakpoints[i];

        if (breakpoint->enabled &&
            (breakpoint->access_mask & access) != 0 &&
            runtime_breakpoint_address_matches(breakpoint, address) &&
            runtime_breakpoint_mapping_matches(rt, breakpoint, address) &&
            runtime_breakpoint_record_match(breakpoint)) {
            return runtime_execute_breakpoint_actions(rt, breakpoint);
        }
    }

    return false;
}

static bool runtime_breakpoint_matches_pc(runtime *rt) {
    return runtime_breakpoint_matches_access(
        rt,
        RUNTIME_BREAKPOINT_ACCESS_EXECUTE,
        rt->machine.cpu.cpu.pc);
}

static void runtime_memory_access(
    void *user,
    c64_memory_access_type access,
    uint16_t address,
    uint8_t value) {
    runtime *rt = user;
    runtime_breakpoint_access breakpoint_access;

    (void)value;

    if (rt == NULL || rt->breakpoint_hit_pending) {
        return;
    }

    breakpoint_access = access == C64_MEMORY_ACCESS_WRITE ?
        RUNTIME_BREAKPOINT_ACCESS_WRITE :
        RUNTIME_BREAKPOINT_ACCESS_READ;

    if (runtime_breakpoint_matches_access(rt, breakpoint_access, address)) {
        rt->breakpoint_hit_pending = true;
    }
}

static void runtime_pause_for_breakpoint(runtime *rt) {
    rt->breakpoint_hit_pending = false;
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_BREAKPOINT;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
    runtime_publish_machine_state(rt);
    runtime_publish_breakpoints(rt);
}

static bool runtime_pause_if_breakpoint_pending(runtime *rt) {
    if (!rt->breakpoint_hit_pending) {
        return false;
    }

    runtime_pause_for_breakpoint(rt);
    return true;
}

static bool runtime_step_cycle(runtime *rt) {
    char error[256];

    if (!c64_step_cycle(&rt->machine, error, sizeof(error))) {
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        runtime_publish_error(rt, error);
        return false;
    }

    return true;
}

static bool runtime_step_cycle_command(runtime *rt) {
    if (runtime_breakpoint_matches_pc(rt)) {
        runtime_pause_for_breakpoint(rt);
        return true;
    }

    if (!runtime_step_cycle(rt)) {
        return false;
    }

    if (runtime_pause_if_breakpoint_pending(rt)) {
        return true;
    }

    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_machine_state(rt);
    return true;
}

static bool runtime_step_instruction(runtime *rt) {
    char error[256];
    c64_cpu_snapshot snapshot;

    if (runtime_breakpoint_matches_pc(rt)) {
        runtime_pause_for_breakpoint(rt);
        return true;
    }

    if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    if (runtime_pause_if_breakpoint_pending(rt)) {
        return true;
    }

    c64_copy_cpu_snapshot(&rt->machine, &snapshot);
    fprintf(
        stderr,
        "STEP instruction PC=%04X CYCLES=%llu\n",
        snapshot.pc,
        (unsigned long long)snapshot.cycles);
    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

static bool runtime_run_instructions(runtime *rt, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; i++) {
        if (runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return true;
        }

        if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
            runtime_publish_error(rt, error);
            return false;
        }

        if (runtime_pause_if_breakpoint_pending(rt)) {
            return true;
        }
    }

    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_cpu_state(rt);
    return true;
}

static bool runtime_run_cycles(runtime *rt, size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return true;
        }

        if (!runtime_step_cycle(rt)) {
            return false;
        }

        if (runtime_pause_if_breakpoint_pending(rt)) {
            return true;
        }
    }

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_RUN_COMPLETE;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RUN_COMPLETE);
    runtime_publish_machine_state(rt);
    return true;
}

static void runtime_set_execute_breakpoint(runtime *rt, const runtime_command *command) {
    runtime_breakpoint_definition definition;
    int index = runtime_find_execute_breakpoint_by_address(
        rt,
        command->data.set_execute_breakpoint.address);

    if (index >= 0) {
        rt->breakpoints[index].enabled = command->data.set_execute_breakpoint.enabled != 0;
        runtime_publish_breakpoints(rt);
        return;
    }

    definition.enabled = command->data.set_execute_breakpoint.enabled;
    definition.start_address = command->data.set_execute_breakpoint.address;
    definition.end_address = command->data.set_execute_breakpoint.address;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition.use_counter = 0;
    definition.initial_count = 0;
    definition.reset_count = 0;
    runtime_add_breakpoint(rt, &definition, NULL);
}

static void runtime_create_breakpoint(runtime *rt, const runtime_command *command) {
    runtime_add_breakpoint(rt, &command->data.create_breakpoint.definition, NULL);
}

static void runtime_update_breakpoint(runtime *rt, const runtime_command *command) {
    int index = runtime_find_breakpoint_by_id(rt, command->data.update_breakpoint.id);

    if (index < 0) {
        runtime_publish_error(rt, "breakpoint id not found");
        runtime_publish_breakpoints(rt);
        return;
    }

    if (!runtime_breakpoint_definition_is_valid(&command->data.update_breakpoint.definition)) {
        runtime_publish_error(rt, "invalid breakpoint definition");
        runtime_publish_breakpoints(rt);
        return;
    }

    runtime_breakpoint_apply_definition(
        &rt->breakpoints[index],
        &command->data.update_breakpoint.definition,
        true);
    runtime_publish_breakpoints(rt);
}

static void runtime_duplicate_breakpoint(runtime *rt, const runtime_command *command) {
    runtime_breakpoint_definition definition;
    runtime_breakpoint *source;
    int index = runtime_find_breakpoint_by_id(rt, command->data.duplicate_breakpoint.id);

    if (index < 0) {
        runtime_publish_error(rt, "breakpoint id not found");
        runtime_publish_breakpoints(rt);
        return;
    }

    source = &rt->breakpoints[index];
    definition.enabled = source->enabled ? 1u : 0u;
    definition.start_address = source->start_address;
    definition.end_address = source->end_address;
    definition.has_end_address = source->has_end_address ? 1u : 0u;
    definition.access = source->access_mask;
    definition.mapping = source->mapping;
    definition.actions = source->action_mask;
    definition.use_counter = source->use_counter ? 1u : 0u;
    definition.initial_count = source->initial_count;
    definition.reset_count = source->reset_count;
    runtime_add_breakpoint(rt, &definition, NULL);
}

static void runtime_clear_breakpoint(runtime *rt, const runtime_command *command) {
    int index = runtime_find_breakpoint_by_id(rt, command->data.clear_breakpoint.id);

    if (index >= 0) {
        size_t i;
        for (i = (size_t)index; i + 1u < rt->breakpoint_count; ++i) {
            rt->breakpoints[i] = rt->breakpoints[i + 1u];
        }
        rt->breakpoint_count--;
    }

    runtime_publish_breakpoints(rt);
}

static void runtime_clear_all_breakpoints(runtime *rt) {
    rt->breakpoint_count = 0;
    runtime_publish_breakpoints(rt);
}

static void runtime_set_breakpoint_enabled(runtime *rt, const runtime_command *command) {
    int index = runtime_find_breakpoint_by_id(rt, command->data.set_breakpoint_enabled.id);

    if (index >= 0) {
        rt->breakpoints[index].enabled = command->data.set_breakpoint_enabled.enabled != 0;
    }

    runtime_publish_breakpoints(rt);
}

static void runtime_set_cpu_register(runtime *rt, const runtime_command *command) {
    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_cpu_state(rt);
        return;
    }

    switch (command->data.set_cpu_register.reg) {
        case RUNTIME_CPU_REGISTER_PC:
            rt->machine.cpu.cpu.pc = command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_SP:
            rt->machine.cpu.cpu.sp = 0x0100u + (command->data.set_cpu_register.value & 0x00ffu);
            break;

        case RUNTIME_CPU_REGISTER_A:
            rt->machine.cpu.cpu.A = (uint8_t)command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_X:
            rt->machine.cpu.cpu.X = (uint8_t)command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_Y:
            rt->machine.cpu.cpu.Y = (uint8_t)command->data.set_cpu_register.value;
            break;

        case RUNTIME_CPU_REGISTER_STATUS:
            rt->machine.cpu.cpu.flags = (uint8_t)command->data.set_cpu_register.value;
            break;

        default:
            runtime_publish_error(rt, "unsupported CPU register set command");
            return;
    }

    runtime_publish_cpu_state(rt);
}

static void runtime_write_memory_byte(runtime *rt, const runtime_command *command) {
    runtime_memory_mode mode;

    if (!runtime_memory_mode_is_valid(command->data.write_memory_byte.mode)) {
        runtime_publish_error(rt, "unsupported memory write mode");
        return;
    }

    mode = (runtime_memory_mode)command->data.write_memory_byte.mode;
    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_memory(rt, command->data.write_memory_byte.address, 1, mode);
        return;
    }

    if (mode == RUNTIME_MEMORY_MODE_RAM) {
        c64_debug_write_ram(
            &rt->machine,
            command->data.write_memory_byte.address,
            command->data.write_memory_byte.value);
    } else {
        c64_debug_write_cpu_map(
            &rt->machine,
            command->data.write_memory_byte.address,
            command->data.write_memory_byte.value);
    }

    runtime_publish_memory(rt, command->data.write_memory_byte.address, 1, mode);
}

static void runtime_load_prg(runtime *rt, const runtime_command *command) {
    FILE *file;
    uint8_t *bytes;
    size_t length;
    size_t payload_length;
    uint16_t load_address;
    size_t i;

    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_error(rt, "PRG load requires paused runtime");
        return;
    }

    file = fopen(command->data.load_prg.path, "rb");
    if (file == NULL) {
        runtime_publish_error(rt, "failed to open PRG file");
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        runtime_publish_error(rt, "failed to inspect PRG file");
        return;
    }
    length = (size_t)ftell(file);
    if (fseek(file, 0, SEEK_SET) != 0 || length < 2 || length > 65538u) {
        fclose(file);
        runtime_publish_error(rt, "invalid PRG file");
        return;
    }

    bytes = malloc(length);
    if (bytes == NULL) {
        fclose(file);
        runtime_publish_error(rt, "failed to allocate PRG buffer");
        return;
    }

    if (fread(bytes, 1, length, file) != length) {
        free(bytes);
        fclose(file);
        runtime_publish_error(rt, "failed to read PRG file");
        return;
    }
    fclose(file);

    load_address = (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
    payload_length = length - 2u;
    for (i = 0; i < payload_length; ++i) {
        c64_debug_write_ram(&rt->machine, (uint16_t)(load_address + i), bytes[i + 2u]);
    }

    free(bytes);
    runtime_publish_memory(
        rt,
        load_address,
        payload_length > RUNTIME_MEMORY_SNAPSHOT_MAX ? RUNTIME_MEMORY_SNAPSHOT_MAX : (uint16_t)payload_length,
        RUNTIME_MEMORY_MODE_RAM);
    runtime_publish_machine_state(rt);
}

static bool runtime_process_command(runtime *rt, const runtime_command *command, bool *alive) {
    switch (command->type) {
        case RUNTIME_COMMAND_PING:
            runtime_publish_simple_event(rt, RUNTIME_EVENT_PONG);
            break;

        case RUNTIME_COMMAND_QUIT:
            rt->exec_state = RUNTIME_EXEC_STOPPED;
            *alive = false;
            break;

        case RUNTIME_COMMAND_RESET:
            runtime_reset_machine(rt);
            break;

        case RUNTIME_COMMAND_RUN:
            fprintf(stderr, "RUN command received\n");
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
            runtime_reset_pacer(rt);
            runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
            break;

        case RUNTIME_COMMAND_PAUSE:
            fprintf(stderr, "PAUSE command received\n");
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            rt->last_stop_reason = RUNTIME_STOP_REASON_PAUSE_COMMAND;
            runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
            runtime_publish_machine_state(rt);
            break;

        case RUNTIME_COMMAND_STEP_CYCLE:
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_step_cycle_command(rt);
            break;

        case RUNTIME_COMMAND_STEP_INSTRUCTION:
            fprintf(stderr, "STEP_INSTRUCTION command received\n");
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_step_instruction(rt);
            break;

        case RUNTIME_COMMAND_RUN_CYCLES:
            runtime_run_cycles(rt, command->data.run_cycles.count);
            break;

        case RUNTIME_COMMAND_RUN_INSTRUCTIONS:
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_run_instructions(rt, command->data.run_instructions.count);
            break;

        case RUNTIME_COMMAND_REQUEST_CPU_STATE:
            runtime_publish_cpu_state(rt);
            break;

        case RUNTIME_COMMAND_REQUEST_MACHINE_STATE:
            runtime_publish_machine_state(rt);
            break;

        case RUNTIME_COMMAND_REQUEST_MEMORY:
            if (runtime_memory_mode_is_valid(command->data.request_memory.mode)) {
                runtime_publish_memory(
                    rt,
                    command->data.request_memory.address,
                    command->data.request_memory.length,
                    (runtime_memory_mode)command->data.request_memory.mode);
            } else {
                runtime_publish_error(rt, "unsupported memory request mode");
            }
            break;

        case RUNTIME_COMMAND_REQUEST_FRAME:
            runtime_publish_debug_frame(rt);
            break;

        case RUNTIME_COMMAND_KEYBOARD_KEY:
            c64_set_key(&rt->machine, command->data.keyboard_key.key, command->data.keyboard_key.pressed != 0);
            break;

        case RUNTIME_COMMAND_RESTORE:
            c64_restore(&rt->machine);
            break;

        case RUNTIME_COMMAND_SET_CPU_REGISTER:
            runtime_set_cpu_register(rt, command);
            break;

        case RUNTIME_COMMAND_WRITE_MEMORY_BYTE:
            runtime_write_memory_byte(rt, command);
            break;

        case RUNTIME_COMMAND_SET_EXECUTE_BREAKPOINT:
            runtime_set_execute_breakpoint(rt, command);
            break;

        case RUNTIME_COMMAND_CLEAR_BREAKPOINT:
            runtime_clear_breakpoint(rt, command);
            break;

        case RUNTIME_COMMAND_CLEAR_ALL_BREAKPOINTS:
            runtime_clear_all_breakpoints(rt);
            break;

        case RUNTIME_COMMAND_SET_BREAKPOINT_ENABLED:
            runtime_set_breakpoint_enabled(rt, command);
            break;

        case RUNTIME_COMMAND_CREATE_BREAKPOINT:
            runtime_create_breakpoint(rt, command);
            break;

        case RUNTIME_COMMAND_UPDATE_BREAKPOINT:
            runtime_update_breakpoint(rt, command);
            break;

        case RUNTIME_COMMAND_DUPLICATE_BREAKPOINT:
            runtime_duplicate_breakpoint(rt, command);
            break;

        case RUNTIME_COMMAND_REQUEST_BREAKPOINTS:
            runtime_publish_breakpoints(rt);
            break;

        case RUNTIME_COMMAND_LOAD_PRG:
            runtime_load_prg(rt, command);
            break;

        case RUNTIME_COMMAND_APPLY_MACHINE_CONFIG:
            runtime_apply_machine_config(rt, command);
            break;

        case RUNTIME_COMMAND_PASTE_TEXT: {
            paste_state *p = &rt->paste;
            size_t len = command->data.paste_text.length;
            if (len > RUNTIME_PASTE_TEXT_MAX) {
                len = RUNTIME_PASTE_TEXT_MAX;
            }
            memcpy(p->text, command->data.paste_text.text, len);
            p->length = len;
            p->position = 0;
            p->shift_needed = false;
            p->in_gap = true;
            p->phase_end_cycle = rt->machine.clock.cycle;
            rt->paste_active = true;
            break;
        }

        case RUNTIME_COMMAND_NONE:
        default:
            runtime_publish_error(rt, "unsupported runtime command");
            break;
    }

    return *alive;
}

typedef struct paste_key_entry {
    c64_key key;
    bool shift;
    bool valid;
} paste_key_entry;

/* Maps ASCII codes 0-127 to C64 key + shift state.
   Letters map without shift; uppercase letters in the host string still press
   the unshifted key because the C64 shows uppercase by default in its standard
   character mode. Shifted symbols match the C64 keyboard layout. */
static const paste_key_entry paste_ascii_map[128] = {
    ['\n'] = { C64_KEY_RETURN,     false, true },
    ['\r'] = { C64_KEY_RETURN,     false, true },
    [' ']  = { C64_KEY_SPACE,      false, true },
    ['!']  = { C64_KEY_1,          true,  true },
    ['"']  = { C64_KEY_2,          true,  true },
    ['#']  = { C64_KEY_3,          true,  true },
    ['$']  = { C64_KEY_4,          true,  true },
    ['%']  = { C64_KEY_5,          true,  true },
    ['&']  = { C64_KEY_6,          true,  true },
    ['\''] = { C64_KEY_7,          true,  true },
    ['(']  = { C64_KEY_8,          true,  true },
    [')']  = { C64_KEY_9,          true,  true },
    ['*']  = { C64_KEY_ASTERISK,   false, true },
    ['+']  = { C64_KEY_PLUS,       false, true },
    [',']  = { C64_KEY_COMMA,      false, true },
    ['-']  = { C64_KEY_MINUS,      false, true },
    ['.']  = { C64_KEY_PERIOD,     false, true },
    ['/']  = { C64_KEY_SLASH,      false, true },
    ['<']  = { C64_KEY_COMMA,      true,  true },
    ['>']  = { C64_KEY_PERIOD,     true,  true },
    ['?']  = { C64_KEY_SLASH,      true,  true },
    ['0']  = { C64_KEY_0,          false, true },
    ['1']  = { C64_KEY_1,          false, true },
    ['2']  = { C64_KEY_2,          false, true },
    ['3']  = { C64_KEY_3,          false, true },
    ['4']  = { C64_KEY_4,          false, true },
    ['5']  = { C64_KEY_5,          false, true },
    ['6']  = { C64_KEY_6,          false, true },
    ['7']  = { C64_KEY_7,          false, true },
    ['8']  = { C64_KEY_8,          false, true },
    ['9']  = { C64_KEY_9,          false, true },
    [':']  = { C64_KEY_COLON,      false, true },
    [';']  = { C64_KEY_SEMICOLON,  false, true },
    ['=']  = { C64_KEY_EQUALS,     false, true },
    ['@']  = { C64_KEY_AT,         false, true },
    ['[']  = { C64_KEY_COLON,      true,  true },
    [']']  = { C64_KEY_SEMICOLON,  true,  true },
    ['^']  = { C64_KEY_UP_ARROW,   false, true },
    ['A']  = { C64_KEY_A,          false, true },
    ['B']  = { C64_KEY_B,          false, true },
    ['C']  = { C64_KEY_C,          false, true },
    ['D']  = { C64_KEY_D,          false, true },
    ['E']  = { C64_KEY_E,          false, true },
    ['F']  = { C64_KEY_F,          false, true },
    ['G']  = { C64_KEY_G,          false, true },
    ['H']  = { C64_KEY_H,          false, true },
    ['I']  = { C64_KEY_I,          false, true },
    ['J']  = { C64_KEY_J,          false, true },
    ['K']  = { C64_KEY_K,          false, true },
    ['L']  = { C64_KEY_L,          false, true },
    ['M']  = { C64_KEY_M,          false, true },
    ['N']  = { C64_KEY_N,          false, true },
    ['O']  = { C64_KEY_O,          false, true },
    ['P']  = { C64_KEY_P,          false, true },
    ['Q']  = { C64_KEY_Q,          false, true },
    ['R']  = { C64_KEY_R,          false, true },
    ['S']  = { C64_KEY_S,          false, true },
    ['T']  = { C64_KEY_T,          false, true },
    ['U']  = { C64_KEY_U,          false, true },
    ['V']  = { C64_KEY_V,          false, true },
    ['W']  = { C64_KEY_W,          false, true },
    ['X']  = { C64_KEY_X,          false, true },
    ['Y']  = { C64_KEY_Y,          false, true },
    ['Z']  = { C64_KEY_Z,          false, true },
    ['a']  = { C64_KEY_A,          false, true },
    ['b']  = { C64_KEY_B,          false, true },
    ['c']  = { C64_KEY_C,          false, true },
    ['d']  = { C64_KEY_D,          false, true },
    ['e']  = { C64_KEY_E,          false, true },
    ['f']  = { C64_KEY_F,          false, true },
    ['g']  = { C64_KEY_G,          false, true },
    ['h']  = { C64_KEY_H,          false, true },
    ['i']  = { C64_KEY_I,          false, true },
    ['j']  = { C64_KEY_J,          false, true },
    ['k']  = { C64_KEY_K,          false, true },
    ['l']  = { C64_KEY_L,          false, true },
    ['m']  = { C64_KEY_M,          false, true },
    ['n']  = { C64_KEY_N,          false, true },
    ['o']  = { C64_KEY_O,          false, true },
    ['p']  = { C64_KEY_P,          false, true },
    ['q']  = { C64_KEY_Q,          false, true },
    ['r']  = { C64_KEY_R,          false, true },
    ['s']  = { C64_KEY_S,          false, true },
    ['t']  = { C64_KEY_T,          false, true },
    ['u']  = { C64_KEY_U,          false, true },
    ['v']  = { C64_KEY_V,          false, true },
    ['w']  = { C64_KEY_W,          false, true },
    ['x']  = { C64_KEY_X,          false, true },
    ['y']  = { C64_KEY_Y,          false, true },
    ['z']  = { C64_KEY_Z,          false, true },
};

static void runtime_advance_paste(runtime *rt) {
    paste_state *p = &rt->paste;
    uint64_t now = rt->machine.clock.cycle;
    paste_key_entry entry;
    unsigned char ch;

    if (now < p->phase_end_cycle) {
        return;
    }

    if (!p->in_gap) {
        /* hold phase ended — release the key */
        entry = paste_ascii_map[(unsigned char)p->text[p->position] & 0x7f];
        c64_set_key(&rt->machine, entry.key, false);
        if (p->shift_needed) {
            c64_set_key(&rt->machine, C64_KEY_LEFT_SHIFT, false);
        }
        p->position++;
        p->in_gap = true;
        p->phase_end_cycle = now + PASTE_GAP_CYCLES;
        return;
    }

    /* gap phase ended — press the next mappable character */
    while (p->position < p->length) {
        ch = (unsigned char)p->text[p->position];
        if (ch >= 128 || !paste_ascii_map[ch].valid) {
            p->position++;
            continue;
        }

        entry = paste_ascii_map[ch];
        p->shift_needed = entry.shift;
        if (entry.shift) {
            c64_set_key(&rt->machine, C64_KEY_LEFT_SHIFT, true);
        }
        c64_set_key(&rt->machine, entry.key, true);
        p->in_gap = false;
        p->phase_end_cycle = now + PASTE_HOLD_CYCLES;
        return;
    }

    rt->paste_active = false;
}

int runtime_thread_main(void *userdata) {
    runtime *rt = userdata;
    bool alive = true;

    c64_init(&rt->machine);
    c64_set_config(&rt->machine, &rt->machine_config);
    c64_set_memory_access_callback(&rt->machine, runtime_memory_access, rt);
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    rt->speed_mode = RUNTIME_SPEED_MODE_SLOW;
    rt->trace_enabled = false;
    rt->breakpoint_count = 0;
    rt->next_breakpoint_id = 1;
    rt->next_frame_cycle = 0;
    rt->pace_initialized = false;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STARTED);
    if (runtime_load_configured_roms(rt)) {
        runtime_reset_machine(rt);
        if (runtime_load_breakpoints_from_ini(rt)) {
            runtime_publish_breakpoints(rt);
        }
    }

    while (alive) {
        runtime_command command;

        if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
            int i;

            while (message_queue_try_pop(rt->command_queue, &command)) {
                if (!runtime_process_command(rt, &command, &alive)) {
                    break;
                }
            }

            for (i = 0; alive && rt->exec_state == RUNTIME_EXEC_RUNNING && i < RUNTIME_RUN_BATCH_CYCLES; i++) {
                if (runtime_breakpoint_matches_pc(rt)) {
                    runtime_pause_for_breakpoint(rt);
                    break;
                }
                if (!runtime_step_cycle(rt)) {
                    break;
                }
                if (rt->paste_active) {
                    runtime_advance_paste(rt);
                }
                if (runtime_pause_if_breakpoint_pending(rt)) {
                    break;
                }
                if (c64_consume_frame_complete(&rt->machine)) {
                    runtime_publish_completed_frame(rt);
                    rt->next_frame_cycle = rt->machine.clock.cycle;
                    runtime_pace_after_frame(rt);
                }
            }

            continue;
        }

        if (!message_queue_wait_pop(rt->command_queue, &command)) {
            continue;
        }

        runtime_process_command(rt, &command, &alive);
    }

    runtime_publish_simple_event(rt, RUNTIME_EVENT_STOPPED);
    return 0;
}
