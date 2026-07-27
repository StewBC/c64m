#include "control_server.h"

#include "control_deferred.h"
#include "message_queue.h"
#include "mutex.h"
#include "platform_socket.h"
#include "thread.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CONTROL_QUEUE_CAPACITY = 32,
    CONTROL_RESPONSE_LINE_MAX = 512,
    /* Match deferred table capacity: max outstanding pipelined requests. */
    CONTROL_PIPELINE_HIGH_WATER = CONTROL_DEFERRED_CAPACITY,
    /* After the client disconnects, wait at most this long for outstanding
       responses to drain before abandoning the connection. Must exceed the
       main-thread deferred timeout (2000 ms) so genuine completions still
       flush; it only backstops responses that never arrive. */
    CONTROL_DISCONNECT_DRAIN_MS = 3000
};

struct control_server {
    uint16_t port;
    bool started;
    bool stopping;
    mutex *lock;
    platform_socket_listener *listener;
    platform_socket_connection *connection;
    message_queue *requests;
    message_queue *responses;
    thread *worker;
    uint64_t connection_epoch;
    control_server_wake_fn wake_hook;
};

static bool control_server_is_stopping(control_server *server)
{
    bool stopping;

    if (server == NULL || server->lock == NULL) {
        return true;
    }
    mutex_lock(server->lock);
    stopping = server->stopping;
    mutex_unlock(server->lock);
    return stopping;
}

static void control_server_set_stopping(control_server *server, bool stopping)
{
    if (server == NULL || server->lock == NULL) {
        return;
    }
    mutex_lock(server->lock);
    server->stopping = stopping;
    mutex_unlock(server->lock);
}

/* Read one line. Returns 1=complete, 0=would-block (partial kept in *used),
   -1=error/eof. *used is the partial length carried across calls. */
static int control_server_read_line_nb(
    platform_socket_connection *connection,
    char *out,
    size_t out_size,
    size_t *used)
{
    if (connection == NULL || out == NULL || out_size == 0 || used == NULL) {
        return -1;
    }

    while (*used + 1 < out_size) {
        char ch;
        int n = platform_socket_read(connection, &ch, 1);
        if (n == -2) {
            return 0; /* would block */
        }
        if (n <= 0) {
            return -1;
        }
        out[(*used)++] = ch;
        if (ch == '\n') {
            out[*used] = '\0';
            return 1;
        }
    }

    out[*used] = '\0';
    return -1; /* line too long */
}

static bool control_server_read_line(
    platform_socket_connection *connection,
    char *out,
    size_t out_size)
{
    size_t used = 0;
    int rc;

    if (connection == NULL || out == NULL || out_size == 0) {
        return false;
    }
    /* Blocking-compatible: keep reading until complete (socket may be blocking). */
    for (;;) {
        rc = control_server_read_line_nb(connection, out, out_size, &used);
        if (rc == 1) {
            return true;
        }
        if (rc < 0) {
            return false;
        }
        /* would-block on a blocking socket should not happen; treat as fail. */
        if (platform_socket_wait_readable(connection, 1000) < 0) {
            return false;
        }
    }
}

static bool control_server_read_exact(
    platform_socket_connection *connection,
    uint8_t *out,
    size_t size)
{
    size_t used = 0;

    while (used < size) {
        int n = platform_socket_read(connection, out + used, size - used);
        if (n == -2) {
            if (platform_socket_wait_readable(connection, 2000u) <= 0) {
                return false;
            }
            continue;
        }
        if (n <= 0) {
            return false;
        }
        used += (size_t)n;
    }
    return true;
}

