#include "runtime.h"
#include "runtime_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void put_be16(FILE *file, uint16_t value) {
    fputc((int)(value >> 8), file);
    fputc((int)(value & 0xffu), file);
}

static void put_be32(FILE *file, uint32_t value) {
    fputc((int)(value >> 24), file);
    fputc((int)((value >> 16) & 0xffu), file);
    fputc((int)((value >> 8) & 0xffu), file);
    fputc((int)(value & 0xffu), file);
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_crt_64c.bin", "wb");
    FILE *character = fopen("runtime_crt_character.bin", "wb");
    size_t i;

    if (system == NULL || character == NULL) {
        fail("failed to create runtime CRT test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);

    for (i = 0; i < C64_CHAR_ROM_SIZE; i++) {
        fputc(0x00, character);
    }

    fclose(system);
    fclose(character);
}

static void write_generic_16k_crt(const char *path) {
    FILE *file = fopen(path, "wb");
    size_t i;

    if (file == NULL) {
        fail("failed to create runtime CRT test file");
    }

    fwrite("C64 CARTRIDGE   ", 1, 16, file);
    put_be32(file, 0x40);
    put_be16(file, 0x0100);
    put_be16(file, 0x0000);
    fputc(0x00, file); /* EXROM */
    fputc(0x00, file); /* GAME */
    for (i = 0; i < 6; ++i) {
        fputc(0x00, file);
    }
    fwrite("RUNTIME CRT TEST", 1, 16, file);
    for (i = 0; i < 16; ++i) {
        fputc(0x00, file);
    }

    fwrite("CHIP", 1, 4, file);
    put_be32(file, 0x4010);
    put_be16(file, 0x0000);
    put_be16(file, 0x0000);
    put_be16(file, 0x8000);
    put_be16(file, 0x4000);
    for (i = 0; i < 0x2000u; ++i) {
        fputc((int)(0x80u + (i & 0x0fu)), file);
    }
    for (i = 0; i < 0x2000u; ++i) {
        fputc((int)(0xa0u + (i & 0x0fu)), file);
    }

    fclose(file);
}

static void write_magic_desk_crt(const char *path) {
    FILE *file = fopen(path, "wb");
    size_t bank;
    size_t i;

    if (file == NULL) {
        fail("failed to create Magic Desk CRT test file");
    }

    fwrite("C64 CARTRIDGE   ", 1, 16, file);
    put_be32(file, 0x40);
    put_be16(file, 0x0100);
    put_be16(file, 19); /* Magic Desk */
    fputc(0x00, file); /* EXROM */
    fputc(0x01, file); /* GAME */
    for (i = 0; i < 6; ++i) {
        fputc(0x00, file);
    }
    fwrite("MAGIC DESK TEST ", 1, 16, file);
    for (i = 0; i < 16; ++i) {
        fputc(0x00, file);
    }

    for (bank = 0; bank < 4; ++bank) {
        fwrite("CHIP", 1, 4, file);
        put_be32(file, 0x2010);
        put_be16(file, 0x0000);
        put_be16(file, (uint16_t)bank);
        put_be16(file, 0x8000);
        put_be16(file, 0x2000);
        for (i = 0; i < 0x2000u; ++i) {
            fputc((int)(((bank + 1u) << 4) | (i & 0x0fu)), file);
        }
    }

    fclose(file);
}

/* Generic 8K-bank writer: each chip filled entirely with fill_bytes[c]. */
static void write_banked_8k_crt(
    const char *path,
    uint16_t hw_type,
    uint8_t exrom,
    uint8_t game,
    const uint16_t *bank_values,
    const uint8_t *fill_bytes,
    size_t count)
{
    FILE *file = fopen(path, "wb");
    size_t c;
    size_t i;

    if (file == NULL) {
        fail("failed to create banked 8K CRT test file");
    }

    fwrite("C64 CARTRIDGE   ", 1, 16, file);
    put_be32(file, 0x40);
    put_be16(file, 0x0100);
    put_be16(file, hw_type);
    fputc(exrom, file);
    fputc(game, file);
    for (i = 0; i < 6; ++i) {
        fputc(0x00, file);
    }
    fwrite("LONGTAIL CRT TST", 1, 16, file);
    for (i = 0; i < 16; ++i) {
        fputc(0x00, file);
    }

    for (c = 0; c < count; ++c) {
        fwrite("CHIP", 1, 4, file);
        put_be32(file, 0x2010);
        put_be16(file, 0x0000);
        put_be16(file, bank_values[c]);
        put_be16(file, 0x8000);
        put_be16(file, 0x2000);
        for (i = 0; i < 0x2000u; ++i) {
            fputc((int)fill_bytes[c], file);
        }
    }

    fclose(file);
}

/* Super Games writer: 16K chips, low 8K = roml_bytes[b], high 8K = romh_bytes[b]. */
static void write_super_games_crt(
    const char *path,
    const uint8_t *roml_bytes,
    const uint8_t *romh_bytes,
    size_t bank_count)
{
    FILE *file = fopen(path, "wb");
    size_t b;
    size_t i;

    if (file == NULL) {
        fail("failed to create Super Games CRT test file");
    }

    fwrite("C64 CARTRIDGE   ", 1, 16, file);
    put_be32(file, 0x40);
    put_be16(file, 0x0100);
    put_be16(file, 8); /* Super Games */
    fputc(0x00, file);
    fputc(0x00, file);
    for (i = 0; i < 6; ++i) {
        fputc(0x00, file);
    }
    fwrite("SUPER GAMES TEST", 1, 16, file);
    for (i = 0; i < 16; ++i) {
        fputc(0x00, file);
    }

    for (b = 0; b < bank_count; ++b) {
        fwrite("CHIP", 1, 4, file);
        put_be32(file, 0x4010);
        put_be16(file, 0x0000);
        put_be16(file, (uint16_t)b);
        put_be16(file, 0x8000);
        put_be16(file, 0x4000);
        for (i = 0; i < 0x2000u; ++i) {
            fputc((int)roml_bytes[b], file);
        }
        for (i = 0; i < 0x2000u; ++i) {
            fputc((int)romh_bytes[b], file);
        }
    }

    fclose(file);
}

static void write_ocean_crt(const char *path) {
    FILE *file = fopen(path, "wb");
    size_t bank;
    size_t i;

    if (file == NULL) {
        fail("failed to create Ocean CRT test file");
    }

    fwrite("C64 CARTRIDGE   ", 1, 16, file);
    put_be32(file, 0x40);
    put_be16(file, 0x0100);
    put_be16(file, 5); /* Ocean */
    fputc(0x00, file); /* EXROM */
    fputc(0x00, file); /* GAME=0 => 16K mirror in tests */
    for (i = 0; i < 6; ++i) {
        fputc(0x00, file);
    }
    fwrite("OCEAN CRT TEST  ", 1, 16, file);
    for (i = 0; i < 16; ++i) {
        fputc(0x00, file);
    }

    for (bank = 0; bank < 4; ++bank) {
        fwrite("CHIP", 1, 4, file);
        put_be32(file, 0x2010);
        put_be16(file, 0x0000);
        put_be16(file, (uint16_t)bank);
        put_be16(file, 0x8000);
        put_be16(file, 0x2000);
        for (i = 0; i < 0x2000u; ++i) {
            fputc((int)(((bank + 2u) << 4) | (i & 0x0fu)), file);
        }
    }

    fclose(file);
}

static void write_test_prg(const char *path) {
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        fail("failed to create runtime CRT test PRG");
    }

    /* Load address $0801 (little-endian) followed by two payload bytes. */
    fputc(0x01, file);
    fputc(0x08, file);
    fputc(0xaa, file);
    fputc(0xbb, file);
    fclose(file);
}

static int poll_event(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static void drain_runtime_events(runtime_client *client) {
    runtime_event event;
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.1) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
        }
    }
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void expect_crt_history_order(runtime_client *client) {
    runtime_history_query query;
    runtime_event event;
    runtime_history_rpc_meta meta;
    uint64_t token = runtime_client_alloc_request_token(client);
    uint8_t *payload = NULL;
    uint32_t length = 0u;
    uint32_t record_count;
    size_t offset = 24u;
    uint32_t previous_timeline = 0u;
    bool saw_crt = false;
    uint32_t i;

    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_FORWARD;
    expect_true(
        "request CRT history",
        runtime_client_history_find(
            client,
            &query,
            RUNTIME_HISTORY_FROM_OLDEST,
            0u,
            128u,
            token));
    if (!poll_event(
            client, &event, RUNTIME_EVENT_HISTORY_RESULT_RESPONSE) ||
        event.request_token != token ||
        event.data.history_rpc.status != RUNTIME_HISTORY_RPC_OK) {
        fail("CRT history completion not received");
    }
    expect_true(
        "claim CRT history",
        runtime_client_claim_history_rpc(
            client, token, &payload, &length, &meta));
    if (length < 24u || memcmp(payload, "HST1", 4u) != 0) {
        free(payload);
        fail("invalid CRT history payload");
    }
    record_count = read_le32(payload + 16u);
    for (i = 0u; i < record_count; ++i) {
        uint16_t record_size;
        uint8_t kind;
        uint32_t timeline;
        uint16_t marker_kind;
        uint32_t marker_arg0;
        if (offset + 48u > length) {
            free(payload);
            fail("truncated CRT history record");
        }
        record_size = read_le16(payload + offset);
        kind = payload[offset + 2u];
        timeline = read_le32(payload + offset + 4u);
        marker_kind = read_le16(payload + offset + 36u);
        marker_arg0 = read_le32(payload + offset + 40u);
        if (saw_crt) {
            if (kind != RUNTIME_HISTORY_RECORD_MARKER ||
                marker_kind != RUNTIME_HISTORY_MARKER_RESET_COMPLETE ||
                marker_arg0 != RUNTIME_HISTORY_RESET_CRT_ATTACH ||
                timeline != previous_timeline + 1u) {
                free(payload);
                fail("CRT attach/reset history order is incorrect");
            }
            free(payload);
            return;
        }
        if (kind == RUNTIME_HISTORY_RECORD_MARKER &&
            marker_kind == RUNTIME_HISTORY_MARKER_CRT_ATTACH) {
            saw_crt = true;
            previous_timeline = timeline;
        }
        if (record_size < 48u || offset + record_size > length) {
            free(payload);
            fail("invalid CRT history record size");
        }
        offset += record_size;
    }
    free(payload);
    fail("CRT marker not found in retained history");
}

