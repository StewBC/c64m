#include "c1541.h"
#include "c64.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* ROM intercept constants                                             */
/* Standard 1541 ROM (DOS 2.6: 325302-01 + 901229-06AA combined).    */
/* Addresses verified against: github.com/mist64/1541rom (dos1541    */
/* source, 325302-01 / 901229-06 variant, labels from dos.lbl).      */
/*                                                                     */
/* Intercept strategy: job-queue level at the physical read path.      */
/* The ROM issues READ jobs via the job queue (ZP $00–$05), writes    */
/* track/sector into hdrs[] (ZP $06–$11), and then enters the GCR     */
/* physical read/search code.  Since the emulator mounts D64 images   */
/* rather than modelling a rotating disk, we satisfy supported jobs    */
/* before the ROM waits for sync bytes from the disk controller.       */
/* ------------------------------------------------------------------ */
#define C1541_ZP_JOBS        0x0000u  /* jobs[0..5]: job queue (6 bytes) */
#define C1541_ZP_HDRS        0x0006u  /* hdrs[0..11]: track/sector pairs */
#define C1541_ZP_JOBN        0x003Fu  /* jobn: index of the active job   */
#define C1541_RAM_SECTOR_BUF 0x0300u  /* buff0; buff_N = $0300 + N*$0100 */

#define C1541_ROM_PHYS_READ  0xF3B1u  /* physical header/search entry */
#define C1541_ROM_REED       0xF4CAu  /* reed: physical sector read entry */
#define C1541_ROM_READ40     0xF505u  /* read40: success return of reed   */
#define C1541_ROM_ERRR       0xF969u  /* errr: job completion (ldy jobn / sta jobs,y) */

#define C1541_JOB_CMD_MASK   0x78u
#define C1541_JOB_CMD_READ   0x00u
#define C1541_JOB_CMD_WRITE  0x10u    /* WRITE job ($90 & MASK); dos.lbl "WRITE" */
#define C1541_JOB_CMD_VERIFY 0x20u
#define C1541_JOB_CMD_SEARCH 0x30u
#define C1541_JOB_CMD_EXECUTE 0x60u   /* EXECUTE job ($E0 & MASK); used by FORMT */

#define C1541_JOB_OK         0x01u    /* job result: success */
#define C1541_JOB_ERROR      0x02u    /* job result: generic error */
#define C1541_JOB_WRITE_PROT 0x08u    /* job result: write protect on (DOS 26) */

/* Maximum valid job index (jobs 0–4 have dedicated buffers at $0300–$0700).
   Job 5 shares page $07 with job 4 and is never a READ job (command channel). */
#define C1541_JOB_MAX        5u

/* ------------------------------------------------------------------ */
/* D64 sector offset (inline table — avoids tools/d64/ dependency)    */
/* ------------------------------------------------------------------ */

/* Returns byte offset of (track, sector) in a standard 35-track D64 image.
   Track is 1-based; sector is 0-based.  Returns -1 for out-of-range inputs. */
static int d64_sector_offset(uint8_t track, uint8_t sector) {
    static const uint8_t spt[36] = {
        0,
        21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
        19,19,19,19,19,19,19,
        18,18,18,18,18,18,
        17,17,17,17,17
    };
    int offset;
    int t;

    if (track < 1 || track > 35) return -1;
    if (sector >= spt[track]) return -1;
    offset = 0;
    for (t = 1; t < (int)track; t++) offset += (int)spt[t] * 256;
    offset += (int)sector * 256;
    return offset;
}

/* Number of sectors on a standard 35-track D64 track, or -1 if out of range. */
static int d64_sectors_per_track(uint8_t track) {
    if (track < 1 || track > 35) return -1;
    if (track <= 17) return 21;
    if (track <= 24) return 19;
    if (track <= 30) return 18;
    return 17;
}

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
    if (addr >= 0x1C00u && addr < 0x2000u) {
        via6522_write(&drive->via2, (uint8_t)(addr & 0x0Fu), value);
        return;
    }
    if (addr >= 0x1800u && addr < 0x2000u) {
        via6522_write(&drive->via1, (uint8_t)(addr & 0x0Fu), value);
        return;
    }
    /* ROM and unmapped: ignore */
}

