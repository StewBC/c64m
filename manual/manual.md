# c64m - A Commodore 64 emulator written by Codex and Claude Code, produced by Stefan Wessels, 2026

c64m is a Commodore 64 emulator written in C99. It supports both PAL (6569) and NTSC
video standards and can run BASIC programs, PRG files, and D64 disk images. The built-in
debugger and assembler make it a practical environment for C64 development and
exploration.

c64m requires C64 ROM files to run. Place `basic`, `kernal`, `character`, and optionally
`system` ROM files in the same directory as the executable, or in a subdirectory named
`rom` or `roms`. Files are matched by stem name and size; extensions are ignored.

## Overview

### Running c64m

Launch c64m from the command line or as a GUI application. Use `--help` (or `-h`) to
see the full command-line reference.

Useful flags:

| Flag                   | Effect                                              |
|------------------------|-----------------------------------------------------|
| `--inifile <file>`     | Load a specific INI file at startup                 |
| `--noini` / `-n`       | Skip INI file loading entirely                      |
| `--nosaveini`          | Disable INI save on quit, regardless of other flags |
| `--saveini` / `-v`     | Save INI on quit (one-time override)                |
| `--remember` / `-r`    | Force save-on-quit into the INI file                |
| `--turbo <list>` / `-t`| Set turbo multipliers, e.g. `2,4,8,16`             |
| `--disk <drive>=<image>` | Mount a D64 image at startup, e.g. `--disk 8=game.d64` |
| `--prg <file>` / `-p`  | Load a file as PRG at startup                       |
| `--basic <file>` / `-B`| Load a file as BASIC program at startup             |
| `--leds on\|off`       | Show or hide disk activity LEDs                     |
| `--audio-smoke`        | Emit a 440 Hz test tone to verify audio output      |

By default, c64m loads `c64m.ini` from the current directory. The INI file records
configuration, window size, debugger layout, and breakpoints.

### Video Standards

PAL and NTSC are selectable in the Configure dialog or via the `[Video]` INI section.
PAL runs at the 6569 timing (approximately 50 Hz frames), NTSC at the 6567 timing
(approximately 60 Hz frames). Changing the video standard reboots the emulated machine.

### Disk Images

c64m supports read-only D64 images on device 8 and device 9. D64 write operations, fast
loaders, and full 1541 emulation are not implemented. `LOAD "NAME",8` and
`LOAD "NAME",8,1` work through a KERNAL trap. Wildcards `*` and `?` are supported, and
`LOAD "$",8` returns a directory listing.

### PRG and BASIC Files

`--prg <file>` (or `-p`) loads any file as a PRG at startup. The machine resets, boots
through KERNAL and BASIC, then injects the file bytes at the load address embedded in
the file's first two bytes. Execution continues automatically — no key press needed.

`--basic <file>` (or `-B`) loads any file as a BASIC program at startup. The machine
resets, boots to BASIC, writes the file to RAM at the address in its two-byte header,
and updates the BASIC start and end pointers (`$2B–$2E`).

For both options the file extension is irrelevant; the flag determines how the file is
treated, not the filename.

### Drag and Drop

Files can be dragged onto the c64m window at any time while the emulator is running.
The file extension determines how the file is handled:

| Extension | Action                                                         |
|-----------|----------------------------------------------------------------|
| `.d64`    | Mount the image on device 8 (replaces any previously mounted disk) |
| `.bas`    | Load as a BASIC program (reset, boot to BASIC, inject, update `$2B–$2E`) |
| anything else | Load as a PRG (reset, boot to BASIC, inject at embedded load address, auto-run) |

Extension matching is case-insensitive (`.D64` and `.d64` are treated identically).
Unlike the `--prg` and `--basic` startup flags, the extension drives the decision when
dropping — there is no way to override it by name alone.

### Audio

SID audio uses a MOS 6581 register model at `$D400-$D41F` with three voices, ADSR
envelopes, a Chamberlin state-variable filter, and voice 3 read-back. Audio output uses
a 48 kHz stereo path through SDL. Turbo mode mutes audio to prevent buffer overflow; SID
state continues to advance normally.

## Interface

### Display Mode

When launched, c64m shows the C64 display filling the entire window. All regular keys
are forwarded to the emulated C64. The window can be resized; the display is always
scaled to fit the available area with aspect-ratio correction.

Press **F9** to open or close Debug Mode. Press **Opt+H** to open or close the in-emulator
help. **Cmd+Q** (macOS) quits.

### Debug Mode

In Debug Mode the window is divided into four main areas:

| Area          | Contents                                                    |
|---------------|-------------------------------------------------------------|
| Upper left    | C64 display (scaled to fit its region)                      |
| Upper right   | CPU register view                                           |
| Right, below  | Disassembly view                                            |
| Lower left    | Memory view                                                 |
| Lower right   | Misc panel (Machine, Debugger, Breakpoints, Hardware, Assembler tabs) |

c64m tracks an active view for keyboard input. When no modal dialog is open, the active
C64 display, Disassembly, Misc, or Memory view is outlined with a neutral gray rectangle.
Click a view to make it active, or press **Opt+Tab** to cycle
C64->Disassembly->Misc->Memory. Press **Shift+Opt+Tab** to cycle in reverse. Modal
dialogs keep input to themselves, so these view-cycling keys do not work while a dialog
is open.

