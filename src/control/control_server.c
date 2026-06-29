#include "control_server.h"

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
    CONTROL_RESPONSE_LINE_MAX = 512
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

static void control_server_set_connection(
    control_server *server,
    platform_socket_connection *connection)
{
    if (server == NULL || server->lock == NULL) {
        return;
    }
    mutex_lock(server->lock);
    server->connection = connection;
    mutex_unlock(server->lock);
}

static platform_socket_connection *control_server_get_connection(control_server *server)
{
    platform_socket_connection *connection;

    if (server == NULL || server->lock == NULL) {
        return NULL;
    }
    mutex_lock(server->lock);
    connection = server->connection;
    mutex_unlock(server->lock);
    return connection;
}

static bool control_server_read_line(
    platform_socket_connection *connection,
    char *out,
    size_t out_size)
{
    size_t used = 0;

    if (connection == NULL || out == NULL || out_size == 0) {
        return false;
    }

    while (used + 1 < out_size) {
        char ch;
        int n = platform_socket_read(connection, &ch, 1);
        if (n <= 0) {
            return false;
        }
        out[used++] = ch;
        if (ch == '\n') {
            out[used] = '\0';
            return true;
        }
    }

    out[used] = '\0';
    return false;
}

static bool control_server_send_response(
    platform_socket_connection *connection,
    const control_response *response)
{
    char line[CONTROL_RESPONSE_LINE_MAX];

    if (!control_protocol_write_response_line(response, line, sizeof(line))) {
        return false;
    }
    return platform_socket_write_all(connection, line, strlen(line));
}

static bool control_server_handle_connection(
    control_server *server,
    platform_socket_connection *connection)
{
    bool keep_open = true;

    while (!control_server_is_stopping(server) && keep_open) {
        char line[CONTROL_LINE_MAX];
        control_request request;
        control_response response;

        if (!control_server_read_line(connection, line, sizeof(line))) {
            break;
        }

        if (!control_protocol_parse_request(line, &request, &response)) {
            if (!control_server_send_response(connection, &response)) {
                break;
            }
            continue;
        }

        if (!message_queue_push(server->requests, &request)) {
            control_protocol_format_error(
                &response,
                request.id,
                "busy",
                "request queue full",
                false);
            if (!control_server_send_response(connection, &response)) {
                break;
            }
            continue;
        }

        if (!message_queue_wait_pop(server->responses, &response)) {
            break;
        }
        if (!control_server_send_response(connection, &response)) {
            break;
        }
        keep_open = !response.close_client;
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
        control_server_set_connection(server, connection);
        control_server_handle_connection(server, connection);
        control_server_set_connection(server, NULL);
        platform_socket_connection_destroy(connection);
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
    {
        platform_socket_connection *connection = control_server_get_connection(server);
        if (connection != NULL) {
            platform_socket_connection_close(connection);
        }
    }
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

bool control_server_post_response(control_server *server, const control_response *response)
{
    if (server == NULL || response == NULL) {
        return false;
    }
    return message_queue_push(server->responses, response);
}
