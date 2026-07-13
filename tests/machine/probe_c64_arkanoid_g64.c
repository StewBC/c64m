/*
 * Arkanoid (Imagine 1988) G64 load regression.
 *
 * Drives the real KERNAL / 1541 ROM / G64 media / IEC path with no
 * title-specific behaviour: PAL C64, true 1541 + GCR media, the exact PETSCII
 * LOAD "*",8,1 in the keyboard buffer. Arkanoid's V-MAX bootstrap then loads
 * "F" and runs its timed dual-bit fast loader, handing off at $9400.
 *
 * The loaded payload is asserted byte-for-byte against a VICE 3.10 true-drive
 * capture at the $9400 handoff (the same image on real-drive VICE). In
 * particular $5F5C..$5F6F is the receiver-installed IRQ code whose control-flow
 * bytes were corrupted before the IEC timing model matched VICE (e.g. $5F64 read
 * $30 BMI — impossible after AND #$01 — instead of $F0 BEQ).
 */
#include "c64.h"
#include "c64_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

static void die(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void require(const char *name, int value) {
    if (!value) {
        die(name);
    }
}

static int read_file(const char *path, uint8_t **out_bytes, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;

    if (file == NULL) return 0;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) { fclose(file); return 0; }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) { fclose(file); return 0; }
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

static void install_media_pal(c64_t *machine) {
    c64_rom_set roms;
    c64_config config;
    char error[256];

    c64_rom_set_init(&roms);
    require("load system rom", c64_rom_load_combined_64c(
        &roms, C64M_SOURCE_DIR "/roms/system.rom", error, sizeof(error)));
    require("load character rom", c64_rom_load_character(
        &roms, C64M_SOURCE_DIR "/roms/character.rom", error, sizeof(error)));

    c64_init(machine);
    config = machine->config;
    config.emulate_1541 = 1;
    config.media_1541 = 1;
    config.video_standard = C64_VIDEO_STANDARD_PAL;
    c64_set_config(machine, &config);
    require("load 1541 rom", c1541_load_rom(
        &machine->drive8, C64M_SOURCE_DIR "/roms/1541.rom") != 0);
    require("install roms", c64_install_roms(machine, &roms, error, sizeof(error)));
    require("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void mount_arkanoid(c64_t *machine, int alt) {
    char path[512];
    uint8_t *bytes = NULL;
    size_t size = 0;

    snprintf(path, sizeof(path), "%s/assets/disks/arkanoid[imagine_1988](pal)%s.g64",
        C64M_SOURCE_DIR, alt ? "(alt)" : "");
    require("read g64", read_file(path, &bytes, &size));
    require("mount g64", c64_mount_g64(machine, 8, bytes, size, "arkanoid.g64") ==
        C64_DRIVE_STATUS_OK);
    free(bytes);
}

static void queue_load_star(c64_t *machine) {
    static const uint8_t command[] = { 'L', 'O', 'A', 'D', ' ', '"', '*', '"', ',', '8', ',', '1', 0x0d };
    memcpy(machine->bus.ram + 0x0277u, command, sizeof(command));
    machine->bus.ram[0x00c6u] = (uint8_t)sizeof(command);
}

/* VICE 3.10 true-drive captures of each image at the $9400 handoff. The two
   dumps differ in a few payload bytes ($9407, $5F60/$5F67) but agree on the
   fix's smoking gun at $5F64 = $F0 (BEQ), which read $30 (BMI) when the IEC
   timing was wrong. */
static const uint8_t vice_9400[8] = { 0xA9, 0x35, 0x85, 0x01, 0x80, 0xAD, 0x4C, 0x5A };
static const uint8_t vice_5f50[32] = {
    0x08, 0x20, 0x49, 0xA9, 0x8D, 0x0C, 0xDC, 0xA9, 0xD9, 0x8D, 0x0E, 0xDC, 0xAD, 0x0D, 0xDC, 0x8D,
    0x65, 0xBA, 0x29, 0x01, 0xF0, 0xF6, 0xAD, 0x65, 0xBA, 0x29, 0x08, 0xC9, 0x08, 0xD0, 0xE8, 0x8D
};
static const uint8_t vice_9400_alt[8] = { 0xA9, 0x35, 0x85, 0x01, 0x80, 0xAD, 0x4C, 0x00 };
static const uint8_t vice_5f50_alt[32] = {
    0x08, 0x20, 0x49, 0xA9, 0x8D, 0x0C, 0xDC, 0xA9, 0xD9, 0x8D, 0x0E, 0xDC, 0xAD, 0x0D, 0xDC, 0x8D,
    0x6C, 0xBA, 0x29, 0x01, 0xF0, 0xF6, 0xAD, 0x6C, 0xBA, 0x29, 0x08, 0xC9, 0x08, 0xD0, 0xE8, 0x8D
};

static void assert_region(const c64_t *m, uint16_t base, const uint8_t *want, size_t n,
                          const char *label) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (m->bus.ram[base + i] != want[i]) {
            fprintf(stderr, "%s mismatch at $%04X: got", label, (unsigned)(base + i));
            for (i = 0; i < n; ++i) fprintf(stderr, " %02X", m->bus.ram[base + i]);
            fprintf(stderr, "\nexpected (VICE):");
            for (i = 0; i < n; ++i) fprintf(stderr, " %02X", want[i]);
            fprintf(stderr, "\n");
            die(label);
        }
    }
}

int main(int argc, char **argv) {
    c64_t machine;
    char error[256];
    uint64_t cycle;
    int alt = argc > 1 && strcmp(argv[1], "alt") == 0;
    int saw_9400 = 0;

    install_media_pal(&machine);
    mount_arkanoid(&machine, alt);

    for (cycle = 0; cycle < 2500000ull; ++cycle) {
        require("boot", c64_step_cycle(&machine, error, sizeof(error)));
    }
    queue_load_star(&machine);

    /* Run the real load until the V-MAX bootstrap hands off at $9400. */
    for (cycle = 0; cycle < 200000000ull && !saw_9400; ++cycle) {
        require("step", c64_step_cycle(&machine, error, sizeof(error)));
        if (!machine.cpu.micro_active && !machine.pending_cpu_trace_active &&
            machine.cpu.cpu.pc == 0x9400u) {
            saw_9400 = 1;
        }
    }
    require("reached $9400 handoff", saw_9400);

    assert_region(&machine, 0x9400u, alt ? vice_9400_alt : vice_9400, 8, "$9400 handoff");
    assert_region(&machine, 0x5f50u, alt ? vice_5f50_alt : vice_5f50, 32, "$5F50 receiver code");

    printf("PASS: arkanoid%s g64 load matches VICE at $9400\n", alt ? " (alt)" : "");
    return 0;
}
