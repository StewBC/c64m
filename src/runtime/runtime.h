#pragma once

#include <stdbool.h>

typedef struct runtime runtime;
typedef struct runtime_client runtime_client;

typedef struct runtime_config {
    const char *basic_rom_path;
    const char *char_rom_path;
    const char *kernal_rom_path;
    const char *system_rom_path;
} runtime_config;

bool runtime_init();
void runtime_shutdown();

runtime *runtime_create(const runtime_config *config);
void runtime_destroy(runtime *rt);

bool runtime_start(runtime *rt);
void runtime_stop(runtime *rt);

runtime_client *runtime_get_client(runtime *rt);
