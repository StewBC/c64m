/*
 * Phase 0.5b: request_token correlation for solicited CPU state.
 *
 * - Distinct tokens on two REQUEST_CPU_STATE commands produce matching
 *   CPU_STATE_RESPONSE events (no cross-wire).
 * - Token-0 (UI-style) CPU_STATE must not be treated as completing a
 *   non-zero token waiter (main deferred gate; verified here by token
 *   values on the wire between client and runtime).
 */

#include "c64_bus.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void write_test_roms(void) {
    FILE *system = fopen("runtime_token_64c.bin", "wb");
    FILE *character = fopen("runtime_token_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create runtime test ROMs");
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

/* Collect the next CPU_STATE_RESPONSE; die on ERROR. */
static int poll_cpu_state(runtime_client *client, runtime_event *event) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == RUNTIME_EVENT_CPU_STATE_RESPONSE) {
                return 1;
            }
        }
    }
    return 0;
}

/* Drain any pending events without matching. */
static void drain_events(runtime_client *client) {
    runtime_event event;
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 0.25) {
        if (!runtime_client_poll_event(client, &event)) {
            break;
        }
    }
}

int main(void) {
    runtime_config config = {
        .system_rom_path = "runtime_token_64c.bin",
        .char_rom_path = "runtime_token_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint64_t token_a;
    uint64_t token_b;
    uint64_t seen_a = 0;
    uint64_t seen_b = 0;

    write_test_roms();

    if (!runtime_init()) {
        fail("runtime_init failed");
    }

    rt = runtime_create(&config);
    if (rt == NULL) {
        fail("runtime_create failed");
    }
    if (!runtime_start(rt)) {
        fail("runtime_start failed");
    }
    client = runtime_get_client(rt);
    if (client == NULL) {
        fail("runtime_get_client failed");
    }

    {
        clock_t start = clock();
        int started = 0;
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fail(event.data.error.message);
                }
                if (event.type == RUNTIME_EVENT_STARTED) {
                    started = 1;
                }
            }
            if (started) {
                break;
            }
        }
        if (!started) {
            fail("timeout waiting for STARTED");
        }
    }

    if (!runtime_client_reset(client)) {
        fail("reset rejected");
    }
    {
        clock_t start = clock();
        int settled = 0;
        while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fail(event.data.error.message);
                }
                if (event.type == RUNTIME_EVENT_RESET_COMPLETE) {
                    settled = 1;
                }
            }
            if (settled) {
                break;
            }
        }
        if (!settled) {
            fail("timeout waiting for RESET_COMPLETE");
        }
    }
    drain_events(client);

    token_a = runtime_client_alloc_request_token(client);
    token_b = runtime_client_alloc_request_token(client);
    if (token_a == 0u || token_b == 0u || token_a == token_b) {
        fail("token allocator must return distinct non-zero tokens");
    }

    if (!runtime_client_request_cpu_state_token(client, token_a)) {
        fail("request_cpu_state_token A rejected");
    }
    if (!runtime_client_request_cpu_state_token(client, token_b)) {
        fail("request_cpu_state_token B rejected");
    }

    /* Ignore leftover token-0 CPU_STATE from reset/telemetry while collecting
       the two solicited completions. */
    {
        clock_t start = clock();
        while ((!seen_a || !seen_b) &&
               (double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
            if (!poll_cpu_state(client, &event)) {
                break;
            }
            if (event.request_token == token_a) {
                if (seen_a) {
                    fail("token A completed twice");
                }
                seen_a = 1;
            } else if (event.request_token == token_b) {
                if (seen_b) {
                    fail("token B completed twice");
                }
                seen_b = 1;
            } else if (event.request_token != 0u) {
                fprintf(
                    stderr,
                    "FAIL: unexpected token %llu (want %llu or %llu)\n",
                    (unsigned long long)event.request_token,
                    (unsigned long long)token_a,
                    (unsigned long long)token_b);
                exit(1);
            }
        }
    }
    if (!seen_a || !seen_b) {
        fail("did not observe both solicited tokens");
    }

    /* UI-style unsolicited request carries token 0. */
    if (!runtime_client_request_cpu_state(client)) {
        fail("token-0 request_cpu_state rejected");
    }
    if (!poll_cpu_state(client, &event)) {
        fail("timeout waiting for token-0 CPU_STATE");
    }
    if (event.request_token != 0u) {
        fprintf(
            stderr,
            "FAIL: UI-style CPU_STATE expected token 0, got %llu\n",
            (unsigned long long)event.request_token);
        exit(1);
    }

    /* Interleave: after a solicited token is outstanding, a token-0 event
       must not carry the solicited token (main deferred gate relies on this). */
    {
        uint64_t token_c = runtime_client_alloc_request_token(client);
        runtime_event token0_event;
        runtime_event token_c_event;
        int got_token0 = 0;
        int got_token_c = 0;

        if (!runtime_client_request_cpu_state_token(client, token_c)) {
            fail("request token C rejected");
        }
        if (!runtime_client_request_cpu_state(client)) {
            fail("interleave token-0 request rejected");
        }

        while (!got_token0 || !got_token_c) {
            if (!poll_cpu_state(client, &event)) {
                fail("timeout during interleave poll");
            }
            if (event.request_token == 0u) {
                token0_event = event;
                got_token0 = 1;
            } else if (event.request_token == token_c) {
                token_c_event = event;
                got_token_c = 1;
            } else {
                fprintf(
                    stderr,
                    "FAIL: interleave unexpected token %llu\n",
                    (unsigned long long)event.request_token);
                exit(1);
            }
        }
        (void)token0_event;
        (void)token_c_event;
        if (token_c == 0u) {
            fail("token C must be non-zero");
        }
    }

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();
    remove("runtime_token_64c.bin");
    remove("runtime_token_character.bin");
    printf("test_runtime_request_token: ok\n");
    return 0;
}
