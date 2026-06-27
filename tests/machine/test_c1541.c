#include "c1541.h"
#include "c64.h"

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

static void expect_eq_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected 0x%04X, got 0x%04X\n", name, expected, actual);
        exit(1);
    }
}

/* Fill ROM with NOPs (0xEA) and mark it loaded so advance_one_cycle proceeds. */
static void load_nop_rom(c1541 *drive) {
    memset(drive->rom, 0xEA, C1541_ROM_SIZE);
    drive->rom_loaded = 1;
}

/* ------------------------------------------------------------------ */
/* Phase 2: Bus map and lifecycle tests                                */
/* ------------------------------------------------------------------ */

static void test_ram_read_write(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);

    drive.ram[0x0000] = 0xAB;
    drive.ram[0x07FF] = 0xCD;

    expect_eq_u8("ram[0x0000]", 0xAB, drive.ram[0x0000]);
    expect_eq_u8("ram[0x07FF]", 0xCD, drive.ram[0x07FF]);

    printf("PASS: test_ram_read_write\n");
}

static void test_rom_not_loaded_nop(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    /* ROM not loaded — advance_one_cycle must be a no-op (no crash). */
    c1541_advance_one_cycle(&drive);
    c1541_advance_one_cycle(&drive);
    printf("PASS: test_rom_not_loaded_nop\n");
}

static void test_rom_loaded_flag(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    if (drive.rom_loaded != 0) fail("rom_loaded should be 0 after init");
    load_nop_rom(&drive);
    if (drive.rom_loaded != 1) fail("rom_loaded should be 1 after load_nop_rom");
    printf("PASS: test_rom_loaded_flag\n");
}

static void test_device_number(void) {
    static c64_t c64;
    static c1541 d8, d9;
    c64_init(&c64);
    c1541_init(&d8, &c64, 8);
    c1541_init(&d9, &c64, 9);
    if (d8.device_number != 8) fail("d8 device_number != 8");
    if (d9.device_number != 9) fail("d9 device_number != 9");
    printf("PASS: test_device_number\n");
}

static void test_destroy_zeroes(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    drive.rom_loaded = 1;
    c1541_destroy(&drive);
    if (drive.rom_loaded != 0) fail("destroy should zero rom_loaded");
    if (drive.device_number != 0) fail("destroy should zero device_number");
    printf("PASS: test_destroy_zeroes\n");
}

/* ------------------------------------------------------------------ */
/* Phase 3A: IEC bus wiring                                            */
/* ------------------------------------------------------------------ */

/* Advancing one cycle with ROM loaded synchronises serial VIA output → C64 pull.
   VIA1 PB1 = DATA out; DDRB=0x02 (output), ORB bit 1 high → 1541 pulls DATA.
   After advance, c64.iec_external_pull should have C64_IEC_DATA set. */
static void test_iec_drive_pulls_data(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    drive.via1.ddrb = 0x02u; /* bit 1 = DATA output */
    drive.via1.orb  = 0x02u; /* bit 1 = 1 → pulls DATA low */

    c1541_advance_one_cycle(&drive);

    if (!(c64.iec_external_pull & C64_IEC_DATA))
        fail("iec_drive_pulls_data: DATA should be pulled");
    if (c64.iec_external_pull & C64_IEC_CLK)
        fail("iec_drive_pulls_data: CLK should not be pulled");
    if (c64.iec_external_pull & C64_IEC_ATN)
        fail("iec_drive_pulls_data: ATN should not be pulled");

    printf("PASS: test_iec_drive_pulls_data\n");
}

/* When 1541 releases DATA (PB1 output = 0), iec_external_pull should clear DATA. */
static void test_iec_drive_releases_data(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    drive.via1.ddrb = 0x02u; /* bit 1 = DATA output */
    drive.via1.orb  = 0x00u; /* bit 1 = 0 → not driving DATA low */

    c1541_advance_one_cycle(&drive);

    if (c64.iec_external_pull & C64_IEC_DATA)
        fail("iec_drive_releases_data: DATA should not be pulled");

    printf("PASS: test_iec_drive_releases_data\n");
}

static void test_cpu_instruction_cycles_are_throttled(void) {
    static c64_t c64;
    static c1541 drive;
    uint16_t pc_after_first;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    drive.cpu.cpu.pc = 0xC000u;

    c1541_advance_one_cycle(&drive);
    pc_after_first = drive.cpu.cpu.pc;
    c1541_advance_one_cycle(&drive);

    expect_eq_u16("throttled NOP PC", pc_after_first, drive.cpu.cpu.pc);
    if (drive.cpu_cycles_remaining != 0)
        fail("throttled NOP should consume its second cycle after two advances");

    c1541_advance_one_cycle(&drive);
    expect_eq_u16("next NOP PC", (uint16_t)(pc_after_first + 1u), drive.cpu.cpu.pc);

    printf("PASS: test_cpu_instruction_cycles_are_throttled\n");
}

