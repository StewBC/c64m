# c64m Assembler — Implementation Status

See `DESIGN.md` for full design. This file tracks what is done, in progress, and pending.

Legend: `[ ]` not started · `[~]` in progress · `[x]` done

---

## First Milestone Target

Phases 1–11 complete = can assemble real programs with labels, expressions, .org, data directives, .include, all core 6502 opcodes, .if/.else/.endif, and .define. Sufficient for writing emulator test code. Loops/macros/scopes come after.

---

## Phase 1 — Foundation

- [x] `dynarray.h/c` — copy verbatim from a2m `src/dynarray.h/c`
- [x] `errorlog.h/c` — local ERRORLOG support copied from a2m `src/utils/errorlog.h/c`
- [x] `err.h/c` — copy from a2m `src/asm/asm_err.h/c`; rename files; fix include
- [x] `asm_lib.h` — internal hub (see DESIGN.md §asm_lib.h)
- [x] `CMakeLists.txt` — assembler library target

## Phase 2 — File and Token

- [x] `file.h/c` — ASM_FILE + FILE_FRAME, file_load, file_stack_push/pop/top, file_read_line, file_stack_reset_for_pass2; recursion-guard (no blanket include guard); path resolution relative to including file
- [x] `token.h/c` — cursor-based tokenizer: get_token, next_token, peek_next_op, expect_op

## Phase 3 — Define

- [x] `define.h/c` — DEFINE table, define_add, defines_free, define_substitute (word-boundary for identifiers; skip string literals; literal match for operator sequences)

## Phase 4 — Symbols and Scopes

- [x] `scope.h/c` — copy verbatim from a2m (fix includes)
- [x] `symbol.h/c` — copy verbatim from a2m (fix includes)

## Phase 5 — Expression Evaluator

- [x] `expr.h/c` — port from a2m; replace `as->input` refs with `as->cur`

## Phase 6 — Opcodes and gperf

- [x] `gperf.gperf` — port from a2m; remove 10 65C02-only opcodes (see DESIGN.md)
- [x] `gperf.h` — enum without removed opcodes
- [x] `gperf.c` — regenerate with gperf tool or hand-edit
- [x] `opcode.h/c` — port from a2m; remove rows for dropped opcodes

## Phase 7 — Segment and Emit

- [x] `segment.h/c` — port from a2m; remove redirect/banking fields
- [x] `emit.h/c` — port from a2m; simplify emit_byte (no redirect)

## Phase 8 — Assembler Skeleton

- [x] `asm.h` — ASSEMBLER struct (incl. if_stack/if_skip_depth, FILE_FRAME file_stack), CB_ASM_CTX, public API
- [x] `asm.c` — assembler_init (no address arg), assembler_shutdown, assembler_assemble (address arg; two-pass loop with pass-boundary resets; emit gated on pass==2)
- [x] `parse.h/c` — is_label, is_opcode, is_parse_dot_command, is_variable, is_address
- [x] parse_line dispatch loop
- [x] parse_if_skip (handles nested .if while skipping a false branch)

## Phase 9 — Core Parser

- [x] parse_label (store address symbol)
- [x] parse_address (* = expr)
- [x] parse_variable (ident = expr, ident++, ident--)
- [x] parse_opcode (all addressing modes)
- [x] parse_macro_if_is_macro (check macro table before treating as variable)

## Phase 10 — Data Dot Commands

- [x] `.org` / `* =`
- [x] `.byte`
- [x] `.word` / `.addr`
- [x] `.dword`
- [x] `.qword`
- [x] `.drow` / `.drowd` / `.drowq`
- [x] `.res`
- [x] `.align`
- [x] `.string` / `.asciiz`
- [x] `.strcode`
- [x] `.incbin`
- [x] `.include`
- [x] `.6502` / `.65c02`
- [x] `.define`

## Phase 11 — Conditional Assembly

- [x] `.if` / `.else` / `.endif` with IF_FRAME stack + if_skip_depth
- [x] `.defined` directive (test macro parameter presence)
- [x] `.lt` `.le` `.gt` `.ge` `.eq` `.ne` comparison operators in expressions

