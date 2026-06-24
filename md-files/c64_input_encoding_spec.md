# C64 Input Encoding Specification
## For Parser Implementors

**Version 1.0**  
**Scope:** Defines the syntax of input strings typed by a user on the emulator host OS (ASCII/UTF-8). The parser consumes these strings and produces a sequence of key events for delivery to the C64 keyboard matrix emulation layer. The format of that output blob is outside the scope of this document.

---

## 1. Overview

An input string is a sequence of one or more *tokens*. The parser processes tokens left to right. Each token produces one or more key events. There are four token types:

| Token type | Introduced by | Example |
|---|---|---|
| Literal character | Any printable ASCII character that is not `\` | `A`, `3`, `!` |
| Named key sequence | `\[` | `\[RETURN]`, `\[F1]`, `\[SH+]` |
| Numeric escape | `\x`, `\d`, `\o` | `\x41`, `\d065`, `\o101` |
| Matrix address | `\m` | `\m3,5` |
| Joystick event | `\j` | `\j12,1` |

There is no other syntax. Any `\` not followed by a recognised introducer is a parse error.

---

## 2. Literal Characters

Any printable ASCII character in the range 0x20–0x7E that is **not** `\` is a literal character token. The parser passes it directly to the output layer, which is responsible for mapping it to the appropriate C64 key or PETSCII code.

**Note on case:** The C64 keyboard does not produce ASCII uppercase/lowercase in the same way a PC keyboard does. Lowercase ASCII letters typed in the input string correspond to unshifted keys on the C64 (which display as uppercase in BASIC mode). Uppercase ASCII letters correspond to shifted keys. The output layer handles this mapping; the parser makes no case transformation.

**Examples:**

| Input | Meaning to parser |
|---|---|
| `a` | Literal 'a' (unshifted A key) |
| `A` | Literal 'A' (shifted A key) |
| `1` | Literal '1' |
| `?` | Literal '?' |

---

## 3. Named Key Sequences

**Syntax:** `\[` *keyname* *modifier*? `]`

- *keyname* is case-insensitive.
- *modifier* is an optional single character immediately following the keyname, before the `]`. It is either `+` (assert / press and hold) or `-` (deassert / release).
- A bare `\[KEY]` with no modifier means **press and release** (a complete keystroke). This is the default and the common case.
- `\[KEY+]` means assert the key (close the matrix switch) without releasing it.
- `\[KEY-]` means deassert the key (open the matrix switch).

**Use of `+` and `-` for chording:**

To simulate simultaneous key presses, assert the first key, assert the second, then deassert both:

```
\[SH+]\[F1]\[SH-]
```
This produces F2 (SHIFT+F1 asserted, F1 pressed and released, SHIFT released).

```
\[RN+]\[RS]\[RN-]
```
This holds RUN/STOP while pressing RESTORE (the NMI sequence).

---

## 3.1 Key Name Table

Each key has a **canonical name** and one **short alias**. The parser accepts either form exactly — no other abbreviations or prefix matching. Key names are case-insensitive.

| Key | Canonical name | Short alias | Notes |
|---|---|---|---|
| RETURN / ENTER | `RETURN` | `RT` | |
| RESTORE | `RESTORE` | `RS` | Not in matrix; drives NMI line directly |
| RUN/STOP | `RUNSTOP` | `RN` | |
| CLR/HOME | `CLRHOME` | `CH` | |
| INS/DEL | `INSDEL` | `ID` | |
| SHIFT (left or right) | `SHIFT` | `SH` | Both shift keys are treated identically unless the output layer distinguishes them |
| COMMODORE | `CBM` | `CB` | |
| CONTROL | `CTRL` | `CT` | |
| CURSOR UP | `CUU` | — | No short alias; 3-char canonical is already short |
| CURSOR DOWN | `CUD` | — | |
| CURSOR LEFT | `CUL` | — | |
| CURSOR RIGHT | `CUR` | — | |
| F1 | `F1` | — | |
| F2 | `F2` | — | |
| F3 | `F3` | — | |
| F4 | `F4` | — | |
| F5 | `F5` | — | |
| F6 | `F6` | — | |
| F7 | `F7` | — | |
| F8 | `F8` | — | |
| POUND (£) | `POUND` | `PO` | The £ key; unshifted |
| LEFT ARROW (←) | `LEFTARROW` | `LA` | Top-left key, PETSCII 95; distinct from cursor left |
| UP ARROW (↑) | `UPARROW` | `UA` | Top-row key; shifted form is π |
| PI (π) | `PI` | — | Shifted UP ARROW; provided as a convenience alias |
| PLUS (+) | `PLUS` | — | |
| MINUS (-) | `MINUS` | — | |
| AT (@) | `AT` | — | |
| ASTERISK (*) | `ASTERISK` | `AS` | |
| SPACE | `SPACE` | `SP` | Useful when a literal space would be ambiguous in context |

**Note on SHIFT variants:** Keys that have graphic characters on their shifted face (POUND, PLUS, MINUS, AT, ASTERISK, UPARROW, LEFTARROW) are accessed by combining SHIFT or CBM with the base key:

```
\[SH+]\[POUND]\[SH-]     → shifted POUND graphic
\[CB+]\[AT]\[CB-]         → CBM+@ graphic character
```

The named key refers to the physical key, not the character produced. The output layer resolves the final PETSCII value from the key + modifier state combination.

---

## 4. Numeric Escapes

These inject a raw PETSCII value directly by number. They do **not** refer to a named key; the output layer receives the numeric value and is responsible for what to do with it.

| Form | Base | Digits | Example | Value |
|---|---|---|---|---|
| `\x` *HH* | Hexadecimal | Exactly 2 hex digits (0–9, a–f, A–F) | `\x41` | 65 decimal |
| `\d` *DDD* | Decimal | Exactly 3 decimal digits (000–255) | `\d065` | 65 decimal |
| `\o` *OOO* | Octal | Exactly 3 octal digits (000–377) | `\o101` | 65 decimal |

All three examples above encode the same value (65 / 0x41 / PETSCII 'A').

**Rules:**
- The digit count is fixed. `\x4` is a parse error (only one hex digit). `\x041` is a parse error (three hex digits). `\x41` is correct.
- Values above 255 (decimal) are a parse error.
- There is no modifier syntax for numeric escapes. They always represent a single value delivered to the output layer.

---

## 5. Matrix Address

**Syntax:** `\m` *R* `,` *C*

Directly specifies a matrix row and column, bypassing key name lookup entirely. Intended for power users and testing.

- *R* and *C* are single decimal digits (0–7), separated by a comma.
- No spaces are permitted within the token.
- No modifier syntax. The output layer receives the (row, column) pair and performs a press-and-release.

**Example:** `\m3,5`

**Note:** The RESTORE key is not addressable via `\m` because it is not part of the keyboard matrix (it connects directly to the NMI line). Use `\[RESTORE]` for that key.

---

## 6. Joystick Events

**Syntax:** `\j` *P* *D* [ `,` *B* ]

| Field | Meaning | Values |
|---|---|---|
| *P* | Port number | `1` or `2` |
| *D* | Direction | `0` = centred (no direction); `1`–`8` clockwise from up |
| *B* | Button (optional) | `1` = button pressed, `0` = button released; omit for no button event |

Direction encoding (clockwise from up):

```
    1
  8   2
