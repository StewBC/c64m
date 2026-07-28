# Guarded breakpoints plan (Tier 1A)

**Status:** implemented (2026-07-28). The source and tests are authoritative;
the wire contract lives in `control-port.md` § Guarded breakpoints.

Implementation record (what shipped, and where it differs from this plan):

- Parse/eval/format live in `src/runtime/runtime_breakpoint_condition.{c,h}` as
  pure functions over a caller-supplied context, so they unit-test without a
  running machine. The plan implied they would sit inline in the runtime; a
  separate unit was needed because the definition text parser
  (`control_parse_breakpoint_definition`) is static in `main.c` and not linkable
  by tests.
- `;` is accepted as a term separator alongside `,`, because the `.ini`
  breakpoint value is itself a comma-separated item list. Persisted conditions
  use `;`.
- Added `runtime_bp_condition_is_valid()` and a sanitize step when a breakpoint
  is armed. Several callers build `runtime_breakpoint_definition` field by field
  without zeroing it (including `runtime_set_execute_breakpoint`, the
  `break-exec` path), so the new field arrived as stack garbage and silently
  disabled those breakpoints. Callers were fixed to memset, and the sanitize
  step bounds the damage from any future caller that forgets: an invalid
  condition yields an *unguarded* breakpoint rather than one that can never
  fire.
- `break-list` records grew `cond=` and `when=`; the per-record payload budget
  went 192 -> 384 bytes, since undersizing it silently truncates the listing.

Measured perf (Apple M2, turbo=2 headless, a loop writing the watched address
twice per 15-cycle iteration - a pathological match rate):

| Config | pre-change | post-change |
|---|---|---|
| no breakpoints | 16.512 MHz | 16.938 MHz |
| unguarded watchpoint | 11.793 MHz | 11.867 MHz |
| guarded, 1 term | - | 11.767 MHz (-0.84% vs unguarded) |
| guarded, 2 terms incl. `mem()` | - | 11.670 MHz (-1.66% vs unguarded) |

The no-breakpoint and unguarded paths are unchanged within noise, as expected:
with no breakpoints the match loop body never executes, and the guard is only
reached after an address match.

## Why this exists

Breakpoints today match on **address + access + mapping** only
(`runtime_breakpoint_matches_access`, `src/runtime/runtime_thread.c`). That
cannot express the conditions that produce time-critical bugs:

> Break on a write to `$D021` **only while the I flag is set** (i.e. inside an
> IRQ handler).
>
> Break on a write to `$D010` **only when the new value clears bit 0 while
> sprite-0 X is already past 240** (the classic XMSB-latch / one-frame-left
> glitch).
>
> Break on a store to the score **only on raster lines 250..262** (bottom border
> housekeeping), not the hundreds of legitimate mid-screen writes.

The human-pause workflow is too late for these (≈400k opcodes of aftermath). The
fix is to let the machine **stop itself** on the exact condition. This is the
"the glitch paused me" primitive the coop loop is missing.

## Scope decision

A **bounded AND-list of comparison terms**, not an expression language. The
flight recorder deliberately made "general query expressions" a non-goal; this
plan keeps that discipline. No parser recursion, no operator precedence, no
parentheses — a fixed-size array of `<lhs> <op> <imm>` terms, all ANDed. This is
enough for every bug class above and stays cheap and auditable.

## Why it is cheap (perf gate)

The condition is evaluated **only after the address test already matched**, in
`runtime_breakpoint_matches_access`. Address matches are rare — a watchpoint on
`$D021` fires the term evaluation on the handful of accesses that touch `$D021`,
never on the general bus stream. Per-hit cost is a short loop over ≤4 integer
compares plus at most one `mem()` fetch. **Gate: no measurable turbo-2 throughput
change with a guarded watchpoint armed vs. the same watchpoint unguarded**
(re-measure against `agents/perf-baseline-turbo2.md`). The hot path (accesses
that miss the address) is untouched.

## Term vocabulary

