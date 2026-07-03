#include "runtime_internal.h"

#include "audio_buffer.h"
#include "c64.h"
#include "message_queue.h"
#include "runtime_breakpoint_ini.h"
#include "runtime_command.h"
#include "runtime_event.h"
#include "runtime_assembler.h"
#include "crt.h"
#include "d64.h"
#include "disasm_6502.h"
#include "t64.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RUNTIME_RUN_BATCH_CYCLES = 1024,
    RUNTIME_TARGET_FPS = 60,
    PASTE_HOLD_CYCLES        =  39400,  /* ~40ms  at PAL 985248 Hz */
    PASTE_GAP_CYCLES         =  19704,  /* ~20ms  at PAL 985248 Hz — must exceed one KERNAL scan period */
    PASTE_RETURN_GAP_CYCLES  = 246312,  /* ~250ms at PAL 985248 Hz */
    /* Step Out fast-loop instruction budget before falling back to free-
       running mode.  Covers typical KERNAL/BASIC subroutine depths while
       ensuring the UI does not appear frozen when the outer routine never
       returns (e.g. a JSR/JMP spin loop). */
    STEP_OUT_FAST_LIMIT      = 10000,

    /* Step Over fast-loop budget: must cover complex KERNAL subroutines while
       still providing a safety fallback if the callee never returns to stop_pc.
       Logged periodically so the terminal shows progress. */
    STEP_OVER_FAST_LIMIT     = 500000,
    STEP_OVER_LOG_INTERVAL   = 10000,
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

static void runtime_update_sid_sample_output(runtime *rt) {
    bool enabled;

    enabled = (rt->audio_out != NULL || rt->audio_record_path != NULL) &&
        rt->audio_sample_rate > 0 &&
        rt->speed_mode != RUNTIME_SPEED_MODE_FAST &&
        rt->audio_smoke == 0;
    c64_set_audio_output_enabled(&rt->machine, enabled);
}

static void runtime_audio_record_write_u16(FILE *file, uint16_t value) {
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8) & 0xffu), file);
}

static void runtime_audio_record_write_u32(FILE *file, uint32_t value) {
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8) & 0xffu), file);
    fputc((int)((value >> 16) & 0xffu), file);
    fputc((int)((value >> 24) & 0xffu), file);
}

static bool runtime_audio_record_write_header(runtime *rt, uint32_t sample_count) {
    FILE *file;
    uint32_t data_bytes;
    uint32_t riff_size;
    uint32_t byte_rate;
    uint16_t block_align;

    if (rt == NULL || rt->audio_record_file == NULL || rt->audio_sample_rate <= 0) {
        return false;
    }

    file = rt->audio_record_file;
    data_bytes = sample_count * 2u;
    riff_size = 36u + data_bytes;
    block_align = 2u;
    byte_rate = (uint32_t)rt->audio_sample_rate * (uint32_t)block_align;

    if (fseek(file, 0, SEEK_SET) != 0) {
        return false;
    }

    fwrite("RIFF", 1, 4, file);
    runtime_audio_record_write_u32(file, riff_size);
    fwrite("WAVE", 1, 4, file);
    fwrite("fmt ", 1, 4, file);
    runtime_audio_record_write_u32(file, 16u);
    runtime_audio_record_write_u16(file, 1u); /* PCM */
    runtime_audio_record_write_u16(file, 1u); /* mono */
    runtime_audio_record_write_u32(file, (uint32_t)rt->audio_sample_rate);
    runtime_audio_record_write_u32(file, byte_rate);
    runtime_audio_record_write_u16(file, block_align);
    runtime_audio_record_write_u16(file, 16u);
    fwrite("data", 1, 4, file);
    runtime_audio_record_write_u32(file, data_bytes);

    return ferror(file) == 0;
}

static void runtime_audio_record_finish(runtime *rt) {
    uint32_t sample_count;

    if (rt == NULL || rt->audio_record_file == NULL) {
        return;
    }

    sample_count = rt->audio_record_written_samples > 0xffffffffu ?
        0xffffffffu : (uint32_t)rt->audio_record_written_samples;
    if (!runtime_audio_record_write_header(rt, sample_count)) {
        rt->audio_record_failed = true;
    }
    fclose(rt->audio_record_file);
    rt->audio_record_file = NULL;
    rt->audio_record_finished = true;
}

static void runtime_audio_record_init(runtime *rt) {
    double start_seconds;
    double duration_seconds;

    if (rt == NULL || rt->audio_record_path == NULL || rt->audio_record_path[0] == '\0') {
        return;
    }
    if (rt->audio_sample_rate <= 0) {
        rt->audio_record_failed = true;
        return;
    }

    start_seconds = rt->audio_record_start_seconds > 0.0 ? rt->audio_record_start_seconds : 0.0;
    duration_seconds = rt->audio_record_duration_seconds > 0.0 ? rt->audio_record_duration_seconds : 0.0;
    rt->audio_record_seen_samples = 0;
    rt->audio_record_written_samples = 0;
    rt->audio_record_target_samples = duration_seconds > 0.0 ?
        (uint64_t)(duration_seconds * (double)rt->audio_sample_rate + 0.5) : 0;
    rt->audio_record_failed = false;
    rt->audio_record_finished = false;

    rt->audio_record_file = fopen(rt->audio_record_path, "wb+");
    if (rt->audio_record_file == NULL) {
        rt->audio_record_failed = true;
        return;
    }

    if (!runtime_audio_record_write_header(rt, 0u) ||
        fseek(rt->audio_record_file, 44, SEEK_SET) != 0) {
        runtime_audio_record_finish(rt);
        rt->audio_record_failed = true;
        return;
    }

    rt->audio_record_seen_samples = (uint64_t)(start_seconds * (double)rt->audio_sample_rate + 0.5);
}

static void runtime_audio_record_sample(runtime *rt, float sample) {
    int value;

    if (rt == NULL ||
        rt->audio_record_file == NULL ||
        rt->audio_record_failed ||
        rt->audio_record_finished) {
        return;
    }

    if (rt->audio_record_seen_samples > 0) {
        rt->audio_record_seen_samples--;
        return;
    }
    if (rt->audio_record_target_samples > 0 &&
        rt->audio_record_written_samples >= rt->audio_record_target_samples) {
        runtime_audio_record_finish(rt);
        return;
    }

    if (sample > 1.0f) {
        sample = 1.0f;
    } else if (sample < -1.0f) {
        sample = -1.0f;
    }
    value = sample < 0.0f ?
        (int)(sample * 32768.0f) :
        (int)(sample * 32767.0f);
    if (value < -32768) {
        value = -32768;
    } else if (value > 32767) {
        value = 32767;
    }
    runtime_audio_record_write_u16(rt->audio_record_file, (uint16_t)(int16_t)value);
    rt->audio_record_written_samples++;
    if (ferror(rt->audio_record_file) != 0) {
        rt->audio_record_failed = true;
        runtime_audio_record_finish(rt);
    } else if (rt->audio_record_target_samples > 0 &&
        rt->audio_record_written_samples >= rt->audio_record_target_samples) {
        runtime_audio_record_finish(rt);
    }
}

static void runtime_audio_emit_sample(runtime *rt, float sample) {
    if (rt->audio_out != NULL) {
        audio_buffer_write(rt->audio_out, &sample, 1);
    }
    runtime_audio_record_sample(rt, sample);
}

static float runtime_audio_next_smoke_sample(runtime *rt, uint32_t sample_rate) {
    rt->audio_smoke_phase += 440.0f / (float)sample_rate;
    if (rt->audio_smoke_phase >= 1.0f) {
        rt->audio_smoke_phase -= 1.0f;
    }
    return rt->audio_smoke_phase < 0.5f ? 0.2f : -0.2f;
}

/* Advance the runtime audio sample scheduler by one completed C64 cycle.
   Host samples are emitted at their cycle deadlines so each SID sample observes
   fresh machine state instead of the final state of an entire run batch. */
static void runtime_audio_advance_cycle(runtime *rt) {
    uint32_t clock_hz, sample_rate;
    float sample;

    if ((rt->audio_out == NULL && rt->audio_record_path == NULL) ||
        rt->audio_sample_rate <= 0) {
        return;
    }

    if (rt->speed_mode == RUNTIME_SPEED_MODE_FAST) {
        /* Turbo: mute and discard pending timing so normal speed resumes cleanly. */
        rt->audio_cycle_accum = 0.0;
        rt->audio_sample_accum = 0.0;
        rt->audio_sample_count = 0;
        return;
    }

    clock_hz    = c64_config_clock_hz(&rt->machine_config);
    sample_rate = (uint32_t)rt->audio_sample_rate;

    if (!rt->audio_smoke) {
        rt->audio_sample_accum += (double)sid_sample(&rt->machine.sid);
        rt->audio_sample_count++;
    }

    rt->audio_cycle_accum += (double)sample_rate;

    while (rt->audio_cycle_accum >= (double)clock_hz) {
        rt->audio_cycle_accum -= (double)clock_hz;

        if (rt->audio_smoke) {
            sample = runtime_audio_next_smoke_sample(rt, sample_rate);
        } else if (rt->audio_sample_count > 0u) {
            sample = (float)(rt->audio_sample_accum / (double)rt->audio_sample_count);
            rt->audio_sample_accum = 0.0;
            rt->audio_sample_count = 0;
        } else {
            sample = sid_sample(&rt->machine.sid);
        }

        runtime_audio_emit_sample(rt, sample);
    }
}

static void runtime_audio_reset(runtime *rt) {
    if (rt->audio_out != NULL) {
        audio_buffer_reset(rt->audio_out);
    }
    rt->audio_cycle_accum = 0.0;
    rt->audio_sample_accum = 0.0;
    rt->audio_sample_count = 0;
    rt->audio_smoke_phase = 0.0f;
    if (rt->audio_record_file != NULL) {
        runtime_audio_record_finish(rt);
    }
    runtime_audio_record_init(rt);
    runtime_update_sid_sample_output(rt);
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

static void runtime_publish_symbols(runtime *rt);

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
    c64_memory_banking_snapshot banking;
    c64_vicii_hardware_snapshot vicii_hardware;
    c64_cia_hardware_snapshot cia1_hardware;
    c64_cia_hardware_snapshot cia2_hardware;
    c64_sid_hardware_snapshot sid_hardware;

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
    event.data.machine_state.sid_register_writes = snapshot.sid_register_writes;
    event.data.machine_state.keyboard_events = snapshot.keyboard_events;
    event.data.machine_state.irq_entries = snapshot.irq_entries;
    event.data.machine_state.cia1_icr_reads = snapshot.cia1_icr_reads;
    event.data.machine_state.cia1_icr_writes = snapshot.cia1_icr_writes;
    event.data.machine_state.cia1_interrupt_assertions = snapshot.cia1_interrupt_assertions;
    event.data.machine_state.nmi_entries = snapshot.nmi_entries;
    event.data.machine_state.restore_requests = snapshot.restore_requests;
    event.data.machine_state.active_turbo_multiplier = rt->active_turbo_multiplier;
    event.data.machine_state.turbo_speed_count = rt->turbo_speed_count;
    event.data.machine_state.cia1_irq_pending = snapshot.cia1_irq_pending ? 1 : 0;
    event.data.machine_state.cia2_nmi_pending = snapshot.cia2_nmi_pending ? 1 : 0;

    c64_copy_memory_banking_snapshot(&rt->machine, &banking);
    event.data.machine_state.memory_banking.cpu_port_direction = banking.cpu_port_direction;
    event.data.machine_state.memory_banking.cpu_port_data = banking.cpu_port_data;
    event.data.machine_state.memory_banking.loram = banking.loram ? 1 : 0;
    event.data.machine_state.memory_banking.hiram = banking.hiram ? 1 : 0;
    event.data.machine_state.memory_banking.charen = banking.charen ? 1 : 0;
    event.data.machine_state.memory_banking.basic_visibility = banking.basic_visibility;
    event.data.machine_state.memory_banking.io_visibility = banking.io_visibility;
    event.data.machine_state.memory_banking.kernal_visibility = banking.kernal_visibility;
    event.data.machine_state.memory_banking.cia2_port_a_pins = banking.cia2_port_a_pins;
    event.data.machine_state.memory_banking.vic_bank_select = banking.vic_bank_select;
    event.data.machine_state.memory_banking.vic_bank_base = banking.vic_bank_base;
    event.data.machine_state.memory_banking.vic_memory_pointer = banking.vic_memory_pointer;
    event.data.machine_state.memory_banking.vic_screen_base = banking.vic_screen_base;
    event.data.machine_state.memory_banking.vic_character_base = banking.vic_character_base;
    event.data.machine_state.memory_banking.vic_bitmap_base = banking.vic_bitmap_base;

    c64_copy_vicii_hardware_snapshot(&rt->machine, &vicii_hardware);
    c64_copy_cia_hardware_snapshot(&rt->machine, 1, &cia1_hardware);
    c64_copy_cia_hardware_snapshot(&rt->machine, 2, &cia2_hardware);
    c64_copy_sid_hardware_snapshot(&rt->machine, &sid_hardware);
    event.data.machine_state.vicii_hardware = vicii_hardware;
    event.data.machine_state.cia1_hardware = cia1_hardware;
    event.data.machine_state.cia2_hardware = cia2_hardware;
    event.data.machine_state.sid_hardware = sid_hardware;

    runtime_publish_event(rt, &event);
}

