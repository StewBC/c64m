#include "runtime_breakpoint_ini.h"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RUNTIME_BREAKPOINT_KEY_MAX = 64,
    RUNTIME_BREAKPOINT_VALUE_MAX = 256,
};

static bool runtime_ini_streq(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0') {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) {
            return false;
        }
        lhs++;
        rhs++;
    }

    return *lhs == '\0' && *rhs == '\0';
}

static char *runtime_ini_trim(char *value) {
    char *end;

    while (isspace((unsigned char)*value)) {
        value++;
    }

    end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return value;
}

static bool runtime_ini_parse_hex16(const char *text, uint16_t *out) {
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return false;
    }

    if (*text == '$') {
        text++;
    }

    value = strtoul(text, &end, 16);
    if (end == text || *end != '\0' || value > 0xfffful) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool runtime_ini_parse_u32(const char *text, uint32_t *out) {
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }

    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static void runtime_ini_warn(const char *key, const char *message) {
    fprintf(stderr, "warning: breakpoint `%s`: %s\n", key, message);
}

static void runtime_ini_strip_suffix(char *address) {
    char *dot = strrchr(address, '.');
    char *p;

    if (dot == NULL || dot[1] == '\0') {
        return;
    }

    for (p = dot + 1; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return;
        }
    }

    *dot = '\0';
}

static bool runtime_ini_parse_address_key(
    const char *key,
    uint16_t *start,
    uint16_t *end,
    bool *has_end) {
    char buffer[RUNTIME_BREAKPOINT_KEY_MAX];
    char *dash;

    if (strncmp(key, "break.", 6) != 0) {
        return false;
    }

    snprintf(buffer, sizeof(buffer), "%s", key + 6);
    runtime_ini_strip_suffix(buffer);
    dash = strchr(buffer, '-');
    if (dash != NULL) {
        *dash = '\0';
        if (!runtime_ini_parse_hex16(buffer, start) ||
            !runtime_ini_parse_hex16(dash + 1, end)) {
            return false;
        }
        *has_end = true;
        return true;
    }

    if (!runtime_ini_parse_hex16(buffer, start)) {
        return false;
    }

    *end = *start;
    *has_end = false;
    return true;
}

static bool runtime_ini_parse_breakpoint(
    const char *key,
    const char *value,
    runtime_breakpoint_definition *definition) {
    char buffer[RUNTIME_BREAKPOINT_VALUE_MAX];
    char *token;
    bool has_end = false;
    bool saw_access = false;
    bool saw_action = false;
    bool saw_reset = false;

    memset(definition, 0, sizeof(*definition));
    definition->enabled = 1;
    definition->mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;

    if (!runtime_ini_parse_address_key(
            key,
            &definition->start_address,
            &definition->end_address,
            &has_end)) {
        runtime_ini_warn(key, "invalid address key");
        return false;
    }
    definition->has_end_address = has_end ? 1u : 0u;

    snprintf(buffer, sizeof(buffer), "%s", value != NULL ? value : "");
    token = strtok(buffer, ",");
    while (token != NULL) {
        char *item = runtime_ini_trim(token);

        if (runtime_ini_streq(item, "execute")) {
            definition->access |= RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
            saw_access = true;
        } else if (runtime_ini_streq(item, "read")) {
            definition->access |= RUNTIME_BREAKPOINT_ACCESS_READ;
            saw_access = true;
        } else if (runtime_ini_streq(item, "write")) {
            definition->access |= RUNTIME_BREAKPOINT_ACCESS_WRITE;
            saw_access = true;
        } else if (runtime_ini_streq(item, "access")) {
            definition->access |= RUNTIME_BREAKPOINT_ACCESS_READ | RUNTIME_BREAKPOINT_ACCESS_WRITE;
            saw_access = true;
        } else if (runtime_ini_streq(item, "map")) {
            definition->mapping = RUNTIME_BREAKPOINT_MAPPING_MAP;
        } else if (runtime_ini_streq(item, "rom")) {
            definition->mapping = RUNTIME_BREAKPOINT_MAPPING_ROM;
        } else if (runtime_ini_streq(item, "ram")) {
            definition->mapping = RUNTIME_BREAKPOINT_MAPPING_RAM;
        } else if (runtime_ini_streq(item, "break")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_BREAK;
            saw_action = true;
        } else if (runtime_ini_streq(item, "fast")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_FAST;
            saw_action = true;
        } else if (runtime_ini_streq(item, "slow")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_SLOW;
            saw_action = true;
        } else if (runtime_ini_streq(item, "tron")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
            saw_action = true;
        } else if (runtime_ini_streq(item, "troff")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_TROFF;
            saw_action = true;
        } else if (runtime_ini_streq(item, "type")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
            saw_action = true;
        } else if (runtime_ini_streq(item, "swap")) {
            definition->actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
            saw_action = true;
        } else if (runtime_ini_streq(item, "enabled")) {
            definition->enabled = 1;
        } else if (runtime_ini_streq(item, "disabled")) {
            definition->enabled = 0;
        } else if (strncmp(item, "count=", 6) == 0) {
            if (!runtime_ini_parse_u32(item + 6, &definition->initial_count)) {
                runtime_ini_warn(key, "invalid count");
                return false;
            }
            definition->use_counter = 1;
        } else if (strncmp(item, "reset=", 6) == 0) {
            if (!runtime_ini_parse_u32(item + 6, &definition->reset_count)) {
                runtime_ini_warn(key, "invalid reset");
                return false;
            }
            saw_reset = true;
        } else if (*item != '\0') {
            runtime_ini_warn(key, "unknown keyword ignored");
        }

        token = strtok(NULL, ",");
    }

    if (definition->use_counter && !saw_reset) {
        definition->reset_count = definition->initial_count;
    }

    if (!saw_access || !saw_action) {
        runtime_ini_warn(key, "missing access or action");
        return false;
    }

    return true;
}

