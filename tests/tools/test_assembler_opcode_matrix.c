/*
 * Assembler opcode/addressing-mode coverage matrix (standing tool, NOT a ctest).
 *
 * Why this exists: the `lsr <zp>` bug (lsr carried width 0 in the opcode table,
 * so `lsr $nn` emitted a lone $46 with no operand byte) survived for years
 * because nothing exercised every mnemonic in every addressing mode. The opcode
 * *byte* was correct; only the emitted *length* was wrong, so a check that
 * re-derives expected output from the assembler's own table would not have
 * caught it. This test needs an INDEPENDENT oracle.
 *
 * The oracle is the disassembler's hand-authored 256-entry table
 * (src/tools/disasm_6502), written separately and carrying a `length` field.
 * For every valid opcode byte we:
 *   1. render canonical operand bytes through the disassembler to a source line
 *      (e.g. $46,$03 -> "LSR $03"), and
 *   2. feed that exact text back to the assembler and check it re-emits the
 *      same opcode byte AND the same instruction length (and the operand bytes,
 *      where they are position-stable).
 * Two independently written tables have to agree across the whole documented
 * NMOS set. `lsr $03` fails loudly here: assembler emits 1 byte, table says 2.
 *
 * Not wired into ctest on purpose - run it by hand during assembler work:
 *
 *     cmake --build build --target test_assembler_opcode_matrix
 *     ./build/test_assembler_opcode_matrix
 *
 * Exit status is 0 when every valid opcode round-trips, 1 otherwise, so it is
 * one `add_test(...)` line away from being a gate if that is ever wanted.
 *
 * Scope: the disasm table is documented-NMOS only (illegal opcodes are gaps),
 * which is exactly the assembler's mnemonic set. 65c02-only variants have no
 * disasm oracle here and are not covered - they would need a hand-authored
 * addendum.
 */

#include "asm.h"
#include "errorlog.h"
#include "disasm_6502.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed assemble origin. Also the disassembly address, so REL targets that the
 * disassembler prints as absolute re-encode to the same relative offset. */
#define ORIGIN 0xC000u

typedef struct {
    uint8_t memory[65536];
    int lo;   /* lowest byte written, or -1 */
    int hi;   /* highest byte written, or -1 */
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
    if (mem->lo < 0 || addr < (unsigned)mem->lo) {
        mem->lo = addr;
    }
    if (mem->hi < 0 || addr > (unsigned)mem->hi) {
        mem->hi = addr;
    }
}

/*
 * Canonical operand bytes for an opcode of the given length.
 *  - length 3 uses $C003 so the high byte is non-zero; otherwise the assembler
 *    would see a value < 256 and pick a zeropage mode, shrinking the encoding.
 *  - length 2 uses $03 (a valid zeropage / immediate / relative operand).
 * The disassembler renders these back to the assembler's own syntax.
 */
static void canonical_bytes(uint8_t opcode, uint8_t length, uint8_t out[3])
{
    out[0] = opcode;
    out[1] = 0x03;
    out[2] = 0xC0;
    if (length < 3) {
        out[2] = 0x00;
    }
    if (length < 2) {
        out[1] = 0x00;
    }
}

/* Assemble one source line at ORIGIN; return emitted bytes via mem. */
static int assemble_line(const char *line, test_memory *mem)
{
    char path[256];
    char source[128];
    CB_ASM_CTX cb;
    ASSEMBLER as;
    ERRORLOG log;
    int result;

    memset(mem, 0, sizeof(*mem));
    mem->lo = -1;
    mem->hi = -1;

    snprintf(source, sizeof(source), "* = $%04X\n    %s\n", ORIGIN, line);
    if (c64m_test_write_temp_file(path, sizeof(path), "c64m_opmatrix", source) != 0) {
        return ASM_ERR;
    }

    errlog_init(&log);
    memset(&cb, 0, sizeof(cb));
    cb.user = mem;
    cb.output_byte = output_byte;

    if (assembler_init(&as, &log, &cb) != ASM_OK) {
        errlog_shutdown(&log);
        c64m_test_remove_file(path);
        return ASM_ERR;
    }
    result = assembler_assemble(&as, path, ORIGIN);
    assembler_shutdown(&as);
    errlog_shutdown(&log);
    c64m_test_remove_file(path);
    return result;
}

int main(void)
{
    int tested = 0;
    int failures = 0;

    for (int op = 0; op < 256; op++) {
        uint8_t opcode = (uint8_t)op;
        if (!disasm_6502_opcode_is_valid(opcode)) {
            continue;
        }

        uint8_t length = disasm_6502_instruction_length(opcode);
        uint8_t bytes[3];
        canonical_bytes(opcode, length, bytes);

        disasm_6502_line dl =
            disasm_6502_decode_line(ORIGIN, bytes, length, NULL);

        tested++;

        test_memory mem;
        if (assemble_line(dl.text, &mem) != ASM_OK) {
            fprintf(stderr, "FAIL $%02X \"%s\": assemble error\n", opcode, dl.text);
            failures++;
            continue;
        }

        if (mem.lo < 0) {
            fprintf(stderr, "FAIL $%02X \"%s\": emitted no bytes\n", opcode, dl.text);
            failures++;
            continue;
        }

        int emitted_len = mem.hi - mem.lo + 1;
        if (emitted_len != length) {
            fprintf(stderr,
                    "FAIL $%02X \"%s\": expected length %u, got %d\n",
                    opcode, dl.text, length, emitted_len);
            failures++;
            continue;
        }

        uint8_t emitted_op = mem.memory[ORIGIN];
        if (emitted_op != opcode) {
            fprintf(stderr,
                    "FAIL $%02X \"%s\": expected opcode $%02X, got $%02X\n",
                    opcode, dl.text, opcode, emitted_op);
            failures++;
            continue;
        }

        /* Operand bytes are position-stable for every mode except relative,
         * where the encoded byte is a recomputed branch offset. Because we
         * disassemble and assemble at the same ORIGIN the offset round-trips to
         * our canonical $03, so we can compare all operand bytes uniformly. */
        int operand_ok = 1;
        for (int i = 1; i < length; i++) {
            if (mem.memory[ORIGIN + i] != bytes[i]) {
                operand_ok = 0;
                break;
            }
        }
        if (!operand_ok) {
            fprintf(stderr,
                    "FAIL $%02X \"%s\": operand bytes mismatch (got",
                    opcode, dl.text);
            for (int i = 0; i < length; i++) {
                fprintf(stderr, " $%02X", mem.memory[ORIGIN + i]);
            }
            fprintf(stderr, ", expected");
            for (int i = 0; i < length; i++) {
                fprintf(stderr, " $%02X", bytes[i]);
            }
            fprintf(stderr, ")\n");
            failures++;
            continue;
        }
    }

    if (failures == 0) {
        printf("PASS %d/%d documented NMOS opcodes round-trip\n", tested, tested);
        return 0;
    }
    printf("FAIL %d/%d opcodes failed\n", failures, tested);
    return 1;
}
