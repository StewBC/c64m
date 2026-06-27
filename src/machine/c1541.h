#ifndef C1541_H
#define C1541_H

#include "c6510.h"
#include "via6522.h"

#include <stdint.h>

/* Forward declaration — avoids circular include with c64.h. */
typedef struct c64_t c64_t;

#define C1541_ROM_SIZE  16384
#define C1541_RAM_SIZE  2048

typedef struct c1541 {
    C6510    cpu;
    via6522  via1;          /* parallel port (disk mechanics stub) */
    via6522  via2;          /* serial IEC port */
    uint8_t  ram[C1541_RAM_SIZE];
    uint8_t  rom[C1541_ROM_SIZE];
    int      rom_loaded;    /* 1 if ROM was successfully loaded */
    c64_t   *c64;           /* back-pointer; used in Phase 3 for IEC bus */
    int      device_number; /* 8 or 9 */
} c1541;

void c1541_init(c1541 *drive, c64_t *c64, int device_number);
void c1541_destroy(c1541 *drive);
void c1541_reset(c1541 *drive);
int  c1541_load_rom(c1541 *drive, const char *path);
int  c1541_load_rom_split(c1541 *drive, const char *path_lo, const char *path_hi);
void c1541_advance_one_cycle(c1541 *drive);

#endif /* C1541_H */