## Phase 12 — Loops

- [ ] `.for` / `.endfor`
- [ ] `.repeat` / `.endrepeat` / `.endrep`

## Phase 13 — Macros

- [ ] `.macro` / `.endmacro`
- [ ] `.local`
- [ ] macro invocation with parameter substitution
- [ ] MACRO_EXPAND stack for .local name mapping

## Phase 14 — Scopes and Segments

- [ ] `.scope` (anonymous and named)
- [ ] `.proc` / `.endproc`
- [ ] `.endscope`
- [ ] `.segdef` / `.segment`

## Phase 15 — c64m Integration

- [ ] `c64_assemble_file()` in runtime/debugger
- [ ] `assembler_walk_symbols()` helper
- [ ] Symbol loading into debugger symbol table
- [ ] Error display in UI

---

## Notes

- Phase 2: `file_load` reuses an already-loaded `ASM_FILE` when the same canonical path is included again, but still pushes a fresh `FILE_FRAME`; this allows repeated includes without duplicate buffers. On pass 2, missing files are fatal because conditionals must have loaded the same file set on pass 1.
- Phase 3: `.define` substitution is in-place on `as->line`, skips string literals, applies identifier word-boundary matching, allows literal operator-pattern replacement, silently replaces an existing define within a pass, and stops after 64 substitutions per line to catch cycles.
- Phase 4: scope/symbol modules are ported with local `asm_fnv_1a_hash` and `asm_strnicmp` helpers replacing a2m util calls. `MACRO_EXPAND`/`RENAME_MAP` live in `symbol.h` now so macro-local symbol support can compile before the macro parser phase.
- Phase 5: expression evaluator is ported to `as->token`/`as->cur`. It saves variable token slices before advancing, which matches the new tokenizer model. `current_output_address(as)` currently reads a temporary `as->output_address` shim until segment/emit state lands in Phase 7.
- Phase 6: gperf output was generated with `gperf --language=ANSI-C -c --output-file=src/tools/assembler/gperf.c src/tools/assembler/gperf.gperf`; the local gperf defaults to K&R C otherwise. 65C02-only mnemonic rows are removed from gperf and opcode tables, while 65C02-only addressing variants on shared mnemonics remain flagged in `asm_opcode_type`.
- Phase 7: `current_output_address(as)` now reads the active segment. `emit_byte` always advances the segment output address, but only invokes the callback on pass 2 and skips `do_not_emit` segments. `emit_string` walks `TOKEN_STR` slices instead of old raw input/token-start pointers.
- Phase 8: `assembler_assemble` now runs the two-pass line pipeline: file read, comment strip, skip-state handling, define substitution, tokenize, dispatch. `parse.c` contains classification helpers and explicit stubs for Phase 9/10/11 parser bodies, plus `.6502`/`.65c02` handling and skip scanning for false `.if` branches.
- Phase 9: core parser behavior is live for labels, address assignment, variables, and opcode addressing modes. `parse_macro_if_is_macro` remains a no-op until the macro table exists in Phase 13, but the dispatch hook is in place before variable parsing.
- Phase 10: data directives are live for `.org`, scalar data widths, reverse-order variants, `.res`, `.align`, strings, `.strcode`, `.include`, `.incbin`, CPU selection, and `.define`. `asm.h` is now self-contained for external callers via `asm_common.h`; loaded files are stored as stable heap objects so include frames survive file-array growth; symbol names are owned copies so pass-2 lookups do not depend on pass-1 line buffers.
- Phase 11: conditional assembly is live for `.if` / `.else` / `.endif`, including nested skipped blocks through `if_skip_depth`. `.if` expressions now reject pass-1 unknowns/forward references for stable branch selection. `.defined` and `.lt`/`.le`/`.gt`/`.ge`/`.eq`/`.ne` are handled by the tokenizer/expression evaluator. `assembler_conditionals` covers true/false branches, skipped nested branches, comparisons, `.defined`, and forward-reference rejection.