static void test_via1_ca1_routes_to_irq(void) {
    static c64_t c64;
    static c1541 drive;
    int i;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    drive.cpu.cpu.pc = 0xC000u;
    drive.cpu.cpu.flags &= (uint8_t)~0x04u; /* IRQs enabled */
    drive.via1.ifr |= 0x02u; /* serial VIA CA1 flag */
    drive.via1.ier |= 0x02u; /* serial VIA CA1 enabled */

    for (i = 0; i < 32; i++) {
        c1541_advance_one_cycle(&drive);
    }

    if (drive.cpu.cpu.irq_entries != 1u)
        fail("serial VIA CA1 should enter the CPU IRQ path");
    if (drive.cpu.cpu.nmi_entries != 0u)
        fail("serial VIA CA1 must not enter the CPU NMI path");

    printf("PASS: test_via1_ca1_routes_to_irq\n");
}

static void test_via2_timer_pb7_sets_cpu_overflow(void) {
    static c64_t c64;
    static c1541 drive;
    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    drive.cpu.cpu.pc = 0xC000u;
    drive.cpu.cpu.V = 0;
    drive.via2.acr = 0x80u;       /* T1 PB7 output enable */
    drive.via2.t1_running = 1;
    drive.via2.t1_counter = 0;    /* underflows on next VIA step */
    drive.via2.t1_latch = 0x0010u;
    drive.via2_t1_pb7_last = drive.via2.t1_pb7_state;

    c1541_advance_one_cycle(&drive);

    if (!drive.cpu.cpu.V)
        fail("VIA2 T1 PB7 toggle should set CPU overflow flag");

    printf("PASS: test_via2_timer_pb7_sets_cpu_overflow\n");
}

/* Drive 8 and drive 9 are separate open-collector pullers.  Stepping an idle
   drive 9 must not clear a line that drive 8 is still pulling low. */
static void test_iec_two_drive_pull_aggregation(void) {
    static c64_t c64;
    static c1541 d8, d9;
    c64_init(&c64);
    c1541_init(&d8, &c64, 8);
    c1541_init(&d9, &c64, 9);
    load_nop_rom(&d8);
    load_nop_rom(&d9);
    c1541_reset(&d8);
    c1541_reset(&d9);

    d8.via1.ddrb = 0x02u; /* bit 1 = DATA output */
    d8.via1.orb  = 0x02u; /* drive 8 pulls DATA low */
    d9.via1.ddrb = 0x02u; /* bit 1 = DATA output */
    d9.via1.orb  = 0x00u; /* drive 9 releases DATA */

    c1541_advance_one_cycle(&d8);
    c1541_advance_one_cycle(&d9);

    if (!(c64.iec_external_pull & C64_IEC_DATA))
        fail("iec_two_drive_pull_aggregation: drive 9 cleared drive 8 DATA pull");

    d8.via1.orb = 0x00u; /* now both drives release DATA */
    c1541_advance_one_cycle(&d8);
    c1541_advance_one_cycle(&d9);

    if (c64.iec_external_pull & C64_IEC_DATA)
        fail("iec_two_drive_pull_aggregation: DATA should release after both drives release");

    printf("PASS: test_iec_two_drive_pull_aggregation\n");
}

/* c64_get_iec_c64_pull reflects CIA2 Port A state (CIA2 bit 5 → C64_IEC_DATA).
   CIA_REG_PORT_A = 0x00, CIA_REG_DDRA = 0x02. */
static void test_iec_c64_pull_data(void) {
    static c64_t c64;
    uint8_t pull;

    c64_init(&c64);

    /* CIA2 IEC outputs feed open-collector inverters:
       DDRA bit 5 = output (0x20), ORA bit 5 = 1 → pull DATA. */
    c64.cia2.registers[0x00] = 0x20u; /* ORA bit 5 = 1 */
    c64.cia2.registers[0x02] = 0x20u; /* DDRA bit 5 = output */

    pull = c64_get_iec_c64_pull(&c64);
    if (!(pull & C64_IEC_DATA))
        fail("iec_c64_pull_data: CIA2 should pull DATA low");

    printf("PASS: test_iec_c64_pull_data\n");
}

/* ------------------------------------------------------------------ */
/* Phase 3C: D64 sector read intercept                                 */
/* ------------------------------------------------------------------ */