static bool runtime_memory_mode_is_valid(uint8_t mode) {
    return mode == RUNTIME_MEMORY_MODE_CPU_MAP ||
           mode == RUNTIME_MEMORY_MODE_RAM ||
           mode == RUNTIME_MEMORY_MODE_ROM;
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
        if (mode == RUNTIME_MEMORY_MODE_RAM) {
            event.data.memory.bytes[i] = c64_debug_read_ram(&rt->machine, current);
        } else if (mode == RUNTIME_MEMORY_MODE_ROM) {
            event.data.memory.bytes[i] = c64_debug_read_rom(&rt->machine, current);
        } else {
            event.data.memory.bytes[i] = c64_debug_read_cpu_map(&rt->machine, current);
        }
        event.data.memory.write_history[i] = c64_debug_read_write_history(&rt->machine, current);
    }

    runtime_publish_event(rt, &event);
}

static void runtime_publish_debug_memory(runtime *rt, bool include_write_history) {
    runtime_event event = {
        .type = RUNTIME_EVENT_DEBUG_MEMORY_READY,
    };
    uint32_t address;
    uint64_t generation;

    mutex_lock(rt->debug_memory_slot.mutex);
    generation = ++rt->debug_memory_slot.generation;
    rt->debug_memory_slot.snapshot.generation = generation;
    rt->debug_memory_slot.snapshot.has_write_history = include_write_history ? 1u : 0u;
    for (address = 0; address < C64_RAM_SIZE; ++address) {
        uint16_t a = (uint16_t)address;
        rt->debug_memory_slot.snapshot.map[address] = c64_debug_read_cpu_map(&rt->machine, a);
        rt->debug_memory_slot.snapshot.ram[address] = c64_debug_read_ram(&rt->machine, a);
        rt->debug_memory_slot.snapshot.rom[address] = c64_debug_read_rom(&rt->machine, a);
        rt->debug_memory_slot.snapshot.write_history[address] = include_write_history ?
            c64_debug_read_write_history(&rt->machine, a) : 0u;
    }
    rt->debug_memory_slot.has_snapshot = true;
    mutex_unlock(rt->debug_memory_slot.mutex);

    event.data.debug_memory_ready.generation = generation;
    event.data.debug_memory_ready.has_write_history = include_write_history ? 1u : 0u;
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
        event.data.breakpoints.entries[i].swap_param = rt->breakpoints[i].swap_param;
        event.data.breakpoints.entries[i].swap_relative = rt->breakpoints[i].swap_relative;
        snprintf(
            event.data.breakpoints.entries[i].tron_path,
            sizeof(event.data.breakpoints.entries[i].tron_path),
            "%s",
            rt->breakpoints[i].tron_path);
        snprintf(
            event.data.breakpoints.entries[i].type_text,
            sizeof(event.data.breakpoints.entries[i].type_text),
            "%s",
            rt->breakpoints[i].type_text);
    }

    runtime_publish_event(rt, &event);
}

static void runtime_publish_drive_status(runtime *rt, uint8_t device) {
    runtime_event event = {
        .type = RUNTIME_EVENT_DISK_STATUS_RESPONSE,
    };
    c64_drive_status status;

    c64_copy_drive_status(&rt->machine, device, &status);
    event.data.disk_status.device = status.device;
    event.data.disk_status.mounted = status.mounted ? 1u : 0u;
    event.data.disk_status.image_kind = status.image_kind;
    event.data.disk_status.last_result = status.last_result;
    snprintf(event.data.disk_status.display_name, sizeof(event.data.disk_status.display_name), "%s", status.display_name);
    snprintf(event.data.disk_status.disk_title, sizeof(event.data.disk_status.disk_title), "%s", status.disk_title);

    runtime_publish_event(rt, &event);
}

static c64_drive_status_result runtime_drive_status_from_d64_result(d64_result result) {
    switch (result) {
    case D64_OK:
        return C64_DRIVE_STATUS_OK;
    case D64_OUT_OF_MEMORY:
        return C64_DRIVE_STATUS_OUT_OF_MEMORY;
    case D64_UNSUPPORTED_IMAGE:
        return C64_DRIVE_STATUS_UNSUPPORTED_IMAGE;
    default:
        return C64_DRIVE_STATUS_PARSE_ERROR;
    }
}

static const char *runtime_basename(const char *path) {
    const char *slash;
    const char *backslash;

    if (path == NULL) {
        return "";
    }

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (slash == NULL || (backslash != NULL && backslash > slash)) {
        slash = backslash;
    }

    return slash == NULL ? path : slash + 1;
}

static bool runtime_read_file_bytes(const char *path, uint8_t **out_bytes, size_t *out_size) {
    FILE *file;
    long size;
    uint8_t *bytes;

    if (path == NULL || out_bytes == NULL || out_size == NULL) {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return false;
    }
    fclose(file);

    *out_bytes = bytes;
    *out_size = (size_t)size;
    return true;
}

static void runtime_mount_d64(runtime *rt, const runtime_command *command) {
    uint8_t *bytes = NULL;
    size_t size = 0;
    size_t entry_count = 0;
    size_t i;
    d64_result result;
    d64_image *image;
    c64_drive_directory_entry *entries = NULL;
    c64_drive_status_result status_result;
    const d64_disk_info *info;
    d64_directory_entry title_entry;
    d64_directory_entry d64_entry;
    char disk_title[C64_DRIVE_DISK_TITLE_MAX];
    char disk_id[3];
    char dos_type[3];
    uint16_t free_blocks = 0;

    if (!runtime_read_file_bytes(command->data.mount_d64.path, &bytes, &size)) {
        int slot_index = (int)(command->data.mount_d64.device - C64_DRIVE_MIN_DEVICE);
        if (slot_index >= 0 && slot_index < C64_DRIVE_SLOT_COUNT) {
            rt->machine.drives[slot_index].last_result = C64_DRIVE_STATUS_IO_ERROR;
        }
        runtime_publish_drive_status(rt, command->data.mount_d64.device);
        return;
    }

    image = d64_image_create(bytes, size, &result);
    if (image == NULL) {
        int slot_index = (int)(command->data.mount_d64.device - C64_DRIVE_MIN_DEVICE);
        status_result = runtime_drive_status_from_d64_result(result);
        if (slot_index >= 0 && slot_index < C64_DRIVE_SLOT_COUNT) {
            rt->machine.drives[slot_index].last_result = status_result;
        }
        free(bytes);
        runtime_publish_drive_status(rt, command->data.mount_d64.device);
        return;
    }

    disk_title[0] = '\0';
    disk_id[0] = '\0';
    dos_type[0] = '\0';
    info = d64_image_disk_info(image);
    if (info != NULL) {
        memset(&title_entry, 0, sizeof(title_entry));
        memcpy(title_entry.filename, info->title, D64_DIRECTORY_NAME_SIZE);
        title_entry.filename_length = info->title_length;
        (void)d64_entry_name_ascii(&title_entry, disk_title, sizeof(disk_title));
        disk_id[0] = (char)info->disk_id[0];
        disk_id[1] = (char)info->disk_id[1];
        disk_id[2] = '\0';
        dos_type[0] = (char)info->dos_type[0];
        dos_type[1] = (char)info->dos_type[1];
        dos_type[2] = '\0';
        free_blocks = info->free_blocks;
    }

    entry_count = d64_image_directory_count(image);
    if (entry_count > 0) {
        entries = (c64_drive_directory_entry *)calloc(entry_count, sizeof(*entries));
        if (entries == NULL) {
            int slot_index = (int)(command->data.mount_d64.device - C64_DRIVE_MIN_DEVICE);
            if (slot_index >= 0 && slot_index < C64_DRIVE_SLOT_COUNT) {
                rt->machine.drives[slot_index].last_result = C64_DRIVE_STATUS_OUT_OF_MEMORY;
            }
            d64_image_destroy(image);
            free(bytes);
            runtime_publish_drive_status(rt, command->data.mount_d64.device);
            return;
        }
        for (i = 0; i < entry_count; ++i) {
            if (d64_image_directory_entry(image, i, &d64_entry) != D64_OK) {
                entry_count = i;
                break;
            }
            entries[i].raw_type = d64_entry.raw_type;
            entries[i].type = (c64_drive_file_type)d64_entry.type;
            entries[i].first_track = d64_entry.first_track;
            entries[i].first_sector = d64_entry.first_sector;
            memcpy(entries[i].filename, d64_entry.filename, sizeof(entries[i].filename));
            entries[i].filename_length = d64_entry.filename_length;
            entries[i].block_count = d64_entry.block_count;
        }
    }

    status_result = c64_mount_d64(
        &rt->machine,
        command->data.mount_d64.device,
        bytes,
        D64_STANDARD_IMAGE_SIZE,
        entries,
        entry_count,
        runtime_basename(command->data.mount_d64.path),
        disk_title,
        disk_id,
        dos_type,
        free_blocks);
    if (status_result != C64_DRIVE_STATUS_OK) {
        int slot_index = (int)(command->data.mount_d64.device - C64_DRIVE_MIN_DEVICE);
        if (slot_index >= 0 && slot_index < C64_DRIVE_SLOT_COUNT) {
            rt->machine.drives[slot_index].last_result = status_result;
        }
    }

    d64_image_destroy(image);
    free(entries);
    free(bytes);
    runtime_publish_drive_status(rt, command->data.mount_d64.device);

    if (rt->autorun && command->data.mount_d64.device == C64_DRIVE_MIN_DEVICE) {
        rt->autorun_d64_phase = 1;
    }
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
    if (!c64_make_frame_snapshot(&rt->machine, &rt->publish_frame)) {
        runtime_publish_error(rt, "failed to generate frame");
        return false;
    }

    return runtime_publish_frame_copy(rt, &rt->publish_frame);
}

