# c64m Assembler — Design Document

## Original
The document src/tools/assembler/DESIGN.md
It now lives at: md-files/ASMDESIGN.md
The document src/tools/assembler/STATUS.md
It now lives at: md-files/ASMSTATUS.md

They were renamed and moved after the completion of PHASE 15.

## Context

This document guides implementation of a 6502/6510 assembler for c64m. It is written for a Claude instance with read access to both:
- `/Users/swessels/Develop/github/personal/a2m/src/asm/` — the reference assembler
- `/Users/swessels/Develop/github/personal/c64m/src/tools/assembler/` — the target

The a2m assembler is the direct ancestor of this design. Some files come over verbatim, some are rewritten. The reasons for the differences are documented below. Always read the a2m source before implementing a component; it is the ground truth for behaviour.

---

## Why Not Port a2m Directly

1. **Parsing in place.** The a2m tokenizer uses a `token_start`/`input` two-pointer pair into the raw source buffer. This makes it impossible to mutate the source before tokenizing, which is required for `.DEFINE`.
2. **`.DEFINE` is a first-class requirement.** It must be a text-level substitution pass over a mutable line buffer before any tokenization. The a2m macro path does something similar but it is bolted on and cannot handle operator sequences like `!=`.
3. **Apple II baggage.** The `TARGET`/`output_redirect` machinery exists to route assembled bytes to different Apple II memory banks (`aux`, `lc2`, etc.). The C64 has a simpler flat memory model. This machinery will be simplified or removed.

---

## Feature Set

Same as a2m (see `../a2m/manual/manual.md` § "Assembler Features and Syntax"), plus:

- `.define <from> <to>` — text-level substitution applied to each line before tokenizing
- Default CPU mode: 6502/6510 (not 65C02)

Omitted (not needed for C64):
- Named output targets with `file=` and `dest=` options on `.scope`
- `output_redirect_start/end/release` callbacks
- The `_asm6502_tool` built-in variable (no standalone tool, emulator only)

---

## File Map

All files live under `src/tools/assembler/`. Columns: **Origin** = verbatim | modified | new.

| File | Origin | Notes |
|------|--------|-------|
| `dynarray.h` | verbatim | Copy from a2m — `src/dynarray.h` |
| `dynarray.c` | verbatim | Copy from a2m — `src/dynarray.c` |
| `errorlog.h` | modified | Copy support type/API from a2m `src/utils/errorlog.h`; keep assembler self-contained |
| `errorlog.c` | modified | Copy support implementation from a2m `src/utils/errorlog.c`; fix includes |
| `err.h` | verbatim | Copy from a2m `asm_err.h` |
| `err.c` | verbatim | Copy from a2m `asm_err.c` |
| `expr.h` | verbatim | Copy from a2m |
| `expr.c` | modified | Adjust `#include` and token accessor calls for new tokenizer |
| `scope.h` | verbatim | Copy from a2m |
| `scope.c` | verbatim | Copy from a2m |
| `symbol.h` | verbatim | Copy from a2m |
| `symbol.c` | verbatim | Copy from a2m |
| `opcode.h` | modified | Drop 65C02-only opcodes from enum; keep `.65c02` gate |
| `opcode.c` | modified | Port opcode table, 6502 rows only |
| `gperf.h` | modified | Remove 65C02-only opcode IDs from enum |
| `gperf.c` | modified | Regenerate or hand-edit; drop DEA/INA/BRA/STZ/TRB/TSB/PHX/PHY/PLX/PLY |
| `gperf.gperf` | modified | Port from `a2m/gperf/asm6502.gperf`; remove 65C02-only entries |
| `emit.h` | modified | Simplify: no `output_redirect` calls |
| `emit.c` | modified | Simplify: single target, no redirect |
| `segment.h` | modified | Remove TARGET redirect fields |
| `segment.c` | modified | Simplify add_target |
| `token.h` | new | Rewritten tokenizer — cursor into `line[]` |
| `token.c` | new | Rewritten tokenizer |
| `file.h` | new | `ASM_FILE`, file loading, include stack |
| `file.c` | new | Replaces a2m `incl_fls.c` |
| `define.h` | new | `DEFINE` table + line substitution |
| `define.c` | new | |
| `parse.h` | modified | Similar public API; rewritten internals |
| `parse.c` | modified | Rewritten for line-buffer model; port dot-command handlers from a2m |
| `asm.h` | modified | `ASSEMBLER` struct; simplified `CB_ASM_CTX` |
| `asm.c` | modified | `assembler_init/assemble/shutdown`; new main parse loop |
| `asm_lib.h` | new | Internal include hub (replaces a2m `asm_lib.h`) |
| `CMakeLists.txt` | new | Build integration |