7       3
  6   4
    5
```

| Value | Direction |
|---|---|
| 0 | Centred |
| 1 | Up |
| 2 | Up-right |
| 3 | Right |
| 4 | Down-right |
| 5 | Down |
| 6 | Down-left |
| 7 | Left |
| 8 | Up-left |

**Examples:**

| Token | Meaning |
|---|---|
| `\j11` | Port 1, direction Up, no button |
| `\j23,1` | Port 2, direction Right, button pressed |
| `\j10,1` | Port 1, centred, button pressed |
| `\j20` | Port 2, centred, no button |

**Note:** The comma and button field are both optional as a unit — either both are present or neither is. `\j11,` (trailing comma with no button digit) is a parse error.

---

## 7. Parse Error Handling

The parser must report an error for any of the following:

- `\` not followed by `[`, `x`, `d`, `o`, `m`, or `j`
- `\[` with an unrecognised key name (not in canonical or alias table)
- `\[` with a modifier character other than `+` or `-`
- `\[` not closed by `]`
- `\x` not followed by exactly 2 hex digits
- `\d` not followed by exactly 3 decimal digits
- `\o` not followed by exactly 3 octal digits
- Any numeric escape value > 255
- `\m` not followed by digit `,` digit
- `\m` with a row or column value outside 0–7
- `\j` with a port value other than `1` or `2`
- `\j` with a direction value outside `0`–`8`
- `\j` with a trailing comma but no button digit
- `\j` with a button value other than `0` or `1`
- Any non-printable ASCII character in the input (bytes below 0x20 or 0x7F)

On a parse error the parser should report the byte offset of the offending character and a human-readable error message. Whether parsing aborts at the first error or attempts to continue is left to the implementor.

---

## 8. Complete Syntax Summary (BNF)

```
input       ::= token*
token       ::= literal | named-key | numeric-esc | matrix | joystick
literal     ::= [0x20-0x7E] except '\'
named-key   ::= '\[' keyname modifier? ']'
keyname     ::= canonical-name | short-alias        (case-insensitive)
modifier    ::= '+' | '-'
numeric-esc ::= hex-esc | dec-esc | oct-esc
hex-esc     ::= '\x' [0-9a-fA-F] [0-9a-fA-F]
dec-esc     ::= '\d' [0-9] [0-9] [0-9]
oct-esc     ::= '\o' [0-7] [0-7] [0-7]
matrix      ::= '\m' [0-7] ',' [0-7]
joystick    ::= '\j' port direction ( ',' button )?
port        ::= '1' | '2'
direction   ::= '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8'
button      ::= '0' | '1'
```

---

## 9. Examples

```
abcd1234
```
Eight literal characters, passed through as-is.

```
10 fOr i=1tO100:?i:nExti
```
Literal characters. Lowercase letters produce unshifted C64 keys (displayed as uppercase in BASIC). Uppercase produce shifted keys.

```
\[SH+]\[F1]\[SH-]
```
Assert SHIFT, press-and-release F1, release SHIFT. Produces F2 behaviour.

```
\[RN+]\[RS]\[RN-]
```
Hold RUN/STOP, press RESTORE (triggers NMI), release RUN/STOP.

```
\[CB+]\[AT]\[CB-]
```
Hold CBM, press @, release CBM. Produces the CBM+@ graphic character.

```
\x0d
```
Raw PETSCII value 13 (CR) injected directly.

```
LOAD\[SPACE]\x22*\x22,8,1\[RT]
```
Types `LOAD "` (note: `\x22` is the double-quote character, which is also just `"` as a literal — `\x22` is shown here for illustration), `,8,1` then presses RETURN.

```
\j12,1
```
Port 1, direction Up-right, button pressed.

```
\m7,4
```
Matrix row 7, column 4, press and release.

---

## 10. Implementation Notes

These are suggestions, not requirements. The implementor owns the blob format and output layer.

- The parser's job is purely lexical and syntactic. It does not resolve PETSCII values, does not know the current SHIFT lock state, and does not interpret what character a key combination produces. All of that is the output layer's responsibility.
- Key names in the table are the complete set. The parser should not attempt partial matching or fuzzy matching — exact match against the canonical name or the short alias only.
- The `+` / `-` modifier on named keys is intended to model the physical state of the key switch. It is the caller's responsibility to ensure that every `\[KEY+]` is eventually paired with a corresponding `\[KEY-]`. The parser does not validate pairing.
- RESTORE (`\[RS]` / `\[RESTORE]`) is architecturally different from all other keys on the C64: it is not part of the 8×8 matrix and cannot be polled by the CIA. The output layer must handle it specially (NMI line, not matrix row/column). The parser treats it as a named key like any other.
