# C64MCPU_NEW.md

# CPU Remaining Work and Undocumented Opcode Audit Plan for c64m

## Purpose

This document defines the high-level CPU work relevant to the current PAL/NTSC
fidelity milestone.

The known CPU/memory/runtime implementation is already sufficient for boot and
normal execution. The main remaining question is practical 6510 undocumented
opcode compatibility.

## Goal

Audit the 6510 CPU implementation for undocumented opcode coverage used by
ordinary C64 software. If coverage is missing or incorrect, write a focused
implementation guide for the practical C64 set.

## Scope

In scope:

```text
- audit current opcode decode and execution behavior;
- audit current CPU validation tests;
- identify undocumented opcodes implemented, missing, stubbed, or wrong;
- decide whether practical C64 compatibility requires implementation work;
- if needed, define a bounded implementation guide.
```

Out of scope for this milestone:

```text
- sub-cycle CPU pin timing;
- exact RDY/AEC hardware pin modeling beyond current BA stall behavior;
- undocumented opcode electrical edge cases not used by selected software;
- every possible illegal opcode variant if a smaller practical set is accepted;
- rewriting the CPU core for style or abstraction reasons.
```

## Why This Matters

A meaningful amount of C64 software uses undocumented 6502/6510 opcodes. If the
CPU treats these as traps, fixed NOPs, or partially wrong operations, software may
crash or behave incorrectly.

This should be handled as an audit first, not assumed missing.

## Audit Questions

The audit should answer:

```text
- Does the CPU decode all 256 opcodes?
- Which undocumented opcodes are implemented?
- Which undocumented opcodes are treated as NOPs?
- Which undocumented opcodes are intentionally unsupported?
- Are addressing modes correct?
- Are flags correct?
- Are cycle counts good enough for current bus timing?
- Are read/modify/write bus events correct enough for VIC BA behavior?
- Do existing CPU tests cover undocumented opcodes?
```

## Practical Opcode Families To Check

The detailed audit should explicitly check at least:

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

It should also check common unofficial NOP forms and their addressing modes.

## Status Categories

Each opcode or opcode family should be classified as:

```text
implemented and tested
implemented but untested
stubbed or NOPed
missing
implemented with known caveats
not accepted for this milestone
```

## Acceptance Direction

The audit is complete when:

```text
- the CPU opcode table or decoder has been inspected;
- tests have been inspected;
- undocumented opcode support is summarized in STATUS.md or a linked audit note;
- missing required behavior has a follow-on implementation guide;
- no speculative CPU rewrite is recommended.
```

If implementation is needed, the follow-on guide should require:

```text
- exact operation semantics;
- flags;
- addressing modes;
- cycle counts;
- bus read/write sequence where relevant;
- regression tests;
- compatibility diagnostics.
```

## Suggested Detailed Specs To Write Later

```text
1. 6510 undocumented opcode audit checklist.
2. 6510 practical undocumented opcode implementation guide, only if needed.
3. 6510 opcode validation corpus guide.
```
