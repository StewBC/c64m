#include "c64.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum {
    C64_VICII_REG_MEMORY_POINTER = 0x18,
    C64_KERNAL_LOAD_ENTRY = 0xffd5,
    C64_ZP_STATUS = 0x90,
    C64_ZP_EAL = 0xae,
    C64_ZP_FILENAME_LENGTH = 0xb7,
    C64_ZP_SECONDARY_ADDRESS = 0xb9,
    C64_ZP_DEVICE_NUMBER = 0xba,
    C64_ZP_FILENAME_POINTER = 0xbb,
    C64_BASIC_START_POINTER = 0x2b,
    C64_BASIC_VARTAB_POINTER = 0x2d,
    C64_BASIC_ARYTAB_POINTER = 0x2f,
    C64_BASIC_STREND_POINTER = 0x31,
    C64_CPU_BUS_MODE_IMMEDIATE = 0,
    C64_CPU_BUS_MODE_TIMED_IMMEDIATE,
    C64_CPU_BUS_MODE_DEFER_WRITES,
};

static const uint8_t c64_d64_sectors_per_track[35] = {
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    19, 19, 19, 19, 19, 19, 19,
    18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 17
};

uint32_t c64_config_clock_hz(const c64_config *config) {
    if (config != NULL && config->video_standard == C64_VIDEO_STANDARD_NTSC) {
        return 1022727u;
    }
    return 985248u;
}

static void c64_set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) {
        return;
    }

    snprintf(error, error_size, "%s", message);
}

static int c64_drive_slot_index(uint8_t device) {
    if (device < C64_DRIVE_MIN_DEVICE || device > C64_DRIVE_MAX_DEVICE) {
        return -1;
    }
    return (int)(device - C64_DRIVE_MIN_DEVICE);
}

static void c64_copy_text(char *target, size_t target_size, const char *source) {
    if (target == NULL || target_size == 0) {
        return;
    }
    if (source == NULL) {
        target[0] = '\0';
        return;
    }
    snprintf(target, target_size, "%s", source);
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
    sid_advance_cycles(&machine->sid, 1);
}