static bool runtime_add_loaded_breakpoint(runtime *rt, const runtime_breakpoint_definition *definition) {
    runtime_breakpoint *breakpoint;

    if (rt->breakpoint_count >= RUNTIME_BREAKPOINT_CAPACITY) {
        runtime_ini_warn("DEBUG", "breakpoint table is full");
        return false;
    }

    if (rt->next_breakpoint_id == 0) {
        rt->next_breakpoint_id = 1;
    }

    breakpoint = &rt->breakpoints[rt->breakpoint_count];
    breakpoint->id = rt->next_breakpoint_id++;
    breakpoint->enabled = definition->enabled != 0;
    breakpoint->start_address = definition->start_address;
    breakpoint->end_address = definition->has_end_address ?
        definition->end_address :
        definition->start_address;
    breakpoint->has_end_address = definition->has_end_address != 0;
    breakpoint->access_mask = definition->access;
    breakpoint->mapping = definition->mapping;
    breakpoint->action_mask = definition->actions;
    breakpoint->use_counter = definition->use_counter != 0;
    breakpoint->initial_count = definition->initial_count;
    breakpoint->reset_count = definition->reset_count;
    breakpoint->counter = definition->initial_count;
    breakpoint->current_hits = 0;
    rt->breakpoint_count++;
    return true;
}

bool runtime_load_breakpoints_from_ini(runtime *rt) {
    config *cfg;
    int count;
    int i;

    if (rt == NULL || !rt->use_ini || rt->ini_path == NULL) {
        return true;
    }

    cfg = config_load(rt->ini_path);
    if (cfg == NULL) {
        return true;
    }

    count = config_entry_count(cfg);
    for (i = 0; i < count; ++i) {
        const char *section;
        const char *key;
        const char *value;
        runtime_breakpoint_definition definition;

        if (!config_entry_at(cfg, i, &section, &key, &value)) {
            continue;
        }

        if (strcmp(section, "DEBUG") != 0 || strncmp(key, "break.", 6) != 0) {
            continue;
        }

        if (runtime_ini_parse_breakpoint(key, value, &definition)) {
            runtime_add_loaded_breakpoint(rt, &definition);
        }
    }

    config_destroy(cfg);
    return true;
}

