#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct platform_socket_listener platform_socket_listener;
typedef struct platform_socket_connection platform_socket_connection;

bool platform_socket_startup(void);
void platform_socket_shutdown(void);

platform_socket_listener *platform_socket_listen_localhost(uint16_t port);
void platform_socket_listener_close(platform_socket_listener *listener);
void platform_socket_listener_destroy(platform_socket_listener *listener);

platform_socket_connection *platform_socket_accept(platform_socket_listener *listener);
int platform_socket_read(platform_socket_connection *connection, void *buffer, size_t size);
bool platform_socket_write_all(
    platform_socket_connection *connection,
    const void *buffer,
    size_t size);
void platform_socket_connection_close(platform_socket_connection *connection);
void platform_socket_connection_destroy(platform_socket_connection *connection);

