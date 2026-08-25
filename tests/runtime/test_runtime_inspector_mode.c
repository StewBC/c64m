/* I2: Inspect mode into live c64_t, enter/leave NOW, land, read-only, control. */
#include "c64.h"
#include "c64_bus.h"
#include "c64_snapshot.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_history.h"
#include "runtime_frame_ring.h"
#include "runtime_inspector.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void write_test_roms(void)
{
    FILE *system = fopen("runtime_insp_mode_64c.bin", "wb");
    FILE *character = fopen("runtime_insp_mode_character.bin", "wb");
    size_t i;

    if (system == NULL || character == NULL) {
        fail("failed to create runtime test ROMs");
    }
    for (i = 0u; i < C64_BASIC_ROM_SIZE + C64_KERNAL_ROM_SIZE; ++i) {
        fputc(0xeau, system);
    }
    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffcu), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);
    for (i = 0u; i < C64_CHAR_ROM_SIZE; ++i) {
        fputc(0x00, character);
    }
    fclose(system);
    fclose(character);
}

static int poll_event(
    runtime_client *client,
    runtime_event_type type,
    uint64_t token,
    runtime_event *out_event,
    int fail_on_error)
{
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 5.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (fail_on_error && event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == type &&
                (token == 0u || event.request_token == token)) {
                if (out_event != NULL) {
                    *out_event = event;
                }
                return 1;
            }
        }
    }
    return 0;
}

static void drain(runtime_client *client)
{
    runtime_event event;
    if (!runtime_client_ping(client) ||
        !poll_event(client, RUNTIME_EVENT_PONG, 0u, &event, 0)) {
        fail("ping timeout");
    }
}

static int wait_inspector_mode(
    runtime_client *client,
    uint64_t token,
    runtime_event *out,
    int *saw_reason,
    runtime_state_changed_reason want_reason)
{
    clock_t start = clock();
    runtime_event event;
    int got_mode = 0;

    if (saw_reason != NULL) {
        *saw_reason = 0;
    }
    while ((double)(clock() - start) / CLOCKS_PER_SEC < 5.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_STATE_CHANGED &&
                event.data.state_changed.reason == want_reason) {
                if (saw_reason != NULL) {
                    *saw_reason = 1;
                }
            }
            if (event.type == RUNTIME_EVENT_INSPECTOR_MODE &&
                event.request_token == token) {
                if (out != NULL) {
                    *out = event;
                }
                got_mode = 1;
            }
        }
        if (got_mode) {
            return 1;
        }
    }
    return 0;
}

static int wait_error_code(runtime_client *client, const char *code)
{
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 5.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR &&
                strcmp(event.data.error.code, code) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int wait_cpu(runtime_client *client, runtime_cpu_snapshot *out)
{
    runtime_event event;

    if (!runtime_client_request_cpu_state(client)) {
        return 0;
    }
    if (!poll_event(client, RUNTIME_EVENT_CPU_STATE_RESPONSE, 0u, &event, 0)) {
        return 0;
    }
    if (out != NULL) {
        *out = event.data.cpu_state;
    }
    return 1;
}

static runtime *start_runtime(runtime_config *config, runtime_client **out_client)
{
    runtime *rt = runtime_create(config);
    runtime_event event;

    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime start failed");
    }
    *out_client = runtime_get_client(rt);
    if (!poll_event(*out_client, RUNTIME_EVENT_STARTED, 0u, &event, 1) ||
        !poll_event(*out_client, RUNTIME_EVENT_RESET_COMPLETE, 0u, &event, 1)) {
        fail("runtime startup timeout");
    }
    return rt;
}

static void stop_runtime(runtime *rt, runtime_client *client)
{
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
}

static void fill_base_config(runtime_config *config)
{
    memset(config, 0, sizeof(*config));
    config->system_rom_path = "runtime_insp_mode_64c.bin";
    config->char_rom_path = "runtime_insp_mode_character.bin";
    config->history_memory_mb = 16u;
    config->history_memory_mb_configured = true;
    config->frame_ring_memory_mb = 8u;
    config->frame_ring_memory_mb_configured = true;
    config->vic_ring_memory_mb = 0u;
    config->vic_ring_memory_mb_configured = true;
    config->inspector = true;
    config->inspector_memory_mb = 16u;
    config->inspector_memory_mb_configured = true;
}

