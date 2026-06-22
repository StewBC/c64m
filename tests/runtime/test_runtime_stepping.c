/*
 * Tests for the invariant: after any Step / Step-Over / Step-Out command the
 * PC must change, except when the instruction itself is a self-targeting
 * JMP/JSR/branch.
 *
 * Step-Out semantics: exit the routine I am currently IN (the one that
 * contains the instruction the PC is sitting on).  If that routine never
 * returns, Step-Out must fall back to free-running mode so the UI is not
 * silently frozen.
 *
 * Test ROM layout (patched into KERNAL, starting at $E000):
 *
 *   E000: EA             NOP
 *   E001: EA             NOP
 *   E002: 20 10 E0       JSR $E010     <- outer call
 *   E005: EA             NOP           <- landing pad after outer JSR returns
 *   E006..E00F: EA       NOP padding
 *   E010: 20 20 E0       JSR $E020     <- middle routine, first instr = JSR
 *   E013: EA             NOP           <- middle routine continues
 *   E014: 60             RTS           <- middle routine exits
 *   E015..E01F: EA       NOP padding
 *   E020: EA             NOP           <- inner routine body
 *   E021: 60             RTS           <- inner routine exits
 *
 * Reset vector: $E000.
 *
 * Step-out invariant from $E010 (PC on JSR $E020, inside middle routine):
 *   jsr_counter = 1  (we are inside middle routine, need 1 net RTS)
 *   execute JSR $E020  -> counter = 2
 *   inner NOP, inner RTS -> counter = 1   (back in middle routine)
 *   middle NOP, middle RTS -> counter = 0 -> STOP at $E005
 */

#include "c64_bus.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* addresses                                                           */
/* ------------------------------------------------------------------ */

enum {
    ADDR_RESET       = 0xe000, /* NOP  -- first instruction after reset  */
    ADDR_NOP1        = 0xe001, /* NOP                                    */
    ADDR_OUTER_JSR   = 0xe002, /* JSR $E010  (outer call)                */
    ADDR_OUTER_LAND  = 0xe005, /* NOP        (landing after outer JSR)   */
    ADDR_MID_ENTRY   = 0xe010, /* JSR $E020  (middle routine starts here) */
    ADDR_MID_NOP     = 0xe013, /* NOP        (middle routine, after JSR) */
    ADDR_MID_RTS     = 0xe014, /* RTS        (middle routine exits)      */
    ADDR_INNER_NOP   = 0xe020, /* NOP        (inner routine body)        */
    ADDR_INNER_RTS   = 0xe021, /* RTS        (inner routine exits)       */
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_true(const char *name, int v) {
    if (!v) {
        fprintf(stderr, "FAIL: %s: expected true\n", name);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %04x, got %04x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_ne_u16(const char *name, uint16_t unexpected, uint16_t actual) {
    if (unexpected == actual) {
        fprintf(stderr, "FAIL: %s: PC stayed at %04x -- step did not advance\n",
                name, actual);
        exit(1);
    }
}

/* Poll up to 2 s for the requested event type.  Dies on ERROR. */
static int poll_event(runtime_client *client, runtime_event *event,
                      runtime_event_type type) {
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

static void drain_events(runtime_client *client) {
    runtime_event event;
    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            fprintf(stderr, "runtime error: %s\n", event.data.error.message);
            exit(1);
        }
    }
}

/* ------------------------------------------------------------------ */
/* ROM setup                                                           */
/* ------------------------------------------------------------------ */

/*
 * Byte layout matching the address map in the file header comment.
 * Total: 34 bytes.
 */
static const uint8_t STEP_TEST_CODE[] = {
    0xEA,                   /* $E000: NOP */
    0xEA,                   /* $E001: NOP */
    0x20, 0x10, 0xE0,       /* $E002: JSR $E010 (outer call) */
    0xEA,                   /* $E005: NOP (outer landing pad) */
    0xEA, 0xEA, 0xEA, 0xEA, /* $E006-$E009: padding */
    0xEA, 0xEA, 0xEA, 0xEA, /* $E00A-$E00D: padding */
    0xEA, 0xEA,             /* $E00E-$E00F: padding */
    0x20, 0x20, 0xE0,       /* $E010: JSR $E020 (middle routine entry) */
    0xEA,                   /* $E013: NOP (middle routine continues) */
    0x60,                   /* $E014: RTS (middle routine exits) */
    0xEA, 0xEA, 0xEA, 0xEA, /* $E015-$E018: padding */
    0xEA, 0xEA, 0xEA, 0xEA, /* $E019-$E01C: padding */
    0xEA, 0xEA, 0xEA,       /* $E01D-$E01F: padding */
    0xEA,                   /* $E020: NOP (inner routine body) */
    0x60,                   /* $E021: RTS (inner routine exits) */
};

static void write_stepping_roms(void) {
    FILE *system    = fopen("stepping_64c.bin",       "wb");
    FILE *character = fopen("stepping_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create stepping test ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < sizeof(STEP_TEST_CODE); i++) {
        fputc(STEP_TEST_CODE[i], system);
    }
    for (i = sizeof(STEP_TEST_CODE); i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    /* Reset vector = $E000 */
    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffcu), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);

    for (i = 0; i < C64_CHAR_ROM_SIZE; i++) {
        fputc(0x00, character);
    }
    fclose(system);
    fclose(character);
}

/* ------------------------------------------------------------------ */
/* runtime lifecycle                                                   */
/* ------------------------------------------------------------------ */

static runtime *start_stepping_runtime(runtime_client **out_client) {
    static const runtime_config config = {
        .system_rom_path = "stepping_64c.bin",
        .char_rom_path   = "stepping_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    expect_true("runtime_init", runtime_init());
    rt = runtime_create(&config);
    if (!rt) { fail("runtime_create failed"); }
    expect_true("runtime_start", runtime_start(rt));
    client = runtime_get_client(rt);

    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED))           { fail("STARTED"); }
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE))    { fail("RESET_COMPLETE"); }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)){ fail("CPU_STATE_RESPONSE"); }
    drain_events(client);

    *out_client = client;
    return rt;
}