### Layout

Two splitters divide the debug layout:

- A **vertical splitter** between the C64 display region and the CPU/Disassembly pane.
- A **horizontal splitter** between the upper and lower halves.

Drag the splitters to resize the panes. Window size and splitter positions are saved to
the INI file on quit.

### Turbo Mode

**Opt+T** cycles through the configured turbo multiplier list (default `2x, 4x, 8x, 16x`).
Turbo speeds are listed in the Configure dialog and stored in the INI file.

## CPU View

The CPU view shows the current state of the 6510 CPU as reported by the most recent
runtime snapshot:

| Field | Width | Description                      |
|-------|-------|----------------------------------|
| PC    | 16-bit | Program counter                 |
| SP    | 8-bit  | Stack pointer (page 1 offset)   |
| A     | 8-bit  | Accumulator                     |
| X     | 8-bit  | X index register                |
| Y     | 8-bit  | Y index register                |
| N V - B D I Z C | 1-bit each | Processor status flags |

When the CPU is paused, all fields are editable:

- PC: four hex digits.
- SP, A, X, Y: two hex digits.
- Flags: `0` (clear) or `1` (set).

The `-` position in the flag row represents the unused bit; it is always 1 and cannot
be modified.

## Disasm

The Disassembly view shows the code at and around the program counter. While running,
the current instruction (the PC line) scrolls into view. While paused, the cursor is
independent of the PC.

### Line Format

Each line follows this general format:

```
C123: LABEL        A9 00       LDA #$00
```

| Column  | Meaning                                              |
|---------|------------------------------------------------------|
| `C123`  | Hex address                                          |
| `LABEL` | Symbol name at that address (when available)         |
| `A9 00` | Raw bytes                                            |
| `LDA #$00` | Disassembled instruction with resolved symbols    |

Breakpoint addresses show an indicator in the left gutter.

### Display Modes

The disassembly view has three source modes that control which bytes are read for
disassembly:

| Mode    | Mode border | Bytes shown                                              |
|---------|-------------|----------------------------------------------------------|
| **Map** | none        | CPU-visible address space (current bank configuration)   |
| **ROM** | amber       | Physical ROM bytes at ROM addresses, regardless of mapping; RAM elsewhere |
| **RAM** | blue        | Raw RAM at every address, regardless of any ROM overlay  |

ROM and RAM draw a colored source-mode border inside the content area. Map has no
source-mode color; if the view is active, the separate neutral active-view border is
still shown.

Switch modes with **right-click** anywhere in the view (a popup lists all three with a
dot next to the active choice), or with **Opt+M** from the keyboard.

**Tab** and **Shift+Tab** cycle the symbol display through `auto`, `names`, and `raw`
modes independently of the source mode.

### Keyboard Controls

| Key             | Action                                                     |
|-----------------|------------------------------------------------------------|
| `Opt+A`         | Enter address-jump mode; type four hex digits then Enter   |
| `Opt+B`         | Toggle execute breakpoint at cursor (paused only)          |
| `Opt+M`         | Cycle source mode: Map -> ROM -> RAM -> Map                   |
| `Opt+S`         | Reload and enumerate the symbol table                      |
| `Opt+Left`      | Set PC to cursor address (paused only)                     |
| `Tab`           | Cycle symbol display mode forward                          |
| `Shift+Tab`     | Cycle symbol display mode backward                         |
| `Up` / `Down`   | Move cursor one instruction                                |
| `PgUp` / `PgDn` | Scroll one page                                            |
| `Home` / `End`  | Jump to first or last line of the current view             |
| `Opt+Home`      | Jump to address `$0000`                                    |
| `Opt+End`       | Jump to address `$FFFF`                                    |

## Mem View

The Memory view shows the full 64 K address space as 16-byte rows in hex and ASCII.

### Line Format

```
C123: 48 65 6C 6C 6F 20 57 6F 72 6C 64 21 00 00 00 00  Hello World!....
```

| Column       | Meaning                                    |
|--------------|--------------------------------------------|
| `C123`       | Hex address of the first byte in the row   |
| `48 65 ...`   | Byte values in hex                         |
| `Hello ...`    | ASCII representation (`.` for non-print)   |

### Display Modes

The memory view has three source modes that control which bytes are displayed:

| Mode    | Mode border | Bytes shown                                              |
|---------|-------------|----------------------------------------------------------|
| **Map** | none        | CPU-visible address space (current bank configuration)   |
| **ROM** | amber       | Physical ROM bytes at ROM addresses, regardless of mapping; RAM elsewhere |
| **RAM** | blue        | Raw RAM at every address, regardless of any ROM overlay  |

ROM and RAM draw a colored source-mode border inside the content area. Map has no
source-mode color; if the view is active, the separate neutral active-view border is
still shown.

Switch modes with **right-click** anywhere in the view (a popup lists all three with a
dot next to the active choice), or with **Opt+M** from the keyboard.

The memory and disassembly view modes are independent of each other -- for example, you
can watch raw RAM in the memory view while the disassembler follows the CPU map
simultaneously.

### Status Row

The bottom of the Memory view shows the active edit field (`Hex`, `ASCII`, or
`Address`), the current cursor address as `Address: XXXX`, and whether memory editing
is currently `editable` or `read-only`.

### Keyboard Controls

