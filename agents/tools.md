# Tools, parsers, and utilities

## Tools

- `src/tools/am65`: two-pass assembler library used by the frontend, runtime,
  and standalone `am65`. The C64 in-emulator host advertises only `map`,
  ignores `file=`, and writes named targets to live RAM; any other `dest=`
  name is an assembly error. Opt-in segment auto-adjust retries pass-1 layout
  up to three times from structured overlap suggestions; pass 2 runs only
  after layout stabilizes. Segments tagged `locked` in `.segdef` are anchors
  auto-adjust never moves. A plain `noemit` segment may not overlap any
  segment. The sanctioned overlay is `.segdef "n", reclaim="host"`: implicit
  noemit, inherits the host start, and must not be larger than the host.
- `src/tools/disasm_6502`: 6502 disassembly and addressing-mode metadata.
  The frontend adds effective-address annotations from copied snapshots.
- `src/tools/symbols`: symbol-file parsing for debugger and control port.
- `src/tools/d64`, `t64`, `crt`, `g64`: format parse/decode only. Runtime
  decides inject, mount, attach, or persist.

A parser must not call runtime, SDL, or frontend. The assembler library is
independent of the live machine; runtime supplies target callbacks, selects
the 6502 profile, and predefines `AM65=0` plus `C64=1`. The CLI supplies
file-output targets, predefines `AM65=1`, and does not define a machine.
The subtree also supports opt-in `.65c02`, `.rockwell`, and `.wdc` without
changing the C64 default.

`src/tools/assembler/` is an empty leftover; the real tree is `am65/`.

## Utilities

`src/util`: config, logging/helpers, mutex/condition/thread wrappers,
message queues, SPSC audio buffer, paste-event parser, BASIC V2
tokenizer/detokenizer. Util must not acquire SDL or machine ownership.

Paste parser output is the runtime event format used by Type/paste.
BASIC V2 does not implement extension dialects.

Public headers stay C99-compatible. `audio_buffer` is the documented
exception: its implementation uses C11 atomics privately.

For a new parser, identify buffer ownership and the error contract in the
existing header. Tests should exercise malformed input and boundary sizes.

## Tests

Add a focused test with a new behavior. Current parser/assembler tests are
under `tests/tools`; audio/BASIC/paste under `tests/util`. Suite size and
SKIP rules: `testing.md`.

When touching assembler opcode/addressing tables (`gperf`, `opcode.c`,
`parse.c`), run the standing coverage matrix (built, not `add_test`):

```text
cmake --build build --target test_assembler_opcode_matrix
./build/test_assembler_opcode_matrix
```

It round-trips every documented NMOS opcode through the disassembler's
independent 256-entry table. 65c02-only variants have no disasm oracle.
