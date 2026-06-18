#include "c64.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    C64_VICII_REG_MEMORY_POINTER = 0x18,
    C64_CPU_BUS_MODE_IMMEDIATE = 0,
    C64_CPU_BUS_MODE_TIMED_IMMEDIATE,
    C64_CPU_BUS_MODE_DEFER_WRITES,
};

static void c64_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static bool c64_cpu_io_visible(const c64_t *machine) {
    uint8_t port = machine->bus.cpu_port_data;
    return (port & 0x03u) != 0 && (port & 0x04u) != 0;
}

static bool c64_cpu_address_is_io(const c64_t *machine, uint16_t address) {
    return address >= 0xd000u && address <= 0xdfffu && c64_cpu_io_visible(machine);
}

static uint64_t c64_current_cycle(const c64_t *machine) {
    return machine->clock.cycle;
}

static void c64_step_vic(c64_t *machine) {
    vicii_step_cycle(&machine->vic, &machine->bus, machine->clock.cycle);
    machine->clock.vic_cycles++;
}

static void c64_step_cia1(c64_t *machine) {
    cia_step_cycle(&machine->cia1);
}

static void c64_step_cia2(c64_t *machine) {
    cia_step_cycle(&machine->cia2);
    machine->clock.cia_cycles++;
}

static void c64_step_sid(c64_t *machine) {
    (void)machine;
}

static void c64_advance_one_cycle(c64_t *machine) {
    c64_step_vic(machine);
    c64_step_cia1(machine);
    c64_step_cia2(machine);
    c64_step_sid(machine);
    machine->clock.cycle++;
}

static void c64_advance_devices_to(c64_t *machine, uint64_t target_cycle) {
    assert(machine);
    assert(target_cycle >= machine->clock.cycle);

    while (machine->clock.cycle < target_cycle) {
        c64_advance_one_cycle(machine);
    }
}

static void c64_trace_reset(c64_cpu_instruction_trace *trace) {
    memset(trace, 0, sizeof(*trace));
}

static c64_cpu_bus_event *c64_trace_append_event(
    c64_t *machine,
    c64_cpu_bus_event_kind kind,
    uint16_t address,
    uint8_t value) {
    c64_cpu_instruction_trace *trace = &machine->last_cpu_trace;
    c64_cpu_bus_event *event;
    uint64_t offset64;

    if (trace->event_count >= C64_CPU_TRACE_MAX_EVENTS) {
        return NULL;
    }

    event = &trace->events[trace->event_count++];
    offset64 = machine->cpu.cpu.cycles - machine->cpu_trace_start_cpu_cycle;
    event->cycle_offset = offset64 > 0xffu ? 0xffu : (uint8_t)offset64;
    event->kind = kind;
    event->address = address;
    event->value = value;
    event->is_io = c64_cpu_address_is_io(machine, address) ? 1u : 0u;
    event->absolute_cycle = c64_current_cycle(machine);
    return event;
}

static void c64_report_memory_access(
    c64_t *machine,
    c64_memory_access_type access,
    uint16_t address,
    uint8_t value) {
    if (machine->memory_access) {
        machine->memory_access(machine->memory_access_user, access, address, value);
    }
}

static void c64_apply_cpu_bus_event(c64_t *machine, c64_cpu_bus_event *event) {
    assert(machine);
    assert(event);

    event->absolute_cycle = c64_current_cycle(machine);

    switch (event->kind) {
    case C64_CPU_BUS_EVENT_READ:
        c64_report_memory_access(machine, C64_MEMORY_ACCESS_READ, event->address, event->value);
        break;

    case C64_CPU_BUS_EVENT_WRITE:
        c64_bus_write(&machine->bus, event->address, event->value);
        c64_report_memory_access(machine, C64_MEMORY_ACCESS_WRITE, event->address, event->value);
#if defined(C64M_DEBUG_CIA1_WRITES)
        if (event->address >= 0xdc04u && event->address <= 0xdc0fu) {
            fprintf(stderr, "[CIA1] PC=$%04X  $%04X <- $%02X\n",
                (unsigned)machine->pending_cpu_trace.opcode_pc,
                (unsigned)event->address,
                (unsigned)event->value);
        }
#endif
        break;

    case C64_CPU_BUS_EVENT_INTERNAL:
    default:
        break;
    }
}

