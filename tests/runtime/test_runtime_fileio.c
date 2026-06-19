#include "runtime.h"
#include "runtime_client.h"

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

static int poll_event_timeout(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static int poll_event_or_error(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                return -1;
            }
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static void write_fileio_roms(void) {
    FILE *system = fopen("fileio_64c.bin", "wb");
    FILE *character = fopen("fileio_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create fileio test ROMs");
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

static runtime *start_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = "fileio_64c.bin",
        .char_rom_path = "fileio_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (!rt) {
        fail("runtime_create failed");
    }

    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }
    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("startup RESET_COMPLETE not received");
    }
    /* drain remaining startup events */
    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.1) {
            while (runtime_client_poll_event(client, &event)) { /* drain */ }
        }
    }

    *out_client = client;
    return rt;
}

static void stop_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
}

static void write_paused_byte(runtime_client *client, uint16_t address, uint8_t value) {
    runtime_event event;

    expect_true("write byte", runtime_client_write_memory_byte(
        client, address, value, RUNTIME_MEMORY_MODE_RAM));

    if (!poll_event_timeout(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE)) {
        fail("memory write response not received");
    }
}

static void test_load_bin_with_file_address(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    int result;

    f = fopen("fileio_test.bin", "wb");
    if (!f) { fail("failed to create bin file"); }
    fputc(0x00, f); /* load address lo = $0800 */
    fputc(0x08, f); /* load address hi */
    fputc(0xAA, f);
    fputc(0xBB, f);
    fputc(0xCC, f);
    fclose(f);

    rt = start_runtime(&client);

    expect_true("load bin file-address",
        runtime_client_load_bin(client, "fileio_test.bin", 0x0000, true, false, false));

    result = poll_event_or_error(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE);
    if (result < 0) {
        fail("load_bin with file-address produced error");
    }
    if (result == 0) {
        fail("load_bin memory response not received");
    }

    expect_u8("file-address load byte 0", 0xAA, event.data.memory.bytes[0]);
    expect_u8("file-address load byte 1", 0xBB, event.data.memory.bytes[1]);
    expect_u8("file-address load byte 2", 0xCC, event.data.memory.bytes[2]);

    /* Verify address from header */
    expect_true("verify address", event.data.memory.address == 0x0800);

    stop_runtime(rt, client);
    remove("fileio_test.bin");
}

static void test_load_bin_without_file_address(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    int result;

    f = fopen("fileio_test.bin", "wb");
    if (!f) { fail("failed to create bin file"); }
    fputc(0x11, f);
    fputc(0x22, f);
    fputc(0x33, f);
    fclose(f);

    rt = start_runtime(&client);

    expect_true("load bin manual address",
        runtime_client_load_bin(client, "fileio_test.bin", 0xC000, false, false, false));

    result = poll_event_or_error(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE);
    if (result < 0) {
        fail("load_bin without file-address produced error");
    }
    if (result == 0) {
        fail("load_bin (no header) memory response not received");
    }

    expect_u8("manual-address load byte 0", 0x11, event.data.memory.bytes[0]);
    expect_u8("manual-address load byte 1", 0x22, event.data.memory.bytes[1]);
    expect_u8("manual-address load byte 2", 0x33, event.data.memory.bytes[2]);
    expect_true("verify manual address", event.data.memory.address == 0xC000);

    stop_runtime(rt, client);
    remove("fileio_test.bin");
}

static void test_load_bin_rejects_short_file_address_mode(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    int result;

    /* One-byte file: too short for a 2-byte header */
    f = fopen("fileio_short.bin", "wb");
    if (!f) { fail("failed to create short bin file"); }
    fputc(0xFF, f);
    fclose(f);

    rt = start_runtime(&client);

    expect_true("send load_bin short",
        runtime_client_load_bin(client, "fileio_short.bin", 0x0000, true, false, false));

    result = poll_event_or_error(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE);
    if (result >= 0) {
        fail("load_bin with short file and file-address should have errored");
    }

    stop_runtime(rt, client);
    remove("fileio_short.bin");
}

static void test_load_bin_rejects_zero_byte_file(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    int result;

    f = fopen("fileio_empty.bin", "wb");
    if (!f) { fail("failed to create empty bin file"); }
    fclose(f);

    rt = start_runtime(&client);

    expect_true("send load_bin empty",
        runtime_client_load_bin(client, "fileio_empty.bin", 0x0000, false, false, false));

    result = poll_event_or_error(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE);
    if (result >= 0) {
        fail("load_bin with empty file should have errored");
    }

    stop_runtime(rt, client);
    remove("fileio_empty.bin");
}

