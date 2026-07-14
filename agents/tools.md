# Tools, parsers, and shared utilities

## Tools

- `src/tools/assembler`: shared two-pass assembler library used by the frontend,
  runtime, and `c64masm`; output-target callbacks are optional and absent in the
  in-emulator host.
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
callbacks and the CLI supplies file-output targets.

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

CMake builds component static libraries and 49 registered tests. Add a focused test
with a new behavior; do not use documentation or a phase name as evidence that an
old implementation still exists. Current parser/assembler tests are under
`tests/tools`; audio/BASIC/paste tests are under `tests/util`.