/* ------------------------------------------------------------------ */
/* IRQ / NMI callbacks                                                 */
/* ------------------------------------------------------------------ */

static uint8_t c1541_irq_pending(void *user) {
    c1541 *drive = (c1541 *)user;
    return (uint8_t)(via6522_irq_pending(&drive->via1) || via6522_irq_pending(&drive->via2));
}

/* ------------------------------------------------------------------ */
/* IEC bus synchronisation                                             */
/* ------------------------------------------------------------------ */

/* Called once per cycle after VIA steps and before CPU step.
   Reads VIA #1 Port B output lines, updates the C64's iec_external_pull,
   then reads the combined bus state and feeds it back into VIA #1
   input pins and CA1 (ATN IRQ source). */
static void c1541_update_iec_bus(c1541 *drive) {
    uint8_t ddrb1 = drive->via1.ddrb;
    uint8_t orb1  = drive->via1.orb;
    uint8_t drive_pull = 0;
    uint8_t c64_pull, bus_low;
    uint8_t data_low, clk_low, atn_low;
    uint8_t port_b_in;

    /* Standard 1541 serial VIA ($1800) Port B:
       bit 0 = DATA in, bit 1 = DATA out, bit 2 = CLK in, bit 3 = CLK out,
       bit 4 = ATN acknowledge control, bits 5/6 = address jumpers, bit 7 = ATN in.
       The serial VIA is behind inverters:
       output bit high pulls the IEC line low, and input bit high means the
       IEC line is currently low.  PB4 is special: when ATN is asserted and
       PB4 is output-low, the drive acknowledges attention by pulling DATA low. */
    c64_pull = c64_get_iec_c64_pull(drive->c64);
    atn_low = (c64_pull & C64_IEC_ATN) != 0;

    if ((ddrb1 & 0x02u) &&  (orb1 & 0x02u)) drive_pull |= C64_IEC_DATA;
    if ((ddrb1 & 0x10u) && !(orb1 & 0x10u) && atn_low) drive_pull |= C64_IEC_DATA;
    if ((ddrb1 & 0x08u) &&  (orb1 & 0x08u)) drive_pull |= C64_IEC_CLK;

    /* Tell the C64 what this 1541 is pulling.  The C64 aggregates drive 8,
       drive 9, and any other external pullers using open-collector OR logic. */
    c64_set_iec_drive_pull(drive->c64, drive->device_number, drive_pull);

    /* Open-collector: either side can pull a line low. */
    bus_low  = c64_get_iec_external_pull(drive->c64) | c64_pull;

    data_low = (bus_low & C64_IEC_DATA) != 0;
    clk_low  = (bus_low & C64_IEC_CLK)  != 0;
    atn_low  = (bus_low & C64_IEC_ATN)  != 0;

    /* Feed combined bus state into VIA #1 Port B input pins.
       Bus line low (asserted) → input bit = 1 due the 1541 input inverters. */
    port_b_in = drive->via1.port_b_in;
    if (data_low) port_b_in |= 0x01u; else port_b_in &= (uint8_t)~0x01u;
    if (clk_low)  port_b_in |= 0x04u; else port_b_in &= (uint8_t)~0x04u;
    if (atn_low)  port_b_in |= 0x80u; else port_b_in &= (uint8_t)~0x80u;
    via6522_set_port_b_inputs(&drive->via1, port_b_in);

    /* ATN → CA1 through the same input polarity: asserted ATN presents high. */
    via6522_set_ca1(&drive->via1, atn_low ? 1u : 0u);
}

/* ------------------------------------------------------------------ */
/* D64 physical job intercept                                          */
/* ------------------------------------------------------------------ */

