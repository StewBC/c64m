# Paste Input — Implementation Brief

## Context

You have already audited the codebase and reported the disconnects. This document tells you exactly what to build. Read it fully before writing any code.

The syntax spec (`c64_input_encoding_spec.md`) defines the user-visible string format. This document defines the implementation.

---

## Scope Boundary

**`use_buffer` path (OPT+INS) is untouched.** No changes to its command, payload, or logic.

All work below applies exclusively to the **matrix path (OPT+SHIFT+INS)**.

---

## New Files

### `util/paste_parser.h` and `util/paste_parser.c`

No runtime dependencies. No C64 machine state. Pure string → event array transformation.

#### Event type

```c
typedef enum {
    PASTE_EV_KEY_PRESS,    // press-and-release; key field valid
    PASTE_EV_KEY_ASSERT,   // hold down (\[KEY+]); key field valid
    PASTE_EV_KEY_DEASSERT, // release (\[KEY-]); key field valid
    PASTE_EV_PETSCII,      // raw value from \x \d \o; petscii field valid
    PASTE_EV_MATRIX,       // direct matrix address; row, col fields valid
    PASTE_EV_JOYSTICK,     // joystick event; port, dir, button, has_button valid
    PASTE_EV_NMI,          // RESTORE key — not in matrix, handled separately
} paste_event_type_t;

typedef struct {
    paste_event_type_t type;
    union {
        struct { c64_key_t key; }                                        key;
        struct { uint8_t petscii; }                                      petscii;
        struct { uint8_t row; uint8_t col; }                             matrix;
        struct { uint8_t port; uint8_t dir; uint8_t button;
                 uint8_t has_button; }                                   joy;
    };
} paste_event_t;
```

#### Parser API

```c
typedef struct {
    int         offset;       // byte offset of offending character; -1 if no error
    const char *message;      // static string; do not free
} paste_parse_error_t;

// Returns heap-allocated array of `*count` events, or NULL on error.
// Caller must free() the returned pointer.
// On error, *count is 0 and *err is populated.
paste_event_t *paste_parse(const char *input,
                            size_t     *count,
                            paste_parse_error_t *err);
```

#### Parser behaviour

Process the input string left to right. For each character:

- If `\` → read the next character to determine token type:
  - `[` → named key sequence (see below)
  - `x` → read exactly 2 hex digits; emit `PASTE_EV_PETSCII`
  - `d` → read exactly 3 decimal digits (000–255); emit `PASTE_EV_PETSCII`
  - `o` → read exactly 3 octal digits (000–377, value ≤ 255); emit `PASTE_EV_PETSCII`
  - `m` → read digit `,` digit (both 0–7); emit `PASTE_EV_MATRIX`
  - `j` → read port (1–2), direction (0–8), optional `,` button (0–1); emit `PASTE_EV_JOYSTICK`
  - anything else → parse error
- If any printable ASCII 0x20–0x7E (not `\`) → emit `PASTE_EV_KEY_PRESS` with key resolved from the literal character table (see below)
- If byte < 0x20 or == 0x7F → parse error

Named key sequence `\[keyname modifier? ]`:
- Collect characters until `]`; if end-of-string reached first → parse error
- Strip trailing `+` or `-` before the `]` if present; that is the modifier
- Look up the remaining string (case-insensitive) in the key table (canonical name or short alias, exact match only — no prefix matching)
- No match → parse error
- Match is `RESTORE` → emit `PASTE_EV_NMI` (modifier ignored; RESTORE has no hold/release model at the NMI line level — emit it as a momentary pulse)
- Otherwise:
  - No modifier → emit `PASTE_EV_KEY_PRESS`
  - `+` → emit `PASTE_EV_KEY_ASSERT`
  - `-` → emit `PASTE_EV_KEY_DEASSERT`

#### Key name table

Implement as a static lookup table of `{ canonical, alias, c64_key_t }` entries. Case-insensitive comparison on both canonical and alias. Exact match only.

| Canonical | Alias | Key enum value |
|---|---|---|
| RETURN | RT | (existing) |
| RESTORE | RS | — (emits NMI event, no key enum) |
| RUNSTOP | RN | (existing) |
| CLRHOME | CH | (existing) |
| INSDEL | ID | (existing) |
| SHIFT | SH | (existing) |
| CBM | CB | (existing) |
| CTRL | CT | (existing) |
| CUU | — | cursor up (see enum note below) |
| CUD | — | cursor down (existing) |
| CUL | — | cursor left (see enum note below) |
| CUR | — | cursor right (existing) |
| F1 | — | (existing) |
| F2 | — | (see enum note below) |
| F3 | — | (existing) |
| F4 | — | (see enum note below) |
| F5 | — | (existing) |
| F6 | — | (see enum note below) |
| F7 | — | (existing) |
| F8 | — | (see enum note below) |
| POUND | PO | (existing) |
| LEFTARROW | LA | (existing — the ← key, PETSCII 95) |
| UPARROW | UA | (existing — the ↑ key) |
| PI | — | (see enum note below) |
| PLUS | — | (existing) |
| MINUS | — | (existing) |
| AT | — | (existing) |
| ASTERISK | AS | (existing) |
| SPACE | SP | (existing) |

#### Enum gaps to resolve

You found these missing from `c64_key_t`: F2, F4, F6, F8, cursor-up, cursor-left, PI. 

These are all SHIFT+something on the physical matrix:
- F2 = SHIFT+F1, F4 = SHIFT+F3, F6 = SHIFT+F5, F8 = SHIFT+F7
- Cursor-up = SHIFT+cursor-down, Cursor-left = SHIFT+cursor-right
- PI = SHIFT+UP_ARROW

**Do not add them to the `c64_key_t` enum.** Instead, in the key table, represent them as a `{ base_key, needs_shift }` pair. When the sequencer sees a `PASTE_EV_KEY_PRESS` for one of these, it asserts SHIFT + the base key, holds, then releases both. This keeps the enum matching the physical matrix exactly.

Update the key table struct accordingly:

```c
typedef struct {
    const char  *canonical;
    const char  *alias;        // NULL if none
    c64_key_t    key;
    bool         needs_shift;  // true for F2/F4/F6/F8, cursor-up/left, PI
    bool         is_nmi;       // true for RESTORE only
} paste_key_entry_t;
```

#### Literal character resolution

For a literal ASCII character (non-`\`), resolve to `c64_key_t` using the existing `paste_ascii_map`. This is already correct for the matrix path — reuse it as-is. If a character has no valid entry in `paste_ascii_map`, emit a parse error (or a warning and skip — your discretion, but document the choice).

---

## Modified Files

### `c64_key_t` enum — no changes needed

See above. The `needs_shift` field in the key table handles the gap cases without touching the enum.

### Runtime command payload

Locate the `RUNTIME_COMMAND_PASTE_TEXT` struct. Add a new variant (or replace the matrix-path payload) with:

```c
typedef struct {
    paste_event_t *events;   // heap-allocated by frontend; freed by runtime after consumption
    size_t         count;
} runtime_paste_events_t;
```

The `use_buffer` path keeps its existing `char text[4096]` payload unchanged. Use whatever dispatch mechanism already exists (the `use_buffer` flag, a separate command enum value, etc.) to route them.

### `paste_state_t` — extend in place

Add to the existing struct:

```c
paste_event_t  *events;               // NULL when not in matrix-event mode
size_t          event_count;
size_t          event_cursor;
bool            asserted_keys[C64_KEY_MAX];  // replaces shift_needed for event mode
```

Keep all existing fields. The existing character-driven path and the new event-driven path coexist in the same state machine; which path is active is determined by whether `events` is non-NULL.

### Sequencer — per-event processing

In the paste tick function, when `events` is non-NULL, consume `events[event_cursor]` instead of the next ASCII character. Per event type:

- `PASTE_EV_KEY_PRESS` — if `needs_shift`, assert SHIFT first. Assert the key. Enter HOLD phase. On HOLD expiry: deassert the key (and SHIFT if asserted). Enter GAP phase. On GAP expiry: advance `event_cursor`.
- `PASTE_EV_KEY_ASSERT` — call `c64_set_key(key, true)`. Set `asserted_keys[key] = true`. Advance `event_cursor` immediately (no hold/gap).
- `PASTE_EV_KEY_DEASSERT` — call `c64_set_key(key, false)`. Clear `asserted_keys[key]`. Advance `event_cursor` immediately.
- `PASTE_EV_PETSCII` — resolve to `(c64_key_t, shift_needed)` via `paste_ascii_map`. Treat as `KEY_PRESS`.
- `PASTE_EV_MATRIX` — assert the matrix cell directly (use whatever low-level setter exists below `c64_set_key`; if none, this is a small addition). Hold. Deassert. Gap. Advance.
- `PASTE_EV_JOYSTICK` — call the joystick port state setter. Advance immediately (no hold/gap).
- `PASTE_EV_NMI` — pulse the NMI line (RESTORE). Use whatever mechanism the emulator uses for NMI; it is not a matrix operation. Advance immediately.

When `event_cursor == event_count`, free `events`, set `events = NULL`, mark paste complete.

### Frontend — OPT+SHIFT+INS handler

Replace the existing string-shipping code with:

```c
paste_parse_error_t err;
size_t count;
paste_event_t *events = paste_parse(text_buffer, &count, &err);
if (!events) {
    // display err.message and err.offset to the user somehow
    return;
}
// ship events + count to runtime via the updated command
```

---

## What Does Not Change

- `use_buffer` path — untouched end to end
- Timing constants (hold ~40ms, gap ~10ms, post-RETURN ~250ms)
- `paste_ascii_map` — reused by parser for literal characters and PETSCII events
- Breakpoint INI infrastructure — parser is in `util/` so it's reachable from there already

---

## Clarification Needed From Stefan Before Starting

**One open question:** does `PASTE_EV_MATRIX` need a direct sub-matrix setter below `c64_set_key`, or is `c64_set_key` already thin enough to map a `(row, col)` pair directly? If `c64_set_key` only accepts `c64_key_t` enum values, a small bypass function will be needed. Check and proceed accordingly — no need to ask, just note the decision in a comment.
