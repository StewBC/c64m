#include "c64_snapshot.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum {
    C64_SNAPSHOT_HEADER_SIZE = 32,
    C64_SNAPSHOT_CHUNK_HEADER_SIZE = 8,
    C64_SNAPSHOT_MAX_CHUNK_SIZE = 32u * 1024u * 1024u
};

#define C64_SNAPSHOT_TAG(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum {
    TAG_META = C64_SNAPSHOT_TAG('M', 'E', 'T', 'A'),
    TAG_RAM  = C64_SNAPSHOT_TAG('R', 'A', 'M', '_'),
    TAG_BUS  = C64_SNAPSHOT_TAG('B', 'U', 'S', '_'),
    TAG_CPU  = C64_SNAPSHOT_TAG('C', 'P', 'U', '_'),
    TAG_VIC  = C64_SNAPSHOT_TAG('V', 'I', 'C', '_'),
    TAG_CIA1 = C64_SNAPSHOT_TAG('C', 'I', 'A', '1'),
    TAG_CIA2 = C64_SNAPSHOT_TAG('C', 'I', 'A', '2'),
    TAG_SID  = C64_SNAPSHOT_TAG('S', 'I', 'D', '_'),
    TAG_MACH = C64_SNAPSHOT_TAG('M', 'A', 'C', 'H'),
    TAG_CART = C64_SNAPSHOT_TAG('C', 'A', 'R', 'T'),
    TAG_DRV8 = C64_SNAPSHOT_TAG('D', 'R', 'V', '8'),
    TAG_DRV9 = C64_SNAPSHOT_TAG('D', 'R', 'V', '9'),
    TAG_DR8C = C64_SNAPSHOT_TAG('D', 'R', '8', 'C'),
    TAG_DR9C = C64_SNAPSHOT_TAG('D', 'R', '9', 'C')
};

typedef struct snapshot_writer {
    uint8_t *out;
    size_t cap;
    size_t pos;
    bool ok;
} snapshot_writer;

typedef struct snapshot_reader {
    const uint8_t *data;
    size_t len;
    size_t pos;
    bool ok;
} snapshot_reader;

typedef struct snapshot_meta {
    uint32_t flags;
    uint32_t content_mode;
    uint8_t has_basic_rom;
    uint8_t has_kernal_rom;
    uint8_t has_character_rom;
    uint8_t config_video_standard;
    uint8_t config_emulate_1541;
    uint64_t basic_hash;
    uint64_t kernal_hash;
    uint64_t character_hash;
} snapshot_meta;

typedef struct parsed_chunks {
    bool meta;
    bool ram;
    bool bus;
    bool cpu;
    bool vic;
    bool cia1;
    bool cia2;
    bool sid;
    bool mach;
    bool cart;
    bool drv8;
    bool drv9;
    bool drv8_core;
    bool drv9_core;
} parsed_chunks;

