# C64MENH Phase 3: Practical 6510 Undocumented Opcode Coverage Audit

## Purpose

Confirm whether practical undocumented opcode coverage is already sufficient for the current C64 milestone, given that the CPU core was imported from the `a2m` emulator and is known to pass the Harte 6502 tests at 100%.

This is an audit-first reconciliation task. It is not permission to rewrite the CPU core, revalidate all documented 6502 behavior from scratch, or chase every unstable NMOS edge case.

## Background

The Commodore 64's 6510 is instruction-compatible with the NMOS 6502 for documented instructions. The main 6510-specific differences relevant to the emulator are integration details: the on-chip I/O port at `$0000/$0001`, memory banking, interrupt/NMI integration, BA/RDY-style stalls, and bus-visible timing.

Therefore, if the imported CPU core is already Harte-clean for documented 6502 behavior, this phase should not duplicate that validation. The useful question is narrower:

```text
Does the current CPU implementation already cover the practical NMOS undocumented opcode behavior ordinary C64 software is likely to use, and does the C64 wrapper preserve the integration-sensitive behavior around those opcodes?
```

The current milestone requires practical 6510 undocumented opcode coverage to be audited and either confirmed or implemented. This does not imply perfect support for every unstable undocumented opcode or analog bus-dependent behavior.

## Milestone Boundary

In scope:

```text
- Inventory current undocumented opcode decode and behavior.
- Determine whether the Harte suite used by this project covers only documented opcodes or all 256 NMOS opcodes.
- Identify any missing common undocumented opcode families likely to matter for ordinary C64 software.
- Add focused tests or documentation where coverage is unclear.
- Implement only small, well-understood, milestone-relevant missing behavior if needed.
- Reconcile STATUS.md and CPU planning docs with implementation and tests.
```

Out of scope:

```text
- CPU rewrite.
- Re-auditing all documented 6502 instructions already covered by Harte tests.
- Cycle-perfect CPU pin behavior.
- Exact RDY/AEC sub-cycle timing.
- Last-byte-on-bus open-bus recreation.
- Perfect behavior for unstable undocumented opcodes whose results depend on chip revision, data bus decay, analog effects, or external bus conditions.
- Demo-scene-only compatibility unless explicitly accepted for this milestone.
```

## Required Reading