static bool runtime_publish_completed_frame(runtime *rt) {
    if (!c64_copy_completed_frame(&rt->machine, &rt->publish_frame)) {
        runtime_publish_error(rt, "no completed live frame available");
        return false;
    }

    return runtime_publish_frame_copy(rt, &rt->publish_frame);
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

    /* 1541 ROM is optional — missing it falls back to the KERNAL LOAD trap. */
    if (rt->rom1541_path != NULL) {
        c1541_load_rom(&rt->machine.drive8, rt->rom1541_path);
        c1541_load_rom(&rt->machine.drive9, rt->rom1541_path);
    }

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

    runtime_audio_reset(rt);

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

static void runtime_reset_command(runtime *rt) {
    bool was_running = rt->exec_state == RUNTIME_EXEC_RUNNING;

    if (!runtime_reset_machine(rt)) {
        return;
    }

    free(rt->pending_prg_path);
    rt->pending_prg_path = NULL;
    rt->pending_prg_resume_running = false;
    free(rt->pending_asm_path);
    rt->pending_asm_path = NULL;
    free(rt->pending_bin_path);
    rt->pending_bin_path = NULL;

    if (was_running) {
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
    }
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

static bool runtime_string_equal(const char *a, const char *b) {
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b) == 0;
}

static void runtime_load_symbol_files(runtime *rt) {
    const char *cursor;

    if (rt == NULL || rt->symbols == NULL) {
        return;
    }

    symbol_table_remove_kind(rt->symbols, SYMBOL_SOURCE_FILE);
    cursor = rt->symbol_files != NULL ? rt->symbol_files : "";
    while (*cursor != '\0') {
        const char *start;
        const char *end;
        char path[RUNTIME_COMMAND_PATH_MAX];
        size_t length;
        size_t loaded = 0;
        symbol_result result;

        while (*cursor == ',' || isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }

        length = (size_t)(end - start);
        if (length == 0) {
            continue;
        }
        if (length >= sizeof(path)) {
            length = sizeof(path) - 1u;
        }
        memcpy(path, start, length);
        path[length] = '\0';

        result = symbol_table_load_file(rt->symbols, path, path, &loaded);
        (void)loaded;
        if (result == SYMBOL_OUT_OF_MEMORY) {
            runtime_publish_error(rt, "out of memory while loading symbol file");
            break;
        }
        if (result != SYMBOL_OK) {
            char message[1152];
            snprintf(message, sizeof(message), "failed to load symbol file: %s", path);
            runtime_publish_error(rt, message);
        }
    }

    runtime_publish_symbols(rt);
}

static void runtime_apply_machine_config(runtime *rt, const runtime_command *command) {
    bool symbols_changed;

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
    symbols_changed = !runtime_string_equal(rt->symbol_files, command->data.apply_machine_config.symbol_files);
    if (!runtime_replace_string(&rt->symbol_files, command->data.apply_machine_config.symbol_files)) {
        runtime_publish_error(rt, "failed to update symbol file list");
        return;
    }
    if (symbols_changed) {
        runtime_load_symbol_files(rt);
    }
    c64_set_config(&rt->machine, &rt->machine_config);
    runtime_update_sid_sample_output(rt);
    if (command->data.apply_machine_config.reset != 0) {
        runtime_reset_machine(rt);
    } else {
        runtime_publish_machine_state(rt);
    }
}

static void runtime_cycle_turbo_speed(runtime *rt) {
    uint8_t i;
    uint8_t next_index = 0;

    if (rt == NULL || rt->turbo_speed_count == 0) {
        return;
    }

    for (i = 0; i < rt->turbo_speed_count; ++i) {
        if (rt->turbo_speeds[i] == rt->active_turbo_multiplier) {
            next_index = (uint8_t)((i + 1u) % rt->turbo_speed_count);
            break;
        }
    }

    rt->active_turbo_multiplier = rt->turbo_speeds[next_index];
    rt->pace_initialized = false;
    runtime_publish_machine_state(rt);
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
    breakpoint->swap_param = definition->swap_param;
    breakpoint->swap_relative = definition->swap_relative;
    snprintf(breakpoint->tron_path, sizeof(breakpoint->tron_path), "%s", definition->tron_path);
    snprintf(breakpoint->type_text, sizeof(breakpoint->type_text), "%s", definition->type_text);
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

static bool runtime_breakpoint_record_match(runtime *rt, runtime_breakpoint *breakpoint) {
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

    if (breakpoint->reset_count == 0) {
        breakpoint->enabled = false;
        runtime_publish_breakpoints(rt);
        return true;
    }

    breakpoint->counter = breakpoint->reset_count;
    return true;
}

static void runtime_write_trace_line(runtime *rt) {
    c64_cpu_instruction_trace trace;
    c64_cpu_snapshot snapshot;
    symbol_resolver resolver;
    disasm_6502_line line;
    uint8_t bytes[3];
    char bytes_str[9];
    char flags[9];
    uint8_t p;

    if (rt->trace_file == NULL) {
        return;
    }

    c64_debug_copy_last_cpu_trace(&rt->machine, &trace);
    c64_copy_cpu_snapshot(&rt->machine, &snapshot);

    bytes[0] = c64_debug_read_cpu_map(&rt->machine, trace.opcode_pc);
    bytes[1] = c64_debug_read_cpu_map(&rt->machine, trace.opcode_pc + 1);
    bytes[2] = c64_debug_read_cpu_map(&rt->machine, trace.opcode_pc + 2);

    symbol_table_make_resolver(rt->symbols, &resolver);
    line = disasm_6502_decode_line(trace.opcode_pc, bytes, 3, &resolver);

    switch (line.length) {
        case 1:  snprintf(bytes_str, sizeof(bytes_str), "%02X      ", bytes[0]); break;
        case 2:  snprintf(bytes_str, sizeof(bytes_str), "%02X %02X   ", bytes[0], bytes[1]); break;
        default: snprintf(bytes_str, sizeof(bytes_str), "%02X %02X %02X", bytes[0], bytes[1], bytes[2]); break;
    }

    p = snapshot.p;
    flags[0] = (p & 0x80) ? 'N' : 'n';
    flags[1] = (p & 0x40) ? 'V' : 'v';
    flags[2] = '-';
    flags[3] = (p & 0x10) ? 'B' : 'b';
    flags[4] = (p & 0x08) ? 'D' : 'd';
    flags[5] = (p & 0x04) ? 'I' : 'i';
    flags[6] = (p & 0x02) ? 'Z' : 'z';
    flags[7] = (p & 0x01) ? 'C' : 'c';
    flags[8] = '\0';

    fprintf(rt->trace_file,
        "%04X  %s  %-12s  A=%02X X=%02X Y=%02X SP=%02X  %s  CYC=%08llX\n",
        trace.opcode_pc,
        bytes_str,
        line.text,
        snapshot.a, snapshot.x, snapshot.y, snapshot.sp,
        flags,
        (unsigned long long)rt->machine.clock.cycle);
}

static bool runtime_execute_breakpoint_actions(runtime *rt, const runtime_breakpoint *breakpoint) {
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
        return true;
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_FAST) != 0) {
        rt->speed_mode = RUNTIME_SPEED_MODE_FAST;
        rt->pace_initialized = false;
        runtime_update_sid_sample_output(rt);
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0) {
        rt->speed_mode = RUNTIME_SPEED_MODE_SLOW;
        rt->pace_initialized = false;
        runtime_update_sid_sample_output(rt);
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TRON) != 0) {
        rt->trace_enabled = true;
        c64_set_cpu_trace_enabled(&rt->machine, true);
        if (rt->trace_file == NULL) {
            const char *path = (breakpoint->tron_path[0] != '\0') ?
                breakpoint->tron_path : "trace.log";
            rt->trace_file = fopen(path, "a");
            if (rt->trace_file != NULL) {
                fprintf(rt->trace_file, "--- TRON  CYC=%08llX ---\n",
                    (unsigned long long)rt->machine.clock.cycle);
            }
        }
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0) {
        rt->trace_enabled = false;
        c64_set_cpu_trace_enabled(&rt->machine, false);
        if (rt->trace_file != NULL) {
            fprintf(rt->trace_file, "--- TROFF CYC=%08llX ---\n",
                (unsigned long long)rt->machine.clock.cycle);
            fclose(rt->trace_file);
            rt->trace_file = NULL;
        }
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0 &&
        breakpoint->swap_param != 0) {
        runtime_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = RUNTIME_EVENT_DISK_SWAP;
        ev.data.disk_swap.swap_param = breakpoint->swap_param;
        ev.data.disk_swap.swap_relative = breakpoint->swap_relative;
        ev.data.disk_swap.device = 8;
        runtime_publish_event(rt, &ev);
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TYPE) != 0 &&
        breakpoint->type_text[0] != '\0') {
        paste_state *p = &rt->paste;
        paste_event_t events[PASTE_EVENTS_MAX];
        size_t count = 0;
        paste_parse_error_t perr = { -1, NULL };

        if (paste_parse(breakpoint->type_text, events, PASTE_EVENTS_MAX, &count, &perr) && count > 0) {
            memcpy(p->events, events, count * sizeof(paste_event_t));
            p->event_count     = count;
            p->event_cursor    = 0;
            p->event_mode      = true;
            p->use_buffer      = false;
            p->in_gap          = true;
            p->phase_end_cycle = rt->machine.clock.cycle;
            memset(p->asserted_keys, 0, sizeof(p->asserted_keys));
            memset(p->oneshot_keys,  0, sizeof(p->oneshot_keys));
            memset(p->temp_keys,     0, sizeof(p->temp_keys));
            memset(p->asserted_joy,  0, sizeof(p->asserted_joy));
            rt->paste_active   = true;
        }
    }

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
            runtime_breakpoint_record_match(rt, breakpoint)) {
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
    rt->suppress_execute_bp = true;
    rt->temp_bp_active = false;
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

static bool runtime_brk_pending(runtime *rt) {
    if (rt->suppress_execute_bp) {
        return false;
    }
    return (uint8_t)c64_debug_read_cpu_map(&rt->machine, rt->machine.cpu.cpu.pc) == 0x00u;
}

static void runtime_pause_for_brk(runtime *rt) {
    rt->breakpoint_hit_pending = false;
    rt->suppress_execute_bp = true;
    rt->temp_bp_active = false;
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_BRK;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
    runtime_publish_machine_state(rt);
}

static bool runtime_step_cycle(runtime *rt) {
    char error[256];

    if (!c64_step_cycle(&rt->machine, error, sizeof(error))) {
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        runtime_publish_error(rt, error);
        return false;
    }

    runtime_audio_advance_cycle(rt);
    return true;
}

static bool runtime_step_cycle_command(runtime *rt) {
    rt->suppress_execute_bp = false;

    if (!runtime_step_cycle(rt)) {
        return false;
    }

    rt->breakpoint_hit_pending = false;
    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
    runtime_publish_machine_state(rt);
    return true;
}

static bool runtime_step_instruction(runtime *rt) {
    char error[256];
    c64_cpu_snapshot snapshot;

    rt->suppress_execute_bp = false;

    if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
        runtime_publish_error(rt, error);
        return false;
    }

    rt->breakpoint_hit_pending = false;
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
        if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return true;
        }
        if (runtime_brk_pending(rt)) {
            runtime_pause_for_brk(rt);
            return true;
        }
        rt->suppress_execute_bp = false;

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
        if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return true;
        }
        if (runtime_brk_pending(rt)) {
            runtime_pause_for_brk(rt);
            return true;
        }

        if (!runtime_step_cycle(rt)) {
            return false;
        }

        if (rt->suppress_execute_bp && c64_consume_instruction_complete(&rt->machine)) {
            rt->suppress_execute_bp = false;
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

static void runtime_rearm_oneshot_breakpoints(runtime *rt) {
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        runtime_breakpoint *bp = &rt->breakpoints[i];

        if (bp->use_counter && bp->reset_count == 0 && !bp->enabled) {
            bp->enabled = true;
            bp->counter = bp->initial_count;
        }
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

static bool runtime_path_has_extension(const char *path, const char *extension) {
    const char *dot;

    if (path == NULL || extension == NULL) {
        return false;
    }

    dot = strrchr(path, '.');
    return dot != NULL && SDL_strcasecmp(dot + 1, extension) == 0;
}

static bool runtime_read_host_file(
    runtime *rt,
    const char *path,
    const char *label,
    uint8_t **out_bytes,
    size_t *out_length)
{
    FILE *file;
    long file_size;
    uint8_t *bytes;
    char message[128];

    if (out_bytes == NULL || out_length == NULL) {
        runtime_publish_error(rt, "invalid host file read request");
        return false;
    }

    *out_bytes = NULL;
    *out_length = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        snprintf(message, sizeof(message), "failed to open %s file", label);
        runtime_publish_error(rt, message);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(message, sizeof(message), "failed to inspect %s file", label);
        runtime_publish_error(rt, message);
        return false;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        snprintf(message, sizeof(message), "failed to inspect %s file", label);
        runtime_publish_error(rt, message);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        snprintf(message, sizeof(message), "failed to seek %s file", label);
        runtime_publish_error(rt, message);
        return false;
    }

    bytes = malloc((size_t)file_size);
    if (bytes == NULL && file_size > 0) {
        fclose(file);
        snprintf(message, sizeof(message), "failed to allocate %s buffer", label);
        runtime_publish_error(rt, message);
        return false;
    }

    if (file_size > 0 && fread(bytes, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(bytes);
        fclose(file);
        snprintf(message, sizeof(message), "failed to read %s file", label);
        runtime_publish_error(rt, message);
        return false;
    }

    fclose(file);
    *out_bytes = bytes;
    *out_length = (size_t)file_size;
    return true;
}

static bool runtime_inject_prg_image(runtime *rt, const uint8_t *bytes, size_t length) {
    size_t payload_length;
    uint16_t load_address;
    size_t i;

    if (bytes == NULL || length < 2 || length > 65538u) {
        runtime_publish_error(rt, "invalid PRG file");
        return false;
    }

    load_address = (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
    payload_length = length - 2u;
    if ((uint32_t)load_address + payload_length > 0x10000u) {
        runtime_publish_error(rt, "PRG load range overflows address space");
        return false;
    }

    for (i = 0; i < payload_length; ++i) {
        c64_debug_write_ram(&rt->machine, (uint16_t)(load_address + i), bytes[i + 2u]);
    }

    runtime_publish_memory(
        rt,
        load_address,
        payload_length > RUNTIME_MEMORY_SNAPSHOT_MAX ? RUNTIME_MEMORY_SNAPSHOT_MAX : (uint16_t)payload_length,
        RUNTIME_MEMORY_MODE_RAM);
    return true;
}

static bool runtime_copy_generic_crt_roms(
    runtime *rt,
    const crt_image *image,
    uint8_t *roml,
    uint8_t *romh,
    bool *out_has_roml,
    bool *out_has_romh)
{
    size_t i;
    size_t chip_count;

    memset(roml, 0, C64_CARTRIDGE_ROM_BANK_SIZE);
    memset(romh, 0, C64_CARTRIDGE_ROM_BANK_SIZE);
    *out_has_roml = false;
    *out_has_romh = false;

    chip_count = crt_image_chip_count(image);
    for (i = 0; i < chip_count; ++i) {
        crt_chip chip;

        if (crt_image_chip(image, i, &chip) != CRT_OK ||
            chip.type != CRT_CHIP_TYPE_ROM ||
            chip.bank != 0) {
            runtime_publish_error(rt, "unsupported CRT CHIP layout");
            return false;
        }

        if (chip.load_address == 0x8000u && chip.rom_size == 0x2000u) {
            if (*out_has_roml) {
                runtime_publish_error(rt, "duplicate CRT ROML chip");
                return false;
            }
            memcpy(roml, chip.bytes, C64_CARTRIDGE_ROM_BANK_SIZE);
            *out_has_roml = true;
        } else if (chip.load_address == 0x8000u && chip.rom_size == 0x4000u) {
            if (*out_has_roml || *out_has_romh) {
                runtime_publish_error(rt, "duplicate CRT ROM chip");
                return false;
            }
            memcpy(roml, chip.bytes, C64_CARTRIDGE_ROM_BANK_SIZE);
            memcpy(romh, chip.bytes + C64_CARTRIDGE_ROM_BANK_SIZE, C64_CARTRIDGE_ROM_BANK_SIZE);
            *out_has_roml = true;
            *out_has_romh = true;
        } else if (chip.load_address == 0xa000u && chip.rom_size == 0x2000u) {
            if (*out_has_romh) {
                runtime_publish_error(rt, "duplicate CRT ROMH chip");
                return false;
            }
            memcpy(romh, chip.bytes, C64_CARTRIDGE_ROM_BANK_SIZE);
            *out_has_romh = true;
        } else {
            runtime_publish_error(rt, "unsupported CRT CHIP address or size");
            return false;
        }
    }

    return true;
}

static void runtime_load_crt(runtime *rt, const runtime_command *command) {
    uint8_t *bytes;
    size_t length;
    crt_result result;
    crt_image *image;
    const crt_header *header;
    uint8_t roml[C64_CARTRIDGE_ROM_BANK_SIZE];
    uint8_t romh[C64_CARTRIDGE_ROM_BANK_SIZE];
    bool has_roml = false;
    bool has_romh = false;
    char error[256];

    if (!runtime_read_host_file(rt, command->data.load_crt.path, "CRT", &bytes, &length)) {
        return;
    }

    image = crt_image_create(bytes, length, &result);
    free(bytes);
    if (image == NULL) {
        char message[128];
        snprintf(message, sizeof(message), "failed to parse CRT: %s", crt_result_string(result));
        runtime_publish_error(rt, message);
        return;
    }

    header = crt_image_header(image);
    if (header == NULL || !crt_image_is_generic_supported(image)) {
        crt_image_destroy(image);
        runtime_publish_error(rt, "unsupported CRT cartridge type");
        return;
    }

    if (!runtime_copy_generic_crt_roms(rt, image, roml, romh, &has_roml, &has_romh)) {
        crt_image_destroy(image);
        return;
    }

    if (header->exrom == 0 && header->game != 0) {
        if (!has_roml || has_romh) {
            crt_image_destroy(image);
            runtime_publish_error(rt, "unsupported 8K CRT ROM layout");
            return;
        }
    } else if (header->exrom == 0 && header->game == 0) {
        if (!has_roml || !has_romh) {
            crt_image_destroy(image);
            runtime_publish_error(rt, "unsupported 16K CRT ROM layout");
            return;
        }
    } else {
        crt_image_destroy(image);
        runtime_publish_error(rt, "unsupported CRT EXROM/GAME mode");
        return;
    }

    if (!c64_attach_generic_cartridge(
            &rt->machine,
            roml,
            sizeof(roml),
            has_romh ? romh : NULL,
            has_romh ? sizeof(romh) : 0u,
            header->exrom,
            header->game,
            error,
            sizeof(error))) {
        crt_image_destroy(image);
        runtime_publish_error(rt, error);
        return;
    }
    crt_image_destroy(image);

    if (!runtime_reset_machine(rt)) {
        return;
    }

    rt->exec_state = RUNTIME_EXEC_RUNNING;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    runtime_reset_pacer(rt);
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
    runtime_publish_machine_state(rt);
}

static bool runtime_load_prg_bytes(runtime *rt, const char *path) {
    uint8_t *bytes;
    size_t length;
    bool loaded;

    if (!runtime_read_host_file(rt, path, "PRG", &bytes, &length)) {
        return false;
    }

    if (runtime_path_has_extension(path, "t64")) {
        t64_file_data file = {0};
        t64_result result = t64_extract_first_prg(bytes, length, &file);
        free(bytes);
        if (result != T64_OK) {
            char message[128];
            snprintf(message, sizeof(message), "failed to extract T64 PRG: %s", t64_result_string(result));
            runtime_publish_error(rt, message);
            return false;
        }
        loaded = runtime_inject_prg_image(rt, file.bytes, file.size);
        t64_file_data_free(&file);
        return loaded;
    }

    loaded = runtime_inject_prg_image(rt, bytes, length);
    free(bytes);
    return loaded;
}

static void runtime_load_prg(runtime *rt, const runtime_command *command) {
    bool was_running = rt->exec_state == RUNTIME_EXEC_RUNNING;

    /* Loading a program boots to BASIC and injects at the $E38B trap. Any
       attached cartridge would take over the reset vector and prevent BASIC
       from running (and the injection from firing), so detach it first. */
    c64_detach_cartridge(&rt->machine);

    if (!runtime_reset_machine(rt)) {
        return;
    }

    if (!runtime_replace_string(&rt->pending_prg_path, command->data.load_prg.path)) {
        runtime_publish_error(rt, "failed to store PRG path");
        return;
    }

    /* Boot the machine so the Kernal and BASIC initialize fully before the
       PRG bytes are injected at the BASIC warm-start address ($E38B). */
    rt->exec_state = RUNTIME_EXEC_RUNNING;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    rt->pending_prg_resume_running = was_running;
    runtime_reset_pacer(rt);
}

static void runtime_autorun_paste(runtime *rt, const char *text) {
    paste_state *p = &rt->paste;
    size_t len = strlen(text);
    if (len > RUNTIME_PASTE_TEXT_MAX) {
        len = RUNTIME_PASTE_TEXT_MAX;
    }
    memcpy(p->text, text, len);
    p->length = len;
    p->position = 0;
    p->shift_needed = false;
    p->in_gap = false;
    p->use_buffer = true;
    p->phase_end_cycle = rt->machine.clock.cycle;
    rt->paste_active = true;
}

static void runtime_complete_pending_prg_load(runtime *rt, char *path) {
    bool resume_running = rt->pending_prg_resume_running;
    bool loaded = runtime_load_prg_bytes(rt, path);

    free(path);
    rt->pending_prg_resume_running = false;

    if (!loaded) {
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = RUNTIME_STOP_REASON_ERROR;
        runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
        runtime_publish_machine_state(rt);
        return;
    }

    if (rt->autorun) {
        rt->autorun_d64_phase = 0;
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        runtime_autorun_paste(rt, "RUN\r");
        runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
        runtime_publish_machine_state(rt);
        return;
    }

    if (resume_running) {
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
        runtime_publish_machine_state(rt);
        return;
    }

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_RESET;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
    runtime_publish_machine_state(rt);
}

static void runtime_publish_assemble_complete(runtime *rt, const char *path, uint16_t address);

static void runtime_publish_symbols(runtime *rt) {
    runtime_symbol_slot *slot = &rt->symbol_slot;
    runtime_symbol_snapshot *snap = &slot->snapshot;
    size_t total;
    size_t i;
    symbol_info info;

    mutex_lock(slot->mutex);

    total = symbol_table_count(rt->symbols);
    snap->total = total;
    snap->count = total < RUNTIME_SYMBOL_SNAPSHOT_MAX ? total : RUNTIME_SYMBOL_SNAPSHOT_MAX;

    for (i = 0; i < snap->count; ++i) {
        if (symbol_table_get(rt->symbols, i, &info) == SYMBOL_OK) {
            snap->entries[i].address = info.address;
            snprintf(snap->entries[i].name, RUNTIME_SYMBOL_NAME_MAX, "%s", info.name);
        } else {
            snap->entries[i].address = 0;
            snap->entries[i].name[0] = '\0';
        }
    }

    slot->has_symbols = true;
    mutex_unlock(slot->mutex);
}

static void runtime_publish_assemble_error(runtime *rt, const char *message) {
    runtime_event event = {
        .type = RUNTIME_EVENT_ASSEMBLE_ERROR,
    };
    snprintf(event.data.error.message, sizeof(event.data.error.message), "%s",
        message != NULL ? message : "assembly failed");
    runtime_publish_event(rt, &event);
}

static void runtime_complete_pending_asm(runtime *rt, char *path) {
    char error[4096];
    bool ok;
    uint16_t address = rt->pending_asm_address;
    uint16_t run_address = rt->pending_asm_run_address;
    bool auto_run = rt->pending_asm_auto_run;

    ok = runtime_assemble_file(&rt->machine, rt->symbols, path, address, path, error, sizeof(error));

    if (ok) {
        runtime_publish_symbols(rt);
        if (auto_run) {
            rt->machine.cpu.cpu.pc = run_address;
            rt->machine.cpu.cpu.sp = 0x0100u + 0xFFu;
        }
    }

    /* Always resume regardless of success or failure. */
    rt->exec_state = RUNTIME_EXEC_RUNNING;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    runtime_reset_pacer(rt);
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
    runtime_publish_machine_state(rt);

    if (ok) {
        runtime_publish_assemble_complete(rt, path, address);
        runtime_publish_memory(rt, address, RUNTIME_MEMORY_SNAPSHOT_MAX, RUNTIME_MEMORY_MODE_RAM);
    } else {
        runtime_publish_assemble_error(rt, error[0] != '\0' ? error : "assembly failed");
    }

    free(path);
}

static void runtime_publish_assemble_complete(runtime *rt, const char *path, uint16_t address) {
    runtime_event event = {
        .type = RUNTIME_EVENT_ASSEMBLE_COMPLETE,
    };

    event.data.assemble.address = address;
    snprintf(event.data.assemble.path, sizeof(event.data.assemble.path), "%s", path ? path : "");
    runtime_publish_event(rt, &event);
}

static void runtime_assemble_file_command(runtime *rt, const runtime_command *command) {
    char error[4096];

    if (command->data.assemble_file.reset_first) {
        /* Reset machine, run to BASIC ($E38B), then assemble (like PRG load). */
        if (!runtime_reset_machine(rt)) {
            return;
        }
        free(rt->pending_asm_path);
        rt->pending_asm_path = NULL;
        if (!runtime_replace_string(&rt->pending_asm_path, command->data.assemble_file.path)) {
            runtime_publish_error(rt, "failed to store assembler path");
            return;
        }
        rt->pending_asm_address     = command->data.assemble_file.address;
        rt->pending_asm_run_address = command->data.assemble_file.run_address;
        rt->pending_asm_auto_run    = command->data.assemble_file.auto_run != 0;
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        return;
    }

    /* No-reset path: assemble directly into live RAM, works in any exec state. */
    if (!runtime_assemble_file(
            &rt->machine,
            rt->symbols,
            command->data.assemble_file.path,
            command->data.assemble_file.address,
            command->data.assemble_file.path,
            error,
            sizeof(error))) {
        runtime_publish_assemble_error(rt, error[0] != '\0' ? error : "assembly failed");
        return;
    }

    runtime_publish_symbols(rt);
    if (command->data.assemble_file.auto_run) {
        rt->machine.cpu.cpu.pc = command->data.assemble_file.run_address;
        rt->machine.cpu.cpu.sp = 0x0100u + 0xFFu;
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
    }
    runtime_publish_assemble_complete(rt, command->data.assemble_file.path, command->data.assemble_file.address);
    runtime_publish_memory(rt, command->data.assemble_file.address, RUNTIME_MEMORY_SNAPSHOT_MAX, RUNTIME_MEMORY_MODE_RAM);
    runtime_publish_machine_state(rt);
}

/* Returns true if the step-out/over loop should abort. Drains only the
   commands that alter execution state; everything else is dropped for now. */
static bool runtime_flow_abort_requested(runtime *rt, bool *alive) {
    runtime_command command;

    while (message_queue_try_pop(rt->command_queue, &command)) {
        switch (command.type) {
            case RUNTIME_COMMAND_QUIT:
                *alive = false;
                return true;
            case RUNTIME_COMMAND_PAUSE:
                rt->exec_state = RUNTIME_EXEC_PAUSED;
                rt->last_stop_reason = RUNTIME_STOP_REASON_PAUSE_COMMAND;
                runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
                runtime_publish_machine_state(rt);
                return true;
            case RUNTIME_COMMAND_RUN:
                rt->temp_bp_active = false;
                rt->exec_state = RUNTIME_EXEC_RUNNING;
                runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
                return true;
            default:
                break;
        }
    }
    return false;
}

static bool runtime_step_out(runtime *rt, bool *alive) {
    char error[256];
    int interrupt_depth = 0;
    uint64_t irq_before, nmi_before;
    uint8_t opcode;
    bool interrupt_taken;

    /* Step Out means "exit the routine I am currently in."  jsr_counter=1
       means we are 1 level deep and need 1 net RTS to get out.  JSRs
       encountered along the way push the counter up; their RTSs bring it
       back.  Only when jsr_counter reaches 0 have we actually returned from
       the current frame.
       If the outer routine never returns (e.g. a tight JSR/JMP loop) we
       would loop here forever with no UI feedback.  After STEP_OUT_FAST_LIMIT
       instructions without finding the exit, transition to free-running mode
       so the user can see the emulator is executing and can pause it. */
    int jsr_counter = 1;
    int fast_limit = 0;
    rt->suppress_execute_bp = true;

    for (;;) {
        if (runtime_flow_abort_requested(rt, alive)) {
            return *alive;
        }

        if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return true;
        }
        if (runtime_brk_pending(rt)) {
            runtime_pause_for_brk(rt);
            return true;
        }

        opcode = (uint8_t)c64_debug_read_cpu_map(&rt->machine, rt->machine.cpu.cpu.pc);
        irq_before = rt->machine.cpu.cpu.irq_entries;
        nmi_before = rt->machine.cpu.cpu.nmi_entries;

        if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
            runtime_publish_error(rt, error);
            return false;
        }

        rt->suppress_execute_bp = false;

        interrupt_taken =
            rt->machine.cpu.cpu.irq_entries != irq_before ||
            rt->machine.cpu.cpu.nmi_entries != nmi_before;

        if (interrupt_taken) {
            interrupt_depth++;
        } else if (opcode == 0x40u /* RTI */ && interrupt_depth > 0) {
            interrupt_depth--;
        } else if (interrupt_depth == 0) {
            if (opcode == 0x20u /* JSR */) {
                jsr_counter++;
            } else if (opcode == 0x60u /* RTS */) {
                jsr_counter--;
                if (jsr_counter <= 0) {
                    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
                    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
                    runtime_publish_cpu_state(rt);
                    return true;
                }
            }
        }

        if (runtime_pause_if_breakpoint_pending(rt)) {
            return true;
        }

        if (++fast_limit >= STEP_OUT_FAST_LIMIT) {
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
            return true;
        }
    }
}

static bool runtime_step_over(runtime *rt, bool *alive) {
    char error[256];
    uint8_t opcode;
    uint16_t stop_pc;
    int jsr_counter;
    int interrupt_depth;
    uint64_t irq_before, nmi_before;
    bool interrupt_taken;

    opcode = (uint8_t)c64_debug_read_cpu_map(&rt->machine, rt->machine.cpu.cpu.pc);

    if (opcode != 0x20u /* JSR */) {
        rt->suppress_execute_bp = false;
        if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
            runtime_publish_error(rt, error);
            return false;
        }
        rt->breakpoint_hit_pending = false;
        rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
        runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
        runtime_publish_cpu_state(rt);
        return true;
    }

    stop_pc = (uint16_t)(rt->machine.cpu.cpu.pc + 3u);
    jsr_counter = 0;
    interrupt_depth = 0;
    rt->suppress_execute_bp = true;

    fprintf(stderr, "STEP OVER: start PC=%04X stop_pc=%04X\n",
            (unsigned)rt->machine.cpu.cpu.pc, (unsigned)stop_pc);

    {
        int fast_limit = 0;
        for (;;) {
            if (runtime_flow_abort_requested(rt, alive)) {
                return *alive;
            }

            if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
                fprintf(stderr, "STEP OVER: breakpoint hit at PC=%04X jsr=%d idepth=%d\n",
                        (unsigned)rt->machine.cpu.cpu.pc, jsr_counter, interrupt_depth);
                runtime_pause_for_breakpoint(rt);
                return true;
            }
            if (runtime_brk_pending(rt)) {
                runtime_pause_for_brk(rt);
                return true;
            }

            opcode = (uint8_t)c64_debug_read_cpu_map(&rt->machine, rt->machine.cpu.cpu.pc);
            irq_before = rt->machine.cpu.cpu.irq_entries;
            nmi_before = rt->machine.cpu.cpu.nmi_entries;

            if (!c64_step_instruction(&rt->machine, error, sizeof(error))) {
                runtime_publish_error(rt, error);
                return false;
            }

            rt->suppress_execute_bp = false;

            interrupt_taken =
                rt->machine.cpu.cpu.irq_entries != irq_before ||
                rt->machine.cpu.cpu.nmi_entries != nmi_before;

            if (interrupt_taken) {
                interrupt_depth++;
                fprintf(stderr, "STEP OVER: IRQ/NMI taken at opc=%02X new_PC=%04X idepth=%d jsr=%d\n",
                        (unsigned)opcode, (unsigned)rt->machine.cpu.cpu.pc,
                        interrupt_depth, jsr_counter);
            } else if (opcode == 0x40u /* RTI */ && interrupt_depth > 0) {
                interrupt_depth--;
                fprintf(stderr, "STEP OVER: RTI at opc-PC, new_PC=%04X idepth=%d jsr=%d\n",
                        (unsigned)rt->machine.cpu.cpu.pc, interrupt_depth, jsr_counter);
            } else if (interrupt_depth == 0) {
                if (opcode == 0x20u /* JSR */) {
                    jsr_counter++;
                    fprintf(stderr, "STEP OVER: JSR -> new_PC=%04X jsr=%d\n",
                            (unsigned)rt->machine.cpu.cpu.pc, jsr_counter);
                } else if (opcode == 0x60u /* RTS */) {
                    jsr_counter--;
                    fprintf(stderr, "STEP OVER: RTS -> new_PC=%04X jsr=%d (stop_pc=%04X)\n",
                            (unsigned)rt->machine.cpu.cpu.pc, jsr_counter,
                            (unsigned)stop_pc);
                }
            }

            if (runtime_pause_if_breakpoint_pending(rt)) {
                return true;
            }

            if (interrupt_depth == 0 && jsr_counter <= 0 &&
                rt->machine.cpu.cpu.pc == stop_pc) {
                fprintf(stderr, "STEP OVER: done at PC=%04X after %d instructions\n",
                        (unsigned)stop_pc, fast_limit);
                rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
                runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
                runtime_publish_cpu_state(rt);
                return true;
            }

            if (++fast_limit % STEP_OVER_LOG_INTERVAL == 0) {
                fprintf(stderr, "STEP OVER: still running iter=%d PC=%04X stop=%04X jsr=%d idepth=%d\n",
                        fast_limit, (unsigned)rt->machine.cpu.cpu.pc,
                        (unsigned)stop_pc, jsr_counter, interrupt_depth);
            }

            if (fast_limit >= STEP_OVER_FAST_LIMIT) {
                fprintf(stderr, "STEP OVER: fallback to RUNNING after %d instructions"
                        " PC=%04X stop_pc=%04X jsr=%d idepth=%d\n",
                        fast_limit, (unsigned)rt->machine.cpu.cpu.pc,
                        (unsigned)stop_pc, jsr_counter, interrupt_depth);
                rt->exec_state = RUNTIME_EXEC_RUNNING;
                runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
                return true;
            }
        }
    }
}

