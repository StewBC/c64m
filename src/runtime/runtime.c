#include "runtime.h"

#include "message_queue.h"
#include "mutex.h"
#include "runtime_breakpoint_ini.h"
#include "runtime_command.h"
#include "runtime_internal.h"
#include "thread.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum {
    RUNTIME_TURBO_DEFAULT_MULTIPLIER = 1,
    RUNTIME_TURBO_MAX_MULTIPLIER = 256,
};

static char *runtime_copy_string(const char *value) {
    char *copy;
    size_t length;

    if (!value) {
        return NULL;
    }

    length = strlen(value);
    copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

bool runtime_init() {
    return true;
}

void runtime_shutdown() {
}

void runtime_config_set_turbo_defaults(runtime_config *config) {
    if (config == NULL) {
        return;
    }

    config->turbo_speeds[0] = RUNTIME_TURBO_DEFAULT_MULTIPLIER;
    config->turbo_speed_count = 1;
    config->active_turbo_multiplier = RUNTIME_TURBO_DEFAULT_MULTIPLIER;
}

bool runtime_config_set_turbo_csv(runtime_config *config, const char *csv) {
    const char *cursor;
    uint8_t count = 0;

    if (config == NULL) {
        return false;
    }

    runtime_config_set_turbo_defaults(config);
    if (csv == NULL || csv[0] == '\0') {
        return true;
    }

    cursor = csv;
    while (*cursor != '\0') {
        char *end;
        unsigned long value;

        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        value = strtoul(cursor, &end, 10);
        if (end == cursor || value == 0 || value > RUNTIME_TURBO_MAX_MULTIPLIER) {
            runtime_config_set_turbo_defaults(config);
            return false;
        }
        while (isspace((unsigned char)*end)) {
            end++;
        }
        if (*end != '\0' && *end != ',') {
            runtime_config_set_turbo_defaults(config);
            return false;
        }
        if (count < (uint8_t)(sizeof(config->turbo_speeds) / sizeof(config->turbo_speeds[0]))) {
            config->turbo_speeds[count++] = (uint32_t)value;
        }
        cursor = *end == ',' ? end + 1 : end;
    }

    if (count == 0) {
        runtime_config_set_turbo_defaults(config);
        return false;
    }

    config->turbo_speed_count = count;
    config->active_turbo_multiplier = config->turbo_speeds[0];
    return true;
}

runtime *runtime_create(const runtime_config *config) {
    runtime *rt = calloc(1, sizeof(*rt));
    if (!rt) {
        return NULL;
    }

    rt->command_queue = message_queue_create(
        sizeof(runtime_command),
        RUNTIME_COMMAND_QUEUE_CAPACITY);
    rt->event_queue = message_queue_create(
        sizeof(runtime_event),
        RUNTIME_EVENT_QUEUE_CAPACITY);

    rt->frame_slot.mutex = mutex_create();
    rt->symbol_slot.mutex = mutex_create();

    if (!rt->command_queue || !rt->event_queue || !rt->frame_slot.mutex || !rt->symbol_slot.mutex) {
        runtime_destroy(rt);
        return NULL;
    }

    rt->client.command_queue = rt->command_queue;
    rt->client.event_queue = rt->event_queue;
    rt->client.frame_slot = &rt->frame_slot;
    rt->client.symbol_slot = &rt->symbol_slot;

    if (config) {
        rt->basic_rom_path = runtime_copy_string(config->basic_rom_path);
        rt->char_rom_path = runtime_copy_string(config->char_rom_path);
        rt->kernal_rom_path = runtime_copy_string(config->kernal_rom_path);
        rt->system_rom_path = runtime_copy_string(config->system_rom_path);
        rt->ini_path = runtime_copy_string(config->ini_path);
        rt->use_ini = config->use_ini;
        rt->save_ini = config->save_ini;
        rt->machine_config = config->machine_config;
        memcpy(rt->turbo_speeds, config->turbo_speeds, sizeof(rt->turbo_speeds));
        rt->turbo_speed_count = config->turbo_speed_count;
        rt->active_turbo_multiplier = config->active_turbo_multiplier;
        if (rt->turbo_speed_count == 0 || rt->active_turbo_multiplier == 0) {
            runtime_config defaults = {0};
            runtime_config_set_turbo_defaults(&defaults);
            memcpy(rt->turbo_speeds, defaults.turbo_speeds, sizeof(rt->turbo_speeds));
            rt->turbo_speed_count = defaults.turbo_speed_count;
            rt->active_turbo_multiplier = defaults.active_turbo_multiplier;
        }

        if ((config->basic_rom_path && !rt->basic_rom_path) ||
            (config->char_rom_path && !rt->char_rom_path) ||
            (config->kernal_rom_path && !rt->kernal_rom_path) ||
            (config->system_rom_path && !rt->system_rom_path) ||
            (config->ini_path && !rt->ini_path)) {
            runtime_destroy(rt);
            return NULL;
        }
    }

    return rt;
}

void runtime_destroy(runtime *rt) {
    if (!rt) {
        return;
    }

    runtime_stop(rt);
    free(rt->basic_rom_path);
    free(rt->char_rom_path);
    free(rt->kernal_rom_path);
    free(rt->system_rom_path);
    free(rt->ini_path);
    mutex_destroy(rt->frame_slot.mutex);
    mutex_destroy(rt->symbol_slot.mutex);
    message_queue_destroy(rt->event_queue);
    message_queue_destroy(rt->command_queue);
    free(rt);
}

bool runtime_start(runtime *rt) {
    if (!rt) {
        return false;
    }

    if (rt->started) {
        return true;
    }

    rt->thread = thread_create("c64m-runtime", runtime_thread_main, rt);
    if (!rt->thread) {
        return false;
    }

    rt->started = true;
    return true;
}

void runtime_stop(runtime *rt) {
    if (!rt || !rt->started) {
        return;
    }

    runtime_command command = {
        .type = RUNTIME_COMMAND_QUIT,
    };

    message_queue_push(rt->command_queue, &command);
    message_queue_wake_all(rt->command_queue);
    thread_join(rt->thread);
    thread_destroy(rt->thread);
    rt->thread = NULL;
    rt->started = false;
}

bool runtime_save_debug_ini(runtime *rt) {
    return runtime_save_breakpoints_to_ini(rt);
}

runtime_client *runtime_get_client(runtime *rt) {
    if (!rt) {
        return NULL;
    }

    return &rt->client;
}