static void stop_stepping_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
}

/* ------------------------------------------------------------------ */
/* stepping helpers                                                    */
/* ------------------------------------------------------------------ */

static uint16_t step_once(runtime_client *client, runtime_event *event,
                          const char *tag) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: step_instruction", tag);
    expect_true(msg, runtime_client_step_instruction(client));
    snprintf(msg, sizeof(msg), "%s: STEP_COMPLETE", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_STEP_COMPLETE)) { fail(msg); }
    snprintf(msg, sizeof(msg), "%s: CPU_STATE_RESPONSE", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) { fail(msg); }
    return event->data.cpu_state.pc;
}

static uint32_t set_execute_bp(runtime_client *client, runtime_event *event,
                               uint16_t addr, const char *tag) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: set_execute_breakpoint", tag);
    expect_true(msg, runtime_client_set_execute_breakpoint(client, addr));
    snprintf(msg, sizeof(msg), "%s: BREAKPOINTS_RESPONSE", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) { fail(msg); }
    if (event->data.breakpoints.count == 0) { fail("set_execute_bp: empty response"); }
    return event->data.breakpoints.entries[0].id;
}

static void clear_bp(runtime_client *client, runtime_event *event,
                     uint32_t id, const char *tag) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: clear_breakpoint", tag);
    expect_true(msg, runtime_client_clear_breakpoint(client, id));
    snprintf(msg, sizeof(msg), "%s: BREAKPOINTS_RESPONSE after clear", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_BREAKPOINTS_RESPONSE)) { fail(msg); }
}

static uint16_t step_over_expect_complete(runtime_client *client,
                                          runtime_event *event,
                                          const char *tag) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: step_over command", tag);
    expect_true(msg, runtime_client_step_over(client));
    snprintf(msg, sizeof(msg), "%s: STEP_COMPLETE after step_over", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_STEP_COMPLETE)) { fail(msg); }
    snprintf(msg, sizeof(msg), "%s: CPU_STATE_RESPONSE after step_over", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) { fail(msg); }
    return event->data.cpu_state.pc;
}

