#include "asm.h"
#include "errorlog.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t memory[65536];
    size_t writes;
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
    mem->writes++;
}

static int write_source(char *path, size_t path_size, const char *source)
{
    return c64m_test_write_temp_file(path, path_size, "c64m_assembler_scopes", source);
}

static int assemble_file(const char *path, test_memory *mem, ERRORLOG *log)
{
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    memset(&cb, 0, sizeof(cb));
    cb.user = mem;
    cb.output_byte = output_byte;

    if (assembler_init(&as, log, &cb) != ASM_OK) {
        fprintf(stderr, "assembler_init failed\n");
        return ASM_ERR;
    }

    result = assembler_assemble(&as, path, 0x0801);
    assembler_shutdown(&as);
    return result;
}

static int test_scopes_and_procs(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const uint8_t expected[] = {
        0x11,
        0x01, 0x08,
        0x22,
        0x04, 0x08,
        0x00, 0x01
    };
    const char *source =
        ".scope Alpha\n"
        "start:\n"
        "    .byte $11\n"
        ".endscope\n"
        ".word Alpha::start\n"
        ".proc Worker\n"
        "entry:\n"
        "    .byte $22\n"
        ".endproc\n"
        ".word Worker::entry\n"
        ".for i=0, i .lt 2, i++\n"
        "    .scope\n"
        "start:\n"
        "        .byte i\n"
        "    .endscope\n"
        ".endfor\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "scope/proc assembly failed with %zu errors\n", log.log_array.items);
        failures++;
    }
    if (memcmp(&mem.memory[0x0801], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "scope/proc output mismatch\n");
        failures++;
    }
    errlog_shutdown(&log);
    c64m_test_remove_file(path);

    return failures;
}

static int test_quoted_scope_name(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const uint8_t expected[] = {
        0x33,
        0x01, 0x08
    };
    /* A quoted scope name is equivalent to the bare identifier and stays
       referenceable through :: qualified lookup. */
    const char *source =
        ".scope \"Alpha\"\n"
        "start:\n"
        "    .byte $33\n"
        ".endscope\n"
        ".word Alpha::start\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "quoted scope name assembly failed with %zu errors\n", log.log_array.items);
        failures++;
    }
    if (memcmp(&mem.memory[0x0801], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "quoted scope name output mismatch\n");
        failures++;
    }
    errlog_shutdown(&log);
    c64m_test_remove_file(path);

    return failures;
}

static int test_segments(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const char *source =
        ".byte $01\n"
        ".segdef \"ZP\", $0002, noemit\n"
        ".segdef \"CODE2\", $0900\n"
        ".segment \"ZP\"\n"
        "    .byte $aa, $bb\n"
        ".segment \"CODE2\"\n"
        "    .byte $cc\n"
        ".segment \"\"\n"
        "    .byte $02\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "segment assembly failed with %zu errors\n", log.log_array.items);
        failures++;
    }
    if (mem.memory[0x0801] != 0x01 || mem.memory[0x0802] != 0x02 ||
        mem.memory[0x0900] != 0xcc || mem.memory[0x0002] != 0x00 ||
        mem.memory[0x0003] != 0x00) {
        fprintf(stderr, "segment output mismatch\n");
        failures++;
    }
    errlog_shutdown(&log);
    c64m_test_remove_file(path);

    return failures;
}

static int test_scope_segment_errors(void)
{
    struct {
        const char *name;
        const char *source;
    } cases[] = {
        {"top-level endscope", ".endscope\n"},
        {"top-level endproc", ".endproc\n"},
        {"missing segment", ".segment \"NOPE\"\n"},
        {"quoted non-identifier scope name", ".scope \"bad name\"\n.endscope\n"},
    };
    int failures = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char path[128];
        test_memory mem;
        ERRORLOG log;

        memset(&mem, 0, sizeof(mem));
        if (write_source(path, sizeof(path), cases[i].source) != 0) {
            failures++;
            continue;
        }

        errlog_init(&log);
        if (assemble_file(path, &mem, &log) != ASM_ERR || log.log_array.items == 0) {
            fprintf(stderr, "%s was not rejected\n", cases[i].name);
            failures++;
        }
        errlog_shutdown(&log);
        c64m_test_remove_file(path);
    }

    return failures;
}