static void c64_apply_pending_cpu_events_at_elapsed(c64_t *machine) {
    while (machine->pending_cpu_event_index < machine->pending_cpu_trace.event_count) {
        c64_cpu_bus_event *event = &machine->pending_cpu_trace.events[machine->pending_cpu_event_index];

        if (event->cycle_offset != machine->pending_cpu_elapsed) {
            break;
        }

        c64_apply_cpu_bus_event(machine, event);
        machine->pending_cpu_event_index++;
    }
}

static bool c64_pending_cpu_elapsed_has_read_event(const c64_t *machine) {
    size_t i;

    for (i = machine->pending_cpu_event_index; i < machine->pending_cpu_trace.event_count; i++) {
        const c64_cpu_bus_event *event = &machine->pending_cpu_trace.events[i];

        if (event->cycle_offset != machine->pending_cpu_elapsed) {
            break;
        }
        if (event->kind == C64_CPU_BUS_EVENT_READ) {
            return true;
        }
    }

    return false;
}

static bool c64_pending_cpu_elapsed_has_write_event(const c64_t *machine) {
    size_t i;

    for (i = machine->pending_cpu_event_index; i < machine->pending_cpu_trace.event_count; i++) {
        const c64_cpu_bus_event *event = &machine->pending_cpu_trace.events[i];

        if (event->cycle_offset != machine->pending_cpu_elapsed) {
            break;
        }
        if (event->kind == C64_CPU_BUS_EVENT_WRITE) {
            return true;
        }
    }

    return false;
}

static bool c64_cpu_cycle_stalled_by_ba(const c64_t *machine) {
    if (!vicii_ba_active(&machine->vic, machine->clock.cycle)) {
        return false;
    }

    if (!machine->pending_cpu_trace_active) {
        return true;
    }

    if (c64_pending_cpu_elapsed_has_read_event(machine)) {
        return true;
    }

    if (c64_pending_cpu_elapsed_has_write_event(machine)) {
        return false;
    }

    return true;
}

static void c64_prepare_deferred_cpu_trace(c64_t *machine) {
    c64_trace_reset(&machine->last_cpu_trace);
    machine->cpu_trace_start_cycle = machine->clock.cycle;
    machine->cpu_trace_start_cpu_cycle = machine->cpu.cpu.cycles;
    machine->cpu_bus_mode = C64_CPU_BUS_MODE_DEFER_WRITES;

    machine->last_cpu_trace.total_cycles = c6510_step(&machine->cpu);
    machine->last_cpu_trace.opcode_pc = machine->cpu.cpu.opcode_pc;

#if defined(C64M_DEBUG_CIA1_WRITES)
    if (machine->last_cpu_trace.opcode_pc == 0x3f7eu) {
        fprintf(stderr, "[CIA1@3F7E] timer_b.counter=$%04X CRB=$%02X (START=%d INMODE=%d)\n",
            (unsigned)machine->cia1.timer_b.counter,
            (unsigned)machine->cia1.registers[0x0f],
            (int)(machine->cia1.registers[0x0f] & 0x01u),
            (int)((machine->cia1.registers[0x0f] >> 5) & 0x03u));
    }
#endif
    if (machine->last_cpu_trace.total_cycles == 0) {
        machine->last_cpu_trace.total_cycles = 1;
    }

    machine->cpu_bus_mode = C64_CPU_BUS_MODE_IMMEDIATE;
    machine->pending_cpu_trace = machine->last_cpu_trace;
    machine->pending_cpu_event_index = 0;
    machine->pending_cpu_elapsed = 0;
    machine->pending_cpu_trace_active = true;
}

static bool c64_step_cycle_internal(c64_t *machine) {
    if (!machine->pending_cpu_trace_active && !vicii_ba_active(&machine->vic, machine->clock.cycle)) {
        c64_prepare_deferred_cpu_trace(machine);
    }

    if (machine->pending_cpu_trace_active && !c64_cpu_cycle_stalled_by_ba(machine)) {
        c64_apply_pending_cpu_events_at_elapsed(machine);
        c64_advance_one_cycle(machine);
        machine->pending_cpu_elapsed++;
        machine->clock.cpu_cycles++;

        if (machine->pending_cpu_elapsed >= machine->pending_cpu_trace.total_cycles) {
            c64_apply_pending_cpu_events_at_elapsed(machine);
            machine->pending_cpu_trace_active = false;
            machine->pending_cpu_event_index = 0;
            machine->pending_cpu_elapsed = 0;
        }
        return true;
    }

    c64_advance_one_cycle(machine);
    return true;
}