static void c64_configure_cia_tod(c64_t *machine) {
    uint64_t pal_tenth = (uint64_t)VICII_PAL_CYCLES_PER_LINE * VICII_PAL_LINES_PER_FRAME * 5u;
    uint64_t ntsc_tenth = (uint64_t)VICII_NTSC_CYCLES_PER_LINE * VICII_NTSC_LINES_PER_FRAME * 6u;

    cia_set_tod_cycles(&machine->cia1, pal_tenth, ntsc_tenth);
    cia_set_tod_cycles(&machine->cia2, pal_tenth, ntsc_tenth);
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

static bool c64_d64_track_sector_offset(uint8_t track, uint8_t sector, size_t *out_offset) {
    size_t offset = 0;
    uint8_t current_track;

    if (out_offset == NULL || track < 1 || track > 35) {
        return false;
    }
    if (sector >= c64_d64_sectors_per_track[track - 1]) {
        return false;
    }
    for (current_track = 1; current_track < track; ++current_track) {
        offset += (size_t)c64_d64_sectors_per_track[current_track - 1] * 256u;
    }
    offset += (size_t)sector * 256u;
    if (offset + 256u > C64_DRIVE_D64_STANDARD_SIZE) {
        return false;
    }
    *out_offset = offset;
    return true;
}

static uint8_t c64_ascii_upper(uint8_t value) {
    if (value >= 'a' && value <= 'z') {
        return (uint8_t)(value - ('a' - 'A'));
    }
    return value;
}

static bool c64_drive_filename_matches(
    const c64_drive_directory_entry *entry,
    const uint8_t *name,
    size_t name_length) {
    size_t i;

    while (name_length >= 2 && name[0] == '"' && name[name_length - 1] == '"') {
        name++;
        name_length -= 2;
    }

    if (entry->filename_length != name_length) {
        return false;
    }
    for (i = 0; i < name_length; ++i) {
        if (c64_ascii_upper(entry->filename[i]) != c64_ascii_upper(name[i])) {
            return false;
        }
    }
    return true;
}

static bool c64_drive_pattern_has_wildcard(const uint8_t *name, size_t name_length) {
    size_t i;

    for (i = 0; i < name_length; ++i) {
        if (name[i] == '*' || name[i] == '?') {
            return true;
        }
    }
    return false;
}

static bool c64_drive_filename_pattern_matches(
    const c64_drive_directory_entry *entry,
    const uint8_t *pattern,
    size_t pattern_length) {
    size_t name_index = 0;
    size_t pattern_index = 0;

    while (pattern_length >= 2 && pattern[0] == '"' && pattern[pattern_length - 1] == '"') {
        pattern++;
        pattern_length -= 2;
    }

    while (pattern_index < pattern_length) {
        uint8_t pattern_char = pattern[pattern_index++];

        if (pattern_char == '*') {
            return true;
        }
        if (name_index >= entry->filename_length) {
            return false;
        }
        if (pattern_char != '?' &&
            c64_ascii_upper(entry->filename[name_index]) != c64_ascii_upper(pattern_char)) {
            return false;
        }
        name_index++;
    }

    return name_index == entry->filename_length;
}

static const c64_drive_directory_entry *c64_drive_find_entry(
    const c64_drive_slot *slot,
    const uint8_t *name,
    size_t name_length) {
    size_t i;
    bool wildcard = c64_drive_pattern_has_wildcard(name, name_length);

    for (i = 0; i < slot->entry_count; ++i) {
        if (wildcard && slot->entries[i].type != C64_DRIVE_FILE_PRG) {
            continue;
        }
        if (wildcard ?
            c64_drive_filename_pattern_matches(&slot->entries[i], name, name_length) :
            c64_drive_filename_matches(&slot->entries[i], name, name_length)) {
            return &slot->entries[i];
        }
    }
    return NULL;
}

static uint16_t c64_read_zp16(const c64_t *machine, uint16_t address) {
    return (uint16_t)machine->bus.ram[address] |
        ((uint16_t)machine->bus.ram[(uint16_t)(address + 1u)] << 8);
}

static void c64_write_zp16(c64_t *machine, uint16_t address, uint16_t value) {
    machine->bus.ram[address] = (uint8_t)(value & 0xffu);
    machine->bus.ram[(uint16_t)(address + 1u)] = (uint8_t)(value >> 8);
}

static void c64_kernal_load_return(
    c64_t *machine,
    bool success,
    uint8_t a,
    uint16_t end_address) {
    uint8_t lo;
    uint8_t hi;
    uint16_t return_address;

    machine->cpu.cpu.A = a;
    machine->cpu.cpu.X = (uint8_t)(end_address & 0xffu);
    machine->cpu.cpu.Y = (uint8_t)(end_address >> 8);
    if (success) {
        machine->cpu.cpu.flags &= (uint8_t)~0x01u;
        machine->bus.ram[C64_ZP_STATUS] = 0;
    } else {
        machine->cpu.cpu.flags |= 0x01u;
        machine->bus.ram[C64_ZP_STATUS] = 0x40;
    }

    machine->cpu.cpu.sp++;
    if (machine->cpu.cpu.sp >= 0x200) {
        machine->cpu.cpu.sp = 0x100;
    }
    lo = machine->bus.ram[machine->cpu.cpu.sp];
    machine->cpu.cpu.sp++;
    if (machine->cpu.cpu.sp >= 0x200) {
        machine->cpu.cpu.sp = 0x100;
    }
    hi = machine->bus.ram[machine->cpu.cpu.sp];
    return_address = (uint16_t)lo | ((uint16_t)hi << 8);
    machine->cpu.cpu.pc = (uint16_t)(return_address + 1u);
}

static bool c64_drive_load_prg_to_memory(
    c64_t *machine,
    const c64_drive_slot *slot,
    const c64_drive_directory_entry *entry,
    bool use_prg_address,
    uint16_t *out_end_address) {
    bool visited[683];
    uint8_t track;
    uint8_t sector_id;
    uint16_t load_address = 0;
    uint16_t target_address = 0;
    uint32_t written = 0;
    bool have_load_address = false;
    bool have_target_address = false;
    size_t offset;
    size_t visited_index;

    memset(visited, 0, sizeof(visited));
    track = entry->first_track;
    sector_id = entry->first_sector;

    while (track != 0) {
        const uint8_t *sector;
        size_t data_size;
        size_t data_index;

        if (!c64_d64_track_sector_offset(track, sector_id, &offset)) {
            return false;
        }
        visited_index = offset / 256u;
        if (visited_index >= sizeof(visited) / sizeof(visited[0]) || visited[visited_index]) {
            return false;
        }
        visited[visited_index] = true;

        sector = &slot->image_bytes[offset];
        data_size = sector[0] == 0 ? (sector[1] <= 1 ? 0 : (size_t)sector[1] - 1u) : 254u;
        if (data_size > 254u) {
            data_size = 254u;
        }

        for (data_index = 0; data_index < data_size; ++data_index) {
            uint8_t value = sector[2u + data_index];

            if (!have_load_address) {
                load_address = value;
                have_load_address = true;
                continue;
            }
            if (!have_target_address) {
                load_address |= (uint16_t)value << 8;
                target_address = use_prg_address ?
                    load_address :
                    c64_read_zp16(machine, C64_BASIC_START_POINTER);
                have_target_address = true;
                continue;
            }

            if ((uint32_t)target_address + written > 0xffffu) {
                return false;
            }
            c64_bus_write(&machine->bus, (uint16_t)(target_address + written), value);
            written++;
        }

        if (sector[0] == 0) {
            break;
        }
        track = sector[0];
        sector_id = sector[1];
    }

    if (!have_target_address) {
        return false;
    }
    if ((uint32_t)target_address + written > 0x10000u) {
        return false;
    }

    *out_end_address = (uint16_t)(target_address + written);
    if (!use_prg_address) {
        c64_write_zp16(machine, C64_BASIC_VARTAB_POINTER, *out_end_address);
        c64_write_zp16(machine, C64_BASIC_ARYTAB_POINTER, *out_end_address);
        c64_write_zp16(machine, C64_BASIC_STREND_POINTER, *out_end_address);
    }
    c64_write_zp16(machine, C64_ZP_EAL, *out_end_address);
    return true;
}

static const char *c64_drive_file_type_text(c64_drive_file_type type) {
    switch (type) {
    case C64_DRIVE_FILE_DEL:
        return "DEL";
    case C64_DRIVE_FILE_SEQ:
        return "SEQ";
    case C64_DRIVE_FILE_PRG:
        return "PRG";
    case C64_DRIVE_FILE_USR:
        return "USR";
    case C64_DRIVE_FILE_REL:
        return "REL";
    case C64_DRIVE_FILE_UNKNOWN:
    default:
        return "???";
    }
}

static char c64_directory_name_char(uint8_t value) {
    if (value >= 0x20 && value <= 0x7e) {
        return (char)value;
    }
    if (value >= 0xc1 && value <= 0xda) {
        return (char)('A' + (value - 0xc1));
    }
    return '?';
}

static void c64_directory_entry_name_ascii(
    const c64_drive_directory_entry *entry,
    char *out,
    size_t out_size) {
    size_t i;
    size_t count;

    if (out == NULL || out_size == 0) {
        return;
    }
    count = entry->filename_length;
    if (count + 1u > out_size) {
        count = out_size - 1u;
    }
    for (i = 0; i < count; ++i) {
        out[i] = c64_directory_name_char(entry->filename[i]);
    }
    out[count] = '\0';
}

static bool c64_basic_append_line(
    uint8_t *program,
    size_t capacity,
    size_t *offset,
    uint16_t base_address,
    uint16_t line_number,
    const char *format,
    ...) {
    char text[96];
    va_list args;
    int length;
    size_t start;
    size_t next;

    va_start(args, format);
    length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(text)) {
        return false;
    }

    start = *offset;
    next = start + 4u + (size_t)length + 1u;
    if (next + 2u > capacity || next > 0xffffu) {
        return false;
    }

    program[start + 0u] = (uint8_t)((base_address + next) & 0xffu);
    program[start + 1u] = (uint8_t)((base_address + next) >> 8);
    program[start + 2u] = (uint8_t)(line_number & 0xffu);
    program[start + 3u] = (uint8_t)(line_number >> 8);
    memcpy(&program[start + 4u], text, (size_t)length);
    program[start + 4u + (size_t)length] = 0;
    *offset = next;
    return true;
}