Read in this order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MCPU_NEW.md`, if present
5. Any current CPU phase or implementation guide, if present
6. Current CPU code, C64 CPU/bus wrapper code, disassembler code, and CPU tests
7. Any local notes explaining the imported `a2m` CPU and Harte test setup

If planning documents disagree with implementation or tests, treat that as reconciliation work.

## Suggested Branch

Use a dedicated branch:

```sh
git checkout -b enhancement/6510-undocumented-audit
```

## Phase Goal

Answer these questions with evidence:

1. Which Harte tests does this repo run or rely on?
2. Do those tests cover only official documented 6502 opcodes, or do they cover all 256 NMOS opcode slots including undocumented opcodes?
3. Which undocumented opcode families are implemented by the imported CPU core?
4. Which undocumented opcode families are tested locally or by the Harte corpus?
5. Which opcode slots, if any, decode as NOP, JAM/KIL, unknown, or placeholder behavior?
6. Are any missing or weakly tested opcodes likely to affect ordinary C64 software?
7. Are any issues actually C64/6510 integration issues rather than opcode execution issues?
8. Should this milestone consider practical undocumented opcode coverage complete?

## Important Distinction: 6502 Core vs 6510 Integration

Do not conflate these layers.

### CPU core instruction semantics

For opcode execution, the 6510 should be treated as NMOS 6502-family compatible. The audit should focus on the imported core's support for NMOS undocumented opcodes, not on inventing a separate 6510 instruction set.

### C64/6510 integration behavior

Some behavior that looks like a CPU issue may actually belong to the C64 wrapper:

```text
- `$0000/$0001` CPU port data direction and data register behavior.
- RAM/ROM/I/O banking controlled by the CPU port.
- CPU-visible bus event timing.
- BA/RDY-style read stalls and write allowance.
- IRQ/NMI sampling points.
- CIA #2 NMI edge latch and RESTORE NMI source.
- KERNAL trap paths and debugger-safe reads.
```

If an issue is integration-related, do not fix it by changing opcode semantics unless the evidence clearly supports that.

## Files And Areas To Inspect

At minimum, inspect:

```text
src/machine/c6510.c
src/machine/c6510.h
src/machine/c6510_inln.h
src/machine/c64.c
src/machine/c64_bus.c, if present
src/tools/disasm_6502.c, if present
src/tools/disasm_6502.h, if present
tests/machine/test_c64_cpu_validation.c
other CPU tests under tests/machine
Harte test harness files, if present
md-files/STATUS.md
md-files/C64MCPU_NEW.md, if present
```

Search terms:

```text
harte
6502
6510
illegal
undocumented
opcode
JAM
KIL
HLT
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
XAA
NOP
```

## Opcode Families To Inventory

Classify each family into one of these states:

```text
implemented and tested
implemented but not clearly tested
placeholder / partial
not implemented
intentionally deferred
not applicable / unstable out of scope
```

### Stable/common undocumented families

These are the most likely practical C64 compatibility candidates:

```text
SLO        ASL then ORA
RLA        ROL then AND
SRE        LSR then EOR
RRA        ROR then ADC
SAX        store A & X
LAX        load A and X
DCP        DEC then CMP
ISC/ISB    INC then SBC
NOP        unofficial immediate/zero-page/absolute NOP variants
SBC #$EB   alternate immediate SBC opcode, if applicable
```

### Moderately quirky families

Audit carefully before changing:

```text
ANC
ALR
ARR
AXS/SBX
LAS
```

### Unstable or hardware-sensitive families

Treat cautiously. Do not chase perfect behavior unless ordinary software or an accepted diagnostic requires it:

```text
AHX/SHA
SHX
SHY
TAS/SHS
XAA/ANE
LAX immediate variants with unstable behavior, if applicable
JAM/KIL halt opcodes
```

`JAM/KIL` behavior may be useful to model as a halted CPU state if tests or software expect it, but do not expand this into full pin-level behavior.

## Analysis Commands

Suggested inspection commands:

```sh
git status --short --branch
rg -n "harte|6502|6510|illegal|undocumented|opcode|JAM|KIL|HLT|SLO|RLA|SRE|RRA|SAX|LAX|DCP|ISC|ISB|ANC|ALR|ARR|AXS|SBX|AHX|SHA|SHX|SHY|TAS|SHS|LAS|XAA|ANE|NOP" src tests md-files .
rg -n "00|01|cpu port|bank|loram|hiram|charen|BA|RDY|IRQ|NMI" src/machine tests/machine md-files
nl -ba src/machine/c6510_inln.h | sed -n '1,260p'
nl -ba src/machine/c6510_inln.h | sed -n '260,620p'
nl -ba src/machine/c6510_inln.h | sed -n '620,980p'
nl -ba src/machine/c6510_inln.h | sed -n '980,1320p'
nl -ba src/machine/c6510.c | sed -n '1,320p'
nl -ba src/machine/c64.c | sed -n '1,220p'
nl -ba tests/machine/test_c64_cpu_validation.c | sed -n '1,320p'
```

Adjust line ranges as needed.

## Harte Test Coverage Check

Before considering implementation, determine exactly what the Harte validation means in this repo.

Document:

```text
- Which Harte corpus is used.
- Whether it covers documented opcodes only or all 256 opcode slots.
- Whether decimal-mode cases are included.
- Whether unofficial opcode cases are included.
- Whether tests check bus cycles or only final CPU state.
- Whether the imported `a2m` core passed those tests before import, in this repo, or both.
```

If the project does not currently run the Harte tests locally, do not assume the current integrated C64 wrapper has equivalent coverage. Treat the `a2m` result as strong evidence about the CPU core, but still verify what this repo tests and what changed during integration.

## C64 Integration Checks

If undocumented opcode semantics appear complete, check whether C64 integration could still affect practical behavior.

Look especially at:

```text
- Read-modify-write illegal opcodes and their bus read/write sequences.
- Whether RMW writes occur at the same CPU event cycle shape as official RMW instructions.
- Page-cross timing for undocumented addressing modes.
- Decimal mode behavior for ADC/SBC-derived undocumented opcodes such as RRA/ISC/ARR.
- IRQ/NMI sampling only at instruction entry.
- BA stalls holding CPU reads/internal cycles but allowing writes, if relevant to opcode bus events.
- `$0000/$0001` access through the C64 memory map.
```

Do not broaden into full RDY/AEC sub-cycle recreation. Stay within the current milestone.

## Stop / Continue Gate

After the audit, stop and write a decision note before implementing.

Continue only if all are true:

```text
- A missing or incorrect undocumented opcode family is found.
- The family is likely relevant to ordinary C64 software or selected diagnostics.
- Expected behavior is sufficiently well understood to test.
- The change can be implemented incrementally without CPU rewrite.
- Existing CPU validation and C64 timing tests can protect against regressions.
```

Stop without implementation if any are true:

```text
- Harte coverage already includes all practical undocumented NMOS opcodes and the current integration has not weakened it.
- Only unstable hardware-dependent opcodes remain.
- The proposed work expands into a CPU rewrite.
- No reliable expected behavior is available for focused tests.
- The risk/reward is worse than moving to another milestone item.
```

A valid completion for this phase is:

```text
Practical undocumented opcode coverage is confirmed sufficient for the current milestone; no code changes needed beyond documentation/status reconciliation.
```

## Implementation Guidance If Continuing

If implementation is needed:

1. Work by opcode family, not random individual opcodes.
2. Add focused tests before or alongside implementation.
3. Validate flags, memory writes, cycle counts, addressing modes, and page-cross behavior.
4. Preserve official opcode behavior.
5. Preserve C64 bus-event ordering.
6. Update disassembler mnemonics only where useful and safe.
7. Keep unstable/deferred opcodes explicitly documented.
8. Do not rewrite the CPU core as part of this phase.

## Acceptance Criteria

This phase is complete when:

```text
- Harte coverage scope has been identified.
- Undocumented opcode coverage has been inventoried.
- Practical missing behavior is either implemented with tests or explicitly deferred.
- C64/6510 integration-sensitive behavior has been considered separately from opcode semantics.
- Official/documented opcode confidence remains intact.
- Existing CPU, VIC, CIA, SID, runtime, PRG, D64, PAL, and NTSC tests still pass.
- STATUS.md reflects reality if a durable status claim changed.
```

## Required Commands Before Final Hand-Off

At minimum:

```sh
cmake --build build
ctest --test-dir build
```

If targeted CPU tests exist, run them separately and report exact commands.

If Harte tests can be run locally in this repo, run the relevant documented and undocumented subsets and report exact commands. If they cannot be run locally, say so explicitly and identify what evidence was used instead.

Do not run `./build/c64m` without a timeout.

## Hand-Off Report

End with a concise hand-off report containing:

```text
- branch name;
- base commit and final commit hash, or note that changes are uncommitted;
- exact commands run;
- files inspected;
- files changed;
- tests run and results;
- Harte coverage scope found;
- opcode families already covered;
- opcode families added, if any;
- opcode families explicitly deferred;
- C64 integration concerns found, if any;
- whether practical undocumented opcode coverage is complete for the current milestone;
- known limitations;
- recommended next step.
```