---

## Architecture

### Pipeline

Every line of source passes through these stages in order:

```
ASM_FILE.buf
  → read_next_line()        copy up to \n into as->line
  → strip_comment()         truncate at ; (respecting strings)
  → define_substitute()     text-scan: replace 'from' with 'to' in as->line
  → as->cur = as->line      tokenizer cursor reset
  → parse_line()            dispatch: label / opcode / dot-command / variable / macro
  → emit_*()                output_byte callback
```

Loops and macros re-enter the pipeline by pushing/restoring `FILE_FRAME` positions that point into an existing `ASM_FILE.buf`. They do not maintain separate text buffers.

### Two Passes

Pass 1 defines symbols; forward references become `SYMBOL_UNKNOWN`. Pass 2 resolves them. This is identical to a2m. The `ASM_ERR_DEFINE`/`ASM_ERR_RESOLVE`/`ASM_ERR_FATAL` error classes from a2m are kept verbatim.

---

## Key Structs

### `CB_ASM_CTX` — simplified, no redirect

```c
typedef void (*asm_output_byte_fn)(void *user, uint16_t addr, uint8_t val);

typedef struct {
    void *user;
    asm_output_byte_fn output_byte;   // required
} CB_ASM_CTX;
```

Note: if named `.scope "n" file=...` targets are needed later, add `redirect_start/end/release` matching a2m's shape. For now, omit.

### `ASM_FILE` — replaces a2m `INCLUDE_FILES`

```c
typedef struct {
    char   *display_name;   // path used in error messages (owned)
    char   *buf;            // entire file content, null-terminated (owned)
    size_t  size;           // strlen(buf)
} ASM_FILE;

typedef struct {
    ASM_FILE   *file;       // which file's buf we're reading
    const char *read_ptr;   // current position within file->buf
    size_t      line_num;   // 1-based line counter for this frame
    int         is_macro;   // 1 if this frame is a macro invocation
} FILE_FRAME;
```

`files` (DYNARRAY of ASM_FILE) holds every loaded file; these live for the whole assembly so body pointers into `buf` remain valid. `file_stack` (DYNARRAY of FILE_FRAME) is the active parse stack. `current_file` is a shortcut to `file_stack[top].file`. Macro invocations push a FILE_FRAME with `is_macro=1` pointing into the macro's body file at `body_start`; natural pop restores the caller's `read_ptr` and `line_num` without mutating any shared object.

### `DEFINE` — new

```c
typedef struct {
    char *from;     // the text to find (owned)
    int   from_len;
    char *to;       // the replacement text (owned)
    int   to_len;
} DEFINE;
```

The `defines` DYNARRAY in `ASSEMBLER` holds all active `DEFINE` entries. There is no scope: defines are global for the entire assembly (matching C `#define` behaviour).

### `ASSEMBLER` — the central state