/* Completes the active ROM job with the given result code. */
static void c1541_complete_job(c1541 *drive, uint8_t result) {
    drive->cpu.cpu.A  = result;
    drive->cpu.cpu.pc = C1541_ROM_ERRR;
}

static int c1541_job_sector_offset(c1541 *drive, uint8_t n, int *out_offset) {
    uint8_t track, sector;
    const c64_drive_slot *slot;
    int offset;

    /* Track and sector from hdrs[n*2] and hdrs[n*2+1] (ZP $06 + n*2). */
    track  = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u];
    sector = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u + 1u];

    /* Get the mounted D64 image for this device. */
    slot = c64_get_drive_slot(drive->c64, drive->device_number);
    if (!slot || !slot->mounted || !slot->image_bytes || slot->image_size == 0) {
        return 0;
    }

    /* Compute byte offset in the D64 image. */
    offset = d64_sector_offset(track, sector);
    if (offset < 0 || (size_t)(offset + 256) > slot->image_size) {
        return 0;
    }
    *out_offset = offset;
    return 1;
}

/* Fills the active job's buffer from the mounted D64 image. */
static int c1541_copy_sector_to_job_buffer(c1541 *drive, uint8_t n) {
    const c64_drive_slot *slot;
    int offset;
    uint16_t buf_addr;

    if (!c1541_job_sector_offset(drive, n, &offset)) {
        return 0;
    }
    slot = c64_get_drive_slot(drive->c64, drive->device_number);

    /* Copy sector data into the 1541's buffer for job n.
       Buffer for job N is at $0300 + N*$0100 (pages 3–7, all within 2 KB RAM). */
    buf_addr = (uint16_t)(C1541_RAM_SECTOR_BUF + (uint16_t)n * 0x0100u);
    memcpy(&drive->ram[buf_addr], slot->image_bytes + offset, 256);
    return 1;
}

/* Writes the active job's buffer back into the mounted D64 image.
   DOS fills the buffer before queuing the WRITE job, so at the job-dispatch
   window the buffer already holds the 256 bytes to persist.
   Returns C1541_JOB_OK on success, C1541_JOB_WRITE_PROT if the image is mounted
   read-only, or C1541_JOB_ERROR if the sector is out of range / not mounted.
   Marks the slot dirty on success; the runtime flushes it to the host .d64. */
static uint8_t c1541_copy_job_buffer_to_sector(c1541 *drive, uint8_t n) {
    c64_drive_slot *slot;
    int offset;
    uint16_t buf_addr;

    if (!c1541_job_sector_offset(drive, n, &offset)) {
        return C1541_JOB_ERROR;
    }
    slot = c64_get_drive_slot_mut(drive->c64, drive->device_number);
    if (!slot) {
        return C1541_JOB_ERROR;
    }
    if (!slot->writable) {
        return C1541_JOB_WRITE_PROT;
    }

    buf_addr = (uint16_t)(C1541_RAM_SECTOR_BUF + (uint16_t)n * 0x0100u);
    memcpy(slot->image_bytes + offset, &drive->ram[buf_addr], 256);
    slot->dirty = true;
    return C1541_JOB_OK;
}

/* Handles the FORMT EXECUTE job for job n by erasing the target track's sectors
   in the mounted image.  The DOS "NEW" command formats every track this way and
   then writes a fresh BAM + directory through ordinary WRITE jobs, so erasing the
   sector data here is sufficient to produce a clean formatted D64.
   Returns C1541_JOB_OK, C1541_JOB_WRITE_PROT (read-only), or C1541_JOB_ERROR. */
static uint8_t c1541_format_track(c1541 *drive, uint8_t n) {
    c64_drive_slot *slot;
    uint8_t track;
    int off0, sectors;

    track = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u];
    sectors = d64_sectors_per_track(track);
    off0 = d64_sector_offset(track, 0);
    if (sectors <= 0 || off0 < 0) {
        return C1541_JOB_ERROR;
    }
    slot = c64_get_drive_slot_mut(drive->c64, drive->device_number);
    if (!slot || !slot->mounted || !slot->image_bytes) {
        return C1541_JOB_ERROR;
    }
    if (!slot->writable) {
        return C1541_JOB_WRITE_PROT;
    }
    if ((size_t)(off0 + sectors * 256) > slot->image_size) {
        return C1541_JOB_ERROR;
    }

    memset(slot->image_bytes + off0, 0, (size_t)sectors * 256);
    slot->dirty = true;
    return C1541_JOB_OK;
}

