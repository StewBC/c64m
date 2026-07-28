#include "runtime_breakpoint_condition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures++; \
        } \
    } while (0)

static bool parse_ok(const char *text, runtime_bp_condition *out) {
    char error[128];
    error[0] = '\0';
    return runtime_bp_condition_parse(text, out, error, sizeof(error));
}

static bool parse_fails(const char *text) {
    runtime_bp_condition condition;
    char error[128];
    error[0] = '\0';
    if (runtime_bp_condition_parse(text, &condition, error, sizeof(error))) {
        return false;
    }
    /* A rejection must carry a diagnostic; a silent false is a bug. */
    return error[0] != '\0';
}

/* -- eval context helpers ------------------------------------------------ */

typedef struct fake_memory {
    uint8_t bytes[0x10000];
    unsigned reads;
} fake_memory;

static uint8_t fake_mem_read(void *user, uint16_t address) {
    fake_memory *mem = (fake_memory *)user;
    mem->reads++;
    return mem->bytes[address];
}

static void context_init(runtime_bp_eval_context *ctx, fake_memory *mem) {
    memset(ctx, 0, sizeof(*ctx));
    memset(mem, 0, sizeof(*mem));
    ctx->mem_read = fake_mem_read;
    ctx->mem_read_user = mem;
}

/* -- parse --------------------------------------------------------------- */

static void test_parse_lhs_tokens(void) {
    static const struct {
        const char *text;
        runtime_bp_term_lhs lhs;
    } cases[] = {
        { "a==1",     RUNTIME_BP_LHS_A },
        { "x==1",     RUNTIME_BP_LHS_X },
        { "y==1",     RUNTIME_BP_LHS_Y },
        { "sp==1",    RUNTIME_BP_LHS_SP },
        { "p==1",     RUNTIME_BP_LHS_P },
        { "n==1",     RUNTIME_BP_LHS_FLAG_N },
        { "v==1",     RUNTIME_BP_LHS_FLAG_V },
        { "b==1",     RUNTIME_BP_LHS_FLAG_B },
        { "d==1",     RUNTIME_BP_LHS_FLAG_D },
        { "i==1",     RUNTIME_BP_LHS_FLAG_I },
        { "z==1",     RUNTIME_BP_LHS_FLAG_Z },
        { "c==1",     RUNTIME_BP_LHS_FLAG_C },
        { "value==1", RUNTIME_BP_LHS_VALUE },
        { "raster==1", RUNTIME_BP_LHS_RASTER },
        { "vic_cycle==1", RUNTIME_BP_LHS_VIC_CYCLE }
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        runtime_bp_condition condition;
        CHECK(parse_ok(cases[i].text, &condition));
        CHECK(condition.term_count == 1u);
        CHECK(condition.terms[0].lhs == cases[i].lhs);
        CHECK(condition.terms[0].imm == 1u);
    }
}

static void test_parse_ops(void) {
    static const struct {
        const char *text;
        runtime_bp_term_op op;
    } cases[] = {
        { "a==5",  RUNTIME_BP_OP_EQ },
        { "a!=5",  RUNTIME_BP_OP_NE },
        { "a<5",   RUNTIME_BP_OP_LT },
        { "a>5",   RUNTIME_BP_OP_GT },
        { "a<=5",  RUNTIME_BP_OP_LE },
        { "a>=5",  RUNTIME_BP_OP_GE },
        { "a&5",   RUNTIME_BP_OP_MASK_SET },
        { "a!&5",  RUNTIME_BP_OP_MASK_CLEAR }
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        runtime_bp_condition condition;
        CHECK(parse_ok(cases[i].text, &condition));
        CHECK(condition.term_count == 1u);
        CHECK(condition.terms[0].op == cases[i].op);
        CHECK(condition.terms[0].imm == 5u);
    }
}

