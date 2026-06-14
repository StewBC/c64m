# THREADS.md

# c64m Threading and Message Passing Architecture

This document defines how c64m creates threads and how the UI thread communicates with the emulation runtime thread.

The goal is to establish the threading model early, before SDL window/display work and before C64 hardware emulation becomes complex.

---

# Core Model

c64m uses two threads from the beginning.

```text
Main/UI thread
    SDL event polling
    SDL rendering
    Nuklear frontend
    runtime_client
    sends commands to runtime
    receives runtime events/responses

Emulation thread
    runtime
    machine
    receives commands from UI
    runs the C64
    publishes events/responses/snapshots
```

Runtime and machine must stay on the same thread.

```text
Do not split runtime and machine.
Do not let frontend touch the live machine.
Do not pass live machine pointers across threads.
```

The UI thread talks to runtime through `runtime_client`.

The runtime thread talks back to the UI through runtime events and snapshot publication.

---

# Use SDL Threading Primitives

c64m already depends on SDL2 for platform services.

Use SDL2 for cross-platform threading primitives:

```text
SDL_Thread
SDL_CreateThread
SDL_WaitThread

SDL_mutex
SDL_LockMutex
SDL_UnlockMutex

SDL_cond
SDL_CondWait
SDL_CondSignal
SDL_CondBroadcast

SDL_sem
SDL_SemWait
SDL_SemPost

SDL_Atomic*
```

Do not vendor another thread library.

Do not use pthreads directly.

Do not use C11 threads directly.

Do not expose SDL thread types outside the platform/util implementation layer.

---

# Wrapping SDL

Project code should not directly depend on SDL thread types.

Create small wrappers in `util/`:

```text
src/util/
    thread.c
    thread.h
    mutex.c
    mutex.h
    cond.c
    cond.h
    message_queue.c
    message_queue.h
```

The wrappers may use SDL internally.

Public project-facing APIs should expose c64m types, not SDL types.

Example:

```c
typedef struct thread thread;

typedef int (*thread_entry_fn)(void *userdata);

thread *thread_create(
    const char *name,
    thread_entry_fn entry,
    void *userdata);

void thread_join(thread *t);
void thread_destroy(thread *t);
```

Internally:

```c
struct thread {
    SDL_Thread *handle;
};
```

---

# Message Passing

Communication between the UI thread and runtime thread uses message queues.

There are two primary queues:

```text
runtime command queue
    UI thread -> runtime thread

runtime event queue
    runtime thread -> UI thread
```

Do not use the SDL event queue for runtime messages.

SDL events are for SDL/window/input events.

Runtime communication is project-owned.

---

# Queue Requirements

The queue implementation lives in:

```text
src/util/message_queue.c
src/util/message_queue.h
```

The queue should be:

```text
fixed-capacity
thread-safe
single or multiple producer safe if practical
blocking pop capable
non-blocking pop capable
non-blocking push capable
```

For initial implementation, fixed capacity is preferred over unbounded allocation.

The runtime command queue and runtime event queue should have explicit capacities.

Suggested initial capacities:

```text
runtime commands: 256
runtime events:   256
```

---

# Message Queue API

Suggested API:

```c
typedef struct message_queue message_queue;

message_queue *message_queue_create(
    size_t item_size,
    size_t capacity);

void message_queue_destroy(message_queue *queue);

bool message_queue_push(
    message_queue *queue,
    const void *item);

bool message_queue_try_pop(
    message_queue *queue,
    void *out_item);

bool message_queue_wait_pop(
    message_queue *queue,
    void *out_item);

bool message_queue_wait_pop_timeout(
    message_queue *queue,
    void *out_item,
    uint32_t timeout_ms);

void message_queue_wake_all(message_queue *queue);
```

Behavior:

```text
message_queue_push
    returns false if queue is full

message_queue_try_pop
    returns false if queue is empty

message_queue_wait_pop
    blocks until an item is available or the queue is woken/shut down

message_queue_wait_pop_timeout
    blocks until item, timeout, or wake/shutdown

message_queue_wake_all
    wakes threads waiting on the queue
```

The queue should copy message bytes into internal storage.

The queue should not store borrowed pointers unless the message type explicitly owns that policy.

---

# Runtime Commands

Runtime commands are sent from the UI thread to the runtime thread.

Files:

```text
src/runtime/runtime_command.c
src/runtime/runtime_command.h
```

Initial command types:

