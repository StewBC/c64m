#pragma once

#include "c1541.h"
#include "c64_bus.h"
#include "c64_frame.h"
#include "c64_rom.h"
#include "c6510.h"
#include "cia.h"
#include "keyboard.h"
#include "sid.h"
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
    /* Fractional 1541 clock accumulator. The 1541 runs at a fixed 1.000 MHz
       (16 MHz / 16), independent of the C64's 985248 Hz PAL / 1022727 Hz NTSC
       Phi2. Each C64 cycle adds 1e6 and every full c64_hz worth steps the drive
       once, so the drive runs faster than PAL and slower than NTSC, matching VICE. */
    uint64_t drive_accum;
    /* Number of C64 cycles' worth of drive credit already consumed. The drive is
       lazily advanced to the current cycle at each CIA2 IEC access (VICE's
       drive_catch_up_hook) and by a per-cycle backstop, so an IEC read/write
       samples the drive exactly at the access clock. */
    uint64_t drive_synced_cycle;
} c64_clock;

typedef enum c64_cpu_bus_event_kind {
    C64_CPU_BUS_EVENT_INTERNAL = 0,
    C64_CPU_BUS_EVENT_READ,
    C64_CPU_BUS_EVENT_WRITE
} c64_cpu_bus_event_kind;

typedef struct c64_cpu_bus_event {
    uint8_t cycle_offset;
    c64_cpu_bus_event_kind kind;
    c6510_bus_access_kind access_kind;
    uint16_t address;
    uint8_t value;
    uint8_t is_io;
    uint8_t record_write_history;
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
    int emulate_1541;   /* 1 = route disk I/O through genuine 1541 ROM; 0 = KERNAL trap */
    int media_1541;     /* 1 = GCR tracks/rotation/SYNC (opt-in media path) */
} c64_config;

/* Returns the master clock frequency in Hz for the given config.
   PAL: 985248 Hz.  NTSC: 1022727 Hz. */
uint32_t c64_config_clock_hz(const c64_config *config);

/* Returns the number of CPU cycles in one video frame for the given config.
   PAL: 19656 (63 x 312).  NTSC: 17095 (65 x 263).
   Dividing by c64_config_clock_hz gives the frame period, hence the frame rate
   (PAL ~50.12 fps, NTSC ~59.83 fps). */
uint32_t c64_config_cycles_per_frame(const c64_config *config);

typedef enum c64_drive_image_kind {
    C64_DRIVE_IMAGE_NONE = 0,
    C64_DRIVE_IMAGE_D64,
    C64_DRIVE_IMAGE_G64
} c64_drive_image_kind;

typedef enum c64_drive_status_result {
    C64_DRIVE_STATUS_OK = 0,
    C64_DRIVE_STATUS_INVALID_DEVICE,
    C64_DRIVE_STATUS_NOT_MOUNTED,
    C64_DRIVE_STATUS_UNSUPPORTED_IMAGE,
    C64_DRIVE_STATUS_PARSE_ERROR,
    C64_DRIVE_STATUS_IO_ERROR,
    C64_DRIVE_STATUS_OUT_OF_MEMORY,
    C64_DRIVE_STATUS_WRITE_PROTECTED,
    C64_DRIVE_STATUS_DISK_FULL,
    C64_DRIVE_STATUS_FILE_EXISTS
} c64_drive_status_result;

enum {
    C64_DRIVE_MIN_DEVICE = 8,
    C64_DRIVE_MAX_DEVICE = 9,
    C64_DRIVE_SLOT_COUNT = 2,
    C64_DRIVE_DISPLAY_NAME_MAX = 128,
    C64_DRIVE_DISK_TITLE_MAX = 17,
    C64_DRIVE_D64_STANDARD_SIZE = 174848
};

typedef struct c64_drive_status {
    uint8_t device;
    bool mounted;
    bool writable;
    bool dirty;
    c64_drive_image_kind image_kind;
    c64_drive_status_result last_result;
    char display_name[C64_DRIVE_DISPLAY_NAME_MAX];
    char disk_title[C64_DRIVE_DISK_TITLE_MAX];
} c64_drive_status;

typedef enum c64_drive_file_type {
    C64_DRIVE_FILE_DEL = 0,
    C64_DRIVE_FILE_SEQ = 1,
    C64_DRIVE_FILE_PRG = 2,
    C64_DRIVE_FILE_USR = 3,
    C64_DRIVE_FILE_REL = 4,
    C64_DRIVE_FILE_UNKNOWN = 255
} c64_drive_file_type;