static void test_parse_mem_and_radixes(void) {
    runtime_bp_condition condition;

    CHECK(parse_ok("mem($D000)>$F0", &condition));
    CHECK(condition.term_count == 1u);
    CHECK(condition.terms[0].lhs == RUNTIME_BP_LHS_MEM);
    CHECK(condition.terms[0].mem_address == 0xD000u);
    CHECK(condition.terms[0].op == RUNTIME_BP_OP_GT);
    CHECK(condition.terms[0].imm == 0xF0u);

    CHECK(parse_ok("mem(0xD000)==0x2A", &condition));
    CHECK(condition.terms[0].mem_address == 0xD000u);
    CHECK(condition.terms[0].imm == 0x2Au);

    CHECK(parse_ok("mem(53248)==240", &condition));
    CHECK(condition.terms[0].mem_address == 0xD000u);
    CHECK(condition.terms[0].imm == 240u);

    /* raster is 16-bit, so immediates above a byte must survive */
    CHECK(parse_ok("raster>=250", &condition));
    CHECK(condition.terms[0].imm == 250u);
    CHECK(parse_ok("raster<=311", &condition));
    CHECK(condition.terms[0].imm == 311u);
}

static void test_parse_multi_term(void) {
    runtime_bp_condition condition;

    /* the motivating XMSB case */
    CHECK(parse_ok("value!&1,mem($D000)>$F0", &condition));
    CHECK(condition.term_count == 2u);
    CHECK(condition.terms[0].lhs == RUNTIME_BP_LHS_VALUE);
    CHECK(condition.terms[0].op == RUNTIME_BP_OP_MASK_CLEAR);
    CHECK(condition.terms[0].imm == 1u);
    CHECK(condition.terms[1].lhs == RUNTIME_BP_LHS_MEM);
    CHECK(condition.terms[1].mem_address == 0xD000u);

    /* exactly at capacity */
    CHECK(parse_ok("a==1,x==2,y==3,i==1", &condition));
    CHECK(condition.term_count == RUNTIME_BREAKPOINT_CONDITION_TERMS);
    CHECK(condition.terms[3].lhs == RUNTIME_BP_LHS_FLAG_I);
}

/* The .ini item list is itself comma separated, so persisted conditions use
   ';' between terms. Both separators must parse identically. */
static void test_parse_semicolon_separator(void) {
    runtime_bp_condition comma;
    runtime_bp_condition semi;
    uint8_t i;

    CHECK(parse_ok("value!&1,mem($D000)>$F0", &comma));
    CHECK(parse_ok("value!&1;mem($D000)>$F0", &semi));
    CHECK(comma.term_count == semi.term_count);
    for (i = 0; i < comma.term_count; ++i) {
        CHECK(comma.terms[i].lhs == semi.terms[i].lhs);
        CHECK(comma.terms[i].op == semi.terms[i].op);
        CHECK(comma.terms[i].imm == semi.terms[i].imm);
        CHECK(comma.terms[i].mem_address == semi.terms[i].mem_address);
    }
    CHECK(parse_fails("a==1;"));
    CHECK(parse_fails("a==1;;x==2"));
}

static void test_parse_rejects(void) {
    CHECK(parse_fails(""));              /* empty */
    CHECK(parse_fails("i"));             /* no op */
    CHECK(parse_fails("i=="));           /* no immediate */
    CHECK(parse_fails("==1"));           /* no lhs */
    CHECK(parse_fails("foo==1"));        /* unknown lhs */
    CHECK(parse_fails("i=1"));           /* single = is not an op */
    CHECK(parse_fails("i~1"));           /* unknown op */
    CHECK(parse_fails("mem(==1"));       /* unterminated mem() */
    CHECK(parse_fails("mem()>1"));       /* mem() without address */
    CHECK(parse_fails("mem($1D000)>1")); /* mem address out of range */
    CHECK(parse_fails("a==70000"));      /* immediate out of range */
    CHECK(parse_fails("a==1,"));         /* trailing separator */
    CHECK(parse_fails("a==1,,x==2"));    /* empty term */
    CHECK(parse_fails("a==zz"));         /* malformed immediate */
    CHECK(parse_fails("a==1 x==2"));     /* space is not a separator */
    /* one past capacity */
    CHECK(parse_fails("a==1,x==2,y==3,i==1,c==1"));
}