static bool c64_drive_load_directory_to_memory(
    c64_t *machine,
    const c64_drive_slot *slot,
    uint16_t *out_end_address) {
    uint8_t *program;
    size_t offset = 0;
    size_t i;
    uint16_t start_address;
    uint16_t line_number = 10;
    char title[C64_DRIVE_DISK_TITLE_MAX];
    char id[3];
    char dos[3];
    bool ok = true;

    program = (uint8_t *)malloc(32768u);
    if (program == NULL) {
        return false;
    }

    c64_copy_text(title, sizeof(title), slot->disk_title[0] != '\0' ? slot->disk_title : "                ");
    c64_copy_text(id, sizeof(id), slot->disk_id);
    c64_copy_text(dos, sizeof(dos), slot->dos_type);
    if (id[0] == '\0') {
        c64_copy_text(id, sizeof(id), "  ");
    }
    if (dos[0] == '\0') {
        c64_copy_text(dos, sizeof(dos), "  ");
    }

    start_address = c64_read_zp16(machine, C64_BASIC_START_POINTER);

    ok = c64_basic_append_line(program, 32768u, &offset, start_address, 0, "\"%s\" %s %s", title, id, dos);
    for (i = 0; ok && i < slot->entry_count; ++i) {
        char name[17];
        c64_directory_entry_name_ascii(&slot->entries[i], name, sizeof(name));
        ok = c64_basic_append_line(
            program,
            32768u,
            &offset,
            start_address,
            line_number,
            "%u \"%s\" %s",
            slot->entries[i].block_count,
            name,
            c64_drive_file_type_text(slot->entries[i].type));
        line_number = (uint16_t)(line_number + 10u);
    }
    if (ok) {
        ok = c64_basic_append_line(
            program,
            32768u,
            &offset,
            start_address,
            line_number,
            "%u BLOCKS FREE.",
            slot->free_blocks);
    }
    if (ok && offset + 2u <= 32768u) {
        program[offset++] = 0;
        program[offset++] = 0;
    } else {
        ok = false;
    }

    if (ok && (uint32_t)start_address + offset <= 0x10000u) {
        for (i = 0; i < offset; ++i) {
            c64_bus_write(&machine->bus, (uint16_t)(start_address + i), program[i]);
        }
        *out_end_address = (uint16_t)(start_address + offset);
        c64_write_zp16(machine, C64_BASIC_VARTAB_POINTER, *out_end_address);
        c64_write_zp16(machine, C64_BASIC_ARYTAB_POINTER, *out_end_address);
        c64_write_zp16(machine, C64_BASIC_STREND_POINTER, *out_end_address);
        c64_write_zp16(machine, C64_ZP_EAL, *out_end_address);
    } else {
        ok = false;
    }

    free(program);
    return ok;
}