static void expect_true(const char *name, int v)
{
    if (!v) {
        fail(name);
    }
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    uint64_t token;
    runtime_event ev;
    int saw_reason = 0;
    runtime_cpu_snapshot cpu;
    uint16_t pc_now;
    uint64_t cycles_now;
    uint8_t ram_now;
    uint8_t sid0_now;
    uint16_t drive_pc_now;
    runtime_history_status hist;

    write_test_roms();
    fill_base_config(&config);
    rt = start_runtime(&config, &client);
    drain(client);

    expect_true("run", runtime_client_run(client));
    expect_true("running", poll_event(client, RUNTIME_EVENT_RUNNING, 0u, NULL, 1));
    {
        clock_t t0 = clock();
        while ((double)(clock() - t0) / CLOCKS_PER_SEC < 0.15) {
        }
    }
    expect_true("pause", runtime_client_pause(client));
    expect_true("paused", poll_event(client, RUNTIME_EVENT_PAUSED, 0u, NULL, 1));
    drain(client);

    expect_true("inspector on", runtime_inspector_enabled(rt));
    expect_true("has CP", runtime_inspector_checkpoint_count(rt) >= 1u);

    pc_now = rt->machine.cpu.cpu.pc;
    cycles_now = rt->machine.clock.cycle;
    ram_now = c64_debug_read_ram(&rt->machine, 0x0000);
    sid0_now = rt->machine.sid.regs[0];
    drive_pc_now = rt->machine.drive8.cpu.cpu.pc;

    /* Inspector off -> UNAVAILABLE. */
    {
        uint64_t tok = runtime_client_alloc_request_token(client);
        expect_true(
            "tm off", runtime_client_inspector_set_enabled(client, false, tok));
        drain(client);
        tok = runtime_client_alloc_request_token(client);
        expect_true("enter off", runtime_client_inspector_enter(client, tok));
        expect_true(
            "enter off event",
            wait_inspector_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_OTHER));
        expect_true(
            "enter unavailable",
            ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_UNAVAILABLE);
        expect_true("still live", !runtime_inspector_in_inspect(rt));
        tok = runtime_client_alloc_request_token(client);
        expect_true(
            "tm on", runtime_client_inspector_set_enabled(client, true, tok));
        drain(client);
        (void)runtime_inspector_checkpoint_take(rt);
    }

    /* Empty tape: inspector_memory_mb=0. */
    {
        runtime_config empty_cfg;
        runtime *empty_rt;
        runtime_client *empty_client;
        uint64_t tok;
        runtime_event empty_ev;

        fill_base_config(&empty_cfg);
        empty_cfg.inspector_memory_mb = 0u;
        empty_rt = start_runtime(&empty_cfg, &empty_client);
        drain(empty_client);
        tok = runtime_client_alloc_request_token(empty_client);
        expect_true(
            "enter empty", runtime_client_inspector_enter(empty_client, tok));
        expect_true(
            "enter empty event",
            wait_inspector_mode(
                empty_client, tok, &empty_ev, NULL, RUNTIME_STATE_CHANGED_OTHER));
        expect_true(
            "enter empty status",
            empty_ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_EMPTY);
        expect_true("empty not inspecting", !runtime_inspector_in_inspect(empty_rt));
        stop_runtime(empty_rt, empty_client);
    }

    /* HST1 off is not a gate. */
    {
        uint64_t tok = runtime_client_alloc_request_token(client);
        expect_true("hist off", runtime_client_history_record(client, false, tok));
        drain(client);
        (void)runtime_inspector_checkpoint_take(rt);
        tok = runtime_client_alloc_request_token(client);
        expect_true("enter hst1 off", runtime_client_inspector_enter(client, tok));
        expect_true(
            "enter hst1 off event",
            wait_inspector_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_INSPECTOR_ENTER));
        expect_true(
            "enter hst1 off ok",
            ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_OK);
        expect_true("hst1 off inspecting", runtime_inspector_in_inspect(rt));
        tok = runtime_client_alloc_request_token(client);
        expect_true("leave hst1 off", runtime_client_inspector_leave(client, tok));
        expect_true(
            "leave hst1 off event",
            wait_inspector_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_INSPECTOR_LEAVE));
        tok = runtime_client_alloc_request_token(client);
        expect_true("hist on", runtime_client_history_record(client, true, tok));
        drain(client);
        (void)runtime_inspector_checkpoint_take(rt);
    }

    /* Frame ring off is not a gate. */
    {
        uint64_t tok;
        runtime_frame_ring_set_recording(&rt->frame_ring, false);
        tok = runtime_client_alloc_request_token(client);
        expect_true("enter film off", runtime_client_inspector_enter(client, tok));
        expect_true(
            "enter film off event",
            wait_inspector_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_INSPECTOR_ENTER));
        expect_true(
            "enter film off ok",
            ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_OK);
        tok = runtime_client_alloc_request_token(client);
        expect_true("leave film off", runtime_client_inspector_leave(client, tok));
        expect_true(
            "leave film off event",
            wait_inspector_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_INSPECTOR_LEAVE));
        runtime_frame_ring_set_recording(&rt->frame_ring, true);
        (void)runtime_inspector_checkpoint_take(rt);
    }

    pc_now = rt->machine.cpu.cpu.pc;
    cycles_now = rt->machine.clock.cycle;
    ram_now = c64_debug_read_ram(&rt->machine, 0x0000);
    sid0_now = rt->machine.sid.regs[0];
    drive_pc_now = rt->machine.drive8.cpu.cpu.pc;

    token = runtime_client_alloc_request_token(client);
    expect_true("enter", runtime_client_inspector_enter(client, token));
    expect_true(
        "enter event",
        wait_inspector_mode(
            client, token, &ev, &saw_reason, RUNTIME_STATE_CHANGED_INSPECTOR_ENTER));
    expect_true("enter ok", ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_OK);
    expect_true(
        "mode inspect",
        ev.data.inspector_mode.mode == RUNTIME_INSPECTOR_MODE_INSPECT);
    expect_true("in inspect", runtime_inspector_in_inspect(rt));
    expect_true("enter inform", saw_reason);
    expect_true("cpu after enter", wait_cpu(client, &cpu));
    expect_true("sealed", rt->machine.replay_sealed);
    expect_true("paused inspecting", rt->exec_state != RUNTIME_EXEC_RUNNING);
    expect_true("enter at live", rt->machine.clock.cycle == cycles_now);
    expect_true("enter pc live", rt->machine.cpu.cpu.pc == pc_now);

    expect_true(
        "poke",
        runtime_client_write_memory_byte(
            client, 0x0300, 0xA9, RUNTIME_MEMORY_MODE_RAM));
    expect_true(
        "poke error", wait_error_code(client, RUNTIME_ERROR_READ_ONLY_INSPECTOR));
    expect_true("still inspecting after poke", runtime_inspector_in_inspect(rt));
    expect_true("set-pc", runtime_client_set_pc(client, 0x0300));
    expect_true(
        "set-pc error", wait_error_code(client, RUNTIME_ERROR_READ_ONLY_INSPECTOR));

    /* Land oldest CP. */
    {
        uint64_t old = 0u;
        uint64_t live = 0u;
        uint64_t n = 0u;
        uint64_t hst1_before = 0u;

        runtime_inspector_timeline_bounds(rt, &old, &live, &n);
        expect_true("timeline", n >= 1u);
        if (rt->history != NULL) {
            runtime_history_get_status(rt->history, &hist);
            hst1_before = hist.record_count;
        }
        token = runtime_client_alloc_request_token(client);
        expect_true("land old", runtime_client_inspector_land(client, old, token));
        {
            clock_t t0 = clock();
            while (rt->machine.clock.cycle != old &&
                   (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
            }
        }
        expect_true("cpu after land", wait_cpu(client, &cpu));
        expect_true("landed old cycle", rt->machine.clock.cycle == old);
        expect_true("still sealed after land", rt->machine.replay_sealed);
        if (rt->history != NULL) {
            runtime_history_get_status(rt->history, &hist);
            expect_true("hst1 unchanged", hist.record_count == hst1_before);
        }

        /* Exact land: checkpoint <= mid then reexecute to mid. */
        {
            uint64_t mid = old + (live - old) / 2u;
            if (mid <= old) {
                mid = old + 1u;
            }
            if (mid >= live) {
                mid = live > old + 1u ? live - 1u : old;
            }
            token = runtime_client_alloc_request_token(client);
            expect_true(
                "land_to_cycle",
                runtime_client_inspector_land_to_cycle(client, mid, token));
            {
                clock_t t0 = clock();
                while (rt->machine.clock.cycle != mid &&
                       (double)(clock() - t0) / CLOCKS_PER_SEC < 3.0) {
                }
            }
            expect_true("cpu after exact", wait_cpu(client, &cpu));
            if (rt->machine.clock.cycle != mid) {
                fprintf(
                    stderr,
                    "land_to_cycle got=%llu want=%llu old=%llu live=%llu\n",
                    (unsigned long long)rt->machine.clock.cycle,
                    (unsigned long long)mid,
                    (unsigned long long)old,
                    (unsigned long long)live);
            }
            expect_true("exact cycle", rt->machine.clock.cycle == mid);
            expect_true("sealed after exact", rt->machine.replay_sealed);
        }

        {
            uint64_t c0 = rt->machine.clock.cycle;
            clock_t t0;
            expect_true("step tt", runtime_client_step_instruction(client));
            t0 = clock();
            while (rt->machine.clock.cycle == c0 &&
                   !runtime_inspector_at_live(rt) &&
                   (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
            }
            expect_true("cpu after step", wait_cpu(client, &cpu));
            expect_true(
                "step advanced or live",
                rt->machine.clock.cycle > c0 || runtime_inspector_at_live(rt));
            expect_true("still inspecting after step", runtime_inspector_in_inspect(rt));
            expect_true("still sealed after step", rt->machine.replay_sealed);
        }

        /* Window edge: land before oldest. */
        if (old > 0u) {
            uint64_t before = rt->machine.clock.cycle;
            uint16_t pc_before = rt->machine.cpu.cpu.pc;
            token = runtime_client_alloc_request_token(client);
            expect_true(
                "land outside",
                runtime_client_inspector_land(client, old - 1u, token));
            expect_true(
                "outside error",
                poll_event(client, RUNTIME_EVENT_ERROR, 0u, &ev, 0));
            expect_true("unchanged cycle", rt->machine.clock.cycle == before);
            expect_true("unchanged pc", rt->machine.cpu.cpu.pc == pc_before);
            expect_true("still inspecting outside", runtime_inspector_in_inspect(rt));
        }

        token = runtime_client_alloc_request_token(client);
        expect_true("land live", runtime_client_inspector_land(client, live, token));
        {
            clock_t t0 = clock();
            while (rt->machine.clock.cycle != cycles_now &&
                   (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
            }
        }
        expect_true("cpu after land live", wait_cpu(client, &cpu));
        expect_true("live cycle", rt->machine.clock.cycle == cycles_now);
        expect_true("live pc", rt->machine.cpu.cpu.pc == pc_now);

        expect_true("run at live", runtime_client_run(client));
        {
            clock_t t0 = clock();
            while ((double)(clock() - t0) / CLOCKS_PER_SEC < 0.05) {
            }
        }
        drain(client);
        expect_true("still inspecting at live run", runtime_inspector_in_inspect(rt));
        expect_true("still paused at live", rt->exec_state != RUNTIME_EXEC_RUNNING);
        expect_true("live cycle after run", rt->machine.clock.cycle == cycles_now);
    }

    /* F12 from a past land stops at live, stays in Inspect. */
    {
        uint64_t old = 0u;
        uint64_t live = 0u;
        uint64_t n = 0u;
        clock_t t0;

        runtime_inspector_timeline_bounds(rt, &old, &live, &n);
        token = runtime_client_alloc_request_token(client);
        expect_true("land for f12", runtime_client_inspector_land(client, old, token));
        t0 = clock();
        while (rt->machine.clock.cycle != old &&
               (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
        }
        expect_true("run f12", runtime_client_run(client));
        t0 = clock();
        while ((!runtime_inspector_at_live(rt) ||
                rt->exec_state == RUNTIME_EXEC_RUNNING) &&
               (double)(clock() - t0) / CLOCKS_PER_SEC < 5.0) {
        }
        expect_true("f12 at live", runtime_inspector_at_live(rt));
        expect_true("f12 still inspect", runtime_inspector_in_inspect(rt));
        expect_true("f12 paused", rt->exec_state != RUNTIME_EXEC_RUNNING);
        expect_true("f12 live cycle", rt->machine.clock.cycle == cycles_now);
    }

    /* Land far back, then Leave: CRT must present NOW film (not the landed past). */
    {
        c64_frame film_now;
        c64_frame presented;
        uint64_t land_old = 0u;
        uint64_t live = 0u;
        uint64_t n = 0u;
        int got_frame = 0;
        clock_t t0;

        runtime_inspector_timeline_bounds(rt, &land_old, &live, &n);
        expect_true(
            "film at now before leave",
            runtime_frame_ring_copy_by_cycle(&rt->frame_ring, cycles_now, &film_now));
        token = runtime_client_alloc_request_token(client);
        expect_true(
            "land past before leave",
            runtime_client_inspector_land(client, land_old, token));
        t0 = clock();
        while (rt->machine.clock.cycle != land_old &&
               (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
        }
        expect_true("landed past", rt->machine.clock.cycle == land_old);

        drain(client);
        token = runtime_client_alloc_request_token(client);
        expect_true("exit", runtime_client_inspector_leave(client, token));
        expect_true(
            "exit event",
            wait_inspector_mode(
                client, token, &ev, &saw_reason, RUNTIME_STATE_CHANGED_INSPECTOR_LEAVE));
        expect_true("exit ok", ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_OK);
        expect_true("exit inform", saw_reason);
        expect_true("mode live", ev.data.inspector_mode.mode == RUNTIME_INSPECTOR_MODE_LIVE);
        expect_true("not inspecting", !runtime_inspector_in_inspect(rt));
        expect_true("unsealed", !rt->machine.replay_sealed);
        expect_true("still paused", rt->exec_state != RUNTIME_EXEC_RUNNING);
        expect_true("pc restored", rt->machine.cpu.cpu.pc == pc_now);
        expect_true("cycles restored", rt->machine.clock.cycle == cycles_now);
        expect_true("ram restored", c64_debug_read_ram(&rt->machine, 0x0000) == ram_now);
        expect_true("sid restored", rt->machine.sid.regs[0] == sid0_now);
        expect_true("1541 pc restored", rt->machine.drive8.cpu.cpu.pc == drive_pc_now);
        expect_true("cpu after exit", wait_cpu(client, &cpu));
        expect_true("cpu pc now", cpu.pc == pc_now);

        t0 = clock();
        while ((double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
            if (runtime_client_poll_frame(client, &presented)) {
                got_frame = 1;
                break;
            }
        }
        expect_true("leave published frame", got_frame);
        /* Prefer film at NOW: cycle must be near enter NOW, not the landed past. */
        expect_true(
            "leave frame near now",
            presented.machine_cycle >= land_old + (cycles_now - land_old) / 2u);
        expect_true(
            "leave frame not past land",
            presented.machine_cycle != land_old || land_old == cycles_now);
    }

    /* Wipe CPs mid-session after leave; re-enter path covered above. */
    {
        /* Re-enter so wipe/leave-fail coverage below still runs. */
        token = runtime_client_alloc_request_token(client);
        expect_true(
            "re-enter after leave crt check",
            runtime_client_inspector_enter(client, token));
        expect_true(
            "re-enter event",
            wait_inspector_mode(
                client, token, &ev, NULL, RUNTIME_STATE_CHANGED_INSPECTOR_ENTER));
        expect_true("re-inspecting", runtime_inspector_in_inspect(rt));
    }

    runtime_inspector_on_history_invalidate(rt);
    expect_true("cps cleared", runtime_inspector_checkpoint_count(rt) == 0u);
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "land after wipe",
        runtime_client_inspector_land(client, 0u, token));
    drain(client);
    expect_true("still inspecting after fail", runtime_inspector_in_inspect(rt));

    token = runtime_client_alloc_request_token(client);
    expect_true("exit after wipe", runtime_client_inspector_leave(client, token));
    expect_true(
        "exit after wipe event",
        wait_inspector_mode(
            client, token, &ev, &saw_reason, RUNTIME_STATE_CHANGED_INSPECTOR_LEAVE));
    expect_true("exit after wipe ok", ev.data.inspector_mode.status == RUNTIME_INSPECTOR_ENTER_OK);
    expect_true("exit after wipe live", !runtime_inspector_in_inspect(rt));

    drain(client);
    expect_true(
        "live poke",
        runtime_client_write_memory_byte(
            client, 0x0300, 0xEA, RUNTIME_MEMORY_MODE_RAM));
    drain(client);

    /* Leave with no session is a no-op that still publishes. */
    token = runtime_client_alloc_request_token(client);
    expect_true("leave noop", runtime_client_inspector_leave(client, token));
    expect_true(
        "leave noop event",
        wait_inspector_mode(
            client, token, &ev, NULL, RUNTIME_STATE_CHANGED_INSPECTOR_LEAVE));
    expect_true("leave noop live", !runtime_inspector_in_inspect(rt));

    stop_runtime(rt, client);
    remove("runtime_insp_mode_64c.bin");
    remove("runtime_insp_mode_character.bin");
    return 0;
}
