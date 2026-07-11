#include "c1541_gcr.h"

#include <string.h>

/* 4-bit nybble → 5-bit GCR code (Commodore 1541). */
static const uint8_t gcr_encode_tab[16] = {
    0x0Au, 0x0Bu, 0x12u, 0x13u, 0x0Eu, 0x0Fu, 0x16u, 0x17u,
    0x09u, 0x19u, 0x1Au, 0x1Bu, 0x0Du, 0x1Du, 0x1Eu, 0x15u
};

/* 5-bit code → nybble; 0xFF = invalid. */
static const uint8_t gcr_decode_tab[32] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0x08u, 0x00u, 0x01u, 0xFFu, 0x0Cu, 0x04u, 0x05u,
    0xFFu, 0xFFu, 0x02u, 0x03u, 0xFFu, 0x0Fu, 0x06u, 0x07u,
    0xFFu, 0x09u, 0x0Au, 0x0Bu, 0xFFu, 0x0Du, 0x0Eu, 0xFFu
};

size_t c1541_gcr_encode(const uint8_t *src, size_t src_len, uint8_t *dst) {
    size_t si;
    size_t di = 0;
    uint32_t shift = 0;
    int bits = 0;

    if (src == NULL || dst == NULL || (src_len & 3u) != 0u) {
        return 0;
    }

    for (si = 0; si < src_len; ++si) {
        uint8_t b = src[si];
        uint8_t hi = gcr_encode_tab[b >> 4];
        uint8_t lo = gcr_encode_tab[b & 0x0Fu];

        shift = (shift << 5) | hi;
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            dst[di++] = (uint8_t)((shift >> bits) & 0xFFu);
        }

        shift = (shift << 5) | lo;
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            dst[di++] = (uint8_t)((shift >> bits) & 0xFFu);
        }
    }

    return di;
}

size_t c1541_gcr_decode(const uint8_t *src, size_t gcr_len, uint8_t *dst) {
    size_t si;
    size_t di = 0;
    uint32_t shift = 0;
    int bits = 0;
    int nybbles = 0;
    uint8_t raw = 0;

    if (src == NULL || dst == NULL || (gcr_len % 5u) != 0u) {
        return 0;
    }

    for (si = 0; si < gcr_len; ++si) {
        shift = (shift << 8) | src[si];
        bits += 8;
        while (bits >= 5) {
            uint8_t code;
            uint8_t nib;

            bits -= 5;
            code = (uint8_t)((shift >> bits) & 0x1Fu);
            nib = gcr_decode_tab[code];
            if (nib == 0xFFu) {
                return 0;
            }
            if ((nybbles & 1) == 0) {
                raw = (uint8_t)(nib << 4);
            } else {
                raw = (uint8_t)(raw | nib);
                dst[di++] = raw;
            }
            nybbles++;
        }
    }

    return di;
}

void c1541_gcr_make_header_raw(
    uint8_t track,
    uint8_t sector,
    uint8_t id_lo,
    uint8_t id_hi,
    uint8_t out_raw[C1541_GCR_HEADER_RAW]) {
    out_raw[0] = 0x08u;
    out_raw[1] = (uint8_t)(sector ^ track ^ id_lo ^ id_hi);
    out_raw[2] = sector;
    out_raw[3] = track;
    out_raw[4] = id_hi;
    out_raw[5] = id_lo;
    out_raw[6] = 0x0Fu;
    out_raw[7] = 0x0Fu;
}

void c1541_gcr_make_data_raw(
    const uint8_t sector[256],
    uint8_t out_raw[C1541_GCR_DATA_RAW]) {
    uint8_t cs = 0;
    int i;

    out_raw[0] = 0x07u;
    for (i = 0; i < 256; ++i) {
        out_raw[1 + i] = sector[i];
        cs ^= sector[i];
    }
    out_raw[257] = cs;
    out_raw[258] = 0x00u;
    out_raw[259] = 0x00u;
}

int c1541_gcr_data_raw_to_sector(
    const uint8_t raw[C1541_GCR_DATA_RAW],
    uint8_t sector[256]) {
    uint8_t cs = 0;
    int i;

    if (raw[0] != 0x07u) {
        return 0;
    }
    for (i = 0; i < 256; ++i) {
        sector[i] = raw[1 + i];
        cs ^= sector[i];
    }
    if (cs != raw[257]) {
        return 0;
    }
    return 1;
}

int c1541_gcr_sectors_per_track(uint8_t track) {
    if (track < 1 || track > 35) {
        return 0;
    }
    if (track <= 17) {
        return 21;
    }
    if (track <= 24) {
        return 19;
    }
    if (track <= 30) {
        return 18;
    }
    return 17;
}

int c1541_gcr_density_for_track(uint8_t track) {
    if (track <= 17) {
        return 3;
    }
    if (track <= 24) {
        return 2;
    }
    if (track <= 30) {
        return 1;
    }
    return 0;
}

int c1541_gcr_cycles_per_byte(int density) {
    static const int table[4] = {32, 30, 28, 26};
    if (density < 0 || density > 3) {
        density = 0;
    }
    return table[density];
}

int c1541_gcr_d64_sector_offset(uint8_t track, uint8_t sector) {
    static const uint8_t spt[36] = {
        0,
        21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
        19, 19, 19, 19, 19, 19, 19,
        18, 18, 18, 18, 18, 18,
        17, 17, 17, 17, 17
    };
    int offset = 0;
    int t;

    if (track < 1 || track > 35) {
        return -1;
    }
    if (sector >= spt[track]) {
        return -1;
    }
    for (t = 1; t < (int)track; ++t) {
        offset += (int)spt[t] * 256;
    }
    offset += (int)sector * 256;
    return offset;
}
