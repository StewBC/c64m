#include "runtime.h"

#include "message_queue.h"
#include "mutex.h"
#include "runtime_command.h"
#include "runtime_internal.h"
#include "thread.h"

#include <stdlib.h>
#include <string.h>

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

    if (!rt->command_queue || !rt->event_queue || !rt->frame_slot.mutex) {
        runtime_destroy(rt);
        return NULL;
    }

    rt->client.command_queue = rt->command_queue;
    rt->client.event_queue = rt->event_queue;
    rt->client.frame_slot = &rt->frame_slot;

    if (config) {
        rt->basic_rom_path = runtime_copy_string(config->basic_rom_path);
        rt->char_rom_path = runtime_copy_string(config->char_rom_path);
        rt->kernal_rom_path = runtime_copy_string(config->kernal_rom_path);
        rt->system_rom_path = runtime_copy_string(config->system_rom_path);

        if ((config->basic_rom_path && !rt->basic_rom_path) ||
            (config->char_rom_path && !rt->char_rom_path) ||
            (config->kernal_rom_path && !rt->kernal_rom_path) ||
            (config->system_rom_path && !rt->system_rom_path)) {
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
    mutex_destroy(rt->frame_slot.mutex);
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

runtime_client *runtime_get_client(runtime *rt) {
    if (!rt) {
        return NULL;
    }

    return &rt->client;
}