/* A definition built field-by-field (rather than memset first) carries stack
   garbage in `condition`. Callers must zero it, but validity is checked when a
   breakpoint is armed so a forgetful caller gets an unguarded breakpoint rather
   than one that silently never fires. */
static void test_validity(void) {
    runtime_bp_condition condition;

    memset(&condition, 0, sizeof(condition));
    CHECK(runtime_bp_condition_is_valid(&condition));

    CHECK(parse_ok("value!&1,mem($D000)>$F0", &condition));
    CHECK(runtime_bp_condition_is_valid(&condition));

    /* Term count past capacity. */
    condition.term_count = RUNTIME_BREAKPOINT_CONDITION_TERMS + 1u;
    CHECK(!runtime_bp_condition_is_valid(&condition));

    /* Out-of-range left-hand side. */
    CHECK(parse_ok("a==1", &condition));
    condition.terms[0].lhs = 200u;
    CHECK(!runtime_bp_condition_is_valid(&condition));

    /* Out-of-range operator. */
    CHECK(parse_ok("a==1", &condition));
    condition.terms[0].op = 200u;
    CHECK(!runtime_bp_condition_is_valid(&condition));

    CHECK(!runtime_bp_condition_is_valid(NULL));
}

static void test_uses_value(void) {
    runtime_bp_condition condition;

    CHECK(parse_ok("a==1,value==2", &condition));
    CHECK(runtime_bp_condition_uses_value(&condition));

    CHECK(parse_ok("a==1,mem($D000)==2", &condition));
    CHECK(!runtime_bp_condition_uses_value(&condition));

    memset(&condition, 0, sizeof(condition));
    CHECK(!runtime_bp_condition_uses_value(&condition));
}

/* -- eval ---------------------------------------------------------------- */

static void test_eval_empty_is_true(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    memset(&condition, 0, sizeof(condition));
    context_init(&ctx, &mem);
    /* An unguarded breakpoint must behave exactly as before. */
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
}

