/* I1: checkpoint ring, sealed materialize to scratch, media truncate. */
#include "c64.h"
#include "c64_bus.h"
#include "c64_snapshot.h"
#include "c1541_media.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_history.h"
#include "runtime_inspector.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_insp_replay_64c.bin", "wb");
    FILE *character = fopen("runtime_insp_replay_character.bin", "wb");
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

static bool poll_event(
    runtime_client *client,
    runtime_event_type type,
    uint64_t token,
    runtime_event *out_event) {
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 5.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == type &&
                (token == 0u || event.request_token == token)) {
                if (out_event != NULL) {
                    *out_event = event;
                }
                return true;
            }
        }
    }
    return false;
}

static void drain_commands(runtime_client *client) {
    runtime_event event;

    if (!runtime_client_ping(client) ||
        !poll_event(client, RUNTIME_EVENT_PONG, 0u, &event)) {
        fail("ping timeout");
    }
}

static void step_instructions(runtime_client *client, size_t count) {
    runtime_event event;
    size_t i;

    for (i = 0u; i < count; ++i) {
        if (!runtime_client_step_instruction(client) ||
            !poll_event(client, RUNTIME_EVENT_STEP_COMPLETE, 0u, &event)) {
            fail("step instruction timeout");
        }
    }
}

static runtime *start_runtime(
    runtime_config *config,
    runtime_client **out_client) {
    runtime *rt = runtime_create(config);
    runtime_event event;

    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime start failed");
    }
    *out_client = runtime_get_client(rt);
    if (!poll_event(*out_client, RUNTIME_EVENT_STARTED, 0u, &event) ||
        !poll_event(*out_client, RUNTIME_EVENT_RESET_COMPLETE, 0u, &event)) {
        fail("runtime startup timeout");
    }
    return rt;
}

static void stop_runtime(runtime *rt, runtime_client *client) {
    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
}

static void fill_base_config(runtime_config *config) {
    memset(config, 0, sizeof(*config));
    config->system_rom_path = "runtime_insp_replay_64c.bin";
    config->char_rom_path = "runtime_insp_replay_character.bin";
    config->history_memory_mb = 16u;
    config->history_memory_mb_configured = true;
    config->frame_ring_memory_mb = 8u;
    config->frame_ring_memory_mb_configured = true;
    config->vic_ring_memory_mb = 0u;
    config->vic_ring_memory_mb_configured = true;
    config->inspector = true;
    config->inspector_memory_mb = 16u;
    config->inspector_memory_mb_configured = true;
    config->inspector_off_on_max = true;
}

static void scratch_cleanup(c64_t *m) {
    if (m == NULL) {
        return;
    }
    c1541_destroy(&m->drive8);
    c1541_destroy(&m->drive9);
    c64_unmount_all_drives(m);
}

static c64_t *scratch_alloc(void) {
    c64_t *dst = (c64_t *)calloc(1u, sizeof(*dst));
    if (dst == NULL) {
        fail("scratch alloc");
    }
    c64_init(dst);
    return dst;
}