typedef struct c64_drive_directory_entry {
    uint8_t raw_type;
    c64_drive_file_type type;
    uint8_t first_track;
    uint8_t first_sector;
    uint8_t filename[16];
    size_t filename_length;
    uint16_t block_count;
} c64_drive_directory_entry;

typedef struct c64_drive_slot {
    bool mounted;
    bool writable;
    bool dirty;
    c64_drive_image_kind image_kind;
    c64_drive_status_result last_result;
    char display_name[C64_DRIVE_DISPLAY_NAME_MAX];
    char disk_title[C64_DRIVE_DISK_TITLE_MAX];
    char disk_id[3];
    char dos_type[3];
    uint16_t free_blocks;
    uint8_t *image_bytes;
    size_t image_size;
    c64_drive_directory_entry *entries;
    size_t entry_count;
    /* UI disk LEDs: monotonic event counters; frontend holds on host time. */
    uint32_t led_read_seq;
    uint32_t led_write_seq;
    /* Bumped whenever image_bytes content changes so GCR media caches rebuild. */
    uint32_t image_content_seq;
} c64_drive_slot;

typedef enum c64_memory_visibility {
    C64_MEMORY_VISIBILITY_RAM = 0,
    C64_MEMORY_VISIBILITY_ROM,
    C64_MEMORY_VISIBILITY_IO
} c64_memory_visibility;

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
    uint64_t sid_register_writes;
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

typedef struct c64_memory_banking_snapshot {
    uint8_t cpu_port_direction;
    uint8_t cpu_port_data;
    bool loram;
    bool hiram;
    bool charen;
    c64_memory_visibility basic_visibility;
    c64_memory_visibility io_visibility;
    c64_memory_visibility kernal_visibility;
    uint8_t cia2_port_a_pins;
    uint8_t vic_bank_select;
    uint16_t vic_bank_base;
    uint8_t vic_memory_pointer;
    uint16_t vic_screen_base;
    uint16_t vic_character_base;
    uint16_t vic_bitmap_base;
} c64_memory_banking_snapshot;

typedef struct c64_vicii_hardware_snapshot {
    c64_video_standard standard;
    uint32_t raster_line;
    uint32_t cycle_in_line;
    uint32_t cycles_per_line;
    uint32_t lines_per_frame;
    uint64_t frame_number;
    uint16_t raster_compare;
    uint8_t control_1;
    uint8_t control_2;
    uint8_t memory_pointer;
    uint8_t irq_status;
    uint8_t irq_enable;
    bool irq_pending;
    uint8_t border_color;
    uint8_t background_color[4];
    bool display_state;
    bool bad_line;
    bool ba_active;
    bool aec_active;
    bool rdy_active;
    uint16_t vc;
    uint16_t vc_base;
    uint8_t rc;
    uint8_t sprite_enable;
    uint8_t sprite_x_expand;
    uint8_t sprite_y_expand;
    uint8_t sprite_multicolor;
    uint8_t sprite_priority;
    uint8_t sprite_sprite_collision;
    uint8_t sprite_background_collision;
} c64_vicii_hardware_snapshot;

typedef struct c64_cia_hardware_snapshot {
    uint8_t port_a;
    uint8_t port_b;
    uint8_t ddra;
    uint8_t ddrb;
    uint16_t timer_a_counter;
    uint16_t timer_a_latch;
    uint8_t timer_a_control;
    bool timer_a_underflow;
    uint16_t timer_b_counter;
    uint16_t timer_b_latch;
    uint8_t timer_b_control;
    bool timer_b_underflow;
    uint8_t interrupt_flags;
    uint8_t interrupt_mask;
    bool interrupt_pending;
    uint8_t tod_tenths;
    uint8_t tod_seconds;
    uint8_t tod_minutes;
    uint8_t tod_hours;
    uint8_t alarm_tenths;
    uint8_t alarm_seconds;
    uint8_t alarm_minutes;
    uint8_t alarm_hours;
} c64_cia_hardware_snapshot;

typedef struct c64_sid_voice_hardware_snapshot {
    uint16_t frequency;
    uint16_t pulse_width;
    uint8_t control;
    uint8_t attack_decay;
    uint8_t sustain_release;
    uint8_t envelope;
    sid_env_state envelope_state;
    uint8_t phase_hi;
    uint8_t waveform_mask;
    bool gate;
    bool sync;
    bool ring;
    bool test;
} c64_sid_voice_hardware_snapshot;