static bool control_server_read_request_payload(
    platform_socket_connection *connection,
    control_request *request,
    control_response *response)
{
    char newline;

    if (request == NULL || request->payload_size == 0) {
        return true;
    }
    request->payload = (uint8_t *)malloc(request->payload_size);
    if (request->payload == NULL) {
        control_protocol_format_error(
            response,
            request->id,
            "memory",
            "payload allocation failed",
            false);
        return false;
    }
    if (!control_server_read_exact(connection, request->payload, request->payload_size)) {
        control_request_release(request);
        control_protocol_format_error(
            response,
            request->id,
            "bad-payload",
            "payload framing error",
            true);
        return false;
    }
    {
        int n;
        for (;;) {
            n = platform_socket_read(connection, &newline, 1);
            if (n == -2) {
                if (platform_socket_wait_readable(connection, 2000u) <= 0) {
                    n = -1;
                    break;
                }
                continue;
            }
            break;
        }
        if (n != 1 || newline != '\n') {
            control_request_release(request);
            control_protocol_format_error(
                response,
                request->id,
                "bad-payload",
                "payload framing error",
                true);
            return false;
        }
    }
    return true;
}

static bool control_server_send_response(
    platform_socket_connection *connection,
    const control_response *response)
{
    char line[CONTROL_RESPONSE_LINE_MAX];

    if (!control_protocol_write_response_line(response, line, sizeof(line))) {
        return false;
    }
    if (!platform_socket_write_all(connection, line, strlen(line))) {
        return false;
    }
    if (response->type == CONTROL_RESPONSE_DATA) {
        static const char newline = '\n';
        if (!platform_socket_write_all(connection, response->payload, response->payload_size)) {
            return false;
        }
        if (!platform_socket_write_all(connection, &newline, 1)) {
            return false;
        }
    }
    return true;
}

static void control_server_notify_wake(control_server *server)
{
    control_server_wake_fn hook;

    if (server == NULL || server->lock == NULL) {
        return;
    }
    mutex_lock(server->lock);
    hook = server->wake_hook;
    mutex_unlock(server->lock);
    if (hook != NULL) {
        hook();
    }
}

void control_server_set_wake_hook(control_server *server, control_server_wake_fn fn)
{
    if (server == NULL || server->lock == NULL) {
        return;
    }
    mutex_lock(server->lock);
    server->wake_hook = fn;
    mutex_unlock(server->lock);
}

static bool control_server_push_request(control_server *server, control_request *request)
{
    if (server == NULL || request == NULL) {
        return false;
    }
    if (!message_queue_push(server->requests, request)) {
        return false;
    }
    control_server_notify_wake(server);
    return true;
}

static bool control_server_ids_contains(const uint32_t *ids, size_t count, uint32_t id)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static void control_server_ids_remove(uint32_t *ids, size_t *count, uint32_t id)
{
    size_t i;
    for (i = 0; i < *count; ++i) {
        if (ids[i] == id) {
            ids[i] = ids[*count - 1u];
            (*count)--;
            return;
        }
    }
}

/* Multiplexed connection loop: accept further requests while responses are
   pending (up to CONTROL_PIPELINE_HIGH_WATER). Completion order may differ
   from request order; clients correlate by wire id. */
