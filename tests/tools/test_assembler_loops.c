#include "asm.h"
#include "errorlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    snprintf(path, path_size, "/tmp/c64m_assembler_loops_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }

    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        perror("fdopen");
        close(fd);
        unlink(path);
        return 1;
    }

    fputs(source, fp);

    if (fclose(fp) != 0) {
        perror("fclose");
        unlink(path);
        return 1;
    }

    return 0;
}

static int assemble_file(const char *path, test_memory *mem, ERRORLOG *log)
{
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

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

static int expect_bytes(const test_memory *mem, const uint8_t *expected, size_t expected_len)
{
    int failures = 0;
    if (memcmp(&mem->memory[0x0801], expected, expected_len) != 0) {
        fprintf(stderr, "loop output mismatch\n");
        failures++;
    }
    for (size_t i = 0; i < expected_len; i++) {
        if (mem->memory[0x0801 + i] != expected[i]) {
            fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                    (size_t)(0x0801 + i), expected[i], mem->memory[0x0801 + i]);
        }
    }
    return failures;
}

static int test_loop_output(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const uint8_t expected[] = {
        0x00, 0x01, 0x02, 0x03,
        0x10, 0x11, 0x12,
        0x00, 0x01, 0x10, 0x11,
        0xaa, 0xbb
    };
    const char *source =
        ".for i=0, i .lt 4, i++\n"
        "    .byte i\n"
        ".endfor\n"
        ".repeat 3, r\n"
        "    .byte $10 + r\n"
        ".endrep\n"
        ".for outer=0, outer .lt 2, outer++\n"
        "    .repeat 2, inner\n"
        "        .byte outer * 16 + inner\n"
        "    .endrepeat\n"
        ".endfor\n"
        ".repeat 0\n"
        "    .byte $ee\n"
        "    .repeat 1\n"
        "        .byte $ef\n"
        "    .endrepeat\n"
        ".endrepeat\n"
        ".for skipped=0, skipped .lt 0, skipped++\n"
        "    .byte $ff\n"
        ".endfor\n"
        ".byte $aa, $bb\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assembler_assemble failed with %zu errors\n", log.log_array.items);
        failures++;
    }
    failures += expect_bytes(&mem, expected, sizeof(expected));
    errlog_shutdown(&log);
    unlink(path);

    return failures;
}

static int test_loop_errors(void)
{
    struct {
        const char *name;
        const char *source;
    } cases[] = {
        {
            "unmatched endfor",
            ".endfor\n",
        },
        {
            "mismatched nesting",
            ".repeat 1\n"
            ".endfor\n",
        },
        {
            "unclosed loop",
            ".repeat 1\n"
            "    .byte $01\n",
        },
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
        unlink(path);
    }

    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_loop_output();
    failures += test_loop_errors();

    return failures == 0 ? 0 : 1;
}
