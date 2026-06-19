#pragma once

#include "c64_bus.h"
#include "c64_frame.h"
#include "c64_rom.h"
#include "c6510.h"
#include "cia.h"
#include "keyboard.h"
#include "vicii.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct c64_cpu_snapshot {
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint64_t cycles;
} c64_cpu_snapshot;

typedef struct c64_clock {
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
} c64_clock;

typedef enum c64_cpu_bus_event_kind {
    C64_CPU_BUS_EVENT_INTERNAL = 0,
    C64_CPU_BUS_EVENT_READ,
    C64_CPU_BUS_EVENT_WRITE
} c64_cpu_bus_event_kind;

typedef struct c64_cpu_bus_event {
    uint8_t cycle_offset;
    c64_cpu_bus_event_kind kind;
    uint16_t address;
    uint8_t value;
    uint8_t is_io;
    uint64_t absolute_cycle;
} c64_cpu_bus_event;

enum {
    C64_CPU_TRACE_MAX_EVENTS = 64
};

typedef struct c64_cpu_instruction_trace {
    uint16_t opcode_pc;
    size_t event_count;
    size_t total_cycles;
    c64_cpu_bus_event events[C64_CPU_TRACE_MAX_EVENTS];
} c64_cpu_instruction_trace;

typedef enum c64_video_standard {
    C64_VIDEO_STANDARD_NTSC = 0,
    C64_VIDEO_STANDARD_PAL
} c64_video_standard;

typedef enum c64_joystick_input {
    C64_JOYSTICK_UP = 0x01,
    C64_JOYSTICK_DOWN = 0x02,
    C64_JOYSTICK_LEFT = 0x04,
    C64_JOYSTICK_RIGHT = 0x08,
    C64_JOYSTICK_FIRE = 0x10
} c64_joystick_input;

typedef enum c64_iec_line {
    C64_IEC_ATN = 0x01,
    C64_IEC_CLK = 0x02,
    C64_IEC_DATA = 0x04
} c64_iec_line;

typedef struct c64_config {
    c64_video_standard video_standard;
} c64_config;

typedef struct c64_machine_snapshot {
    uint64_t cycle;
    uint64_t cpu_cycles;
    uint64_t vic_cycles;
    uint64_t cia_cycles;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    bool ready;
    uint64_t screen_ram_writes;
    uint64_t color_ram_writes;
    uint64_t vic_register_writes;
    uint64_t cia1_register_writes;
    uint64_t cia2_register_writes;
    uint64_t keyboard_events;
    uint64_t irq_entries;
    uint64_t cia1_icr_reads;
    uint64_t cia1_icr_writes;
    uint64_t cia1_interrupt_assertions;
    uint64_t nmi_entries;
    uint64_t restore_requests;
    bool cia1_irq_pending;
    bool cia2_nmi_pending;
} c64_machine_snapshot;

typedef enum c64_memory_access_type {
    C64_MEMORY_ACCESS_READ = 0,
    C64_MEMORY_ACCESS_WRITE
} c64_memory_access_type;

typedef enum c64_memory_visibility {
    C64_MEMORY_VISIBILITY_RAM = 0,
    C64_MEMORY_VISIBILITY_ROM,
    C64_MEMORY_VISIBILITY_IO
} c64_memory_visibility;

typedef void (*c64_memory_access_fn)(
    void *user,
    c64_memory_access_type access,
    uint16_t address,
    uint8_t value);

typedef struct c64_t {
    c64_bus_t bus;
    C6510 cpu;
    vicii vic;
    cia cia1;
    cia cia2;
    c64_keyboard keyboard;
    uint8_t joystick1;
    uint8_t joystick2;
    uint8_t iec_external_pull;
    c64_clock clock;
    c64_frame working_frame;
    uint64_t keyboard_events;
    uint64_t restore_requests;
    c64_memory_access_fn memory_access;
    void *memory_access_user;
    c64_cpu_instruction_trace last_cpu_trace;
    c64_cpu_instruction_trace pending_cpu_trace;
    uint64_t cpu_trace_start_cycle;
    uint64_t cpu_trace_start_cpu_cycle;
    size_t pending_cpu_event_index;
    size_t pending_cpu_elapsed;
    uint8_t cpu_bus_mode;
    bool pending_cpu_trace_active;
    bool instruction_complete;
    bool restore_pending;
    bool cia2_nmi_line;
    size_t cpu_cycles_remaining;
    bool has_basic_rom;
    bool has_kernal_rom;
    bool has_character_rom;
    bool ready;
    c64_config config;
} c64_t;

void c64_init(c64_t *machine);
void c64_set_config(c64_t *machine, const c64_config *config);
bool c64_install_roms(c64_t *machine, const c64_rom_set *roms, char *error, size_t error_size);
bool c64_reset(c64_t *machine, char *error, size_t error_size);
bool c64_step_instruction(c64_t *machine, char *error, size_t error_size);
bool c64_step_cycle(c64_t *machine, char *error, size_t error_size);
bool c64_generate_test_frame(c64_t *machine, c64_frame *out_frame);
bool c64_make_frame_snapshot(c64_t *machine, c64_frame *out_frame);
bool c64_copy_completed_frame(c64_t *machine, c64_frame *out_frame);
bool c64_consume_frame_complete(c64_t *machine);
bool c64_consume_instruction_complete(c64_t *machine);
void c64_set_key(c64_t *machine, c64_key key, bool pressed);
void c64_set_joystick(c64_t *machine, unsigned port, uint8_t inputs);
void c64_set_iec_external_pull(c64_t *machine, uint8_t lines);
void c64_restore(c64_t *machine);
void c64_set_memory_access_callback(c64_t *machine, c64_memory_access_fn callback, void *user);
void c64_copy_cpu_snapshot(const c64_t *machine, c64_cpu_snapshot *out);
void c64_copy_machine_snapshot(const c64_t *machine, c64_machine_snapshot *out);
void c64_copy_vicii_snapshot(const c64_t *machine, c64_vicii_snapshot *out);
uint8_t c64_debug_read_cpu_map(const c64_t *machine, uint16_t address);
uint8_t c64_debug_read_ram(const c64_t *machine, uint16_t address);
void c64_debug_write_cpu_map(c64_t *machine, uint16_t address, uint8_t value);
void c64_debug_write_ram(c64_t *machine, uint16_t address, uint8_t value);
c64_memory_visibility c64_memory_visibility_at(const c64_t *machine, uint16_t address);
size_t c64_debug_copy_last_cpu_trace(const c64_t *machine, c64_cpu_instruction_trace *out);