| Key             | Action                                                     |
|-----------------|------------------------------------------------------------|
| `Opt+A`         | Toggle address-entry mode; type four hex digits to jump    |
| `Opt+M`         | Cycle source mode: Map -> ROM -> RAM -> Map                   |
| `Opt+X`         | Toggle between hex and ASCII edit modes                    |
| `Up` / `Down`   | Move cursor one row (16 bytes)                             |
| `Left` / `Right`| Move cursor one byte (or nibble in hex mode)               |
| `PgUp` / `PgDn` | Scroll one page                                            |
| `Home`          | Move cursor to start of the current row                    |
| `Opt+Home`      | Move cursor to the start of the visible window             |
| `End`           | Move cursor to end of the current row                      |
| `Opt+End`       | Move cursor to the end of the visible window               |
| `0-9`, `A-F`   | Edit hex nibble at cursor (paused only, hex mode)          |

The scrollbar on the right spans the full 64 K space and can be dragged to any position.

Memory editing is only possible while the CPU is paused. In hex mode, typing hex digits
overwrites the nibble at the cursor. In ASCII mode, printable characters overwrite the
byte at the cursor.

## Machine

The Machine tab is the first tab in the Misc panel and groups controls for disks,
programs, and emulator management.

### Disks

Devices 8 and 9 each have a mount button (labeled **[8]** and **[9]**) and an **Eject**
button. Clicking the mount button opens a file browser; select a D64 image to mount it.
The disk title or host filename is shown next to the device number after a successful
mount. Ejecting a device clears it; the other device is unaffected.

### Programs

**[Load]** opens the Load dialog:

| Field           | Meaning                                                    |
|-----------------|------------------------------------------------------------|
| Name + Browse   | Select the host PRG or binary file to load                 |
| From File       | Read the two-byte load address header from the file (default on) |
| Address field   | Manual load address in hex, active when From File is off   |
| Reset           | Reset the machine and wait for BASIC (`$E38B`) before injecting |
| Basic Program   | Update TXTTAB (`$2B/$2C`) and VARTAB (`$2D/$2E`) after load |

**[Save]** opens the Save dialog:

| Field                | Meaning                                                 |
|----------------------|---------------------------------------------------------|
| Name + Browse        | Choose the output filename                              |
| Basic Program        | Read start and end from `$2B-$2E`; forces header on    |
| Write address header | Prefix the saved file with the two-byte load address   |
| Start / End          | Hex address range for a raw memory save                |

### Emulator Controls

**[Configure...]** opens the Configure dialog (see **Configure**).

**[Reset]** performs a hard reset of the emulated C64. Any pending PRG injection or
assembler-queued run is cancelled.

## Debugger

The Debugger tab shows runtime counters and the call stack.

### Counters

| Counter         | Meaning                                              |
|-----------------|------------------------------------------------------|
| CPU cycles      | Total 6510 cycles executed since last reset          |
| Machine cycles  | Master cycle counter                                 |
| VIC cycles      | VIC-II cycles advanced                               |
| CIA cycles      | CIA cycles advanced                                  |

### Call Stack

The call stack shows the chain of active JSR calls reconstructed from the 6510 hardware
stack each frame. Entries have the form:

```
E69E | JSR FF59 OLDRST
```

| Part    | Meaning                                            |
|---------|----------------------------------------------------|
| `E69E`  | Address of the JSR instruction                     |
| `FF59`  | Destination address (subroutine entry)             |
| `OLDRST`| Symbol name for `$FF59`, when available            |

Clicking either address in an entry moves the Disassembly view cursor to that address.
Up to 16 entries are displayed; the list has its own scrollbar when there are more.

## Breakpoints

The Breakpoints tab lists all configured breakpoints. When no breakpoints exist, the tab
is empty. A **[Clear All]** button appears at the top when more than one breakpoint is
present.

### Breakpoint Types

| Type    | Trigger condition                                        |
|---------|----------------------------------------------------------|
| Execute | CPU fetches an instruction at the address                |
| Read    | CPU reads from the address or range                      |
| Write   | CPU writes to the address or range                       |

### Breakpoint List Format

Each entry in the list shows a label and action buttons:

```
W[C123-C1FF] (5/10)  [Edit] [Disable] [Clear]
```

| Part           | Meaning                                              |
|----------------|------------------------------------------------------|
| `R`, `W`, `RW` | Access type (read, write, or either)                 |
| `[C123]`       | Address; or `[C123-C1FF]` for a range                |
| `(5/10)`       | Counter: hit count / threshold (shown when non-zero) |
| Action label   | `Fast`, `Slow`, `Tron`, `Troff`, or nothing (Break)  |

- **[Edit]** opens the Breakpoint Editor.
- **[Disable]** / **[Enable]** toggles the breakpoint without removing it.
- **[Clear]** deletes the breakpoint.

Breakpoints are persisted in the INI file under the `[DEBUG]` section.

### Breakpoint Editor

The editor dialog lets you set the address (or address range), access type, mapping
filter, action, and counters for a breakpoint.

**Access** options: `PC` (execute), `Address Access` (read/write).

**Mapping filter** (Map / ROM / RAM): restricts the breakpoint to fire only when the
CPU's current mapping matches. `Map` means fire regardless of mapping.

**Range**: check to enter a second address; any access in `[start, end]` triggers the
breakpoint.

**Actions**:

| Action  | Effect                                                       |
|---------|--------------------------------------------------------------|
| Break   | Pause execution (default)                                    |
| Fast    | Switch to maximum turbo speed                                |
| Slow    | Restore normal paced speed                                   |
| Tron    | Enable per-instruction execution trace                       |
| Troff   | Disable per-instruction execution trace                      |

**Counter**: enter a hit count and a reset count. With hit count `N` and reset count
`M`, the action fires on the Nth hit and then every Mth hit thereafter. Set both to 0
to disable counting.

**[Apply]** applies changes. **[Cancel]** discards them.

Setting an execute breakpoint from the keyboard while the cursor is in the Disassembly
view is faster: position the cursor and press **Opt+B**. A second press removes the
breakpoint.

## Hardware

The Hardware tab provides a read-only view of the emulated C64 hardware state using
data from the most recent runtime snapshot. All sections are collapsible tree views.

| Section       | Contents                                                   |
|---------------|------------------------------------------------------------|
| Memory/Banks  | CPU port banking, CPU-visible regions, VIC bank, `$D018` bases |
| VIC-II        | Raster position, IRQ state, all VIC registers, color, sprite state |
| CIA #1        | Ports A/B, timers A/B, ICR, TOD, alarm                    |
| CIA #2        | Ports A/B, timers A/B, ICR, TOD, alarm, IEC line state    |
| SID           | Voice parameters, filter, read-back registers, last sample |
| Counters      | Audio overrun/underrun, VIC BA events                      |

The Hardware tab is intended for diagnostics. Clicking values here does not modify
machine state.

## Assembler

The Assembler tab provides access to the integrated two-pass 6502 assembler.

### Assembler Controls

| Field        | Meaning                                                        |
|--------------|----------------------------------------------------------------|
| File Name    | Path to the root assembly source file; use **Browse...** to pick |
| Address      | Hex load and assembly origin address (default `$8000`)         |
| Run Address  | Hex address to jump to after successful assembly               |
| Auto Run     | If checked, sets PC to Run Address and resumes after assembly  |
| Reset        | If checked, resets the machine and waits for BASIC (`$E38B`) before assembling |
| **[Assemble]** | Assembles the source and loads bytes into C64 RAM            |

When **Reset** is checked (the default), assembly waits for BASIC to initialize before
writing code. This is the safe path for programs that expect a clean BASIC environment.
When **Reset** is unchecked, the assembler writes directly into live RAM in whatever
state the machine is in. If **Auto Run** is also set, the emulator immediately jumps to
the Run Address and resumes execution.

Assembler labels are exported to the debugger symbol table immediately after a
successful assembly, and appear in the Disassembly view.

If assembly fails, a scrollable error dialog shows each error with its source file and
line number.

### Assembler Language

The assembler supports standard 6502 mnemonics and addressing modes. C64 programs
target the 6510, which is instruction-compatible with the 6502.

**Comments:** `;` begins a comment; everything after it on the line is ignored.

**Labels:** start with a letter or `_`, may contain letters, digits, and `_`, and end
with `:`.

**Variables:** assigned with `ident = expr`. Postfix `++` and `--` modify the variable
as a prefix operation: `lda #i++` loads the value after increment.

**Current address:** `*` reads the current output address, but note that it returns
the address incremented by one at the point of evaluation. Use `* - 1` or the anonymous
label `:` (which returns the exact current address) when the exact value is needed.

```
* = $C000
a = *       ; a = $C001
: b = :-    ; b = $C000
```

**Numbers:**

| Prefix   | Base        |
|----------|-------------|
| `$`      | Hexadecimal |
| `%`      | Binary      |
| `0`      | Octal       |
| `1`-`9`  | Decimal     |

**Expressions** support the full C precedence table:

| Operators                            | Notes                               |
|--------------------------------------|-------------------------------------|
| `*`, `:`, numbers, variables, `(`    | Atoms                               |
| `+`, `-`, `<`, `>`, `~`             | Unary: plus, minus, low byte, high byte, bitwise NOT |
| `**`                                 | Exponentiation                      |
| `*`, `/`, `%`                        | Multiply, divide, modulo            |
| `+`, `-`                             | Add, subtract                       |
| `<<`, `>>`                           | Shift left, shift right             |
| `.lt`, `.le`, `.gt`, `.ge`           | Relational comparison               |
| `.eq`, `.ne`                         | Equality, inequality                |
| `&`                                  | Bitwise AND                         |
| `^`                                  | Bitwise XOR                         |
| `\|`                                 | Bitwise OR                          |
| `&&`, `\|\|`                         | Logical AND, OR                     |
| `?`, `:`                             | Ternary conditional                 |

The ternary form is `condition ? true-expr : false-expr`.

### Directives

