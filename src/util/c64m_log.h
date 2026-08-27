#pragma once

#include "log.h"

#include <stdbool.h>

typedef enum c64m_log_level {
    C64M_LOG_LEVEL_ALL = 0,
    C64M_LOG_LEVEL_WARN,
    C64M_LOG_LEVEL_ERROR,
    C64M_LOG_LEVEL_NONE
} c64m_log_level;

/* Default policy is WARN. */
void c64m_log_init(void);
void c64m_log_apply(c64m_log_level level);

const char *c64m_log_level_name(c64m_log_level level);
bool c64m_log_level_from_string(const char *s, c64m_log_level *out_level);