static bool c64_try_kernal_load_trap(c64_t *machine) {
    uint8_t device;
    uint8_t secondary_address;
    uint8_t filename_length;
    uint16_t filename_pointer;
    uint8_t filename[16];
    c64_drive_slot *slot;
    const c64_drive_directory_entry *entry;
    uint16_t end_address = 0;
    size_t i;

    if (machine->cpu.cpu.pc != C64_KERNAL_LOAD_ENTRY) {
        return false;
    }

    device = machine->bus.ram[C64_ZP_DEVICE_NUMBER];
    if (!c64_drive_device_supported(device)) {
        return false;
    }

    filename_length = machine->bus.ram[C64_ZP_FILENAME_LENGTH];
    filename_pointer = c64_read_zp16(machine, C64_ZP_FILENAME_POINTER);
    secondary_address = machine->bus.ram[C64_ZP_SECONDARY_ADDRESS];
    if (machine->cpu.cpu.A != 0 || (secondary_address != 0 && secondary_address != 1)) {
        c64_kernal_load_return(machine, false, 0x05, 0);
        return true;
    }
    if (filename_length == 0 || filename_length > sizeof(filename)) {
        c64_kernal_load_return(machine, false, 0x04, 0);
        return true;
    }
    for (i = 0; i < filename_length; ++i) {
        filename[i] = c64_debug_read_cpu_map(machine, (uint16_t)(filename_pointer + i));
    }

    slot = &machine->drives[device - C64_DRIVE_MIN_DEVICE];
    if (!slot->mounted || slot->image_kind != C64_DRIVE_IMAGE_D64 || slot->image_bytes == NULL) {
        c64_kernal_load_return(machine, false, 0x05, 0);
        return true;
    }

    if (filename_length == 1 && filename[0] == '$') {
        if (!c64_drive_load_directory_to_memory(machine, slot, &end_address)) {
            c64_kernal_load_return(machine, false, 0x05, 0);
            return true;
        }
        c64_kernal_load_return(machine, true, 0, end_address);
        return true;
    }

    entry = c64_drive_find_entry(slot, filename, filename_length);
    if (entry == NULL || entry->type != C64_DRIVE_FILE_PRG) {
        c64_kernal_load_return(machine, false, 0x04, 0);
        return true;
    }

    if (!c64_drive_load_prg_to_memory(machine, slot, entry, secondary_address == 1, &end_address)) {
        c64_kernal_load_return(machine, false, 0x05, 0);
        return true;
    }

    c64_kernal_load_return(machine, true, 0, end_address);
    return true;
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
        event->value = c64_bus_read(&machine->bus, event->address);
        c64_report_memory_access(machine, C64_MEMORY_ACCESS_READ, event->address, event->value);
        break;

    case C64_CPU_BUS_EVENT_WRITE:
        c64_bus_write(&machine->bus, event->address, event->value);
        c64_report_memory_access(machine, C64_MEMORY_ACCESS_WRITE, event->address, event->value);
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
    if (!machine->pending_cpu_trace_active && c64_try_kernal_load_trap(machine)) {
        machine->instruction_complete = true;
        machine->clock.cpu_cycles++;
        machine->clock.cycle++;
        return true;
    }

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
            machine->instruction_complete = true;
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

    if (machine->cpu_bus_mode == C64_CPU_BUS_MODE_DEFER_WRITES) {
        value = c64_debug_read_cpu_map(machine, address);
        c64_trace_append_event(machine, C64_CPU_BUS_EVENT_READ, address, value);
        return value;
    }

    if (machine->cpu_bus_mode == C64_CPU_BUS_MODE_TIMED_IMMEDIATE) {
        uint64_t offset = machine->cpu.cpu.cycles - machine->cpu_trace_start_cpu_cycle;
        c64_advance_devices_to(machine, machine->cpu_trace_start_cycle + offset);
    }

    value = c64_bus_read(&machine->bus, address);

    if (machine->cpu_bus_mode != C64_CPU_BUS_MODE_IMMEDIATE) {
        c64_trace_append_event(machine, C64_CPU_BUS_EVENT_READ, address, value);
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
    bool cia2_line = cia_irq_pending(&machine->cia2);
    bool cia2_edge = cia2_line && !machine->cia2_nmi_line;
    bool pending = machine->restore_pending || cia2_edge;
    machine->cia2_nmi_line = cia2_line;
    machine->restore_pending = false;
    return pending ? 1u : 0u;
}

static void c64_cia1_port_inputs(
    void *user,
    uint8_t port_a_pins,
    uint8_t port_b_pins,
    cia_port_inputs *out) {
    c64_t *machine = user;
    uint8_t selected_rows;
    uint8_t selected_columns;

    assert(machine);
    assert(out);

    selected_rows = (uint8_t)~port_a_pins;
    selected_columns = (uint8_t)~port_b_pins;

    out->port_b_pull_down |= (uint8_t)~c64_keyboard_read_columns(&machine->keyboard, selected_rows);
    out->port_a_pull_down |= (uint8_t)~c64_keyboard_read_rows(&machine->keyboard, selected_columns);
    out->port_b_pull_down |= (uint8_t)(machine->joystick1 & 0x1fu);
    out->port_a_pull_down |= (uint8_t)(machine->joystick2 & 0x1fu);
}

static void c64_cia2_port_inputs(
    void *user,
    uint8_t port_a_pins,
    uint8_t port_b_pins,
    cia_port_inputs *out) {
    c64_t *machine = user;
    uint8_t pull = 0;

    (void)port_b_pins;
    assert(machine);
    assert(out);

    if ((port_a_pins & 0x08u) == 0) {
        pull |= C64_IEC_ATN;
    }
    if ((port_a_pins & 0x10u) == 0) {
        pull |= C64_IEC_CLK;
    }
    if ((port_a_pins & 0x20u) == 0) {
        pull |= C64_IEC_DATA;
    }

    pull |= (uint8_t)(machine->iec_external_pull & (C64_IEC_ATN | C64_IEC_CLK | C64_IEC_DATA));

    if ((pull & C64_IEC_ATN) != 0) {
        out->port_a_pull_down |= 0x08u;
    }
    if ((pull & C64_IEC_CLK) != 0) {
        out->port_a_pull_down |= 0x10u | 0x40u;
    }
    if ((pull & C64_IEC_DATA) != 0) {
        out->port_a_pull_down |= 0x20u | 0x80u;
    }
}

void c64_init(c64_t *machine) {
    char error[256];
    uint8_t device;

    assert(machine);

    memset(machine, 0, sizeof(*machine));
    for (device = C64_DRIVE_MIN_DEVICE; device <= C64_DRIVE_MAX_DEVICE; ++device) {
        machine->drives[device - C64_DRIVE_MIN_DEVICE].last_result = C64_DRIVE_STATUS_NOT_MOUNTED;
    }
    machine->config.video_standard = C64_VIDEO_STANDARD_NTSC;
    c64_bus_init(&machine->bus);
    (void)vicii_init(&machine->vic, error, sizeof(error));
    (void)cia_init(&machine->cia1, error, sizeof(error));
    (void)cia_init(&machine->cia2, error, sizeof(error));
    c64_configure_cia_tod(machine);
    c64_keyboard_reset(&machine->keyboard);
    cia_attach_port_input(&machine->cia1, c64_cia1_port_inputs, machine);
    cia_attach_port_input(&machine->cia2, c64_cia2_port_inputs, machine);
    sid_init(&machine->sid);
    c64_bus_attach_vicii(&machine->bus, &machine->vic);
    c64_bus_attach_cias(&machine->bus, &machine->cia1, &machine->cia2);
    c64_bus_attach_sid(&machine->bus, &machine->sid);
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
    sid_reset(&machine->sid);
    vicii_reset(&machine->vic);
    vicii_set_video_standard(
        &machine->vic,
        machine->config.video_standard == C64_VIDEO_STANDARD_PAL ?
            VICII_VIDEO_STANDARD_PAL :
            VICII_VIDEO_STANDARD_NTSC);
    vicii_write_register(&machine->vic, C64_VICII_REG_MEMORY_POINTER, 0x15);
    cia_reset(&machine->cia1);
    cia_reset(&machine->cia2);
    c64_configure_cia_tod(machine);
    c64_keyboard_reset(&machine->keyboard);
    machine->joystick1 = 0;
    machine->joystick2 = 0;
    machine->iec_external_pull = 0;

    c6510_reset(&machine->cpu);
    memset(&machine->clock, 0, sizeof(machine->clock));
    memset(&machine->working_frame, 0, sizeof(machine->working_frame));
    c64_trace_reset(&machine->last_cpu_trace);
    c64_trace_reset(&machine->pending_cpu_trace);
    machine->keyboard_events = 0;
    machine->restore_requests = 0;
    machine->restore_pending = false;
    machine->cia2_nmi_line = false;
    machine->cpu_cycles_remaining = 0;
    machine->cpu_trace_start_cycle = 0;
    machine->cpu_trace_start_cpu_cycle = 0;
    machine->pending_cpu_event_index = 0;
    machine->pending_cpu_elapsed = 0;
    machine->cpu_bus_mode = C64_CPU_BUS_MODE_IMMEDIATE;
    machine->pending_cpu_trace_active = false;
    machine->instruction_complete = false;
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

    if (c64_try_kernal_load_trap(machine)) {
        machine->clock.cycle++;
        machine->clock.cpu_cycles++;
        machine->instruction_complete = true;
        c64_set_error(error, error_size, "");
        return true;
    }

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

bool c64_consume_instruction_complete(c64_t *machine) {
    assert(machine);

    if (!machine->instruction_complete) {
        return false;
    }
    machine->instruction_complete = false;
    return true;
}

void c64_set_key(c64_t *machine, c64_key key, bool pressed) {
    assert(machine);

    c64_keyboard_set_key(&machine->keyboard, key, pressed);
    machine->keyboard_events++;
}

void c64_set_joystick(c64_t *machine, unsigned port, uint8_t inputs) {
    assert(machine);

    if (port == 1u) {
        machine->joystick1 = (uint8_t)(inputs & 0x1fu);
    } else if (port == 2u) {
        machine->joystick2 = (uint8_t)(inputs & 0x1fu);
    }
}

void c64_set_iec_external_pull(c64_t *machine, uint8_t lines) {
    assert(machine);

    machine->iec_external_pull = (uint8_t)(lines & (C64_IEC_ATN | C64_IEC_CLK | C64_IEC_DATA));
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

bool c64_drive_device_supported(uint8_t device) {
    return c64_drive_slot_index(device) >= 0;
}

c64_drive_status_result c64_mount_d64(
    c64_t *machine,
    uint8_t device,
    const uint8_t *standard_image_bytes,
    size_t standard_image_size,
    const c64_drive_directory_entry *entries,
    size_t entry_count,
    const char *display_name,
    const char *disk_title,
    const char *disk_id,
    const char *dos_type,
    uint16_t free_blocks) {
    int slot_index;
    c64_drive_slot *slot;
    uint8_t *copy;
    c64_drive_directory_entry *entry_copy = NULL;

    assert(machine);

    slot_index = c64_drive_slot_index(device);
    if (slot_index < 0) {
        return C64_DRIVE_STATUS_INVALID_DEVICE;
    }
    if (standard_image_bytes == NULL || standard_image_size != C64_DRIVE_D64_STANDARD_SIZE) {
        machine->drives[slot_index].last_result = C64_DRIVE_STATUS_UNSUPPORTED_IMAGE;
        return C64_DRIVE_STATUS_UNSUPPORTED_IMAGE;
    }

    copy = (uint8_t *)malloc(standard_image_size);
    if (copy == NULL) {
        machine->drives[slot_index].last_result = C64_DRIVE_STATUS_OUT_OF_MEMORY;
        return C64_DRIVE_STATUS_OUT_OF_MEMORY;
    }
    if (entry_count > 0) {
        entry_copy = (c64_drive_directory_entry *)malloc(entry_count * sizeof(*entry_copy));
        if (entry_copy == NULL) {
            free(copy);
            machine->drives[slot_index].last_result = C64_DRIVE_STATUS_OUT_OF_MEMORY;
            return C64_DRIVE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(entry_copy, entries, entry_count * sizeof(*entry_copy));
    }
    memcpy(copy, standard_image_bytes, standard_image_size);

    slot = &machine->drives[slot_index];
    free(slot->image_bytes);
    free(slot->entries);
    memset(slot, 0, sizeof(*slot));
    slot->mounted = true;
    slot->image_kind = C64_DRIVE_IMAGE_D64;
    slot->last_result = C64_DRIVE_STATUS_OK;
    slot->image_bytes = copy;
    slot->image_size = standard_image_size;
    slot->entries = entry_copy;
    slot->entry_count = entry_count;
    c64_copy_text(slot->display_name, sizeof(slot->display_name), display_name);
    c64_copy_text(slot->disk_title, sizeof(slot->disk_title), disk_title);
    c64_copy_text(slot->disk_id, sizeof(slot->disk_id), disk_id);
    c64_copy_text(slot->dos_type, sizeof(slot->dos_type), dos_type);
    slot->free_blocks = free_blocks;
    return C64_DRIVE_STATUS_OK;
}

void c64_unmount_drive(c64_t *machine, uint8_t device) {
    int slot_index;
    c64_drive_slot *slot;

    assert(machine);

    slot_index = c64_drive_slot_index(device);
    if (slot_index < 0) {
        return;
    }

    slot = &machine->drives[slot_index];
    free(slot->image_bytes);
    free(slot->entries);
    memset(slot, 0, sizeof(*slot));
    slot->last_result = C64_DRIVE_STATUS_NOT_MOUNTED;
}

void c64_unmount_all_drives(c64_t *machine) {
    uint8_t device;

    assert(machine);

    for (device = C64_DRIVE_MIN_DEVICE; device <= C64_DRIVE_MAX_DEVICE; ++device) {
        c64_unmount_drive(machine, device);
    }
}

bool c64_copy_drive_status(const c64_t *machine, uint8_t device, c64_drive_status *out_status) {
    int slot_index;
    const c64_drive_slot *slot;

    assert(machine);

    if (out_status == NULL) {
        return false;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->device = device;
    slot_index = c64_drive_slot_index(device);
    if (slot_index < 0) {
        out_status->last_result = C64_DRIVE_STATUS_INVALID_DEVICE;
        return false;
    }

    slot = &machine->drives[slot_index];
    out_status->mounted = slot->mounted;
    out_status->image_kind = slot->image_kind;
    out_status->last_result = slot->last_result;
    c64_copy_text(out_status->display_name, sizeof(out_status->display_name), slot->display_name);
    c64_copy_text(out_status->disk_title, sizeof(out_status->disk_title), slot->disk_title);
    return true;
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
