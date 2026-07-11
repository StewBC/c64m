/*
 * c64m CIA timing corpus runner.
 *
 * Loads a VICE-style test PRG (BASIC SYS header), enables the $D7FF debugcart,
 * autostarts via keyboard-buffer RUN, and steps until debugcart write or cycle
 * limit. Does NOT poll ICR mid-race.
 *
 * Usage:
 *   run_c64m --prg path.prg [--limit N] [--pal|--ntsc] [--boot-cycles N]
 * Exit: 0 pass, 255 fail, 1 timeout, 2 usage/error
 */
#include "c64.h"
#include "c64_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum {
    DEFAULT_BOOT_CYCLES = 4000000u,
    KEYBOARD_BUFFER = 0x0277,
    KEYBOARD_NDX = 0x00c6,
    BASIC_TXTTAB = 0x002b,
    BASIC_VARTAB = 0x002d,
    BASIC_ARYTAB = 0x002f,
    BASIC_STREND = 0x0031
};

static int read_file(const char *path, uint8_t **out_bytes, size_t *out_size) {
    FILE *file;
    long size;
    uint8_t *bytes;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return 0;
    }
    fclose(file);
    *out_bytes = bytes;
    *out_size = (size_t)size;
    return 1;
}

static void write_zp16(c64_t *machine, uint16_t addr, uint16_t value) {
    c64_debug_write_ram(machine, addr, (uint8_t)(value & 0xffu));
    c64_debug_write_ram(machine, (uint16_t)(addr + 1u), (uint8_t)(value >> 8));
}

static int install_roms(c64_t *machine, c64_video_standard standard) {
    c64_rom_set roms;
    c64_config config;
    char error[256];

    c64_rom_set_init(&roms);
    if (!c64_rom_load_combined_64c(
            &roms,
            C64M_SOURCE_DIR "/roms/system.rom",
            error,
            sizeof(error))) {
        fprintf(stderr, "rom load failed: %s\n", error);
        return 0;
    }
    if (!c64_rom_load_character(
            &roms,
            C64M_SOURCE_DIR "/roms/character.rom",
            error,
            sizeof(error))) {
        fprintf(stderr, "char rom load failed: %s\n", error);
        return 0;
    }

    c64_init(machine);
    config = machine->config;
    config.video_standard = standard;
    config.emulate_1541 = 0;
    config.media_1541 = 0;
    c64_set_config(machine, &config);
    if (!c64_install_roms(machine, &roms, error, sizeof(error))) {
        fprintf(stderr, "install roms failed: %s\n", error);
        return 0;
    }
    if (!c64_reset(machine, error, sizeof(error))) {
        fprintf(stderr, "reset failed: %s\n", error);
        return 0;
    }
    return 1;
}

static void step_cycles(c64_t *machine, uint64_t count) {
    char error[256];
    uint64_t i;

    for (i = 0; i < count; ++i) {
        if (!c64_step_cycle(machine, error, sizeof(error))) {
            fprintf(stderr, "step failed: %s\n", error);
            return;
        }
    }
}

/* Inject PETSCII "RUN\r" into the KERNAL keyboard buffer (classic autostart). */
static void inject_run(c64_t *machine) {
    c64_debug_write_ram(machine, KEYBOARD_BUFFER + 0, 0x52u); /* R */
    c64_debug_write_ram(machine, KEYBOARD_BUFFER + 1, 0x55u); /* U */
    c64_debug_write_ram(machine, KEYBOARD_BUFFER + 2, 0x4eu); /* N */
    c64_debug_write_ram(machine, KEYBOARD_BUFFER + 3, 0x0du); /* CR */
    c64_debug_write_ram(machine, KEYBOARD_NDX, 4);
}