/* Lightweight 1541 media/mechanics view (no track flux payload). */
typedef struct c64_1541_hardware_snapshot {
    int device_number;
    int rom_loaded;
    int media_enabled;
    int tracks_valid;
    int from_g64;
    int motor_on;
    int motor_ready;
    int writing;
    int in_sync;
    int density;
    int half_track; /* 2 = track 1.0; odd = half-track */
    uint16_t pc;
    /* UI activity event counters (incremented on disk R/W). Frontend turns
       LEDs on from seq changes and holds them for a short host-time window. */
    uint32_t activity_read_seq;
    uint32_t activity_write_seq;
} c64_1541_hardware_snapshot;

typedef struct c64_sid_hardware_snapshot {
    c64_sid_voice_hardware_snapshot voices[3];
    uint16_t filter_cutoff;
    uint8_t filter_res_route;
    uint8_t mode_volume;
    uint8_t volume;
    uint8_t filter_mode;
    uint8_t voice3_osc_read;
    uint8_t voice3_env_read;
    float last_sample;
    bool sample_output_enabled;
} c64_sid_hardware_snapshot;

typedef enum c64_memory_access_type {
    C64_MEMORY_ACCESS_READ = 0,
    C64_MEMORY_ACCESS_WRITE
} c64_memory_access_type;

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
    sid sid;
    c64_keyboard keyboard;
    uint8_t joystick1;
    uint8_t joystick2;
    uint8_t iec_external_pull;
    uint8_t iec_external_pull_other;
    uint8_t iec_external_pull_drive8;
    uint8_t iec_external_pull_drive9;
    c64_clock clock;
    c64_frame working_frame;
    uint64_t keyboard_events;
    uint64_t restore_requests;
    c64_memory_access_fn memory_access;
    void *memory_access_user;
    c64_cpu_instruction_trace last_cpu_trace;
    c64_cpu_instruction_trace pending_cpu_trace;
    uint64_t write_history[C64_RAM_SIZE];
    uint64_t cpu_trace_start_cycle;
    uint64_t cpu_trace_start_cpu_cycle;
    size_t pending_cpu_event_index;
    size_t pending_cpu_elapsed;
    uint8_t cpu_bus_mode;
    bool pending_cpu_trace_active;
    bool cpu_trace_enabled;
    bool instruction_complete;
    bool restore_pending;
    bool cia2_nmi_line;
    size_t cpu_cycles_remaining;
    bool has_basic_rom;
    bool has_kernal_rom;
    bool has_character_rom;
    bool ready;
    c64_config config;
    c64_drive_slot drives[C64_DRIVE_SLOT_COUNT];
    c1541 drive8;
    c1541 drive9;
} c64_t;

void c64_init(c64_t *machine);
void c64_set_config(c64_t *machine, const c64_config *config);
bool c64_install_roms(c64_t *machine, const c64_rom_set *roms, char *error, size_t error_size);
bool c64_reset(c64_t *machine, char *error, size_t error_size);
bool c64_step_instruction(c64_t *machine, char *error, size_t error_size);
bool c64_step_cycle(c64_t *machine, char *error, size_t error_size);
bool c64_generate_test_frame(c64_t *machine, c64_frame *out_frame);
bool c64_make_frame_snapshot(c64_t *machine, c64_frame *out_frame);
bool c64_make_current_frame_snapshot(c64_t *machine, c64_frame *out_frame);
bool c64_copy_completed_frame(c64_t *machine, c64_frame *out_frame);
bool c64_consume_frame_complete(c64_t *machine);
bool c64_consume_instruction_complete(c64_t *machine);
void c64_set_key(c64_t *machine, c64_key key, bool pressed);
void c64_set_matrix(c64_t *machine, uint8_t row, uint8_t col, bool pressed);
void c64_set_joystick(c64_t *machine, unsigned port, uint8_t inputs);
void c64_set_iec_external_pull(c64_t *machine, uint8_t lines);
void c64_set_iec_drive_pull(c64_t *machine, int device_number, uint8_t lines);
uint8_t c64_get_iec_external_pull(c64_t *machine);
/* External IEC pulls excluding the given drive (8 or 9). Used so a drive can
   sense the bus with immediate self-pull while publishing delayed pull to peers. */
uint8_t c64_get_iec_pull_excluding_drive(c64_t *machine, int device_number);
/* Returns a bitmask (C64_IEC_* flags) of IEC lines CIA #2 is actively asserting.
   Does not include iec_external_pull; no side effects. */