int main(void) {
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_history_status st_before;
    runtime_history_status st_after;
    runtime_frame_ring_info fi_before;
    runtime_frame_ring_info fi_after;
    runtime_inspector_window window;
    c64_t *scratch;
    c64_t *scratch2;
    uint64_t t0;
    uint64_t t1;
    uint16_t pc_live;
    uint8_t a_live;
    uint8_t ram_live;
    size_t blob_no_disk;
    uint64_t token;

    write_test_roms();
    if (!runtime_init()) {
        fail("runtime_init failed");
    }

    fill_base_config(&config);
    rt = start_runtime(&config, &client);
    if (!runtime_inspector_enabled(rt) ||
        !runtime_inspector_recorder_is_recording(rt)) {
        fail("inspector recorder not on at startup");
    }
    if (runtime_inspector_checkpoint_count(rt) < 1u) {
        fail("no startup checkpoint");
    }
    blob_no_disk = c64_snapshot_size(&rt->machine);
    if (blob_no_disk == 0u) {
        fail("snapshot size is 0");
    }
    printf(
        "i1 snapshot_bytes_no_disk=%zu cadence=%u slots_at_128mib=%zu\n",
        blob_no_disk,
        (unsigned)runtime_inspector_cadence_cycles(rt),
        blob_no_disk == 0u ? 0u : ((size_t)128u * 1024u * 1024u) / blob_no_disk);

    (void)runtime_inspector_checkpoint_take(rt);
    t0 = rt->machine.clock.cycle;
    pc_live = rt->machine.cpu.cpu.pc;
    a_live = rt->machine.cpu.cpu.A;
    ram_live = c64_debug_read_ram(&rt->machine, 0x0400u);

    scratch = scratch_alloc();
    runtime_history_get_status(rt->history, &st_before);
    runtime_client_get_frame_ring_info(client, &fi_before);
    if (!runtime_inspector_materialize(rt, t0, scratch)) {
        fail("materialize at CP cycle");
    }
    runtime_history_get_status(rt->history, &st_after);
    runtime_client_get_frame_ring_info(client, &fi_after);
    if (st_before.record_count != st_after.record_count) {
        fail("seal: HST1 record count changed");
    }
    if (fi_before.count != fi_after.count) {
        fail("seal: frame ring count changed");
    }
    if (scratch->cpu.cpu.pc != pc_live || scratch->cpu.cpu.A != a_live ||
        c64_debug_read_ram(scratch, 0x0400u) != ram_live) {
        fail("CP round-trip mismatch");
    }
    scratch_cleanup(scratch);
    free(scratch);

    step_instructions(client, 40u);
    t1 = rt->machine.clock.cycle;
    pc_live = rt->machine.cpu.cpu.pc;
    a_live = rt->machine.cpu.cpu.A;
    ram_live = c64_debug_read_ram(&rt->machine, 0x0400u);
    step_instructions(client, 40u);

    scratch = scratch_alloc();
    if (!runtime_inspector_materialize(rt, t1, scratch)) {
        fail("materialize mid-window");
    }
    if (scratch->clock.cycle < t0 || scratch->cpu.cpu.pc != pc_live ||
        scratch->cpu.cpu.A != a_live ||
        c64_debug_read_ram(scratch, 0x0400u) != ram_live) {
        fail("mid-window golden mismatch");
    }

    scratch2 = scratch_alloc();
    if (!runtime_inspector_materialize(rt, t1, scratch2)) {
        fail("materialize twice");
    }
    if (scratch->cpu.cpu.pc != scratch2->cpu.cpu.pc ||
        scratch->cpu.cpu.A != scratch2->cpu.cpu.A ||
        scratch->cpu.cpu.X != scratch2->cpu.cpu.X ||
        scratch->cpu.cpu.Y != scratch2->cpu.cpu.Y ||
        c64_debug_read_ram(scratch, 0x0400u) !=
            c64_debug_read_ram(scratch2, 0x0400u)) {
        fail("determinism mismatch");
    }
    scratch_cleanup(scratch);
    scratch_cleanup(scratch2);
    free(scratch);
    free(scratch2);

    {
        runtime_breakpoint_definition def;
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = 0xd020u;
        def.access = RUNTIME_BREAKPOINT_ACCESS_WRITE;
        def.mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        if (!runtime_client_create_breakpoint(client, &def)) {
            fail("create watchpoint");
        }
        drain_commands(client);
        scratch = scratch_alloc();
        if (!runtime_inspector_materialize(rt, t1, scratch)) {
            fail("materialize with watchpoint");
        }
        if (rt->breakpoint_hit_pending) {
            fail("seal: watchpoint fired during materialize");
        }
        scratch_cleanup(scratch);
        free(scratch);
        runtime_client_clear_all_breakpoints(client);
        drain_commands(client);
    }

    if (!runtime_client_keyboard_key(client, C64_KEY_A, true)) {
        fail("key down");
    }
    drain_commands(client);
    step_instructions(client, 8u);
    {
        uint64_t now = rt->machine.clock.cycle;
        scratch = scratch_alloc();
        if (!runtime_inspector_materialize(rt, now, scratch)) {
            fail("materialize after key");
        }
        scratch_cleanup(scratch);
        free(scratch);
    }

    {
        uint64_t trunc = runtime_inspector_media_truncations(rt);
        uint64_t cps = runtime_inspector_checkpoint_count(rt);
        if (!runtime_client_save_state(client, "insp_replay_housekeeping.c64state")) {
            fail("save-state");
        }
        if (!poll_event(client, RUNTIME_EVENT_SAVE_STATE_COMPLETE, 0u, &event)) {
            fail("save-state complete");
        }
        (void)c1541_media_sync_dirty(&rt->machine.drive8);
        if (runtime_inspector_media_truncations(rt) != trunc) {
            fail("housekeeping truncated Inspector");
        }
        if (runtime_inspector_checkpoint_count(rt) != cps) {
            fail("housekeeping wiped checkpoints");
        }
        remove("insp_replay_housekeeping.c64state");
    }

    {
        uint64_t trunc_before = runtime_inspector_media_truncations(rt);
        runtime_history_record rec;

        (void)runtime_inspector_checkpoint_take(rt);
        runtime_inspector_on_media_event(
            rt, rt->machine.clock.cycle, 8);
        if (runtime_inspector_media_truncations(rt) <= trunc_before) {
            fail("guest write did not truncate");
        }
        if (!runtime_history_first(rt->history, &rec) ||
            rec.kind != RUNTIME_HISTORY_RECORD_MARKER ||
            rec.marker_kind != RUNTIME_HISTORY_MARKER_MEDIA_CHANGED ||
            rec.marker_arg0 != RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE) {
            fail("MEDIA_CHANGED marker not oldest");
        }
        runtime_inspector_window_info(rt, &window);
        if (!window.valid ||
            window.start_kind != RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE) {
            fail("window start kind after cut");
        }
        if (runtime_inspector_checkpoint_count(rt) < 1u) {
            fail("recording did not continue after cut");
        }
    }

    /* I4: max/warp wipe Record; leave restores into an empty window. */
    {
        uint64_t oldest = 0u;
        uint64_t live = 0u;
        uint64_t n = 0u;
        runtime_history_status hist_before;
        runtime_history_status hist_after;
        runtime_frame_ring_info film;

        if (!runtime_inspector_enabled(rt) ||
            runtime_inspector_checkpoint_count(rt) < 1u) {
            fail("I4 setup: Record not on");
        }
        runtime_history_get_status(rt->history, &hist_before);
        if (!hist_before.recording) {
            fail("I4 setup: HST1 not recording");
        }

        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_NORMAL)) {
            fail("set turbo 1");
        }
        drain_commands(client);
        if (!runtime_inspector_enabled(rt) ||
            runtime_inspector_checkpoint_count(rt) < 1u) {
            fail("turbo 1 wiped Inspector");
        }

        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_MAX)) {
            fail("set max");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt)) {
            fail("Record still on in max");
        }
        if (runtime_inspector_recorder_is_recording(rt)) {
            fail("recorder still armed in max");
        }
        if (runtime_inspector_checkpoint_count(rt) != 0u) {
            fail("tape not wiped in max");
        }
        runtime_inspector_timeline_bounds(rt, &oldest, &live, &n);
        if (n != 0u) {
            fail("timeline not empty in max");
        }
        runtime_history_get_status(rt->history, &hist_after);
        if (!hist_after.recording) {
            fail("HST1 paused on enter max");
        }
        runtime_client_get_frame_ring_info(client, &film);
        if (film.count != 0u) {
            fail("film not wiped with Record");
        }

        step_instructions(client, 20u);
        runtime_history_get_status(rt->history, &hist_after);
        if (hist_after.record_count <= hist_before.record_count) {
            fail("HST1 did not keep recording in max");
        }
        if (runtime_inspector_checkpoint_count(rt) != 0u) {
            fail("Inspector recorded in max");
        }

        /* Record click in max does not arm; remembered for leave. */
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_inspector_set_enabled(client, true, token)) {
            fail("Record click in max");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt) ||
            runtime_inspector_recorder_is_recording(rt) ||
            runtime_inspector_checkpoint_count(rt) != 0u) {
            fail("Record click in max armed a tape");
        }

        if (!runtime_client_set_turbo_multiplier(
                client, RUNTIME_TURBO_MODE_NORMAL)) {
            fail("leave max");
        }
        drain_commands(client);
        if (!runtime_inspector_enabled(rt)) {
            fail("Record not restored on leave max");
        }
        if (runtime_inspector_checkpoint_count(rt) < 1u) {
            fail("fresh tape missing after leave max");
        }
        runtime_inspector_timeline_bounds(rt, &oldest, &live, &n);
        if (n < 1u || oldest != live) {
            fail("leave max window not empty at live");
        }

        /* Record-off stays off across a max round-trip. */
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_inspector_set_enabled(client, false, token)) {
            fail("Record off before max");
        }
        drain_commands(client);
        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_MAX)) {
            fail("set max again");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt)) {
            fail("Record on in max after off");
        }
        if (!runtime_client_set_turbo_multiplier(
                client, RUNTIME_TURBO_MODE_NORMAL)) {
            fail("leave max again");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt)) {
            fail("Record-off did not stay off");
        }

        /* Warp (turbo 3) is the same as max. */
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_inspector_set_enabled(client, true, token)) {
            fail("Record on for warp");
        }
        drain_commands(client);
        (void)runtime_inspector_checkpoint_take(rt);
        if (runtime_inspector_checkpoint_count(rt) < 1u) {
            fail("no CP before warp");
        }
        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_WARP)) {
            fail("set warp");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt) ||
            runtime_inspector_checkpoint_count(rt) != 0u) {
            fail("warp did not wipe Record");
        }
        /* max <-> warp must not forget the saved Record. */
        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_MAX)) {
            fail("warp to max");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt) ||
            runtime_inspector_checkpoint_count(rt) != 0u) {
            fail("max after warp re-armed Record");
        }
        if (!runtime_client_set_turbo_multiplier(
                client, RUNTIME_TURBO_MODE_NORMAL)) {
            fail("leave warp/max");
        }
        drain_commands(client);
        if (!runtime_inspector_enabled(rt) ||
            runtime_inspector_checkpoint_count(rt) < 1u) {
            fail("Record not restored after warp");
        }

        /* Disable while in max clears the restore memory. */
        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_MAX)) {
            fail("set max for disable");
        }
        drain_commands(client);
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_inspector_set_enabled(client, false, token)) {
            fail("Record disable in max");
        }
        drain_commands(client);
        if (!runtime_client_set_turbo_multiplier(
                client, RUNTIME_TURBO_MODE_NORMAL)) {
            fail("leave max after disable");
        }
        drain_commands(client);
        if (runtime_inspector_enabled(rt)) {
            fail("disable in max was still restored");
        }

        /* Inspecting + enter max: leave Inspect (restore NOW) first, then wipe. */
        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_inspector_set_enabled(client, true, token)) {
            fail("Record on for inspect+max");
        }
        drain_commands(client);
        (void)runtime_inspector_checkpoint_take(rt);
        step_instructions(client, 40u);
        (void)runtime_inspector_checkpoint_take(rt);
        {
            uint64_t now_cycle;
            uint16_t now_pc;
            uint64_t old = 0u;
            uint64_t live_end = 0u;
            uint64_t count = 0u;
            runtime_event ev;

            now_cycle = rt->machine.clock.cycle;
            now_pc = rt->machine.cpu.cpu.pc;
            runtime_inspector_timeline_bounds(rt, &old, &live_end, &count);
            if (count < 1u) {
                fail("inspect+max: no timeline");
            }
            token = runtime_client_alloc_request_token(client);
            if (!runtime_client_inspector_enter(client, token)) {
                fail("enter inspect");
            }
            if (!poll_event(client, RUNTIME_EVENT_INSPECTOR_MODE, token, &ev)) {
                fail("enter inspect timeout");
            }
            if (!runtime_inspector_in_inspect(rt)) {
                fail("not inspecting");
            }
            if (old != now_cycle) {
                token = runtime_client_alloc_request_token(client);
                if (!runtime_client_inspector_land(client, old, token)) {
                    fail("land oldest");
                }
                {
                    clock_t t0 = clock();
                    while (rt->machine.clock.cycle != old &&
                           (double)(clock() - t0) / CLOCKS_PER_SEC < 2.0) {
                    }
                }
                if (rt->machine.clock.cycle != old) {
                    fail("land oldest did not move C64");
                }
            }
            if (!runtime_client_set_turbo_multiplier(
                    client, RUNTIME_TURBO_MODE_MAX)) {
                fail("max while inspecting");
            }
            drain_commands(client);
            if (runtime_inspector_in_inspect(rt)) {
                fail("still inspecting after max");
            }
            if (rt->machine.clock.cycle != now_cycle) {
                fail("NOW not restored before wipe");
            }
            if (rt->machine.cpu.cpu.pc != now_pc) {
                fail("NOW PC not restored before wipe");
            }
            if (runtime_inspector_enabled(rt) ||
                runtime_inspector_checkpoint_count(rt) != 0u) {
                fail("inspect+max did not wipe Record");
            }
            if (!runtime_client_set_turbo_multiplier(
                    client, RUNTIME_TURBO_MODE_NORMAL)) {
                fail("leave max after inspect");
            }
            drain_commands(client);
            if (!runtime_inspector_enabled(rt)) {
                fail("Record not restored after inspect+max");
            }
        }
    }

    {
        uint64_t cps;

        token = runtime_client_alloc_request_token(client);
        if (!runtime_client_inspector_set_enabled(client, false, token)) {
            fail("inspector off");
        }
        drain_commands(client);
        cps = runtime_inspector_checkpoint_count(rt);
        step_instructions(client, 30u);
        if (runtime_inspector_checkpoint_count(rt) != cps) {
            fail("off path grew Inspector buffers");
        }
        if (runtime_inspector_recorder_is_recording(rt)) {
            fail("recorder still on after off");
        }
    }

    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector_memory_mb = 1u;
    rt = start_runtime(&config, &client);
    {
        uint64_t i;
        uint64_t dropped0 = runtime_inspector_checkpoints_dropped(rt);
        for (i = 0u; i < 80u; ++i) {
            step_instructions(client, 8u);
            (void)runtime_inspector_checkpoint_take(rt);
        }
        runtime_inspector_window_info(rt, &window);
        if (!window.valid || window.checkpoint_count < 1u) {
            fail("budget drop left no window");
        }
        if (runtime_inspector_checkpoints_dropped(rt) <= dropped0 &&
            window.checkpoint_count > 4u) {
            /* 1 MiB should evict once snapshots accumulate. */
            size_t sz = c64_snapshot_size(&rt->machine);
            if (sz * 8u > 1024u * 1024u &&
                runtime_inspector_checkpoints_dropped(rt) == dropped0) {
                fail("budget drop did not advance oldest");
            }
        }
    }
    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector = false;
    rt = start_runtime(&config, &client);
    step_instructions(client, 20u);
    if (runtime_inspector_checkpoint_count(rt) != 0u) {
        fail("inspector off at boot allocated checkpoints");
    }
    stop_runtime(rt, client);

    fill_base_config(&config);
    config.inspector_off_on_max = false;
    rt = start_runtime(&config, &client);
    if (!runtime_inspector_enabled(rt) ||
        runtime_inspector_checkpoint_count(rt) < 1u) {
        fail("opt-out startup Record");
    }
    {
        uint64_t cps = runtime_inspector_checkpoint_count(rt);
        if (!runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MODE_MAX)) {
            fail("opt-out set max");
        }
        drain_commands(client);
        if (!runtime_inspector_enabled(rt)) {
            fail("opt-out wiped Record in max");
        }
        if (runtime_inspector_checkpoint_count(rt) < cps) {
            fail("opt-out dropped checkpoints");
        }
        step_instructions(client, 8u);
        (void)runtime_inspector_checkpoint_take(rt);
        if (runtime_inspector_checkpoint_count(rt) < 1u ||
            !runtime_inspector_recorder_is_recording(rt)) {
            fail("opt-out did not keep recording in max");
        }
        if (!runtime_client_set_turbo_multiplier(
                client, RUNTIME_TURBO_MODE_NORMAL)) {
            fail("opt-out leave max");
        }
        drain_commands(client);
        if (!runtime_inspector_enabled(rt)) {
            fail("opt-out Record off after leave");
        }
    }
    stop_runtime(rt, client);

    runtime_shutdown();
    remove("runtime_insp_replay_64c.bin");
    remove("runtime_insp_replay_character.bin");
    printf("test_runtime_inspector_replay: ok\n");
    return 0;
}
