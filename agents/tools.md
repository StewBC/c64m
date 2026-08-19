# Tools, parsers, and shared utilities

## Tools

- `src/tools/am65`: Git-subtree copy of the neutral two-pass assembler library
  used by the frontend, runtime, and standalone `am65`; output-target callbacks
  are optional and absent in the in-emulator host. Its opt-in segment
  auto-adjust mode performs up to three
  fresh pass-1 layout retries using structured overlap suggestions; pass 2 only
  runs after layout stabilizes, and hosts can walk the applied address map.
  Segments tagged `locked` in `.segdef` are anchors auto-adjust never moves; if
  lower segments overrun a locked segment the retry is abandoned and assembly
  fails naming the anchor rather than reshuffling around it. A plain `noemit`
  segment may not overlap any segment (checked, hard error). The sanctioned
  overlay is a reclaim segment, `.segdef "n", reclaim="host"`: implicitly noemit,
  it inherits the emitted host's start (re-read on each auto-adjust re-parse, so
  it follows the host with no extra machinery) and is validated to be no larger
  than the host. The noemit/reclaim rules are enforced by `check_noemit_reclaim`
  separately from the emit-only overlap/auto-adjust machinery, which continues to
  ignore all `do_not_emit` segments.
- `src/tools/disasm_6502`: 6502 disassembly and opcode addressing-mode metadata.
  The frontend adds effective-address/value annotations from copied CPU/memory
  snapshots.
- `src/tools/symbols`: symbol-file parsing/table support for debugger and control
  port.
- `src/tools/d64`, `t64`, `crt`, `g64`: reusable format parsing. They do not own the
  live machine or host UI policy.

The parser boundaries are deliberately simple: D64/T64/CRT/G64 return parsed or
decoded data; runtime decides whether to inject, mount, attach, or persist it. A
format parser must not call runtime, SDL, or frontend code. The assembler library
is likewise independent of the live machine; the runtime supplies its target
callbacks, explicitly selects the 6502 profile, and the CLI supplies file-output
targets. The subtree also supports opt-in `.65c02`, `.rockwell`, and `.wdc`
profiles without changing the C64 default.

## Utilities

`src/util` contains config, logging/helpers, mutex/condition/thread wrappers,
message queues, SPSC audio buffer, paste-event parser, and stock BASIC V2
tokenizer/detokenizer. Keep util dependency-safe; it must not acquire SDL or machine
ownership.

Paste parser output is the runtime event format used by Type/paste actions. BASIC V2
does not implement extension dialects.

For a new parser or utility, first identify ownership of allocated buffers and the
error contract in the existing header. Tests should exercise malformed input and
boundary sizes, not only a successful fixture. Keep public headers C99-compatible;
the audio buffer is the documented exception where its implementation uses C11
atomics privately.

## Build/test ownership

CMake builds component static libraries and 69 registered tests. Add a focused test
with a new behavior; do not use documentation or a phase name as evidence that an
old implementation still exists. Current parser/assembler tests are under
`tests/tools`; audio/BASIC/paste tests are under `tests/util`.

When touching the assembler's opcode/addressing tables (`gperf`, `opcode.c`,
`parse.c`), run the standing coverage matrix
`tests/tools/test_assembler_opcode_matrix.c`. It is built with the tree but is
**not** an `add_test` gate - run it by hand:

```text
cmake --build build --target test_assembler_opcode_matrix
./build/test_assembler_opcode_matrix        # PASS 151/151 when clean
```

It round-trips every documented NMOS opcode through the *disassembler's*
independent 256-entry table (assemble the disassembly, check opcode byte and
length), so it catches silent encoding drift a table-vs-itself check cannot -
e.g. `lsr <zp>` dropping its operand byte, or `ldx/stx <zp>,y` mis-promoting to
absolute,Y. 65c02-only variants have no disasm oracle and are not covered.