static bool runtime_load_bin_bytes(
    runtime *rt,
    const char *path,
    uint16_t address,
    bool use_file_address,
    bool is_basic) {
    FILE *file;
    uint8_t *bytes;
    size_t length;
    size_t payload_length;
    uint16_t load_address;
    size_t i;

    file = fopen(path, "rb");
    if (file == NULL) {
        runtime_publish_error(rt, "failed to open bin file");
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        runtime_publish_error(rt, "failed to inspect bin file");
        return false;
    }
    length = (size_t)ftell(file);
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        runtime_publish_error(rt, "failed to seek bin file");
        return false;
    }

    if (use_file_address) {
        if (length < 2) {
            fclose(file);
            runtime_publish_error(rt, "bin file too short for address header");
            return false;
        }
        if (length > 65538u) {
            fclose(file);
            runtime_publish_error(rt, "bin file too large");
            return false;
        }
    } else {
        if (length == 0 || length > 65536u) {
            fclose(file);
            runtime_publish_error(rt, "invalid bin file size");
            return false;
        }
    }

    bytes = malloc(length);
    if (bytes == NULL) {
        fclose(file);
        runtime_publish_error(rt, "failed to allocate bin buffer");
        return false;
    }

    if (fread(bytes, 1, length, file) != length) {
        free(bytes);
        fclose(file);
        runtime_publish_error(rt, "failed to read bin file");
        return false;
    }
    fclose(file);

    if (use_file_address) {
        load_address = (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
        payload_length = length - 2u;
        if ((uint32_t)load_address + payload_length > 0x10000u) {
            free(bytes);
            runtime_publish_error(rt, "bin load range overflows address space");
            return false;
        }
        for (i = 0; i < payload_length; ++i) {
            c64_debug_write_ram(&rt->machine, (uint16_t)(load_address + i), bytes[i + 2u]);
        }
        runtime_publish_memory(
            rt,
            load_address,
            payload_length > RUNTIME_MEMORY_SNAPSHOT_MAX ? RUNTIME_MEMORY_SNAPSHOT_MAX : (uint16_t)payload_length,
            RUNTIME_MEMORY_MODE_RAM);
    } else {
        load_address = address;
        payload_length = length;
        if ((uint32_t)load_address + payload_length > 0x10000u) {
            free(bytes);
            runtime_publish_error(rt, "bin load range overflows address space");
            return false;
        }
        for (i = 0; i < payload_length; ++i) {
            c64_debug_write_ram(&rt->machine, (uint16_t)(load_address + i), bytes[i]);
        }
        runtime_publish_memory(
            rt,
            load_address,
            payload_length > RUNTIME_MEMORY_SNAPSHOT_MAX ? RUNTIME_MEMORY_SNAPSHOT_MAX : (uint16_t)payload_length,
            RUNTIME_MEMORY_MODE_RAM);
    }

    free(bytes);

    if (is_basic) {
        uint16_t vartab = (uint16_t)(load_address + payload_length);
        c64_debug_write_ram(&rt->machine, 0x2Bu, (uint8_t)(load_address & 0xFFu));
        c64_debug_write_ram(&rt->machine, 0x2Cu, (uint8_t)((load_address >> 8) & 0xFFu));
        c64_debug_write_ram(&rt->machine, 0x2Du, (uint8_t)(vartab & 0xFFu));
        c64_debug_write_ram(&rt->machine, 0x2Eu, (uint8_t)((vartab >> 8) & 0xFFu));
    }

    return true;
}