| Directive               | Meaning                                                      |
|-------------------------|--------------------------------------------------------------|
| `.org n` or `* = n`     | Set assembly origin to address n                             |
| `.byte b[,b]*`          | Emit one or more bytes (strings accepted)                    |
| `.word w[,w]*`          | Emit 16-bit little-endian words                              |
| `.addr w[,w]*`          | Synonym for `.word`                                          |
| `.dword dw[,dw]*`       | Emit 32-bit little-endian double-words                       |
| `.qword qw[,qw]*`       | Emit 64-bit little-endian quad-words                         |
| `.drow w[,w]*`          | Emit 16-bit big-endian words                                 |
| `.drowd dw[,dw]*`       | Emit 32-bit big-endian double-words                          |
| `.drowq qw[,qw]*`       | Emit 64-bit big-endian quad-words                            |
| `.res l[,b]`            | Reserve `l` bytes, filled with `b` (default `$00`)          |
| `.align v`              | Advance to the next multiple of `v`, padding with zeros      |
| `.string "s"` or `.asciiz "s"` | Emit string bytes; `.asciiz` appends a `\0`         |
| `.strcode e`            | Set a per-character mapping expression using `_`; see below  |
| `.include "f"`          | Include and assemble another source file                     |
| `.incbin "f"`           | Include a binary file verbatim                               |
| `.define name text`     | Text substitution on word boundaries, skipping string literals |
| `.if cond`              | Conditional assembly; condition uses `.lt .le .gt .ge .eq .ne .defined` |
| `.else`                 | Alternate branch of `.if`                                    |
| `.endif`                | End of conditional block                                     |
| `.for init, cond, iter` | Loop with initialization, condition, and iteration clauses   |
| `.endfor`               | End of `.for` loop                                           |
| `.repeat n[,v]`         | Repeat block `n` times; optional counter variable `v`        |
| `.endrepeat` / `.endrep`| End of `.repeat` block                                       |
| `.macro name [args]`    | Define a macro with optional parameter list                  |
| `.endmacro`             | End of macro definition                                      |
| `.local name`           | Macro-local label; expanded to a unique name at call time    |
| `.scope [name]`         | Open a scope namespace; anonymous if no name given           |
| `.endscope`             | Close the innermost scope                                    |
| `.proc name`            | Open a named procedure (a named scope)                       |
| `.endproc`              | Close the innermost proc                                     |
| `.segdef "n",addr[,noemit]` | Define a named segment at `addr`; `noemit` suppresses output |
| `.segment "n"`          | Activate the named segment; `.segment ""` returns to native mode |
| `.6502`                 | Restrict to 6502 opcodes (default)                           |
| `.65c02`                | Allow 65C02 opcodes                                          |

**Paths:** `.include` and `.incbin` resolve relative to the directory of the including
file.

**`.byte` and strings:** `.byte` accepts strings as well as numeric expressions.
`.strcode` is not applied to string arguments of `.byte`.

### Macros

```
.macro name [arg [, arg]*]
    body
.endmacro
```

Arguments are substituted as text. Arguments that contain a comma must be wrapped in
double quotes: `"($55),y"`. Use `.local` inside a macro body to generate label names
that are unique per invocation.

```
.macro add_a_b a b
    clc
    lda a
    adc b
.endmacro

    add_a_b $12, $34
    add_a_b "($55),y", #12
```

### Scopes and Procs

`.proc` and `.scope` define namespaces that allow label reuse:

```
.proc loader
start:
    lda $D011
.endproc

.proc display
start:
    sta loader::start + 1    ; access a label in another scope
.endproc
```

Use `::` at the start of a name to resolve from the root scope. Labels in different
scopes do not collide. Anonymous scopes (`.scope` without a name) are useful inside
loops to prevent iteration labels from clashing:

```
.for i=0, i .lt 8, i++
  .scope
    addr = $D800 + i
  start:
    lda addr
  .endscope
.endfor
```

### Segments

Named segments let you lay out memory regions and switch between them:

```
.segdef "ZEROPAGE", $02, noemit
.segdef "CODE", $0810
.segdef "DATA", $C000

.segment "ZEROPAGE"
ptr:    .res 2

.segment "CODE"
main:
    lda (ptr),y
    ...

.segment "DATA"
table:  .byte $01,$02,$03
```

`noemit` segments advance the location counter but produce no output -- useful for
mapping zero-page variables without emitting placeholder bytes.

### Strcode

`.strcode e` remaps string characters using the expression `e`. The variable `_` holds
the current character's ASCII value:

```
.strcode _ .ge 'A' && _ .le 'Z' ? _ - 'A' : _ .ge 'a' && _ .le 'z' ? _ - 'a' : _
.string "Hello"
```

This remaps A-Z to 0-25 and a-z to 0-25, leaving other characters unchanged. Restore
default behavior with `.strcode _`. Quoted escape sequences (`\n`, `\r`, `\t`, `\0`,
`\\`, `\xNN`, `\0NN` octal) are processed in strings but are not passed through
`.strcode`.

## Configure

The Configure dialog (opened from **[Configure...]** in the Machine tab) has two tabs:
**Machine** and **Emulator**.

### Machine

| Control       | Effect                                            |
|---------------|---------------------------------------------------|
| PAL / NTSC    | Select the video standard; changes take effect on reboot |

### Emulator

| Control              | Effect                                          |
|----------------------|-------------------------------------------------|
| Scroll Wheel Lines   | Number of rows scrolled per wheel click (1-100) |
| Turbo Speeds         | Comma-separated multiplier list, e.g. `2,4,8,16` |
| Symbol Files         | Comma-separated paths to symbol files loaded at startup |
| Disk LEDs            | Show or hide the disk activity LED indicator    |
| Auto-save INI on Quit| Save `c64m.ini` automatically when quitting     |

### INI File

The INI file path is shown in the Configure dialog with a **Browse...** button. Changing
the path prompts to parse the selected file immediately. Save-on-quit behavior is
controlled by the **Auto-save INI on Quit** checkbox.

