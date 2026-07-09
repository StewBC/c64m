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
    return c64m_test_write_temp_file(path, path_size, "c64m_assembler_expressions", source);
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

/*
 * The `*` (program counter) symbol must evaluate to the address of the current
 * instruction/line, matching the standard assembler convention. In particular
 * `jmp *` is the classic self-loop and must encode its own address.
 */
static int test_star_is_current_address(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /*
     * $c000: AD 00 C0   lda $c000   ; lda * -> address of this lda
     * $c003: 4C 03 C0   jmp $c003   ; jmp * -> self-loop
     * $c006: 08 C0      .word *     ; -> address of this .word
     */
    const uint8_t expected[] = {
        0xAD, 0x00, 0xC0,
        0x4C, 0x03, 0xC0,
        0x06, 0xC0,
    };
    const char *source =
        "* = $c000\n"
        "    lda *\n"
        "    jmp *\n"
        "    .word *\n";
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

    if (memcmp(&mem.memory[0xC000], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "'*' current-address output mismatch\n");
        for (size_t i = 0; i < sizeof(expected); i++) {
            if (mem.memory[0xC000 + i] != expected[i]) {
                fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                        (size_t)(0xC000 + i), expected[i], mem.memory[0xC000 + i]);
            }
        }
        failures++;
    }

    errlog_shutdown(&log);
    c64m_test_remove_file(path);

    return failures;
}

/* `*` participates in arithmetic as the current address (e.g. skip-over jump). */
static int test_star_arithmetic(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /*
     * $c000: 4C 05 C0   jmp $c005   ; jmp *+5 -> skip the two .byte $ea after it
     * $c003: EA EA      .byte $ea, $ea
     */
    const uint8_t expected[] = {
        0x4C, 0x05, 0xC0,
        0xEA, 0xEA,
    };
    const char *source =
        "* = $c000\n"
        "    jmp *+5\n"
        "    .byte $ea, $ea\n";
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

    if (memcmp(&mem.memory[0xC000], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "'*' arithmetic output mismatch\n");
        for (size_t i = 0; i < sizeof(expected); i++) {
            if (mem.memory[0xC000 + i] != expected[i]) {
                fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                        (size_t)(0xC000 + i), expected[i], mem.memory[0xC000 + i]);
            }
        }
        failures++;
    }

    errlog_shutdown(&log);
    c64m_test_remove_file(path);

    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_star_is_current_address();
    failures += test_star_arithmetic();

    return failures == 0 ? 0 : 1;
}