static uint64_t snapshot_hash64(const uint8_t *data, size_t size) {
    uint64_t h = 1469598103934665603ull;
    size_t i;

    for (i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void w_bytes(snapshot_writer *w, const void *data, size_t size) {
    assert(w);
    if (!w->ok) {
        return;
    }
    if (size > w->cap || w->pos > w->cap - size) {
        w->ok = false;
        return;
    }
    if (w->out != NULL && data != NULL && size > 0) {
        memcpy(w->out + w->pos, data, size);
    }
    w->pos += size;
}

static void w_u8(snapshot_writer *w, uint8_t value) {
    w_bytes(w, &value, sizeof(value));
}

static void w_bool(snapshot_writer *w, bool value) {
    w_u8(w, value ? 1u : 0u);
}

static void w_u16(snapshot_writer *w, uint16_t value) {
    uint8_t b[2];
    b[0] = (uint8_t)(value & 0xffu);
    b[1] = (uint8_t)(value >> 8);
    w_bytes(w, b, sizeof(b));
}

static void w_u32(snapshot_writer *w, uint32_t value) {
    uint8_t b[4];
    b[0] = (uint8_t)(value & 0xffu);
    b[1] = (uint8_t)((value >> 8) & 0xffu);
    b[2] = (uint8_t)((value >> 16) & 0xffu);
    b[3] = (uint8_t)(value >> 24);
    w_bytes(w, b, sizeof(b));
}

static void w_u64(snapshot_writer *w, uint64_t value) {
    size_t i;
    for (i = 0; i < 8; ++i) {
        w_u8(w, (uint8_t)(value >> (i * 8u)));
    }
}

static void w_size(snapshot_writer *w, size_t value) {
    w_u64(w, (uint64_t)value);
}

static void w_float(snapshot_writer *w, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    w_u32(w, bits);
}

static void w_double(snapshot_writer *w, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    w_u64(w, bits);
}

static void w_i32(snapshot_writer *w, int32_t value) {
    w_u32(w, (uint32_t)value);
}

static void w_frame(snapshot_writer *w, const c64_frame *frame) {
    size_t i;

    w_u32(w, frame->width);
    w_u32(w, frame->height);
    w_u32(w, frame->stride_bytes);
    w_u32(w, frame->pixel_format);
    w_u64(w, frame->frame_number);
    w_u64(w, frame->machine_cycle);
    for (i = 0; i < C64_FRAME_WIDTH * C64_FRAME_HEIGHT; ++i) {
        w_u32(w, frame->pixels[i]);
    }
}

static uint8_t r_u8(snapshot_reader *r) {
    if (!r->ok || r->pos >= r->len) {
        r->ok = false;
        return 0;
    }
    return r->data[r->pos++];
}

static bool r_bool(snapshot_reader *r) {
    return r_u8(r) != 0;
}

static uint16_t r_u16(snapshot_reader *r) {
    uint16_t lo = r_u8(r);
    uint16_t hi = r_u8(r);
    return (uint16_t)(lo | (uint16_t)(hi << 8));
}

static uint32_t r_u32(snapshot_reader *r) {
    uint32_t b0 = r_u8(r);
    uint32_t b1 = r_u8(r);
    uint32_t b2 = r_u8(r);
    uint32_t b3 = r_u8(r);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static uint64_t r_u64(snapshot_reader *r) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        value |= (uint64_t)r_u8(r) << (i * 8u);
    }
    return value;
}

static size_t r_size(snapshot_reader *r) {
    uint64_t value = r_u64(r);
    if (value > (uint64_t)((size_t)-1)) {
        r->ok = false;
        return 0;
    }
    return (size_t)value;
}

static float r_float(snapshot_reader *r) {
    uint32_t bits = r_u32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_double(snapshot_reader *r) {
    uint64_t bits = r_u64(r);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int32_t r_i32(snapshot_reader *r) {
    return (int32_t)r_u32(r);
}

static void r_bytes(snapshot_reader *r, void *out, size_t size) {
    if (!r->ok || size > r->len || r->pos > r->len - size) {
        r->ok = false;
        return;
    }
    if (size > 0 && out != NULL) {
        memcpy(out, r->data + r->pos, size);
    }
    r->pos += size;
}

/* The pixel payload is always the full C64_FRAME_WIDTH x C64_FRAME_HEIGHT array
   regardless of the stored height - NTSC keeps its 263 visible rows in the same
   312-row buffer (w_frame above). So honouring the header means *validating* it,
   not sizing the read from it. Do not "fix" this loop to run to frame->height:
   that would desynchronise it from the writer.

   Validation matters because the geometry is a build-time constant. A file whose
   frame does not match this build's framebuffer cannot be loaded, and the old
   code did not notice: it read a fixed pixel count past a mismatched header, so
   every chunk after the VIC state landed at the wrong offset and the load either
   failed far from the real cause or restored garbage. Reject it here instead.

   A frame that has never been painted is legitimately all-zero (vicii_reset
   memsets both frames; completed_frame stays zeroed until the first frame
   finishes), so accept that shape too. */
static bool r_frame_geometry_ok(const c64_frame *frame) {
    if (frame->width == 0u && frame->height == 0u &&
        frame->stride_bytes == 0u && frame->pixel_format == 0u) {
        return true;
    }
    return frame->width == (uint32_t)C64_FRAME_WIDTH &&
        frame->height > 0u &&
        frame->height <= (uint32_t)C64_FRAME_HEIGHT &&
        frame->stride_bytes ==
            (uint32_t)C64_FRAME_WIDTH * (uint32_t)sizeof(frame->pixels[0]) &&
        frame->pixel_format == (uint32_t)C64_FRAME_PIXEL_FORMAT_ARGB8888;
}

static void r_frame(snapshot_reader *r, c64_frame *frame) {
    size_t i;

    frame->width = r_u32(r);
    frame->height = r_u32(r);
    frame->stride_bytes = r_u32(r);
    frame->pixel_format = r_u32(r);
    frame->frame_number = r_u64(r);
    frame->machine_cycle = r_u64(r);

    if (!r->ok || !r_frame_geometry_ok(frame)) {
        r->ok = false;
        return;
    }

    for (i = 0; i < C64_FRAME_WIDTH * C64_FRAME_HEIGHT; ++i) {
        frame->pixels[i] = r_u32(r);
    }
}

static void begin_chunk(snapshot_writer *w, uint32_t tag, size_t *len_pos) {
    w_u32(w, tag);
    *len_pos = w->pos;
    w_u32(w, 0);
}

static void end_chunk(snapshot_writer *w, size_t len_pos) {
    size_t payload_start = len_pos + sizeof(uint32_t);
    size_t payload_size;

    if (!w->ok || len_pos + sizeof(uint32_t) > w->pos) {
        w->ok = false;
        return;
    }
    payload_size = w->pos - payload_start;
    if (payload_size > UINT32_MAX) {
        w->ok = false;
        return;
    }
    if (w->out != NULL) {
        uint32_t value = (uint32_t)payload_size;
        w->out[len_pos + 0] = (uint8_t)(value & 0xffu);
        w->out[len_pos + 1] = (uint8_t)((value >> 8) & 0xffu);
        w->out[len_pos + 2] = (uint8_t)((value >> 16) & 0xffu);
        w->out[len_pos + 3] = (uint8_t)(value >> 24);
    }
}

static void write_header(snapshot_writer *w, uint32_t flags) {
    w_u32(w, C64_SNAPSHOT_MAGIC);
    w_u32(w, C64_SNAPSHOT_VERSION);
    w_u32(w, C64_SNAPSHOT_HEADER_SIZE);
    w_u32(w, C64_SNAPSHOT_CONTENT_REFERENCED);
    w_u32(w, flags);
    w_u32(w, 0);
    w_u32(w, 0);
    w_u32(w, 0);
}

static uint32_t snapshot_flags_for_machine(const c64_t *m) {
    uint32_t flags = C64_SNAPSHOT_FLAG_EXTERNAL_ROMS_REQUIRED;

    if ((m->drive8.rom_loaded || m->drive9.rom_loaded) && m->config.emulate_1541) {
        flags |= C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED;
    }
    if (m->drives[0].mounted || m->drives[1].mounted) {
        flags |= C64_SNAPSHOT_FLAG_EXTERNAL_MEDIA_REFERENCES;
    }
    return flags;
}

static bool snapshot_includes_1541_core(uint32_t version, uint32_t flags) {
    return version >= 9u && (flags & C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED) != 0u;
}

static void write_meta(snapshot_writer *w, const c64_t *m, uint32_t flags) {
    size_t chunk;

    begin_chunk(w, TAG_META, &chunk);
    w_u32(w, flags);
    w_u32(w, C64_SNAPSHOT_CONTENT_REFERENCED);
    w_bool(w, m->has_basic_rom);
    w_bool(w, m->has_kernal_rom);
    w_bool(w, m->has_character_rom);
    w_u8(w, (uint8_t)m->config.video_standard);
    w_u8(w, (uint8_t)(m->config.emulate_1541 ? 1u : 0u));
    w_u64(w, snapshot_hash64(m->bus.basic_rom, sizeof(m->bus.basic_rom)));
    w_u64(w, snapshot_hash64(m->bus.kernal_rom, sizeof(m->bus.kernal_rom)));
    w_u64(w, snapshot_hash64(m->bus.char_rom, sizeof(m->bus.char_rom)));
    end_chunk(w, chunk);
}

static void write_ram(snapshot_writer *w, const c64_t *m) {
    size_t chunk;

    begin_chunk(w, TAG_RAM, &chunk);
    w_bytes(w, m->bus.ram, sizeof(m->bus.ram));
    w_bytes(w, m->bus.color_ram, sizeof(m->bus.color_ram));
    end_chunk(w, chunk);
}

static void write_bus(snapshot_writer *w, const c64_t *m) {
    const c64_bus_t *bus = &m->bus;
    size_t chunk;

    begin_chunk(w, TAG_BUS, &chunk);
    w_u8(w, bus->cpu_port_direction);
    w_u8(w, bus->cpu_port_data);
    w_u64(w, bus->screen_ram_writes);
    w_u64(w, bus->color_ram_writes);
    w_u64(w, bus->vic_register_writes);
    w_u64(w, bus->cia1_register_writes);
    w_u64(w, bus->cia2_register_writes);
    w_u64(w, bus->sid_register_writes);
    w_u16(w, bus->vic_bank_base);
    end_chunk(w, chunk);
}

static void write_cpu(snapshot_writer *w, const c64_t *m) {
    const CPU *cpu = &m->cpu.cpu;
    size_t chunk;

    begin_chunk(w, TAG_CPU, &chunk);
    w_u16(w, cpu->pc);
    w_u16(w, cpu->opcode_pc);
    w_u16(w, cpu->sp);
    w_u8(w, cpu->A);
    w_u8(w, cpu->X);
    w_u8(w, cpu->Y);
    w_u8(w, cpu->flags);
    w_u16(w, cpu->address_16);
    w_u16(w, cpu->scratch_16);
    w_u8(w, cpu->page_fault);
    w_u8(w, cpu->irq_defer);
    w_u8(w, cpu->irq_defer_i);
    w_u8(w, cpu->opcode_active);
    w_u32(w, cpu->class);
    w_u64(w, cpu->cycles);
    w_u64(w, cpu->irq_entries);
    w_u64(w, cpu->nmi_entries);
    end_chunk(w, chunk);
}

static void write_vic(snapshot_writer *w, const c64_t *m) {
    const vicii *v = &m->vic;
    size_t i;
    size_t n;
    size_t chunk;

    begin_chunk(w, TAG_VIC, &chunk);
    w_bytes(w, v->registers, sizeof(v->registers));
    w_u32(w, v->timing.cycles_per_line);
    w_u32(w, v->timing.lines_per_frame);
    w_u32(w, v->timing.cycle_in_line);
    w_u32(w, v->timing.raster_line);
    w_u64(w, v->timing.frame_number);
    w_bool(w, v->timing.frame_complete);
    w_u32(w, (uint32_t)v->timing.standard);
    w_bool(w, v->timing.aec_active);
    w_bool(w, v->timing.rdy_active);
    w_u8(w, v->timing.prefetch_cycles);
    w_u16(w, v->timing.raster_compare);
    w_u64(w, v->timing.ba_low_until_abs);
    w_u64(w, v->timing.sprite_ba_low_until_abs);
    w_bool(w, v->completed_frame_ready);
    w_u16(w, v->vc);
    w_u16(w, v->vc_base);
    w_u8(w, v->vmli);
    w_u8(w, v->rc);
    w_bool(w, v->display_state);
    w_bool(w, v->bad_line);
    w_bytes(w, v->video_matrix, sizeof(v->video_matrix));
    w_bytes(w, v->color_line, sizeof(v->color_line));
    w_bytes(w, v->g_line, sizeof(v->g_line));
    w_u8(w, v->reg11_delay);
    w_u8(w, v->irq_status);
    w_u8(w, v->irq_enable);
    w_bytes(w, v->sprite_mc, sizeof(v->sprite_mc));
    w_bytes(w, v->sprite_mcbase, sizeof(v->sprite_mcbase));
    for (i = 0; i < 8; ++i) w_bool(w, v->sprite_active[i]);
    for (i = 0; i < 8; ++i) w_bool(w, v->sprite_visible[i]);
    for (i = 0; i < 8; ++i) w_bool(w, v->sprite_y_exp_ff[i]);
    w_bytes(w, v->sprite_pointer, sizeof(v->sprite_pointer));
    for (i = 0; i < 8; ++i) {
        for (n = 0; n < 3; ++n) w_u8(w, v->sprite_data[i][n]);
    }
    for (i = 0; i < 8; ++i) w_bool(w, v->sprite_line_enabled[i]);
    for (i = 0; i < 8; ++i) w_u16(w, v->sprite_line_x[i]);
    for (i = 0; i < 8; ++i) w_bool(w, v->sprite_line_x_expand[i]);
    for (i = 0; i < 8; ++i) w_bool(w, v->sprite_line_multicolor[i]);
    w_bytes(w, v->sprite_line_color, sizeof(v->sprite_line_color));
    w_u8(w, v->sprite_line_mm0);
    w_u8(w, v->sprite_line_mm1);
    w_u8(w, v->sprite_priority);
    w_u8(w, v->sprite_sprite_collision);
    w_u8(w, v->sprite_background_collision);
    w_bool(w, v->vertical_border_active);
    w_bool(w, v->set_vborder);
    w_bool(w, v->allow_bad_lines);
    w_frame(w, &v->working_frame);
    w_frame(w, &v->completed_frame);
    end_chunk(w, chunk);
}

static void write_cia_timer(snapshot_writer *w, const cia_timer *timer) {
    w_u16(w, timer->latch);
    w_u16(w, timer->counter);
    w_bool(w, timer->underflow);
    w_bool(w, timer->output_level);
    w_bool(w, timer->pulse_active);
}

static void write_cia_tod(snapshot_writer *w, const cia_tod *tod) {
    w_u8(w, tod->tenth);
    w_u8(w, tod->seconds);
    w_u8(w, tod->minutes);
    w_u8(w, tod->hours);
}

static void write_cia(snapshot_writer *w, uint32_t tag, const cia *c) {
    size_t chunk;

    begin_chunk(w, tag, &chunk);
    w_bytes(w, c->registers, sizeof(c->registers));
    write_cia_timer(w, &c->timer_a);
    write_cia_timer(w, &c->timer_b);
    write_cia_tod(w, &c->tod);
    write_cia_tod(w, &c->tod_alarm);
    write_cia_tod(w, &c->tod_latch);
    w_u64(w, c->tod_cycle_accum);
    w_u64(w, c->tod_50hz_cycles);
    w_u64(w, c->tod_60hz_cycles);
    w_u8(w, c->interrupt_flags);
    w_u8(w, c->interrupt_mask);
    w_u64(w, c->icr_reads);
    w_u64(w, c->icr_writes);
    w_u64(w, c->interrupt_assertions);
    w_bool(w, c->tod_latched);
    w_bool(w, c->tod_stopped);
    w_bool(w, c->cnt_pulse);
    end_chunk(w, chunk);
}

static void write_sid(snapshot_writer *w, const c64_t *m) {
    const sid *s = &m->sid;
    size_t i;
    size_t chunk;

    begin_chunk(w, TAG_SID, &chunk);
    w_bytes(w, s->regs, sizeof(s->regs));
    for (i = 0; i < 3; ++i) {
        const sid_voice *v = &s->voices[i];
        w_u16(w, v->freq);
        w_u16(w, v->pulse_width);
        w_u8(w, v->control);
        w_u8(w, v->attack_decay);
        w_u8(w, v->sustain_release);
        w_u32(w, v->phase);
        w_u32(w, v->noise_lfsr);
        w_u8(w, v->envelope);
        w_u32(w, (uint32_t)v->env_state);
        w_double(w, v->env_counter);
        w_float(w, v->last_wave);
    }
    w_u16(w, s->filter_cutoff);
    w_u8(w, s->filter_res_route);
    w_u8(w, s->mode_volume);
    w_float(w, s->filter_lp);
    w_float(w, s->filter_bp);
    w_float(w, s->filter_hp);
    w_float(w, s->dc_block_prev_input);
    w_float(w, s->dc_block_prev_output);
    w_float(w, s->hfroll_state);
    w_float(w, s->last_sample);
    w_bool(w, s->sample_output_enabled);
    w_u8(w, s->voice3_osc_read);
    w_u8(w, s->voice3_env_read);
    end_chunk(w, chunk);
}

static void write_mach(snapshot_writer *w, const c64_t *m) {
    size_t chunk;

    begin_chunk(w, TAG_MACH, &chunk);
    w_u8(w, m->keyboard.rows[0]);
    w_u8(w, m->keyboard.rows[1]);
    w_u8(w, m->keyboard.rows[2]);
    w_u8(w, m->keyboard.rows[3]);
    w_u8(w, m->keyboard.rows[4]);
    w_u8(w, m->keyboard.rows[5]);
    w_u8(w, m->keyboard.rows[6]);
    w_u8(w, m->keyboard.rows[7]);
    w_u8(w, m->joystick1);
    w_u8(w, m->joystick2);
    w_u8(w, m->iec_external_pull);
    w_u8(w, m->iec_external_pull_other);
    w_u8(w, m->iec_external_pull_drive8);
    w_u8(w, m->iec_external_pull_drive9);
    w_u64(w, m->clock.cycle);
    w_u64(w, m->clock.cpu_cycles);
    w_u64(w, m->clock.vic_cycles);
    w_u64(w, m->clock.cia_cycles);
    w_u64(w, m->clock.drive_accum);
    w_u64(w, m->clock.drive_synced_cycle);
    w_u64(w, m->keyboard_events);
    w_u64(w, m->restore_requests);
    w_bool(w, m->restore_pending);
    w_bool(w, m->cia2_nmi_line);
    w_size(w, m->cpu_cycles_remaining);
    w_bool(w, m->ready);
    w_u8(w, (uint8_t)m->config.video_standard);
    w_u8(w, (uint8_t)(m->config.emulate_1541 ? 1u : 0u));
    w_bool(w, m->instruction_complete);
    w_u8(w, (uint8_t)(m->config.media_1541 ? 1u : 0u));
    end_chunk(w, chunk);
}

static void write_cart(snapshot_writer *w, const c64_t *m) {
    const c64_bus_t *bus = &m->bus;
    size_t chunk;

    begin_chunk(w, TAG_CART, &chunk);
    w_bool(w, bus->cartridge_mounted);
    w_bool(w, bus->cartridge_roml_present);
    w_bool(w, bus->cartridge_romh_present);
    w_u8(w, bus->cartridge_exrom);
    w_u8(w, bus->cartridge_game);
    w_u32(w, (uint32_t)bus->cartridge_mode);
    w_bytes(w, bus->cartridge_roml, sizeof(bus->cartridge_roml));
    w_bytes(w, bus->cartridge_romh, sizeof(bus->cartridge_romh));
    end_chunk(w, chunk);
}

static void write_drive(snapshot_writer *w, uint32_t tag, const c64_drive_slot *slot) {
    size_t i;
    size_t chunk;

    begin_chunk(w, tag, &chunk);
    w_bool(w, slot->mounted);
    w_u32(w, (uint32_t)slot->image_kind);
    w_u32(w, (uint32_t)slot->last_result);
    w_bytes(w, slot->display_name, sizeof(slot->display_name));
    w_bytes(w, slot->disk_title, sizeof(slot->disk_title));
    w_bytes(w, slot->disk_id, sizeof(slot->disk_id));
    w_bytes(w, slot->dos_type, sizeof(slot->dos_type));
    w_u16(w, slot->free_blocks);
    w_size(w, slot->image_size);
    if (slot->image_size > 0 && slot->image_bytes != NULL) {
        w_bytes(w, slot->image_bytes, slot->image_size);
    }
    w_size(w, slot->entry_count);
    for (i = 0; i < slot->entry_count; ++i) {
        const c64_drive_directory_entry *e = &slot->entries[i];
        w_u8(w, e->raw_type);
        w_u32(w, (uint32_t)e->type);
        w_u8(w, e->first_track);
        w_u8(w, e->first_sector);
        w_bytes(w, e->filename, sizeof(e->filename));
        w_size(w, e->filename_length);
        w_u16(w, e->block_count);
    }
    end_chunk(w, chunk);
}

static void write_cpu_fields(snapshot_writer *w, const CPU *cpu) {
    w_u16(w, cpu->pc);
    w_u16(w, cpu->opcode_pc);
    w_u16(w, cpu->sp);
    w_u8(w, cpu->A);
    w_u8(w, cpu->X);
    w_u8(w, cpu->Y);
    w_u8(w, cpu->flags);
    w_u16(w, cpu->address_16);
    w_u16(w, cpu->scratch_16);
    w_u8(w, cpu->page_fault);
    w_u8(w, cpu->irq_defer);
    w_u8(w, cpu->irq_defer_i);
    w_u8(w, cpu->opcode_active);
    w_u32(w, cpu->class);
    w_u64(w, cpu->cycles);
    w_u64(w, cpu->irq_entries);
    w_u64(w, cpu->nmi_entries);
}

static void write_c6510_drive(snapshot_writer *w, const C6510 *cpu) {
    write_cpu_fields(w, &cpu->cpu);
    w_u32(w, (uint32_t)cpu->bus_access_kind);
    w_u8(w, cpu->micro_active);
    w_u8(w, cpu->micro_opcode);
    w_u8(w, cpu->micro_phase);
    w_u8(w, cpu->micro_branch_taken);
    w_u16(w, cpu->micro_target);
    w_u16(w, cpu->micro_interrupt_vector);
    w_u8(w, cpu->micro_is_interrupt);
}

static void write_via(snapshot_writer *w, const via6522 *v) {
    w_u8(w, v->ora);
    w_u8(w, v->orb);
    w_u8(w, v->ddra);
    w_u8(w, v->ddrb);
    w_u8(w, v->port_a_in);
    w_u8(w, v->port_b_in);
    w_u16(w, v->t1_counter);
    w_u16(w, v->t1_latch);
    w_i32(w, v->t1_running);
    w_i32(w, v->t1_pb7_state);
    w_u16(w, v->t2_counter);
    w_u8(w, v->t2_latch_low);
    w_i32(w, v->t2_running);
    w_u8(w, v->sr);
    w_u8(w, v->acr);
    w_u8(w, v->pcr);
    w_u8(w, v->ifr);
    w_u8(w, v->ier);
    w_u8(w, v->ca1_last);
}

static void write_media(snapshot_writer *w, const c1541_media *media) {
    int i;

    w_i32(w, media->enabled);
    w_i32(w, media->motor_on);
    w_i32(w, media->motor_ready);
    w_u32(w, media->motor_spin_left);
    w_i32(w, media->half_track);
    w_u8(w, media->stepper_phase);
    w_i32(w, media->density);
    w_u32(w, media->bit_acc);
    w_u32(w, media->ref_cycle);
    w_u32(w, media->bit_event_ref);
    w_u32(w, media->ref_tick_accum);
    w_u32(w, media->ref_advance);
    w_u32(w, media->req_ref_cycles);
    w_u32(w, media->flux_acc);
    w_i32(w, media->filter_counter);
    w_i32(w, media->filter_state);
    w_i32(w, media->filter_last_state);
    w_i32(w, media->no_flux_cycles);
    w_u32(w, media->flux_rand);
    w_i32(w, media->ue7_counter);
    w_i32(w, media->uf4_counter);
    w_u32(w, media->head_bit_pos);
    w_u16(w, media->shift10);
    w_i32(w, media->in_sync);
    w_i32(w, media->bits_in_byte);
    w_u8(w, media->shifting_byte);
    w_u8(w, media->port_a_byte);
    w_i32(w, media->byte_ready);
    w_i32(w, media->so_pulse);
    w_i32(w, media->so_delay);
    w_i32(w, media->writing);
    w_i32(w, media->write_bits_left);
    w_u8(w, media->write_shift);
    w_i32(w, media->last_write_bit);
    w_i32(w, media->tracks_valid);
    w_i32(w, media->from_g64);
    w_bool(w, media->built_from != NULL);
    w_size(w, media->built_size);
    w_u32(w, media->built_from_seq);
    w_u32(w, media->attach_left);
    w_i32(w, media->attach_pending);

    for (i = 0; i < C1541_MEDIA_HALF_SLOTS; ++i) {
        const c1541_track *tr = &media->halves[i];
        int present = (tr->data != NULL && tr->length > 0) ? 1 : 0;

        w_bool(w, present != 0);
        if (!present) {
            continue;
        }
        w_size(w, tr->length);
        w_i32(w, tr->density);
        w_i32(w, tr->dirty);
        w_bytes(w, tr->data, tr->length);
    }
}

static void write_drive_core(snapshot_writer *w, uint32_t tag, const c1541 *drive) {
    size_t chunk;

    begin_chunk(w, tag, &chunk);
    /* TODO(total-snapshot §5): optional FNV hash of drive ROM for load validation. */
    w_bytes(w, drive->ram, sizeof(drive->ram));
    w_i32(w, drive->rom_loaded);
    w_i32(w, drive->device_number);
    w_size(w, drive->cpu_cycles_remaining);
    w_i32(w, drive->via2_t1_pb7_last);
    write_c6510_drive(w, &drive->cpu);
    write_via(w, &drive->via1);
    write_via(w, &drive->via2);
    write_media(w, &drive->media);
    end_chunk(w, chunk);
}

static bool write_snapshot(const c64_t *m, uint8_t *out, size_t out_cap, size_t *out_size) {
    snapshot_writer w;
    uint32_t flags;

    if (m == NULL || out_size == NULL || m->pending_cpu_trace_active || m->cpu.micro_active) {
        return false;
    }

    flags = snapshot_flags_for_machine(m);
    w.out = out;
    w.cap = out == NULL ? (size_t)-1 : out_cap;
    w.pos = 0;
    w.ok = true;

    write_header(&w, flags);
    write_meta(&w, m, flags);
    write_ram(&w, m);
    write_bus(&w, m);
    write_cpu(&w, m);
    write_vic(&w, m);
    write_cia(&w, TAG_CIA1, &m->cia1);
    write_cia(&w, TAG_CIA2, &m->cia2);
    write_sid(&w, m);
    write_mach(&w, m);
    write_cart(&w, m);
    write_drive(&w, TAG_DRV8, &m->drives[0]);
    write_drive(&w, TAG_DRV9, &m->drives[1]);
    if ((flags & C64_SNAPSHOT_FLAG_1541_STATE_INCLUDED) != 0u) {
        write_drive_core(&w, TAG_DR8C, &m->drive8);
        write_drive_core(&w, TAG_DR9C, &m->drive9);
    }

    if (!w.ok) {
        return false;
    }
    *out_size = w.pos;
    return true;
}

static bool read_meta(snapshot_reader *r, snapshot_meta *meta) {
    memset(meta, 0, sizeof(*meta));
    meta->flags = r_u32(r);
    meta->content_mode = r_u32(r);
    meta->has_basic_rom = r_u8(r);
    meta->has_kernal_rom = r_u8(r);
    meta->has_character_rom = r_u8(r);
    meta->config_video_standard = r_u8(r);
    meta->config_emulate_1541 = r_u8(r);
    meta->basic_hash = r_u64(r);
    meta->kernal_hash = r_u64(r);
    meta->character_hash = r_u64(r);
    return r->ok && meta->content_mode == C64_SNAPSHOT_CONTENT_REFERENCED;
}

static bool meta_matches_loaded_roms(const c64_t *m, const snapshot_meta *meta) {
    if ((meta->has_basic_rom != 0) != m->has_basic_rom ||
        (meta->has_kernal_rom != 0) != m->has_kernal_rom ||
        (meta->has_character_rom != 0) != m->has_character_rom) {
        return false;
    }
    if (meta->has_basic_rom &&
        meta->basic_hash != snapshot_hash64(m->bus.basic_rom, sizeof(m->bus.basic_rom))) {
        return false;
    }
    if (meta->has_kernal_rom &&
        meta->kernal_hash != snapshot_hash64(m->bus.kernal_rom, sizeof(m->bus.kernal_rom))) {
        return false;
    }
    if (meta->has_character_rom &&
        meta->character_hash != snapshot_hash64(m->bus.char_rom, sizeof(m->bus.char_rom))) {
        return false;
    }
    return true;
}

static void read_ram(snapshot_reader *r, c64_t *m) {
    r_bytes(r, m->bus.ram, sizeof(m->bus.ram));
    r_bytes(r, m->bus.color_ram, sizeof(m->bus.color_ram));
}

static void read_bus(snapshot_reader *r, c64_t *m) {
    c64_bus_t *bus = &m->bus;

    bus->cpu_port_direction = r_u8(r);
    bus->cpu_port_data = r_u8(r);
    bus->screen_ram_writes = r_u64(r);
    bus->color_ram_writes = r_u64(r);
    bus->vic_register_writes = r_u64(r);
    bus->cia1_register_writes = r_u64(r);
    bus->cia2_register_writes = r_u64(r);
    bus->sid_register_writes = r_u64(r);
    bus->vic_bank_base = r_u16(r);
}

static void read_cpu(snapshot_reader *r, c64_t *m) {
    CPU *cpu = &m->cpu.cpu;

    cpu->pc = r_u16(r);
    cpu->opcode_pc = r_u16(r);
    cpu->sp = r_u16(r);
    cpu->A = r_u8(r);
    cpu->X = r_u8(r);
    cpu->Y = r_u8(r);
    cpu->flags = r_u8(r);
    cpu->address_16 = r_u16(r);
    cpu->scratch_16 = r_u16(r);
    cpu->page_fault = r_u8(r) ? 1u : 0u;
    cpu->irq_defer = r_u8(r);
    cpu->irq_defer_i = r_u8(r);
    cpu->opcode_active = r_u8(r);
    cpu->class = r_u32(r);
    cpu->cycles = r_u64(r);
    cpu->irq_entries = r_u64(r);
    cpu->nmi_entries = r_u64(r);
}

static void read_vic(snapshot_reader *r, c64_t *m) {
    vicii *v = &m->vic;
    size_t i;
    size_t n;

    r_bytes(r, v->registers, sizeof(v->registers));
    v->timing.cycles_per_line = r_u32(r);
    v->timing.lines_per_frame = r_u32(r);
    v->timing.cycle_in_line = r_u32(r);
    v->timing.raster_line = r_u32(r);
    v->timing.frame_number = r_u64(r);
    v->timing.frame_complete = r_bool(r);
    v->timing.standard = (vicii_video_standard)r_u32(r);
    v->timing.aec_active = r_bool(r);
    v->timing.rdy_active = r_bool(r);
    v->timing.prefetch_cycles = r_u8(r);
    v->timing.raster_compare = r_u16(r);
    v->timing.ba_low_until_abs = r_u64(r);
    v->timing.sprite_ba_low_until_abs = r_u64(r);
    v->completed_frame_ready = r_bool(r);
    v->vc = r_u16(r);
    v->vc_base = r_u16(r);
    v->vmli = r_u8(r);
    v->rc = r_u8(r);
    v->display_state = r_bool(r);
    v->bad_line = r_bool(r);
    r_bytes(r, v->video_matrix, sizeof(v->video_matrix));
    r_bytes(r, v->color_line, sizeof(v->color_line));
    r_bytes(r, v->g_line, sizeof(v->g_line));
    v->reg11_delay = r_u8(r);
    v->irq_status = r_u8(r);
    v->irq_enable = r_u8(r);
    r_bytes(r, v->sprite_mc, sizeof(v->sprite_mc));
    r_bytes(r, v->sprite_mcbase, sizeof(v->sprite_mcbase));
    for (i = 0; i < 8; ++i) v->sprite_active[i] = r_bool(r);
    for (i = 0; i < 8; ++i) v->sprite_visible[i] = r_bool(r);
    for (i = 0; i < 8; ++i) v->sprite_y_exp_ff[i] = r_bool(r);
    r_bytes(r, v->sprite_pointer, sizeof(v->sprite_pointer));
    for (i = 0; i < 8; ++i) {
        for (n = 0; n < 3; ++n) v->sprite_data[i][n] = r_u8(r);
    }
    for (i = 0; i < 8; ++i) v->sprite_line_enabled[i] = r_bool(r);
    for (i = 0; i < 8; ++i) v->sprite_line_x[i] = r_u16(r);
    for (i = 0; i < 8; ++i) v->sprite_line_x_expand[i] = r_bool(r);
    for (i = 0; i < 8; ++i) v->sprite_line_multicolor[i] = r_bool(r);
    r_bytes(r, v->sprite_line_color, sizeof(v->sprite_line_color));
    v->sprite_line_mm0 = r_u8(r);
    v->sprite_line_mm1 = r_u8(r);
    v->sprite_priority = r_u8(r);
    v->sprite_sprite_collision = r_u8(r);
    v->sprite_background_collision = r_u8(r);
    v->vertical_border_active = r_bool(r);
    v->set_vborder = r_bool(r);
    v->allow_bad_lines = r_bool(r);
    r_frame(r, &v->working_frame);
    r_frame(r, &v->completed_frame);
}

static void read_cia_timer(snapshot_reader *r, cia_timer *timer) {
    timer->latch = r_u16(r);
    timer->counter = r_u16(r);
    timer->underflow = r_bool(r);
    timer->output_level = r_bool(r);
    timer->pulse_active = r_bool(r);
}

static void read_cia_tod(snapshot_reader *r, cia_tod *tod) {
    tod->tenth = r_u8(r);
    tod->seconds = r_u8(r);
    tod->minutes = r_u8(r);
    tod->hours = r_u8(r);
}

static void read_cia(snapshot_reader *r, cia *c) {
    c64_keyboard *keyboard = c->keyboard;
    cia_port_input_fn port_input = c->port_input;
    void *port_input_user = c->port_input_user;

    r_bytes(r, c->registers, sizeof(c->registers));
    read_cia_timer(r, &c->timer_a);
    read_cia_timer(r, &c->timer_b);
    read_cia_tod(r, &c->tod);
    read_cia_tod(r, &c->tod_alarm);
    read_cia_tod(r, &c->tod_latch);
    c->tod_cycle_accum = r_u64(r);
    c->tod_50hz_cycles = r_u64(r);
    c->tod_60hz_cycles = r_u64(r);
    c->interrupt_flags = r_u8(r);
    c->interrupt_mask = r_u8(r);
    c->icr_reads = r_u64(r);
    c->icr_writes = r_u64(r);
    c->interrupt_assertions = r_u64(r);
    c->tod_latched = r_bool(r);
    c->tod_stopped = r_bool(r);
    c->cnt_pulse = r_bool(r);
    c->keyboard = keyboard;
    c->port_input = port_input;
    c->port_input_user = port_input_user;
}

static void read_sid(snapshot_reader *r, c64_t *m) {
    sid *s = &m->sid;
    size_t i;

    r_bytes(r, s->regs, sizeof(s->regs));
    for (i = 0; i < 3; ++i) {
        sid_voice *v = &s->voices[i];
        v->freq = r_u16(r);
        v->pulse_width = r_u16(r);
        v->control = r_u8(r);
        v->attack_decay = r_u8(r);
        v->sustain_release = r_u8(r);
        v->phase = r_u32(r);
        v->noise_lfsr = r_u32(r);
        v->envelope = r_u8(r);
        v->env_state = (sid_env_state)r_u32(r);
        v->env_counter = r_double(r);
        v->last_wave = r_float(r);
    }
    s->filter_cutoff = r_u16(r);
    s->filter_res_route = r_u8(r);
    s->mode_volume = r_u8(r);
    s->filter_lp = r_float(r);
    s->filter_bp = r_float(r);
    s->filter_hp = r_float(r);
    s->dc_block_prev_input = r_float(r);
    s->dc_block_prev_output = r_float(r);
    s->hfroll_state = r_float(r);
    s->last_sample = r_float(r);
    s->sample_output_enabled = r_bool(r);
    s->voice3_osc_read = r_u8(r);
    s->voice3_env_read = r_u8(r);
}

static void read_mach(snapshot_reader *r, c64_t *m, uint32_t version) {
    size_t i;

    for (i = 0; i < 8; ++i) {
        m->keyboard.rows[i] = r_u8(r);
    }
    m->joystick1 = r_u8(r);
    m->joystick2 = r_u8(r);
    m->iec_external_pull = r_u8(r);
    m->iec_external_pull_other = r_u8(r);
    m->iec_external_pull_drive8 = r_u8(r);
    m->iec_external_pull_drive9 = r_u8(r);
    m->clock.cycle = r_u64(r);
    m->clock.cpu_cycles = r_u64(r);
    m->clock.vic_cycles = r_u64(r);
    m->clock.cia_cycles = r_u64(r);
    if (version >= 9u) {
        m->clock.drive_accum = r_u64(r);
        m->clock.drive_synced_cycle = r_u64(r);
    } else {
        m->clock.drive_accum = 0;
        m->clock.drive_synced_cycle = 0;
    }
    m->keyboard_events = r_u64(r);
    m->restore_requests = r_u64(r);
    m->restore_pending = r_bool(r);
    m->cia2_nmi_line = r_bool(r);
    m->cpu_cycles_remaining = r_size(r);
    m->ready = r_bool(r);
    m->config.video_standard = (c64_video_standard)r_u8(r);
    m->config.emulate_1541 = r_u8(r) != 0;
    m->instruction_complete = r_bool(r);
    if (version >= 9u) {
        m->config.media_1541 = r_u8(r) != 0;
    } else {
        m->config.media_1541 = 0;
    }
}

static void read_cart(snapshot_reader *r, c64_t *m) {
    c64_bus_t *bus = &m->bus;

    bus->cartridge_mounted = r_bool(r);
    bus->cartridge_roml_present = r_bool(r);
    bus->cartridge_romh_present = r_bool(r);
    bus->cartridge_exrom = r_u8(r);
    bus->cartridge_game = r_u8(r);
    bus->cartridge_mode = (c64_cartridge_mode)r_u32(r);
    r_bytes(r, bus->cartridge_roml, sizeof(bus->cartridge_roml));
    r_bytes(r, bus->cartridge_romh, sizeof(bus->cartridge_romh));
}

static void clear_drive_slot(c64_drive_slot *slot) {
    free(slot->image_bytes);
    free(slot->entries);
    memset(slot, 0, sizeof(*slot));
    slot->last_result = C64_DRIVE_STATUS_NOT_MOUNTED;
}

static void read_drive(snapshot_reader *r, c64_drive_slot *slot) {
    size_t i;
    size_t image_size;
    size_t entry_count;
    uint8_t *image = NULL;
    c64_drive_directory_entry *entries = NULL;

    clear_drive_slot(slot);
    slot->mounted = r_bool(r);
    slot->image_kind = (c64_drive_image_kind)r_u32(r);
    slot->last_result = (c64_drive_status_result)r_u32(r);
    r_bytes(r, slot->display_name, sizeof(slot->display_name));
    r_bytes(r, slot->disk_title, sizeof(slot->disk_title));
    r_bytes(r, slot->disk_id, sizeof(slot->disk_id));
    r_bytes(r, slot->dos_type, sizeof(slot->dos_type));
    slot->free_blocks = r_u16(r);

    image_size = r_size(r);
    if (r->ok && image_size > 0) {
        image = (uint8_t *)malloc(image_size);
        if (image == NULL) {
            r->ok = false;
            return;
        }
        r_bytes(r, image, image_size);
    }

    entry_count = r_size(r);
    if (r->ok && entry_count > 0) {
        if (entry_count > C64_SNAPSHOT_MAX_CHUNK_SIZE / sizeof(*entries)) {
            r->ok = false;
        } else {
            entries = (c64_drive_directory_entry *)calloc(entry_count, sizeof(*entries));
            if (entries == NULL) {
                r->ok = false;
            }
        }
    }

    for (i = 0; r->ok && i < entry_count; ++i) {
        entries[i].raw_type = r_u8(r);
        entries[i].type = (c64_drive_file_type)r_u32(r);
        entries[i].first_track = r_u8(r);
        entries[i].first_sector = r_u8(r);
        r_bytes(r, entries[i].filename, sizeof(entries[i].filename));
        entries[i].filename_length = r_size(r);
        entries[i].block_count = r_u16(r);
        if (entries[i].filename_length > sizeof(entries[i].filename)) {
            r->ok = false;
        }
    }

    if (!r->ok) {
        free(image);
        free(entries);
        return;
    }
    slot->image_bytes = image;
    slot->image_size = image_size;
    slot->entries = entries;
    slot->entry_count = entry_count;
}

static void read_cpu_fields(snapshot_reader *r, CPU *cpu) {
    cpu->pc = r_u16(r);
    cpu->opcode_pc = r_u16(r);
    cpu->sp = r_u16(r);
    cpu->A = r_u8(r);
    cpu->X = r_u8(r);
    cpu->Y = r_u8(r);
    cpu->flags = r_u8(r);
    cpu->address_16 = r_u16(r);
    cpu->scratch_16 = r_u16(r);
    cpu->page_fault = r_u8(r) ? 1u : 0u;
    cpu->irq_defer = r_u8(r);
    cpu->irq_defer_i = r_u8(r);
    cpu->opcode_active = r_u8(r);
    cpu->class = r_u32(r);
    cpu->cycles = r_u64(r);
    cpu->irq_entries = r_u64(r);
    cpu->nmi_entries = r_u64(r);
}

static void read_c6510_drive(snapshot_reader *r, C6510 *cpu) {
    read_cpu_fields(r, &cpu->cpu);
    cpu->bus_access_kind = (c6510_bus_access_kind)r_u32(r);
    cpu->micro_active = r_u8(r);
    cpu->micro_opcode = r_u8(r);
    cpu->micro_phase = r_u8(r);
    cpu->micro_branch_taken = r_u8(r);
    cpu->micro_target = r_u16(r);
    cpu->micro_interrupt_vector = r_u16(r);
    cpu->micro_is_interrupt = r_u8(r);
}

static void read_via(snapshot_reader *r, via6522 *v) {
    v->ora = r_u8(r);
    v->orb = r_u8(r);
    v->ddra = r_u8(r);
    v->ddrb = r_u8(r);
    v->port_a_in = r_u8(r);
    v->port_b_in = r_u8(r);
    v->t1_counter = r_u16(r);
    v->t1_latch = r_u16(r);
    v->t1_running = r_i32(r);
    v->t1_pb7_state = r_i32(r);
    v->t2_counter = r_u16(r);
    v->t2_latch_low = r_u8(r);
    v->t2_running = r_i32(r);
    v->sr = r_u8(r);
    v->acr = r_u8(r);
    v->pcr = r_u8(r);
    v->ifr = r_u8(r);
    v->ier = r_u8(r);
    v->ca1_last = r_u8(r);
}

static void read_media(snapshot_reader *r, c1541_media *media) {
    int i;
    bool has_built_from;

    c1541_media_free_tracks(media);

    media->enabled = r_i32(r);
    media->motor_on = r_i32(r);
    media->motor_ready = r_i32(r);
    media->motor_spin_left = r_u32(r);
    media->half_track = r_i32(r);
    media->stepper_phase = r_u8(r);
    media->density = r_i32(r);
    media->bit_acc = r_u32(r);
    media->ref_cycle = r_u32(r);
    media->bit_event_ref = r_u32(r);
    media->ref_tick_accum = r_u32(r);
    media->ref_advance = r_u32(r);
    media->req_ref_cycles = r_u32(r);
    media->flux_acc = r_u32(r);
    media->filter_counter = r_i32(r);
    media->filter_state = r_i32(r);
    media->filter_last_state = r_i32(r);
    media->no_flux_cycles = r_i32(r);
    media->flux_rand = r_u32(r);
    media->ue7_counter = r_i32(r);
    media->uf4_counter = r_i32(r);
    media->head_bit_pos = r_u32(r);
    media->shift10 = r_u16(r);
    media->in_sync = r_i32(r);
    media->bits_in_byte = r_i32(r);
    media->shifting_byte = r_u8(r);
    media->port_a_byte = r_u8(r);
    media->byte_ready = r_i32(r);
    media->so_pulse = r_i32(r);
    media->so_delay = r_i32(r);
    media->writing = r_i32(r);
    media->write_bits_left = r_i32(r);
    media->write_shift = r_u8(r);
    media->last_write_bit = r_i32(r);
    media->tracks_valid = r_i32(r);
    media->from_g64 = r_i32(r);
    has_built_from = r_bool(r);
    media->built_size = r_size(r);
    media->built_from_seq = r_u32(r);
    media->attach_left = r_u32(r);
    media->attach_pending = r_i32(r);
    /* Non-NULL sentinel: re-pointed to the restored slot image in apply. */
    media->built_from = has_built_from ? (const uint8_t *)(uintptr_t)1 : NULL;

    for (i = 0; i < C1541_MEDIA_HALF_SLOTS; ++i) {
        c1541_track *tr = &media->halves[i];
        bool present = r_bool(r);
        size_t length;
        uint8_t *data;

        tr->data = NULL;
        tr->length = 0;
        tr->density = 0;
        tr->dirty = 0;
        if (!present || !r->ok) {
            continue;
        }
        length = r_size(r);
        tr->density = r_i32(r);
        tr->dirty = r_i32(r);
        if (!r->ok || length == 0 || length > C64_SNAPSHOT_MAX_CHUNK_SIZE) {
            r->ok = false;
            return;
        }
        data = (uint8_t *)malloc(length);
        if (data == NULL) {
            r->ok = false;
            return;
        }
        r_bytes(r, data, length);
        if (!r->ok) {
            free(data);
            return;
        }
        tr->data = data;
        tr->length = length;
    }
}

static void read_drive_core(snapshot_reader *r, c1541 *drive) {
    r_bytes(r, drive->ram, sizeof(drive->ram));
    drive->rom_loaded = r_i32(r);
    drive->device_number = r_i32(r);
    drive->cpu_cycles_remaining = r_size(r);
    drive->via2_t1_pb7_last = r_i32(r);
    read_c6510_drive(r, &drive->cpu);
    read_via(r, &drive->via1);
    read_via(r, &drive->via2);
    read_media(r, &drive->media);
}

static void clear_media_track_ownership(c1541_media *media) {
    int i;

    for (i = 0; i < C1541_MEDIA_HALF_SLOTS; ++i) {
        media->halves[i].data = NULL;
        media->halves[i].length = 0;
        media->halves[i].density = 0;
        media->halves[i].dirty = 0;
    }
}

/* Move serialised drive-object state from src into dst, preserving host ROM
   bytes already loaded on dst. Track buffers transfer ownership from src. */
static void apply_drive_core(c1541 *dst, c1541 *src, c64_t *owner, const c64_drive_slot *slot) {
    uint8_t rom[C1541_ROM_SIZE];

    memcpy(rom, dst->rom, sizeof(rom));
    c1541_media_free_tracks(&dst->media);

    dst->cpu = src->cpu;
    dst->via1 = src->via1;
    dst->via2 = src->via2;
    memcpy(dst->ram, src->ram, sizeof(dst->ram));
    memcpy(dst->rom, rom, sizeof(dst->rom));
    dst->rom_loaded = src->rom_loaded;
    dst->device_number = src->device_number;
    dst->cpu_cycles_remaining = src->cpu_cycles_remaining;
    dst->via2_t1_pb7_last = src->via2_t1_pb7_last;
    dst->media = src->media;
    clear_media_track_ownership(&src->media);

    if (dst->media.built_from != NULL && slot != NULL && slot->image_bytes != NULL) {
        dst->media.built_from = slot->image_bytes;
        dst->media.built_size = slot->image_size;
        /* Slot image_content_seq is not in the DRV chunk; keep ensure_tracks
           from rebuilding over the verbatim GCR we just restored. */
        dst->media.built_from_seq = slot->image_content_seq;
    } else {
        dst->media.built_from = NULL;
        dst->media.built_size = 0;
        dst->media.built_from_seq = 0;
    }

    c1541_rewire(dst, owner);
}

static void move_drive_slot(c64_drive_slot *dst, c64_drive_slot *src) {
    clear_drive_slot(dst);
    *dst = *src;
    memset(src, 0, sizeof(*src));
    src->last_result = C64_DRIVE_STATUS_NOT_MOUNTED;
}

static void snapshot_clear_trace(c64_cpu_instruction_trace *trace) {
    trace->opcode_pc = 0;
    trace->event_count = 0;
    trace->total_cycles = 0;
}

static void apply_loaded_machine(c64_t *dst, c64_t *src, bool restore_1541_core) {
    c64_memory_access_fn memory_access = dst->memory_access;
    void *memory_access_user = dst->memory_access_user;
    bool cpu_trace_enabled = dst->cpu_trace_enabled;
    uint8_t basic_rom[C64_BASIC_ROM_SIZE];
    uint8_t kernal_rom[C64_KERNAL_ROM_SIZE];
    uint8_t char_rom[C64_CHAR_ROM_SIZE];
    bool has_basic_rom = dst->has_basic_rom;
    bool has_kernal_rom = dst->has_kernal_rom;
    bool has_character_rom = dst->has_character_rom;
    /* Shallow-preserve host drive objects (ROM + callbacks) for the legacy path. */
    c1541 drive8_host = dst->drive8;
    c1541 drive9_host = dst->drive9;

    memcpy(basic_rom, dst->bus.basic_rom, sizeof(basic_rom));
    memcpy(kernal_rom, dst->bus.kernal_rom, sizeof(kernal_rom));
    memcpy(char_rom, dst->bus.char_rom, sizeof(char_rom));

    memcpy(dst->bus.ram, src->bus.ram, sizeof(dst->bus.ram));
    memcpy(dst->bus.color_ram, src->bus.color_ram, sizeof(dst->bus.color_ram));
    memcpy(dst->bus.cartridge_roml, src->bus.cartridge_roml, sizeof(dst->bus.cartridge_roml));
    memcpy(dst->bus.cartridge_romh, src->bus.cartridge_romh, sizeof(dst->bus.cartridge_romh));
    memcpy(dst->bus.basic_rom, basic_rom, sizeof(dst->bus.basic_rom));
    memcpy(dst->bus.kernal_rom, kernal_rom, sizeof(dst->bus.kernal_rom));
    memcpy(dst->bus.char_rom, char_rom, sizeof(dst->bus.char_rom));
    dst->bus.cpu_port_direction = src->bus.cpu_port_direction;
    dst->bus.cpu_port_data = src->bus.cpu_port_data;
    dst->bus.screen_ram_writes = src->bus.screen_ram_writes;
    dst->bus.color_ram_writes = src->bus.color_ram_writes;
    dst->bus.vic_register_writes = src->bus.vic_register_writes;
    dst->bus.cia1_register_writes = src->bus.cia1_register_writes;
    dst->bus.cia2_register_writes = src->bus.cia2_register_writes;
    dst->bus.sid_register_writes = src->bus.sid_register_writes;
    dst->bus.vic_bank_base = src->bus.vic_bank_base;
    dst->bus.cartridge_mounted = src->bus.cartridge_mounted;
    dst->bus.cartridge_roml_present = src->bus.cartridge_roml_present;
    dst->bus.cartridge_romh_present = src->bus.cartridge_romh_present;
    dst->bus.cartridge_exrom = src->bus.cartridge_exrom;
    dst->bus.cartridge_game = src->bus.cartridge_game;
    dst->bus.cartridge_mode = src->bus.cartridge_mode;
    dst->cpu.cpu = src->cpu.cpu;
    dst->vic = src->vic;
    dst->cia1 = src->cia1;
    dst->cia2 = src->cia2;
    dst->sid = src->sid;
    c64_bus_attach_vicii(&dst->bus, &dst->vic);
    c64_bus_attach_cias(&dst->bus, &dst->cia1, &dst->cia2);
    c64_bus_attach_sid(&dst->bus, &dst->sid);
    dst->bus.vic_bank_base = src->bus.vic_bank_base;
    dst->keyboard = src->keyboard;
    dst->joystick1 = src->joystick1;
    dst->joystick2 = src->joystick2;
    dst->iec_external_pull = src->iec_external_pull;
    dst->iec_external_pull_other = src->iec_external_pull_other;
    dst->iec_external_pull_drive8 = src->iec_external_pull_drive8;
    dst->iec_external_pull_drive9 = src->iec_external_pull_drive9;
    dst->clock = src->clock;
    dst->keyboard_events = src->keyboard_events;
    dst->restore_requests = src->restore_requests;
    dst->restore_pending = src->restore_pending;
    dst->cia2_nmi_line = src->cia2_nmi_line;
    dst->cpu_cycles_remaining = src->cpu_cycles_remaining;
    dst->ready = src->ready;
    dst->config = src->config;
    dst->instruction_complete = src->instruction_complete;
    dst->has_basic_rom = has_basic_rom;
    dst->has_kernal_rom = has_kernal_rom;
    dst->has_character_rom = has_character_rom;
    dst->memory_access = memory_access;
    dst->memory_access_user = memory_access_user;
    dst->cpu_trace_enabled = cpu_trace_enabled;
    snapshot_clear_trace(&dst->last_cpu_trace);
    snapshot_clear_trace(&dst->pending_cpu_trace);
    memset(dst->write_history, 0, sizeof(dst->write_history));
    dst->cpu_trace_start_cycle = 0;
    dst->cpu_trace_start_cpu_cycle = 0;
    dst->pending_cpu_event_index = 0;
    dst->pending_cpu_elapsed = 0;
    dst->cpu_bus_mode = 0;
    dst->pending_cpu_trace_active = false;
    /* Snapshots are only written at a C64 instruction boundary (save refuses
       micro_active). The format does not store the resumable micro/bus fields
       or the BA-stall interrupt deferral. Clear host-side mid-instruction
       residue so loading into a free-running machine cannot resume a foreign
       micro-op under the restored PC — that path was observed as a spurious
       post-load BRK pause (title bar "BRK") after SYS 64738 then load-state. */
    dst->cpu.micro_active = 0;
    dst->cpu.micro_opcode = 0;
    dst->cpu.micro_phase = 0;
    dst->cpu.micro_branch_taken = 0;
    dst->cpu.micro_target = 0;
    dst->cpu.micro_interrupt_vector = 0;
    dst->cpu.micro_is_interrupt = 0;
    dst->cpu.bus_access_kind = C6510_BUS_ACCESS_DATA_READ;
    dst->cpu_prev_between_stall = false;
    dst->cpu_deferred_interrupt = C6510_INTERRUPT_NONE;

    dst->cia1.keyboard = NULL;
    dst->cia2.keyboard = NULL;
    cia_attach_port_input(&dst->cia1, src->cia1.port_input, dst);
    cia_attach_port_input(&dst->cia2, src->cia2.port_input, dst);
    dst->cpu.user = dst;

    move_drive_slot(&dst->drives[0], &src->drives[0]);
    move_drive_slot(&dst->drives[1], &src->drives[1]);

    if (restore_1541_core) {
        /* Host ROM bytes stay on dst; live drive state comes from src. */
        apply_drive_core(&dst->drive8, &src->drive8, dst, &dst->drives[0]);
        apply_drive_core(&dst->drive9, &src->drive9, dst, &dst->drives[1]);
    } else {
        /* Legacy v8 / deferred: keep host drive objects and hard-reset. */
        dst->drive8 = drive8_host;
        dst->drive9 = drive9_host;
        dst->drive8.c64 = dst;
        dst->drive9.c64 = dst;
        c1541_rewire(&dst->drive8, dst);
        c1541_rewire(&dst->drive9, dst);
        if (dst->config.emulate_1541 && (dst->drive8.rom_loaded || dst->drive9.rom_loaded)) {
            c1541_reset(&dst->drive8);
            c1541_reset(&dst->drive9);
        }
    }
}

static bool read_snapshot_into_temp(
    c64_t *temp,
    const c64_t *current,
    const uint8_t *in,
    size_t in_len,
    bool *out_restore_1541_core) {
    snapshot_reader top;
    snapshot_meta meta;
    parsed_chunks seen;
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t content_mode;
    uint32_t flags;
    bool restore_1541_core;

    memset(&seen, 0, sizeof(seen));
    memset(&meta, 0, sizeof(meta));
    if (out_restore_1541_core != NULL) {
        *out_restore_1541_core = false;
    }

    if (in == NULL || in_len < C64_SNAPSHOT_HEADER_SIZE) {
        return false;
    }

    top.data = in;
    top.len = in_len;
    top.pos = 0;
    top.ok = true;
    magic = r_u32(&top);
    version = r_u32(&top);
    header_size = r_u32(&top);
    content_mode = r_u32(&top);
    flags = r_u32(&top);
    (void)r_u32(&top);
    (void)r_u32(&top);
    (void)r_u32(&top);
    if (!top.ok ||
        magic != C64_SNAPSHOT_MAGIC ||
        version < C64_SNAPSHOT_VERSION_MIN ||
        version > C64_SNAPSHOT_VERSION ||
        header_size < C64_SNAPSHOT_HEADER_SIZE ||
        header_size > in_len ||
        content_mode != C64_SNAPSHOT_CONTENT_REFERENCED) {
        return false;
    }
    top.pos = header_size;
    restore_1541_core = snapshot_includes_1541_core(version, flags);

    c64_init(temp);
    memcpy(temp->bus.basic_rom, current->bus.basic_rom, sizeof(temp->bus.basic_rom));
    memcpy(temp->bus.kernal_rom, current->bus.kernal_rom, sizeof(temp->bus.kernal_rom));
    memcpy(temp->bus.char_rom, current->bus.char_rom, sizeof(temp->bus.char_rom));
    temp->has_basic_rom = current->has_basic_rom;
    temp->has_kernal_rom = current->has_kernal_rom;
    temp->has_character_rom = current->has_character_rom;
    temp->ready = current->ready;

    while (top.ok && top.pos < top.len) {
        uint32_t tag;
        uint32_t length;
        snapshot_reader chunk;

        if (top.len - top.pos < C64_SNAPSHOT_CHUNK_HEADER_SIZE) {
            top.ok = false;
            break;
        }
        tag = r_u32(&top);
        length = r_u32(&top);
        if (length > C64_SNAPSHOT_MAX_CHUNK_SIZE || length > top.len || top.pos > top.len - length) {
            top.ok = false;
            break;
        }

        chunk.data = top.data + top.pos;
        chunk.len = length;
        chunk.pos = 0;
        chunk.ok = true;

        switch (tag) {
        case TAG_META:
            seen.meta = read_meta(&chunk, &meta);
            break;
        case TAG_RAM:
            read_ram(&chunk, temp);
            seen.ram = chunk.ok;
            break;
        case TAG_BUS:
            read_bus(&chunk, temp);
            seen.bus = chunk.ok;
            break;
        case TAG_CPU:
            read_cpu(&chunk, temp);
            seen.cpu = chunk.ok;
            break;
        case TAG_VIC:
            read_vic(&chunk, temp);
            seen.vic = chunk.ok;
            break;
        case TAG_CIA1:
            read_cia(&chunk, &temp->cia1);
            seen.cia1 = chunk.ok;
            break;
        case TAG_CIA2:
            read_cia(&chunk, &temp->cia2);
            seen.cia2 = chunk.ok;
            break;
        case TAG_SID:
            read_sid(&chunk, temp);
            seen.sid = chunk.ok;
            break;
        case TAG_MACH:
            read_mach(&chunk, temp, version);
            seen.mach = chunk.ok;
            break;
        case TAG_CART:
            read_cart(&chunk, temp);
            seen.cart = chunk.ok;
            break;
        case TAG_DRV8:
            read_drive(&chunk, &temp->drives[0]);
            seen.drv8 = chunk.ok;
            break;
        case TAG_DRV9:
            read_drive(&chunk, &temp->drives[1]);
            seen.drv9 = chunk.ok;
            break;
        case TAG_DR8C:
            read_drive_core(&chunk, &temp->drive8);
            seen.drv8_core = chunk.ok;
            break;
        case TAG_DR9C:
            read_drive_core(&chunk, &temp->drive9);
            seen.drv9_core = chunk.ok;
            break;
        default:
            chunk.pos = chunk.len;
            break;
        }

        if (!chunk.ok || chunk.pos != chunk.len) {
            top.ok = false;
            break;
        }
        top.pos += length;
    }

    if (!top.ok || top.pos != top.len) {
        return false;
    }
    if (!seen.meta || !seen.ram || !seen.bus || !seen.cpu || !seen.vic ||
        !seen.cia1 || !seen.cia2 || !seen.sid || !seen.mach ||
        !seen.cart || !seen.drv8 || !seen.drv9) {
        return false;
    }
    if (restore_1541_core && (!seen.drv8_core || !seen.drv9_core)) {
        return false;
    }
    if (meta.flags != flags || !meta_matches_loaded_roms(current, &meta)) {
        return false;
    }
    if (out_restore_1541_core != NULL) {
        *out_restore_1541_core = restore_1541_core;
    }
    return true;
}

size_t c64_snapshot_size(const c64_t *m) {
    size_t size = 0;

    if (!write_snapshot(m, NULL, 0, &size)) {
        return 0;
    }
    return size;
}

size_t c64_snapshot_save(const c64_t *m, uint8_t *out, size_t out_cap) {
    size_t size = 0;

    if (out == NULL || !write_snapshot(m, out, out_cap, &size)) {
        return 0;
    }
    return size;
}

bool c64_snapshot_load(c64_t *m, const uint8_t *in, size_t in_len) {
    c64_t *temp;
    bool ok;
    bool restore_1541_core = false;

    if (m == NULL || in == NULL) {
        return false;
    }

    temp = (c64_t *)calloc(1, sizeof(*temp));
    if (temp == NULL) {
        return false;
    }

    ok = read_snapshot_into_temp(temp, m, in, in_len, &restore_1541_core);
    if (!ok) {
        c1541_destroy(&temp->drive8);
        c1541_destroy(&temp->drive9);
        c64_unmount_all_drives(temp);
        free(temp);
        return false;
    }

    apply_loaded_machine(m, temp, restore_1541_core);
    c1541_destroy(&temp->drive8);
    c1541_destroy(&temp->drive9);
    c64_unmount_all_drives(temp);
    free(temp);
    return true;
}