**[OK]** applies all changes immediately. **[Cancel]** discards them.

## INI Files

c64m reads `c64m.ini` from the current directory by default. Use `--inifile <path>` to
load a different file, or `--noini` to skip loading entirely.

All section names are case-insensitive. Comment lines start with `#`. Saving from the
emulator removes comments.

### [config]

| Key               | Value                                                  |
|-------------------|--------------------------------------------------------|
| `Save`            | `yes` -- save INI on quit                               |
| `scroll_wheel_lines` | Integer; lines scrolled per wheel click             |
| `symbol_files`    | Comma-separated list of symbol file paths              |
| `turbo_speeds`    | Comma-separated turbo multipliers, e.g. `2,4,8,16`    |

### [ui]

| Key    | Value                           |
|--------|---------------------------------|
| `leds` | `on` or `1` -- show disk LEDs   |

### [Video]

| Key              | Value                                                    |
|------------------|----------------------------------------------------------|
| `standard`       | `PAL` or `NTSC` (default `NTSC`)                        |
| `display_width`  | Integer; internal display width in pixels               |
| `display_height` | Integer; internal display height in pixels              |
| `integer_scale`  | `1` -- use integer-only scaling                          |
| `aspect_correct` | `1` -- preserve pixel aspect ratio (default on)          |
| `filter`         | `nearest` or `linear` (default `nearest`)               |

### [Window]

| Key      | Value                            |
|----------|----------------------------------|
| `width`  | Window width in pixels           |
| `height` | Window height in pixels          |

### [Layout]

| Key                    | Value                                              |
|------------------------|----------------------------------------------------|
| `split_display_right`  | Float 0-1; vertical split between C64 and debugger |
| `split_top_bottom`     | Float 0-1; horizontal split between top and bottom |
| `split_memory_misc`    | Float 0-1; split between memory and misc panel     |

### [rom] or [roms]

| Key         | Value                                |
|-------------|--------------------------------------|
| `basic`     | Path to BASIC ROM (8192 bytes)       |
| `kernal`    | Path to KERNAL ROM (8192 bytes)      |
| `char` or `character` | Path to character ROM (4096 bytes) |
| `system`    | Path to combined system ROM (16384 bytes) |

If no ROM paths are specified, c64m searches for files named `basic`, `kernal`,
`character`, and `system` (with any extension) in `.`, `rom/`, and `roms/`.

### [disk]

| Key      | Value                                            |
|----------|--------------------------------------------------|
| `disk8`  | Path to the D64 image for device 8               |
| `disk9`  | Path to the D64 image for device 9               |

### [DEBUG]

Breakpoints are stored as repeated `break` keys. Entries are not required to be unique
at the key level; a numeric suffix is added automatically when the emulator saves
multiple breakpoints.

Each entry has the form:

```
break.<suffix> = <address[-address]>[,pc|read|write|access][,map|rom|ram][,fast|slow|tron|troff][,count[,reset]]
```

| Part              | Meaning                                           |
|-------------------|---------------------------------------------------|
| `address`         | Hex address, e.g. `0xC000`                        |
| `-address`        | Optional range end                                |
| `pc`              | Execute breakpoint                                |
| `read`            | Read-access breakpoint                            |
| `write`           | Write-access breakpoint                           |
| `access`          | Read or write                                     |
| `map` / `rom` / `ram` | Mapping filter                              |
| `fast`            | Fast action                                       |
| `slow`            | Slow action                                       |
| `tron` / `troff`  | Trace on/off actions                              |
| `count`           | Hit count before action fires                     |
| `reset`           | Counter reset value after firing                  |

Examples:
```
break.1 = 0xE000,pc
break.2 = 0xD000-0xD3FF,write,fast
break.3 = 0xC000,pc,10,2
```

## Keyboard

Keys listed here are intercepted by the emulator before reaching the C64. On macOS,
**Opt** = Option/Alt.

### Emulator Keys

| Key             | Action                                                     |
|-----------------|------------------------------------------------------------|
| **F9**          | Toggle Debug Mode on/off                                   |
| **Opt+H**       | Toggle in-emulator help on/off                             |
| **F10**         | Step instruction (paused) or Pause (running)               |
| **Shift+F10**   | Step out of current subroutine                             |
| **F11** / **Opt+S** | Step over JSR                                         |
| **F12**         | Run (resume execution)                                     |
| **Shift+F12**   | Run to the cursor address in the Disassembly view          |
| **Opt+T**       | Cycle turbo speed                                          |
| **Opt+Tab**     | Cycle active view: C64 -> Disassembly -> Misc -> Memory    |
| **Shift+Opt+Tab** | Cycle active view in reverse                            |
| **Opt+1**       | Map gamepad to joystick port 1                             |
| **Opt+2**       | Map gamepad to joystick port 2 (default)                   |
| **Cmd+Q**       | Quit (macOS)                                               |

### Paste and Clipboard

| Key                | Action                                                  |
|--------------------|---------------------------------------------------------|
| **Opt+Ins**        | Paste clipboard as timed C64 keystrokes (~40 ms per key) |
| **Shift+Opt+Ins**  | Paste clipboard via direct keyboard-matrix injection    |

Timed paste scales with the active turbo multiplier. Matrix paste bypasses timing and
is more reliable for bulk text.

