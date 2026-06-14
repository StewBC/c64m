#include "c64.h"
#include "runtime.h"
#include "runtime_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
    TEST_RESET_VECTOR = 0xe000,
};

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void expect_true(const char *name, bool value) {
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t expected, uint16_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %04x, got %04x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64_gt(const char *name, uint64_t lhs, uint64_t rhs) {
    if (lhs <= rhs) {
        fprintf(stderr, "%s: expected %llu > %llu\n", name, (unsigned long long)lhs, (unsigned long long)rhs);
        exit(1);
    }
}

static void build_roms(c64_rom_set *roms, uint16_t reset_vector) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;

    roms->kernal[0x1ffc] = (uint8_t)(reset_vector & 0xff);
    roms->kernal[0x1ffd] = (uint8_t)(reset_vector >> 8);
}

static void copy_to_kernal(c64_rom_set *roms, uint16_t address, const uint8_t *program, size_t size) {
    size_t offset = address - 0xe000;
    size_t i;

    if (address < 0xe000 || offset + size > C64_KERNAL_ROM_SIZE) {
        fail("test program does not fit in KERNAL ROM");
    }

    for (i = 0; i < size; i++) {
        roms->kernal[offset + i] = program[i];
    }
}

static void copy_to_basic(c64_rom_set *roms, uint16_t address, const uint8_t *program, size_t size) {
    size_t offset = address - 0xa000;
    size_t i;

    if (address < 0xa000 || address > 0xbfff || offset + size > C64_BASIC_ROM_SIZE) {
        fail("test program does not fit in BASIC ROM");
    }

    for (i = 0; i < size; i++) {
        roms->basic[offset + i] = program[i];
    }
}

