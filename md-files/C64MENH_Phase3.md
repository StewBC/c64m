# C64MENH Phase 3: Practical 6510 Undocumented Opcode Audit

## Purpose

Audit practical 6510 undocumented opcode coverage for ordinary C64 software and implement only the missing in-scope subset, if needed.

This is an audit-first CPU compatibility task. It is not permission to rewrite the CPU core or implement every obscure electrical edge case.

## Background

The current milestone requires practical 6510 undocumented opcode coverage to be audited and either confirmed or implemented.

The milestone target is ordinary C64 software: BASIC programs, single-file PRGs, many KERNAL-loading D64 titles, games, simple demos, and selected diagnostics. It is not full demo-scene compatibility or perfect hardware recreation.

Undocumented opcodes are relevant because some real C64 software and loaders use common illegal opcode families. However, implementing all variants without tests can introduce CPU regressions.

## Required Reading

Read in this order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MCPU_NEW.md`, if present
5. Any current CPU phase or implementation guide, if present
6. Current CPU code, disassembler code, and CPU tests

If planning documents disagree with implementation or tests, treat that as reconciliation work.

## Suggested Branch

Use a dedicated branch:

```sh
git checkout -b enhancement/6510-undocumented-audit
```

## Phase Goal

Answer these questions with evidence:

1. Which undocumented opcodes are currently implemented?
2. Which undocumented opcodes decode but behave as NOP, JAM, or unknown?
3. Which common 6510 illegal opcode families are missing?
4. Which missing families are likely relevant to ordinary C64 software?
5. Which tests already cover undocumented opcode behavior?
6. What is the smallest practical implementation set, if any?

## Files And Areas To Inspect

At minimum, inspect:

```text
src/machine/c6510.c
src/machine/c6510.h
src/machine/c6510_inln.h
src/machine/c64.c
src/tools/disasm_6502.c, if present
src/tools/disasm_6502.h, if present
tests/machine/test_c64_cpu_validation.c
other CPU tests under tests/machine
md-files/STATUS.md
md-files/C64MCPU_NEW.md, if present
```

Search terms:

```text
illegal
undocumented
opcode
JAM
KIL
SLO
RLA
SRE
RRA
SAX
LAX
DCP
ISC
ISB
ANC
ALR
ARR
AXS
LAS
TAS
AHX
SHX
SHY
NOP
```

## Opcode Families To Consider

At minimum, inventory common practical families:

```text
SLO  ASL then ORA
RLA  ROL then AND
SRE  LSR then EOR
RRA  ROR then ADC
SAX  store A & X
LAX  load A and X
DCP  DEC then CMP
ISC/ISB  INC then SBC
ANC, ALR, ARR, AXS
illegal NOP variants
JAM/KIL halt opcodes
```

Unstable or highly hardware-dependent store/load combinations should be treated cautiously:

```text
AHX, SHX, SHY, TAS, LAS and related variants
```

Do not implement unstable behavior unless it is explicitly accepted for the milestone and testable.

## Analysis Commands

Suggested inspection commands:

```sh
git status --short --branch
rg -n "illegal|undocumented|opcode|JAM|KIL|SLO|RLA|SRE|RRA|SAX|LAX|DCP|ISC|ISB|ANC|ALR|ARR|AXS|AHX|SHX|SHY|TAS|LAS|NOP" src tests md-files
nl -ba src/machine/c6510_inln.h | sed -n '1,260p'
nl -ba src/machine/c6510_inln.h | sed -n '260,620p'
nl -ba src/machine/c6510_inln.h | sed -n '620,980p'
nl -ba src/machine/c6510_inln.h | sed -n '980,1320p'
nl -ba src/machine/c6510.c | sed -n '1,260p'
nl -ba tests/machine/test_c64_cpu_validation.c | sed -n '1,260p'
```

Adjust line ranges as needed.

## Stop / Continue Gate

After the audit, stop and write a decision note before implementing.

Continue only if:

- a missing or incorrect opcode family is likely relevant to ordinary C64 software;
- behavior is sufficiently well understood to test;
- implementation can be done incrementally;
- existing CPU validation tests can be extended safely.

Stop without implementation if:

- practical undocumented coverage is already sufficient;
- only unstable hardware-dependent opcodes remain;
- proposed work expands into a CPU rewrite;
- no reliable expected behavior is available for focused tests.

## Implementation Guidance If Continuing

If implementation is needed:

1. Work by opcode family, not random individual opcodes.
2. Add focused tests before or alongside implementation.
3. Validate flags, memory writes, cycle counts, addressing modes, and page-cross behavior.
4. Keep documented opcode behavior unchanged.
5. Update disassembler mnemonics only where useful and safe.
6. Leave unstable/deferred opcodes explicitly documented.

Do not rewrite the CPU core as part of this phase.

## Acceptance Criteria

This phase is complete when:

- undocumented opcode coverage has been inventoried;
- practical missing behavior is either implemented with tests or explicitly deferred;
- documented opcode tests still pass;
- CPU cycle-count tests still pass;
- existing boot, keyboard, joystick, debugger, PRG, D64, PAL, and NTSC smoke tests still pass;
- `STATUS.md` reflects reality if a durable status claim changed.

## Required Commands Before Final Hand-Off

At minimum:

```sh
cmake --build build
ctest --test-dir build
```

If targeted CPU tests exist, run them separately and report exact commands.

Do not run `./build/c64m` without a timeout.

## Hand-Off Report

End with a concise hand-off report containing:

- branch name;
- commit hash or note that changes are uncommitted;
- exact commands run;
- files inspected;
- files changed;
- tests run and results;
- opcode families already covered;
- opcode families added, if any;
- opcode families explicitly deferred;
- whether practical undocumented opcode coverage is complete for the current milestone;
- known limitations;
- recommended next step.
