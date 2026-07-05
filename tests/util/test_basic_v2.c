#include "basic_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, int value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_str(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected [%s], got [%s]\n", name, expected, actual);
        exit(1);
    }
}

static void expect_bytes(const char *name, const uint8_t *expected, size_t exp_len,
                         const uint8_t *actual, size_t act_len) {
    size_t i;
    if (exp_len != act_len) {
        fprintf(stderr, "%s: expected %zu bytes, got %zu\n", name, exp_len, act_len);
        exit(1);
    }
    for (i = 0; i < exp_len; ++i) {
        if (expected[i] != actual[i]) {
            fprintf(stderr, "%s: byte %zu expected %02X, got %02X\n",
                    name, i, expected[i], actual[i]);
            exit(1);
        }
    }
}

/* 10 PRINT "HI" tokenized at $0801:
   link=$0809, line#=10, PRINT($99), space, "HI", $00, then $0000 end. */
static void test_tokenize_simple(void) {
    static const uint8_t expected[] = {
        0x0C, 0x08,             /* link -> $080C (11-byte line record) */
        0x0A, 0x00,             /* line 10 */
        0x99,                   /* PRINT */
        0x20,                   /* space */
        0x22, 0x48, 0x49, 0x22, /* "HI" */
        0x00,                   /* end of line */
        0x00, 0x00              /* end of program */
    };
    uint8_t out[256];
    size_t out_len = 0;
    char err[128];
    const char *src = "10 PRINT \"HI\"\n";

    expect_true("tokenize simple ok",
        basic_v2_tokenize(src, strlen(src), 0x0801u, out, sizeof(out), &out_len, err, sizeof(err)));
    expect_bytes("tokenize simple bytes", expected, sizeof(expected), out, out_len);
}

/* Round-trip: source -> tokens -> source should reproduce the (uppercased,
   normalised) text. */
static void roundtrip(const char *name, const char *src, const char *expected_out) {
    uint8_t tokens[4096];
    char text[4096];
    size_t token_len = 0;
    size_t text_len = 0;
    char err[128];

    expect_true(name,
        basic_v2_tokenize(src, strlen(src), 0x0801u, tokens, sizeof(tokens), &token_len, err, sizeof(err)));
    expect_true(name,
        basic_v2_detokenize(tokens, token_len, 0x0801u, text, sizeof(text), &text_len, err, sizeof(err)));
    expect_str(name, expected_out, text);
}

static void test_roundtrip_basic(void) {
    roundtrip("roundtrip print",
              "10 PRINT \"HELLO\"\n20 GOTO 10\n",
              "10 PRINT \"HELLO\"\n20 GOTO 10\n");
}

/* REM suspends keyword crunching to end of line: GOTO inside a REM must stay
   literal text, not become a token. */
static void test_rem_literal(void) {
    roundtrip("rem literal",
              "10 REM GOTO PRINT DATA\n",
              "10 REM GOTO PRINT DATA\n");
}

/* DATA suspends crunching until a colon: keywords in the data list stay
   literal, but a statement after the colon is tokenized again. */
static void test_data_literal(void) {
    roundtrip("data literal",
              "10 DATA END,ON,GO:PRINT\n",
              "10 DATA END,ON,GO:PRINT\n");
}

/* Quoted text is copied literally; a keyword inside quotes is not tokenized. */
static void test_quote_literal(void) {
    roundtrip("quote literal",
              "10 PRINT \"RUN THEN STOP\"\n",
              "10 PRINT \"RUN THEN STOP\"\n");
}

/* Operators are tokens too, and prefixes must not shadow longer keywords
   (GOTO not GO+TO, INPUT# not INPUT). */
static void test_operators_and_prefixes(void) {
    roundtrip("operators", "10 A=1+2*3\n", "10 A=1+2*3\n");
    roundtrip("goto prefix", "10 GOTO 100\n", "10 GOTO 100\n");
    roundtrip("go statement", "10 GO TO 100\n", "10 GO TO 100\n");
    roundtrip("input hash", "10 INPUT#1,A\n", "10 INPUT#1,A\n");
}

/* Lowercase source is normalised to uppercase (C64 default character set). */
static void test_lowercase_normalised(void) {
    roundtrip("lowercase", "10 print \"hi\"\n", "10 PRINT \"HI\"\n");
}

/* Multiple lines produce correctly chained link pointers. */
static void test_link_pointers(void) {
    uint8_t out[256];
    size_t out_len = 0;
    char err[128];
    const char *src = "10 A\n20 B\n";
    uint16_t link1, link2;

    expect_true("links tokenize ok",
        basic_v2_tokenize(src, strlen(src), 0x0801u, out, sizeof(out), &out_len, err, sizeof(err)));
    /* line 1 record: link(2) line#(2) 'A'(1) 00(1) = 6 bytes -> link = $0807 */
    link1 = (uint16_t)(out[0] | (out[1] << 8));
    expect_true("first link", link1 == 0x0807u);
    /* line 2 record starts at offset 6, same 6-byte length -> link = $080D */
    link2 = (uint16_t)(out[6] | (out[7] << 8));
    expect_true("second link", link2 == 0x080Du);
    /* terminating null link */
    expect_true("end link lo", out[12] == 0x00);
    expect_true("end link hi", out[13] == 0x00);
}

/* A line without a leading number is an error. */
static void test_missing_line_number(void) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];
    const char *src = "PRINT\n";

    if (basic_v2_tokenize(src, strlen(src), 0x0801u, out, sizeof(out), &out_len, err, sizeof(err))) {
        fail("expected error for missing line number");
    }
    expect_true("error message set", err[0] != '\0');
}

int main(void) {
    test_tokenize_simple();
    test_roundtrip_basic();
    test_rem_literal();
    test_data_literal();
    test_quote_literal();
    test_operators_and_prefixes();
    test_lowercase_normalised();
    test_link_pointers();
    test_missing_line_number();
    printf("basic_v2 tests passed\n");
    return 0;
}