uint8_t c64_get_iec_c64_pull(c64_t *machine);
/* Returns a pointer to the drive slot for device_number (8 or 9), or NULL. */
const c64_drive_slot *c64_get_drive_slot(c64_t *machine, int device_number);
/* Mutable variant, used by the 1541 job intercept to write sector data back
   into a mounted, writable D64 image. */
c64_drive_slot *c64_get_drive_slot_mut(c64_t *machine, int device_number);
void c64_set_audio_output_enabled(c64_t *machine, bool enabled);
/* When false, VIC-II still advances timing/BA/IRQ but skips ARGB pixel fill and
   completed-frame buffer copies. Used under high turbo. Default true. */
void c64_set_video_output_enabled(c64_t *machine, bool enabled);
bool c64_video_output_enabled(const c64_t *machine);
void c64_restore(c64_t *machine);
void c64_set_memory_access_callback(c64_t *machine, c64_memory_access_fn callback, void *user);
void c64_set_cpu_trace_enabled(c64_t *machine, bool enabled);
bool c64_attach_generic_cartridge(
    c64_t *machine,
    const uint8_t *roml,
    size_t roml_size,
    const uint8_t *romh,
    size_t romh_size,
    uint8_t exrom,
    uint8_t game,
    char *error,
    size_t error_size);
void c64_detach_cartridge(c64_t *machine);
bool c64_cartridge_attached(const c64_t *machine);
bool c64_drive_device_supported(uint8_t device);
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
    uint16_t free_blocks);
c64_drive_status_result c64_mount_d64_ex(
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
    uint16_t free_blocks,
    bool writable);
/* Mount a G64 image. v1 is read-only (writable flag ignored / forced false).
   Requires media_1541 + 1541 ROM for useful loads (no sector intercept). */
c64_drive_status_result c64_mount_g64(
    c64_t *machine,
    uint8_t device,
    const uint8_t *image_bytes,
    size_t image_size,
    const char *display_name);
bool c64_set_drive_writable(c64_t *machine, uint8_t device, bool writable);
void c64_unmount_drive(c64_t *machine, uint8_t device);
void c64_unmount_all_drives(c64_t *machine);
bool c64_copy_drive_status(const c64_t *machine, uint8_t device, c64_drive_status *out_status);
/* Pulse sticky disk activity LEDs (visible for ~0.35s after the last event). */
void c64_disk_activity_read(c64_t *machine, int device_number);
void c64_disk_activity_write(c64_t *machine, int device_number);
void c64_copy_cpu_snapshot(const c64_t *machine, c64_cpu_snapshot *out);
void c64_copy_machine_snapshot(const c64_t *machine, c64_machine_snapshot *out);
void c64_copy_memory_banking_snapshot(const c64_t *machine, c64_memory_banking_snapshot *out);
void c64_copy_vicii_hardware_snapshot(const c64_t *machine, c64_vicii_hardware_snapshot *out);
void c64_copy_cia_hardware_snapshot(const c64_t *machine, unsigned cia_index, c64_cia_hardware_snapshot *out);
void c64_copy_sid_hardware_snapshot(const c64_t *machine, c64_sid_hardware_snapshot *out);
void c64_copy_1541_hardware_snapshot(
    const c64_t *machine,
    int device_number,
    c64_1541_hardware_snapshot *out);
void c64_copy_vicii_snapshot(const c64_t *machine, c64_vicii_snapshot *out);
uint8_t c64_debug_read_cpu_map(const c64_t *machine, uint16_t address);
uint8_t c64_debug_read_ram(const c64_t *machine, uint16_t address);
uint8_t c64_debug_read_rom(const c64_t *machine, uint16_t address);
bool c64_debug_read_drive_map(const c64_t *machine, uint8_t device, uint16_t address, uint8_t *out_value);
uint64_t c64_debug_read_write_history(const c64_t *machine, uint16_t address);
void c64_debug_write_cpu_map(c64_t *machine, uint16_t address, uint8_t value);
void c64_debug_write_ram(c64_t *machine, uint16_t address, uint8_t value);
c64_memory_visibility c64_memory_visibility_at(const c64_t *machine, uint16_t address);
size_t c64_debug_copy_last_cpu_trace(const c64_t *machine, c64_cpu_instruction_trace *out);

/* VICE-compatible debugcart at $D7FF (testbench exit code). Disabled by default. */
void c64_set_debugcart_enabled(c64_t *machine, bool enabled);
void c64_clear_debugcart(c64_t *machine);
bool c64_debugcart_hit(const c64_t *machine);
uint8_t c64_debugcart_value(const c64_t *machine);