static void runtime_format_breakpoint_key(
    const runtime_breakpoint *breakpoint,
    int suffix,
    char *out,
    size_t out_size) {
    char base[RUNTIME_BREAKPOINT_KEY_MAX];

    if (breakpoint->has_end_address) {
        snprintf(
            base,
            sizeof(base),
            "break.%04X-%04X",
            breakpoint->start_address,
            breakpoint->end_address);
    } else {
        snprintf(base, sizeof(base), "break.%04X", breakpoint->start_address);
    }

    if (suffix > 0) {
        snprintf(out, out_size, "%s.%d", base, suffix);
    } else {
        snprintf(out, out_size, "%s", base);
    }
}

static bool runtime_breakpoint_same_base(
    const runtime_breakpoint *lhs,
    const runtime_breakpoint *rhs) {
    return lhs->start_address == rhs->start_address &&
        lhs->end_address == rhs->end_address &&
        lhs->has_end_address == rhs->has_end_address;
}

static int runtime_breakpoint_suffix_for_index(runtime *rt, size_t index) {
    int suffix = 0;
    size_t i;

    for (i = 0; i < index; ++i) {
        if (runtime_breakpoint_same_base(&rt->breakpoints[i], &rt->breakpoints[index])) {
            suffix++;
        }
    }

    return suffix;
}

static void runtime_append_token(char *buffer, size_t size, const char *token) {
    if (buffer[0] != '\0') {
        strncat(buffer, ",", size - strlen(buffer) - 1u);
    }
    strncat(buffer, token, size - strlen(buffer) - 1u);
}

static void runtime_format_breakpoint_value(
    const runtime_breakpoint *breakpoint,
    char *out,
    size_t out_size) {
    char counter[32];

    out[0] = '\0';
    if (!breakpoint->enabled) {
        runtime_append_token(out, out_size, "disabled");
    }
    if ((breakpoint->access_mask & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0) {
        runtime_append_token(out, out_size, "execute");
    }
    if ((breakpoint->access_mask & RUNTIME_BREAKPOINT_ACCESS_READ) != 0) {
        runtime_append_token(out, out_size, "read");
    }
    if ((breakpoint->access_mask & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0) {
        runtime_append_token(out, out_size, "write");
    }

    if (breakpoint->mapping == RUNTIME_BREAKPOINT_MAPPING_ROM) {
        runtime_append_token(out, out_size, "rom");
    } else if (breakpoint->mapping == RUNTIME_BREAKPOINT_MAPPING_RAM) {
        runtime_append_token(out, out_size, "ram");
    } else {
        runtime_append_token(out, out_size, "map");
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
        runtime_append_token(out, out_size, "break");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_FAST) != 0) {
        runtime_append_token(out, out_size, "fast");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0) {
        runtime_append_token(out, out_size, "slow");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TRON) != 0) {
        runtime_append_token(out, out_size, "tron");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0) {
        runtime_append_token(out, out_size, "troff");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0) {
        runtime_append_token(out, out_size, "swap");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TYPE) != 0) {
        runtime_append_token(out, out_size, "type");
    }

    if (breakpoint->use_counter) {
        snprintf(counter, sizeof(counter), "count=%u", breakpoint->initial_count);
        runtime_append_token(out, out_size, counter);
        snprintf(counter, sizeof(counter), "reset=%u", breakpoint->reset_count);
        runtime_append_token(out, out_size, counter);
    }
}

bool runtime_save_breakpoints_to_ini(runtime *rt) {
    config *cfg;
    size_t i;
    bool ok;

    if (rt == NULL || !rt->save_ini || rt->ini_path == NULL) {
        return true;
    }

    cfg = config_load(rt->ini_path);
    if (cfg == NULL) {
        cfg = config_load(NULL);
    }
    if (cfg == NULL) {
        return false;
    }

    config_remove_prefix(cfg, "DEBUG", "break.");
    for (i = 0; i < rt->breakpoint_count; ++i) {
        char key[RUNTIME_BREAKPOINT_KEY_MAX];
        char value[RUNTIME_BREAKPOINT_VALUE_MAX];
        int suffix = runtime_breakpoint_suffix_for_index(rt, i);

        runtime_format_breakpoint_key(&rt->breakpoints[i], suffix, key, sizeof(key));
        runtime_format_breakpoint_value(&rt->breakpoints[i], value, sizeof(value));
        config_set(cfg, "DEBUG", key, value);
    }

    ok = config_save(cfg, rt->ini_path);
    config_destroy(cfg);
    return ok;
}