static void reset_machine(c64_t *machine, const c64_rom_set *roms) {
    char error[256];

    c64_init(machine);
    expect_true("install synthetic ROMs", c64_install_roms(machine, roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void step_machine(c64_t *machine, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; i++) {
        expect_true("step instruction", c64_step_instruction(machine, error, sizeof(error)));
    }
}

static c64_cpu_snapshot snapshot(const c64_t *machine) {
    c64_cpu_snapshot out;
    c64_copy_cpu_snapshot(machine, &out);
    return out;
}

static void test_reset_vector(void) {
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    reset_machine(&machine, &roms);

    expect_u8("reset vector lo through bus", 0x00, c64_bus_read(&machine.bus, 0xfffc));
    expect_u8("reset vector hi through bus", 0xe0, c64_bus_read(&machine.bus, 0xfffd));
    expect_u16("reset pc", TEST_RESET_VECTOR, snapshot(&machine).pc);
}

static void test_instruction_fetch(void) {
    static const uint8_t program[] = {0xea, 0xea, 0xea};
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_machine(&machine, 3);

    expect_u16("three NOP instructions advance PC", 0xe003, snapshot(&machine).pc);
}

static void test_ram_read_write(void) {
    static const uint8_t program[] = {
        0xa9, 0x55,       /* LDA #$55 */
        0x8d, 0x34, 0x12, /* STA $1234 */
        0xa9, 0x00,       /* LDA #$00 */
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_snapshot state;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_machine(&machine, 4);
    state = snapshot(&machine);

    expect_u8("RAM write through bus", 0x55, c64_bus_read(&machine.bus, 0x1234));
    expect_u8("RAM read into A", 0x55, state.a);
}

static void test_stack(void) {
    static const uint8_t program[] = {
        0xa9, 0x7b, /* LDA #$7b */
        0x48,       /* PHA */
        0xa9, 0x00, /* LDA #$00 */
        0x68        /* PLA */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_snapshot state;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_machine(&machine, 4);
    state = snapshot(&machine);

    expect_u8("stack write through bus", 0x7b, c64_bus_read(&machine.bus, 0x0100));
    expect_u8("PLA restores A", 0x7b, state.a);
    expect_u8("SP restored", 0x00, state.sp);
}

static void test_branch_taken_and_not_taken(void) {
    static const uint8_t program[] = {
        0xa9, 0x00, /* LDA #$00, Z set */
        0xf0, 0x02, /* BEQ +2, taken */
        0xa9, 0x01, /* skipped */
        0xd0, 0x02, /* BNE +2, not taken */
        0xa9, 0x02, /* LDA #$02, Z clear */
        0xd0, 0x02, /* BNE +2, taken */
        0xa9, 0x03, /* skipped */
        0x18,       /* CLC */
        0x90, 0x02, /* BCC +2, taken */
        0xa9, 0x04, /* skipped */
        0x38,       /* SEC */
        0xb0, 0x02, /* BCS +2, taken */
        0xa9, 0x05, /* skipped */
        0xa9, 0x80, /* LDA #$80, N set */
        0x30, 0x02, /* BMI +2, taken */
        0xa9, 0x06, /* skipped */
        0xa9, 0x01, /* LDA #$01, N clear */
        0x10, 0x02, /* BPL +2, taken */
        0xa9, 0x07, /* skipped */
        0xb8,       /* CLV */
        0x50, 0x02, /* BVC +2, taken */
        0xa9, 0x08, /* skipped */
        0xa9, 0x40, /* LDA #$40 */
        0x2c, 0x80, 0xe0, /* BIT $e080, V set from operand */
        0x70, 0x02, /* BVS +2, taken */
        0xa9, 0x09, /* skipped */
        0xa9, 0x42  /* LDA #$42 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_snapshot state;

    build_roms(&roms, TEST_RESET_VECTOR);
    roms.kernal[0x0080] = 0x40;
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_machine(&machine, 19);
    state = snapshot(&machine);

    expect_u8("branch sequence final A", 0x42, state.a);
}

static void run_branch_not_taken_case(const char *name, const uint8_t *program, size_t size, size_t steps) {
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    roms.kernal[0x0080] = 0x40;
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, size);
    reset_machine(&machine, &roms);
    step_machine(&machine, steps);

    expect_u8(name, 0x5a, snapshot(&machine).a);
}

static void test_branch_not_taken_cases(void) {
    static const uint8_t beq_program[] = {
        0xa9, 0x01, /* LDA #$01, Z clear */
        0xf0, 0x02, /* BEQ +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bne_program[] = {
        0xa9, 0x00, /* LDA #$00, Z set */
        0xd0, 0x02, /* BNE +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bmi_program[] = {
        0xa9, 0x01, /* LDA #$01, N clear */
        0x30, 0x02, /* BMI +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bpl_program[] = {
        0xa9, 0x80, /* LDA #$80, N set */
        0x10, 0x02, /* BPL +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bcs_program[] = {
        0x18,       /* CLC */
        0xb0, 0x02, /* BCS +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bcc_program[] = {
        0x38,       /* SEC */
        0x90, 0x02, /* BCC +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bvs_program[] = {
        0xb8,       /* CLV */
        0x70, 0x02, /* BVS +2, not taken */
        0xa9, 0x5a  /* LDA #$5a */
    };
    static const uint8_t bvc_program[] = {
        0xa9, 0x40,       /* LDA #$40 */
        0x2c, 0x80, 0xe0, /* BIT $e080, V set from operand */
        0x50, 0x02,       /* BVC +2, not taken */
        0xa9, 0x5a        /* LDA #$5a */
    };

    run_branch_not_taken_case("BEQ not taken", beq_program, sizeof(beq_program), 3);
    run_branch_not_taken_case("BNE not taken", bne_program, sizeof(bne_program), 3);
    run_branch_not_taken_case("BMI not taken", bmi_program, sizeof(bmi_program), 3);
    run_branch_not_taken_case("BPL not taken", bpl_program, sizeof(bpl_program), 3);
    run_branch_not_taken_case("BCS not taken", bcs_program, sizeof(bcs_program), 3);
    run_branch_not_taken_case("BCC not taken", bcc_program, sizeof(bcc_program), 3);
    run_branch_not_taken_case("BVS not taken", bvs_program, sizeof(bvs_program), 3);
    run_branch_not_taken_case("BVC not taken", bvc_program, sizeof(bvc_program), 4);
}

static void test_page_crossing_branch(void) {
    static const uint8_t program[] = {
        0xa9, 0x00, /* LDA #$00 */
        0xf0, 0x10  /* BEQ from $e0f2 to $e104 */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, 0xe0f0);
    copy_to_kernal(&roms, 0xe0f0, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_machine(&machine, 2);

    expect_u16("page-crossing branch PC", 0xe104, snapshot(&machine).pc);
}

static void test_page_wrap(void) {
    static const uint8_t program[] = {
        0xa9, 0x7e, /* LDA #$7e */
        0x85, 0x00, /* STA $00 */
        0xa2, 0x01, /* LDX #$01 */
        0xb5, 0xff  /* LDA $ff,X wraps to $00 */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    step_machine(&machine, 4);

    expect_u8("zero-page indexed wrap", 0x7e, snapshot(&machine).a);
}

static void test_banking_affects_execution(void) {
    static const uint8_t kernal_program[] = {
        0xa9, 0x36,       /* LDA #$36 */
        0x8d, 0x01, 0x00, /* STA $0001, hiding BASIC ROM */
        0x4c, 0x00, 0xa0  /* JMP $a000 */
    };
    static const uint8_t basic_program[] = {
        0xa9, 0x11 /* LDA #$11 */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, kernal_program, sizeof(kernal_program));
    copy_to_basic(&roms, 0xa000, basic_program, sizeof(basic_program));
    reset_machine(&machine, &roms);
    c64_bus_write(&machine.bus, 0xa000, 0xa9);
    c64_bus_write(&machine.bus, 0xa001, 0x42);

    expect_u8("BASIC ROM initially visible", 0xa9, c64_bus_read(&machine.bus, 0xa000));
    step_machine(&machine, 4);

    expect_u8("RAM visible after banking change", 0xa9, c64_bus_read(&machine.bus, 0xa000));
    expect_u8("execution follows banked RAM", 0x42, snapshot(&machine).a);
}

static void write_runtime_roms(void) {
    FILE *system = fopen("cpu_validation_64c.bin", "wb");
    FILE *character = fopen("cpu_validation_character.bin", "wb");
    size_t i;

    if (!system || !character) {
        fail("failed to create runtime validation ROMs");
    }

    for (i = 0; i < C64_BASIC_ROM_SIZE; i++) {
        fputc(0xea, system);
    }
    for (i = 0; i < C64_KERNAL_ROM_SIZE; i++) {
        fputc(0xea, system);
    }

    fseek(system, (long)(C64_BASIC_ROM_SIZE + 0x1ffc), SEEK_SET);
    fputc(0x00, system);
    fputc(0xe0, system);

    for (i = 0; i < C64_CHAR_ROM_SIZE; i++) {
        fputc(0x00, character);
    }

    fclose(system);
    fclose(character);
}

static int poll_event(runtime_client *client, runtime_event *event, runtime_event_type type) {
    clock_t start = clock();

    while ((double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) {
                return 1;
            }
        }
    }

    return 0;
}

static void test_runtime_run_instructions(void) {
    runtime_config config = {
        .system_rom_path = "cpu_validation_64c.bin",
        .char_rom_path = "cpu_validation_character.bin",
    };
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint16_t reset_pc;
    uint64_t reset_cycles;

    write_runtime_roms();
    expect_true("runtime init", runtime_init());
    rt = runtime_create(&config);
    if (!rt) {
        fail("runtime_create failed");
    }

    expect_true("runtime start", runtime_start(rt));
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED)) {
        fail("STARTED event not received");
    }

    expect_true("runtime reset command", runtime_client_reset(client));
    if (!poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE) ||
        !poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("reset response not received");
    }

    reset_pc = event.data.cpu_state.pc;
    reset_cycles = event.data.cpu_state.cycles;
    expect_u16("runtime reset PC", TEST_RESET_VECTOR, reset_pc);

    expect_true("runtime run command", runtime_client_run_instructions(client, 3));
    if (!poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE) ||
        !poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE)) {
        fail("run response not received");
    }

    expect_u16("runtime run advances PC", (uint16_t)(reset_pc + 3), event.data.cpu_state.pc);
    expect_u64_gt("runtime run advances cycles", event.data.cpu_state.cycles, reset_cycles);

    runtime_client_quit(client);
    runtime_stop(rt);
    runtime_destroy(rt);
    runtime_shutdown();

    remove("cpu_validation_64c.bin");
    remove("cpu_validation_character.bin");
}

int main(void) {
    test_reset_vector();
    test_instruction_fetch();
    test_ram_read_write();
    test_stack();
    test_branch_taken_and_not_taken();
    test_branch_not_taken_cases();
    test_page_crossing_branch();
    test_page_wrap();
    test_banking_affects_execution();
    test_runtime_run_instructions();
    return 0;
}