/*
 * Call step_out and expect it to find the outer RTS (STEP_COMPLETE).
 * Fails if STEP_COMPLETE doesn't arrive (e.g. if PAUSED is emitted instead,
 * meaning the BP at the current instruction was re-triggered without executing).
 */
static uint16_t step_out_expect_complete(runtime_client *client,
                                         runtime_event *event,
                                         const char *tag) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: step_out command", tag);
    expect_true(msg, runtime_client_step_out(client));
    snprintf(msg, sizeof(msg), "%s: STEP_COMPLETE after step_out", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_STEP_COMPLETE)) { fail(msg); }
    snprintf(msg, sizeof(msg), "%s: CPU_STATE_RESPONSE after step_out", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) { fail(msg); }
    return event->data.cpu_state.pc;
}

/*
 * Call step_out and expect it to give up and switch to RUNNING because the
 * outer routine never returns.  Pauses the runtime immediately afterwards
 * and returns the PC at that point.
 */
static uint16_t step_out_expect_running(runtime_client *client,
                                        runtime_event *event,
                                        const char *tag) {
    uint16_t pc;
    char msg[128];

    snprintf(msg, sizeof(msg), "%s: step_out command", tag);
    expect_true(msg, runtime_client_step_out(client));

    snprintf(msg, sizeof(msg), "%s: RUNNING after step_out fallback", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_RUNNING)) { fail(msg); }

    /* Pause so we can inspect the PC. */
    expect_true("pause after running fallback", runtime_client_pause(client));
    snprintf(msg, sizeof(msg), "%s: PAUSED after pause", tag);
    if (!poll_event(client, event, RUNTIME_EVENT_PAUSED)) { fail(msg); }
    if (!poll_event(client, event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE)) { fail(msg); }
    pc = event->data.machine_state.pc;
    drain_events(client);
    return pc;
}

static void write_ram_byte(runtime_client *client, runtime_event *event,
                           uint16_t addr, uint8_t value, const char *tag) {
    char msg[128];
    snprintf(msg, sizeof(msg), "%s: write $%04x=$%02x", tag, addr, value);
    expect_true(msg, runtime_client_write_memory_byte(
        client, addr, value, RUNTIME_MEMORY_MODE_RAM));
    snprintf(msg, sizeof(msg), "%s: MEMORY_RESPONSE $%04x", tag, addr);
    if (!poll_event(client, event, RUNTIME_EVENT_MEMORY_RESPONSE)) { fail(msg); }
}

/* ------------------------------------------------------------------ */
/* tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * step_instruction with a breakpoint at the current PC.
 * step_instruction has no BP check; it always executes.  PC must advance.
 */
static void test_step_instruction_with_bp_at_current_pc(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t bp_id;
    uint16_t pc;

    rt = start_stepping_runtime(&client);

    /* Arrive at $E001 via step (suppress_execute_bp=false after step). */
    step_once(client, &event, "pre-step to E001");
    expect_u16("pre-step PC", ADDR_NOP1, event.data.cpu_state.pc);

    bp_id = set_execute_bp(client, &event, ADDR_NOP1, "step_instruction bp");

    pc = step_once(client, &event, "step_instruction with bp at current pc");

    expect_ne_u16("step_instruction must advance past bp", ADDR_NOP1, pc);
    expect_u16("step_instruction lands at outer JSR", ADDR_OUTER_JSR, pc);

    clear_bp(client, &event, bp_id, "step_instruction bp cleanup");
    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_instruction_with_bp_at_current_pc\n");
}

/*
 * step_over on a non-JSR instruction with a breakpoint at the current PC.
 * The non-JSR path of step_over executes directly with no loop and no BP
 * check.  PC must advance.
 */
