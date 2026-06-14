#pragma once

#include <stdbool.h>

typedef struct config config;

config *config_load(const char *path);
bool config_save(config *cfg, const char *path);

const char *config_get(config *cfg, const char *section, const char *key);
int config_get_int(config *cfg, const char *section, const char *key, int default_value);
bool config_get_bool(config *cfg, const char *section, const char *key, bool default_value);

void config_set(config *cfg, const char *section, const char *key, const char *value);
void config_set_int(config *cfg, const char *section, const char *key, int value);
void config_set_bool(config *cfg, const char *section, const char *key, bool value);

void config_destroy(config *cfg);
