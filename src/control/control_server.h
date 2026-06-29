#pragma once

#include "control_protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct control_server control_server;

control_server *control_server_create(uint16_t port);
void control_server_destroy(control_server *server);

bool control_server_start(control_server *server);
void control_server_stop(control_server *server);

bool control_server_poll_request(control_server *server, control_request *out_request);
bool control_server_post_response(control_server *server, const control_response *response);