static void runtime_complete_pending_bin_load(runtime *rt, char *path) {
    bool is_basic = rt->pending_bin_is_basic != 0;
    bool loaded = runtime_load_bin_bytes(
        rt, path, rt->pending_bin_address,
        rt->pending_bin_use_file_address != 0,
        is_basic);

    free(path);

    if (!loaded) {
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = RUNTIME_STOP_REASON_ERROR;
        runtime_publish_simple_event(rt, RUNTIME_EVENT_PAUSED);
        runtime_publish_machine_state(rt);
        return;
    }

    rt->exec_state = RUNTIME_EXEC_RUNNING;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    runtime_reset_pacer(rt);
    if (rt->autorun && is_basic) {
        rt->autorun_d64_phase = 0;
        runtime_autorun_paste(rt, "RUN\r");
    }
    runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
    runtime_publish_machine_state(rt);
}

static void runtime_load_bin(runtime *rt, const runtime_command *command) {
    if (command->data.load_bin.reset_first) {
        /* A reset preserves any attached cartridge, which would boot instead of
           BASIC; detach so the freshly loaded program is what runs. */
        c64_detach_cartridge(&rt->machine);
        if (!runtime_reset_machine(rt)) {
            return;
        }
        free(rt->pending_bin_path);
        rt->pending_bin_path = NULL;
        if (!runtime_replace_string(&rt->pending_bin_path, command->data.load_bin.path)) {
            runtime_publish_error(rt, "failed to store bin path");
            return;
        }
        rt->pending_bin_address = command->data.load_bin.address;
        rt->pending_bin_use_file_address = command->data.load_bin.use_file_address;
        rt->pending_bin_is_basic = command->data.load_bin.is_basic;
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        return;
    }

    runtime_load_bin_bytes(
        rt,
        command->data.load_bin.path,
        command->data.load_bin.address,
        command->data.load_bin.use_file_address != 0,
        command->data.load_bin.is_basic != 0);
}

