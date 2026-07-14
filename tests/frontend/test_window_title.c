#include "window_title.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_title(
    const char *name,
    const char *expected,
    const char *video,
    uint32_t turbo,
    frontend_runtime_state state,
    runtime_stop_reason reason)
{
    char actual[96];

    frontend_format_window_title(
        actual, sizeof(actual), video, turbo, state, reason);
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n", name, expected, actual);
        exit(1);
    }
}

int main(void)
{
    expect_title("PAL running", "c64m - PAL - 1x - Running",
        "PAL", 1, FRONTEND_RUNTIME_STATE_RUNNING, RUNTIME_STOP_REASON_NONE);
    expect_title("NTSC turbo", "c64m - NTSC - 16x - Running",
        "NTSC", 16, FRONTEND_RUNTIME_STATE_RUNNING, RUNTIME_STOP_REASON_NONE);
    expect_title("maximum turbo", "c64m - PAL - MAX - Running",
        "PAL", 256, FRONTEND_RUNTIME_STATE_RUNNING, RUNTIME_STOP_REASON_NONE);
    expect_title("BRK pause", "c64m - PAL - 4x - Paused (BRK)",
        "PAL", 4, FRONTEND_RUNTIME_STATE_PAUSED, RUNTIME_STOP_REASON_BRK);
    expect_title("error", "c64m - NTSC - 2x - Error",
        "NTSC", 2, FRONTEND_RUNTIME_STATE_ERROR, RUNTIME_STOP_REASON_ERROR);
    return 0;
}
