#include "c1541.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Bus callbacks (static — not exposed)                                */
/* ------------------------------------------------------------------ */

static uint8_t c1541_bus_read(void *user, uint16_t addr) {
    c1541 *drive = (c1541 *)user;
    if (addr < 0x0800u) return drive->ram[addr];
    if (addr < 0x1000u) return drive->ram[addr & 0x07FFu];
    /* Check VIA #2 before VIA #1 — $1C00–$1FFF is a subset of $1800–$1FFF */
    if (addr >= 0x1C00u && addr < 0x2000u) return via6522_read(&drive->via2, (uint8_t)(addr & 0x0Fu));
    if (addr >= 0x1800u && addr < 0x2000u) return via6522_read(&drive->via1, (uint8_t)(addr & 0x0Fu));
    if (addr >= 0xC000u) return drive->rom[addr - 0xC000u];
    return 0xFFu;
}

static void c1541_bus_write(void *user, uint16_t addr, uint8_t value) {
    c1541 *drive = (c1541 *)user;
    if (addr < 0x0800u) { drive->ram[addr] = value; return; }
    if (addr < 0x1000u) { drive->ram[addr & 0x07FFu] = value; return; }
    if (addr >= 0x1C00u && addr < 0x2000u) { via6522_write(&drive->via2, (uint8_t)(addr & 0x0Fu), value); return; }
    if (addr >= 0x1800u && addr < 0x2000u) { via6522_write(&drive->via1, (uint8_t)(addr & 0x0Fu), value); return; }
    /* ROM and unmapped: ignore */
}

/* ------------------------------------------------------------------ */
/* IRQ / NMI callbacks                                                 */
/* ------------------------------------------------------------------ */

static uint8_t c1541_irq_pending(void *user) {
    c1541 *drive = (c1541 *)user;
    return (uint8_t)(via6522_irq_pending(&drive->via1) || via6522_irq_pending(&drive->via2));
}

/* CA1 on VIA #2 is the ATN line; IFR bit 1 & IER bit 1 together = NMI pending. */
static uint8_t c1541_nmi_pending(void *user) {
    c1541 *drive = (c1541 *)user;
    return (drive->via2.ifr & drive->via2.ier & 0x02u) ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void c1541_init(c1541 *drive, c64_t *c64, int device_number) {
    memset(drive, 0, sizeof(c1541));
    drive->c64 = c64;
    drive->device_number = device_number;
    via6522_init(&drive->via1);
    via6522_init(&drive->via2);
    c6510_init(&drive->cpu, drive, c1541_bus_read, c1541_bus_write);
    c6510_set_irq_pending_callback(&drive->cpu, c1541_irq_pending);
    c6510_set_nmi_pending_callback(&drive->cpu, c1541_nmi_pending);
}

void c1541_destroy(c1541 *drive) {
    memset(drive, 0, sizeof(c1541));
}

void c1541_reset(c1541 *drive) {
    memset(drive->ram, 0, C1541_RAM_SIZE);
    via6522_reset(&drive->via1);
    via6522_reset(&drive->via2);
    /* c6510_reset() reads $FFFC/$FFFD through the bus callbacks.
       With ROM loaded at $C000–$FFFF this correctly fetches the 1541 reset vector. */
    c6510_reset(&drive->cpu);
}

/* ------------------------------------------------------------------ */
/* ROM loading                                                         */
/* ------------------------------------------------------------------ */

int c1541_load_rom(c1541 *drive, const char *path) {
    FILE *f;
    size_t n;

    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(drive->rom, 1, C1541_ROM_SIZE, f);
    fclose(f);
    if (n != C1541_ROM_SIZE) {
        memset(drive->rom, 0, C1541_ROM_SIZE);
        return 0;
    }
    drive->rom_loaded = 1;
    return 1;
}

int c1541_load_rom_split(c1541 *drive, const char *path_lo, const char *path_hi) {
    FILE *f;
    size_t n;

    f = fopen(path_lo, "rb");
    if (!f) return 0;
    n = fread(drive->rom, 1, C1541_ROM_SIZE / 2, f);
    fclose(f);
    if (n != (size_t)(C1541_ROM_SIZE / 2)) {
        memset(drive->rom, 0, C1541_ROM_SIZE);
        return 0;
    }

    f = fopen(path_hi, "rb");
    if (!f) { memset(drive->rom, 0, C1541_ROM_SIZE); return 0; }
    n = fread(drive->rom + C1541_ROM_SIZE / 2, 1, C1541_ROM_SIZE / 2, f);
    fclose(f);
    if (n != (size_t)(C1541_ROM_SIZE / 2)) {
        memset(drive->rom, 0, C1541_ROM_SIZE);
        return 0;
    }

    drive->rom_loaded = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Cycle step                                                          */
/* ------------------------------------------------------------------ */

void c1541_advance_one_cycle(c1541 *drive) {
    if (!drive->rom_loaded) return;
    via6522_step(&drive->via1);
    via6522_step(&drive->via2);
    c6510_step(&drive->cpu);
}