```c
#define ASM_MAX_LINE 1024

typedef struct ASSEMBLER {
    CB_ASM_CTX   cb;              // output callback

    /* source input */
    DYNARRAY     files;           // ASM_FILE — all loaded files (never freed mid-assembly)
    DYNARRAY     file_stack;      // FILE_FRAME — active parse stack
    ASM_FILE    *root_file;       // first/root source file, reused for pass 2
    ASM_FILE    *current_file;    // shortcut: file_stack[top].file

    /* line buffer */
    char         line[ASM_MAX_LINE]; // mutable working copy
    int          line_len;
    const char  *cur;             // tokenizer cursor into line[]

    /* current token */
    TOKEN        token;

    /* .define */
    DYNARRAY     defines;         // DEFINE entries

    /* symbols & scopes (from a2m) */
    SCOPE       *root_scope;
    SCOPE       *active_scope;
    DYNARRAY     scope_stack;     // SCOPE*
    DYNARRAY     anon_symbols;    // uint16_t — anonymous label addresses

    /* opcode state */
    OPCODEINFO   opcode_info;

    /* segments & targets */
    TARGET      *active_target;
    DYNARRAY     targets;         // TARGET*

    /* macros */
    DYNARRAY     macros;          // MACRO definitions
    DYNARRAY     macro_stack;     // MACRO_EXPAND — active invocations
    int          macro_id;        // for .local unique name generation

    /* loops */
    DYNARRAY     loop_stack;      // LOOP

    /* conditional assembly */
    DYNARRAY     if_stack;        // IF_FRAME
    int          if_skip_depth;   // >0: inside a false branch, skipping lines

    /* two-pass */
    int          pass;            // 1 or 2
    int          valid_opcodes;   // 0 = 6502 (default), 1 = 65c02

    /* .strcode */
    const char  *strcode;         // active .strcode expression (ptr into line[])

    ERRORLOG    *errorlog;        // external; caller owns
} ASSEMBLER;

typedef struct {
    int was_true;    // the .if condition was true for this block
    int else_seen;   // .else has been processed for this block
} IF_FRAME;
```

Fields **not present** (vs a2m): `input`, `token_start`, `next_line_start`, `line_start`, `next_line_count`, `macro_buffers`, `input_stack`, `if_active`.

---

## Component Specs

### `dynarray.h/c`

Copy verbatim from a2m. These files have no a2m-specific dependencies. Place them in the assembler directory.

### `err.h/c`

Copy verbatim from a2m `asm_err.h`/`asm_err.c`. Rename files from `asm_err` to `err`. Update the `#include` to reference the local `asm_lib.h`. The `ASM_ERR_DEFINE`/`ASM_ERR_RESOLVE`/`ASM_ERR_FATAL` enum and `asm_err()` function are unchanged.

### `errorlog.h/c`

Copy the small `ERRORLOG` support type/API from a2m `src/utils/errorlog.h/c`, fixing includes to use local `dynarray.h`. Keep this in the assembler directory so the assembler remains self-contained and does not depend on c64m's `util` library.

### `file.h/c` (new)

Replaces a2m's `incl_fls.h/c` completely.

```c
// file.h
int  file_load(ASSEMBLER *as, const char *path);        // load file; push FILE_FRAME onto file_stack
int  file_stack_push(ASSEMBLER *as, ASM_FILE *f,
                     const char *read_ptr,
                     size_t line_num, int is_macro);    // push a frame
FILE_FRAME *file_stack_top(ASSEMBLER *as);              // peek without popping
void file_stack_pop(ASSEMBLER *as);                     // pop; update current_file
int  file_read_line(ASSEMBLER *as);                     // fill as->line; return 0 at EOF
```

`file_read_line` reads from `file_stack[top].read_ptr` up to (not including) `\n`, copies into `as->line`, advances `read_ptr` in the top frame, increments `line_num` in the top frame, returns 1. Returns 0 at EOF. Maximum `ASM_MAX_LINE - 1` bytes; `asm_err` + truncate if too long.

`file_load` reads the entire file into `buf` (malloc + fread), adds an `ASM_FILE` to `as->files`, then calls `file_stack_push` with `read_ptr = f->buf`, `line_num = 0`, `is_macro = 0`.