typedef struct {
    size_t count;
    size_t target_index[8];
    char name[8][32];
    uint16_t address[8];
} adjustment_capture;

static void capture_adjustment(
    size_t target_index,
    const char *segment_name,
    uint16_t address,
    void *user)
{
    adjustment_capture *capture = (adjustment_capture *)user;
    if (capture->count >= 8) {
        return;
    }
    capture->target_index[capture->count] = target_index;
    snprintf(
        capture->name[capture->count],
        sizeof(capture->name[capture->count]),
        "%s",
        segment_name);
    capture->address[capture->count] = address;
    capture->count++;
}

static int test_segment_auto_adjust_converges(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    ASSEMBLER as;
    CB_ASM_CTX cb;
    adjustment_capture capture;
    int failures = 0;
    const char *source =
        ".segdef \"A\", $1000\n"
        ".segdef \"B\", $1080\n"
        ".segdef \"C\", $1200\n"
        ".segment \"A\"\n"
        "    .res $100\n"
        "    .byte $a1\n"
        ".segment \"B\"\n"
        "    .align $100\n"
        "    .byte $b1\n"
        ".segment \"C\"\n"
        "    .byte $c1\n";

    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    /* Existing/default behavior remains a hard overlap failure. */
    memset(&mem, 0, sizeof(mem));
    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_ERR ||
        log.log_array.items == 0) {
        fprintf(stderr, "overlap unexpectedly succeeded without auto-adjust\n");
        failures++;
    }
    errlog_shutdown(&log);

    /* Moving B across the $1100 alignment boundary grows its padding, so the
       first suggestion makes B overlap C. A second retry must move C again. */
    memset(&mem, 0, sizeof(mem));
    memset(&capture, 0, sizeof(capture));
    memset(&cb, 0, sizeof(cb));
    cb.user = &mem;
    cb.output_byte = output_byte;
    errlog_init(&log);
    if (assembler_init(&as, &log, &cb) != ASM_OK) {
        fprintf(stderr, "assembler_init failed for auto-adjust test\n");
        errlog_shutdown(&log);
        c64m_test_remove_file(path);
        return failures + 1;
    }
    assembler_set_auto_adjust_segments(&as, 1);
    if (assembler_assemble(&as, path, 0x0801) != ASM_OK) {
        fprintf(stderr, "auto-adjust assembly failed with %zu errors\n",
                log.log_array.items);
        failures++;
    }
    assembler_walk_segment_adjustments(&as, capture_adjustment, &capture);
    if (capture.count != 3 ||
        strcmp(capture.name[0], "A") != 0 || capture.address[0] != 0x1000 ||
        strcmp(capture.name[1], "B") != 0 || capture.address[1] != 0x1101 ||
        strcmp(capture.name[2], "C") != 0 || capture.address[2] != 0x1201) {
        fprintf(stderr, "auto-adjust final suggestion map mismatch\n");
        failures++;
    }
    if (mem.memory[0x1100] != 0xa1 ||
        mem.memory[0x1200] != 0xb1 ||
        mem.memory[0x1201] != 0xc1) {
        fprintf(stderr, "auto-adjust output landed at wrong addresses\n");
        failures++;
    }
    if (mem.writes != 514) {
        fprintf(stderr, "auto-adjust emitted provisional passes (%zu writes)\n",
                mem.writes);
        failures++;
    }
    assembler_shutdown(&as);
    errlog_shutdown(&log);
    c64m_test_remove_file(path);
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_scopes_and_procs();
    failures += test_quoted_scope_name();
    failures += test_segments();
    failures += test_scope_segment_errors();
    failures += test_segment_auto_adjust_converges();

    return failures == 0 ? 0 : 1;
}