static void c1541_complete_queued_job(c1541 *drive, uint8_t n, uint8_t result) {
    drive->ram[C1541_ZP_JOBS + n] = result;
}

static int c1541_satisfy_queued_job(c1541 *drive, uint8_t n) {
    uint8_t raw, command, track, sector;
    int offset;

    raw = drive->ram[C1541_ZP_JOBS + n];
    if ((raw & 0x80u) == 0 || raw == 0xD0u) {
        return 0;
    }

    command = (uint8_t)(raw & C1541_JOB_CMD_MASK);
    switch (command) {
        case C1541_JOB_CMD_READ:
            c1541_complete_queued_job(
                drive,
                n,
                c1541_copy_sector_to_job_buffer(drive, n) ? C1541_JOB_OK : C1541_JOB_ERROR);
            return 1;

        case C1541_JOB_CMD_WRITE:
            c1541_complete_queued_job(drive, n, c1541_copy_job_buffer_to_sector(drive, n));
            return 1;

        case C1541_JOB_CMD_VERIFY:
            c1541_complete_queued_job(
                drive,
                n,
                c1541_job_sector_offset(drive, n, &offset) ? C1541_JOB_OK : C1541_JOB_ERROR);
            return 1;

        case C1541_JOB_CMD_SEARCH:
            if (!c1541_job_sector_offset(drive, n, &offset)) {
                c1541_complete_queued_job(drive, n, C1541_JOB_ERROR);
            } else {
                track  = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u];
                sector = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u + 1u];
                drive->ram[0x0012u] = track;
                drive->ram[0x0013u] = sector;
                c1541_complete_queued_job(drive, n, C1541_JOB_OK);
            }
            return 1;

        case C1541_JOB_CMD_EXECUTE:
            /* The DOS "NEW" command formats each track via an EXECUTE job that
               runs the ROM's GCR FORMT routine against the (unmodelled) disk
               mechanics.  We cannot GCR-format a D64 (it stores decoded sectors,
               not tracks), so complete the job successfully and let the ROM's own
               DOS code write the fresh BAM + directory through normal WRITE jobs.
               The disk is marked dirty so the formatted result is persisted. */
            c1541_complete_queued_job(drive, n, c1541_format_track(drive, n));
            return 1;

        default:
            return 0;
    }
}

static void c1541_satisfy_queued_jobs(c1541 *drive) {
    uint8_t n;

    for (n = 0; n < C1541_JOB_MAX; ++n) {
        (void)c1541_satisfy_queued_job(drive, n);
    }
}

/* Called when the 1541 ROM is about to wait for real disk-controller data.
   READ jobs need sector data copied into the job buffer.  SEARCH and VERIFY
   jobs only need the same completion status the ROM would set after finding a
   matching header on the disk. */
static int c1541_satisfy_physical_job(c1541 *drive) {
    uint8_t n, command, track, sector;

    n = drive->ram[C1541_ZP_JOBN];
    if (n >= C1541_JOB_MAX) {
        return 0;
    }

    command = (uint8_t)(drive->ram[C1541_ZP_JOBS + n] & C1541_JOB_CMD_MASK);
    switch (command) {
        case C1541_JOB_CMD_READ:
            if (!c1541_copy_sector_to_job_buffer(drive, n)) {
                c1541_complete_job(drive, C1541_JOB_ERROR);
            } else {
                c1541_complete_job(drive, C1541_JOB_OK);
            }
            return 1;

        case C1541_JOB_CMD_WRITE:
            c1541_complete_job(drive, c1541_copy_job_buffer_to_sector(drive, n));
            return 1;

        case C1541_JOB_CMD_VERIFY:
            c1541_complete_job(drive, C1541_JOB_OK);
            return 1;

        case C1541_JOB_CMD_SEARCH:
            track  = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u];
            sector = drive->ram[C1541_ZP_HDRS + (uint16_t)n * 2u + 1u];
            drive->ram[0x0012u] = track;
            drive->ram[0x0013u] = sector;
            c1541_complete_job(drive, C1541_JOB_OK);
            return 1;

        default:
            return 0;
    }
}

