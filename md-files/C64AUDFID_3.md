# C64AUDFID_3.md
# Practical 6510 Undocumented Opcode Audit Guide

## Component

C64MCPU_NEW

## Status

Coding-agent-ready audit guide. Implementation is conditional.

## Purpose

Audit practical 6510 undocumented opcode support for the PAL/NTSC fidelity
milestone. Do not assume implementation is missing. First inspect the current CPU
core and tests. Write a follow-on implementation guide only if this audit finds
real gaps that affect ordinary C64 software compatibility.

## Required Reading Before Auditing

Read these in order:

1. `AGENTS.md`
2. `MASTER.md`
3. `STATUS.md`
4. `C64MCPU_NEW.md`
5. This guide

## Goal

Produce a concrete audit result that answers:

- Does the CPU decode all 256 opcodes?
- Which undocumented opcodes are implemented?
- Which are implemented but untested?
- Which are NOPed, stubbed, missing, or intentionally unsupported?
- Are addressing modes, flags, cycle counts, and memory accesses good enough for
  ordinary C64 software?
- Is a follow-on implementation guide required?

## In Scope

- Inspect CPU decode and execution tables/switches.
- Inspect opcode metadata, cycle counts, and addressing mode helpers.
- Inspect CPU validation tests.
- Add audit-only notes or test inventory if useful.
- Add small characterization tests if the current harness can run them safely.
- Update `STATUS.md` with the audit result.
- If gaps are found, create a follow-on implementation plan or TODO section.

## Explicit Non-Goals

Do not implement these as part of the audit unless the audit explicitly finds and
scopes a practical undocumented-opcode gap:

- CPU core rewrite.
- Exact sub-cycle RDY/AEC pin modeling.
- Open-bus behavior.
- Every obscure illegal opcode electrical edge case.
- New emulator timing architecture.
- IEC serial bus.
- 1541 emulation.
- Fast loaders.
- D64 writes.
- Cartridge support.
- CIA Phase I or Phase J.
- VIC-II light pen.

## Audit Output File

Create a concise audit note in the repo if no equivalent exists:

```text
docs/C64AUDFID_3_CPU_UNDOC_AUDIT.md
```

If the repo does not use `docs/`, place it beside the existing planning docs or
use the project-preferred location.

The audit note must include:

- CPU source files inspected.
- Test files inspected.
- Opcode coverage table or family summary.
- Gaps found.
- Decision: no implementation needed, tests only needed, or implementation guide
  needed.

## Files To Inspect

Find actual names in the repo. Likely areas include:

```text
src/machine/cpu_6510.*
src/machine/cpu.*
src/machine/6510.*
src/tools/disasm_6502.*
tests/*cpu*
tests/*6510*
tests/*opcode*
STATUS.md
```

Do not rely on filenames alone. Search for opcode values and family names.

## Practical Opcode Families To Audit

Audit at least these families:

```text
LAX
SAX
DCP
ISC / ISB
SLO
SRE
RLA
RRA
ANC
ALR
ARR
AXS / SBX
LAS
TAS / SHS
AHX / SHA
SHX
SHY
XAA
```

Also audit common unofficial NOP forms:

```text
1-byte NOPs
2-byte immediate/zero-page NOPs
3-byte absolute/absolute,X NOPs
page-crossing cycle behavior where relevant
```

## Required Classification

Classify each opcode family as one of:

```text
implemented and tested
implemented but untested
implemented with known caveats
stubbed or NOPed
missing
not accepted for this milestone
```

If possible, classify individual opcodes too. At minimum, family-level
classification must include a note on addressing modes.

## Semantics Checklist

For each implemented or proposed opcode family, check:

- Operation semantics.
- Affected registers.
- Affected flags.
- Addressing modes.
- Cycle counts.
- Page-crossing behavior where applicable.
- Read/write or read/modify/write sequence where relevant.
- Whether behavior interacts with VIC BA stalls through existing bus access
  modeling.
- Whether tests cover at least one opcode per addressing mode.