static void runtime_save_bin(runtime *rt, const runtime_command *command) {
    const char *path = command->data.save_bin.path;
    uint16_t start_addr;
    uint16_t end_addr;
    bool write_header;
    uint32_t length;
    FILE *file;
    uint32_t i;

    if (command->data.save_bin.is_basic) {
        start_addr = (uint16_t)c64_debug_read_ram(&rt->machine, 0x2Bu) |
                     (uint16_t)((uint16_t)c64_debug_read_ram(&rt->machine, 0x2Cu) << 8);
        end_addr   = (uint16_t)c64_debug_read_ram(&rt->machine, 0x2Du) |
                     (uint16_t)((uint16_t)c64_debug_read_ram(&rt->machine, 0x2Eu) << 8);
        write_header = true;
        if (end_addr <= start_addr) {
            runtime_publish_error(rt, "BASIC program appears empty");
            return;
        }
        /* VARTAB is one-past-end */
        length = (uint32_t)end_addr - start_addr;
    } else {
        start_addr   = command->data.save_bin.start_address;
        end_addr     = command->data.save_bin.end_address;
        write_header = command->data.save_bin.write_file_address != 0;
        if (start_addr > end_addr) {
            runtime_publish_error(rt, "bin save start address exceeds end address");
            return;
        }
        /* end address is inclusive */
        length = (uint32_t)end_addr - start_addr + 1u;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        runtime_publish_error(rt, "failed to open bin save file");
        return;
    }

    if (write_header) {
        fputc((int)(start_addr & 0xFFu), file);
        fputc((int)((start_addr >> 8) & 0xFFu), file);
    }

    for (i = 0; i < length; ++i) {
        fputc((int)c64_debug_read_ram(&rt->machine, (uint16_t)(start_addr + i)), file);
    }

    if (ferror(file)) {
        fclose(file);
        runtime_publish_error(rt, "failed to write bin save file");
        return;
    }
    fclose(file);
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
            runtime_reset_command(rt);
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

        case RUNTIME_COMMAND_REQUEST_MEMORY_VIEW:
            if (runtime_memory_mode_is_valid(command->data.request_memory.mode)) {
                runtime_event mem_view_event = {
                    .type = RUNTIME_EVENT_MEMORY_VIEW_RESPONSE,
                };
                uint16_t mv_address = command->data.request_memory.address;
                uint16_t mv_length = command->data.request_memory.length;
                runtime_memory_mode mv_mode = (runtime_memory_mode)command->data.request_memory.mode;
                uint16_t i;

                if (mv_length > RUNTIME_MEMORY_SNAPSHOT_MAX) {
                    mv_length = RUNTIME_MEMORY_SNAPSHOT_MAX;
                }
                mem_view_event.data.memory.address = mv_address;
                mem_view_event.data.memory.mode = mv_mode;
                mem_view_event.data.memory.length = mv_length;
                for (i = 0; i < mv_length; ++i) {
                    uint16_t cur = (uint16_t)(mv_address + i);
                    if (mv_mode == RUNTIME_MEMORY_MODE_RAM) {
                        mem_view_event.data.memory.bytes[i] = c64_debug_read_ram(&rt->machine, cur);
                    } else if (mv_mode == RUNTIME_MEMORY_MODE_ROM) {
                        mem_view_event.data.memory.bytes[i] = c64_debug_read_rom(&rt->machine, cur);
                    } else {
                        mem_view_event.data.memory.bytes[i] = c64_debug_read_cpu_map(&rt->machine, cur);
                    }
                    mem_view_event.data.memory.write_history[i] =
                        c64_debug_read_write_history(&rt->machine, cur);
                }
                runtime_publish_event(rt, &mem_view_event);
            } else {
                runtime_publish_error(rt, "unsupported memory view request mode");
            }
            break;

        case RUNTIME_COMMAND_REQUEST_DEBUG_MEMORY:
            runtime_publish_debug_memory(
                rt,
                command->data.request_debug_memory.include_write_history != 0);
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

        case RUNTIME_COMMAND_SET_JOYSTICK:
            c64_set_joystick(
                &rt->machine,
                command->data.set_joystick.port,
                command->data.set_joystick.inputs);
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

        case RUNTIME_COMMAND_REARM_ONESHOT_BREAKPOINTS:
            runtime_rearm_oneshot_breakpoints(rt);
            break;

        case RUNTIME_COMMAND_REQUEST_BREAKPOINTS:
            runtime_publish_breakpoints(rt);
            break;

        case RUNTIME_COMMAND_LOAD_PRG:
            runtime_load_prg(rt, command);
            break;

        case RUNTIME_COMMAND_LOAD_CRT:
            runtime_load_crt(rt, command);
            break;

        case RUNTIME_COMMAND_MOUNT_D64:
            runtime_mount_d64(rt, command);
            break;

        case RUNTIME_COMMAND_UNMOUNT_DISK:
            c64_unmount_drive(&rt->machine, command->data.disk_device.device);
            runtime_publish_drive_status(rt, command->data.disk_device.device);
            break;

        case RUNTIME_COMMAND_REQUEST_DISK_STATUS:
            runtime_publish_drive_status(rt, command->data.disk_device.device);
            break;

        case RUNTIME_COMMAND_ASSEMBLE_FILE:
            runtime_assemble_file_command(rt, command);
            break;

        case RUNTIME_COMMAND_APPLY_MACHINE_CONFIG:
            runtime_apply_machine_config(rt, command);
            break;

        case RUNTIME_COMMAND_CYCLE_TURBO_SPEED:
            runtime_cycle_turbo_speed(rt);
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
            p->use_buffer = command->data.paste_text.use_buffer != 0;
            p->phase_end_cycle = rt->machine.clock.cycle;
            rt->paste_active = true;
            break;
        }

        case RUNTIME_COMMAND_STEP_OUT:
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_step_out(rt, alive);
            break;

        case RUNTIME_COMMAND_STEP_OVER:
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            runtime_step_over(rt, alive);
            break;

        case RUNTIME_COMMAND_RUN_TO_CURSOR:
            rt->temp_bp_active = true;
            rt->temp_bp_address = command->data.run_to_cursor.address;
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            runtime_publish_simple_event(rt, RUNTIME_EVENT_RUNNING);
            break;

        case RUNTIME_COMMAND_LOAD_BIN:
            runtime_load_bin(rt, command);
            break;

        case RUNTIME_COMMAND_SAVE_BIN:
            runtime_save_bin(rt, command);
            break;

        case RUNTIME_COMMAND_REQUEST_CALL_STACK: {
            runtime_event cs_event = { .type = RUNTIME_EVENT_CALL_STACK_RESPONSE };
            uint16_t sp16 = rt->machine.cpu.cpu.sp;
            uint16_t cs_addr = (uint16_t)(sp16 + 1u);
            cs_event.data.call_stack.sp = (uint8_t)(sp16 & 0xFFu);
            cs_event.data.call_stack.count = 0;
            while (cs_addr <= 0x01FEu &&
                   cs_event.data.call_stack.count < RUNTIME_CALL_STACK_MAX) {
                uint8_t lo = c64_debug_read_ram(&rt->machine, cs_addr);
                uint8_t hi = c64_debug_read_ram(&rt->machine, (uint16_t)(cs_addr + 1u));
                uint16_t stack_val = (uint16_t)((uint16_t)hi << 8u | lo);
                if (stack_val >= 2u &&
                    c64_debug_read_cpu_map(&rt->machine, (uint16_t)(stack_val - 2u)) == 0x20u) {
                    uint8_t dst_lo = c64_debug_read_cpu_map(&rt->machine, (uint16_t)(stack_val - 1u));
                    uint8_t dst_hi = c64_debug_read_cpu_map(&rt->machine, stack_val);
                    uint8_t idx = cs_event.data.call_stack.count;
                    cs_event.data.call_stack.entries[idx].jsr_address = (uint16_t)(stack_val - 2u);
                    cs_event.data.call_stack.entries[idx].dest_address =
                        (uint16_t)((uint16_t)dst_hi << 8u | dst_lo);
                    cs_event.data.call_stack.count++;
                    cs_addr += 2u;
                } else {
                    cs_addr++;
                }
            }
            runtime_publish_event(rt, &cs_event);
            break;
        }

        case RUNTIME_COMMAND_PASTE_EVENTS: {
            paste_state *p = &rt->paste;
            size_t count = command->data.paste_events.count;
            if (count > PASTE_EVENTS_MAX) count = PASTE_EVENTS_MAX;
            memcpy(p->events, command->data.paste_events.events,
                   count * sizeof(paste_event_t));
            p->event_count  = count;
            p->event_cursor = 0;
            p->event_mode   = true;
            p->use_buffer   = false;
            p->in_gap       = true;
            p->phase_end_cycle = rt->machine.clock.cycle;
            memset(p->asserted_keys, 0, sizeof(p->asserted_keys));
            memset(p->oneshot_keys,  0, sizeof(p->oneshot_keys));
            memset(p->temp_keys,     0, sizeof(p->temp_keys));
            memset(p->asserted_joy,  0, sizeof(p->asserted_joy));
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

/* Maps ASCII 0-127 to PETSCII keyboard-buffer codes (0 = unmapped/skip).
   $20-$3F and uppercase letters are identity. Lowercase folds to uppercase.
   [ and ] use their PETSCII equivalents from SHIFT+colon/semicolon.
   ^ maps to PETSCII $5E (C64 up-arrow, the BASIC exponentiation operator). */
static const uint8_t ascii_to_petscii[128] = {
    ['\n'] = 0x0D, ['\r'] = 0x0D,
    [' ' ] = 0x20, ['!' ] = 0x21, ['"' ] = 0x22, ['#' ] = 0x23,
    ['$' ] = 0x24, ['%' ] = 0x25, ['&' ] = 0x26, ['\''] = 0x27,
    ['(' ] = 0x28, [')' ] = 0x29, ['*' ] = 0x2A, ['+' ] = 0x2B,
    [',' ] = 0x2C, ['-' ] = 0x2D, ['.' ] = 0x2E, ['/' ] = 0x2F,
    ['0' ] = 0x30, ['1' ] = 0x31, ['2' ] = 0x32, ['3' ] = 0x33,
    ['4' ] = 0x34, ['5' ] = 0x35, ['6' ] = 0x36, ['7' ] = 0x37,
    ['8' ] = 0x38, ['9' ] = 0x39, [':' ] = 0x3A, [';' ] = 0x3B,
    ['<' ] = 0x3C, ['=' ] = 0x3D, ['>' ] = 0x3E, ['?' ] = 0x3F,
    ['@' ] = 0x40,
    ['A' ] = 0x41, ['B' ] = 0x42, ['C' ] = 0x43, ['D' ] = 0x44,
    ['E' ] = 0x45, ['F' ] = 0x46, ['G' ] = 0x47, ['H' ] = 0x48,
    ['I' ] = 0x49, ['J' ] = 0x4A, ['K' ] = 0x4B, ['L' ] = 0x4C,
    ['M' ] = 0x4D, ['N' ] = 0x4E, ['O' ] = 0x4F, ['P' ] = 0x50,
    ['Q' ] = 0x51, ['R' ] = 0x52, ['S' ] = 0x53, ['T' ] = 0x54,
    ['U' ] = 0x55, ['V' ] = 0x56, ['W' ] = 0x57, ['X' ] = 0x58,
    ['Y' ] = 0x59, ['Z' ] = 0x5A,
    ['[' ] = 0x5B, [']' ] = 0x5D, ['^' ] = 0x5E,
    ['a' ] = 0x41, ['b' ] = 0x42, ['c' ] = 0x43, ['d' ] = 0x44,
    ['e' ] = 0x45, ['f' ] = 0x46, ['g' ] = 0x47, ['h' ] = 0x48,
    ['i' ] = 0x49, ['j' ] = 0x4A, ['k' ] = 0x4B, ['l' ] = 0x4C,
    ['m' ] = 0x4D, ['n' ] = 0x4E, ['o' ] = 0x4F, ['p' ] = 0x50,
    ['q' ] = 0x51, ['r' ] = 0x52, ['s' ] = 0x53, ['t' ] = 0x54,
    ['u' ] = 0x55, ['v' ] = 0x56, ['w' ] = 0x57, ['x' ] = 0x58,
    ['y' ] = 0x59, ['z' ] = 0x5A,
};

/* Joystick direction (spec: 0=centred, 1-8 clockwise from up) → C64 bitmask */
static const uint8_t s_joy_dir_map[9] = {
    0x00,                                          /* 0: centred */
    C64_JOYSTICK_UP,                               /* 1: up */
    C64_JOYSTICK_UP   | C64_JOYSTICK_RIGHT,        /* 2: up-right */
    C64_JOYSTICK_RIGHT,                            /* 3: right */
    C64_JOYSTICK_DOWN | C64_JOYSTICK_RIGHT,        /* 4: down-right */
    C64_JOYSTICK_DOWN,                             /* 5: down */
    C64_JOYSTICK_DOWN | C64_JOYSTICK_LEFT,         /* 6: down-left */
    C64_JOYSTICK_LEFT,                             /* 7: left */
    C64_JOYSTICK_UP   | C64_JOYSTICK_LEFT,         /* 8: up-left */
};

/* Event-driven paste: consumes paste_event_t[] with full matrix key control.
   Timed events go through hold/gap phases. Explicit holds, one-shot modifiers,
   and temporary synthetic keys are tracked separately so cleanup releases only
   the keys owned by that path. */
static uint64_t paste_normal_keypress_cycles(void) {
    return (uint64_t)PASTE_HOLD_CYCLES + (uint64_t)PASTE_GAP_CYCLES;
}

static void paste_clear_temp_keys(runtime *rt) {
    paste_state *p = &rt->paste;
    int i;

    for (i = 0; i < C64_KEY_COUNT; i++) {
        if (p->temp_keys[i]) {
            c64_set_key(&rt->machine, (c64_key)i, false);
            p->temp_keys[i] = false;
        }
    }
}

static void paste_release_oneshot_keys(runtime *rt) {
    paste_state *p = &rt->paste;
    int i;

    for (i = 0; i < C64_KEY_COUNT; i++) {
        if (p->oneshot_keys[i]) {
            if (!p->asserted_keys[i] && !p->temp_keys[i]) {
                c64_set_key(&rt->machine, (c64_key)i, false);
            }
            p->oneshot_keys[i] = false;
        }
    }
}

static void paste_events_cleanup(runtime *rt) {
    paste_state *p = &rt->paste;
    int i;

    paste_clear_temp_keys(rt);
    for (i = 0; i < C64_KEY_COUNT; i++) {
        if (p->oneshot_keys[i]) {
            if (!p->asserted_keys[i]) {
                c64_set_key(&rt->machine, (c64_key)i, false);
            }
            p->oneshot_keys[i] = false;
        }
        if (p->asserted_keys[i]) {
            c64_set_key(&rt->machine, (c64_key)i, false);
            p->asserted_keys[i] = false;
        }
    }
    c64_set_joystick(&rt->machine, 1, 0);
    c64_set_joystick(&rt->machine, 2, 0);
    p->asserted_joy[0] = 0;
    p->asserted_joy[1] = 0;
}

static void paste_press_temp_key(runtime *rt, c64_key key) {
    paste_state *p = &rt->paste;

    if (!p->asserted_keys[key] && !p->oneshot_keys[key] && !p->temp_keys[key]) {
        c64_set_key(&rt->machine, key, true);
        p->temp_keys[key] = true;
    }
}

static void paste_begin_key_press(runtime *rt, c64_key key, bool needs_shift) {
    paste_state *p = &rt->paste;

    if (needs_shift) {
        paste_press_temp_key(rt, C64_KEY_LEFT_SHIFT);
    }
    paste_press_temp_key(rt, key);
    p->in_gap = false;
    p->phase_end_cycle = rt->machine.clock.cycle + PASTE_HOLD_CYCLES;
}

static void paste_finish_current_timed_event(runtime *rt, paste_event_t *ev, uint64_t now) {
    paste_state *p = &rt->paste;

    switch (ev->type) {
        case PASTE_EV_KEY_PRESS:
            paste_clear_temp_keys(rt);
            paste_release_oneshot_keys(rt);
            p->phase_end_cycle = now + ((c64_key)ev->key.key == C64_KEY_RETURN
                                        ? PASTE_RETURN_GAP_CYCLES : PASTE_GAP_CYCLES);
            break;
        case PASTE_EV_PETSCII: {
            uint8_t val = ev->petscii.petscii;
            if (val < 128 && paste_ascii_map[val].valid) {
                paste_key_entry e = paste_ascii_map[val];
                paste_clear_temp_keys(rt);
                paste_release_oneshot_keys(rt);
                p->phase_end_cycle = now + (e.key == C64_KEY_RETURN
                                            ? PASTE_RETURN_GAP_CYCLES : PASTE_GAP_CYCLES);
            } else {
                p->phase_end_cycle = now + PASTE_GAP_CYCLES;
            }
            break;
        }
        case PASTE_EV_MATRIX:
            c64_set_matrix(&rt->machine, ev->matrix.row, ev->matrix.col, false);
            paste_release_oneshot_keys(rt);
            p->phase_end_cycle = now + PASTE_GAP_CYCLES;
            break;
        case PASTE_EV_JOYSTICK:
            if (ev->joy.port >= 1 && ev->joy.port <= 2) {
                c64_set_joystick(&rt->machine, ev->joy.port, 0);
                p->asserted_joy[ev->joy.port - 1] = 0;
            }
            paste_release_oneshot_keys(rt);
            p->phase_end_cycle = now + PASTE_GAP_CYCLES;
            break;
        case PASTE_EV_NMI:
            paste_release_oneshot_keys(rt);
            p->phase_end_cycle = now + PASTE_GAP_CYCLES;
            break;
        case PASTE_EV_WAIT:
        default:
            p->phase_end_cycle = now;
            break;
    }

    p->event_cursor++;
    p->in_gap = true;
    if (p->event_cursor >= p->event_count) {
        paste_events_cleanup(rt);
        rt->paste_active = false;
        p->event_mode    = false;
    }
}

static void runtime_advance_paste_events(runtime *rt) {
    paste_state   *p   = &rt->paste;
    uint64_t       now = rt->machine.clock.cycle;
    paste_event_t *ev;

    if (now < p->phase_end_cycle) {
        return;
    }

    if (!p->in_gap) {
        paste_finish_current_timed_event(rt, &p->events[p->event_cursor], now);
        return;
    }

    if (p->event_cursor < p->event_count) {
        ev = &p->events[p->event_cursor];
        switch (ev->type) {
            case PASTE_EV_KEY_PRESS:
                paste_begin_key_press(rt, (c64_key)ev->key.key, ev->key.needs_shift != 0);
                return;

            case PASTE_EV_KEY_ASSERT:
                c64_set_key(&rt->machine, (c64_key)ev->key.key, true);
                p->asserted_keys[ev->key.key] = true;
                p->oneshot_keys[ev->key.key] = false;
                p->temp_keys[ev->key.key] = false;
                p->event_cursor++;
                return;

            case PASTE_EV_KEY_DEASSERT:
                c64_set_key(&rt->machine, (c64_key)ev->key.key, false);
                p->asserted_keys[ev->key.key] = false;
                p->oneshot_keys[ev->key.key] = false;
                p->temp_keys[ev->key.key] = false;
                p->event_cursor++;
                return;

            case PASTE_EV_KEY_ONESHOT:
                if (!p->asserted_keys[ev->key.key] && !p->oneshot_keys[ev->key.key]) {
                    c64_set_key(&rt->machine, (c64_key)ev->key.key, true);
                    p->oneshot_keys[ev->key.key] = true;
                }
                p->event_cursor++;
                return;

            case PASTE_EV_PETSCII: {
                uint8_t val = ev->petscii.petscii;
                if (val < 128 && paste_ascii_map[val].valid) {
                    paste_key_entry e = paste_ascii_map[val];
                    paste_begin_key_press(rt, e.key, e.shift);
                    return;
                }
                p->event_cursor++; /* unmapped PETSCII — skip */
                return;
            }

            case PASTE_EV_MATRIX:
                c64_set_matrix(&rt->machine, ev->matrix.row, ev->matrix.col, true);
                p->in_gap = false;
                p->phase_end_cycle = now + PASTE_HOLD_CYCLES;
                return;

            case PASTE_EV_JOYSTICK: {
                uint8_t inputs = s_joy_dir_map[ev->joy.dir];
                if (ev->joy.has_button && ev->joy.button) {
                    inputs |= C64_JOYSTICK_FIRE;
                }
                c64_set_joystick(&rt->machine, ev->joy.port, inputs);
                if (ev->joy.port >= 1 && ev->joy.port <= 2) {
                    p->asserted_joy[ev->joy.port - 1] = inputs;
                }
                p->in_gap = false;
                p->phase_end_cycle = now + PASTE_HOLD_CYCLES;
                return;
            }

            case PASTE_EV_NMI:
                c64_restore(&rt->machine);
                p->in_gap = false;
                p->phase_end_cycle = now + PASTE_HOLD_CYCLES;
                return;

            case PASTE_EV_WAIT:
                if (ev->wait.count == 0) {
                    p->event_cursor++;
                    return;
                }
                p->in_gap = false;
                p->phase_end_cycle = now + paste_normal_keypress_cycles() * ev->wait.count;
                return;

            default:
                p->event_cursor++;
                return;
        }
    }

    paste_events_cleanup(rt);
    rt->paste_active = false;
    p->event_mode    = false;
}

/* Buffer-injection paste: writes PETSCII directly into the KERNAL keyboard
   buffer ($0277-$0280, count at $00C6) whenever space is available.
   Reliable for BASIC and any KERNAL-based app; does not work for games that
   bypass the KERNAL keyboard IRQ. */
static void runtime_advance_paste_buffer(runtime *rt) {
    paste_state *p = &rt->paste;
    uint8_t ndx = rt->machine.bus.ram[0x00C6];
    uint8_t petscii;
    unsigned char ch;

    while (ndx < 10 && p->position < p->length) {
        ch = (unsigned char)p->text[p->position++];
        if (ch >= 128) {
            continue;
        }
        petscii = ascii_to_petscii[ch];
        if (petscii == 0) {
            continue;
        }
        rt->machine.bus.ram[0x0277 + ndx] = petscii;
        ndx++;
    }

    rt->machine.bus.ram[0x00C6] = ndx;

    if (p->position >= p->length) {
        rt->paste_active = false;
    }
}

static void runtime_advance_paste(runtime *rt) {
    paste_state *p = &rt->paste;
    uint64_t now = rt->machine.clock.cycle;
    paste_key_entry entry;
    unsigned char ch;

    if (p->use_buffer) {
        runtime_advance_paste_buffer(rt);
        return;
    }

    if (p->event_mode) {
        runtime_advance_paste_events(rt);
        return;
    }

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
        ch = (unsigned char)p->text[p->position];
        p->position++;
        p->in_gap = true;
        p->phase_end_cycle = now + (ch == '\n' || ch == '\r' ? PASTE_RETURN_GAP_CYCLES : PASTE_GAP_CYCLES);
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

    rt->symbols = symbol_table_create();

    c64_init(&rt->machine);
    c64_set_config(&rt->machine, &rt->machine_config);
    c64_set_memory_access_callback(&rt->machine, runtime_memory_access, rt);
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    rt->speed_mode = RUNTIME_SPEED_MODE_SLOW;
    rt->trace_enabled = false;
    c64_set_cpu_trace_enabled(&rt->machine, false);
    rt->trace_file = NULL;
    rt->breakpoint_count = 0;
    rt->next_breakpoint_id = 1;
    rt->next_frame_cycle = 0;
    rt->pace_initialized = false;
    rt->audio_cycle_accum = 0.0;
    rt->audio_sample_accum = 0.0;
    rt->audio_sample_count = 0;
    rt->audio_smoke_phase = 0.0f;
    runtime_audio_record_init(rt);
    runtime_update_sid_sample_output(rt);
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STARTED);
    runtime_load_symbol_files(rt);
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
                if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
                    runtime_pause_for_breakpoint(rt);
                    break;
                }
                if (runtime_brk_pending(rt)) {
                    runtime_pause_for_brk(rt);
                    break;
                }
                if (rt->temp_bp_active &&
                    rt->machine.cpu.cpu.pc == rt->temp_bp_address) {
                    rt->temp_bp_active = false;
                    rt->suppress_execute_bp = false;
                    rt->exec_state = RUNTIME_EXEC_PAUSED;
                    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;
                    runtime_publish_simple_event(rt, RUNTIME_EVENT_STEP_COMPLETE);
                    runtime_publish_machine_state(rt);
                    break;
                }
                if (rt->pending_prg_path != NULL &&
                    rt->machine.cpu.cpu.pc == 0xE38Bu) {
                    char *path = rt->pending_prg_path;
                    rt->pending_prg_path = NULL;
                    runtime_complete_pending_prg_load(rt, path);
                }
                if (rt->pending_asm_path != NULL &&
                    rt->machine.cpu.cpu.pc == 0xE38Bu) {
                    char *path = rt->pending_asm_path;
                    rt->pending_asm_path = NULL;
                    runtime_complete_pending_asm(rt, path);
                }
                if (rt->pending_bin_path != NULL &&
                    rt->machine.cpu.cpu.pc == 0xE38Bu) {
                    char *path = rt->pending_bin_path;
                    rt->pending_bin_path = NULL;
                    runtime_complete_pending_bin_load(rt, path);
                }
                if (rt->autorun_d64_phase > 0 &&
                    rt->machine.cpu.cpu.pc == 0xE38Bu) {
                    if (rt->autorun_d64_phase == 1) {
                        rt->autorun_d64_phase = 2;
                        runtime_autorun_paste(rt, "LOAD\"*\",8\r");
                    } else if (rt->autorun_d64_phase == 2) {
                        rt->autorun_d64_phase = 0;
                        runtime_autorun_paste(rt, "RUN\r");
                    }
                }
                if (!runtime_step_cycle(rt)) {
                    break;
                }
                {
                    bool instr_done = c64_consume_instruction_complete(&rt->machine);
                    if (rt->trace_enabled && instr_done) {
                        runtime_write_trace_line(rt);
                    }
                    if (rt->suppress_execute_bp && instr_done) {
                        rt->suppress_execute_bp = false;
                    }
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
                    if (rt->trace_file != NULL) {
                        fflush(rt->trace_file);
                    }
                }
            }

            continue;
        }

        if (!message_queue_wait_pop(rt->command_queue, &command)) {
            continue;
        }

        runtime_process_command(rt, &command, &alive);
    }

    free(rt->pending_prg_path);
    rt->pending_prg_path = NULL;
    free(rt->pending_asm_path);
    rt->pending_asm_path = NULL;
    free(rt->pending_bin_path);
    rt->pending_bin_path = NULL;
    if (rt->trace_file != NULL) {
        fprintf(rt->trace_file, "--- STOP  CYC=%08llX ---\n",
            (unsigned long long)rt->machine.clock.cycle);
        fclose(rt->trace_file);
        rt->trace_file = NULL;
    }
    runtime_audio_record_finish(rt);
    symbol_table_destroy(rt->symbols);
    rt->symbols = NULL;
    runtime_publish_simple_event(rt, RUNTIME_EVENT_STOPPED);
    return 0;
}
