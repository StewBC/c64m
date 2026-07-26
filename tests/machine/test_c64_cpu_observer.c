#include "c64.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            return false; \
        } \
    } while (0)

enum {
    TEST_RESET_VECTOR = 0xe000,
    OBSERVED_BEGIN_MAX = 64,
    OBSERVED_ACCESS_MAX = 512
};

typedef struct observer_capture {
    c64_cpu_observer_begin begins[OBSERVED_BEGIN_MAX];
    c64_cpu_observer_access accesses[OBSERVED_ACCESS_MAX];
    c64_cpu_observer_trap traps[8];
    size_t begin_count;
    size_t access_count;
    size_t complete_count;
    size_t trap_count;
} observer_capture;

static void observer_begin(
    void *user,
    const c64_cpu_observer_begin *begin) {
    observer_capture *capture = (observer_capture *)user;
    if (capture->begin_count < OBSERVED_BEGIN_MAX) {
        capture->begins[capture->begin_count++] = *begin;
    }
}

static void observer_access(
    void *user,
    uint64_t machine_cycle,
    uint16_t address,
    uint8_t value,
    c6510_bus_access_kind kind) {
    observer_capture *capture = (observer_capture *)user;
    if (capture->access_count < OBSERVED_ACCESS_MAX) {
        c64_cpu_observer_access *access =
            &capture->accesses[capture->access_count++];
        access->machine_cycle = machine_cycle;
        access->address = address;
        access->value = value;
        access->kind = kind;
    }
}

static void observer_complete(void *user) {
    observer_capture *capture = (observer_capture *)user;
    capture->complete_count++;
}

static void observer_trap(
    void *user,
    const c64_cpu_observer_trap *trap) {
    observer_capture *capture = (observer_capture *)user;
    if (capture->trap_count < 8u) {
        capture->traps[capture->trap_count++] = *trap;
    }
}

static const c64_cpu_observer test_observer = {
    .begin = observer_begin,
    .access = observer_access,
    .complete = observer_complete,
    .host_trap = observer_trap,
};

static void build_roms(
    c64_rom_set *roms,
    const uint8_t *program,
    size_t program_size) {
    c64_rom_set_init(roms);
    roms->has_basic = true;
    roms->has_kernal = true;
    roms->has_character = true;
    roms->kernal[0x1ffcu] = (uint8_t)(TEST_RESET_VECTOR & 0xffu);
    roms->kernal[0x1ffdu] = (uint8_t)(TEST_RESET_VECTOR >> 8);
    if (program != NULL && program_size > 0u) {
        memcpy(roms->kernal, program, program_size);
    }
}

static bool prepare_machine(
    c64_t *machine,
    const uint8_t *program,
    size_t program_size) {
    c64_rom_set roms;
    c64_config config = {0};
    char error[256];

    build_roms(&roms, program, program_size);
    c64_init(machine);
    config.video_standard = C64_VIDEO_STANDARD_NTSC;
    c64_set_config(machine, &config);
    CHECK(c64_install_roms(machine, &roms, error, sizeof(error)));
    CHECK(c64_reset(machine, error, sizeof(error)));
    return true;
}

static bool step_instruction(c64_t *machine) {
    char error[256];
    CHECK(c64_step_instruction(machine, error, sizeof(error)));
    return true;
}

static size_t count_kind(
    const observer_capture *capture,
    c6510_bus_access_kind kind) {
    size_t count = 0u;
    size_t i;
    for (i = 0u; i < capture->access_count; ++i) {
        if (capture->accesses[i].kind == kind) {
            count++;
        }
    }
    return count;
}

static const c64_cpu_observer_access *find_access(
    const observer_capture *capture,
    c6510_bus_access_kind kind,
    uint16_t address) {
    size_t i;
    for (i = 0u; i < capture->access_count; ++i) {
        if (capture->accesses[i].kind == kind &&
            capture->accesses[i].address == address) {
            return &capture->accesses[i];
        }
    }
    return NULL;
}

static bool test_prestate_and_actual_bytes(void) {
    static const uint8_t program[] = {
        0xeau,                   /* NOP */
        0xa9u, 0x42u,            /* LDA #$42 */
        0xadu, 0x34u, 0x12u      /* LDA $1234 */
    };
    c64_t machine;
    observer_capture capture = {0};

    CHECK(prepare_machine(&machine, program, sizeof(program)));
    machine.bus.ram[0x1234u] = 0x99u;
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    CHECK(step_instruction(&machine));
    CHECK(step_instruction(&machine));
    CHECK(capture.begin_count == 3u);
    CHECK(capture.complete_count == 3u);
    CHECK(capture.begins[0].pc == 0xe000u);
    CHECK(capture.begins[1].pc == 0xe001u);
    CHECK(capture.begins[1].a == capture.begins[0].a);
    CHECK(capture.begins[2].a == 0x42u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_OPCODE_FETCH, 0xe003u) != NULL);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_OPERAND_READ, 0xe004u)->value == 0x34u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_OPERAND_READ, 0xe005u)->value == 0x12u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_DATA_READ, 0x1234u)->value == 0x99u);
    return true;
}