static void c64_finish_pending_cpu_trace(c64_t *machine) {
    while (machine->pending_cpu_trace_active) {
        c64_step_cycle_internal(machine);
    }
}

static uint8_t c64_cpu_read(void *user, uint16_t address) {
    c64_t *machine = user;
    uint8_t value;

    if (machine->cpu_bus_mode == C64_CPU_BUS_MODE_TIMED_IMMEDIATE) {
        uint64_t offset = machine->cpu.cpu.cycles - machine->cpu_trace_start_cpu_cycle;
        c64_advance_devices_to(machine, machine->cpu_trace_start_cycle + offset);
    }

    value = c64_bus_read(&machine->bus, address);

    if (machine->cpu_bus_mode != C64_CPU_BUS_MODE_IMMEDIATE) {
        c64_trace_append_event(machine, C64_CPU_BUS_EVENT_READ, address, value);
        if (machine->cpu_bus_mode == C64_CPU_BUS_MODE_DEFER_WRITES) {
            return value;
        }
    }

    c64_report_memory_access(machine, C64_MEMORY_ACCESS_READ, address, value);
    return value;
}

static void c64_cpu_write(void *user, uint16_t address, uint8_t value) {
    c64_t *machine = user;

    if (machine->cpu_bus_mode == C64_CPU_BUS_MODE_TIMED_IMMEDIATE) {
        uint64_t offset = machine->cpu.cpu.cycles - machine->cpu_trace_start_cpu_cycle;
        c64_advance_devices_to(machine, machine->cpu_trace_start_cycle + offset);
        c64_bus_write(&machine->bus, address, value);
        c64_trace_append_event(machine, C64_CPU_BUS_EVENT_WRITE, address, value);
        c64_report_memory_access(machine, C64_MEMORY_ACCESS_WRITE, address, value);
        return;
    }

    if (machine->cpu_bus_mode == C64_CPU_BUS_MODE_DEFER_WRITES) {
        c64_trace_append_event(machine, C64_CPU_BUS_EVENT_WRITE, address, value);
        return;
    }

    c64_bus_write(&machine->bus, address, value);
    c64_report_memory_access(machine, C64_MEMORY_ACCESS_WRITE, address, value);
}

static uint8_t c64_cpu_irq_pending(void *user) {
    c64_t *machine = user;
    bool cia_irq = cia_irq_pending(&machine->cia1);
    bool vic_irq = (machine->vic.irq_status & machine->vic.irq_enable) != 0;
    return (cia_irq || vic_irq) ? 1u : 0u;
}

static uint8_t c64_cpu_nmi_pending(void *user) {
    c64_t *machine = user;
    bool pending = machine->restore_pending;
    machine->restore_pending = false;
    return pending ? 1u : 0u;
}

void c64_init(c64_t *machine) {
    char error[256];

    assert(machine);

    memset(machine, 0, sizeof(*machine));
    machine->config.video_standard = C64_VIDEO_STANDARD_NTSC;
    c64_bus_init(&machine->bus);
    (void)vicii_init(&machine->vic, error, sizeof(error));
    (void)cia_init(&machine->cia1, error, sizeof(error));
    (void)cia_init(&machine->cia2, error, sizeof(error));
    c64_keyboard_reset(&machine->keyboard);
    cia_attach_keyboard(&machine->cia1, &machine->keyboard);
    c64_bus_attach_vicii(&machine->bus, &machine->vic);
    c64_bus_attach_cias(&machine->bus, &machine->cia1, &machine->cia2);
    c6510_init(&machine->cpu, machine, c64_cpu_read, c64_cpu_write);
    c6510_set_irq_pending_callback(&machine->cpu, c64_cpu_irq_pending);
    c6510_set_nmi_pending_callback(&machine->cpu, c64_cpu_nmi_pending);
}

void c64_set_config(c64_t *machine, const c64_config *config) {
    assert(machine);

    if (config == NULL) {
        return;
    }

    machine->config = *config;
}

