#pragma once

#include "runtime.h"
#include "runtime_client.h"

#include "c64.h"
#include "c64_rom.h"

#include <stdbool.h>

#define RUNTIME_COMMAND_QUEUE_CAPACITY 256
#define RUNTIME_EVENT_QUEUE_CAPACITY 256

typedef struct message_queue message_queue;
typedef struct thread thread;

struct runtime_client {
    message_queue *command_queue;
    message_queue *event_queue;
};

struct runtime {
    thread *thread;
    message_queue *command_queue;
    message_queue *event_queue;
    runtime_client client;
    c64_t machine;
    c64_rom_set roms;
    char *basic_rom_path;
    char *char_rom_path;
    char *kernal_rom_path;
    char *system_rom_path;
    bool started;
};

int runtime_thread_main(void *userdata);
