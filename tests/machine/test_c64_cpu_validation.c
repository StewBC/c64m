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

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "%s: expected %llu, got %llu\n", name, (unsigned long long)expected, (unsigned long long)actual);
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
    c64_config config = { 0 };
    char error[256];

    c64_init(machine);
    config.video_standard = C64_VIDEO_STANDARD_NTSC;
    c64_set_config(machine, &config);
    expect_true("install synthetic ROMs", c64_install_roms(machine, roms, error, sizeof(error)));
    expect_true("reset machine", c64_reset(machine, error, sizeof(error)));
}

static void reset_machine_standard(
    c64_t *machine,
    const c64_rom_set *roms,
    c64_video_standard standard)
{
    c64_config config = { 0 };
    char error[256];

    c64_init(machine);
    config.video_standard = standard;
    c64_set_config(machine, &config);
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

static void step_machine_cycles(c64_t *machine, size_t count) {
    char error[256];
    size_t i;

    for (i = 0; i < count; i++) {
        expect_true("step cycle", c64_step_cycle(machine, error, sizeof(error)));
    }
}

/* Phase 0 timing-fixture sample. Keep this test-only: it records the observable
   machine boundary that later Phi2-scheduler phases must preserve, without
   exposing live machine state to runtime or frontend. */
typedef struct timing_sample {
    uint64_t master_cycle_before;
    uint64_t master_cycle_after;
    uint64_t cpu_cycles_before;
    uint64_t cpu_cycles_after;
    uint32_t raster_line_before;
    uint32_t cycle_in_line_before;
    bool ba_before;
    bool pending_before;
    size_t elapsed_before;
} timing_sample;

static void capture_timing_step(c64_t *machine, timing_sample *sample) {
    char error[256];

    sample->master_cycle_before = machine->clock.cycle;
    sample->cpu_cycles_before = machine->clock.cpu_cycles;
    sample->raster_line_before = machine->vic.timing.raster_line;
    sample->cycle_in_line_before = machine->vic.timing.cycle_in_line;
    sample->ba_before = vicii_ba_active(&machine->vic, machine->clock.cycle);
    sample->pending_before = machine->pending_cpu_trace_active || machine->cpu.micro_active;
    sample->elapsed_before = machine->pending_cpu_elapsed;

    expect_true("captured step cycle", c64_step_cycle(machine, error, sizeof(error)));

    sample->master_cycle_after = machine->clock.cycle;
    sample->cpu_cycles_after = machine->clock.cpu_cycles;
}

static c64_cpu_snapshot snapshot(const c64_t *machine) {
    c64_cpu_snapshot out;
    c64_copy_cpu_snapshot(machine, &out);
    return out;
}

static void expect_cpu_snapshots_equal(
    const char *name,
    const c64_cpu_snapshot *expected,
    const c64_cpu_snapshot *actual)
{
    expect_u16(name, expected->pc, actual->pc);
    expect_u8(name, expected->a, actual->a);
    expect_u8(name, expected->x, actual->x);
    expect_u8(name, expected->y, actual->y);
    expect_u8(name, expected->sp, actual->sp);
    expect_u8(name, expected->p, actual->p);
}

/* The PAL current-model timing baseline defines bad-line BA assertion at
   raster $33/cycle 12.  Keep this setup shared by migrated-family validation
   so the contention path exercises the documented fixture, not a synthetic
   BA flag. */
static void arm_pal_badline_fixture(c64_t *machine) {
    vicii_write_register(&machine->vic, 0xd011, 0x13u);
    machine->vic.timing.raster_line = 0x33u;
    machine->vic.timing.cycle_in_line = 12u;
}

static void expect_contended_trace_matches_normal(
    const char *name,
    const c64_cpu_instruction_trace *normal,
    const c64_cpu_instruction_trace *contended)
{
    size_t i;
    bool delayed = false;

    expect_u16(name, normal->opcode_pc, contended->opcode_pc);
    expect_u64(name, normal->event_count, contended->event_count);
    expect_u64(name, normal->total_cycles, contended->total_cycles);
    for (i = 0; i < normal->event_count; i++) {
        const c64_cpu_bus_event *expected = &normal->events[i];
        const c64_cpu_bus_event *actual = &contended->events[i];

        expect_u8(name, expected->cycle_offset, actual->cycle_offset);
        expect_u8(name, (uint8_t)expected->kind, (uint8_t)actual->kind);
        expect_u8(name, (uint8_t)expected->access_kind, (uint8_t)actual->access_kind);
        expect_u16(name, expected->address, actual->address);
        expect_u8(name, expected->value, actual->value);
        expect_u8(name, expected->is_io, actual->is_io);
        expect_u8(name, expected->record_write_history, actual->record_write_history);
        expect_true(name, actual->absolute_cycle >= expected->absolute_cycle);
        if (actual->absolute_cycle > expected->absolute_cycle) {
            delayed = true;
        }
    }
    expect_true(name, delayed);
}

static void seed_migrated_family_memory(c64_t *machine) {
    machine->bus.ram[0x0010] = 0x01u;
    machine->bus.ram[0x0011] = 0x01u;
    machine->bus.ram[0x0024] = 0x34u;
    machine->bus.ram[0x0025] = 0x12u;
    machine->bus.ram[0x0030] = 0xffu;
    machine->bus.ram[0x0031] = 0x12u;
    machine->bus.ram[0x1234] = 0x01u;
    machine->bus.ram[0x1235] = 0xc0u;
    machine->bus.ram[0x1200] = 0xaau;
    machine->bus.ram[0x1300] = 0x01u;
    machine->bus.ram[0x20ff] = 0x10u;
    machine->bus.ram[0x2000] = 0xe1u;
}

static void validate_migrated_family_on_pal_badline(
    const char *name,
    const uint8_t *program,
    size_t program_size,
    size_t setup_instructions,
    uint16_t expected_pc,
    uint8_t expected_a,
    uint16_t expected_memory_address,
    uint8_t expected_memory_value)
{
    c64_rom_set roms;
    c64_t normal;
    c64_t contended;
    c64_cpu_snapshot normal_cpu;
    c64_cpu_snapshot contended_cpu;
    c64_cpu_instruction_trace normal_trace;
    c64_cpu_instruction_trace contended_trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, program_size);
    reset_machine_standard(&normal, &roms, C64_VIDEO_STANDARD_PAL);
    reset_machine_standard(&contended, &roms, C64_VIDEO_STANDARD_PAL);
    seed_migrated_family_memory(&normal);
    seed_migrated_family_memory(&contended);
    c64_set_cpu_trace_enabled(&normal, true);
    c64_set_cpu_trace_enabled(&contended, true);

    step_machine(&normal, setup_instructions);
    step_machine(&contended, setup_instructions);
    /* Keep the non-contended execution at the same raster position. */
    vicii_write_register(&normal.vic, 0xd011, 0x03u);
    normal.vic.timing.raster_line = 0x33u;
    normal.vic.timing.cycle_in_line = 12u;
    arm_pal_badline_fixture(&contended);

    step_machine(&normal, 1);
    /* c64_step_instruction() deliberately finishes an already-active micro
       instruction before starting its requested step.  This fixture needs to
       preserve the opcode-fetch boundary, so complete the active instruction
       through the public cycle API instead. */
    contended.instruction_complete = false;
    step_machine_cycles(&contended, 1); /* opcode fetch; VIC asserts BA. */
    while (!contended.instruction_complete) {
        step_machine_cycles(&contended, 1);
    }

    normal_cpu = snapshot(&normal);
    contended_cpu = snapshot(&contended);
    expect_cpu_snapshots_equal(name, &normal_cpu, &contended_cpu);
    expect_u16(name, expected_pc, normal_cpu.pc);
    expect_u8(name, expected_a, normal_cpu.a);
    expect_u8(name, expected_memory_value,
        c64_bus_read(&normal.bus, expected_memory_address));
    expect_u8(name, expected_memory_value,
        c64_bus_read(&contended.bus, expected_memory_address));
    c64_debug_copy_last_cpu_trace(&normal, &normal_trace);
    c64_debug_copy_last_cpu_trace(&contended, &contended_trace);
    expect_contended_trace_matches_normal(name, &normal_trace, &contended_trace);
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

static void test_ram_write_history(void) {
    static const uint8_t program[] = {
        0xa9, 0x55,       /* LDA #$55 */
        0x8d, 0x34, 0x12, /* STA $1234 at $E002 */
        0x8d, 0x34, 0x12, /* STA $1234 at $E005 */
        0x8d, 0x34, 0x12, /* STA $1234 at $E008 */
        0x8d, 0x34, 0x12, /* STA $1234 at $E00B */
        0x8d, 0x34, 0x12  /* STA $1234 at $E00E */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);

    expect_u64("initial RAM write history", 0, c64_debug_read_write_history(&machine, 0x1234));

    step_machine(&machine, 2);
    expect_u64("first RAM write history", 0xe002, c64_debug_read_write_history(&machine, 0x1234));

    step_machine(&machine, 1);
    expect_u64(
        "second RAM write history",
        0xe002e005,
        c64_debug_read_write_history(&machine, 0x1234));

    step_machine(&machine, 3);
    expect_u64(
        "RAM write history drops oldest writer",
        0xe005e008e00be00eULL,
        c64_debug_read_write_history(&machine, 0x1234));
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

static void test_sta_abs_bus_event_trace(void) {
    static const uint8_t program[] = {
        0xa9, 0x5a,       /* LDA #$5a */
        0x8d, 0x34, 0x12  /* STA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    step_machine(&machine, 1);
    expect_u64("copy trace event count", 4, c64_debug_copy_last_cpu_trace(&machine, &trace));

    expect_u16("trace opcode pc", (uint16_t)(TEST_RESET_VECTOR + 2), trace.opcode_pc);
    expect_u64("trace total cycles", 4, trace.total_cycles);
    expect_u8("opcode fetch kind", C64_CPU_BUS_EVENT_READ, (uint8_t)trace.events[0].kind);
    expect_u8("opcode fetch access", C6510_BUS_ACCESS_OPCODE_FETCH,
        (uint8_t)trace.events[0].access_kind);
    expect_u8("opcode fetch offset", 0, trace.events[0].cycle_offset);
    expect_u16("opcode fetch address", (uint16_t)(TEST_RESET_VECTOR + 2), trace.events[0].address);
    expect_u8("addr lo kind", C64_CPU_BUS_EVENT_READ, (uint8_t)trace.events[1].kind);
    expect_u8("addr lo access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("addr lo offset", 1, trace.events[1].cycle_offset);
    expect_u16("addr lo address", (uint16_t)(TEST_RESET_VECTOR + 3), trace.events[1].address);
    expect_u8("addr hi kind", C64_CPU_BUS_EVENT_READ, (uint8_t)trace.events[2].kind);
    expect_u8("addr hi access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("addr hi offset", 2, trace.events[2].cycle_offset);
    expect_u16("addr hi address", (uint16_t)(TEST_RESET_VECTOR + 4), trace.events[2].address);
    expect_u8("sta write kind", C64_CPU_BUS_EVENT_WRITE, (uint8_t)trace.events[3].kind);
    expect_u8("sta write access", C6510_BUS_ACCESS_DATA_WRITE,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("sta write offset", 3, trace.events[3].cycle_offset);
    expect_u16("sta write address", 0x1234, trace.events[3].address);
    expect_u8("sta write value", 0x5a, trace.events[3].value);
    expect_u8("sta write not io", 0, trace.events[3].is_io);
}

static void test_cpu_trace_tags_dummy_stack_and_vector_accesses(void) {
    static const uint8_t program[] = {
        0x08,             /* PHP */
        0x00, 0xea        /* BRK, padding byte */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    expect_u64("php trace events", 3, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("php opcode access", C6510_BUS_ACCESS_OPCODE_FETCH,
        (uint8_t)trace.events[0].access_kind);
    expect_u8("php dummy access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("php stack write access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace.events[2].access_kind);

    step_machine(&machine, 1);
    expect_u64("brk trace events", 7, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("brk opcode access", C6510_BUS_ACCESS_OPCODE_FETCH,
        (uint8_t)trace.events[0].access_kind);
    expect_u8("brk padding operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("brk PC high stack access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("brk PC low stack access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("brk flags stack access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace.events[4].access_kind);
    expect_u8("brk vector low access", C6510_BUS_ACCESS_VECTOR_READ,
        (uint8_t)trace.events[5].access_kind);
    expect_u8("brk vector high access", C6510_BUS_ACCESS_VECTOR_READ,
        (uint8_t)trace.events[6].access_kind);
}

static void test_cpu_trace_tags_rmw_accesses(void) {
    static const uint8_t program[] = {
        0x0e, 0x34, 0x12  /* ASL $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1234] = 0x41u;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    expect_u64("asl trace events", 6, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("asl data read access", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("asl dummy write access", C6510_BUS_ACCESS_RMW_DUMMY_WRITE,
        (uint8_t)trace.events[4].access_kind);
    expect_u8("asl final write access", C6510_BUS_ACCESS_DATA_WRITE,
        (uint8_t)trace.events[5].access_kind);
    expect_u8("asl result", 0x82u, machine.bus.ram[0x1234]);
}

static void test_cpu_trace_tags_branch_and_stack_reads(void) {
    static const uint8_t program[] = {
        0xd0, 0x00,       /* BNE +0 (taken after reset) */
        0x68              /* PLA */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x0101] = 0x5au;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    expect_u64("branch trace events", 3, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("branch opcode access", C6510_BUS_ACCESS_OPCODE_FETCH,
        (uint8_t)trace.events[0].access_kind);
    expect_u8("branch displacement access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("branch taken dummy access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);

    step_machine(&machine, 1);
    expect_u64("pla trace events", 4, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("pla dummy pc access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("pla dummy stack access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("pla stack read access", C6510_BUS_ACCESS_STACK_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("pla value", 0x5au, snapshot(&machine).a);
}

static void expect_interrupt_trace(
    const char *name,
    const c64_cpu_instruction_trace *trace,
    uint16_t vector)
{
    expect_u64(name, 7, trace->event_count);
    expect_u8("interrupt dummy fetch access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace->events[0].access_kind);
    expect_u8("interrupt dummy stack access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace->events[1].access_kind);
    expect_u8("interrupt PC high stack access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace->events[2].access_kind);
    expect_u8("interrupt PC low stack access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace->events[3].access_kind);
    expect_u8("interrupt flags stack access", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace->events[4].access_kind);
    expect_u8("interrupt vector low access", C6510_BUS_ACCESS_VECTOR_READ,
        (uint8_t)trace->events[5].access_kind);
    expect_u16("interrupt vector low address", vector, trace->events[5].address);
    expect_u8("interrupt vector high access", C6510_BUS_ACCESS_VECTOR_READ,
        (uint8_t)trace->events[6].access_kind);
    expect_u16("interrupt vector high address", (uint16_t)(vector + 1u), trace->events[6].address);
}

static void test_cpu_trace_tags_irq_and_nmi_accesses(void) {
    static const uint8_t program[] = {
        0x58,             /* CLI */
        0xea              /* NOP */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);
    step_machine(&machine, 1); /* CLI enables IRQ after the deferred boundary. */
    cia_write_register(&machine.cia1, 0xdc0d, 0x81u);
    cia_set_interrupt_source(&machine.cia1, 0x01u);
    step_machine(&machine, 1); /* NOP consumes CLI's one-instruction IRQ defer. */
    step_machine(&machine, 1);
    c64_debug_copy_last_cpu_trace(&machine, &trace);
    expect_interrupt_trace("irq trace events", &trace, 0xfffeu);

    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);
    c64_restore(&machine);
    step_machine(&machine, 1);
    c64_debug_copy_last_cpu_trace(&machine, &trace);
    expect_interrupt_trace("nmi trace events", &trace, 0xfffau);
}

static void test_cpu_trace_tags_page_cross_and_rti_stack_reads(void) {
    static const uint8_t page_cross_program[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xbd, 0xff, 0x12  /* LDA $12ff,X */
    };
    static const uint8_t rti_program[] = {
        0x40              /* RTI */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, page_cross_program, sizeof(page_cross_program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1200] = 0x11u;
    machine.bus.ram[0x1300] = 0x5au;
    c64_set_cpu_trace_enabled(&machine, true);
    step_machine(&machine, 1);
    step_machine(&machine, 1);
    expect_u64("page-cross trace events", 5, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("page-cross opcode access", C6510_BUS_ACCESS_OPCODE_FETCH,
        (uint8_t)trace.events[0].access_kind);
    expect_u8("page-cross low operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("page-cross high operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("page-cross dummy access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u16("page-cross dummy address", 0x1200u, trace.events[3].address);
    expect_u8("page-cross final data access", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[4].access_kind);
    expect_u16("page-cross final data address", 0x1300u, trace.events[4].address);
    expect_u8("page-cross loaded value", 0x5au, snapshot(&machine).a);

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, rti_program, sizeof(rti_program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x0101] = 0x20u;
    machine.bus.ram[0x0102] = 0x34u;
    machine.bus.ram[0x0103] = 0x12u;
    c64_set_cpu_trace_enabled(&machine, true);
    step_machine(&machine, 1);
    expect_u64("rti trace events", 6, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("rti dummy pc access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("rti dummy stack access", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("rti flags stack access", C6510_BUS_ACCESS_STACK_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("rti PC low stack access", C6510_BUS_ACCESS_STACK_READ,
        (uint8_t)trace.events[4].access_kind);
    expect_u8("rti PC high stack access", C6510_BUS_ACCESS_STACK_READ,
        (uint8_t)trace.events[5].access_kind);
    expect_u16("rti restored PC", 0x1234u, snapshot(&machine).pc);
}

static void test_microcycle_zero_page_load_store_trace(void) {
    static const uint8_t program[] = {
        0xa9, 0x5a,       /* LDA #$5a */
        0x85, 0x80,       /* STA $80 */
        0xa5, 0x80        /* LDA $80 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    step_machine(&machine, 1);
    expect_u64("zp store trace events", 3, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("zp store operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("zp store data access", C6510_BUS_ACCESS_DATA_WRITE,
        (uint8_t)trace.events[2].access_kind);
    expect_u16("zp store address", 0x0080u, trace.events[2].address);
    expect_u8("zp store value", 0x5au, machine.bus.ram[0x0080]);

    step_machine(&machine, 1);
    expect_u64("zp load trace events", 3, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("zp load operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("zp load data access", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u16("zp load address", 0x0080u, trace.events[2].address);
    expect_u8("zp load value", 0x5au, snapshot(&machine).a);
}

static void test_microcycle_jmp_trace(void) {
    static const uint8_t program[] = {
        0x4c, 0x05, 0xe0, /* JMP $e005 */
        0xea, 0xea, 0xea
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    expect_u64("jmp trace events", 3, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("jmp opcode access", C6510_BUS_ACCESS_OPCODE_FETCH,
        (uint8_t)trace.events[0].access_kind);
    expect_u8("jmp low operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("jmp high operand access", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u16("jmp destination", 0xe005u, snapshot(&machine).pc);
}

static void test_microcycle_branch_page_cross_trace(void) {
    static const uint8_t program[] = {
        0xd0, 0x02,       /* BNE from $e0fd: target $e101 */
        0xea, 0xea, 0xea
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, 0xe0fdu);
    copy_to_kernal(&roms, 0xe0fdu, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    expect_u64("branch page-cross trace events", 4,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("branch page-cross operand", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("branch page-cross first dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u16("branch page-cross first dummy address", 0xe0ffu, trace.events[2].address);
    expect_u8("branch page-cross second dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u16("branch page-cross second dummy address", 0xe001u, trace.events[3].address);
    expect_u16("branch page-cross target", 0xe101u, snapshot(&machine).pc);
}

static void test_microcycle_jsr_rts_trace(void) {
    static const uint8_t program[] = {
        0x20, 0x05, 0xe0, /* JSR $e005 */
        0xea, 0xea,
        0x60              /* RTS */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    expect_u64("jsr trace events", 6, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("jsr low operand", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("jsr stack dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("jsr PC high push", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("jsr PC low push", C6510_BUS_ACCESS_STACK_WRITE,
        (uint8_t)trace.events[4].access_kind);
    expect_u8("jsr high operand", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[5].access_kind);
    expect_u16("jsr destination", 0xe005u, snapshot(&machine).pc);

    step_machine(&machine, 1);
    expect_u64("rts trace events", 6, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("rts stack low", C6510_BUS_ACCESS_STACK_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u8("rts stack high", C6510_BUS_ACCESS_STACK_READ,
        (uint8_t)trace.events[4].access_kind);
    expect_u8("rts final dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[5].access_kind);
    expect_u16("rts destination", 0xe003u, snapshot(&machine).pc);
}

static void test_microcycle_indexed_zero_page_trace(void) {
    static const uint8_t program[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xb5, 0x10        /* LDA $10,X */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x0011] = 0x5au;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    step_machine(&machine, 1);
    expect_u64("indexed zero-page trace events", 4,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("indexed zero-page operand", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("indexed zero-page dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u16("indexed zero-page dummy address", 0x0010u, trace.events[2].address);
    expect_u8("indexed zero-page data", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u16("indexed zero-page data address", 0x0011u, trace.events[3].address);
    expect_u8("indexed zero-page value", 0x5au, snapshot(&machine).a);
}

static void test_microcycle_absolute_read_operation_trace(void) {
    static const uint8_t program[] = {
        0xa9, 0x40,       /* LDA #$40 */
        0x6d, 0x34, 0x12, /* ADC $1234 */
        0x2c, 0x35, 0x12, /* BIT $1235 */
        0x24, 0x10        /* BIT $10 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1234] = 0x01u;
    machine.bus.ram[0x1235] = 0xc0u;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 2);
    expect_u64("absolute adc trace events", 4,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("absolute adc low operand", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8("absolute adc high operand", C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8("absolute adc data access", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u16("absolute adc data address", 0x1234u, trace.events[3].address);
    expect_u8("absolute adc result", 0x41u, snapshot(&machine).a);

    step_machine(&machine, 1);
    expect_u8("absolute bit negative flag", 1u, machine.cpu.cpu.N);
    expect_u8("absolute bit overflow flag", 1u, machine.cpu.cpu.V);
    expect_u8("absolute bit clears zero", 0u, machine.cpu.cpu.Z);

    machine.bus.ram[0x0010] = 0x80u;
    step_machine(&machine, 1);
    expect_u8("zero-page bit negative flag", 1u, machine.cpu.cpu.N);
    expect_u8("zero-page bit clears overflow", 0u, machine.cpu.cpu.V);
    expect_u8("zero-page bit sets zero", 1u, machine.cpu.cpu.Z);
}

static void test_microcycle_indexed_read_operation_trace(void) {
    static const uint8_t program[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xa9, 0x40,       /* LDA #$40 */
        0x7d, 0xff, 0x12  /* ADC $12ff,X */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1200] = 0xffu;
    machine.bus.ram[0x1300] = 0x01u;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 3);
    expect_u64("indexed adc trace events", 5,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("indexed adc page-cross dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u16("indexed adc page-cross dummy address", 0x1200u, trace.events[3].address);
    expect_u8("indexed adc final data", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[4].access_kind);
    expect_u16("indexed adc final data address", 0x1300u, trace.events[4].address);
    expect_u8("indexed adc result", 0x41u, snapshot(&machine).a);
}

static void test_microcycle_indirect_read_write_trace(void) {
    static const uint8_t program[] = {
        0xa2, 0x04,       /* LDX #$04 */
        0xa1, 0x20,       /* LDA ($20,X) */
        0xa0, 0x01,       /* LDY #$01 */
        0x71, 0x30,       /* ADC ($30),Y */
        0x91, 0x30        /* STA ($30),Y */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x0024] = 0x34u;
    machine.bus.ram[0x0025] = 0x12u;
    machine.bus.ram[0x1234] = 0x40u;
    machine.bus.ram[0x0030] = 0xffu;
    machine.bus.ram[0x0031] = 0x12u;
    machine.bus.ram[0x1200] = 0xffu;
    machine.bus.ram[0x1300] = 0x01u;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 2);
    expect_u64("x-indirect load trace events", 6,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("x-indirect dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u16("x-indirect dummy address", 0x0020u, trace.events[2].address);
    expect_u16("x-indirect pointer low", 0x0024u, trace.events[3].address);
    expect_u16("x-indirect pointer high", 0x0025u, trace.events[4].address);
    expect_u16("x-indirect final data", 0x1234u, trace.events[5].address);
    expect_u8("x-indirect loaded value", 0x40u, snapshot(&machine).a);

    step_machine(&machine, 2);
    expect_u64("indirect-y adc trace events", 6,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("indirect-y dummy", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[4].access_kind);
    expect_u16("indirect-y dummy address", 0x1200u, trace.events[4].address);
    expect_u16("indirect-y final data", 0x1300u, trace.events[5].address);
    expect_u8("indirect-y adc result", 0x41u, snapshot(&machine).a);

    step_machine(&machine, 1);
    expect_u64("indirect-y store trace events", 6,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("indirect-y store final access", C6510_BUS_ACCESS_DATA_WRITE,
        (uint8_t)trace.events[5].access_kind);
    expect_u16("indirect-y store final address", 0x1300u, trace.events[5].address);
    expect_u8("indirect-y store value", 0x41u, machine.bus.ram[0x1300]);
}

static void test_microcycle_indexed_rmw_and_indirect_jump_trace(void) {
    static const uint8_t program[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xfe, 0xff, 0x12, /* INC $12ff,X */
        0x6c, 0xff, 0x20  /* JMP ($20ff) */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1200] = 0xaau;
    machine.bus.ram[0x1300] = 0x41u;
    machine.bus.ram[0x20ff] = 0x10u;
    machine.bus.ram[0x2000] = 0xe1u; /* NMOS indirect-JMP page-wrap high byte. */
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 2);
    expect_u64("indexed rmw trace events", 7,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u8("indexed rmw dummy read", C6510_BUS_ACCESS_DUMMY_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u16("indexed rmw dummy read address", 0x1200u, trace.events[3].address);
    expect_u8("indexed rmw data read", C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[4].access_kind);
    expect_u16("indexed rmw data read address", 0x1300u, trace.events[4].address);
    expect_u8("indexed rmw dummy write", C6510_BUS_ACCESS_RMW_DUMMY_WRITE,
        (uint8_t)trace.events[5].access_kind);
    expect_u8("indexed rmw final write", C6510_BUS_ACCESS_DATA_WRITE,
        (uint8_t)trace.events[6].access_kind);
    expect_u8("indexed rmw result", 0x42u, machine.bus.ram[0x1300]);

    step_machine(&machine, 1);
    expect_u64("indirect jump trace events", 5,
        c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u16("indirect jump pointer low", 0x20ffu, trace.events[3].address);
    expect_u16("indirect jump wrapped pointer high", 0x2000u, trace.events[4].address);
    expect_u16("indirect jump destination", 0xe110u, snapshot(&machine).pc);
}

static void test_migrated_families_match_normal_trace_under_pal_badline(void) {
    static const uint8_t absolute_read[] = {
        0xa9, 0x40,       /* LDA #$40 */
        0x6d, 0x34, 0x12  /* ADC $1234 */
    };
    static const uint8_t zero_page_indexed_read[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xa9, 0x40,       /* LDA #$40 */
        0x75, 0x0f        /* ADC $0f,X */
    };
    static const uint8_t absolute_indexed_read[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xa9, 0x40,       /* LDA #$40 */
        0x7d, 0xff, 0x12  /* ADC $12ff,X */
    };
    static const uint8_t x_indirect_read[] = {
        0xa2, 0x04,       /* LDX #$04 */
        0xa9, 0x40,       /* LDA #$40 */
        0x61, 0x20        /* ADC ($20,X) */
    };
    static const uint8_t indirect_y_read[] = {
        0xa0, 0x01,       /* LDY #$01 */
        0xa9, 0x40,       /* LDA #$40 */
        0x71, 0x30        /* ADC ($30),Y */
    };
    static const uint8_t absolute_bit[] = {
        0xa9, 0x40,       /* LDA #$40 */
        0x2c, 0x35, 0x12  /* BIT $1235 */
    };
    static const uint8_t indexed_rmw[] = {
        0xa2, 0x01,       /* LDX #$01 */
        0xfe, 0xff, 0x12  /* INC $12ff,X */
    };
    static const uint8_t indirect_y_store[] = {
        0xa0, 0x01,       /* LDY #$01 */
        0xa9, 0x5a,       /* LDA #$5a */
        0x91, 0x30        /* STA ($30),Y */
    };
    static const uint8_t indirect_jump[] = {
        0x6c, 0xff, 0x20  /* JMP ($20ff), NMOS page-wrap high byte */
    };

    validate_migrated_family_on_pal_badline("absolute read under badline",
        absolute_read, sizeof(absolute_read), 1, 0xe005u, 0x41u, 0x1234u, 0x01u);
    validate_migrated_family_on_pal_badline("zero-page indexed read under badline",
        zero_page_indexed_read, sizeof(zero_page_indexed_read), 2, 0xe006u,
        0x41u, 0x0010u, 0x01u);
    validate_migrated_family_on_pal_badline("absolute indexed read under badline",
        absolute_indexed_read, sizeof(absolute_indexed_read), 2, 0xe007u,
        0x41u, 0x1300u, 0x01u);
    validate_migrated_family_on_pal_badline("x-indirect read under badline",
        x_indirect_read, sizeof(x_indirect_read), 2, 0xe006u, 0x41u, 0x1234u, 0x01u);
    validate_migrated_family_on_pal_badline("indirect-y read under badline",
        indirect_y_read, sizeof(indirect_y_read), 2, 0xe006u, 0x41u, 0x1300u, 0x01u);
    validate_migrated_family_on_pal_badline("absolute BIT under badline",
        absolute_bit, sizeof(absolute_bit), 1, 0xe005u, 0x40u, 0x1235u, 0xc0u);
    validate_migrated_family_on_pal_badline("indexed RMW under badline",
        indexed_rmw, sizeof(indexed_rmw), 1, 0xe005u, 0x00u, 0x1300u, 0x02u);
    validate_migrated_family_on_pal_badline("indirect-y store under badline",
        indirect_y_store, sizeof(indirect_y_store), 2, 0xe006u, 0x5au, 0x1300u, 0x5au);
    validate_migrated_family_on_pal_badline("indirect jump under badline",
        indirect_jump, sizeof(indirect_jump), 0, 0xe110u, 0x00u, 0x20ffu, 0x10u);
}

static void test_practical_undocumented_microcycle_semantics(void) {
    static const uint8_t program[] = {
        0xa9, 0x01, 0x18, 0x07, 0x10, /* LDA #1; CLC; SLO $10 */
        0xa9, 0xff, 0x18, 0x27, 0x11, /* LDA #ff; CLC; RLA $11 */
        0xa9, 0x80, 0x18, 0x47, 0x12, /* LDA #80; CLC; SRE $12 */
        0xa9, 0x10, 0x18, 0x67, 0x13, /* LDA #10; CLC; RRA $13 */
        0xa9, 0x40, 0xc7, 0x14,       /* LDA #40; DCP $14 */
        0xa9, 0x40, 0x38, 0xe7, 0x15, /* LDA #40; SEC; ISC $15 */
        0xa7, 0x20,                   /* LAX $20 */
        0xa9, 0x5a, 0xa2, 0x0f, 0x87, 0x21 /* LDA; LDX; SAX $21 */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x0010] = 0x80u;
    machine.bus.ram[0x0011] = 0x80u;
    machine.bus.ram[0x0012] = 0x01u;
    machine.bus.ram[0x0013] = 0x02u;
    machine.bus.ram[0x0014] = 0x41u;
    machine.bus.ram[0x0015] = 0x0fu;
    machine.bus.ram[0x0020] = 0x80u;
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 3);
    expect_u8("SLO writes shifted value", 0x00u, machine.bus.ram[0x0010]);
    expect_u8("SLO ORs accumulator", 0x01u, snapshot(&machine).a);
    expect_u8("SLO carry", 1u, machine.cpu.cpu.C);
    step_machine(&machine, 3);
    expect_u8("RLA writes rotated value", 0x00u, machine.bus.ram[0x0011]);
    expect_u8("RLA ANDs accumulator", 0x00u, snapshot(&machine).a);
    expect_u8("RLA carry", 1u, machine.cpu.cpu.C);
    step_machine(&machine, 3);
    expect_u8("SRE writes shifted value", 0x00u, machine.bus.ram[0x0012]);
    expect_u8("SRE XORs accumulator", 0x80u, snapshot(&machine).a);
    step_machine(&machine, 3);
    expect_u8("RRA writes rotated value", 0x01u, machine.bus.ram[0x0013]);
    expect_u8("RRA adds accumulator", 0x11u, snapshot(&machine).a);
    step_machine(&machine, 2);
    expect_u8("DCP decrements memory", 0x40u, machine.bus.ram[0x0014]);
    expect_u8("DCP compare sets zero", 1u, machine.cpu.cpu.Z);
    step_machine(&machine, 3);
    expect_u8("ISC increments memory", 0x10u, machine.bus.ram[0x0015]);
    expect_u8("ISC subtracts accumulator", 0x30u, snapshot(&machine).a);
    step_machine(&machine, 1);
    expect_u8("LAX accumulator", 0x80u, snapshot(&machine).a);
    expect_u8("LAX index", 0x80u, snapshot(&machine).x);
    step_machine(&machine, 3);
    expect_u8("SAX stores A and X", 0x0au, machine.bus.ram[0x0021]);
}

static void test_practical_undocumented_y_indexed_forms(void) {
    /* LAX abs,Y (0xBF) and SAX zp,Y (0x97) are Y-indexed.  Their opcode
       columns collide with the X-indexed RMW templates, so a classifier that
       keys only on the low nibble wrongly indexes them with X.  Use X != Y and
       distinct targets so an X-indexed regression is observable. */
    static const uint8_t program[] = {
        0xa0, 0x03,       /* LDY #$03 */
        0xa2, 0x07,       /* LDX #$07 */
        0xbf, 0x34, 0x12, /* LAX $1234,Y -> reads $1237 (X-bug would read $123b) */
        0xa9, 0x5a,       /* LDA #$5a */
        0xa2, 0x0f,       /* LDX #$0f */
        0x97, 0x40        /* SAX $40,Y  -> writes $43   (X-bug would write $4f) */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1237] = 0x3cu; /* correct LAX abs,Y target */
    machine.bus.ram[0x123bu] = 0xffu; /* X-indexed decoy target */
    machine.bus.ram[0x0043] = 0x00u; /* correct SAX zp,Y target */
    machine.bus.ram[0x004f] = 0x11u; /* X-indexed decoy target */
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 3);
    expect_u8("LAX abs,Y loads accumulator via Y", 0x3cu, snapshot(&machine).a);
    expect_u8("LAX abs,Y loads index via Y", 0x3cu, snapshot(&machine).x);
    step_machine(&machine, 3);
    expect_u8("SAX zp,Y stores A&X via Y", 0x0au, machine.bus.ram[0x0043]);
    expect_u8("SAX zp,Y leaves X-indexed cell untouched", 0x11u, machine.bus.ram[0x004f]);
}

static void test_practical_undocumented_families_match_badline_trace(void) {
    static const uint8_t slo_absolute[] = {
        0xa9, 0x01,       /* LDA #$01 */
        0x0f, 0x34, 0x12  /* SLO $1234 */
    };
    static const uint8_t lax_x_indirect[] = {
        0xa2, 0x04,       /* LDX #$04 */
        0xa3, 0x20        /* LAX ($20,X) */
    };
    static const uint8_t sax_x_indirect[] = {
        0xa9, 0x5f,       /* LDA #$5f */
        0xa2, 0x04,       /* LDX #$04 */
        0x83, 0x20        /* SAX ($20,X) */
    };
    static const uint8_t dcp_indirect_y[] = {
        0xa0, 0x01,       /* LDY #$01 */
        0xa9, 0x40,       /* LDA #$40 */
        0xd3, 0x30        /* DCP ($30),Y */
    };

    validate_migrated_family_on_pal_badline("SLO absolute under badline",
        slo_absolute, sizeof(slo_absolute), 1, 0xe005u, 0x03u, 0x1234u, 0x02u);
    validate_migrated_family_on_pal_badline("LAX x-indirect under badline",
        lax_x_indirect, sizeof(lax_x_indirect), 1, 0xe004u, 0x01u, 0x1234u, 0x01u);
    validate_migrated_family_on_pal_badline("SAX x-indirect under badline",
        sax_x_indirect, sizeof(sax_x_indirect), 2, 0xe006u, 0x5fu, 0x1234u, 0x04u);
    validate_migrated_family_on_pal_badline("DCP indirect-y under badline",
        dcp_indirect_y, sizeof(dcp_indirect_y), 2, 0xe006u, 0x40u, 0x1300u, 0x00u);
}

static void test_migrated_rmw_writes_proceed_while_ba_is_low(void) {
    static const uint8_t program[] = {
        0xee, 0x34, 0x12  /* INC $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    uint64_t before_cpu_cycles;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1234] = 0x41u;

    step_machine_cycles(&machine, 4); /* opcode, operands, and data read */
    machine.vic.timing.ba_low_until_abs = machine.clock.cycle + 1000u;
    before_cpu_cycles = machine.clock.cpu_cycles;

    step_machine_cycles(&machine, 1); /* NMOS old-value dummy write */
    expect_u8("BA permits RMW dummy write", 0x41u, c64_bus_read(&machine.bus, 0x1234));
    expect_u64("BA RMW dummy write advances CPU", before_cpu_cycles + 1u,
        machine.clock.cpu_cycles);
    step_machine_cycles(&machine, 1); /* final modified write */
    expect_u8("BA permits RMW final write", 0x42u, c64_bus_read(&machine.bus, 0x1234));
}

static void test_sta_d020_applies_at_event_cycle(void) {
    static const uint8_t program[] = {
        0xa9, 0x0b,       /* LDA #$0b */
        0x8d, 0x20, 0xd0  /* STA $D020 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;
    uint64_t start_cycle;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine(&machine, 1);
    start_cycle = machine.clock.cycle;
    step_machine(&machine, 1);
    c64_debug_copy_last_cpu_trace(&machine, &trace);

    expect_u8("d020 write kind", C64_CPU_BUS_EVENT_WRITE, (uint8_t)trace.events[3].kind);
    expect_u16("d020 write address", 0xd020, trace.events[3].address);
    expect_u8("d020 write io", 1, trace.events[3].is_io);
    expect_u8("d020 final register", 0xfb, c64_bus_read(&machine.bus, 0xd020));
    expect_u64("d020 write absolute cycle", start_cycle + 3, trace.events[3].absolute_cycle);
    expect_u64("sta advances machine cycles", start_cycle + 4, machine.clock.cycle);
}

static void test_cpu_trace_disabled_leaves_debug_trace_empty(void) {
    static const uint8_t program[] = {
        0xa9, 0x5a
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);

    step_machine(&machine, 1);
    expect_u64("disabled trace event count", 0, c64_debug_copy_last_cpu_trace(&machine, &trace));
}

static void test_cycle_step_trace_enabled_records_bus_events(void) {
    static const uint8_t program[] = {
        0xa9, 0x5a,       /* LDA #$5a */
        0x8d, 0x34, 0x12  /* STA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    c64_set_cpu_trace_enabled(&machine, true);

    step_machine_cycles(&machine, 2);
    step_machine_cycles(&machine, 4);

    expect_u64("cycle trace event count", 4, c64_debug_copy_last_cpu_trace(&machine, &trace));
    expect_u16("cycle trace opcode pc", (uint16_t)(TEST_RESET_VECTOR + 2), trace.opcode_pc);
    expect_u8("cycle trace write kind", C64_CPU_BUS_EVENT_WRITE, (uint8_t)trace.events[3].kind);
    expect_u16("cycle trace write address", 0x1234, trace.events[3].address);
    expect_u8("cycle trace write value", 0x5a, trace.events[3].value);
}

static void test_ba_allows_pending_write_cycle(void) {
    static const uint8_t program[] = {
        0xa9, 0x5a,       /* LDA #$5a */
        0x8d, 0x34, 0x12  /* STA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    uint64_t before_cpu_cycles;
    uint64_t before_machine_cycles;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);

    step_machine_cycles(&machine, 2); /* LDA #$5a */
    step_machine_cycles(&machine, 3); /* STA opcode, low address, high address reads */
    expect_true("sta write pending", machine.pending_cpu_trace_active || machine.cpu.micro_active);
    expect_u8("ram before ba write", 0x00, c64_bus_read(&machine.bus, 0x1234));

    machine.vic.timing.ba_low_until_abs = machine.clock.cycle + 1000u;
    before_cpu_cycles = machine.clock.cpu_cycles;
    before_machine_cycles = machine.clock.cycle;

    step_machine_cycles(&machine, 1);

    expect_u8("ba write completed", 0x5a, c64_bus_read(&machine.bus, 0x1234));
    expect_u64("ba write advances cpu", before_cpu_cycles + 1, machine.clock.cpu_cycles);
    expect_u64("ba write advances machine", before_machine_cycles + 1, machine.clock.cycle);
    expect_true("sta trace complete", !machine.pending_cpu_trace_active && !machine.cpu.micro_active);
}

static void test_aec_blocks_pending_write_during_vic_phi2(void) {
    static const uint8_t program[] = {
        0xa9, 0x5a,       /* LDA #$5a */
        0x8d, 0x34, 0x12  /* STA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    uint64_t before_cpu_cycles;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine_standard(&machine, &roms, C64_VIDEO_STANDARD_PAL);

    step_machine_cycles(&machine, 2); /* LDA #$5a */
    step_machine_cycles(&machine, 3); /* STA opcode and address reads */
    expect_true("AEC fixture has STA write pending",
        machine.pending_cpu_trace_active || machine.cpu.micro_active);

    /* Put the pending write on a real bad-line c-access. RDY is not forced
       here: AEC alone must keep the CPU off the Phi2 bus. */
    vicii_write_register(&machine.vic, 0xd011, 0x13u);
    machine.vic.timing.raster_line = 0x33u;
    machine.vic.timing.cycle_in_line = 15u;
    before_cpu_cycles = machine.clock.cpu_cycles;

    expect_true("AEC low at c-access", !vicii_aec_active(&machine.vic));
    expect_true("RDY remains high without a preceding BA lead",
        vicii_rdy_active(&machine.vic, machine.clock.cycle));
    step_machine_cycles(&machine, 1);
    expect_u8("AEC blocks pending write", 0x00u, c64_bus_read(&machine.bus, 0x1234));
    expect_u64("AEC blocks CPU cycle", before_cpu_cycles, machine.clock.cpu_cycles);

    /* At cycle 55 the c-access run is over and both pins permit the write. */
    machine.vic.timing.cycle_in_line = 55u;
    step_machine_cycles(&machine, 1);
    expect_u8("AEC release completes pending write", 0x5au,
        c64_bus_read(&machine.bus, 0x1234));
    expect_u64("AEC release advances CPU", before_cpu_cycles + 1u,
        machine.clock.cpu_cycles);
}

static void test_ba_stalls_pending_read_cycle(void) {
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    uint64_t before_cpu_cycles;
    uint64_t before_machine_cycles;
    size_t before_elapsed;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1234] = 0x5a;

    step_machine_cycles(&machine, 3); /* opcode, low address, high address reads */
    expect_true("lda read pending", machine.pending_cpu_trace_active || machine.cpu.micro_active);

    machine.vic.timing.ba_low_until_abs = machine.clock.cycle + 1000u;
    before_cpu_cycles = machine.clock.cpu_cycles;
    before_machine_cycles = machine.clock.cycle;
    before_elapsed = machine.pending_cpu_elapsed;

    step_machine_cycles(&machine, 1);

    expect_u64("ba read stalls cpu", before_cpu_cycles, machine.clock.cpu_cycles);
    expect_u64("ba read advances machine", before_machine_cycles + 1, machine.clock.cycle);
    expect_u64("ba read keeps elapsed", before_elapsed, machine.pending_cpu_elapsed);
    expect_true("lda trace still pending", machine.pending_cpu_trace_active || machine.cpu.micro_active);
}

static void run_cpu_vic_interaction_trace(
    c64_video_standard standard,
    uint32_t raster_line,
    uint32_t cycle_in_line,
    bool bad_line,
    int sprite,
    uint64_t expected_first_cycle,
    uint64_t expected_data_cycle,
    uint32_t expected_final_line)
{
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    c64_cpu_instruction_trace trace;
    char name[96];

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine_standard(&machine, &roms, standard);
    machine.bus.ram[0x1234] = 0x5au;
    machine.clock.cycle = expected_first_cycle;
    machine.vic.timing.raster_line = raster_line;
    machine.vic.timing.cycle_in_line = cycle_in_line;

    if (bad_line) {
        vicii_write_register(&machine.vic, 0xd011, 0x13u);
    } else {
        vicii_write_register(&machine.vic, 0xd011, 0x03u); /* DEN=0: sprite only. */
        vicii_write_register(&machine.vic, 0xd015, (uint8_t)(1u << sprite));
        machine.vic.sprite_active[sprite] = true;
        machine.vic.sprite_visible[sprite] = true;
    }
    c64_set_cpu_trace_enabled(&machine, true);

    machine.instruction_complete = false;
    step_machine_cycles(&machine, 1); /* opcode fetch, then VIC derives BA. */
    while (!machine.instruction_complete) {
        step_machine_cycles(&machine, 1);
    }

    c64_debug_copy_last_cpu_trace(&machine, &trace);
    snprintf(name, sizeof(name), "%s interaction r%u/c%u",
        standard == C64_VIDEO_STANDARD_PAL ? "PAL" : "NTSC",
        raster_line, cycle_in_line);
    expect_u64(name, 4u, trace.event_count);
    expect_u64(name, expected_first_cycle, trace.events[0].absolute_cycle);
    expect_u64(name, expected_data_cycle, trace.events[1].absolute_cycle);
    expect_u64(name, expected_data_cycle + 1u, trace.events[2].absolute_cycle);
    expect_u64(name, expected_data_cycle + 2u, trace.events[3].absolute_cycle);
    expect_u8(name, C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[1].access_kind);
    expect_u8(name, C6510_BUS_ACCESS_OPERAND_READ,
        (uint8_t)trace.events[2].access_kind);
    expect_u8(name, C6510_BUS_ACCESS_DATA_READ,
        (uint8_t)trace.events[3].access_kind);
    expect_u8(name, 0x5au, snapshot(&machine).a);
    expect_u64(name, expected_final_line, machine.vic.timing.raster_line);
    expect_true(name, !vicii_ba_active(&machine.vic, machine.clock.cycle));
}

static void test_cpu_vic_pal_ntsc_interaction_traces(void) {
    /* Bad-line C accesses: opcode at 12, then the first operand read at 57
       after the schedule-derived C run [15,54] and three-cycle release margin. */
    run_cpu_vic_interaction_trace(C64_VIDEO_STANDARD_PAL, 0x33u, 12u, true, 0,
        12u, 57u, 0x33u);

    /* PAL sprite 0 data is scheduled at cycles 57/58; BA is derived at 54. */
    run_cpu_vic_interaction_trace(C64_VIDEO_STANDARD_PAL, 100u, 54u, false, 0,
        54u, 60u, 101u);

    /* NTSC sprite 0 uses the 6567R8 slot at cycles 59/60; BA derives at 56. */
    run_cpu_vic_interaction_trace(C64_VIDEO_STANDARD_NTSC, 100u, 56u, false, 0,
        56u, 62u, 101u);

    /* PAL sprite 3 is the cross-line case: data is at line N+1/cycle 0/1. */
    run_cpu_vic_interaction_trace(C64_VIDEO_STANDARD_PAL, 100u, 60u, false, 3,
        60u, 66u, 101u);
}

static void test_instruction_and_cycle_step_share_ba_arbiter(void) {
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t instruction_machine;
    c64_t cycle_machine;
    c64_cpu_snapshot instruction_cpu;
    c64_cpu_snapshot cycle_cpu;
    c64_cpu_instruction_trace instruction_trace;
    c64_cpu_instruction_trace cycle_trace;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&instruction_machine, &roms);
    reset_machine(&cycle_machine, &roms);
    instruction_machine.bus.ram[0x1234] = 0x5au;
    cycle_machine.bus.ram[0x1234] = 0x5au;
    instruction_machine.vic.timing.ba_low_until_abs = instruction_machine.clock.cycle + 2u;
    cycle_machine.vic.timing.ba_low_until_abs = cycle_machine.clock.cycle + 2u;
    c64_set_cpu_trace_enabled(&instruction_machine, true);
    c64_set_cpu_trace_enabled(&cycle_machine, true);

    step_machine(&instruction_machine, 1);
    while (!cycle_machine.instruction_complete) {
        step_machine_cycles(&cycle_machine, 1);
    }

    instruction_cpu = snapshot(&instruction_machine);
    cycle_cpu = snapshot(&cycle_machine);
    expect_u16("arbiter same PC", instruction_cpu.pc, cycle_cpu.pc);
    expect_u8("arbiter same accumulator", instruction_cpu.a, cycle_cpu.a);
    expect_u64("arbiter same master cycle", instruction_machine.clock.cycle,
        cycle_machine.clock.cycle);
    expect_u64("arbiter same CPU cycle count", instruction_machine.clock.cpu_cycles,
        cycle_machine.clock.cpu_cycles);
    expect_u64("arbiter same trace event count",
        c64_debug_copy_last_cpu_trace(&instruction_machine, &instruction_trace),
        c64_debug_copy_last_cpu_trace(&cycle_machine, &cycle_trace));
    expect_u64("arbiter same data-read cycle",
        instruction_trace.events[3].absolute_cycle, cycle_trace.events[3].absolute_cycle);
}

static void test_timing_fixture_records_pending_read_stall(void) {
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    timing_sample sample;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);
    machine.bus.ram[0x1234] = 0x5a;

    step_machine_cycles(&machine, 3); /* leave the LDA data read pending */
    machine.vic.timing.ba_low_until_abs = machine.clock.cycle + 2u;

    capture_timing_step(&machine, &sample);
    expect_true("fixture begins with pending instruction", sample.pending_before);
    expect_true("fixture sees ba low", sample.ba_before);
    expect_u64("fixture master cycle advances during stall",
        sample.master_cycle_before + 1u, sample.master_cycle_after);
    expect_u64("fixture CPU is held during stalled read",
        sample.cpu_cycles_before, sample.cpu_cycles_after);
    expect_u64("fixture pending elapsed unchanged during stall",
        sample.elapsed_before, machine.pending_cpu_elapsed);

    capture_timing_step(&machine, &sample);
    expect_true("fixture still sees ba low", sample.ba_before);
    expect_u64("fixture second stalled master cycle advances",
        sample.master_cycle_before + 1u, sample.master_cycle_after);
    expect_u64("fixture second stalled CPU remains held",
        sample.cpu_cycles_before, sample.cpu_cycles_after);

    capture_timing_step(&machine, &sample);
    expect_true("fixture ba releases before resumed read", !sample.ba_before);
    expect_u64("fixture resumed CPU advances", sample.cpu_cycles_before + 1u,
        sample.cpu_cycles_after);
    expect_u8("fixture resumed read reaches accumulator", 0x5a, snapshot(&machine).a);
}

static void test_timing_fixture_records_real_badline_stall(void) {
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    timing_sample sample;
    uint64_t held_cpu_cycles;
    size_t i;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine_standard(&machine, &roms, C64_VIDEO_STANDARD_PAL);
    machine.bus.ram[0x1234] = 0x5a;

    /* Begin an instruction on the documented PAL badline BA-assert cycle.
       The first CPU cycle runs, then vicii_step_cycle() asserts BA through
       the three-cycle release margin before the pending read resumes. */
    vicii_write_register(&machine.vic, 0xd011, 0x13u);
    machine.vic.timing.raster_line = 0x33u;
    machine.vic.timing.cycle_in_line = 12u;

    capture_timing_step(&machine, &sample);
    expect_u64("badline fixture starts at cycle 12", 12, sample.cycle_in_line_before);
    expect_true("badline fixture BA high before VIC assertion", !sample.ba_before);
    expect_true("badline fixture VIC asserts BA", vicii_ba_active(&machine.vic, machine.clock.cycle));

    held_cpu_cycles = machine.clock.cpu_cycles;
    for (i = 0; i < 44u; i++) {
        capture_timing_step(&machine, &sample);
        expect_true("badline fixture BA stays low", sample.ba_before);
        expect_u64("badline fixture holds CPU", held_cpu_cycles, sample.cpu_cycles_after);
    }

    capture_timing_step(&machine, &sample);
    expect_true("badline fixture BA releases", !sample.ba_before);
    expect_u64("badline fixture CPU resumes", held_cpu_cycles + 1u, sample.cpu_cycles_after);
    step_machine_cycles(&machine, 2);
    expect_u8("badline fixture instruction completes", 0x5a, snapshot(&machine).a);
}

static void test_timing_fixture_records_sprite_ba_stall(
    c64_video_standard standard,
    uint32_t ba_assert_cycle,
    const char *prefix)
{
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    timing_sample sample;
    uint64_t held_cpu_cycles;
    size_t i;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine_standard(&machine, &roms, standard);
    machine.bus.ram[0x1234] = 0x5a;
    vicii_write_register(&machine.vic, 0xd011, 0x03u); /* DEN=0: sprite BA only. */
    vicii_write_register(&machine.vic, 0xd015, 0x01u);
    machine.vic.sprite_active[0] = true;
    machine.vic.timing.raster_line = 100u;
    machine.vic.timing.cycle_in_line = ba_assert_cycle;

    capture_timing_step(&machine, &sample);
    expect_u64(prefix, ba_assert_cycle, sample.cycle_in_line_before);
    expect_true("sprite fixture BA high before VIC assertion", !sample.ba_before);
    expect_true("sprite fixture VIC asserts BA", vicii_ba_active(&machine.vic, machine.clock.cycle));

    held_cpu_cycles = machine.clock.cpu_cycles;
    for (i = 0; i < 5u; i++) {
        capture_timing_step(&machine, &sample);
        expect_true("sprite fixture BA stays low", sample.ba_before);
        expect_u64("sprite fixture holds CPU", held_cpu_cycles, sample.cpu_cycles_after);
    }

    capture_timing_step(&machine, &sample);
    expect_true("sprite fixture BA releases", !sample.ba_before);
    expect_u64("sprite fixture CPU resumes", held_cpu_cycles + 1u, sample.cpu_cycles_after);
}

static void test_timing_fixture_records_pal_sprite_ba_stall(void) {
    test_timing_fixture_records_sprite_ba_stall(
        C64_VIDEO_STANDARD_PAL,
        54u,
        "PAL sprite fixture starts at PAL sprite-0 BA cycle");
}

static void test_timing_fixture_records_ntsc_sprite_ba_stall(void) {
    test_timing_fixture_records_sprite_ba_stall(
        C64_VIDEO_STANDARD_NTSC,
        56u,
        "NTSC sprite fixture starts at NTSC sprite-0 BA cycle");
}

static void test_timing_fixture_records_cross_line_sprite_ba_stall(void) {
    static const uint8_t program[] = {
        0xad, 0x34, 0x12  /* LDA $1234 */
    };
    c64_rom_set roms;
    c64_t machine;
    timing_sample sample;
    uint64_t held_cpu_cycles;
    bool saw_next_line = false;
    size_t i;

    build_roms(&roms, TEST_RESET_VECTOR);
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine_standard(&machine, &roms, C64_VIDEO_STANDARD_PAL);
    machine.bus.ram[0x1234] = 0x5a;
    vicii_write_register(&machine.vic, 0xd011, 0x03u); /* DEN=0: sprite BA only. */
    vicii_write_register(&machine.vic, 0xd015, 0x08u);
    vicii_write_register(&machine.vic, 0xd007, 49u);
    machine.vic.timing.raster_line = 49u;
    machine.vic.timing.cycle_in_line = 60u;

    /* Sprite 3 asserts on line N-1 for its line-N fetch. */
    capture_timing_step(&machine, &sample);
    expect_u64("cross-line fixture starts at sprite-3 assert", 60,
        sample.cycle_in_line_before);
    expect_true("cross-line fixture VIC asserts BA", vicii_ba_active(&machine.vic, machine.clock.cycle));

    held_cpu_cycles = machine.clock.cpu_cycles;
    for (i = 0; i < 5u; i++) {
        capture_timing_step(&machine, &sample);
        saw_next_line = saw_next_line || sample.raster_line_before == 50u;
        expect_true("cross-line fixture BA stays low", sample.ba_before);
        expect_u64("cross-line fixture holds CPU", held_cpu_cycles, sample.cpu_cycles_after);
    }
    expect_true("cross-line fixture spans into next raster line", saw_next_line);

    capture_timing_step(&machine, &sample);
    expect_true("cross-line fixture BA releases", !sample.ba_before);
    expect_u64("cross-line fixture CPU resumes", held_cpu_cycles + 1u, sample.cpu_cycles_after);
}

static void test_sei_cancels_cli_irq_defer(void) {
    static const uint8_t program[] = {
        0x58,       /* CLI */
        0x78,       /* SEI */
        0xa9, 0x42  /* LDA #$42 */
    };
    c64_rom_set roms;
    c64_t machine;

    build_roms(&roms, TEST_RESET_VECTOR);
    roms.kernal[0x1ffe] = 0x00;
    roms.kernal[0x1fff] = 0xe1;
    copy_to_kernal(&roms, TEST_RESET_VECTOR, program, sizeof(program));
    reset_machine(&machine, &roms);

    machine.vic.irq_status = 0x01;
    machine.vic.irq_enable = 0x01;

    step_machine(&machine, 3);

    expect_u16("sei blocks pending irq after cli defer", (uint16_t)(TEST_RESET_VECTOR + sizeof(program)), snapshot(&machine).pc);
    expect_u8("instruction after sei executes", 0x42, snapshot(&machine).a);
    expect_u64("no irq entered after sei", 0, machine.cpu.cpu.irq_entries);
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
    test_ram_write_history();
    test_stack();
    test_branch_taken_and_not_taken();
    test_branch_not_taken_cases();
    test_page_crossing_branch();
    test_page_wrap();
    test_banking_affects_execution();
    test_sta_abs_bus_event_trace();
    test_cpu_trace_tags_dummy_stack_and_vector_accesses();
    test_cpu_trace_tags_rmw_accesses();
    test_cpu_trace_tags_branch_and_stack_reads();
    test_cpu_trace_tags_irq_and_nmi_accesses();
    test_cpu_trace_tags_page_cross_and_rti_stack_reads();
    test_microcycle_zero_page_load_store_trace();
    test_microcycle_jmp_trace();
    test_microcycle_branch_page_cross_trace();
    test_microcycle_jsr_rts_trace();
    test_microcycle_indexed_zero_page_trace();
    test_microcycle_absolute_read_operation_trace();
    test_microcycle_indexed_read_operation_trace();
    test_microcycle_indirect_read_write_trace();
    test_microcycle_indexed_rmw_and_indirect_jump_trace();
    test_migrated_families_match_normal_trace_under_pal_badline();
    test_practical_undocumented_microcycle_semantics();
    test_practical_undocumented_y_indexed_forms();
    test_practical_undocumented_families_match_badline_trace();
    test_sta_d020_applies_at_event_cycle();
    test_cpu_trace_disabled_leaves_debug_trace_empty();
    test_cycle_step_trace_enabled_records_bus_events();
    test_ba_allows_pending_write_cycle();
    test_aec_blocks_pending_write_during_vic_phi2();
    test_migrated_rmw_writes_proceed_while_ba_is_low();
    test_ba_stalls_pending_read_cycle();
    test_cpu_vic_pal_ntsc_interaction_traces();
    test_instruction_and_cycle_step_share_ba_arbiter();
    test_timing_fixture_records_pending_read_stall();
    test_timing_fixture_records_real_badline_stall();
    test_timing_fixture_records_pal_sprite_ba_stall();
    test_timing_fixture_records_ntsc_sprite_ba_stall();
    test_timing_fixture_records_cross_line_sprite_ba_stall();
    test_sei_cancels_cli_irq_defer();
    test_runtime_run_instructions();
    return 0;
}