bool c64_install_roms(c64_t *machine, const c64_rom_set *roms, char *error, size_t error_size) {
    assert(machine);
    assert(roms);

    if (!roms->has_basic) {
        c64_set_error(error, error_size, "BASIC ROM missing");
        machine->ready = false;
        return false;
    }

    if (!roms->has_kernal) {
        c64_set_error(error, error_size, "KERNAL ROM missing");
        machine->ready = false;
        return false;
    }

    if (!roms->has_character) {
        c64_set_error(error, error_size, "character ROM missing");
        machine->ready = false;
        return false;
    }

    if (!c64_bus_set_basic_rom(&machine->bus, roms->basic, sizeof(roms->basic)) ||
        !c64_bus_set_kernal_rom(&machine->bus, roms->kernal, sizeof(roms->kernal)) ||
        !c64_bus_set_char_rom(&machine->bus, roms->character, sizeof(roms->character))) {
        c64_set_error(error, error_size, "failed to install ROM data");
        machine->ready = false;
        return false;
    }

    machine->has_basic_rom = true;
    machine->has_kernal_rom = true;
    machine->has_character_rom = true;
    machine->ready = true;
    c64_set_error(error, error_size, "");
    return true;
}

bool c64_reset(c64_t *machine, char *error, size_t error_size) {
    uint16_t vector;

    assert(machine);

    if (!machine->ready ||
        !machine->has_basic_rom ||
        !machine->has_kernal_rom ||
        !machine->has_character_rom) {
        c64_set_error(error, error_size, "machine cannot reset without BASIC, KERNAL, and character ROMs");
        return false;
    }

    c64_bus_reset(&machine->bus);
    vicii_reset(&machine->vic);
    vicii_set_video_standard(
        &machine->vic,
        machine->config.video_standard == C64_VIDEO_STANDARD_PAL ?
            VICII_VIDEO_STANDARD_PAL :
            VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&machine->vic, C64_VICII_REG_MEMORY_POINTER, 0x15);
    cia_reset(&machine->cia1);
    cia_reset(&machine->cia2);
    c64_keyboard_reset(&machine->keyboard);

    c6510_reset(&machine->cpu);
    memset(&machine->clock, 0, sizeof(machine->clock));
    memset(&machine->working_frame, 0, sizeof(machine->working_frame));
    c64_trace_reset(&machine->last_cpu_trace);
    c64_trace_reset(&machine->pending_cpu_trace);
    machine->keyboard_events = 0;
    machine->restore_requests = 0;
    machine->restore_pending = false;
    machine->cpu_cycles_remaining = 0;
    machine->cpu_trace_start_cycle = 0;
    machine->cpu_trace_start_cpu_cycle = 0;
    machine->pending_cpu_event_index = 0;
    machine->pending_cpu_elapsed = 0;
    machine->cpu_bus_mode = C64_CPU_BUS_MODE_IMMEDIATE;
    machine->pending_cpu_trace_active = false;
    vector = (uint16_t)c64_bus_read(&machine->bus, 0xfffc) |
        ((uint16_t)c64_bus_read(&machine->bus, 0xfffd) << 8);

    if (machine->cpu.cpu.pc != vector) {
        c64_set_error(error, error_size, "CPU reset vector mismatch");
        return false;
    }

    c64_set_error(error, error_size, "");
    return true;
}

bool c64_step_instruction(c64_t *machine, char *error, size_t error_size) {
    uint64_t start_cycle;
    size_t total_cycles;

    assert(machine);

    if (!machine->ready) {
        c64_set_error(error, error_size, "machine is not ready");
        return false;
    }

    c64_finish_pending_cpu_trace(machine);

    start_cycle = machine->clock.cycle;
    c64_trace_reset(&machine->last_cpu_trace);
    machine->cpu_trace_start_cycle = start_cycle;
    machine->cpu_trace_start_cpu_cycle = machine->cpu.cpu.cycles;
    machine->cpu_bus_mode = C64_CPU_BUS_MODE_TIMED_IMMEDIATE;
    total_cycles = c6510_step(&machine->cpu);
    machine->cpu_bus_mode = C64_CPU_BUS_MODE_IMMEDIATE;
    if (total_cycles == 0) {
        total_cycles = 1;
    }
    machine->last_cpu_trace.opcode_pc = machine->cpu.cpu.opcode_pc;
    machine->last_cpu_trace.total_cycles = total_cycles;
    c64_advance_devices_to(machine, start_cycle + total_cycles);
    machine->clock.cpu_cycles = machine->cpu.cpu.cycles;
    machine->cpu_cycles_remaining = 0;
    c64_set_error(error, error_size, "");
    return true;
}