Each term is `lhs op imm`. Up to `RUNTIME_BREAKPOINT_CONDITION_TERMS` (= 4).

| LHS token | Source at match time |
|-----------|----------------------|
| `a` `x` `y` `sp` `p` | CPU registers (`rt->machine.cpu.cpu`) |
| `n` `v` `b` `d` `i` `z` `c` | individual P flag bits (0/1) |
| `value` | the accessed byte (already delivered to `runtime_memory_access`; today `(void)value`). For `exec` access this term is invalid → `bad-args`. |
| `mem($addr)` | one CPU-map byte read at match time via the debug read path |
| `raster` | `vic.raster_line` (VIC beam line) |
| `vic_cycle` | VIC cycle within the line |

| Op | Meaning |
|----|---------|
| `==` `!=` `<` `>` `<=` `>=` | integer compare |
| `&` | mask set: `(lhs & imm) != 0` |
| `!&` | mask clear: `(lhs & imm) == 0` |

`imm` accepts `$hex` / `0x` / decimal, same as the rest of the protocol.

### Worked examples (the three motivating cases)

```text
break-create write $D021 when=i==1
break-create write $D010 when=value!&1,mem($D000)>$F0
break-create write $00C3 when=raster>=250
```

## Data model changes

`runtime_breakpoint_definition` (`src/runtime/runtime_event.h`) and
`runtime_breakpoint` (`src/runtime/runtime_internal.h`) each gain:

```c
typedef enum runtime_bp_term_lhs {
    RUNTIME_BP_LHS_A, RUNTIME_BP_LHS_X, RUNTIME_BP_LHS_Y,
    RUNTIME_BP_LHS_SP, RUNTIME_BP_LHS_P,
    RUNTIME_BP_LHS_FLAG_N, /* ... V B D I Z C */
    RUNTIME_BP_LHS_VALUE,
    RUNTIME_BP_LHS_MEM,        /* uses term.mem_address */
    RUNTIME_BP_LHS_RASTER,
    RUNTIME_BP_LHS_VIC_CYCLE
} runtime_bp_term_lhs;

typedef enum runtime_bp_term_op {
    RUNTIME_BP_OP_EQ, RUNTIME_BP_OP_NE, RUNTIME_BP_OP_LT,
    RUNTIME_BP_OP_GT, RUNTIME_BP_OP_LE, RUNTIME_BP_OP_GE,
    RUNTIME_BP_OP_MASK_SET, RUNTIME_BP_OP_MASK_CLEAR
} runtime_bp_term_op;

typedef struct runtime_bp_term {
    runtime_bp_term_lhs lhs;
    runtime_bp_term_op  op;
    uint16_t mem_address;  /* valid only when lhs == MEM */
    uint32_t imm;
} runtime_bp_term;

/* added to both definition and runtime_breakpoint */
uint8_t         condition_term_count;   /* 0 = unconditional (today's behavior) */
runtime_bp_term condition_terms[RUNTIME_BREAKPOINT_CONDITION_TERMS];
```

`condition_term_count == 0` is exactly today's semantics, so every existing
breakpoint, `.ini` restore, and test is unchanged. `runtime_breakpoint_ini`
gains serialize/parse for the new terms (round-trip covered by test).

## Evaluation seam

Extend `runtime_breakpoint_matches_access(rt, access, address)` at
`src/runtime/runtime_thread.c` (the loop at ~2481). The chain becomes:

```c
if (breakpoint->enabled &&
    (breakpoint->access_mask & access) != 0 &&
    runtime_breakpoint_address_matches(breakpoint, address) &&
    runtime_breakpoint_mapping_matches(rt, breakpoint, address) &&
    runtime_breakpoint_condition_matches(rt, breakpoint, access, address, value) &&
    runtime_breakpoint_record_match(rt, breakpoint)) {
    return runtime_execute_breakpoint_actions(rt, breakpoint);
}
```