static int inject_prg(c64_t *machine, const uint8_t *bytes, size_t length, uint16_t *out_end) {
    uint16_t load_address;
    size_t payload;
    size_t i;

    if (bytes == NULL || length < 2u) {
        return 0;
    }
    load_address = (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
    payload = length - 2u;
    if ((uint32_t)load_address + (uint32_t)payload > 0x10000u) {
        return 0;
    }
    for (i = 0; i < payload; ++i) {
        c64_debug_write_ram(machine, (uint16_t)(load_address + i), bytes[i + 2u]);
    }
    *out_end = (uint16_t)(load_address + payload);
    /* BASIC program pointers: start at load, variables after image. */
    if (load_address == 0x0801u || load_address < 0x1000u) {
        write_zp16(machine, BASIC_TXTTAB, load_address);
        write_zp16(machine, BASIC_VARTAB, *out_end);
        write_zp16(machine, BASIC_ARYTAB, *out_end);
        write_zp16(machine, BASIC_STREND, *out_end);
    }
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s --prg path.prg [--limit N] [--boot-cycles N] [--pal|--ntsc]\n",
        argv0);
}

int main(int argc, char **argv) {
    const char *prg_path = NULL;
    uint64_t limit = 20000000ull;
    uint64_t boot_cycles = DEFAULT_BOOT_CYCLES;
    c64_video_standard standard = C64_VIDEO_STANDARD_PAL;
    c64_t machine;
    uint8_t *prg = NULL;
    size_t prg_size = 0;
    uint16_t end_addr = 0;
    char error[256];
    uint64_t i;
    int ai;

    for (ai = 1; ai < argc; ++ai) {
        if (strcmp(argv[ai], "--prg") == 0 && ai + 1 < argc) {
            prg_path = argv[++ai];
        } else if (strcmp(argv[ai], "--limit") == 0 && ai + 1 < argc) {
            limit = strtoull(argv[++ai], NULL, 0);
        } else if (strcmp(argv[ai], "--boot-cycles") == 0 && ai + 1 < argc) {
            boot_cycles = strtoull(argv[++ai], NULL, 0);
        } else if (strcmp(argv[ai], "--pal") == 0) {
            standard = C64_VIDEO_STANDARD_PAL;
        } else if (strcmp(argv[ai], "--ntsc") == 0) {
            standard = C64_VIDEO_STANDARD_NTSC;
        } else if (strcmp(argv[ai], "-h") == 0 || strcmp(argv[ai], "--help") == 0) {
            usage(argv[0]);
            return 2;
        } else {
            fprintf(stderr, "unknown arg: %s\n", argv[ai]);
            usage(argv[0]);
            return 2;
        }
    }

    if (prg_path == NULL) {
        usage(argv[0]);
        return 2;
    }
    if (!read_file(prg_path, &prg, &prg_size)) {
        fprintf(stderr, "failed to read %s\n", prg_path);
        return 2;
    }
    if (!install_roms(&machine, standard)) {
        free(prg);
        return 2;
    }

    c64_set_debugcart_enabled(&machine, true);
    c64_clear_debugcart(&machine);

    /* Boot to BASIC READY. */
    step_cycles(&machine, boot_cycles);
    if (c64_debugcart_hit(&machine)) {
        /* Unexpected early exit during boot. */
        int code = (int)c64_debugcart_value(&machine);
        free(prg);
        return code == 0 ? 0 : (code == 255 ? 255 : 1);
    }

    if (!inject_prg(&machine, prg, prg_size, &end_addr)) {
        fprintf(stderr, "failed to inject PRG\n");
        free(prg);
        return 2;
    }
    free(prg);
    inject_run(&machine);

    for (i = 0; i < limit; ++i) {
        if (!c64_step_cycle(&machine, error, sizeof(error))) {
            fprintf(stderr, "step failed at %llu: %s\n", (unsigned long long)i, error);
            return 2;
        }
        if (c64_debugcart_hit(&machine)) {
            uint8_t code = c64_debugcart_value(&machine);
            /* Match VICE process exit: 0 pass, 255 fail. */
            if (code == 0) {
                return 0;
            }
            if (code == 0xffu) {
                return 255;
            }
            return (int)code;
        }
    }

    return 1; /* timeout */
}
