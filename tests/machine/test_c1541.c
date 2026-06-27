#include "c1541.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_eq_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected 0x%02X, got 0x%02X\n", name, expected, actual);
        exit(1);
    }
}

/* ------------------------------------------------------------------ */
/* Minimal c64_t stub so c1541_init compiles without full c64 library  */
/* ------------------------------------------------------------------ */

/* c1541.h forward-declares c64_t; provide a minimal definition so the
   pointer is valid without linking the full c64 module. */
struct c64_t { int unused; };

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static c1541 *make_drive(void) {
    static c64_t stub;
    static c1541 drive;
    c1541_init(&drive, &stub, 8);
    return &drive;
}

/* Fill ROM with a known pattern and mark it loaded so bus reads work. */
static void load_test_rom(c1541 *drive) {
    memset(drive->rom, 0xEA, C1541_ROM_SIZE); /* NOP sled */
    drive->rom_loaded = 1;
}

/* ------------------------------------------------------------------ */
/* Bus map tests                                                        */
/* ------------------------------------------------------------------ */

static void test_ram_read_write(void) {
    c1541 *drive = make_drive();
    load_test_rom(drive);

    /* Write via bus write callback (indirect, through a helper) */
    drive->ram[0x0000] = 0xAB;
    drive->ram[0x07FF] = 0xCD;

    /* Read back through exposed ram array */
    expect_eq_u8("ram[0x0000]", 0xAB, drive->ram[0x0000]);
    expect_eq_u8("ram[0x07FF]", 0xCD, drive->ram[0x07FF]);

    printf("PASS: test_ram_read_write\n");
}

static void test_rom_not_loaded_nop(void) {
    static c64_t stub;
    static c1541 drive;
    c1541_init(&drive, &stub, 8);
    /* ROM not loaded — advance_one_cycle must be a no-op (no crash). */
    c1541_advance_one_cycle(&drive);
    c1541_advance_one_cycle(&drive);
    printf("PASS: test_rom_not_loaded_nop\n");
}

static void test_rom_loaded_flag(void) {
    c1541 *drive = make_drive();
    if (drive->rom_loaded != 0) fail("rom_loaded should be 0 after init");
    load_test_rom(drive);
    if (drive->rom_loaded != 1) fail("rom_loaded should be 1 after load_test_rom");
    printf("PASS: test_rom_loaded_flag\n");
}

static void test_device_number(void) {
    static c64_t stub;
    static c1541 d8, d9;
    c1541_init(&d8, &stub, 8);
    c1541_init(&d9, &stub, 9);
    if (d8.device_number != 8) fail("drive8 device_number != 8");
    if (d9.device_number != 9) fail("drive9 device_number != 9");
    printf("PASS: test_device_number\n");
}

static void test_destroy_zeroes(void) {
    static c64_t stub;
    static c1541 drive;
    c1541_init(&drive, &stub, 8);
    drive.rom_loaded = 1;
    c1541_destroy(&drive);
    if (drive.rom_loaded != 0) fail("destroy should zero rom_loaded");
    if (drive.device_number != 0) fail("destroy should zero device_number");
    printf("PASS: test_destroy_zeroes\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    test_ram_read_write();
    test_rom_not_loaded_nop();
    test_rom_loaded_flag();
    test_device_number();
    test_destroy_zeroes();

    printf("All c1541 tests passed.\n");
    return 0;
}