static void test_step_over_non_jsr_with_bp_at_current_pc(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t bp_id;
    uint16_t pc;

    rt = start_stepping_runtime(&client);

    step_once(client, &event, "pre-step to E001");
    expect_u16("pre-step PC", ADDR_NOP1, event.data.cpu_state.pc);

    bp_id = set_execute_bp(client, &event, ADDR_NOP1, "step_over non-jsr bp");

    pc = step_over_expect_complete(client, &event, "step_over non-jsr with bp");

    expect_ne_u16("step_over non-jsr must advance", ADDR_NOP1, pc);
    expect_u16("step_over non-jsr lands at outer JSR", ADDR_OUTER_JSR, pc);

    clear_bp(client, &event, bp_id, "step_over non-jsr bp cleanup");
    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_over_non_jsr_with_bp_at_current_pc\n");
}

/*
 * step_over a JSR with a breakpoint at the JSR address.
 * Arrived via step (suppress_execute_bp=false).  step_over must execute the
 * full JSR chain and stop at $E005 (the instruction after the outer JSR).
 *
 * Bug repro: without the suppress_execute_bp=true fix, step_over's loop
 * re-triggers the BP immediately and emits PAUSED instead of STEP_COMPLETE.
 */
static void test_step_over_jsr_with_bp_at_jsr(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t bp_id;
    uint16_t pc;

    rt = start_stepping_runtime(&client);

    /* Step to $E002 (the outer JSR). */
    step_once(client, &event, "step to E001");
    step_once(client, &event, "step to E002");
    expect_u16("at outer JSR before test", ADDR_OUTER_JSR, event.data.cpu_state.pc);

    bp_id = set_execute_bp(client, &event, ADDR_OUTER_JSR, "step_over jsr bp");

    pc = step_over_expect_complete(client, &event, "step_over jsr with bp");

    expect_ne_u16("step_over jsr must advance past JSR", ADDR_OUTER_JSR, pc);
    expect_u16("step_over jsr lands at outer landing", ADDR_OUTER_LAND, pc);

    clear_bp(client, &event, bp_id, "step_over jsr bp cleanup");
    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_over_jsr_with_bp_at_jsr\n");
}

/*
 * step_out from inside the middle routine (PC on its first instruction,
 * which is a JSR).  No breakpoint.
 *
 * jsr_counter=1 (inside middle routine, need 1 net RTS to exit).
 * Expected path: JSR $E020 (counter→2), NOP, RTS (counter→1), NOP, RTS
 * (counter→0) → stop at $E005.
 *
 * Bug repro (old jsr_counter=0 fix): step_out would stop when $E021 RTS
 * fired (counter would go from 1 to 0 after just the inner RTS), landing at
 * $E013 instead of $E005.
 */
static void test_step_out_inside_routine_on_jsr_no_bp(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint16_t pc;

    rt = start_stepping_runtime(&client);

    /* Enter middle routine via step on the outer JSR. */
    step_once(client, &event, "step to E001");
    step_once(client, &event, "step to E002");
    step_once(client, &event, "step outer JSR -> enter middle at E010");
    expect_u16("at middle routine entry", ADDR_MID_ENTRY, event.data.cpu_state.pc);

    pc = step_out_expect_complete(client, &event, "step_out inside routine on JSR no bp");

    expect_ne_u16("step_out must leave middle routine", ADDR_MID_ENTRY, pc);
    expect_u16("step_out exits middle routine to E005", ADDR_OUTER_LAND, pc);

    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_out_inside_routine_on_jsr_no_bp\n");
}

/*
 * step_out from inside the middle routine, PC on JSR, with a breakpoint at
 * that JSR address.  Arrived via step (suppress_execute_bp=false).
 *
 * Bug repro: without suppress_execute_bp=true fix, step_out re-triggers the
 * BP immediately and PC stays at $E010.
 */
