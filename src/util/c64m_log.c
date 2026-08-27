#include "c64m_log.h"

#include <string.h>
#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

void c64m_log_init(void)
{
    c64m_log_apply(C64M_LOG_LEVEL_WARN);
}

void c64m_log_apply(c64m_log_level level)
{
    switch (level) {
    case C64M_LOG_LEVEL_ALL:
        log_set_quiet(false);
        log_set_level(LOG_TRACE);
        break;
    case C64M_LOG_LEVEL_ERROR:
        log_set_quiet(false);
        log_set_level(LOG_ERROR);
        break;
    case C64M_LOG_LEVEL_NONE:
        log_set_quiet(true);
        break;
    case C64M_LOG_LEVEL_WARN:
    default:
        log_set_quiet(false);
        log_set_level(LOG_WARN);
        break;
    }
}

const char *c64m_log_level_name(c64m_log_level level)
{
    switch (level) {
    case C64M_LOG_LEVEL_ALL:
        return "all";
    case C64M_LOG_LEVEL_ERROR:
        return "error";
    case C64M_LOG_LEVEL_NONE:
        return "none";
    case C64M_LOG_LEVEL_WARN:
    default:
        return "warn";
    }
}

bool c64m_log_level_from_string(const char *s, c64m_log_level *out_level)
{
    c64m_log_level level;

    if (s == NULL || out_level == NULL) {
        return false;
    }
    if (strcasecmp(s, "all") == 0) {
        level = C64M_LOG_LEVEL_ALL;
    } else if (strcasecmp(s, "warn") == 0) {
        level = C64M_LOG_LEVEL_WARN;
    } else if (strcasecmp(s, "error") == 0) {
        level = C64M_LOG_LEVEL_ERROR;
    } else if (strcasecmp(s, "none") == 0) {
        level = C64M_LOG_LEVEL_NONE;
    } else {
        return false;
    }
    *out_level = level;
    return true;
}