/* Returns a freshly allocated 174848-byte D64 image with track 1 sector 0
   (byte offset 0) filled with `pattern`.  Caller owns the memory. */
static uint8_t *make_test_d64(uint8_t pattern) {
    uint8_t *img = (uint8_t *)calloc(1, C64_DRIVE_D64_STANDARD_SIZE);
    if (img == NULL)
        fail("make_test_d64: out of memory");
    /* Track 1, sector 0 is at D64 byte offset 0. */
    memset(img, pattern, 256);
    return img;
}

static void test_sector_read_success(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img;
    c64_drive_status_result result;
    int i;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    img = make_test_d64(0x5A);
    result = c64_mount_d64(
        &c64, 8, img, C64_DRIVE_D64_STANDARD_SIZE,
        NULL, 0, "test", "TEST", "AA", "2A", 664);
    free(img); /* c64_mount_d64 makes an internal copy */
    if (result != C64_DRIVE_STATUS_OK)
        fail("test_sector_read_success: c64_mount_d64 failed");

    /* Job 0: track 1, sector 0 → D64 offset 0 → all 0x5A. */
    drive.ram[0x3F] = 0;  /* jobn */
    drive.ram[0x06] = 1;  /* hdrs[0] = track */
    drive.ram[0x07] = 0;  /* hdrs[1] = sector */
    drive.cpu.cpu.pc = 0xF4CAu; /* REED intercept address */

    c1541_advance_one_cycle(&drive);

    /* Buffer at $0300 should now contain the sector data (0x5A × 256). */
    for (i = 0; i < 256; i++) {
        if (drive.ram[0x0300 + i] != 0x5A) {
            fprintf(stderr,
                "FAIL: test_sector_read_success: ram[0x%04X] = 0x%02X, expected 0x5A\n",
                0x0300 + i, drive.ram[0x0300 + i]);
            exit(1);
        }
    }

    /* After success path + CPU NOP step, A was not set to JOB_ERROR. */
    if (drive.cpu.cpu.A == 0x02u)
        fail("test_sector_read_success: A = JOB_ERROR, expected success");

    printf("PASS: test_sector_read_success\n");
}

static void test_physical_read_job_success(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img;
    c64_drive_status_result result;
    int i;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    img = make_test_d64(0xA5);
    result = c64_mount_d64(
        &c64, 8, img, C64_DRIVE_D64_STANDARD_SIZE,
        NULL, 0, "test", "TEST", "AA", "2A", 664);
    free(img);
    if (result != C64_DRIVE_STATUS_OK)
        fail("test_physical_read_job_success: c64_mount_d64 failed");

    drive.ram[0x00] = 0x80u; /* read job in buffer 0 */
    drive.ram[0x3F] = 0;
    drive.ram[0x06] = 1;
    drive.ram[0x07] = 0;
    drive.cpu.cpu.pc = 0xF3B1u; /* physical read/header-search entry */

    c1541_advance_one_cycle(&drive);

    for (i = 0; i < 256; i++) {
        if (drive.ram[0x0300 + i] != 0xA5) {
            fail("test_physical_read_job_success: sector buffer mismatch");
        }
    }
    expect_eq_u8("physical read A", 0x01u, drive.cpu.cpu.A);

    printf("PASS: test_physical_read_job_success\n");
}

static void test_physical_search_job_success(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img;
    c64_drive_status_result result;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    img = make_test_d64(0x00);
    result = c64_mount_d64(
        &c64, 8, img, C64_DRIVE_D64_STANDARD_SIZE,
        NULL, 0, "test", "TEST", "AA", "2A", 664);
    free(img);
    if (result != C64_DRIVE_STATUS_OK)
        fail("test_physical_search_job_success: c64_mount_d64 failed");

    drive.ram[0x04] = 0xB0u; /* search/header job in buffer 4 */
    drive.ram[0x3F] = 4;
    drive.ram[0x0E] = 1;
    drive.ram[0x0F] = 0;
    drive.cpu.cpu.pc = 0xF3B1u;

    c1541_advance_one_cycle(&drive);

    expect_eq_u8("physical search A", 0x01u, drive.cpu.cpu.A);
    expect_eq_u8("physical search track cache", 1u, drive.ram[0x12]);
    expect_eq_u8("physical search sector cache", 0u, drive.ram[0x13]);

    printf("PASS: test_physical_search_job_success\n");
}

