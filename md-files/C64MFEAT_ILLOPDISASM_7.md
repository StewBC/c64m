# C64MFEAT_ILLOPDISASM_7 — Illegal / undocumented opcode disassembly

## Status of this document

Implementation guide. Fully agent-ready. Feature #7 of the "next features" list.
Smallest, most self-contained item — a debugger quality-of-life fix.

**Milestone scope:** Effectively in scope as a debugger-quality item.
`docs/status/DEFERRED.md` and `docs/status/CPU_MACHINE.md` both note: "The
debugger disassembler still renders undocumented opcode bytes as `.BYTE` rather
than illegal-opcode mnemonics." The CPU already *executes* all 256 opcodes
(`docs/status/CPU_MACHINE.md`: explicit dispatch for all 256 slots), so the
disassembler is the only place that pretends they do not exist.

## Required reading before starting

1. `AGENTS.md`.
2. `STATUS.md`.
3. `docs/status/CPU_MACHINE.md` — the list of implemented undocumented opcode
   families (SLO, RLA, SRE, RRA, SAX, LAX, DCP, ISC/ISB, NOPs, ANC, ALR, ARR,
   AXS/SBX, LAS, AHX/SHA, SHX, SHY, TAS/SHS, XAA/ANE, LAX #imm, JAM/KIL).
4. `docs/status/FRONTEND_DEBUGGER.md` — the disassembly view and its
   effective-address annotation.
5. This document.

## Goal

Make the debugger disassembler emit standard illegal-opcode mnemonics (with the
correct addressing mode and instruction length) for all 256 opcodes, instead of
falling back to `.BYTE $xx`. Keep them visually distinguishable as illegal (they
are not assemblable by default).

## Non-goals

- No CPU behavior change — this is disassembly text only.
- No requirement that the assembler tab *accept* these mnemonics (see Open
  Questions; can be a follow-on).
- No new addressing modes; the existing `disasm_6502_mode` enum already covers
  every mode the illegal opcodes use.

## Current state (verified against source)

- The disassembler is `src/tools/disasm_6502/disasm_6502.c`. It uses a 256-entry
  table:
  ```c
  typedef struct opcode_info { const char *mnemonic; ... uint8_t length; ... disasm_6502_mode mode; } opcode_info;
  static const opcode_info opcode_table[256] = { ... };   /* src/tools/disasm_6502/disasm_6502.c:15 */
  ```
- Undocumented opcodes currently have `mnemonic == NULL`. Decode falls back to
  `.BYTE`:
  ```c
  if (info.mnemonic == NULL) {          /* src/tools/disasm_6502/disasm_6502.c:174 */
      line.length = 1;
      line.forced_byte = true;
      snprintf(line.text, sizeof(line.text), ".BYTE $%02X", opcode);
      ...
  }
  ```
  Note this also forces `length = 1`, which is wrong for multi-byte illegal
  opcodes and desynchronizes the following disassembly lines.
- Helpers keyed on the table:
  `disasm_6502_instruction_length()` (`:95`),
  `disasm_6502_opcode_is_valid()` (`:100`, returns `mnemonic != NULL`),
  `disasm_6502_opcode_mode()` (`:105`).
- Addressing modes available: `disasm_6502_mode` enum
  (`src/tools/disasm_6502/disasm_6502.h:9-22`) — IMP, ACC, IMM, ZP, ZPX, ZPY,
  ABS, ABSX, ABSY, IND, INDX, INDY, REL. All illegal opcodes fit these.

## Design

Fill in every remaining slot of `opcode_table[256]` with the standard illegal
mnemonic, correct length, and correct addressing mode. Then mark illegal opcodes
so the UI can render them distinctly and so callers that mean "is this a *legal*
opcode" still work.

### Mark illegal without breaking `is_valid`
Add an `is_illegal` flag to `opcode_info` (default false for the 151 documented
opcodes). Keep `disasm_6502_opcode_is_valid()` semantics deliberate:
- Recommended: `disasm_6502_opcode_is_valid()` returns true for *any* opcode with
  a mnemonic (now all 256). Add a separate
  `bool disasm_6502_opcode_is_illegal(uint8_t)` for callers that care.
- **Audit callers** of `disasm_6502_opcode_is_valid` and the `mnemonic == NULL`
  fallback before changing semantics — some code may currently rely on NULL to
  mean "undocumented" (e.g. the effective-address annotation added recently, see
  `docs/status/FRONTEND_DEBUGGER.md`). Preserve their intent.

### Rendering
- For illegal opcodes, render the mnemonic exactly like a legal one via the
  existing operand formatting (`src/tools/disasm_6502/disasm_6502.c:181-228`) —
  the mode-driven `snprintf` block already handles every mode, so once the table
  has the right `mode`, operands format for free.
- Distinguish illegal visually. Recommended: prefix the mnemonic with `*`
  (a long-standing convention, e.g. `*SLO $44`) or set `forced_byte`-adjacent flag
  the UI can color. Do **not** reuse `forced_byte` (that specifically means "this
  is a `.BYTE` directive"); add a small `bool illegal` to `disasm_6502_line`
  (`src/tools/disasm_6502/disasm_6502.h:56`).
- Only genuinely undecodable inputs (empty buffer) keep the `.BYTE` path.
- JAM/KIL (`$02,$12,$22,...`) are 1-byte; render as `JAM` (or `KIL`), not `.BYTE`.

### The opcode table content
Use the canonical NMOS 6502 illegal-opcode table. Map each undocumented opcode to
mnemonic + mode + length. Cross-check the mnemonics against the families the CPU
core already implements (`docs/status/CPU_MACHINE.md` / `c6510_inln.h`) so the
disassembly names match the emulated behavior. Canonical set (mnemonic — modes):

```
SLO/ASO, RLA, SRE/LSE, RRA         (zp, zpx, abs, absx, absy, (zp,x), (zp),y)
SAX/AXS  (store)                   (zp, zpy, abs, (zp,x))
LAX                                (zp, zpy, abs, absy, (zp,x), (zp),y)  + LAX #imm (unstable)
DCP/DCM, ISC/ISB/INS               (zp, zpx, abs, absx, absy, (zp,x), (zp),y)
ANC (x2), ALR/ASR, ARR, ANE/XAA, SBX/AXS, LAS/LAR   (immediate / absy)
SHA/AHX, SHX/SXA/A11, SHY/SYA/A11, TAS/SHS/XAS       (absy / absx / (zp),y)
NOP variants                       (imp, imm, zp, zpx, abs, absx)
SBC #imm ($EB)  == SBC immediate alias
JAM/KIL/HLT     ($02,12,22,32,42,52,62,72,92,B2,D2,F2)   (imp, length 1)
```

Getting the **length** right per slot is the important correctness point (it keeps
subsequent lines aligned). Every entry's length must match
`disasm_6502_instruction_length` expectations and the real opcode size.

## Implementation phases

### Phase 1 — Complete the table
- Fill all 256 `opcode_table` entries with mnemonic/mode/length; add `is_illegal`.
- Remove the `length = 1` forcing for known (now-non-NULL) opcodes; only the
  empty-buffer case forces `.BYTE`.

### Phase 2 — API + line flag
- Add `disasm_6502_opcode_is_illegal()`; add `bool illegal` to
  `disasm_6502_line`; set it in `disasm_6502_decode_line`.
- Audit and, if needed, adjust callers of `disasm_6502_opcode_is_valid`.

### Phase 3 — Frontend rendering
- In the disassembly view (`src/frontend/*`, see
  `docs/status/FRONTEND_DEBUGGER.md`), render illegal lines distinctly (color or
  `*` prefix). Confirm the recently-added effective-address/value column
  (`f0ec3d3`, `docs/status/FRONTEND_DEBUGGER.md`) still computes correctly for
  illegal opcodes that use indexed/indirect modes.

## Tests / smoke checks

- **Table completeness unit test** (`tests/tools/test_disasm_6502.c` or extend
  the existing disasm test): assert all 256 opcodes now have a non-NULL mnemonic
  and a length in {1,2,3}; assert a representative illegal opcode of each mode
  decodes to the expected text and length (e.g. `$07 -> SLO $nn`, len 2;
  `$0F -> SLO $nnnn`, len 3; `$02 -> JAM`, len 1; `$EB -> SBC #$nn`).
- **Alignment test:** decode a byte stream containing multi-byte illegal opcodes
  and assert the running address advances by the correct lengths (no
  desynchronization).
- **Smoke (manual):** open the debugger disassembly view over memory containing
  known illegal opcodes and confirm mnemonics render and are marked illegal.

## Docs to update on completion

- `docs/status/DEFERRED.md` — remove the "renders undocumented opcode bytes as
  `.BYTE`" line under CPU/machine.
- `docs/status/CPU_MACHINE.md` — remove the same note; mention disassembler now
  covers illegal opcodes.
- `docs/status/FRONTEND_DEBUGGER.md` — note the illegal-opcode rendering.
- `docs/status/TESTING.md` — new/extended disasm tests.

## Open questions / decisions for the author

1. **Assembler round-trip.** Decide whether the assembler tab should *accept*
   illegal mnemonics (with `*` or a directive). Recommended: out of scope here;
   disassembly-only. Note it in DEFERRED if you keep the asymmetry.
2. **Mnemonic dialect.** Multiple naming conventions exist (SLO/ASO, SAX/AXS,
   ISC/ISB/INS, LAX #imm variants). Pick one dialect and match it to the CPU
   core's comments in `c6510_inln.h` for consistency. Recommended: the "SLO/RLA/
   SRE/RRA/SAX/LAX/DCP/ISC" family names already used in `docs/status/CPU_MACHINE.md`.
3. **`is_valid` semantics.** Confirm no caller depends on
   `disasm_6502_opcode_is_valid` meaning "documented". If one does, keep a
   separate documented/illegal distinction rather than overloading it.