bool c64_step_cycle(c64_t *machine, char *error, size_t error_size) {
    assert(machine);

    if (!machine->ready) {
        c64_set_error(error, error_size, "machine is not ready");
        return false;
    }

    c64_step_cycle_internal(machine);
    machine->cpu_cycles_remaining = machine->pending_cpu_trace_active ?
        machine->pending_cpu_trace.total_cycles - machine->pending_cpu_elapsed :
        0;
    c64_set_error(error, error_size, "");
    return true;
}

bool c64_generate_test_frame(c64_t *machine, c64_frame *out_frame) {
    return c64_make_frame_snapshot(machine, out_frame);
}

bool c64_make_frame_snapshot(c64_t *machine, c64_frame *out_frame) {
    assert(machine);
    assert(out_frame);

    return vicii_make_frame_snapshot(&machine->vic, &machine->bus, out_frame, machine->clock.cycle);
}

bool c64_copy_completed_frame(c64_t *machine, c64_frame *out_frame) {
    assert(machine);
    assert(out_frame);

    return vicii_copy_completed_frame(&machine->vic, out_frame, machine->clock.cycle);
}

bool c64_consume_frame_complete(c64_t *machine) {
    assert(machine);

    return vicii_consume_frame_complete(&machine->vic);
}

void c64_set_key(c64_t *machine, c64_key key, bool pressed) {
    assert(machine);

    c64_keyboard_set_key(&machine->keyboard, key, pressed);
    machine->keyboard_events++;
}

void c64_restore(c64_t *machine) {
    assert(machine);

    machine->restore_requests++;
    machine->restore_pending = true;
}

void c64_set_memory_access_callback(c64_t *machine, c64_memory_access_fn callback, void *user) {
    assert(machine);

    machine->memory_access = callback;
    machine->memory_access_user = user;
}

void c64_copy_cpu_snapshot(const c64_t *machine, c64_cpu_snapshot *out) {
    assert(machine);
    assert(out);

    out->pc = machine->cpu.cpu.pc;
    out->a = machine->cpu.cpu.A;
    out->x = machine->cpu.cpu.X;
    out->y = machine->cpu.cpu.Y;
    out->sp = (uint8_t)(machine->cpu.cpu.sp & 0xff);
    out->p = machine->cpu.cpu.flags;
    out->cycles = machine->cpu.cpu.cycles;
}

void c64_copy_machine_snapshot(const c64_t *machine, c64_machine_snapshot *out) {
    assert(machine);
    assert(out);

    out->cycle = machine->clock.cycle;
    out->cpu_cycles = machine->clock.cpu_cycles;
    out->vic_cycles = machine->clock.vic_cycles;
    out->cia_cycles = machine->clock.cia_cycles;
    out->pc = machine->cpu.cpu.pc;
    out->a = machine->cpu.cpu.A;
    out->x = machine->cpu.cpu.X;
    out->y = machine->cpu.cpu.Y;
    out->sp = (uint8_t)(machine->cpu.cpu.sp & 0xff);
    out->p = machine->cpu.cpu.flags;
    out->ready = machine->ready;
    out->screen_ram_writes = machine->bus.screen_ram_writes;
    out->color_ram_writes = machine->bus.color_ram_writes;
    out->vic_register_writes = machine->bus.vic_register_writes;
    out->cia1_register_writes = machine->bus.cia1_register_writes;
    out->cia2_register_writes = machine->bus.cia2_register_writes;
    out->keyboard_events = machine->keyboard_events;
    out->irq_entries = machine->cpu.cpu.irq_entries;
    out->cia1_icr_reads = machine->cia1.icr_reads;
    out->cia1_icr_writes = machine->cia1.icr_writes;
    out->cia1_interrupt_assertions = machine->cia1.interrupt_assertions;
    out->nmi_entries = machine->cpu.cpu.nmi_entries;
    out->restore_requests = machine->restore_requests;
    out->cia1_irq_pending = cia_irq_pending(&machine->cia1);
    out->cia2_nmi_pending = cia_irq_pending(&machine->cia2);
}

