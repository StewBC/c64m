#include "runtime.h"
#include "runtime_client.h"

#include "c64_bus.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
    TEST_COLOR_GREEN = 0xff56ac4du,
    TEST_COLOR_BLUE = 0xff2e2c9bu,
    TEST_COLOR_LIGHT_BLUE = 0xff706debu,
    TEST_COLOR_WHITE = 0xffffffffu,
    TEST_ACTIVE_PIXEL = 51 * C64_FRAME_WIDTH + 24,
};

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

static void expect_u32(const char *name, uint32_t expected, uint32_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %llu, got %llu\n", name, (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

static void write_runtime_roms(void) {
    FILE *system = fopen("frame_64c.bin", "wb");
    FILE *character = fopen("frame_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create frame test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }

    fseek(system, (long)C64_BASIC_ROM_SIZE, SEEK_SET);
    fputc(0xa9, system); /* LDA #$10 */
    fputc(0x10, system);
    fputc(0x8d, system); /* STA $D011 */
    fputc(0x11, system);
    fputc(0xd0, system);
    fputc(0xa9, system); /* LDA #$05 */
    fputc(0x05, system);
    fputc(0x8d, system); /* STA $D020 */
    fputc(0x20, system);
    fputc(0xd0, system);
    fputc(0xa9, system); /* LDA #$01 */
    fputc(0x01, system);
    fputc(0x8d, system); /* STA $0400 */
    fputc(0x00, system);
    fputc(0x04, system);
    fputc(0x4c, system); /* JMP $E00F */
    fputc(0x0f, system);
    fputc(0xe0, system);

    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc((int)(TEST_RESET_VECTOR & 0xff), system);
    fputc((int)(TEST_RESET_VECTOR >> 8), system);

    for (i = 0; i < C64_CHAR_ROM_SIZE; i++) {
        fputc(0x00, character);
    }
    fseek(character, 8, SEEK_SET);
    fputc(0x80, character);

    fclose(system);
    fclose(character);
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

static void step_and_expect_frame(runtime_client *client, runtime_event *event, c64_frame *frame) {
    expect_true("step instruction", runtime_client_step_instruction(client));
    if (!poll_event(client, event, RUNTIME_EVENT_STEP_COMPLETE)) {
        fail("STEP_COMPLETE not received");
    }
    if (!poll_event(client, event, RUNTIME_EVENT_FRAME_READY)) {
        fail("FRAME_READY not received after step");
    }
    expect_true("poll step frame", runtime_client_poll_frame(client, frame));
}

static void write_byte(
    runtime_client *client,
    uint16_t address,
    uint8_t value,
    runtime_memory_mode mode) {
    expect_true("write memory byte", runtime_client_write_memory_byte(client, address, value, mode));
}

static void run_until_frame_then_pause(runtime_client *client, runtime_event *event, c64_frame *frame) {
    expect_true("run command", runtime_client_run(client));
    if (!poll_event(client, event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received");
    }
    if (!poll_event(client, event, RUNTIME_EVENT_FRAME_READY)) {
        fail("running FRAME_READY not received");
    }
    expect_true("poll running frame", runtime_client_poll_frame(client, frame));
    expect_true("pause command", runtime_client_pause(client));
    if (!poll_event(client, event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received");
    }
}

static runtime *start_runtime(runtime_client **out_client) {
    runtime_config config = {
        .system_rom_path = "frame_64c.bin",
        .char_rom_path = "frame_character.bin",
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
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }

    expect_true("runtime reset", runtime_client_reset(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE)) {
        fail("RESET_COMPLETE event not received");
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

static void test_request_frame_while_paused(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;

    rt = start_runtime(&client);

    expect_true("request frame", runtime_client_request_frame(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("FRAME_READY not received");
    }
    expect_true("poll copied frame", runtime_client_poll_frame(client, &frame));

    expect_u32("frame width", C64_FRAME_WIDTH, frame.width);
    expect_u32("frame height", C64_FRAME_NTSC_HEIGHT, frame.height);
    expect_u32("frame pixel format", C64_FRAME_PIXEL_FORMAT_ARGB8888, frame.pixel_format);
    expect_u64("paused frame cycle", 0, frame.machine_cycle);

    expect_true("request machine state", runtime_client_request_machine_state(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) {
        fail("MACHINE_STATE not received");
    }
    expect_u64("request frame does not advance cycles", 0, event.data.machine_state.cycle);
    expect_u64("runtime remains paused", 0, event.data.machine_state.running);

    stop_runtime(rt, client);
}

static void test_frame_while_running(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;

    rt = start_runtime(&client);

    expect_true("run command", runtime_client_run(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RUNNING)) {
        fail("RUNNING event not received");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("running FRAME_READY not received");
    }
    expect_true("poll running frame", runtime_client_poll_frame(client, &frame));
    if (frame.machine_cycle == 0) {
        fail("running frame did not carry an advanced machine cycle");
    }
    expect_u32("running frame starts blank before den", TEST_COLOR_LIGHT_BLUE, frame.pixels[8]);
    expect_u32("running frame shows border after den", TEST_COLOR_BLUE, frame.pixels[40]);
    expect_u32("running frame shows timed d020 write", TEST_COLOR_GREEN, frame.pixels[80]);

    expect_true("pause command", runtime_client_pause(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_PAUSED)) {
        fail("PAUSED event not received");
    }

    stop_runtime(rt, client);
}

static void test_step_instruction_publishes_updated_frame(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;
    uint32_t before;

    rt = start_runtime(&client);

    run_until_frame_then_pause(client, &event, &frame);

    write_byte(client, 0xd011, 0x1b, RUNTIME_MEMORY_MODE_CPU_MAP); /* DEN=1, RSEL=1, YSCROLL=3 */
    write_byte(client, 0xd016, 0x08, RUNTIME_MEMORY_MODE_CPU_MAP); /* CSEL=1, MCM=0 */
    write_byte(client, 0x0400, 0x00, RUNTIME_MEMORY_MODE_RAM);
    write_byte(client, 0xc000, 0xa9, RUNTIME_MEMORY_MODE_RAM);     /* LDA #$01 */
    write_byte(client, 0xc001, 0x01, RUNTIME_MEMORY_MODE_RAM);
    write_byte(client, 0xc002, 0x8d, RUNTIME_MEMORY_MODE_RAM);     /* STA $0400 */
    write_byte(client, 0xc003, 0x00, RUNTIME_MEMORY_MODE_RAM);
    write_byte(client, 0xc004, 0x04, RUNTIME_MEMORY_MODE_RAM);
    expect_true("set text PC", runtime_client_set_pc(client, 0xc000));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU_STATE not received after text PC set");
    }

    expect_true("request initial frame", runtime_client_request_frame(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("initial FRAME_READY not received");
    }
    expect_true("poll initial frame", runtime_client_poll_frame(client, &frame));
    before = frame.pixels[TEST_ACTIVE_PIXEL];

    step_and_expect_frame(client, &event, &frame); /* LDA #$01 */
    step_and_expect_frame(client, &event, &frame); /* STA $0400 */

    expect_true("request post-step text frame", runtime_client_request_frame(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("post-step text FRAME_READY not received");
    }
    expect_true("poll post-step text frame", runtime_client_poll_frame(client, &frame));

    if (frame.pixels[TEST_ACTIVE_PIXEL] == before) {
        fail("step frame did not reflect screen RAM write");
    }

    stop_runtime(rt, client);
}

static void test_step_instruction_publishes_updated_hires_frame(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    c64_frame frame;
    uint32_t before;

    rt = start_runtime(&client);

    run_until_frame_then_pause(client, &event, &frame);

    write_byte(client, 0xd011, 0x3b, RUNTIME_MEMORY_MODE_CPU_MAP); /* BMM=1, DEN=1, RSEL=1 */
    write_byte(client, 0xd016, 0x08, RUNTIME_MEMORY_MODE_CPU_MAP); /* CSEL=1, MCM=0 */
    write_byte(client, 0xd018, 0x18, RUNTIME_MEMORY_MODE_CPU_MAP); /* screen=$0400, bitmap=$2000 */
    write_byte(client, 0x0400, 0x10, RUNTIME_MEMORY_MODE_RAM);     /* white foreground, black background */

    write_byte(client, 0xc000, 0xa9, RUNTIME_MEMORY_MODE_RAM);     /* LDA #$80 */
    write_byte(client, 0xc001, 0x80, RUNTIME_MEMORY_MODE_RAM);
    write_byte(client, 0xc002, 0x8d, RUNTIME_MEMORY_MODE_RAM);     /* STA $2000 */
    write_byte(client, 0xc003, 0x00, RUNTIME_MEMORY_MODE_RAM);
    write_byte(client, 0xc004, 0x20, RUNTIME_MEMORY_MODE_RAM);

    expect_true("set PC", runtime_client_set_pc(client, 0xc000));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU_STATE not received after PC set");
    }

    expect_true("request hires initial frame", runtime_client_request_frame(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("initial hires FRAME_READY not received");
    }
    expect_true("poll initial hires frame", runtime_client_poll_frame(client, &frame));
    before = frame.pixels[TEST_ACTIVE_PIXEL];
    if (before == TEST_COLOR_WHITE) {
        fail("initial hires pixel was already foreground color");
    }

    step_and_expect_frame(client, &event, &frame); /* LDA #$80 */
    step_and_expect_frame(client, &event, &frame); /* STA $2000 */

    expect_true("request post-step hires frame", runtime_client_request_frame(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_FRAME_READY)) {
        fail("post-step hires FRAME_READY not received");
    }
    expect_true("poll post-step hires frame", runtime_client_poll_frame(client, &frame));

    expect_u32("hires bitmap step foreground", TEST_COLOR_WHITE, frame.pixels[TEST_ACTIVE_PIXEL]);

    stop_runtime(rt, client);
}

int main(void) {
    write_runtime_roms();
    test_request_frame_while_paused();
    test_frame_while_running();
    test_step_instruction_publishes_updated_frame();
    test_step_instruction_publishes_updated_hires_frame();
    remove("frame_64c.bin");
    remove("frame_character.bin");
    return 0;
}
