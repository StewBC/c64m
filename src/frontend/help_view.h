#pragma once

#include "nuklear_config.h"

#include <stdbool.h>

typedef struct frontend_help_state {
    bool open;
    bool paused_by_help;
    int section_index;
} frontend_help_state;

void help_view_init(frontend_help_state *state);
void help_view_open(frontend_help_state *state, bool paused_by_help);
void help_view_close(frontend_help_state *state);
bool help_view_is_open(const frontend_help_state *state);
bool help_view_paused_by_help(const frontend_help_state *state);
void help_view_render(struct nk_context *ctx, frontend_help_state *state, int width, int height);