/* A paused write_memory_byte publishes a memory echo event; consume it so a
   following read's poll_event does not pick up the write's echo by mistake. */
static void write_cpu_map_byte(runtime_client *client, uint16_t address, uint8_t value) {
    runtime_event event;

    if (!runtime_client_write_memory_byte(client, address, value, RUNTIME_MEMORY_MODE_CPU_MAP)) {
        fail("write_memory_byte command failed");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("write echo not received");
    }
}

static void load_crt_and_pause(runtime_client *client, const char *path, const char *label) {
    runtime_event event;

    if (!runtime_client_load_crt(client, path)) {
        fail("load CRT command failed");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("CRT RESET_COMPLETE not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("CRT RUNNING not received");
    }
    if (!runtime_client_pause(client)) {
        fail("pause after CRT load failed");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("CRT PAUSED not received");
    }
    /* The machine free-runs (executing cart bytes) before we pause, leaving the
       CPU port in an arbitrary banking state. Force $01=$37 (LORAM+HIRAM+CHAREN)
       so IO and the cart window are deterministically visible for our probes. */
    write_cpu_map_byte(client, 0x0001, 0x37);
    (void)label;
}

static uint8_t read_cpu_map_byte(runtime_client *client, uint16_t address) {
    runtime_event event;

    if (!runtime_client_request_memory(client, address, 1, RUNTIME_MEMORY_MODE_CPU_MAP)) {
        fail("request_memory command failed");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("memory response not received");
    }
    return event.data.memory.bytes[0];
}

int main(void) {
    static const char crt_path[] = "runtime crt (test).crt";
    runtime_config config = {
        .system_rom_path = "runtime_crt_64c.bin",
        .char_rom_path = "runtime_crt_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    write_test_roms();
    write_generic_16k_crt(crt_path);

    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (rt == NULL) {
        fail("runtime_create failed");
    }
    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("startup RESET_COMPLETE not received");
    }
    drain_runtime_events(client);

    expect_true("load CRT", runtime_client_load_crt(client, crt_path));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("CRT RESET_COMPLETE not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("CRT RUNNING event not received");
    }

    expect_true("pause after CRT", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("CRT PAUSED event not received");
    }
    expect_crt_history_order(client);

    expect_true(
        "request ROML map",
        runtime_client_request_memory(client, 0x8000, 4, RUNTIME_MEMORY_MODE_CPU_MAP));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("ROML memory response not received");
    }
    expect_u8("ROML byte 0", 0x80, event.data.memory.bytes[0]);
    expect_u8("ROML byte 1", 0x81, event.data.memory.bytes[1]);

    expect_true(
        "request ROMH map",
        runtime_client_request_memory(client, 0xa000, 4, RUNTIME_MEMORY_MODE_CPU_MAP));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("ROMH memory response not received");
    }
    expect_u8("ROMH byte 0", 0xa0, event.data.memory.bytes[0]);
    expect_u8("ROMH byte 1", 0xa1, event.data.memory.bytes[1]);

    /* Loading a PRG must detach the cartridge so the program boots instead of
       the cartridge. After the load, $8000 should read RAM, not cartridge
       ROML (which the CRT filled with 0x80,0x81,0x82,0x83). */
    write_test_prg("runtime_crt_test.prg");
    expect_true("load PRG detaches CRT",
                runtime_client_load_prg(client, "runtime_crt_test.prg"));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("PRG RESET_COMPLETE not received");
    }
    expect_true("pause after PRG", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PRG PAUSED event not received");
    }
    expect_true(
        "request $8000 after PRG",
        runtime_client_request_memory(client, 0x8000, 4, RUNTIME_MEMORY_MODE_CPU_MAP));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("post-PRG memory response not received");
    }
    if (event.data.memory.bytes[0] == 0x80 && event.data.memory.bytes[1] == 0x81 &&
        event.data.memory.bytes[2] == 0x82 && event.data.memory.bytes[3] == 0x83) {
        fail("cartridge still mapped at $8000 after loading a PRG");
    }

    /* Re-attach a cartridge to exercise the reset unmount option. */
    expect_true("reload CRT", runtime_client_load_crt(client, crt_path));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("reload CRT RESET_COMPLETE not received");
    }

    /* Reset keeping the cartridge: $8000 stays cartridge ROML. */
    expect_true("reset keeping cartridge", runtime_client_reset_ex(client, false));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("keep-cartridge RESET_COMPLETE not received");
    }
    expect_true("pause after keep-cartridge reset", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("keep-cartridge PAUSED not received");
    }
    expect_true(
        "request $8000 after keep reset",
        runtime_client_request_memory(client, 0x8000, 4, RUNTIME_MEMORY_MODE_CPU_MAP));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("keep-cartridge memory response not received");
    }
    expect_u8("cartridge kept ROML byte 0", 0x80, event.data.memory.bytes[0]);

    /* Reset unmounting the cartridge: $8000 becomes RAM. */
    expect_true("reset unmounting cartridge", runtime_client_reset_ex(client, true));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("unmount RESET_COMPLETE not received");
    }
    expect_true("pause after unmount reset", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("unmount PAUSED not received");
    }
    expect_true(
        "request $8000 after unmount reset",
        runtime_client_request_memory(client, 0x8000, 4, RUNTIME_MEMORY_MODE_CPU_MAP));
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("unmount memory response not received");
    }
    if (event.data.memory.bytes[0] == 0x80 && event.data.memory.bytes[1] == 0x81 &&
        event.data.memory.bytes[2] == 0x82 && event.data.memory.bytes[3] == 0x83) {
        fail("cartridge still mapped at $8000 after reset with unmount");
    }

    /* Magic Desk (type 19): load multi-bank cart and verify bank 0 at $8000. */
    {
        static const char md_path[] = "runtime magic desk (test).crt";
        write_magic_desk_crt(md_path);
        expect_true("load Magic Desk CRT", runtime_client_load_crt(client, md_path));
        if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
            fail("Magic Desk RESET_COMPLETE not received");
        }
        if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
            fail("Magic Desk RUNNING not received");
        }
        expect_true("pause after Magic Desk", runtime_client_pause(client));
        if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
            fail("Magic Desk PAUSED not received");
        }
        expect_true(
            "request Magic Desk ROML",
            runtime_client_request_memory(client, 0x8000, 2, RUNTIME_MEMORY_MODE_CPU_MAP));
        if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
            fail("Magic Desk memory response not received");
        }
        /* Bank 0 first byte pattern: ((0+1)<<4) | 0 = 0x10 */
        expect_u8("Magic Desk bank0 byte0", 0x10, event.data.memory.bytes[0]);
        expect_u8("Magic Desk bank0 byte1", 0x11, event.data.memory.bytes[1]);
        remove(md_path);
    }

    /* Ocean type 1: multi-bank with ROML/ROMH mirror. */
    {
        static const char ocean_path[] = "runtime ocean (test).crt";
        write_ocean_crt(ocean_path);
        expect_true("load Ocean CRT", runtime_client_load_crt(client, ocean_path));
        if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
            fail("Ocean RESET_COMPLETE not received");
        }
        if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
            fail("Ocean RUNNING not received");
        }
        expect_true("pause after Ocean", runtime_client_pause(client));
        if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
            fail("Ocean PAUSED not received");
        }
        expect_true(
            "request Ocean ROML",
            runtime_client_request_memory(client, 0x8000, 2, RUNTIME_MEMORY_MODE_CPU_MAP));
        if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
            fail("Ocean ROML memory response not received");
        }
        /* Bank 0: ((0+2)<<4)|0 = 0x20 */
        expect_u8("Ocean bank0 roml0", 0x20, event.data.memory.bytes[0]);
        expect_true(
            "request Ocean ROMH",
            runtime_client_request_memory(client, 0xa000, 1, RUNTIME_MEMORY_MODE_CPU_MAP));
        if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
            fail("Ocean ROMH memory response not received");
        }
        expect_u8("Ocean bank0 romh0", 0x20, event.data.memory.bytes[0]);
        remove(ocean_path);
    }

    /* C64GS (type 15): bank from address on write; verify bank switch. */
    {
        static const char path[] = "runtime c64gs (test).crt";
        uint16_t banks[8];
        uint8_t fills[8];
        size_t i;
        for (i = 0; i < 8; ++i) {
            banks[i] = (uint16_t)i;
            fills[i] = (uint8_t)(0x20u + i);
        }
        write_banked_8k_crt(path, 15, 0, 1, banks, fills, 8);
        load_crt_and_pause(client, path, "C64GS");
        expect_u8("C64GS power-on bank0", 0x20, read_cpu_map_byte(client, 0x8000));
        /* Write to $DE05 selects bank 5 (address bits, value ignored). */
        write_cpu_map_byte(client, 0xde05, 0x00);
        expect_u8("C64GS bank5", 0x25, read_cpu_map_byte(client, 0x8000));
        remove(path);
    }

    /* Dinamic (type 17): bank switches on READ of $de00-$de0f. */
    {
        static const char path[] = "runtime dinamic (test).crt";
        uint16_t banks[16];
        uint8_t fills[16];
        size_t i;
        for (i = 0; i < 16; ++i) {
            banks[i] = (uint16_t)i;
            fills[i] = (uint8_t)(0x30u + i);
        }
        write_banked_8k_crt(path, 17, 0, 1, banks, fills, 16);
        load_crt_and_pause(client, path, "Dinamic");
        /* Dinamic switches banks only on a real CPU read of $de00-$de0f; the
           debug memory inspector is side-effect-free (peek), so it cannot drive
           the switch here — that path is covered by test_c64_bus.c. This block
           verifies the load/copy/attach wiring produces bank 0 at power-on. */
        expect_u8("Dinamic power-on bank0", 0x30, read_cpu_map_byte(client, 0x8000));
        remove(path);
    }

    /* Fun Play (type 7): CRT chip.bank is the scrambled register value; the
       runtime de-scrambles to linear on load. Verify a non-zero bank. */
    {
        static const char path[] = "runtime funplay (test).crt";
        uint16_t banks[16];
        uint8_t fills[16];
        size_t linear;
        for (linear = 0; linear < 16; ++linear) {
            uint8_t scrambled =
                (uint8_t)(((linear & 7u) << 3) | ((linear >> 3) & 1u));
            banks[linear] = scrambled;      /* CRT stores the register value */
            fills[linear] = (uint8_t)(0x40u + linear); /* data tags the linear index */
        }
        write_banked_8k_crt(path, 7, 0, 0, banks, fills, 16);
        load_crt_and_pause(client, path, "Fun Play");
        expect_u8("Fun Play power-on bank0", 0x40, read_cpu_map_byte(client, 0x8000));
        /* Register value $08 de-scrambles to linear bank 1. */
        write_cpu_map_byte(client, 0xde00, 0x08);
        expect_u8("Fun Play linear bank1", 0x41, read_cpu_map_byte(client, 0x8000));
        remove(path);
    }

    /* Super Games (type 8): 16K chips split into ROML+ROMH; IO2 $DF00 control. */
    {
        static const char path[] = "runtime super games (test).crt";
        uint8_t roml[4] = { 0x50, 0x51, 0x52, 0x53 };
        uint8_t romh[4] = { 0x60, 0x61, 0x62, 0x63 };
        write_super_games_crt(path, roml, romh, 4);
        load_crt_and_pause(client, path, "Super Games");
        expect_u8("Super Games bank0 roml", 0x50, read_cpu_map_byte(client, 0x8000));
        expect_u8("Super Games bank0 romh", 0x60, read_cpu_map_byte(client, 0xa000));
        /* $DF00 = $01: bank 1, 16K enabled. */
        write_cpu_map_byte(client, 0xdf00, 0x01);
        expect_u8("Super Games bank1 roml", 0x51, read_cpu_map_byte(client, 0x8000));
        expect_u8("Super Games bank1 romh", 0x61, read_cpu_map_byte(client, 0xa000));
        remove(path);
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();

    remove(crt_path);
    remove("runtime_crt_test.prg");
    remove("runtime_crt_64c.bin");
    remove("runtime_crt_character.bin");
    return 0;
}
