#include "asm.h"
#include "errorlog.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t memory[65536];
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
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

int main(void)
{
    int failures = 0;

    failures += test_scopes_and_procs();
    failures += test_segments();
    failures += test_scope_segment_errors();

    return failures == 0 ? 0 : 1;
}
