#include "control_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, bool value)
{
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_false(const char *name, bool value)
{
    if (value) {
        fprintf(stderr, "%s: expected false\n", name);
        exit(1);
    }
}

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_int(const char *name, int expected, int actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        exit(1);
    }
}

static void expect_string(const char *name, const char *expected, const char *actual)
{
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n", name, expected, actual);
        exit(1);
    }
}

static void test_parse_known_commands(void)
{
    control_request request;
    control_response error;

    expect_true("parse hello", control_protocol_parse_request("1 hello\n", &request, &error));
    expect_u32("hello id", 1, request.id);
    expect_int("hello type", CONTROL_COMMAND_HELLO, request.type);

    expect_true("parse version", control_protocol_parse_request("2 version\r\n", &request, &error));
    expect_u32("version id", 2, request.id);
    expect_int("version type", CONTROL_COMMAND_VERSION, request.type);

    expect_true("parse capabilities", control_protocol_parse_request("3 capabilities", &request, &error));
    expect_int("capabilities type", CONTROL_COMMAND_CAPABILITIES, request.type);

    expect_true("parse ping", control_protocol_parse_request("4 ping", &request, &error));
    expect_int("ping type", CONTROL_COMMAND_PING, request.type);

    expect_true("parse quit-client", control_protocol_parse_request("5 quit-client", &request, &error));
    expect_int("quit-client type", CONTROL_COMMAND_QUIT_CLIENT, request.type);
}

static void test_parse_rejects_invalid_input(void)
{
    control_request request;
    control_response error;

    expect_false("reject bad id", control_protocol_parse_request("x ping\n", &request, &error));
    expect_u32("bad id response id", 0, error.id);
    expect_int("bad id response type", CONTROL_RESPONSE_ERROR, error.type);

    expect_false("reject unknown", control_protocol_parse_request("7 frob\n", &request, &error));
    expect_u32("unknown response id", 7, error.id);
    expect_int("unknown response type", CONTROL_RESPONSE_ERROR, error.type);

    expect_false("reject args", control_protocol_parse_request("8 ping extra\n", &request, &error));
    expect_u32("args response id", 8, error.id);
    expect_int("args response type", CONTROL_RESPONSE_ERROR, error.type);
}

static void test_response_formatting(void)
{
    control_response response;
    char line[128];

    control_protocol_format_ok(&response, 9, NULL, false);
    expect_true("write ok", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("ok line", "9 ok\n", line);

    control_protocol_format_ok(&response, 10, "protocol=C64M/1", false);
    expect_true("write ok text", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("ok text line", "10 ok protocol=C64M/1\n", line);

    control_protocol_format_error(&response, 11, "bad-request", "missing command", false);
    expect_true("write error", control_protocol_write_response_line(&response, line, sizeof(line)));
    expect_string("error line", "11 error bad-request missing command\n", line);
}

int main(void)
{
    test_parse_known_commands();
    test_parse_rejects_invalid_input();
    test_response_formatting();
    return 0;
}