```c
typedef enum runtime_command_type {
    RUNTIME_COMMAND_NONE = 0,

    RUNTIME_COMMAND_PING,
    RUNTIME_COMMAND_RUN,
    RUNTIME_COMMAND_PAUSE,
    RUNTIME_COMMAND_RESET,

    RUNTIME_COMMAND_STEP_INSTRUCTION,
    RUNTIME_COMMAND_STEP_CYCLE,
    RUNTIME_COMMAND_STEP_FRAME,

    RUNTIME_COMMAND_SET_SPEED_MODE,

    RUNTIME_COMMAND_SET_BREAKPOINT,
    RUNTIME_COMMAND_CLEAR_BREAKPOINT,
    RUNTIME_COMMAND_SET_WATCHPOINT,
    RUNTIME_COMMAND_CLEAR_WATCHPOINT,

    RUNTIME_COMMAND_REQUEST_MEMORY,
    RUNTIME_COMMAND_REQUEST_CPU_STATE,
    RUNTIME_COMMAND_REQUEST_VIC_STATE,
    RUNTIME_COMMAND_REQUEST_SID_STATE,
    RUNTIME_COMMAND_REQUEST_FRAME,

    RUNTIME_COMMAND_LOAD_PROGRAM,

    RUNTIME_COMMAND_QUIT
} runtime_command_type;
```

Initial command struct:

```c
typedef struct runtime_command {
    runtime_command_type type;

    union {
        struct {
            int unused;
        } ping;

        struct {
            uint16_t address;
        } breakpoint;

        struct {
            uint16_t address;
            uint16_t length;
        } memory_request;

        struct {
            char path[1024];
        } load_program;
    } data;
} runtime_command;
```

The initial bring-up only needs:

```text
PING
QUIT
```

Add other fields when those features are implemented.

---

# Runtime Events

Runtime events are sent from the runtime thread to the UI thread.

Files:

```text
src/runtime/runtime_event.c
src/runtime/runtime_event.h
```

Initial event types:

```c
typedef enum runtime_event_type {
    RUNTIME_EVENT_NONE = 0,

    RUNTIME_EVENT_PONG,
    RUNTIME_EVENT_STARTED,
    RUNTIME_EVENT_PAUSED,
    RUNTIME_EVENT_STOPPED,

    RUNTIME_EVENT_BREAKPOINT_HIT,
    RUNTIME_EVENT_WATCHPOINT_HIT,

    RUNTIME_EVENT_FRAME_READY,

    RUNTIME_EVENT_MEMORY_RESPONSE,
    RUNTIME_EVENT_CPU_STATE_RESPONSE,
    RUNTIME_EVENT_VIC_STATE_RESPONSE,
    RUNTIME_EVENT_SID_STATE_RESPONSE,

    RUNTIME_EVENT_ERROR
} runtime_event_type;
```

Initial event struct:

```c
typedef struct runtime_event {
    runtime_event_type type;

    union {
        struct {
            int unused;
        } pong;

        struct {
            char message[1024];
        } error;

        struct {
            uint64_t frame_id;
        } frame_ready;
    } data;
} runtime_event;
```

The initial bring-up only needs:

```text
PONG
STOPPED
ERROR
```

---

# Runtime Object

Runtime owns:

```text
runtime thread
command queue
 event queue
live machine instance
runtime state
shutdown flag
```

Suggested files:

```text
src/runtime/runtime.c
src/runtime/runtime.h
src/runtime/runtime_thread.c
src/runtime/runtime_thread.h
src/runtime/runtime_client.c
src/runtime/runtime_client.h
```

`runtime_client` is the UI-facing API.

The frontend must not directly push to raw queues.

The frontend calls `runtime_client_*` functions.

---

# Runtime Client API

The UI thread talks to runtime through `runtime_client`.

Suggested API:

```c
typedef struct runtime runtime;
typedef struct runtime_client runtime_client;
typedef struct runtime_config runtime_config;

runtime *runtime_create(const runtime_config *config);
void runtime_destroy(runtime *rt);

bool runtime_start(runtime *rt);
void runtime_stop(runtime *rt);

runtime_client *runtime_get_client(runtime *rt);

bool runtime_client_ping(runtime_client *client);
bool runtime_client_run(runtime_client *client);
bool runtime_client_pause(runtime_client *client);
bool runtime_client_reset(runtime_client *client);
bool runtime_client_quit(runtime_client *client);

bool runtime_client_poll_event(
    runtime_client *client,
    runtime_event *out_event);
```

The frontend should usually use non-blocking event polling:

```c
runtime_event event;
while(runtime_client_poll_event(client, &event)) {
    frontend_handle_runtime_event(&event);
}
```

The UI thread should not block waiting for runtime during normal operation.

---

# Runtime Thread Loop

The runtime thread owns the live machine.

Initial bring-up loop:

```c
static int runtime_thread_main(void *userdata)
{
    runtime *rt = userdata;
    bool running = true;

    runtime_publish_started(rt);

    while(running) {
        runtime_command cmd;

        if(!message_queue_wait_pop(rt->command_queue, &cmd)) {
            continue;
        }

        switch(cmd.type) {
            case RUNTIME_COMMAND_PING:
                runtime_publish_pong(rt);
                break;

            case RUNTIME_COMMAND_QUIT:
                running = false;
                break;

            default:
                runtime_publish_error(rt, "unsupported runtime command");
                break;
        }
    }

    runtime_publish_stopped(rt);
    return 0;
}
```

Later, when emulation is running, runtime should not block forever waiting for commands.

It should periodically poll commands while running the machine.

Example later behavior:

```text
paused:
    wait for command

running:
    poll commands
    execute machine cycles/frame slice
    publish frames/events
```

---

# UI Thread Rules

The UI thread owns:

```text
SDL window
SDL renderer
SDL events
Nuklear context
frontend views
runtime_client
```

The UI thread may:

```text
send runtime commands
poll runtime events
consume published snapshots
render latest frame
```

The UI thread must not:

```text
read live machine memory
write live machine memory
call machine functions
block indefinitely waiting for runtime
hold runtime locks during rendering
```

---

# Runtime Thread Rules

The runtime thread owns:

```text
runtime state
live machine instance
emulation execution
breakpoints
watchpoints
snapshot creation
```

The runtime thread may:

```text
read/write machine state
execute CPU/VIC/SID/CIA logic
publish copied snapshots
push runtime events
```

The runtime thread must not:

```text
call SDL rendering APIs
call Nuklear APIs
touch frontend state
use frontend pointers
publish live machine pointers
```

---

# Frame Publication

Do not send full video frames through the normal event queue repeatedly.

Use a separate latest-frame handoff.

Events should only notify the UI that a frame is ready.

Recommended model:

```text
runtime writes latest complete frame snapshot
runtime pushes FRAME_READY event
frontend receives FRAME_READY
frontend copies or swaps latest frame into SDL texture
older frames may be dropped
```

This prevents the event queue from filling with stale video frames.

Turbo mode may generate more frames than the UI displays.

The UI should display the newest complete frame and drop older frames.

---

# Shutdown

Shutdown must be explicit and clean.

Normal shutdown sequence:

```text
UI thread sends RUNTIME_COMMAND_QUIT
runtime thread exits loop
runtime thread publishes RUNTIME_EVENT_STOPPED
UI thread joins runtime thread
queues are destroyed
runtime is destroyed
platform/frontend shutdown continues
```

The runtime thread must always be joined before process exit.

`runtime_destroy()` should not leak threads.

If `runtime_destroy()` is called while the runtime thread is still active, it should request shutdown and join.

---

# Initial Bring-Up Milestone

Implement this before opening an SDL window.

Program behavior:

```text
main starts runtime
runtime thread starts
main sends PING
runtime replies PONG
main prints confirmation
main sends QUIT
runtime replies STOPPED
main joins runtime thread
program exits cleanly
```

Expected output example:

```text
main: starting runtime
runtime: started
main: sending ping
runtime: ping received
main: pong received
main: sending quit
runtime: stopped
main: runtime joined
main: exit
```

This milestone proves:

```text
thread creation works
message queues work
UI-to-runtime communication works
runtime-to-UI communication works
clean shutdown works
```

Only after this milestone should the SDL window be brought up.

---

# Suggested Implementation Order

```text
1. Add SDL2 dependency at platform/build level if not already present.
2. Implement util/thread wrapper.
3. Implement util/mutex wrapper.
4. Implement util/cond wrapper.
5. Implement util/message_queue.
6. Define runtime_command.
7. Define runtime_event.
8. Implement runtime_create/destroy.
9. Implement runtime_start/stop.
10. Implement runtime_thread_main.
11. Implement runtime_client_ping.
12. Implement runtime_client_quit.
13. Implement runtime_client_poll_event.
14. Add main.c bring-up test.
15. Verify clean shutdown under repeated runs.
```

---

# Error Policy

Runtime errors should be published as events.

```text
RUNTIME_EVENT_ERROR
```

The runtime should not print directly except during early bring-up/debugging.

Eventually logging should go through `util/log`.

For early bring-up, simple logging to stdout is acceptable.

---

# Core Rules

```text
SDL provides primitives.
util wraps primitives.
runtime owns queues.
runtime_client is the UI-facing API.
frontend never touches machine.
runtime never touches frontend.
frames are published as snapshots, not live pointers.
```

The architecture is:

```text
UI thread
    frontend
    platform
    runtime_client
        |
        | command queue
        v
runtime thread
    runtime
    machine
        |
        | event queue + snapshots
        v
UI thread
```