## Opcode Family Expected Semantics Summary

Use a trusted 6502/6510 reference while auditing. The summaries below are an
audit aid, not a substitute for checking the selected reference.

```text
LAX: load memory into A and X; set N/Z.
SAX: store A & X.
DCP: decrement memory, then compare with A; set CMP-style flags.
ISC/ISB: increment memory, then SBC memory.
SLO: ASL memory, then ORA memory into A.
SRE: LSR memory, then EOR memory into A.
RLA: ROL memory, then AND memory into A.
RRA: ROR memory, then ADC memory.
ANC: AND immediate, set N/Z, copy bit 7 to C.
ALR: AND immediate, then LSR A.
ARR: AND immediate, then ROR-like ADC-related flag behavior.
AXS/SBX: A & X minus immediate into X; set flags according to selected reference.
LAS: load A, X, SP from memory & SP; set N/Z.
TAS/SHS, AHX/SHA, SHX, SHY, XAA: unstable or address-high-byte dependent; decide if accepted.
NOP unofficial: consume operand bytes and cycles without state change except PC/cycles.
```

For unstable families, do not over-implement unless ordinary software need is
identified. It is acceptable to classify some as not accepted for this milestone
if documented.

## Audit Procedure

1. Build the project and run existing tests to establish baseline.
2. Locate CPU opcode dispatch.
3. Confirm all 256 opcode values have defined behavior.
4. Locate cycle-count metadata or cycle increment logic.
5. Locate addressing mode helpers.
6. Locate read/write bus helper calls used by RMW instructions.
7. Locate disassembler handling for undocumented opcodes, if present.
8. Locate CPU tests and opcode validation tests.
9. Fill the classification table.
10. Run existing CPU tests.
11. Add characterization tests only if they are small and do not turn the audit
    into a broad implementation phase.
12. Write the audit note.
13. Update `STATUS.md`.

## Required Audit Table Format

Use this table shape in the audit note:

```text
Family | Opcodes checked | Implementation status | Test status | Caveats | Decision
```

Example row:

```text
LAX | A7,B7,AF,BF,A3,B3 | implemented | partial tests | cycle count unchecked for abs,Y | add tests only
```

## Conditional Implementation Trigger

Write a follow-on implementation guide only if one of these is true:

- A practical family is missing.
- A practical family is decoded as NOP.
- Flags or register results are wrong.
- Addressing modes used by ordinary C64 software are missing.
- Cycle counts are wrong enough to break existing bus/VIC timing assumptions.
- Tests are absent for behavior that code claims as implemented and the risk is
  high enough that tests alone should be added.

If only tests are missing, do not write a broad implementation guide. Write a
small test guide or add the tests directly if project workflow allows.

## Conditional Implementation Guide Requirements

If gaps are found, the follow-on implementation guide must include:

- Exact opcode byte list.
- Exact operation semantics.
- Exact flags.
- Exact addressing mode behavior.
- Exact cycle counts.
- Bus read/write sequence for RMW operations.
- Regression tests.
- Compatibility diagnostics or selected external references.
- Explicit non-goals.

## STATUS.md Update

After the audit, update `STATUS.md` with one of these outcomes:

```text
Outcome A: practical undocumented opcode coverage audited and sufficient.
Outcome B: implementation sufficient but tests incomplete; test work planned or added.
Outcome C: gaps found; follow-on implementation guide required.
```

Include a link or filename for the audit note.

## Acceptance Checklist

The audit is complete when:

- CPU opcode dispatch has been inspected.
- All 256 opcodes are accounted for.
- Practical undocumented opcode families are classified.
- Addressing mode coverage is checked.
- Flag behavior is checked or identified as unverified.
- Cycle behavior is checked or identified as unverified.
- Existing CPU tests are inspected.
- Existing tests still pass.
- An audit note exists or `STATUS.md` contains equivalent detail.
- Missing behavior, if any, has a bounded follow-on plan.
- No speculative CPU rewrite is recommended.