**No blanket include guard.** Repeated `.include` of the same file is allowed (needed for data tables). Only guard against recursion: before pushing, scan the current `file_stack` for any frame whose `file->display_name` matches the file being loaded; error if found (`Recursive include: %s`).

**Path resolution for `.include`/`.incbin`:** resolve relative paths against the directory of the currently-including file (`file_stack[top].file->display_name`), not the process cwd. Construct the full path by replacing the filename portion of the including file's path with the included filename.

**Between passes:** do not reload files. On pass 1, the first successful root `file_load` sets `as->root_file`. At the start of pass 2, reset `as->file_stack` to a single frame pointing at `as->root_file->buf` with `read_ptr = buf`, `line_num = 0`, and `is_macro = 0`. The `as->files` DYNARRAY is untouched (bufs remain valid).

### `define.h/c` (new)

```c
// define.h
void define_add(ASSEMBLER *as, const char *from, int from_len, const char *to, int to_len);
void define_substitute(ASSEMBLER *as);  // operates on as->line in place
void defines_free(ASSEMBLER *as);
```

`define_substitute` scans `as->line` left to right. At each position, try every `DEFINE` entry's `from` string. Matching rules:

- **Skip string literals:** if the scan position is inside a `"..."`, advance past the closing `"` without substituting. Track open/close with a simple `in_string` flag (handle `\"` escapes).
- **Identifier `from` patterns** (from starts with `[A-Za-z_]`): only match at a word boundary — the character before the match (if any) must not be `[A-Za-z0-9_]`, and the character after the match must not be `[A-Za-z0-9_]`. This prevents `FOO` from matching inside `MYFOO` or `FOO_BAR`.
- **Operator `from` patterns** (from starts with a non-identifier char, e.g. `!=`): match literally anywhere; word-boundary rules do not apply.

On match: replace the matched region with `to` in-place, shifting the remainder. Update `as->line_len`. If the result would exceed `ASM_MAX_LINE - 1`, `asm_err` and truncate.

After a successful substitution, restart the scan from the start of the replacement (not the start of the line) to handle cascades. Add an iteration limit (64 substitutions per line) to catch cycles.

**Between passes:** call `defines_free(as)` at the start of each pass before parsing begins. `.define` directives are re-parsed and re-accumulated in each pass. Redefining a name within a pass silently replaces the previous entry.

### `token.h/c` (new)

The tokenizer operates on `as->line[]` via the cursor `as->cur`. The `TOKEN` struct is identical to a2m:

```c
typedef enum { TOKEN_NUM, TOKEN_OP, TOKEN_VAR, TOKEN_STR, TOKEN_END } TOKENTYPE;

typedef struct {
    TOKENTYPE type;
    int64_t   value;
    char      op;           // operator character or token type ID
    const char *name;       // points into as->line[]
    uint32_t  name_length;
    uint32_t  name_hash;
} TOKEN;
```

`get_token(as)` — advance `as->cur` past whitespace, then lex one token into `as->token`. Rules:
- `\0` or end of line → `TOKEN_END`
- digit or `$` or `%` → `TOKEN_NUM` (parse number with same logic as a2m `tokens.c`)
- `'c'` → `TOKEN_NUM` (character literal)
- `"..."` → `TOKEN_STR`
- alpha / `_` / `::` → `TOKEN_VAR`; set `name` + `name_length` + `name_hash`
- anything else → `TOKEN_OP`; set `op`

`next_token(as)` — same as a2m: advance and return.
`peek_next_op(as, out)` — save cur, call get_token, restore cur.
`expect_op(as, op)` — call get_token, error if not the expected op.

Port the number parsing and hash logic verbatim from a2m `tokens.c`. The only change: `as->cur` replaces `as->input` and there is no `as->token_start` (use `as->token.name` for the start of a var/str token).

### `expr.h/c`

Port verbatim from a2m. The only changes needed are `#include "asm_lib.h"` and replacing any reference to `as->input` → `as->cur`, and removing references to `as->token_start` (use `as->token.name` instead). Review `expr.c` carefully; most of it calls `get_token`/`next_token` which already work on `as->cur`.