`value` must be threaded to this function. It already arrives at
`runtime_memory_access` (currently discarded). For `exec` matches
(`runtime_breakpoint_matches_pc`) there is no accessed byte, so a definition that
uses the `value` term with an exec access is rejected at parse time.

`runtime_breakpoint_condition_matches` returns `true` when
`condition_term_count == 0`, else ANDs every term. `mem()` reads use the existing
debug read (CPU-map) so it observes current banking; `raster`/`vic_cycle` read
the live VIC. Order matters: the condition is evaluated **before**
`record_match`, so `hits=`/counter only advance when the guard also passed (a
guarded count-only breakpoint counts guarded hits — the intended semantic).

## Wire protocol

Additive token `when=` on `break-create` and `break-update`
(`src/control/control_protocol.c`, parser near line 488), same key=value style as
`actions=`/`counter=`. Grammar:

```text
when=<term>[,<term>...]      term = <lhs><op><imm>   (lhs may be mem($addr))
```

Parse errors → `bad-args` with a specific message (unknown lhs, `value` on exec,
too many terms, malformed imm). `break-list` records gain
`cond=<n>` (term count) and echo the terms so a client can read back what it
armed. **Protocol bump to C64M/4** (`hello`/`version`, and the versioning-policy
note in `agents/control-port.md`). Update the `capabilities` string only if a new
capability token is warranted — `breakpoints` already covers it; no new token.

`coop_watch.py`: extend the `arm`/`count` inbox verbs to pass a trailing
`when=...` through untouched (it already forwards `extra`), and print the guard in
the snap header so the pack records what condition fired.

## Tests (test-first)

New/extended in `tests/control/test_control_protocol.c` and a runtime breakpoint
test:

1. Parse: every lhs token, every op, `mem($addr)`, multi-term, imm radixes.
2. Parse rejects: `value` with exec access, 5th term, unknown lhs, bad op, bad
   imm, `mem()` without address.
3. Eval: write-guard on P.I true/false; `value!&1` on written byte; `mem()`
   cross-reference; `raster` window; multi-term AND (all-true fires,
   any-false does not).
4. Semantics: `condition_term_count==0` behaves exactly like an unconditional
   breakpoint (regression pin for existing behavior).
5. Counting: guarded `actions=none` advances `hits=` only on guarded matches.
6. `.ini` round-trip of a guarded definition.
7. Wire: `break-list` echoes `cond=`/terms; `hello` reports C64M/4.

## Acceptance checklist

- [x] `ctest --test-dir build` green — 65/65.
- [x] Perf gate met (see the measured table above).
- [x] `agents/control-port.md` updated: `when=` grammar, C64M/4, `break-list`
      `cond=`/`when=` fields, exec+`value` rejection, and the "no
      expression-guarded breakpoint" trap line replaced.
- [x] `agents/README.md` current-baseline count and index entry updated.
- [x] `tools/coop_watch.py` `arm`/`count` documented as accepting `when=`.
- [x] Manual updated per `manual/HELP_MARKDOWN.md`.

Tests added:

- `tests/runtime/test_runtime_breakpoint_condition.c` — parse (every lhs/op,
  `mem()`, radixes, both separators), rejects, validity, eval (each lhs, each
  op, AND semantics, missing `value`/reader), format round-trip and overflow.
- `tests/runtime/test_runtime_breakpoint_ini.c` — save/load round-trip through
  the real ini entry points, including an unguarded control case.
- `tests/control/test_guarded_breakpoint_control.py` — end to end: the guard
  gates hit counting (exactly half of a two-write loop), gates the stop, an
  impossible guard never stops, `break-list` echo, rejections, and unguarded
  behavior unchanged.

## Non-goals

- No OR / grouping / precedence. If a real case needs OR, arm two breakpoints.
- No drive-CPU guarded breakpoints in v1 (main 6510 only, like the recorder).
- No cross-cycle temporal conditions ("X then Y within N cycles") — that is the
  frame/VIC ring's job (`agents/frame-ring-plan.md`), not a breakpoint's.