static void test_eval_registers_and_flags(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    context_init(&ctx, &mem);
    ctx.a = 0x42u;
    ctx.x = 0x10u;
    ctx.y = 0x20u;
    ctx.sp = 0xF9u;
    ctx.p = 0x24u; /* I set (0x04), Z clear, C clear */

    CHECK(parse_ok("a==$42", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("a==$43", &condition));
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));

    CHECK(parse_ok("x<$20", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("y>=$20", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("sp!=$F9", &condition));
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("p==$24", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));

    /* The motivating case: break only inside an IRQ handler. */
    CHECK(parse_ok("i==1", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    ctx.p = 0x20u; /* I clear */
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));

    /* Flags decode to 0/1, not to their mask bit. */
    ctx.p = 0xFFu;
    CHECK(parse_ok("n==1", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("c==1", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    ctx.p = 0x00u;
    CHECK(parse_ok("z==0", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("v==0", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
}

static void test_eval_mask_ops(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    context_init(&ctx, &mem);
    ctx.a = 0x81u;

    CHECK(parse_ok("a&$80", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("a&$40", &condition));
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("a!&$40", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("a!&$80", &condition));
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
}

static void test_eval_value_term(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    context_init(&ctx, &mem);
    ctx.has_value = true;
    ctx.value = 0x06u;

    CHECK(parse_ok("value==$06", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("value!&1", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));

    /* Without an accessed byte (exec match) a value term cannot hold. */
    ctx.has_value = false;
    CHECK(parse_ok("value==$06", &condition));
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
}

static void test_eval_mem_term(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    context_init(&ctx, &mem);
    mem.bytes[0xD000] = 0xF8u;

    CHECK(parse_ok("mem($D000)>$F0", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(mem.reads > 0u);

    mem.bytes[0xD000] = 0x10u;
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));

    /* A missing reader must not crash and must not spuriously match. */
    ctx.mem_read = NULL;
    ctx.mem_read_user = NULL;
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
}

static void test_eval_vic_terms(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    context_init(&ctx, &mem);
    ctx.raster = 251u;
    ctx.vic_cycle = 12u;

    CHECK(parse_ok("raster>=250", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("raster<250", &condition));
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
    CHECK(parse_ok("vic_cycle==12", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
}

static void test_eval_and_semantics(void) {
    runtime_bp_condition condition;
    runtime_bp_eval_context ctx;
    fake_memory mem;

    context_init(&ctx, &mem);
    ctx.has_value = true;
    ctx.value = 0x06u;   /* bit 0 clear */
    mem.bytes[0xD000] = 0xF8u;

    /* Both hold. */
    CHECK(parse_ok("value!&1,mem($D000)>$F0", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));

    /* Second term fails -> whole condition fails. */
    mem.bytes[0xD000] = 0x10u;
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));

    /* First term fails -> whole condition fails. */
    mem.bytes[0xD000] = 0xF8u;
    ctx.value = 0x07u; /* bit 0 set */
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));

    /* Four-term AND, all true. */
    ctx.a = 1u;
    ctx.x = 2u;
    ctx.y = 3u;
    ctx.p = 0x04u;
    CHECK(parse_ok("a==1,x==2,y==3,i==1", &condition));
    CHECK(runtime_bp_condition_eval(&condition, &ctx));
    ctx.y = 4u;
    CHECK(!runtime_bp_condition_eval(&condition, &ctx));
}

/* -- format -------------------------------------------------------------- */

static void test_format_round_trip(void) {
    static const char *cases[] = {
        "i==1",
        "value!&1,mem($D000)>$F0",
        "a==1,x==2,y==3,i==1",
        "raster>=250",
        "vic_cycle==12",
        "sp<=$F9",
        "a&$80"
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        runtime_bp_condition first;
        runtime_bp_condition second;
        char text[256];
        size_t t;

        CHECK(parse_ok(cases[i], &first));
        CHECK(runtime_bp_condition_format(&first, text, sizeof(text)));
        CHECK(parse_ok(text, &second));
        CHECK(first.term_count == second.term_count);
        for (t = 0; t < first.term_count; ++t) {
            CHECK(first.terms[t].lhs == second.terms[t].lhs);
            CHECK(first.terms[t].op == second.terms[t].op);
            CHECK(first.terms[t].imm == second.terms[t].imm);
            CHECK(first.terms[t].mem_address == second.terms[t].mem_address);
        }
    }
}

static void test_format_empty_and_overflow(void) {
    runtime_bp_condition condition;
    char text[256];
    char tiny[4];

    memset(&condition, 0, sizeof(condition));
    CHECK(runtime_bp_condition_format(&condition, text, sizeof(text)));
    CHECK(text[0] == '\0');

    CHECK(parse_ok("value!&1,mem($D000)>$F0", &condition));
    /* Too small a buffer must fail rather than emit a truncated condition
       that would parse back as something different. */
    CHECK(!runtime_bp_condition_format(&condition, tiny, sizeof(tiny)));
}

int main(void) {
    test_parse_lhs_tokens();
    test_parse_ops();
    test_parse_mem_and_radixes();
    test_parse_multi_term();
    test_parse_semicolon_separator();
    test_parse_rejects();
    test_validity();
    test_uses_value();

    test_eval_empty_is_true();
    test_eval_registers_and_flags();
    test_eval_mask_ops();
    test_eval_value_term();
    test_eval_mem_term();
    test_eval_vic_terms();
    test_eval_and_semantics();

    test_format_round_trip();
    test_format_empty_and_overflow();

    if (failures != 0) {
        fprintf(stderr, "test_runtime_breakpoint_condition: %d failure(s)\n",
                failures);
        return 1;
    }
    printf("test_runtime_breakpoint_condition: ok\n");
    return 0;
}
