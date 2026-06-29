#include "platform_socket.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET c64m_socket_handle;
#define C64M_INVALID_SOCKET INVALID_SOCKET
#define c64m_close_socket closesocket
#else
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int c64m_socket_handle;
#define C64M_INVALID_SOCKET (-1)
#define c64m_close_socket close
#endif

struct platform_socket_listener {
    c64m_socket_handle handle;
};

struct platform_socket_connection {
    c64m_socket_handle handle;
};

bool platform_socket_startup(void)
{
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void platform_socket_shutdown(void)
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

platform_socket_listener *platform_socket_listen_localhost(uint16_t port)
{
    platform_socket_listener *listener;
    c64m_socket_handle handle;
    struct sockaddr_in addr;
    int one = 1;

    handle = socket(AF_INET, SOCK_STREAM, 0);
    if (handle == C64M_INVALID_SOCKET) {
        return NULL;
    }

    setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(handle, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(handle, 1) != 0) {
        c64m_close_socket(handle);
        return NULL;
    }

    listener = (platform_socket_listener *)calloc(1, sizeof(*listener));
    if (listener == NULL) {
        c64m_close_socket(handle);
        return NULL;
    }
    listener->handle = handle;
    return listener;
}

void platform_socket_listener_close(platform_socket_listener *listener)
{
    if (listener == NULL || listener->handle == C64M_INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    shutdown(listener->handle, SD_BOTH);
#else
    shutdown(listener->handle, SHUT_RDWR);
#endif
    c64m_close_socket(listener->handle);
    listener->handle = C64M_INVALID_SOCKET;
}

void platform_socket_listener_destroy(platform_socket_listener *listener)
{
    if (listener == NULL) {
        return;
    }
    platform_socket_listener_close(listener);
    free(listener);
}

platform_socket_connection *platform_socket_accept(platform_socket_listener *listener)
{
    platform_socket_connection *connection;
    c64m_socket_handle handle;

    if (listener == NULL || listener->handle == C64M_INVALID_SOCKET) {
        return NULL;
    }

    handle = accept(listener->handle, NULL, NULL);
    if (handle == C64M_INVALID_SOCKET) {
        return NULL;
    }

    connection = (platform_socket_connection *)calloc(1, sizeof(*connection));
    if (connection == NULL) {
        c64m_close_socket(handle);
        return NULL;
    }
    connection->handle = handle;
    return connection;
}

int platform_socket_read(platform_socket_connection *connection, void *buffer, size_t size)
{
    int received;

    if (connection == NULL || connection->handle == C64M_INVALID_SOCKET ||
        buffer == NULL || size == 0) {
        return -1;
    }

    received = (int)recv(connection->handle, (char *)buffer, (int)size, 0);
    if (received == 0) {
        return 0;
    }
    if (received < 0) {
        return -1;
    }
    return received;
}

bool platform_socket_write_all(
    platform_socket_connection *connection,
    const void *buffer,
    size_t size)
{
    const char *cursor = (const char *)buffer;
    size_t remaining = size;

    if (connection == NULL || connection->handle == C64M_INVALID_SOCKET ||
        (buffer == NULL && size > 0)) {
        return false;
    }

    while (remaining > 0) {
        int sent = (int)send(connection->handle, cursor, (int)remaining, 0);
        if (sent <= 0) {
            return false;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }
    return true;
}

void platform_socket_connection_close(platform_socket_connection *connection)
{
    if (connection == NULL || connection->handle == C64M_INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    shutdown(connection->handle, SD_BOTH);
#else
    shutdown(connection->handle, SHUT_RDWR);
#endif
    c64m_close_socket(connection->handle);
    connection->handle = C64M_INVALID_SOCKET;
}

void platform_socket_connection_destroy(platform_socket_connection *connection)
{
    if (connection == NULL) {
        return;
    }
    platform_socket_connection_close(connection);
    free(connection);
}
