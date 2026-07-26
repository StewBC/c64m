/* End-to-end coverage for assemble's BASIC-run mode: assemble a BASIC-stub
   program, have the runtime fix the BASIC pointers and paste RUN, and confirm
   the SYS'd machine-language body actually executed. Uses the real BASIC/KERNAL
   ROMs because the flow depends on the editor consuming the pasted RUN and BASIC
   interpreting the SYS. */

#include "runtime.h"
#include "runtime_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef C64M_SOURCE_DIR
#define C64M_SOURCE_DIR "."
#endif

enum { RUN_TIMEOUT_SECONDS = 30 };

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

static int poll_event(runtime_client *client, runtime_event *event, runtime_event_type type) {
    time_t start = time(NULL);

    while (time(NULL) - start < 10) {
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
    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            fprintf(stderr, "runtime error: %s\n", event.data.error.message);
            exit(1);
        }
    }
}

static runtime *start_real_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = C64M_SOURCE_DIR "/roms/system.rom",
        .char_rom_path = C64M_SOURCE_DIR "/roms/character.rom",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    config.turbo_speeds[0] = 3; /* warp: free-run so the paste + SYS finish fast */
    config.turbo_speed_count = 1;
    config.active_turbo_multiplier = 3;

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
        fail("RESET_COMPLETE event not received");
    }
    drain_runtime_events(client);

    *out_client = client;
    return rt;
}

static void stop_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
}

/* Write a BASIC-stub program: "10 SYS 2061" followed by a small ML body at
   $080D that stores `marker` into `target`, so the caller can prove the SYS'd
   code ran. */
static void write_stub_source(const char *path, uint8_t marker, uint16_t target) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fail("failed to create basic-run test source");
    }
    fprintf(f,
        "* = $0801\n"
        "        .byte $0b, $08          ; link to end-of-program\n"
        "        .byte $0a, $00          ; line number 10\n"
        "        .byte $9e               ; SYS token\n"
        "        .byte $32, $30, $36, $31 ; \"2061\" -> $080d\n"
        "        .byte $00               ; end of line\n"
        "        .byte $00, $00          ; end of program\n"
        "        lda #$%02x\n"
        "        sta $%04x\n"
        "        rts\n",
        (unsigned)marker, (unsigned)target);
    fclose(f);
}

/* Poll one RAM byte until it equals `expected` (or time out). */
static bool poll_byte_equals(runtime_client *client, uint16_t address, uint8_t expected) {
    time_t start = time(NULL);
    runtime_event event;
    bool request_pending = false;

    while (time(NULL) - start < RUN_TIMEOUT_SECONDS) {
        if (!request_pending) {
            if (!runtime_client_request_memory(client, address, 1, RUNTIME_MEMORY_MODE_RAM)) {
                continue;
            }
            request_pending = true;
        }
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_MEMORY_RESPONSE &&
                event.data.memory.address == address &&
                event.data.memory.length >= 1) {
                if (event.data.memory.bytes[0] == expected) {
                    return true;
                }
                request_pending = false;
            }
        }
    }
    return false;
}

static void test_basic_run_reset_first(void) {
    const char *src = "basic_run_reset.asm";
    runtime *rt;
    runtime_client *client;

    write_stub_source(src, 0x42, 0xC000);
    rt = start_real_runtime(&client);

    /* reset=1: reset -> boot to BASIC READY -> assemble -> fix pointers ->
       paste RUN -> SYS 2061 runs the ML body. */
    expect_true("assemble basic-run reset",
        runtime_client_assemble_file_full(client, src, 0x0801, 0x0801,
            /*auto_run=*/false, /*basic_run=*/true, /*reset_first=*/true));

    if (!poll_byte_equals(client, 0xC000, 0x42)) {
        fail("basic-run (reset) did not execute the SYS'd ML body");
    }

    stop_runtime(rt, client);
    remove(src);
}

static void test_basic_run_no_reset_from_ready(void) {
    const char *src1 = "basic_run_first.asm";
    const char *src2 = "basic_run_second.asm";
    runtime *rt;
    runtime_client *client;

    write_stub_source(src1, 0x42, 0xC000);
    write_stub_source(src2, 0x43, 0xC001);
    rt = start_real_runtime(&client);

    /* First, reset+basic-run to leave the machine at a live BASIC READY prompt. */
    expect_true("assemble basic-run reset (first)",
        runtime_client_assemble_file_full(client, src1, 0x0801, 0x0801,
            false, true, true));
    if (!poll_byte_equals(client, 0xC000, 0x42)) {
        fail("first basic-run did not run");
    }

    /* Now basic-run with reset=0: this exercises the same immediate assemble +
       paste-RUN path the fresh-launch reset-skip optimization takes. */
    expect_true("assemble basic-run no-reset",
        runtime_client_assemble_file_full(client, src2, 0x0801, 0x0801,
            false, true, false));
    if (!poll_byte_equals(client, 0xC001, 0x43)) {
        fail("no-reset basic-run did not run from a READY prompt");
    }

    stop_runtime(rt, client);
    remove(src1);
    remove(src2);
}

int main(void) {
    test_basic_run_reset_first();
    test_basic_run_no_reset_from_ready();
    return 0;
}