The anonymous symbol (`:` forward/backward label) logic is unchanged.

### `scope.h/c` and `symbol.h/c`

Copy verbatim from a2m. These have no dependency on the tokenizer or file model. Only change: include path adjustments.

### `gperf.h` and `gperf.c`

Port `a2m/gperf/asm6502.gperf` → `gperf.gperf`. Remove these 65C02-only opcodes:
`bra`, `dea`, `ina`, `phx`, `phy`, `plx`, `ply`, `stz`, `trb`, `tsb`

Keep all dot-commands unchanged. Regenerate `gperf.c` with:
```
gperf --output-file=gperf.c gperf.gperf
```
or hand-edit the existing `a2m/src/asm/gperf.c`.

`gperf.h` enum: copy from a2m, remove the 10 dropped opcode IDs. Keep `GPERF_DOT_*` enum unchanged.

`.65c02` is kept in the gperf table. When encountered, it sets `as->valid_opcodes = 1` and emits no error. However since the opcode table has no 65C02-only entries, any attempt to use one will produce an "unknown opcode" error. This means code that begins with `.65c02` as a convention (common in ca65 ports) assembles without a directive error. `.6502` is also kept as a no-op (it is already the default).

### `opcode.h/c`

Port `opcds.h/c` from a2m. Same 2D array `asm_opcode[mnemonic][addressing_mode]`. Remove rows for the 10 dropped opcodes. Address mode enum is unchanged (all 11 modes still valid for 6502). `address_mode_txt[]` unchanged.

### `segment.h/c`

Port from a2m with simplifications:
- Remove `TARGET.prev_target` (not needed without redirect)
- Remove `TARGET.name` and `TARGET.name_length` (single unnamed target)
- Keep `SEGMENT` struct unchanged
- `add_target` no longer takes a `target_ctx`; it creates the default segment at address 0

`CB_ASM_CTX` has no redirect callbacks, so `emit_byte` only advances the active segment state and, on pass 2, calls `as->cb.output_byte` directly. It never invokes the output callback on pass 1.

### `emit.h/c`

Port from a2m `emit.h/c`. Remove the `output_redirect` calls in `emit_byte`. Otherwise identical.

### `parse.h/c`

This is the most significant rewrite. The public surface matches a2m:

```c
void parse_address(ASSEMBLER *as);
void parse_dot_command(ASSEMBLER *as);
void parse_label(ASSEMBLER *as);
int  parse_macro_if_is_macro(ASSEMBLER *as);
void parse_opcode(ASSEMBLER *as);
void parse_variable(ASSEMBLER *as);
```

But the internals change. The main parse loop (in `asm.c`, called per line) is:

```c
static void parse_line(ASSEMBLER *as) {
    get_token(as);
    if (as->token.type == TOKEN_END) return;

    // label: ends with ':'
    if (is_label(as)) {
        parse_label(as);
        get_token(as);
        if (as->token.type == TOKEN_END) return;
    }

    // address assignment: * =
    if (is_address(as)) { parse_address(as); return; }

    // opcode (exactly 3 chars, gperf match)
    if (is_opcode(as)) { parse_opcode(as); return; }

    // dot command
    if (is_parse_dot_command(as)) { parse_dot_command(as); return; }

    // variable assignment (identifier followed by = or ++ or --)
    if (is_variable(as)) {
        if (parse_macro_if_is_macro(as)) return;
        parse_variable(as);
        return;
    }

    asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token: %.*s",
            (int)as->token.name_length, as->token.name);
}
```

Port all the dot-command handlers from a2m `parse.c` one by one. The logic is identical; only the file/cursor model changes.

**Loops and macros** in the rewritten model:

