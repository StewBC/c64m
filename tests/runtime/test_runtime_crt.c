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
