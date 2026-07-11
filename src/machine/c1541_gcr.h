#ifndef C1541_GCR_H
#define C1541_GCR_H

#include <stddef.h>
#include <stdint.h>

/* Commodore 1541 4-to-5 GCR encode/decode and standard sector framing helpers.
   Pure functions — no drive state. */

enum {
    C1541_GCR_SYNC_BYTES = 5,       /* 0xFF x N before header/data */
    C1541_GCR_HEADER_RAW = 8,       /* raw header before GCR */
    C1541_GCR_HEADER_ENC = 10,      /* 8 * 5 / 4 */
    C1541_GCR_DATA_RAW = 260,       /* $07 + 256 data + checksum + 2 zero */
    C1541_GCR_DATA_ENC = 325,       /* 260 * 5 / 4 */
    C1541_GCR_HEADER_GAP = 9,       /* 0x55 after header */
    C1541_GCR_MAX_TRACK_BYTES = 8192
};

/* Encode src_len raw bytes (must be multiple of 4) to GCR.
   Writes src_len * 5 / 4 bytes to dst. Returns encoded length, or 0 on error. */
size_t c1541_gcr_encode(const uint8_t *src, size_t src_len, uint8_t *dst);

/* Decode gcr_len GCR bytes (must be multiple of 5) to raw.
   Writes gcr_len * 4 / 5 bytes to dst. Returns decoded length, or 0 on error. */
size_t c1541_gcr_decode(const uint8_t *src, size_t gcr_len, uint8_t *dst);

/* Build 8-byte raw header for (track, sector) with disk id lo/hi. */
void c1541_gcr_make_header_raw(
    uint8_t track,
    uint8_t sector,
    uint8_t id_lo,
    uint8_t id_hi,
    uint8_t out_raw[C1541_GCR_HEADER_RAW]);

/* Build 260-byte raw data block from 256 sector bytes. */
void c1541_gcr_make_data_raw(
    const uint8_t sector[256],
    uint8_t out_raw[C1541_GCR_DATA_RAW]);

/* Decode a raw data block back to 256 sector bytes. Returns 0 on bad id/size. */
int c1541_gcr_data_raw_to_sector(
    const uint8_t raw[C1541_GCR_DATA_RAW],
    uint8_t sector[256]);

/* Sectors-per-track for standard 35-track DOS layout. Returns 0 if invalid. */
int c1541_gcr_sectors_per_track(uint8_t track);

/* Density zone 0..3 for a standard track (3 = outermost / fastest). */
int c1541_gcr_density_for_track(uint8_t track);

/* GCR byte-clock period in drive CPU cycles for density zone 0..3. */
int c1541_gcr_cycles_per_byte(int density);

/* D64 byte offset of (track, sector). Track 1-based. Returns -1 if invalid. */
int c1541_gcr_d64_sector_offset(uint8_t track, uint8_t sector);

#endif /* C1541_GCR_H */