For `.for`/`.repeat`: after parsing the header, save `file_stack_top(as)->file`, `file_stack_top(as)->read_ptr`, and `file_stack_top(as)->line_num` as `body_file`, `body_start`, and `body_line`. On `.endfor`/`.endrepeat` with a continuing condition, restore those values into the top `FILE_FRAME` instead of doing an `INPUT_STACK` push/pop.

For `.macro`/`.endmacro`: `MACRO` stores `ASM_FILE *body_file`, `const char *body_start`, `size_t body_line`. On macro invocation, push a new `FILE_FRAME` with `file = macro->body_file`, `read_ptr = macro->body_start`, `line_num = macro->body_line`, and `is_macro = 1`. On reaching `.endmacro`, `file_stack_pop` naturally restores the caller's frame.

```c
// LOOP struct
typedef struct {
    LOOP_TYPE    type;
    ASM_FILE    *body_file;
    const char  *body_start;
    size_t       body_line;
    union {
        struct { /* FOR */
            const char *condition_start;  // ptr into file buf
            const char *adjust_start;
        };
        struct { /* REPEAT */
            int max_iterations;
            const char *var_name;
            int var_name_len;
        };
    };
    size_t iterations;   // runaway guard (max 65536)
} LOOP;

// MACRO struct
typedef struct {
    const char  *name;
    int          name_length;
    ASM_FILE    *body_file;
    const char  *body_start;
    size_t       body_line;
    DYNARRAY     parameters;  // const char* names
} MACRO;
```

Port `MACRO_EXPAND` and `.local` handling verbatim from a2m.

### `asm.h` and `asm.c`

Public API:

```c
int  assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, CB_ASM_CTX *cb);
int  assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address);
void assembler_shutdown(ASSEMBLER *as);
```

`assembler_init`: memset, copy cb, set up all DYNARRAYs, create root scope, init root target with a single default segment. **Does not set the assembly address** — that happens in `assembler_assemble`. No `initial_target_context` parameter (single target).

`assembler_assemble` — two-pass loop:

```c
// Set initial address on the default segment
as->active_target->active_segment->segment_start_address   = address;
as->active_target->active_segment->segment_output_address  = address;

for (as->pass = 1; as->pass <= 2; as->pass++) {
    // Reset segment output addresses to start addresses
    // Reset scope tree (scope_reset_ids)
    // Clear defines table (defines_free)
    // Reset if_stack and if_skip_depth to 0

    if (as->pass == 1) {
        file_load(as, input_file);       // load files on pass 1 only
    } else {
        // Reset file_stack to single frame pointing at root_file buf start
        file_stack_reset_for_pass2(as);
    }

    while (as->file_stack.items > 0) {
        if (!file_read_line(as)) {
            file_stack_pop(as);
            continue;
        }
        // If inside a false .if branch, only look for .if/.else/.endif
        if (as->if_skip_depth > 0) {
            parse_if_skip(as);
            continue;
        }
        strip_comment(as->line, &as->line_len);
        define_substitute(as);
        as->cur = as->line;
        parse_line(as);
    }

    // check for unclosed scopes / if blocks
}
```

`output_byte` is **never called on pass 1**. All `emit_*` functions check `if (as->pass == 2)` before invoking `as->cb.output_byte`. On pass 1, emit functions still advance `segment_output_address` so that address arithmetic is correct for forward-reference resolution.
```

`assembler_shutdown`: free everything. Walk `defines`, `files`, `macros`, `scope` tree, `targets`. Pattern identical to a2m `assembler_shutdown`.

### `asm_lib.h` (new)

The internal include hub, replacing a2m's `asm_lib.h`. Include order matters because of forward declarations:

```c
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>

#include "asm_common.h"
#include "dynarray.h"

#include "err.h"
#include "token.h"
#include "file.h"
#include "define.h"
#include "gperf.h"
#include "opcode.h"
#include "scope.h"
#include "symbol.h"
#include "segment.h"
#include "emit.h"
#include "expr.h"
#include "parse.h"
```

### `CMakeLists.txt`

```cmake
add_library(assembler STATIC
    asm.c
    define.c
    dynarray.c
    errorlog.c
    emit.c
    err.c
    expr.c
    file.c
    gperf.c
    opcode.c
    parse.c
    scope.c
    segment.c
    symbol.c
    token.c
)