### C64 Key Mapping

The host keyboard maps semantically to C64 keys. Common mappings:

| Host Key           | C64 Key                                |
|--------------------|----------------------------------------|
| Letters A-Z        | A-Z                                    |
| Shift + Letter     | Shifted letter (left graphics set)     |
| Tab + Letter       | Commodore + Letter (right graphics set)|
| Digits 0-9         | 0-9                                    |
| Tab                | Commodore (C= key)                     |
| Ctrl               | CONTROL                                |
| Escape             | RUN/STOP                               |
| Backspace          | DEL                                    |
| Shift+Backspace    | INST (insert)                          |
| Delete             | RESTORE (triggers NMI)                 |
| Home               | HOME / CLR HOME                        |
| Shift+Home         | CLR (clear screen)                     |
| Arrow keys         | Cursor keys (left/up synthesize Shift) |
| F1-F8              | C64 F1-F8 (F2 = Shift+F1, F4 = Shift+F3, etc.) |
| Return             | Return                                 |
| Space              | Space                                  |
| `=`                | =                                      |
| `Shift+=`          | + (plus)                               |
| `-`                | - (minus)                              |
| `;`                | ;                                      |
| `Shift+;`          | :                                      |
| `,`                | ,                                      |
| `.`                | .                                      |
| `/`                | /                                      |
| backtick           | <- (left-arrow key)                     |
| `[` or `(`         | @                                      |
| `\` or `Shift+^`   | ^ (up-arrow key)                       |
| `]` or `Shift+*`   | *                                      |

All ASCII printable characters are mapped where possible. Characters with no C64
equivalent are silently ignored during paste.

## Details

This section records implementation details that are not necessary for day-to-day use
but are useful for understanding what the emulator actually does under the hood.

### Architecture

c64m uses a layered C99 architecture: Machine -> Runtime -> Frontend, with a shared Tools
layer. The machine (CPU, VIC-II, CIA, SID, bus, and drives) runs entirely on a dedicated
runtime thread. The UI and renderer run on the main thread. No live machine pointers
cross threads -- all inter-thread data travels as copied snapshots through a
command/event queue. The snapshot rule is strictly enforced: the frontend may only read
runtime-provided copies and must never access live machine state directly.

The machine owns a monotonic master cycle counter. VIC-II, CIA, and SID hooks receive
timestamped CPU bus events and advance their own state to the exact event cycle before
applying visible side effects.

### CPU

The MOS 6510 core is instruction-compatible with the 6502. Timed bus events classify
each memory access (read or write) and timestamp it within the opcode so that
mid-instruction side effects (CIA ICR reads, SID writes, VIC register writes with
raster-timed effects) land at the correct cycle. IRQ is level-sensitive; NMI uses an
edge-triggered latch so a single NMI edge triggers exactly once even if the NMI line
stays asserted. RESTORE routes through the NMI path.

BA stall: when VIC-II asserts BA, the CPU is held on read cycles only. Write cycles
continue normally. The emulator classifies each bus event as read or write so the stall
predicate is applied correctly. AEC is not modeled as separate emulator state.

### Memory and Bus

The full 64 K address space is backed by RAM. BASIC ROM ($A000-$BFFF), KERNAL ROM
($E000-$FFFF), and character ROM ($D000-$DFFF) are mapped in or out by the CPU I/O port
at $0001 (LORAM, HIRAM, CHAREN bits). I/O ($D000-$DFFF) takes priority over character
ROM when CHAREN is set.

Character ROM is also visible through the VIC-II bus in VIC banks 0 and 2 at the
corresponding 4 K ranges. VIC bank selection uses CIA #2 Port A bits PA0/PA1 with
inverted mapping (four 16 K banks).

Debugger-safe read functions (`c64_debug_read_cpu_map`, `c64_debug_read_ram`,
`c64_debug_read_rom`) let the UI inspect memory without triggering CIA ICR
clear-on-read, TOD latching, or other bus side effects. The memory and disassembly views
use these safe reads exclusively.

### VIC-II

Pixels are emitted cycle-by-cycle as VIC-II time advances. Mid-frame register changes
(color, scroll, mode bits) take effect at their exact event cycle rather than at frame
start. The internal frame is 384x272 (PAL) or 384x263 (NTSC); the displayed crop is a
balanced 352x240 window.

**Graphics modes:** standard text (40x25), multicolor text, standard bitmap, multicolor
bitmap, ECM (extended color), and invalid modes 5/6/7 (black background and display
layer). XSCROLL and YSCROLL are applied as delayed display-window edges. RSEL/CSEL
control 25/24-row and 40/38-column window clamps. DEN=0 blanks display and border pixels
to the background color ($D021).

**Sprites:** all 8 hardware sprites with 9-bit X position (MSB register), per-sprite
enable, hires (24x21) and multicolor (12x21 logical pixels) modes, X-expand and
Y-expand with correct flip-flop behavior, front/behind-foreground priority ($D01B),
sprite-sprite and sprite-background collision latches ($D01E/$D01F) with read-clear and
write-ignore, and IRQs (IMMC/IMBC) wired through the VIC IRQ path. Sprite pointer
(p-access) and data (s-access) fetches are bank-aware.

BA cycle stealing for sprite fetch windows is implemented with correct cross-line
handling for sprites 3 and 4.

**Registers:** full mirroring at $D000-$D3FF; unused high-bit masking per hardware spec;
open-bus high nibble on color register reads returns 1; unused block $D02F-$D03F reads
as $FF.

**VIC-II IRQ sources:** raster compare (programmable line), sprite-sprite collision, and
sprite-background collision. IRQ status ($D019) and enable ($D01A) with aggregate
enabled-pending bit 7.

### CIA

**CIA #1** drives keyboard and joystick input and generates IRQs via timer underflow.
Timers A and B are 16-bit down-counters with separate latch and live counter, continuous
and one-shot modes, Phi2/CNT/cascade source selection, PB6/PB7 output in pulse and
toggle modes, and ICR flag/mask management with clear-on-read. The keyboard matrix scan
supports bidirectional Port A/Port B scanning and multi-key simultaneous press with
correct active-low combining. Joystick port 1 reads from Port B bits 0-4; port 2 from
Port A bits 0-4. Keyboard and joystick inputs share CIA lines with correct combining.

**CIA #2** provides VIC bank selection (Port A PA0/PA1, inverted), IEC ATN/CLK/DATA
open-collector line modeling, and NMI via timer underflow. The NMI output uses an edge
latch to avoid repeated NMI assertions while the line stays asserted.

**Time-of-Day (TOD):** BCD tenths, seconds, minutes, hours in 12-hour AM/PM format with
correct 11->12 and 12->1 rollover. Source frequency (50/60 Hz) is selected per CRA bit 7.
Reading hours latches all TOD fields; reading tenths releases the latch. The alarm fires
via ICR bit 2 and routes through the normal IRQ/NMI mask logic. Debugger peeks read TOD
without creating or releasing the CPU-visible latch.

### SID

The MOS 6581 register map covers $D400-$D41F. Three voices each have a 24-bit phase
accumulator, four waveforms (triangle, sawtooth, pulse with 12-bit width, noise via a
23-bit LFSR), TEST bit, ADSR envelope with a fractional double accumulator and PAL rate
tables, and gate control. Voice 3 oscillator (phase bits 23-16) and envelope are
readable at $D41B/$D41C. Paddle reads ($D419/$D41A) return $FF.

The mixer scales each voice by its envelope, sums all three, divides by 3 for headroom,
and applies the master volume ($D418 bits 0-3). Voice 3 can be disconnected from the mix
via $D418 bit 7.

The Chamberlin state-variable filter runs once per cycle. Filter mode is selected by
$D418 bits 4-6 (LP/BP/HP); no mode bits bypass the filter. Filter states are clamped
to [-2, +2].

Audio reaches the host via a lock-free SPSC ring buffer. A fractional cycle accumulator
converts PAL (985248 Hz) or NTSC (1022727 Hz) machine cycles to the host sample rate
(48 kHz). Turbo mode skips audio writes to prevent buffer flooding while SID state
continues to advance. The SDL audio callback reads from the ring buffer on a separate
thread; it never calls runtime or machine code.

**SID deferred:** per-voice filter routing ($D417 bits 0-2), exact 6581/8580
combined-waveform analog blending, ring modulation and oscillator sync, paddle input
beyond not-connected policy, NTSC SID rate tables.

### Disk

Device 8 and device 9 each hold an independent read-only D64 image. The KERNAL LOAD
trap intercepts $FFD5 for devices 8/9 only; all other devices fall through to ROM.
`LOAD "NAME",8` loads at the BASIC start pointer and updates end-of-program pointers.
`LOAD "NAME",8,1` loads at the embedded PRG load address. `LOAD "$",8` synthesises a
tokenized BASIC directory listing with the disk title, file list, and blocks-free line.
Filename matching supports exact names, `*` prefix wildcard, `?` single-character
wildcard, and bare `*` for the first PRG. The parser validates chain links and guards
against sector loops and out-of-range references.

### Joystick

Port 1 is emulated via CIA #1 Port B bits 0-4. Port 2 via CIA #1 Port A bits 0-4. A
connected gamepad defaults to port 2; Opt+1 and Opt+2 reassign it.

### Display and Scaling

The C64 display is scaled to fit its panel with aspect-ratio correction and optional
integer scaling. Letterbox or pillarbox fills the unused space. The internal pixel
dimensions and scaling mode are configurable in the Configure dialog and saved to the
INI file.

### Vendored third-party code and assets
- `C64_TrueType_v1.2.1-STYLE`
  - Upstteam: <http://style64.org/c64-truetype>
  - License: http://style64.org/c64-truetype/license
- `stb/stb_ds.h`
  - Upstream: <https://github.com/nothings/stb>
  - License: public domain or MIT
- `inih/ini.c`, `inih/ini.h`
  - Upstream: <https://github.com/benhoyt/inih>
  - License: BSD-3-Clause
- `logc/log.c`, `logc/log.h`
  - Upstream: <https://github.com/rxi/log.c>
  - License: MIT
- `argparse/argparse.c`, `argparse/argparse.h`
  - Upstream: <https://github.com/cofyc/argparse>
  - License: MIT
- `whereami/whereami.c`, `whereami/whereami.h`
  - Upstream: <https://github.com/gpakosz/whereami>
  - License: MIT or WTFPL v2

## Versions

20 Jul 2026
:   The 1 week into it version.  Not a release yet.