static bool test_self_modifying_and_read_write_rmw(void) {
    static const uint8_t program[] = {
        0xeeu, 0x34u, 0x12u      /* INC $1234 */
    };
    c64_t machine;
    observer_capture capture = {0};
    const c64_cpu_observer_access *access;

    CHECK(prepare_machine(&machine, NULL, 0u));
    machine.cpu.cpu.pc = 0xc000u;
    machine.bus.ram[0xc000u] = 0xeau;
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    machine.bus.ram[0xc000u] = 0xa9u;
    machine.bus.ram[0xc001u] = 0x7bu;
    CHECK(step_instruction(&machine));
    access = find_access(
        &capture, C6510_BUS_ACCESS_OPCODE_FETCH, 0xc000u);
    CHECK(access != NULL && access->value == 0xa9u);

    memset(&capture, 0, sizeof(capture));
    CHECK(prepare_machine(&machine, program, sizeof(program)));
    machine.bus.ram[0x1234u] = 0x40u;
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_DATA_READ, 0x1234u)->value == 0x40u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_RMW_DUMMY_WRITE, 0x1234u)->value == 0x40u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_DATA_WRITE, 0x1234u)->value == 0x41u);
    return true;
}

static bool test_stack_and_io_dummy_kinds(void) {
    static const uint8_t stack_program[] = {
        0x48u, /* PHA */
        0x68u  /* PLA */
    };
    static const uint8_t dummy_program[] = {
        0xa2u, 0x01u,             /* LDX #1 */
        0xbdu, 0xffu, 0xd0u       /* LDA $D0FF,X */
    };
    c64_t machine;
    observer_capture capture = {0};

    CHECK(prepare_machine(&machine, stack_program, sizeof(stack_program)));
    machine.cpu.cpu.A = 0x5au;
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    CHECK(step_instruction(&machine));
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_STACK_WRITE) == 1u);
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_STACK_READ) == 1u);

    memset(&capture, 0, sizeof(capture));
    CHECK(prepare_machine(&machine, dummy_program, sizeof(dummy_program)));
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    CHECK(step_instruction(&machine));
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_DUMMY_READ) == 1u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_DUMMY_READ, 0xd000u) != NULL);
    return true;
}

static bool test_irq_and_nmi_records(void) {
    static const uint8_t program[] = {
        0x58u, /* CLI */
        0xeau, /* NOP */
        0xeau
    };
    c64_t machine;
    observer_capture capture = {0};

    CHECK(prepare_machine(&machine, program, sizeof(program)));
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    cia_write_register(&machine.cia1, 0xdc0du, 0x81u);
    cia_set_interrupt_source(&machine.cia1, 0x01u);
    CHECK(step_instruction(&machine));
    CHECK(step_instruction(&machine));
    CHECK(capture.begins[capture.begin_count - 1u].kind ==
          C64_CPU_OBSERVER_IRQ);
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_VECTOR_READ) >= 2u);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_VECTOR_READ, 0xfffeu) != NULL);

    memset(&capture, 0, sizeof(capture));
    CHECK(prepare_machine(&machine, program, sizeof(program)));
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    c64_restore(&machine);
    CHECK(step_instruction(&machine));
    CHECK(capture.begin_count == 1u);
    CHECK(capture.begins[0].kind == C64_CPU_OBSERVER_NMI);
    CHECK(find_access(
        &capture, C6510_BUS_ACCESS_VECTOR_READ, 0xfffau) != NULL);
    return true;
}

static bool test_undocumented_micro_and_deferred_replay(void) {
    static const uint8_t micro_program[] = {
        0x07u, 0x10u /* SLO $10 */
    };
    static const uint8_t deferred_program[] = {
        0x0bu, 0x7fu /* ANC #$7f: compatibility path */
    };
    c64_t machine;
    observer_capture capture = {0};

    CHECK(prepare_machine(&machine, micro_program, sizeof(micro_program)));
    machine.bus.ram[0x0010u] = 1u;
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    CHECK(capture.begin_count == 1u && capture.complete_count == 1u);
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_RMW_DUMMY_WRITE) == 1u);

    memset(&capture, 0, sizeof(capture));
    CHECK(prepare_machine(&machine, deferred_program, sizeof(deferred_program)));
    machine.cpu.cpu.A = 0xffu;
    CHECK(!c6510_micro_can_begin(&machine.cpu, deferred_program[0]));
    c64_set_cpu_observer(&machine, &test_observer, &capture);
    CHECK(step_instruction(&machine));
    CHECK(capture.begin_count == 1u);
    CHECK(capture.complete_count == 1u);
    CHECK(capture.begins[0].a == 0xffu);
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_OPCODE_FETCH) == 1u);
    CHECK(count_kind(&capture, C6510_BUS_ACCESS_OPERAND_READ) == 1u);
    return true;
}