target_include_directories(assembler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(assembler PUBLIC c_std_99)
```

No dependency on c64m's `util` library. The assembler is self-contained.

---

## Integration with c64m

The caller (runtime or debugger) provides a `CB_ASM_CTX` with an `output_byte` that writes directly into the C64's memory array. Example:

```c
static void c64_asm_output_byte(void *user, uint16_t addr, uint8_t val) {
    C64 *c64 = (C64 *)user;
    c64->ram[addr] = val;   // adjust for bank mapping if needed
}

void c64_assemble_file(C64 *c64, const char *path, uint16_t org) {
    ERRORLOG log;
    errlog_init(&log);
    CB_ASM_CTX cb = { .user = c64, .output_byte = c64_asm_output_byte };
    ASSEMBLER as;
    assembler_init(&as, &log, &cb);
    assembler_assemble(&as, path, org);
    // display log.log_array errors in UI
    assembler_shutdown(&as);
    errlog_shutdown(&log);
}
```

### Exporting Symbols to c64m Debug Metadata

The assembler's internal symbol system remains private to the assembler. It uses
a2m-derived `SCOPE`, `SYMBOL_LABEL`, and `DYNARRAY` machinery for pass-time
assembly behavior. Do not expose those structures to the emulator, runtime, or
frontend symbol system.

c64m's debug/disassembly symbol table lives in `src/tools/symbols/` and is the
human-facing metadata store used by the disassembler through `symbol_resolver`.
Future assembler integration should bridge into that table by exporting resolved
address labels only.

Preferred assembler-side helper:

```c
typedef void (*assembler_symbol_cb)(
    const char *name,
    uint16_t address,
    void *user);

void assembler_walk_symbols(
    ASSEMBLER *as,
    assembler_symbol_cb cb,
    void *user);
```

The helper should:

- walk the resolved scope tree after a successful `assembler_assemble`
- report only `SYMBOL_ADDRESS` labels
- skip variables, unknowns, macro temporaries unless they are real address labels
- pass stable names to the callback for the duration of the call
- keep the assembler independent of `src/tools/symbols`

The caller/debug session owns the emulator symbol table and performs the import:

```c
typedef struct {
    symbol_table *symbols;
    const char *source_name;
} ASM_SYMBOL_IMPORT;

static void add_asm_symbol(const char *name, uint16_t address, void *user) {
    ASM_SYMBOL_IMPORT *import = (ASM_SYMBOL_IMPORT *)user;
    symbol_table_add(
        import->symbols,
        address,
        name,
        SYMBOL_SOURCE_ASSEMBLER,
        import->source_name,
        true);
}

ASM_SYMBOL_IMPORT import = { symbols, source_name };
symbol_table_remove_source(symbols, SYMBOL_SOURCE_ASSEMBLER, source_name);
assembler_walk_symbols(as, add_asm_symbol, &import);
```

Use `SYMBOL_SOURCE_ASSEMBLER` and an exact `source_name` chosen by the debug
session, such as `"current"` or the assembled source path. Reassembly should
remove that same source before importing the new assembler labels so imported
file symbols, built-ins, and user labels remain intact.

---

## Conditional Assembly (.if/.else/.endif)

`parse_line` checks `as->if_skip_depth > 0` before doing any real work. When skipping, call `parse_if_skip(as)` instead, which only looks for `.if` (increment `if_skip_depth`) and `.else`/`.endif` (decrement, or flip when depth == 1).

When not skipping:
- `.if <expr>`: evaluate expression. Conditional assembly expressions must be resolvable on pass 1; if evaluation produces an unknown/forward reference on pass 1, emit an error and treat the condition as false for recovery. Push `IF_FRAME { was_true = (result != 0), else_seen = 0 }`. If false, set `if_skip_depth = 1`.
- `.else`: peek top of `if_stack`. If `else_seen`, error. Set `else_seen = 1`. If `was_true`, start skipping (`if_skip_depth = 1`); if not `was_true`, stop skipping.
- `.endif`: pop `if_stack`. If stack is now empty and `if_skip_depth > 0`, reset to 0.

Reset `if_stack` and `if_skip_depth` between passes and on any scope/file boundary error.

---

## First Milestone

Phases 1–11 (foundation through conditional assembly), skipping loops, macros, and named scopes. Acceptance criteria:

- Can assemble a file with labels, expressions, `.org`, `.byte`, `.word`, `.res`, `.string`, `.include`
- All core 6502 opcodes work in all addressing modes
- `.if`/`.else`/`.endif` work with numeric expressions
- `.define` substitutes correctly (identifiers and operator sequences)
- Errors name the file and line number
- Assembled bytes land in the correct C64 memory addresses

This is sufficient to write real test programs that exercise the emulator.

---

## 65C02-Only Opcodes to Remove

These are in a2m but not valid on 6502/6510:

| Opcode | Reason |
|--------|--------|
| `BRA`  | 65C02 branch-always |
| `DEA`  | 65C02 dec-accumulator |
| `INA`  | 65C02 inc-accumulator |
| `PHX`  | 65C02 push X |
| `PHY`  | 65C02 push Y |
| `PLX`  | 65C02 pull X |
| `PLY`  | 65C02 pull Y |
| `STZ`  | 65C02 store zero |
| `TRB`  | 65C02 test and reset bits |
| `TSB`  | 65C02 test and set bits |

Keep the `.6502` / `.65c02` dot commands. Default is 6502 (valid_opcodes = 0). If `.65c02` is seen and the above opcodes are not in the table, error gracefully.

---

## Known a2m Issues to Not Carry Over

Read a2m `parse.c` carefully before porting dot-command handlers. Some known problem areas:
- The `*` (current address) evaluates to the address of the current instruction/line — the standard assembler convention (e.g. `jmp *` is a self-loop). NOTE: a2m historically returned `address + 1` here; c64m corrected this to the normal model.
- The two-pass error suppression logic is correct; keep it.
- The `input_stack` push/pop for macros/loops is the part being replaced with the file-ptr model above.
- The `macro_buffers` DYNARRAY (stores heap copies of expanded macro text) is eliminated — not needed in the new model.

---

## Implementation Order

See `STATUS.md` for current state. Recommended order:

1. **Foundation**: `dynarray`, `err`, `asm_lib.h` skeleton, `CMakeLists.txt`
2. **File + token**: `file.h/c`, `token.h/c` — get `file_read_line` + `get_token` working
3. **Define**: `define.h/c` — text substitution
4. **Symbols + Scopes**: `scope.h/c`, `symbol.h/c`
5. **Expression**: `expr.h/c`
6. **Opcodes + gperf**: `gperf.h/c`, `opcode.h/c`
7. **Segment + Emit**: `segment.h/c`, `emit.h/c`
8. **Assembler skeleton**: `asm.h/c` — init/shutdown/two-pass loop, `parse_line` dispatch
9. **Core parser**: labels, variables, opcodes, `parse_address`
10. **Dot commands — data**: `.org`, `.byte`, `.word`, `.dword`, `.qword`, `.res`, `.align`, `.string`, `.strcode`, `.incbin`, `.include`
11. **Dot commands — conditional**: `.if`, `.else`, `.endif`
12. **Dot commands — loops**: `.for`/`.endfor`, `.repeat`/`.endrepeat`
13. **Macros**: `.macro`/`.endmacro`, `.local`, macro invocation
14. **Scopes + Segments**: `.scope`, `.proc`, `.endscope`, `.endproc`, `.segdef`, `.segment`
15. **Integration**: wire `c64_assemble_file`, symbol walk for debugger