static void test_step_out_inside_routine_on_jsr_with_bp(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t bp_id;
    uint16_t pc;

    rt = start_stepping_runtime(&client);

    step_once(client, &event, "step to E001");
    step_once(client, &event, "step to E002");
    step_once(client, &event, "step outer JSR -> enter middle at E010");
    expect_u16("at middle entry for bp test", ADDR_MID_ENTRY, event.data.cpu_state.pc);

    bp_id = set_execute_bp(client, &event, ADDR_MID_ENTRY, "step_out inside routine bp");

    pc = step_out_expect_complete(client, &event, "step_out inside routine with bp");

    expect_ne_u16("step_out with bp must leave E010", ADDR_MID_ENTRY, pc);
    expect_u16("step_out with bp exits to E005", ADDR_OUTER_LAND, pc);

    clear_bp(client, &event, bp_id, "step_out inside routine bp cleanup");
    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_out_inside_routine_on_jsr_with_bp\n");
}

/*
 * step_out from inside the middle routine, PC on the NOP after the inner JSR
 * has already returned.
 * jsr_counter=1: execute NOP, then RTS → counter=0 → stop at $E005.
 */
static void test_step_out_inside_routine_on_non_jsr(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint16_t pc;

    rt = start_stepping_runtime(&client);

    /* Get to $E013 (NOP inside middle routine, after inner JSR $E020 returned). */
    step_once(client, &event, "step to E001");
    step_once(client, &event, "step to E002");
    step_once(client, &event, "step outer JSR -> E010");
    /* step_over the inner JSR so we land at $E013 */
    step_over_expect_complete(client, &event, "step_over inner JSR E010->E013");
    expect_u16("at E013 (mid NOP)", ADDR_MID_NOP, event.data.cpu_state.pc);

    pc = step_out_expect_complete(client, &event, "step_out on non-jsr at E013");

    expect_ne_u16("step_out on NOP must leave E013", ADDR_MID_NOP, pc);
    expect_u16("step_out on NOP exits middle routine to E005", ADDR_OUTER_LAND, pc);

    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_out_inside_routine_on_non_jsr\n");
}

/*
 * step_out when the outer routine never returns (no RTS in sight).
 *
 * We write a JMP $2000 self-loop to RAM and position the CPU there.
 * The tight loop contains no JSR or RTS, so jsr_counter (starts at 1) never
 * reaches 0.  After STEP_OUT_FAST_LIMIT iterations step_out must fall back to
 * free-running RUNNING mode so the UI is never silently frozen.
 *
 * Bug repro: without the fallback, step_out spins forever; the UI shows no
 * change and the user perceives "nothing happened."
 */
static void test_step_out_fallback_to_running_when_no_rts(void) {
    runtime *rt;
    runtime_client *client;
    runtime_event event;

    rt = start_stepping_runtime(&client);

    /* Write JMP $2000 ($4C $00 $20) to RAM so we have a true infinite loop
     * with no JSR/RTS — guarantees the fallback fires, not a phantom RTS from
     * stale stack bytes walking off the ROM NOP sled. */
    write_ram_byte(client, &event, 0x2000, 0x4c, "fallback jmp");
    write_ram_byte(client, &event, 0x2001, 0x00, "fallback jmp");
    write_ram_byte(client, &event, 0x2002, 0x20, "fallback jmp");

    expect_true("set PC to $2000", runtime_client_set_pc(client, 0x2000));
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("CPU_STATE_RESPONSE after set PC to $2000");
    }
    expect_u16("PC set to $2000", 0x2000, event.data.cpu_state.pc);
    drain_events(client);

    /* step_out must fall back to RUNNING; the exact PC after pause is $2000
     * (self-JMP), so we only verify that the RUNNING event arrived. */
    step_out_expect_running(client, &event, "step_out fallback to running");

    stop_stepping_runtime(rt, client);
    printf("PASS: test_step_out_fallback_to_running_when_no_rts\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    write_stepping_roms();

    test_step_instruction_with_bp_at_current_pc();
    test_step_over_non_jsr_with_bp_at_current_pc();
    test_step_over_jsr_with_bp_at_jsr();
    test_step_out_inside_routine_on_jsr_no_bp();
    test_step_out_inside_routine_on_jsr_with_bp();
    test_step_out_inside_routine_on_non_jsr();
    test_step_out_fallback_to_running_when_no_rts();

    remove("stepping_64c.bin");
    remove("stepping_character.bin");
    return 0;
}