static bool test_null_observer_and_reset_vector_exclusion(void) {
    static const uint8_t program[] = {0xeau, 0x4cu, 0x00u, 0xe0u};
    c64_rom_set roms;
    c64_config config = {0};
    c64_t observed;
    c64_t plain;
    observer_capture capture = {0};
    c64_cpu_snapshot observed_cpu;
    c64_cpu_snapshot plain_cpu;
    char error[256];

    CHECK(prepare_machine(&plain, program, sizeof(program)));
    CHECK(prepare_machine(&observed, program, sizeof(program)));
    c64_set_cpu_observer(&plain, NULL, NULL);
    c64_set_cpu_observer(&observed, &test_observer, &capture);
    CHECK(c64_step_cycles(&plain, 1000u, error, sizeof(error)));
    CHECK(c64_step_cycles(&observed, 1000u, error, sizeof(error)));
    c64_copy_cpu_snapshot(&plain, &plain_cpu);
    c64_copy_cpu_snapshot(&observed, &observed_cpu);
    CHECK(memcmp(&plain_cpu, &observed_cpu, sizeof(plain_cpu)) == 0);
    CHECK(plain.clock.cycle == observed.clock.cycle);

    memset(&capture, 0, sizeof(capture));
    build_roms(&roms, program, sizeof(program));
    c64_init(&observed);
    c64_set_cpu_observer(&observed, &test_observer, &capture);
    config.video_standard = C64_VIDEO_STANDARD_NTSC;
    c64_set_config(&observed, &config);
    CHECK(c64_install_roms(&observed, &roms, error, sizeof(error)));
    CHECK(c64_reset(&observed, error, sizeof(error)));
    CHECK(capture.begin_count == 0u);
    CHECK(step_instruction(&observed));
    CHECK(capture.begin_count == 1u);
    return true;
}

static bool test_instruction_and_cycle_step_traces_match(void) {
    static const uint8_t program[] = {
        0xa9u, 0x42u,       /* LDA #$42 */
        0xeeu, 0x34u, 0x12u,/* INC $1234 */
        0x0bu, 0x7fu        /* deferred ANC #$7f */
    };
    c64_t instruction_machine;
    c64_t cycle_machine;
    observer_capture instruction_capture = {0};
    observer_capture cycle_capture = {0};
    char error[256];
    size_t i;

    CHECK(prepare_machine(
        &instruction_machine, program, sizeof(program)));
    CHECK(prepare_machine(&cycle_machine, program, sizeof(program)));
    instruction_machine.bus.ram[0x1234u] = 0x40u;
    cycle_machine.bus.ram[0x1234u] = 0x40u;
    c64_set_cpu_observer(
        &instruction_machine, &test_observer, &instruction_capture);
    c64_set_cpu_observer(
        &cycle_machine, &test_observer, &cycle_capture);
    for (i = 0u; i < 3u; ++i) {
        CHECK(c64_step_instruction(
            &instruction_machine, error, sizeof(error)));
    }
    while (cycle_capture.complete_count < 3u) {
        CHECK(c64_step_cycles(
            &cycle_machine, 1u, error, sizeof(error)));
    }
    CHECK(instruction_capture.begin_count ==
          cycle_capture.begin_count);
    CHECK(instruction_capture.access_count ==
          cycle_capture.access_count);
    CHECK(instruction_capture.complete_count ==
          cycle_capture.complete_count);
    CHECK(memcmp(
        instruction_capture.begins,
        cycle_capture.begins,
        instruction_capture.begin_count *
            sizeof(instruction_capture.begins[0])) == 0);
    CHECK(memcmp(
        instruction_capture.accesses,
        cycle_capture.accesses,
        instruction_capture.access_count *
            sizeof(instruction_capture.accesses[0])) == 0);
    return true;
}

typedef bool (*test_fn)(void);

int main(void) {
    static const struct {
        const char *name;
        test_fn fn;
    } tests[] = {
        { "prestate_and_actual_bytes", test_prestate_and_actual_bytes },
        { "self_modifying_and_read_write_rmw", test_self_modifying_and_read_write_rmw },
        { "stack_and_io_dummy_kinds", test_stack_and_io_dummy_kinds },
        { "irq_and_nmi_records", test_irq_and_nmi_records },
        { "undocumented_micro_and_deferred_replay", test_undocumented_micro_and_deferred_replay },
        { "null_observer_and_reset_vector_exclusion", test_null_observer_and_reset_vector_exclusion },
        { "instruction_and_cycle_step_traces_match", test_instruction_and_cycle_step_traces_match },
    };
    size_t i;

    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].fn()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 1;
        }
    }
    printf("test_c64_cpu_observer: ok (%u cases)\n",
           (unsigned)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