void c64_copy_vicii_snapshot(const c64_t *machine, c64_vicii_snapshot *out) {
    assert(machine);
    assert(out);

    vicii_copy_snapshot(&machine->vic, out);
}

static bool c64_debug_basic_visible(const c64_t *machine) {
    return (machine->bus.cpu_port_data & 0x03u) == 0x03u;
}

static bool c64_debug_kernal_visible(const c64_t *machine) {
    return (machine->bus.cpu_port_data & 0x02u) != 0;
}

static bool c64_debug_io_or_char_visible(const c64_t *machine) {
    return (machine->bus.cpu_port_data & 0x03u) != 0;
}

static bool c64_debug_io_visible(const c64_t *machine) {
    return c64_debug_io_or_char_visible(machine) && (machine->bus.cpu_port_data & 0x04u) != 0;
}

static bool c64_debug_char_visible(const c64_t *machine) {
    return c64_debug_io_or_char_visible(machine) && (machine->bus.cpu_port_data & 0x04u) == 0;
}

static uint8_t c64_debug_peek_io(const c64_t *machine, uint16_t address) {
    if (address >= 0xd000 && address <= 0xd3ff) {
        return vicii_debug_read_register(&machine->vic, address);
    }

    if (address >= 0xd800 && address <= 0xdbff) {
        return (uint8_t)(machine->bus.color_ram[address - 0xd800u] & 0x0fu);
    }

    if (address >= 0xdc00 && address <= 0xdcff) {
        return cia_debug_read_register(&machine->cia1, address);
    }

    if (address >= 0xdd00 && address <= 0xddff) {
        return cia_debug_read_register(&machine->cia2, address);
    }

    return 0xff;
}

c64_memory_visibility c64_memory_visibility_at(const c64_t *machine, uint16_t address) {
    assert(machine);

    if (address >= 0xa000 && address <= 0xbfff && c64_debug_basic_visible(machine)) {
        return C64_MEMORY_VISIBILITY_ROM;
    }

    if (address >= 0xd000 && address <= 0xdfff) {
        if (c64_debug_io_visible(machine)) {
            return C64_MEMORY_VISIBILITY_IO;
        }

        if (c64_debug_char_visible(machine)) {
            return C64_MEMORY_VISIBILITY_ROM;
        }
    }

    if (address >= 0xe000 && c64_debug_kernal_visible(machine)) {
        return C64_MEMORY_VISIBILITY_ROM;
    }

    return C64_MEMORY_VISIBILITY_RAM;
}

size_t c64_debug_copy_last_cpu_trace(const c64_t *machine, c64_cpu_instruction_trace *out) {
    assert(machine);
    assert(out);

    *out = machine->last_cpu_trace;
    return out->event_count;
}

uint8_t c64_debug_read_cpu_map(const c64_t *machine, uint16_t address) {
    assert(machine);

    if (address == 0x0000u) {
        return machine->bus.cpu_port_direction;
    }

    if (address == 0x0001u) {
        return machine->bus.cpu_port_data;
    }

    if (address >= 0xa000 && address <= 0xbfff && c64_debug_basic_visible(machine)) {
        return machine->bus.basic_rom[address - 0xa000u];
    }

    if (address >= 0xd000 && address <= 0xdfff) {
        if (c64_debug_io_visible(machine)) {
            return c64_debug_peek_io(machine, address);
        }

        if (c64_debug_char_visible(machine)) {
            return machine->bus.char_rom[address - 0xd000u];
        }
    }

    if (address >= 0xe000 && c64_debug_kernal_visible(machine)) {
        return machine->bus.kernal_rom[address - 0xe000u];
    }

    return machine->bus.ram[address];
}

uint8_t c64_debug_read_ram(const c64_t *machine, uint16_t address) {
    assert(machine);

    return machine->bus.ram[address];
}

void c64_debug_write_cpu_map(c64_t *machine, uint16_t address, uint8_t value) {
    assert(machine);

    c64_bus_write(&machine->bus, address, value);
}

void c64_debug_write_ram(c64_t *machine, uint16_t address, uint8_t value) {
    assert(machine);

    machine->bus.ram[address] = value;
    if (address >= 0x0400u && address < 0x0400u + 1000u) {
        machine->bus.screen_ram_writes++;
    }
}