static bool control_server_handle_connection(
    control_server *server,
    platform_socket_connection *connection)
{
    bool keep_open = true;
    char line[CONTROL_LINE_MAX];
    size_t line_used = 0;
    uint32_t outstanding_ids[CONTROL_PIPELINE_HIGH_WATER];
    size_t outstanding = 0;
    bool read_closed = false;
    uint64_t read_closed_deadline = 0;

    if (!platform_socket_set_nonblocking(connection, true)) {
        /* Fall back to legacy one-in-flight blocking loop. */
        platform_socket_set_nonblocking(connection, false);
        while (!control_server_is_stopping(server) && keep_open) {
            control_request request;
            control_response response;
            line_used = 0;
            if (control_server_read_line_nb(connection, line, sizeof(line), &line_used) != 1 &&
                !control_server_read_line(connection, line, sizeof(line))) {
                break;
            }
            if (!control_protocol_parse_request(line, &request, &response)) {
                if (!control_server_send_response(connection, &response)) {
                    break;
                }
                continue;
            }
            if (!control_server_read_request_payload(connection, &request, &response)) {
                control_server_send_response(connection, &response);
                keep_open = !response.close_client;
                continue;
            }
            if (!control_server_push_request(server, &request)) {
                control_request_release(&request);
                control_protocol_format_error(
                    &response, request.id, "busy", "request queue full", false);
                if (!control_server_send_response(connection, &response)) {
                    break;
                }
                continue;
            }
            if (!message_queue_wait_pop(server->responses, &response)) {
                break;
            }
            if (!control_server_send_response(connection, &response)) {
                control_response_release(&response);
                break;
            }
            control_response_release(&response);
            keep_open = !response.close_client;
        }
        return true;
    }

    while (!control_server_is_stopping(server) && keep_open) {
        control_response response;
        bool did_work = false;

        /* Flush any completed responses first. */
        while (message_queue_try_pop(server->responses, &response)) {
            did_work = true;
            control_server_ids_remove(outstanding_ids, &outstanding, response.id);
            if (!control_server_send_response(connection, &response)) {
                control_response_release(&response);
                keep_open = false;
                break;
            }
            keep_open = !response.close_client;
            control_response_release(&response);
            if (!keep_open) {
                break;
            }
        }
        if (!keep_open) {
            break;
        }

        /* Accept more requests while under the high-water mark. */
        if (!read_closed && outstanding < CONTROL_PIPELINE_HIGH_WATER) {
            int line_rc = control_server_read_line_nb(
                connection, line, sizeof(line), &line_used);
            if (line_rc == 1) {
                control_request request;
                control_response err;

                did_work = true;
                line_used = 0;
                if (!control_protocol_parse_request(line, &request, &err)) {
                    if (!control_server_send_response(connection, &err)) {
                        keep_open = false;
                        break;
                    }
                } else if (control_server_ids_contains(
                               outstanding_ids, outstanding, request.id)) {
                    control_request_release(&request);
                    control_protocol_format_error(
                        &err, request.id, "bad-id", "duplicate outstanding id", false);
                    if (!control_server_send_response(connection, &err)) {
                        keep_open = false;
                        break;
                    }
                } else if (!control_server_read_request_payload(
                               connection, &request, &err)) {
                    control_server_send_response(connection, &err);
                    keep_open = !err.close_client;
                } else if (!control_server_push_request(server, &request)) {
                    control_request_release(&request);
                    control_protocol_format_error(
                        &err, request.id, "busy", "request queue full", false);
                    if (!control_server_send_response(connection, &err)) {
                        keep_open = false;
                        break;
                    }
                } else {
                    outstanding_ids[outstanding++] = request.id;
                }
            } else if (line_rc < 0) {
                if (!read_closed) {
                    /* Peer hung up. Give outstanding deferred work a bounded
                       grace period to post its responses (the main-thread
                       deferred timeout is shorter, so legitimate completions
                       still flush), then abandon the connection regardless.
                       Without this, a single response that never arrives - a
                       lost or misrouted completion - would keep this handler
                       looping forever and, because only one client is served
                       at a time, wedge the control port for every future
                       client. */
                    read_closed_deadline =
                        SDL_GetTicks64() + CONTROL_DISCONNECT_DRAIN_MS;
                }
                read_closed = true;
            }
        }

        if (read_closed &&
            (outstanding == 0 || SDL_GetTicks64() >= read_closed_deadline)) {
            break;
        }

        if (!did_work) {
            /* Wait for socket readability and/or a response. */
            if (outstanding > 0) {
                if (message_queue_wait_pop_timeout(server->responses, &response, 5u)) {
                    control_server_ids_remove(outstanding_ids, &outstanding, response.id);
                    if (!control_server_send_response(connection, &response)) {
                        control_response_release(&response);
                        break;
                    }
                    keep_open = !response.close_client;
                    control_response_release(&response);
                } else if (!read_closed && outstanding < CONTROL_PIPELINE_HIGH_WATER) {
                    (void)platform_socket_wait_readable(connection, 5u);
                }
            } else if (!read_closed) {
                if (platform_socket_wait_readable(connection, 100u) < 0) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    /* Drain leftover responses for this connection so the next client is clean. */
    {
        control_response response;
        while (message_queue_try_pop(server->responses, &response)) {
            control_response_release(&response);
        }
    }
    return true;
}

static int control_server_thread_main(void *userdata)
{
    control_server *server = (control_server *)userdata;

    if (server == NULL) {
        return 1;
    }

    while (!control_server_is_stopping(server)) {
        platform_socket_connection *connection = platform_socket_accept(server->listener);
        if (connection == NULL) {
            break;
        }
        mutex_lock(server->lock);
        server->connection_epoch += 1u;
        if (server->connection_epoch == 0u) {
            server->connection_epoch = 1u;
        }
        server->connection = connection;
        mutex_unlock(server->lock);
        control_server_handle_connection(server, connection);
        /* Clear and free the connection under the lock so this destroy cannot
           race control_server_stop() closing the same pointer from the main
           thread (heap-use-after-free on teardown / client-disconnect). */
        mutex_lock(server->lock);
        server->connection = NULL;
        platform_socket_connection_destroy(connection);
        mutex_unlock(server->lock);
    }

    return 0;
}

control_server *control_server_create(uint16_t port)
{
    control_server *server;

    server = (control_server *)calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->port = port;
    server->lock = mutex_create();
    server->requests = message_queue_create(sizeof(control_request), CONTROL_QUEUE_CAPACITY);
    server->responses = message_queue_create(sizeof(control_response), CONTROL_QUEUE_CAPACITY);
    if (server->lock == NULL || server->requests == NULL || server->responses == NULL) {
        control_server_destroy(server);
        return NULL;
    }
    return server;
}

void control_server_destroy(control_server *server)
{
    if (server == NULL) {
        return;
    }
    control_server_stop(server);
    message_queue_destroy(server->requests);
    message_queue_destroy(server->responses);
    mutex_destroy(server->lock);
    free(server);
}

bool control_server_start(control_server *server)
{
    if (server == NULL || server->started) {
        return false;
    }
    if (!platform_socket_startup()) {
        return false;
    }
    control_server_set_stopping(server, false);
    server->listener = platform_socket_listen_localhost(server->port);
    if (server->listener == NULL) {
        platform_socket_shutdown();
        return false;
    }
    server->worker = thread_create("c64m-control", control_server_thread_main, server);
    if (server->worker == NULL) {
        platform_socket_listener_destroy(server->listener);
        server->listener = NULL;
        platform_socket_shutdown();
        return false;
    }
    server->started = true;
    SDL_Log("control: listening on 127.0.0.1:%u", (unsigned)server->port);
    return true;
}

void control_server_stop(control_server *server)
{
    if (server == NULL || !server->started) {
        return;
    }

    control_server_set_stopping(server, true);
    /* Close the active connection while holding the lock so we cannot close a
       pointer the worker thread is concurrently destroying (see the matching
       locked destroy in control_server_thread_main). Close is idempotent and
       only shuts the socket down to wake a blocked read; the worker still owns
       the free. */
    mutex_lock(server->lock);
    if (server->connection != NULL) {
        platform_socket_connection_close(server->connection);
    }
    mutex_unlock(server->lock);
    if (server->listener != NULL) {
        platform_socket_listener_close(server->listener);
    }
    message_queue_wake_all(server->responses);
    message_queue_wake_all(server->requests);
    thread_join(server->worker);
    thread_destroy(server->worker);
    server->worker = NULL;
    platform_socket_listener_destroy(server->listener);
    server->listener = NULL;
    platform_socket_shutdown();
    server->started = false;
}

bool control_server_poll_request(control_server *server, control_request *out_request)
{
    if (server == NULL || out_request == NULL) {
        return false;
    }
    return message_queue_try_pop(server->requests, out_request);
}

uint64_t control_server_connection_epoch(control_server *server)
{
    uint64_t epoch;

    if (server == NULL || server->lock == NULL) {
        return 0u;
    }
    mutex_lock(server->lock);
    epoch = server->connection_epoch;
    mutex_unlock(server->lock);
    return epoch;
}

bool control_server_has_client(control_server *server)
{
    bool has;

    if (server == NULL || server->lock == NULL) {
        return false;
    }
    mutex_lock(server->lock);
    has = server->connection != NULL;
    mutex_unlock(server->lock);
    return has;
}

bool control_server_post_response(control_server *server, const control_response *response)
{
    if (server == NULL || response == NULL) {
        return false;
    }
    return message_queue_push(server->responses, response);
}