/* Called when the 1541 CPU's PC == C1541_ROM_REED.  This is kept as a fallback
   for ROM paths that reach reed directly. */
static void c1541_satisfy_sector_read(c1541 *drive) {
    uint8_t n;

    n = drive->ram[C1541_ZP_JOBN];
    if (n >= C1541_JOB_MAX) {
        /* Job 5 is the command/error channel — never a READ.  Ignore. */
        return;
    }

    if (!c1541_copy_sector_to_job_buffer(drive, n)) {
        c1541_complete_job(drive, C1541_JOB_ERROR);
        return;
    }

    /* Jump to read40: ROM loads A = #1 (success), falls into errr, marks job done. */
    drive->cpu.cpu.pc = C1541_ROM_READ40;
}

static void c1541_update_so(c1541 *drive) {
    if (drive->via2.t1_pb7_state != drive->via2_t1_pb7_last) {
        drive->via2_t1_pb7_last = drive->via2.t1_pb7_state;
        c6510_set_overflow(&drive->cpu);
    }
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
}

void c1541_destroy(c1541 *drive) {
    memset(drive, 0, sizeof(c1541));
}

void c1541_reset(c1541 *drive) {
    memset(drive->ram, 0, C1541_RAM_SIZE);
    via6522_reset(&drive->via1);
    via6522_reset(&drive->via2);
    /* Device address jumpers are serial VIA ($1800) Port B bits 5/6.
       The DOS ROM converts 00→device 8, 20→device 9, 40→device 10, 60→device 11. */
    if (drive->device_number == 9) {
        drive->via1.port_b_in |= 0x20u;
    }
    /* c6510_reset() reads $FFFC/$FFFD through the bus callbacks.
       With ROM loaded at $C000–$FFFF this correctly fetches the 1541 reset vector. */
    c6510_reset(&drive->cpu);
    drive->cpu_cycles_remaining = 0;
    drive->via2_t1_pb7_last = drive->via2.t1_pb7_state;
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
    size_t cycles;

    if (!drive->rom_loaded) return;

    /* 1. Step VIAs (may set IFR flags before CPU samples them). */
    via6522_step(&drive->via1);
    via6522_step(&drive->via2);
    c1541_update_so(drive);

    /* 2. Synchronise IEC bus: serial VIA outputs → C64 pull; C64 state → serial VIA inputs + CA1. */
    c1541_update_iec_bus(drive);

    if (drive->cpu_cycles_remaining == 0) {
        if (drive->cpu.cpu.pc >= 0xF2B0u && drive->cpu.cpu.pc <= 0xF2F6u) {
            c1541_satisfy_queued_jobs(drive);
        }

        /* 3. Intercept disk jobs before the ROM waits for unmodelled GCR hardware. */
        if (drive->cpu.cpu.pc == C1541_ROM_PHYS_READ) {
            (void)c1541_satisfy_physical_job(drive);
        } else if (drive->cpu.cpu.pc == C1541_ROM_REED) {
            c1541_satisfy_sector_read(drive);
        }

        /* 4. Execute one 6502 instruction, then spread its elapsed cycles over
           subsequent c1541_advance_one_cycle() calls.  c6510_step() is an
           instruction step, not a one-cycle step. */
        cycles = c6510_step(&drive->cpu);
        drive->cpu_cycles_remaining = cycles != 0 ? cycles : 1;
    }

    drive->cpu_cycles_remaining--;
}