static void test_queued_read_job_success(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img;
    c64_drive_status_result result;
    int i;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    img = make_test_d64(0xC3);
    result = c64_mount_d64(
        &c64, 8, img, C64_DRIVE_D64_STANDARD_SIZE,
        NULL, 0, "test", "TEST", "AA", "2A", 664);
    free(img);
    if (result != C64_DRIVE_STATUS_OK)
        fail("test_queued_read_job_success: c64_mount_d64 failed");

    drive.ram[0x02] = 0x80u; /* read job in buffer 2 */
    drive.ram[0x0A] = 1;
    drive.ram[0x0B] = 0;
    drive.cpu.cpu.pc = 0xF2BEu; /* controller job loop, before physical GCR path */

    c1541_advance_one_cycle(&drive);

    expect_eq_u8("queued read job result", 0x01u, drive.ram[0x02]);
    for (i = 0; i < 256; i++) {
        if (drive.ram[0x0500 + i] != 0xC3) {
            fail("test_queued_read_job_success: sector buffer mismatch");
        }
    }

    printf("PASS: test_queued_read_job_success\n");
}

static void test_queued_search_job_success(void) {
    static c64_t c64;
    static c1541 drive;
    uint8_t *img;
    c64_drive_status_result result;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    img = make_test_d64(0x00);
    result = c64_mount_d64(
        &c64, 8, img, C64_DRIVE_D64_STANDARD_SIZE,
        NULL, 0, "test", "TEST", "AA", "2A", 664);
    free(img);
    if (result != C64_DRIVE_STATUS_OK)
        fail("test_queued_search_job_success: c64_mount_d64 failed");

    drive.ram[0x04] = 0xB0u; /* search/header job in buffer 4 */
    drive.ram[0x0E] = 1;
    drive.ram[0x0F] = 0;
    drive.cpu.cpu.pc = 0xF2BEu; /* controller job loop, before physical GCR path */

    c1541_advance_one_cycle(&drive);

    expect_eq_u8("queued search job result", 0x01u, drive.ram[0x04]);
    expect_eq_u8("queued search track cache", 1u, drive.ram[0x12]);
    expect_eq_u8("queued search sector cache", 0u, drive.ram[0x13]);

    printf("PASS: test_queued_search_job_success\n");
}

static void test_sector_read_no_disk(void) {
    static c64_t c64;
    static c1541 drive;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    /* No disk mounted. */
    drive.ram[0x3F] = 0;
    drive.ram[0x06] = 1;
    drive.ram[0x07] = 0;
    drive.cpu.cpu.pc = 0xF4CAu;
    drive.cpu.cpu.A  = 0x00u;

    c1541_advance_one_cycle(&drive);

    /* satisfy_sector_read set A = JOB_ERROR (0x02) and jumped to ERRR ($F969).
       CPU then executed NOP at $F969 → A unchanged, still 0x02. */
    expect_eq_u8("no_disk A", 0x02u, drive.cpu.cpu.A);

    printf("PASS: test_sector_read_no_disk\n");
}

/* jobn = 5 is the command/error channel and is never a READ job.
   satisfy_sector_read must return early without touching A or PC. */
static void test_sector_read_jobn_out_of_range(void) {
    static c64_t c64;
    static c1541 drive;

    c64_init(&c64);
    c1541_init(&drive, &c64, 8);
    load_nop_rom(&drive);
    c1541_reset(&drive);

    drive.ram[0x3F]  = 5;       /* jobn = 5 → ignored by satisfy_sector_read */
    drive.cpu.cpu.pc = 0xF4CAu;
    drive.cpu.cpu.A  = 0x00u;

    c1541_advance_one_cycle(&drive);

    /* satisfy_sector_read returned early; CPU executed NOP at $F4CA → PC = $F4CB. */
    if (drive.cpu.cpu.A == 0x02u)
        fail("test_sector_read_jobn_out_of_range: A should not be JOB_ERROR");
    expect_eq_u16("jobn5 PC", 0xF4CBu, drive.cpu.cpu.pc);

    printf("PASS: test_sector_read_jobn_out_of_range\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    /* Phase 2 */
    test_ram_read_write();
    test_rom_not_loaded_nop();
    test_rom_loaded_flag();
    test_device_number();
    test_destroy_zeroes();

    /* Phase 3 */
    test_iec_drive_pulls_data();
    test_iec_drive_releases_data();
    test_cpu_instruction_cycles_are_throttled();
    test_via1_ca1_routes_to_irq();
    test_via2_timer_pb7_sets_cpu_overflow();
    test_iec_two_drive_pull_aggregation();
    test_iec_c64_pull_data();
    test_sector_read_success();
    test_physical_read_job_success();
    test_physical_search_job_success();
    test_queued_read_job_success();
    test_queued_search_job_success();
    test_sector_read_no_disk();
    test_sector_read_jobn_out_of_range();

    printf("All c1541 tests passed.\n");
    return 0;
}