static void test_save_basic_writes_prg_header(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    uint8_t buf[32];
    size_t n;

    rt = start_runtime(&client);

    /* Set TXTTAB ($2B-$2C) = $0801 */
    write_paused_byte(client, 0x2B, 0x01);
    write_paused_byte(client, 0x2C, 0x08);

    /* Set VARTAB ($2D-$2E) = $0806 (5 bytes of BASIC program) */
    write_paused_byte(client, 0x2D, 0x06);
    write_paused_byte(client, 0x2E, 0x08);

    /* Write 5 test bytes at $0801-$0805 */
    write_paused_byte(client, 0x0801, 0x0A);
    write_paused_byte(client, 0x0802, 0x0B);
    write_paused_byte(client, 0x0803, 0x0C);
    write_paused_byte(client, 0x0804, 0x0D);
    write_paused_byte(client, 0x0805, 0x0E);

    expect_true("save basic", runtime_client_save_bin(client, "fileio_basic.prg", 0, 0, true, true));

    /* No dedicated event for save; poll for any error to detect failure */
    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.5) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fail("save_basic produced error event");
                }
            }
        }
    }

    stop_runtime(rt, client);

    f = fopen("fileio_basic.prg", "rb");
    if (!f) { fail("save_basic output file not found"); }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (n != 7) {
        fprintf(stderr, "save_basic: expected 7 bytes, got %zu\n", n);
        exit(1);
    }
    /* PRG header: $01 $08 */
    expect_u8("basic header lo", 0x01, buf[0]);
    expect_u8("basic header hi", 0x08, buf[1]);
    /* BASIC program bytes */
    expect_u8("basic byte 0", 0x0A, buf[2]);
    expect_u8("basic byte 1", 0x0B, buf[3]);
    expect_u8("basic byte 2", 0x0C, buf[4]);
    expect_u8("basic byte 3", 0x0D, buf[5]);
    expect_u8("basic byte 4", 0x0E, buf[6]);

    remove("fileio_basic.prg");
}

static void test_save_bin_with_file_address(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    uint8_t buf[8];
    size_t n;

    rt = start_runtime(&client);

    /* Write 3 test bytes at $C000-$C002 */
    write_paused_byte(client, 0xC000, 0x11);
    write_paused_byte(client, 0xC001, 0x22);
    write_paused_byte(client, 0xC002, 0x33);

    expect_true("save bin with header",
        runtime_client_save_bin(client, "fileio_out.bin", 0xC000, 0xC002, true, false));

    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.5) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fail("save_bin with header produced error event");
                }
            }
        }
    }

    stop_runtime(rt, client);

    f = fopen("fileio_out.bin", "rb");
    if (!f) { fail("save_bin output file not found"); }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (n != 5) {
        fprintf(stderr, "save_bin with header: expected 5 bytes, got %zu\n", n);
        exit(1);
    }
    expect_u8("bin header lo", 0x00, buf[0]);
    expect_u8("bin header hi", 0xC0, buf[1]);
    expect_u8("bin byte 0", 0x11, buf[2]);
    expect_u8("bin byte 1", 0x22, buf[3]);
    expect_u8("bin byte 2", 0x33, buf[4]);

    remove("fileio_out.bin");
}

static void test_save_bin_without_file_address(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    uint8_t buf[8];
    size_t n;

    rt = start_runtime(&client);

    write_paused_byte(client, 0xD000, 0xAA);
    write_paused_byte(client, 0xD001, 0xBB);

    expect_true("save bin without header",
        runtime_client_save_bin(client, "fileio_noheader.bin", 0xD000, 0xD001, false, false));

    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.5) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fail("save_bin without header produced error event");
                }
            }
        }
    }

    stop_runtime(rt, client);

    f = fopen("fileio_noheader.bin", "rb");
    if (!f) { fail("save_bin (no header) output file not found"); }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (n != 2) {
        fprintf(stderr, "save_bin without header: expected 2 bytes, got %zu\n", n);
        exit(1);
    }
    expect_u8("no-header byte 0", 0xAA, buf[0]);
    expect_u8("no-header byte 1", 0xBB, buf[1]);

    remove("fileio_noheader.bin");
}

static void test_save_bin_single_byte(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *f;
    uint8_t buf[4];
    size_t n;

    rt = start_runtime(&client);

    write_paused_byte(client, 0x1000, 0x42);

    expect_true("save bin single byte",
        runtime_client_save_bin(client, "fileio_single.bin", 0x1000, 0x1000, false, false));

    {
        clock_t start = clock();
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.5) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fail("save_bin single byte produced error event");
                }
            }
        }
    }

    stop_runtime(rt, client);

    f = fopen("fileio_single.bin", "rb");
    if (!f) { fail("save_bin single byte output file not found"); }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (n != 1) {
        fprintf(stderr, "save_bin single byte: expected 1 byte, got %zu\n", n);
        exit(1);
    }
    expect_u8("single byte value", 0x42, buf[0]);

    remove("fileio_single.bin");
}

static void test_save_bin_rejects_start_greater_than_end(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    int result;

    rt = start_runtime(&client);

    expect_true("save bin start > end send",
        runtime_client_save_bin(client, "fileio_bad.bin", 0x8000, 0x7FFF, false, false));

    result = poll_event_or_error(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE);
    if (result >= 0) {
        fail("save_bin with start > end should have produced error");
    }

    stop_runtime(rt, client);
    remove("fileio_bad.bin");
}

int main(void) {
    write_fileio_roms();
    test_load_bin_with_file_address();
    test_load_bin_without_file_address();
    test_load_bin_rejects_short_file_address_mode();
    test_load_bin_rejects_zero_byte_file();
    test_save_basic_writes_prg_header();
    test_save_bin_with_file_address();
    test_save_bin_without_file_address();
    test_save_bin_single_byte();
    test_save_bin_rejects_start_greater_than_end();
    remove("fileio_64c.bin");
    remove("fileio_character.bin");
    return 0;
}
