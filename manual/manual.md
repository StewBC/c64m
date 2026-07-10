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
| `--video PAL|NTSC`, `-P`, `-N` | Override the configured video standard for this run |
| `--disk <drive>=<image[,image…]>` | Mount a D64 image at startup, e.g. `--disk 8=game.d64`; comma-separated to pre-load a queue |
| `--prg <file>` / `-p`  | Load a file as PRG at startup                       |
| `--basic <file>` / `-B`| Load a file as BASIC program at startup             |
| `--crt <file>`         | Attach a generic 8K/16K cartridge at startup        |
| `--autorun` / `-a`     | Run automatically after load (combine with `--prg`, `--basic`, or `--disk`) |
| `--kbdjoy <0|1|2>`     | Drive the keyboard joystick on the given C64 port (`0` disables) |
| `--kbdjoy-layout <numpad|wasd>` | Select the keyboard joystick key layout        |
| `--audio-smoke`        | Emit a 440 Hz test tone to verify audio output      |

By default, c64m loads `c64m.ini` from the current directory. The INI file records
configuration, window size, debugger layout, and breakpoints.

### Video Standards

PAL and NTSC are selectable in the Configure dialog or via the `[Video]` INI section.
PAL runs at the 6569 timing (approximately 50 Hz frames), NTSC at the 6567 timing
(approximately 60 Hz frames). Changing the video standard reboots the emulated machine.
For a one-run override, use `--video PAL`, `--video NTSC`, `-P`, or `-N`; the command-line
choice is applied after the INI file is loaded.

### Disk Images

c64m supports D64 images on device 8 and device 9. Images mount read-only by default.
`LOAD "NAME",8`, `LOAD "NAME",8,1`, wildcard loads, and `LOAD "$",8` work through a
compatibility KERNAL trap by default. When an image is marked writable, standard
`SAVE "NAME",8` writes a PRG back into the mounted D64 and flushes the host `.d64`
file after the save. If a 1541 ROM is available and `[disk] emulate_1541=1` is set,
standard disk LOADs for devices 8/9 run through the real C64 KERNAL IEC routines and
the emulated 1541 DOS ROM instead.

### PRG and BASIC Files

`--prg <file>` (or `-p`) loads any file as a PRG at startup. The machine resets, boots
through KERNAL and BASIC, then injects the file bytes at the load address embedded in
the file's first two bytes. Execution continues automatically — no key press needed.

`--basic <file>` (or `-B`) loads any file as a BASIC program at startup. The machine
resets, boots to BASIC, writes the file to RAM at the address in its two-byte header,
and updates the BASIC start and end pointers (`$2B–$2E`).

For both options the file extension is irrelevant; the flag determines how the file is
treated, not the filename.

### Cartridges

c64m supports generic 8K and 16K `.crt` cartridges (normal hardware type). `--crt <file>`
attaches the cartridge at startup and resets with it running; a `.crt` file can also be
dragged onto the window at any time. ROML maps at `$8000–$9FFF` and, for 16K cartridges,
ROMH at `$A000–$BFFF`.

Loading a program (drag a `.prg`/`.t64`/`.bas`, or use `--prg`/`--basic`) detaches an
attached cartridge first, so the program boots to BASIC instead of the cartridge. To go
from a running cartridge to BASIC without loading a program — for example to reach a disk
you have mounted — use **[Reset]** and leave **Unmount cartridge on reset** checked (see
**Emulator Controls**). Because mounting a disk does not reset the machine, mounting a disk
while a cartridge is running only becomes reachable after such a reset.

### Auto Run

`--autorun` (or `-a`) can be combined with `--prg`, `--basic`, or `--disk` to start
execution without any manual key press.

**With `--prg` or `--basic`:** after the file bytes are placed in memory at the BASIC
warm-start point (`$E38B`), `RUN` followed by Return is injected directly into the
KERNAL keyboard buffer. The machine begins executing the program immediately.

**With `--disk 8=<image>`:** c64m uses a two-phase relay on the BASIC main-loop address:

1. On the first BASIC `READY` prompt after boot, `LOAD"*",8` and Return are injected
   into the keyboard buffer. The KERNAL loads the first PRG file from the disk image.
2. On the next `READY` prompt (after the load finishes), `RUN` and Return are injected.
   The loaded program starts.

Both paths write PETSCII characters directly into the KERNAL keyboard buffer
(`$0277-$0280`), so they work with any BASIC or KERNAL-based program. Games or demos
that bypass the KERNAL keyboard handler entirely will not receive the injected text.

`--autorun` alone, without a loader flag, has no effect.

### Drag and Drop

Files can be dragged onto the c64m window at any time while the emulator is running.
The file extension determines how the file is handled:

| Extension | Action                                                         |
|-----------|----------------------------------------------------------------|
| `.d64`    | Mount the image on device 8 (replaces any previously mounted disk) |
| `.c64state` | Load a saved machine state snapshot                         |
| `.crt`    | Attach a generic 8K/16K cartridge and reset with it running |
| `.bas`    | Load as a BASIC program (reset, boot to BASIC, inject, update `$2B–$2E`) |
| `.t64`    | Extract the first loadable T64 entry and load it like a PRG |
| anything else | Load as a PRG (reset, boot to BASIC, inject at embedded load address, auto-run) |

Extension matching is case-insensitive (`.D64` and `.d64` are treated identically).
Unlike the `--prg` and `--basic` startup flags, the extension drives the decision when
dropping — there is no way to override it by name alone.

Dropping a program (`.prg`, `.t64`, or `.bas`) while a cartridge is attached automatically
detaches the cartridge, so the dropped program boots instead of the cartridge. Dropping
another `.crt` replaces the current cartridge. See **Cartridges**.

Dropping a `.c64state` file restores the saved machine state. The snapshot must match
the currently loaded ROMs.

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

### Window Title

The OS window title shows whether the machine is running or paused, even when Debug Mode
is closed and no other indicator is visible:

| Title                    | Meaning                                                  |
|---------------------------|----------------------------------------------------------|
| `c64m - Running`          | The C64 is executing normally.                           |
| `c64m - Paused (reason)`  | Execution has stopped. `reason` is one of `breakpoint`, `BRK`, `step`, `reset`, `pause`, or `run complete`. |
| `c64m - Error`            | The runtime hit an error and stopped.                    |

This is the quickest way to tell whether the emulator is waiting on something (paused) or
just busy (running) without opening the debugger.

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

### Help

Press **Opt+H** or **ESC** to open or close the in-emulator help overlay. The C64 is
paused while the overlay is open and resumes when it is dismissed.

The overlay shows one section of the manual at a time in a scrollable content area. A
navigation bar along the bottom of the overlay contains:

| Control | Action |
|---------|--------|
| **Prev** | Go to the previous section. Inert when already on the first section. |
| *Section name* (centre) | Shows the current section. Click to open a pop-up index of all sections; click any entry to jump directly to it. |
| **Next** | Go to the next section. Inert when already on the last section. |
| **Search:** field | Type a search term and press **Enter**, **->**, or **<-** to search. Supports regular expressions. |
| **<-** | Find the previous match, searching backward from the current match. |
| **->** | Find the next match, searching forward from the current match. |

Search is case-insensitive. When no match exists the search text turns red; it returns
to normal as soon as the term is changed. Both directions wrap around the full document.
Navigating to a new section via **Prev**, **Next**, or the index resets the search
starting point to the top of that section.

Keyboard shortcuts active while the help overlay is open:

| Key | Action |
|-----|--------|
| **Left / Right** | Previous / next section |
| **PageUp / PageDown** | Scroll content up / down by one page |
| **Home** | Scroll to top of the current section |
| **End** | Scroll to bottom of the current section |
| **ESC** | Close the help overlay |

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

### Effective Address And Value

When the emulator is paused, lines whose target address is not already obvious
from the operand gain a trailing annotation showing the resolved address and,
for memory reads and writes, the byte currently at that address:

```
C123:             B1 FB       LDA ($FB),Y   [$4050:25]
```

Here the pointer at `$FB/$FC` plus the current **Y** register resolves to
`$4050`, which currently holds `$25`. The address is computed from the current
CPU registers and the CPU-visible memory, so it reflects what the running CPU
would actually read or write.

The annotation appears for:

- indexed and indirect operands, such as `$40,X`, `$40,Y`, `screen,X`,
  `screen,Y`, `($40,X)`, and `($FB),Y`, which show `[$addr:value]`;
- `JMP ($xxxx)` indirect jumps, which show the resolved `[$addr]` target;
- direct addresses, branches, `JMP`, and `JSR` operands that are shown as a
  **label** — data references show `[$addr:value]`, branch and jump targets
  show `[$addr]`.

It is deliberately omitted where the address is already plain in the operand,
such as `LDA #$00` (immediate), `LDA $4000` (literal absolute), and
`LDA $FB` (literal zero page). Because the annotation depends on the current
register and memory snapshot, it is shown only while the machine is paused, and
it is not drawn while the emulator is running.

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

Switch modes with **right-click** anywhere in the view (the **Source** group lists all
three with an asterisk next to the active choice), or with **Opt+M** from the keyboard.

When the emulator is paused, the same popup also shows an **Access** group for the
address under the disassembly cursor. The four `XXXX` entries are the recorded program
counters of the last instructions that wrote to that C64 address, oldest retained entry
first and newest entry last. `0000` means no writer has been recorded for that slot.
Selecting one of the entries moves the Disassembly cursor to that address, the same kind
of jump as entering the writer PC with `Opt+A`.

**Tab** and **Shift+Tab** cycle the symbol display through `auto`, `names`, and `raw`
modes independently of the source mode.

### Keyboard Controls

| Key             | Action                                                     |
|-----------------|------------------------------------------------------------|
| `Opt+A`         | Enter address-jump mode; type four hex digits then Enter   |
| `Opt+B`         | Toggle execute breakpoint at cursor (paused only)          |
| `Opt+M`         | Cycle source mode: Map -> ROM -> RAM -> Map                |
| `Opt+S`         | Open the Symbol Lookup dialog                              |
| `Opt+Left`      | Set PC to cursor address (paused only)                     |
| `Tab`           | Cycle symbol display mode forward                          |
| `Shift+Tab`     | Cycle symbol display mode backward                         |
| `Up` / `Down`   | Move cursor one instruction                                |
| `PgUp` / `PgDn` | Scroll one page                                            |
| `Home` / `End`  | Jump to first or last line of the current view             |
| `Opt+Home`      | Jump to address `$0000`                                    |
| `Opt+End`       | Jump to address `$FFFF`                                    |

### Symbol Lookup

**Opt+S** opens the Symbol Lookup dialog while the Disassembly view is active.

The dialog shows a searchable, sortable table of all symbols known to the debugger,
including labels exported from the assembler and symbols loaded from external symbol
files.

**Columns:**

| Column  | Contents                                                          |
|---------|-------------------------------------------------------------------|
| `ADDR`  | Symbol address in hex (`XXXX`)                                    |
| `SCOPE` | Assembler scope path, e.g. `anon_0001` (up to 15 characters)     |
| `LABEL` | Symbol name (leaf portion, up to 15 characters)                   |
| `SOURCE`| File basename (no extension), or `assembler` for inline assembly  |

**Search:** the field at the top has focus when the dialog opens. Type to filter the
list. The pattern is matched against a combined string `"XXXX scope label source"` for
each row using simple regex syntax: `.` matches any character, `*` matches zero or more
of the previous character, `^` anchors to the start, `$` anchors to the end.

**Sorting:** clicking any column header sorts by that column ascending (`^`). Clicking
the same header again reverses to descending (`v`). The default sort is by address
ascending.

**Navigation:**

| Key / Action            | Effect                                                 |
|-------------------------|--------------------------------------------------------|
| Type in search box      | Filter rows to matching symbols                        |
| `Tab`                   | Switch keyboard focus between search box and table     |
| `Up` / `Down`           | Move the selection in the table (table focus)          |
| `Enter`                 | Commit selected row (table focus)                      |
| Click a row             | Commit that row                                        |
| Click a column header   | Sort by that column (toggle direction)                 |
| **[Close]** or `ESC`    | Dismiss without navigating                             |

**On commit:** the Disassembly view cursor jumps to the symbol's address, equivalent to
entering the address with `Opt+A`.

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

The memory view has source modes that control which address space is displayed:

| Mode    | Mode border | Bytes shown                                              |
|---------|-------------|----------------------------------------------------------|
| **Map** | none        | C64 CPU-visible address space (current bank configuration) |
| **ROM** | amber       | Physical C64 ROM bytes at ROM addresses, regardless of mapping; RAM elsewhere |
| **RAM** | blue        | Raw C64 RAM at every address, regardless of any ROM overlay |
| **1541 Map 8** | gray | Device 8's 1541 address map, read-only                  |
| **1541 Map 9** | gray | Device 9's 1541 address map, read-only                  |

The 1541 map modes are inspection views for the selected drive. They show drive RAM at
`$0000-$07FF`, the RAM mirror at `$0800-$0FFF`, the serial VIA at `$1800-$1BFF`, the
disk-controller VIA at `$1C00-$1FFF`, and the 1541 ROM at `$C000-$FFFF` when a ROM is
loaded for that drive. Addresses outside the available 1541 map are shown as `--` in the
hex column and blank in the ASCII column.

ROM, RAM, and 1541 map modes draw a colored source-mode border inside the content area.
Map has no source-mode color; if the view is active, the separate neutral active-view
border is still shown.

Switch modes with **right-click** anywhere in the view (the **Source** group lists all
choices with an asterisk next to the active choice), or with **Opt+M** from the keyboard.

The memory and disassembly view modes are independent of each other -- for example, you
can watch raw RAM in the memory view while the disassembler follows the CPU map
simultaneously. The disassembly view remains a C64 view; selecting a 1541 map in memory
does not change the CPU register panel, stepping behavior, breakpoints, or disassembly.

### Status Row

The bottom of the Memory view shows the active edit field (`Hex`, `ASCII`, or
`Address`), the current cursor address as `Address: XXXX`, and whether memory editing
is currently `editable` or `read-only`.

### Virtual Views

The memory panel can be split into up to 16 independent virtual views stacked vertically.
Each virtual view maintains its own cursor, scroll position, source mode, and edit state.
A thin separator line marks the boundary between adjacent views.

**Splitting** inserts a new view directly below the active view. The new view inherits the
active view's source mode and starts with its cursor at the split address. Row height is
distributed proportionally among all views; each view has a minimum of one row.

**Dissolving** removes the active view and returns its rows proportionally to the remaining
views. If only one view exists, dissolving is a no-op. After dissolving, focus moves to
the view below, or to the view above if the dissolved view was the bottommost.

Each virtual view has a unique background color drawn from a 16-slot palette. Slots are
assigned in order and freed when a view is dissolved; a freed slot is reused by the next
split.

Click anywhere in a view to make it active. The mouse wheel scrolls the view under the
pointer regardless of which view is currently active.

ROM/RAM/1541 source-mode borders are drawn inside each view's own region. The neutral
active-panel selection border still wraps the entire memory panel regardless of how many
views are present.

The scrollbar on the right represents the active view's position in the 64 K space.
Switching the active view moves the thumb without scrolling the memory itself.

Right-clicking a memory view opens a popup for the view under the pointer. The
**Source** group changes that view's source mode. The **View** group can **Split** the
clicked view at the clicked address; when more than one virtual view exists it also
offers **Join** to dissolve the clicked view.

When the emulator is paused, the popup also shows an **Access** group for the clicked
address. The four `XXXX` entries are the write history for that C64 address:

```
oldest  older  newer  newest
```

Each entry is the 16-bit program counter of an instruction that wrote to the address.
For example, `0000 0000 A000 A123` means two writes have been recorded so far: first
from `$A000`, then from `$A123`. Selecting an entry moves the Disassembly cursor to that
writer PC.

### Keyboard Controls

| Key               | Action                                                       |
|-------------------|--------------------------------------------------------------|
| `Opt+A`           | Toggle address-entry mode; type four hex digits to jump      |
| `Opt+M`           | Cycle source mode: Map -> ROM -> RAM -> 1541 Map 8 -> 1541 Map 9 -> Map |
| `Opt+S`           | Open the Symbol Lookup dialog                                |
| `Opt+X`           | Toggle between hex and ASCII edit modes                      |
| `Opt+V`           | Split active view at cursor                                  |
| `Shift+Opt+V`     | Split active view at the start of the cursor row             |
| `Opt+J`           | Dissolve active view (no-op when only one view exists)       |
| `Opt+Up`          | Switch focus to the view above                               |
| `Opt+Down`        | Switch focus to the view below                               |
| `Up` / `Down`     | Move cursor one row (16 bytes)                               |
| `Left` / `Right`  | Move cursor one byte (or nibble in hex mode)                 |
| `PgUp` / `PgDn`   | Scroll one page                                              |
| `Home`            | Move cursor to start of the current row                      |
| `Opt+Home`        | Move cursor to the start of the visible window               |
| `End`             | Move cursor to end of the current row                        |
| `Opt+End`         | Move cursor to the end of the visible window                 |
| `0-9`, `A-F`     | Edit hex nibble at cursor (paused only, hex mode)            |

**Opt+S** also opens the Symbol Lookup dialog from the Memory view. On commit, the
active virtual view scrolls so that the symbol's address is row-aligned (the row
containing that address appears at the top of the view) and the cursor is placed on
the exact byte. See **Symbol Lookup** under **Disasm** for full dialog reference.

Memory editing is only possible while the CPU is paused. In hex mode, typing hex digits
overwrites the nibble at the cursor. In ASCII mode, printable characters overwrite the
byte at the cursor. The 1541 map modes are read-only; edit keystrokes are ignored there.

## Machine

The Machine tab is the first tab in the Misc panel and groups controls for disks,
programs, and emulator management.

### Disks

Devices 8 and 9 each show a row of controls followed by a disk selector:

```
[8] [Add] [Eject]  <disk name ▼>
[9] [Add] [Eject]  <disk name ▼>
```

Each device maintains an ordered queue of D64 images. At most one image is mounted at a
time; the rest are queued for later use.

**[8] / [9]** — Opens a file browser. Selecting an image replaces the entire queue with
that one disk and mounts it immediately.

**[Add]** — Opens a file browser. The selected image is inserted into the queue
immediately after the currently mounted disk. If the drive is empty, the image is added
and mounted.

**[Eject]** — Removes the currently mounted disk from the queue and mounts the next one.
If the last disk in the queue is ejected, the drive becomes empty. The queue wraps
round-robin: ejecting the final entry mounts the first remaining entry, not nothing.

**[Eject!]** (hold Shift while clicking **[Eject]**) — Ejects all disks from the queue
and leaves the drive empty.

The selector to the right of the buttons shows the basename of the currently mounted
image. Clicking it opens a drop-down listing every image in the queue; selecting one
mounts it immediately and makes it current.

The queue order and current index are not saved when the emulator quits. On the next
launch the first image in the saved list is mounted.

### Programs

**[Load]** opens the Load dialog:

| Field           | Meaning                                                    |
|-----------------|------------------------------------------------------------|
| Name + Browse   | Select the host PRG or binary file to load                 |
| From File       | Read the two-byte load address header from the file (default on) |
| Address field   | Manual load address in hex, active when From File is off   |
| Reset           | Reset the machine and wait for BASIC (`$E38B`) before injecting |
| Basic Program   | Update TXTTAB (`$2B/$2C`) and VARTAB (`$2D/$2E`) after load |
| Basic Text      | Treat the file as ASCII BASIC listing, tokenize it, and load it at `$0801` |

Selecting a `.T64` file extracts the first loadable tape-container entry and loads it
through the PRG-style reset/inject path. The raw binary options in this dialog do not
apply to `.T64` files.

The Load dialog is keyboard-driven: **Return** (or keypad Return) performs the same
action as **[OK]** when the Name field is nonempty, including the normal address
validation. **Opt+Return** opens **Browse...**. Once the file browser is open, it is the
active dialog, so typing selects a file and **Return** opens the selected file.

**Basic Text** loads a plain-text BASIC listing — the kind you get from a `LIST`,
or an ASCII `.bas` file — rather than a tokenized PRG. c64m tokenizes the source on
the host exactly as the C64 would, writes the program to memory at `$0801`, and sets
the BASIC pointers so the result is ready to `LIST` or `RUN`. Each line must begin with
a line number, and lines are expected in ascending order. Only stock BASIC V2 keywords
are recognized; extension dialects such as Simon's BASIC are not supported. When Basic
Text is selected the From File and Address controls do not apply, and it is mutually
exclusive with Basic Program.

**[Save]** opens the Save dialog:

| Field                | Meaning                                                 |
|----------------------|---------------------------------------------------------|
| Name + Browse        | Choose the output filename                              |
| Basic Program        | Read start and end from `$2B-$2E`; forces header on    |
| Basic Text           | Detokenize the live BASIC program and save it as an ASCII listing |
| Write address header | Prefix the saved file with the two-byte load address   |
| Start / End          | Hex address range for a raw memory save                |

**Basic Text** saves the BASIC program currently in memory as a plain-text listing,
the same text you would see from a `LIST`, instead of a tokenized PRG. The program is
read from the live BASIC pointers (`$2B-$2E`) and written with no load-address header.
As with loading, only stock BASIC V2 keywords are handled. When Basic Text is selected
the Start / End fields do not apply, and it is mutually exclusive with Basic Program.

Control codes embedded in string literals — cursor movement, colour changes, CLR/HOME,
reverse on/off, and so on — cannot be written as raw ASCII, so they are saved as named
escapes in braces and translated back to the original bytes on load. For example a
CLR/HOME character (`CHR$(147)`) is written as `{clr/home}`, HOME as `{home}`, cursor
down as `{down}`, and colour codes as `{red}`, `{blue}`, and so on. The `π` character is
written as `{pi}`. Any byte without a name — including graphics characters — is written
as a hexadecimal escape such as `{$a0}`; a decimal form like `{147}` is also accepted on
load. Escapes are recognized anywhere in a line, so a literal `{` in the text is itself
written as `{$7b}`. Because the codes are normalized to the uppercase/graphics character
set, lowercase-mode listings and embedded graphics characters do not round-trip
literally, but their exact bytes are always preserved through the numeric escapes.

### State

**[Load...]** opens a file browser for a `.c64state` snapshot and restores it.

**[Save As...]** opens a save dialog and writes a named `.c64state` snapshot.

State snapshots preserve the emulated machine state, RAM, color RAM, CPU, VIC-II, CIA,
SID, attached generic cartridge, mounted D64 references, and the frontend keyboard
joystick layout/port. The current v1 format stores references and hashes for external
content rather than embedding every ROM or media byte, so a snapshot is expected to be
loaded with the same ROM files available.

**Shift+Opt+>** quicksaves to the snapshot folder (Configure → Paths → `snapshot`,
which defaults to the current directory). Each quicksave creates a new timestamped
`.c64state` file; existing quicksaves are not overwritten. **Shift+Opt+<** quickloads
the newest `.c64state` in that folder.

### Emulator Controls

**[Configure...]** opens the Configure dialog (see **Configure**).

**[Reset]** performs a hard reset of the emulated C64. Any pending PRG injection or
assembler-queued run is cancelled. If a cartridge is attached, **[Reset]** first opens
a prompt with an **Unmount cartridge on reset** checkbox (checked by default): leaving it
checked resets to BASIC with the cartridge removed, while unchecking it keeps the
cartridge so the reset re-runs it. When no cartridge is attached, Reset happens
immediately with no prompt.

## Debugger

The Debugger tab shows runtime counters and the call stack.

### Counters

| Counter         | Meaning                                              |
|-----------------|------------------------------------------------------|
| CPU cycles      | Total 6510 cycles executed since last reset          |
| Machine cycles  | Master cycle counter                                 |
| VIC cycles      | VIC-II cycles advanced                               |
| CIA cycles      | CIA cycles advanced                                  |
| Frame           | Frame number, cycle within frame, dropped frames     |

### Step Counters

```
Step - CPU: 2  Machine: 2
```

The step counter line resets to zero each time a step or run command is issued (F10,
Shift+F10, F11, F12, Shift+F12) and freezes when the CPU stops. It shows how many cycles
elapsed during that operation.

Two values are shown because they can differ:

- **CPU** — cycles the 6510 actually spent executing. This is deterministic with respect
  to your code and is the right metric when profiling or counting instruction cycles.

- **Machine** — wall-clock cycles of the C64 chip: every tick of the crystal oscillator,
  including cycles where the CPU was frozen while VIC-II held the bus. This reflects real
  elapsed time on the hardware.

The difference between the two is **BA stall cycles**. When VIC-II needs the bus to fetch
sprite or bitmap data it asserts the BA (Bus Available) line low. The CPU sees RDY go low
and freezes — it has no say in the matter. Depending on sprite configuration and timing,
a stall can last 1–3 cycles or longer. Because stalls depend on what VIC-II is doing at
that exact moment, machine cycle counts can vary between otherwise identical runs. CPU
cycle counts will not.

In practice the two numbers are identical whenever no sprites are active or the measured
code does not overlap a BA window. When they differ, the gap is the VIC-II overhead, not
your code.

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

### BRK

A `BRK` opcode ($00) always pauses the emulator, with no breakpoint needed. The CPU does
not execute it; the instruction is intercepted before the stack is touched, and the
window title (see **Window Title**) reads `c64m - Paused (BRK)`.

This matters because real C64 software essentially never executes BRK on its normal
control path. If the CPU lands on one anyway, it is almost always a sign that execution
has run off the rails — into uninitialized memory, past the end of a program, or through
a corrupted jump vector. Letting that BRK execute is rarely useful: with no real KERNAL
handler behind the vector it triggers, the CPU just re-enters BRK on whatever it jumps to
next, repeatedly pushing to the stack and wrapping the stack pointer through `$0100-$01FF`
forever. Pausing on the first BRK stops that immediately so you can inspect what happened
instead of digging through an overwritten stack.

This applies only to free-running execution (Run, Step Over, Step Out, and run-N
commands). A single explicit step (**F11**) still executes a BRK normally, since you asked
for that exact instruction.

### Breakpoint List Format

Each entry in the list shows a label and action buttons:

```
W[C123-C1FF] (5/10)  [Edit] [Disable] [Clear]
```

| Part           | Meaning                                                        |
|----------------|----------------------------------------------------------------|
| `R`, `W`, `RW` | Access type (read, write, or either)                           |
| `[C123]`       | Address; or `[C123-C1FF]` for a range                          |
| `(5/10)`       | Counter: total hits / repeat countdown (shown when counter is active) |
| Action label   | `Fast`, `Slow`, `Tron`, `Troff`, `Swap`, `Type`, or nothing (Break) |

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

| Action  | Parameter field | Effect                                                  |
|---------|-----------------|---------------------------------------------------------|
| Break   | —               | Pause execution (default)                               |
| Fast    | —               | Switch to maximum turbo speed                           |
| Slow    | —               | Restore normal paced speed                              |
| Troff   | —               | Disable per-instruction execution trace                 |
| Tron    | Filename        | Enable per-instruction execution trace; writes to the given file, or `trace.log` if the field is empty |
| Swap    | Queue step      | Navigate the device 8 disk queue (see below)            |
| Type    | Text            | Inject text as C64 keystrokes when the breakpoint fires |

Tron and Troff are mutually exclusive: checking one automatically clears the other.
When Tron, Swap, or Type is unchecked, its parameter field is grayed out.

**Swap parameter format:**

| Form  | Meaning                                                              |
|-------|----------------------------------------------------------------------|
| `+N`  | Move forward N steps in the queue (wraps)                            |
| `-N`  | Move backward N steps in the queue (wraps)                           |
| `N`   | Mount the Nth disk in the queue, 1-based (wraps if out of range)     |
| empty | No-op (Swap flag is set but does nothing)                            |

**Counter**: enter a hit count and a repeat count. With hit count `N` and repeat count
`M`, the action fires on the Nth hit and then every Mth hit thereafter. Set repeat to
`0` to auto-disable the breakpoint after it fires once. Set both to 0 to disable
counting (the Use Counter checkbox controls this).

**Type text format:**

The Type field uses an escape-based input encoding. Literal printable characters (space
through `~`) are typed as-is; `\` introduces an escape sequence. Uppercase letters
produce SHIFT+key; lowercase produce the unshifted key. Up to 128 events per sequence.

| Form              | Meaning                                                          |
|-------------------|------------------------------------------------------------------|
| `\[NAME]`         | Press and release named key; bare modifiers are one-shot         |
| `\[NAME+]`        | Assert (hold) named key                                          |
| `\[NAME-]`        | Deassert (release) named key                                     |
| `\[W:N]`          | Wait N normal keypress durations (`0` is a no-op)                |
| `\[WAIT:N]`       | Same as `\[W:N]`                                                 |
| `\xHH`            | PETSCII byte, two hex digits                                     |
| `\dDDD`           | PETSCII byte, three decimal digits                               |
| `\oOOO`           | PETSCII byte, three octal digits                                 |
| `\mR,C`           | Direct matrix key: row R, column C (digits 0–7)                  |
| `\jPD` / `\jPD,B` | Joystick: port P (1–2), direction D (0–8), optional button B (0–1) |

Direction codes: 0=centre, 1=up, 2=up-right, 3=right, 4=down-right, 5=down,
6=down-left, 7=left, 8=up-left.

**Named keys (case-insensitive):**

| Name       | Alias | Key           | Name        | Alias | Key          |
|------------|-------|---------------|-------------|-------|--------------|
| `RETURN`   | `RT`  | Return        | `SHIFT`     | `SH`  | Shift        |
| `RESTORE`  | `RE`  | Restore (NMI) | `CBM`       | `CB`  | Commodore    |
| `RUNSTOP`  | `RS`  | Run/Stop      | `CTRL`      | `CT`  | Control      |
| `CLRHOME`  | `CH`  | Clr/Home      | `SPACE`     | `SP`  | Space        |
| `INSDEL`   | `ID`  | Ins/Del       | `POUND`     | `PO`  | £            |
| `CUU`      |       | Cursor up     | `LEFTARROW` | `LA`  | ←            |
| `CUD`      |       | Cursor down   | `UPARROW`   | `UA`  | ↑            |
| `CUL`      |       | Cursor left   | `PI`        |       | π            |
| `CUR`      |       | Cursor right  | `F1`–`F8`   |       | Function keys |
| `PLUS`     |       | +             | `MINUS`     |       | -            |
| `AT`       |       | @             | `ASTERISK`  | `AS`  | *            |

Characters with no C64 equivalent produce a parse error. The editor rejects the commit
and positions the cursor at the offending character; the INI loader logs the error and
skips the Type action for that breakpoint.

Examples:

```
load\[RT]               Type LOAD and press Return
\[SH+]\[F1]\[SH-]       Produce F2 (SHIFT+F1) but \[F2] would have also worked
\[CT]S                  Type Ctrl+S, then release Ctrl
\[CT]\[SH]S             Type Ctrl+Shift+S, then release both modifiers
\[CTRL+]\[CTRL]SA\[CTRL-]  Keep Ctrl held for both S and A
\x22*\x22,8,1\[RT]      "* ",8,1 then Return
\j11,1                  Joystick port 1 up with fire button
\[RS]\[RE]              RUN/STOP + RESTORE soft reset (see note below)
S\[W:2]A                Type S, wait for two normal keypress durations, then A
S\[WAIT:2]A             Same as above
```

Bare `SHIFT`, `CTRL`, `CBM`, and `RUNSTOP` tokens are one-shot modifiers. They are
asserted immediately and released after the next non-modifier key or RESTORE event
completes. The short aliases are `SH`, `CT`, `CB`, and `RS`.

Explicit `+` and `-` modifier forms still hold and release keys manually.
For example, `\[CTRL+]\[CTRL]SA\[CTRL-]` keeps Ctrl held for both `S` and `A`;
the middle bare `\[CTRL]` is redundant because Ctrl is already explicitly held.

**RESTORE and the `+`/`-` modifier:** `RESTORE` is an NMI key, not a keyboard matrix key.
The `+` and `-` modifiers are silently ignored — `\[RE]`, `\[RE+]`, and `\[RE-]` all fire
the same one-shot NMI. There is no way to "hold" RESTORE.

**RUN/STOP + RESTORE soft reset:** Use `\[RS]\[RE]`.

Alias note: `RE` means RESTORE; `RS` means RUNSTOP.

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
| Auto Run        | If checked, sets PC to Run Address and resumes after assembly  |
| Reset C64       | If checked, resets the machine and waits for BASIC (`$E38B`) before assembling |
| Rearm one-shots | If checked, re-enables every auto-disabled one-shot breakpoint (`repeat = 0`) and resets its hit counter before assembling |
| **[Assemble]**  | Assembles the source and loads bytes into C64 RAM            |

When **Reset C64** is checked (the default), assembly waits for BASIC to initialize
before writing code. This is the safe path for programs that expect a clean BASIC
environment. When **Reset C64** is unchecked, the assembler writes directly into live
RAM in whatever state the machine is in. If **Auto Run** is also set, the emulator
immediately jumps to the Run Address and resumes execution.

**Rearm one-shots** is useful during iterative development: set a breakpoint with
`repeat = 0` so it fires exactly once, then check this box so each re-assemble brings
it back to life without manual re-enabling.

Assembler labels are exported to the debugger symbol table immediately after a
successful assembly, and appear in the Disassembly view.

If assembly fails, a scrollable error dialog shows each error with its source file and
line number.

### Assembler INI persistence

The following assembler settings are saved in the `[assembler]` section of the INI
file and restored on next launch. All keys are optional.

```
[assembler]
file           = path/to/source.asm   ; path to source file (relative to INI or absolute)
address        = 8000                 ; hex load/assembly origin address
run_address    = 8000                 ; hex run address
auto_run       = no                   ; jump to run address after assembly (default: no)
reset          = yes                  ; Reset C64 before assembling (default: yes)
rearm_oneshots = no                   ; Rearm one-shot breakpoints before assembling (default: no)
```

### Assembler Language

The assembler supports standard 6502 mnemonics and addressing modes. C64 programs
target the 6510, which is instruction-compatible with the 6502.

**Comments:** `;` begins a comment; everything after it on the line is ignored.

**Labels:** start with a letter or `_`, may contain letters, digits, and `_`, and end
with `:`.

**Variables:** assigned with `ident = expr`. Postfix `++` and `--` modify the variable
as a prefix operation: `lda #i++` loads the value after increment.

**Current address:** `*` reads the current output address — the address of the
instruction or line being assembled, following the standard assembler convention. For
example `jmp *` assembles a self-loop.

```
* = $C000
a = *       ; a = $C000
jmp *       ; jmp $C000 (self-loop)
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
| `.scope [name] [file="f"] [dest="d"]` | Open a scope namespace; anonymous if no name given. `file=`/`dest=` redirect the scope's output to a separate file (command-line `c64masm` only) |
| `.endscope`             | Close the innermost scope (and end any output redirect)     |
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

### Output Redirection (Multiple Files)

A named `.scope` can send its output to a separate file, so a single source can build
several binaries in one pass -- a loader plus a game, a main program plus overlays, and
so on:

```
* = $0801
        ; ... loader code, written to the default output ...

.scope game file="game.bin"
    * = $C000
main:
        inc $D021
        jmp main
.endscope
```

Everything between the named `.scope file="..."` and its `.endscope` is assembled into
its own output image; code outside goes to the default output. Labels remain visible
across files through the normal scope rules (`game::main`), so the loader can reference
addresses in the game and vice versa. An optional `dest="..."` names the target for
tooling without binding it to a file name.

Output redirection is honoured by the command-line assembler (`c64masm`, below). The
in-emulator Assembler tab assembles into live RAM and has no per-file targets, so a
`.scope` with `file=`/`dest=` reports an error there.

### Build-Time Detection

The define `C64MASM` distinguishes how the source is being assembled: it is `1` under the
command-line `c64masm` tool and `0` when assembled by the in-emulator Assembler tab. Use
it to switch behaviour between a live-poke session and a file build:

```
.if C64MASM
    * = $0801           ; standalone build: normal load address
.else
    * = $C000           ; live in the emulator: assemble into spare RAM
.endif
```

Additional build flags can be injected from the command line with `-D name[=value]`
(see below) and tested the same way.

### Command-Line Assembler (c64masm)

`c64masm` is a standalone build of the same assembler, for use in scripts and makefiles.
It writes raw binary files rather than poking live memory.

```
c64masm -i <infile> [-o <outfile>] [-a <addr>] [-s <symfile|->]
        [-D name[=value]]... [-v] [-h]
```

| Switch            | Effect                                                             |
|-------------------|-------------------------------------------------------------------|
| `-i <infile>`     | Assembly source to assemble (required)                            |
| `-o <outfile>`    | Binary output for the default (unnamed) target                    |
| `-a <addr>`       | Origin/load address of the default target (default `$0000`; accepts `$hex`, `0xhex`, or decimal). Not needed if the source sets its own origin with `* =` / `.org` |
| `-s <symfile\|->` | Write a symbol + segment listing; `-` sends it to stdout          |
| `-D name[=value]` | Predefine a text define (value defaults to `1`); repeatable       |
| `-v`              | Verbose: hex-dump each target's emitted bytes                     |
| `-h`              | Show usage                                                        |

Each output file contains exactly the range of addresses the source emitted into.
Named `.scope file="..."` targets are written to their own files (resolved relative to
the current directory). `C64MASM` is predefined to `1`.

```
c64masm -i demo.asm -o loader.prg -a $0801 -D VERSION=3 -s symbols.txt
```

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
| Keyboard Joystick    | Tri-state port selector (Off / Port 1 / Port 2) and Numpad / WASD layout |
| Auto-save INI on Quit| Save `c64m.ini` automatically when quitting     |

The Keyboard Joystick port selector matches the runtime **Shift+Opt+1** /
**Shift+Opt+2** assignment; either place can change the active port. The layout can
only be changed here. Applying the dialog takes effect immediately.

### Paths

The Paths tab holds the default folder the file browser remembers per browse type.
Each field starts empty (the browser then opens at the shell's current directory) and
updates as you pick files. Paths are shown and stored relative to the INI file's
directory, and each row has a **[...]** button that opens a folder picker:

| Field     | Used by                                                        |
|-----------|----------------------------------------------------------------|
| assembler | Select Assembler Source                                        |
| disk      | Mount / Add Disk Image (drives 8 and 9 share this)             |
| program   | Load PRG/BAS, and Load/Save Binary with no Basic checkbox      |
| basic     | Load/Save Binary with **Basic Program** ticked                 |
| text      | Load/Save Binary with **Basic Text** ticked                    |
| snapshot  | Save/Load State — and the quicksave folder (Shift+Opt+> / <)   |

Edits here take effect on the next browse immediately. The folder picker's **[Use This
Folder]** button selects the folder currently shown. **[Save Paths Only]** rewrites just
these paths into the named INI file, leaving every other setting untouched; it is a
silent no-op if no INI file is set.

### INI File

The INI file path is shown in the Configure dialog with a **Browse...** button. Changing
the path prompts to parse the selected file immediately. Save-on-quit behavior is
controlled by the **Auto-save INI on Quit** checkbox.

**[OK]** applies all changes immediately. **[Cancel]** discards them.

## INI Files

c64m reads `c64m.ini` from the current directory by default. Use `--inifile <path>` to
load a different file, or `--noini` to skip loading entirely.

The `[Video] standard` setting can be overridden for a single launch with `--video PAL`,
`--video NTSC`, `-P`, or `-N`.

All section names are case-insensitive. Comment lines start with `#`. Saving from the
emulator removes comments.

### [config]

| Key               | Value                                                  |
|-------------------|--------------------------------------------------------|
| `Save`            | `yes` -- save INI on quit                               |
| `scroll_wheel_lines` | Integer; lines scrolled per wheel click             |
| `symbol_files`    | Comma-separated list of symbol file paths              |
| `turbo_speeds`    | Comma-separated turbo multipliers, e.g. `2,4,8,16`    |

### [Video]

| Key              | Value                                                    |
|------------------|----------------------------------------------------------|
| `standard`       | `PAL` or `NTSC` (default `NTSC`)                        |
| `display_width`  | Integer; internal display width in pixels               |
| `display_height` | Integer; internal display height in pixels              |
| `integer_scale`  | `1` -- use integer-only scaling                          |
| `aspect_correct` | `1` -- preserve pixel aspect ratio (default on)          |
| `filter`         | `nearest` or `linear` (default `nearest`)               |

### [input]

| Key                        | Value                                               |
|----------------------------|-----------------------------------------------------|
| `keyboard_joystick_layout` | `numpad` or `wasd` (default `numpad`)               |
| `keyboard_joystick_port`   | `0` (disabled), `1`, or `2` (default `0`)           |

The port can also be set for one launch with `--kbdjoy <0|1|2>`, and the layout with
`--kbdjoy-layout <numpad|wasd>`.

### [browse]

Default folders the file browser remembers per browse type (see the Configure
dialog's Paths tab). Any missing key defaults to the shell's current directory.

| Key         | Used by                                                   |
|-------------|-----------------------------------------------------------|
| `assembler` | Select Assembler Source                                   |
| `disk`      | Mount / Add Disk Image (drives 8 and 9)                   |
| `program`   | Load PRG/BAS and Load/Save Binary with no Basic checkbox  |
| `basic`     | Load/Save Binary with Basic Program ticked                |
| `text`      | Load/Save Binary with Basic Text ticked                   |
| `snapshot`  | Save/Load State and the quicksave folder                  |

The legacy `[state] quicksave_folder` key is read once and migrated into
`[browse] snapshot` if the latter is absent.

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
| `1541`      | Path to combined 1541 DOS ROM (16384 bytes) |

If no ROM paths are specified, c64m searches for files named `basic`, `kernal`,
`character`, `system`, and `1541` (with any extension) in `.`, `rom/`, and `roms/`.

### [disk]

Each key is the device number. The value is a comma-separated list of D64 image paths.
Paths may be absolute or relative to the directory containing the INI file.

| Key | Value                                                            |
|-----|------------------------------------------------------------------|
| `8` | D64 image or comma-separated list of images for device 8        |
| `9` | D64 image or comma-separated list of images for device 9        |
| `8_writable` | Parallel `0`/`1` list for device 8 images; omitted means read-only |
| `9_writable` | Parallel `0`/`1` list for device 9 images; omitted means read-only |
| `emulate_1541` | `true`/`false`; when true and a 1541 ROM is loaded, route disk LOADs through real IEC/1541 emulation |

Example — single disk:

```
[disk]
8=./games/galencia.d64
```

Example — multi-disk queue for a multi-part game:

```
[disk]
8=./games/tron_1.d64,./games/tron_2.d64,./games/tron_3.d64
```

The first image in the list is mounted at startup. The current position within the queue
is not saved; launching the emulator always starts from the first image.

Example — writable scratch disk:

```
[disk]
8=./disks/scratch.d64
8_writable=1
```

### [assembler]

Persists the Assembler tab state. All keys are optional; absent keys restore to their
defaults on next launch.

| Key              | Value                                               | Default |
|------------------|-----------------------------------------------------|---------|
| `file`           | Path to the root source file (relative or absolute) | —       |
| `address`        | Hex load/assembly origin, e.g. `8000`               | `8000`  |
| `run_address`    | Hex run address, e.g. `8000`                        | `8000`  |
| `auto_run`       | `yes` / `no` — Jump to run address after assembling | `no`    |
| `reset`          | `yes` / `no` — Reset C64 before assembling          | `yes`   |
| `rearm_oneshots` | `yes` / `no` — Rearm one-shot breakpoints before assembling | `no` |

### [DEBUG]

Breakpoints are stored as repeated `break` keys. Entries are not required to be unique
at the key level; a numeric suffix is added automatically when the emulator saves
multiple breakpoints.

Each entry has the form:

```
break.<suffix> = <address[-address]>[,access][,mapping][,actions][,count=N][,reset=N]
```

**Address and access:**

| Part              | Meaning                                           |
|-------------------|---------------------------------------------------|
| `address`         | Hex address, e.g. `C000` or `$C000`              |
| `-address`        | Optional range end                                |
| `execute`         | Execute breakpoint                                |
| `read`            | Read-access breakpoint                            |
| `write`           | Write-access breakpoint                           |
| `access`          | Read or write                                     |
| `map` / `rom` / `ram` | Mapping filter                              |

**Actions:**

| Token              | Meaning                                                              |
|--------------------|----------------------------------------------------------------------|
| `break`            | Pause execution                                                      |
| `fast`             | Switch to maximum turbo speed                                        |
| `slow`             | Restore normal paced speed                                           |
| `troff`            | Disable execution trace                                              |
| `tron`             | Enable execution trace; writes to `trace.log`                        |
| `tron=path`        | Enable execution trace; writes to `path`                             |
| `swap=+N`          | Advance device 8 disk queue forward N steps (wraps)                  |
| `swap=-N`          | Advance device 8 disk queue backward N steps (wraps)                 |
| `swap=N`           | Mount the Nth disk in the device 8 queue, 1-based (wraps)            |
| `swap` or `swap=0` | Swap action present but no-op                                        |
| `type=text`        | Inject text as C64 keystrokes; text uses the input-encoding format (see **Type text format** under **Breakpoints**) |
| `count=N`          | Fire on the Nth hit                                                  |
| `reset=N`          | Repeat interval after firing; `1` = every hit (default), `N>1` = every Nth hit, `0` = auto-disable after firing |

Examples:
```
break.C000 = execute,map,break
break.D000-D3FF = write,map,fast
break.C100 = execute,map,break,count=10,reset=2
break.E000 = execute,map,tron=my_trace.log
break.C000.1 = execute,map,swap=+1
break.E38B = execute,map,type=load\x22*\x22\x2c8\x2c1\[RT]
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
| **F11**             | Step over JSR                                         |
| **F12**         | Run (resume execution)                                     |
| **Shift+F12**   | Run to the cursor address in the Disassembly view          |
| **Opt+T**       | Cycle turbo speed                                          |
| **Opt+Tab**     | Cycle active view: C64 -> Disassembly -> Misc -> Memory    |
| **Shift+Opt+Tab** | Cycle active view in reverse                            |
| **Opt+1**       | Map gamepad to joystick port 1                             |
| **Opt+2**       | Map gamepad to joystick port 2 (default)                   |
| **Shift+Opt+1** | Assign the keyboard joystick to port 1 (press again to disable) |
| **Shift+Opt+2** | Assign the keyboard joystick to port 2 (press again to disable) |
| **Shift+Opt+0** | Disable the keyboard joystick on any port                       |
| **Shift+Opt+>** | Quicksave state to the snapshot folder (Configure -> Paths)     |
| **Shift+Opt+<** | Quickload the newest state from the snapshot folder             |
| **Cmd+Q**       | Quit (macOS)                                               |

### Paste and Clipboard

| Key                | Action                                                  |
|--------------------|---------------------------------------------------------|
| **Opt+Ins**        | Paste clipboard as timed C64 keystrokes (~40 ms per key) |
| **Shift+Opt+Ins**  | Paste clipboard text via the input-encoding parser (same format as the Type breakpoint action) |

Timed paste (**Opt+Ins**) scales with the active turbo multiplier. Parser-based paste
(**Shift+Opt+Ins**) supports named keys, PETSCII escapes, matrix addresses, and joystick
events in addition to literal text; see **Type text format** under **Breakpoints**.

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

## Remote

c64m has an opt-in localhost TCP control port for remote debugging and automation. It is
disabled by default. Start it with:

```sh
./c64m --control-port 6510
```

For automation without a visible window or host audio device, use headless mode:

```sh
./c64m --headless --control-port 6510
```

The server always binds to `127.0.0.1`. It accepts one client at a time. The socket
thread performs network I/O only; runtime commands and snapshot requests are dispatched
by the main loop, so remote control follows the same thread-ownership rules as the GUI
debugger. The current protocol name is `C64M/1`.

### Quick Start

A remote session is line-oriented. Each request starts with a decimal request id, then a
command, then command arguments:

```text
1 ping
2 pause
3 get-cpu
4 get-memory $0400 64 map
5 quit-client
```

Responses begin with the same id:

```text
1 ok
2 ok accepted=1
3 ok pc=FD84 a=00 x=00 y=2F sp=FD p=25 cycles=1712136
4 data memory 64 addr=0400 length=64 mode=0
<64 raw bytes>
5 ok
```

`quit-client` closes the TCP client connection. It does not quit the emulator process.
Headless automation should terminate the process externally after the final client
command.

### Request Format

Requests are ASCII lines terminated by `\n`:

```text
<id> <command> [arguments...]\n
```

`<id>` is a decimal unsigned integer chosen by the client. c64m does not require ids to
be sequential, but sequential ids make logs easier to read. Commands are lower-case
words with hyphens. Hex addresses may be written as `0xC000` or `$C000`; decimal counts
and timeouts are written without a prefix.

Most commands are single-line. Two paste commands carry a length-prefixed payload:

```text
<id> paste-text-data <byte_count>\n
<byte_count raw bytes>\n

<id> paste-events-data <byte_count>\n
<byte_count raw bytes>\n
```

The payload may contain arbitrary bytes except that the framing still requires exactly
one trailing newline after the payload. Payload size is limited to 4096 bytes.

### Response Format

Text responses are ASCII lines terminated by `\n`:

```text
<id> ok [metadata...]\n
<id> error <code> <message>\n
```

Binary responses use a header line, exactly `<byte_count>` raw payload bytes, and one
trailing newline:

```text
<id> data <type> <byte_count> [metadata...]\n
<byte_count raw bytes>
\n
```

The client should parse the byte count from the `data` header and then read exactly that
many bytes before consuming the trailing newline. Do not treat binary payloads as
newline-delimited text.

Only one deferred response can be active at a time. Commands that wait for a runtime
event, fresh snapshot, breakpoint mutation, disk status, or wait condition may return:

```text
<id> error busy deferred-response-active
```

when another deferred command is still pending. Deferred commands time out with:

```text
<id> error timeout deferred response timed out
```

### Connection and Introspection

| Command | Response |
|---------|----------|
| `hello` | `ok name=c64m protocol=C64M/1` |
| `version` | `ok protocol=C64M/1 app=0.1.0` |
| `capabilities` | Space-separated capability names |
| `ping` | `ok` |
| `quit-client` | `ok`, then the server closes the client connection |

### Execution Control

Execution commands return after the command is accepted by the runtime, not after the
machine reaches a new state. Use `wait-*` commands when a script needs to synchronize.

| Command | Meaning |
|---------|---------|
| `reset` | Reset the emulated machine |
| `run` | Resume execution |
| `pause` | Pause execution |
| `step-cycle` | Execute one machine cycle |
| `step-instruction` | Execute one CPU instruction |
| `step-over` | Step over a JSR |
| `step-out` | Run until the current subroutine returns |
| `run-cycles <count>` | Run for a positive cycle count |
| `run-instructions <count>` | Run for a positive instruction count |
| `run-to <addr>` | Run until the PC reaches a 16-bit address |

Accepted execution commands respond:

```text
<id> ok accepted=1
```

### State and Snapshots

| Command | Response |
|---------|----------|
| `get-state` | Text state summary: runtime state, CPU availability, frame, cycle, stop reason, turbo |
| `get-cpu` | Text CPU snapshot |
| `get-frame [format=argb8888]` | Binary frame snapshot |
| `get-memory <addr> <length> <mode>` | Binary memory snapshot |
| `get-debug-memory [write-history=0|1]` | Binary debugger memory snapshot |
| `get-call-stack` | Text call-stack summary |

`get-state` is answered from the main loop's cached frontend debug state. `get-cpu`,
`get-memory`, and `get-call-stack` request fresh runtime snapshots and complete later.
`get-frame` uses the latest completed frame cached by the main loop, or requests one if
no cached frame exists yet. In headless mode the main loop still polls frame snapshots
for `get-frame` and `wait-frame`.

Memory modes:

| Mode | Meaning |
|------|---------|
| `map` | CPU-visible memory after current banking |
| `ram` | Physical RAM |
| `rom` | Physical ROM where available, RAM elsewhere |

`get-memory` length is limited to 1..1024 bytes.

Frame payloads are ARGB8888 pixels. PAL frames are 384x272; NTSC frames are 384x263.
The frame metadata is:

```text
<id> data frame <bytes> width=384 height=<272|263> stride=1536 format=argb8888 frame=<n> cycle=<cycle>
```

`<bytes>` is `height * stride`. `stride` is bytes per row.

`get-debug-memory` returns concatenated 64 K buffers:

```text
map bytes, then ram bytes, then rom bytes
```

Without write history the payload is `196608` bytes. With `write-history=1`, an
additional write-history payload may be included when available; the header metadata
records whether write history is present.

### Waiting

Wait commands are deferred responses checked by the main loop after normal runtime
polling. They never block SDL rendering or event processing.

| Command | Completes when |
|---------|----------------|
| `wait-paused [timeout_ms]` | Runtime state is paused |
| `wait-running [timeout_ms]` | Runtime state is running |
| `wait-frame <frame_delta> [timeout_ms]` | The frame counter advances by at least `frame_delta` |
| `wait-event <event_name> [timeout_ms]` | A named runtime event is observed |

The default timeout is 2000 ms. Explicit timeouts must be 1..600000 ms.

Useful event names include `running`, `paused`, `reset-complete`, `step-complete`,
`run-complete`, `frame`, `breakpoints`, `disk-status`, `call-stack`, `debug-memory`,
`assemble-complete`, `assemble-error`, `disk-swap`, `started`, `stopped`, and `error`.

Example synchronization:

```text
1 reset
2 run
3 wait-running 2000
4 wait-frame 10 5000
5 get-frame
6 pause
7 wait-paused 2000
8 get-state
```

### Input and Paste

| Command | Meaning |
|---------|---------|
| `key-down <key>` | Press a C64 key |
| `key-up <key>` | Release a C64 key |
| `restore` | Trigger RESTORE/NMI |
| `joystick <port> <mask>` | Set joystick state for port 1 or 2 |
| `paste-text <text>` | Paste literal text using timed C64 keystrokes |
| `paste-events <text>` | Parse and paste input-encoding events |
| `paste-text-data <byte_count>` | Payload form of `paste-text` |
| `paste-events-data <byte_count>` | Payload form of `paste-events` |

Key names are lower-case C64 key names: `a` through `z`, `0` through `9`, `space`,
`return`, `delete`, `left-shift`, `right-shift`, `plus`, `minus`, `asterisk`, `equals`,
`colon`, `semicolon`, `comma`, `period`, `slash`, `at`, `cursor-right`, `cursor-down`,
`home`, `run-stop`, `control`, `commodore`, `left-arrow`, `up-arrow`, `pound`, `f1`,
`f3`, `f5`, and `f7`.

Joystick masks use the C64 joystick bit layout:

| Bit | Value | Meaning |
|-----|-------|---------|
| 0 | `1` | Up |
| 1 | `2` | Down |
| 2 | `4` | Left |
| 3 | `8` | Right |
| 4 | `16` | Fire |

For parser-based paste, use the same input-encoding format as the Type breakpoint
action.

### Files and Disks

| Command | Meaning |
|---------|---------|
| `load-prg <path>` | Load a PRG file using the file's two-byte load address |
| `load-bin <path> <addr> <use_file_addr> <reset_first> <is_basic>` | Load a binary file with explicit flags |
| `save-bin <path> <start> <end> <write_file_addr> <is_basic>` | Save a host file from memory |
| `mount-d64 <device> <path>` | Mount a D64 on device 8 or 9 |
| `unmount-disk <device>` | Unmount device 8 or 9 |
| `get-disk-status <device>` | Return mounted/status information |

Boolean flags accept `0`, `1`, `false`, or `true`. Paths may contain spaces. For
`load-prg` and `mount-d64`, the path is the rest of the line after the fixed arguments.
For `load-bin` and `save-bin`, the parser treats the final fixed address/boolean
arguments as command arguments and everything before them as the path. `save-bin` can
overwrite host files, so use it with the same care as any other local file-writing
command.

### Breakpoints

| Command | Meaning |
|---------|---------|
| `break-exec <addr>` | Create an enabled execute breakpoint with the default break action |
| `break-list` | Request the breakpoint list |
| `get-breakpoints` | Alias for `break-list` |
| `break-clear <id>` | Clear one breakpoint |
| `break-clear-all` | Clear all breakpoints |
| `break-enable <id> <enabled>` | Enable or disable one breakpoint |
| `break-create <definition>` | Create a breakpoint from an explicit definition |
| `break-update <id> <definition>` | Replace an existing breakpoint definition |
| `rearm-oneshots` | Rearm one-shot breakpoints |

Definition syntax:

```text
exec <addr> [enabled=0|1] [end=<addr>] [actions=<list>] [counter=N] [reset=N]
```

`actions` is a comma-separated list containing one or more of: `break`, `fast`, `slow`,
`tron`, `troff`, `type`, and `swap`.

Breakpoint list responses are binary-framed `data breakpoints` responses whose payload
is newline-separated ASCII text:

```text
<id> data breakpoints <byte_count> count=<count>
id=1 enabled=1 start=C000 end=C000 has_end=0 access=1 mapping=0 actions=1 use_counter=0 hits=0 initial=0 reset=1 counter=0
```

The payload is text even though it uses `data` framing, so clients should still honor
the byte count.

### Assembler and Symbols

The control port can assemble a source file into the running machine and look up the
labels that result. This drives the same assembler and settings as the Misc -> Assembler
tab, so a script can build code, find where a routine landed, and set a breakpoint on it.

| Command | Meaning |
|---------|---------|
| `assemble [address=<hex>] [run-address=<hex>] [auto-run=0\|1] [reset=0\|1] <source-path>` | Assemble a source file into the machine |
| `find-symbol <name>` | Resolve a label from the most recent assembly |

The optional `key=value` settings precede the source path and may appear in any order.
Any token that is not a recognized option begins the source path, which runs to the end
of the line and may contain spaces. The settings mirror the Assembler tab, and their
defaults are the same:

| Setting | Default | Meaning |
|---------|---------|---------|
| `address` | `$8000` | Address the code is assembled to |
| `run-address` | same as `address` | PC used when `auto-run` is on |
| `auto-run` | `0` | Run the assembled code after a successful build |
| `reset` | `1` | Reset the machine and return to BASIC before assembling |

Before assembling, the control port pauses the machine so the result lands in a defined
state. The assembler's own reset and auto-run handling then applies: a `reset=1` assemble
resets, runs to BASIC, assembles, and resumes; `auto-run=1` sets the PC to `run-address`
and resumes.

`assemble` is a deferred (asynchronous) command. On success the reply carries the
assembly address:

```text
1 assemble reset=0 address=$c000 samples/test1.asm
1 ok address=$C000
```

On failure the reply is an error whose message is the assembler diagnostic:

```text
2 assemble reset=0 badsource.asm
2 error assemble-error File: badsource.asm L:00001 C:012: Unexpected token after expression
```

A successful assembly publishes the resolved symbol table. `find-symbol` resolves a label
from that table by exact name:

```text
3 find-symbol loop
3 ok address=$C004 name=loop
```

If the name is not present the reply is `error not-found`. If no assembly (or symbol file)
has published symbols yet, the reply is `error not-ready`. Symbols are also published when
a symbol file is loaded, and in the GUI a successful remote assembly refreshes the
debugger's Disasm and Symbol Lookup views just as the Assembler tab does.

A typical automation sequence assembles, locates a routine, breakpoints it, and runs:

```text
1 assemble address=$c000 samples/test1.asm
2 find-symbol loop
3 break-exec $C004
4 run
5 wait-paused 5000
```

### Example Python Client

This small client sends a command and handles both text and binary responses:

```python
import socket

def read_line(sock):
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(1)
        if not chunk:
            raise EOFError("connection closed")
        data += chunk
    return data.decode("ascii").rstrip("\n")

def send(sock, line):
    sock.sendall((line + "\n").encode("ascii"))
    header = read_line(sock)
    parts = header.split()
    payload = b""
    if len(parts) >= 4 and parts[1] == "data":
        byte_count = int(parts[3])
        while len(payload) < byte_count:
            payload += sock.recv(byte_count - len(payload))
        if sock.recv(1) != b"\n":
            raise RuntimeError("bad data terminator")
    return header, payload

with socket.create_connection(("127.0.0.1", 6510)) as s:
    print(send(s, "1 ping")[0])
    print(send(s, "2 pause")[0])
    print(send(s, "3 get-cpu")[0])
    header, frame = send(s, "4 get-frame")
    print(header, len(frame))
    print(send(s, "5 quit-client")[0])
```

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

The memory view's `1541 Map 8` and `1541 Map 9` modes use the same rule for the drive:
drive RAM, ROM, and VIA registers are copied through side-effect-safe debug reads. Reading
the memory view does not acknowledge VIA interrupt flags or otherwise act like a live
drive CPU bus read. Unmapped drive addresses are marked unavailable rather than treated
as writable C64 memory.

For debugging writes, the machine also keeps a 64 K write-history table, one 64-bit value
per C64 address. Normal CPU opcode writes update the addressed entry by shifting the old
value left 16 bits and appending the opcode PC in the low 16 bits. This retains the last
four instruction PCs that wrote to the address. Loader injection, debugger pokes, reset
initialization, and other non-opcode writes are not recorded in the first version of the
feature.

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
23-bit LFSR), TEST bit, ADSR envelope with a fractional double accumulator and
clock-parameterized rate tables (PAL and NTSC constants selected at `sid_init`), and gate
control. Voice 3 oscillator (phase bits 23-16) and envelope are
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
combined-waveform analog blending, ring modulation and oscillator sync, and paddle input
beyond the not-connected policy.

### Disk

Device 8 and device 9 each hold an independent D64 image, read-only by default and
optionally marked writable. In the default
compatibility mode, the KERNAL LOAD trap intercepts $FFD5 for devices 8/9 only; all
other devices fall through to ROM. `LOAD "NAME",8` loads at the BASIC start pointer and
updates end-of-program pointers. `LOAD "NAME",8,1` loads at the embedded PRG load
address. `LOAD "$",8` synthesises a tokenized BASIC directory listing with the disk
title, file list, and blocks-free line. Filename matching supports exact names, `*`
prefix wildcard, `?` single-character wildcard, and bare `*` for the first PRG. The
parser validates chain links and guards against sector loops and out-of-range
references.

When `[disk] emulate_1541=1` is set and a combined 16 K 1541 DOS ROM is loaded through
`[roms] 1541`, the trap is disabled for devices 8/9 and KERNAL LOAD proceeds over the
emulated IEC bus. The 1541 model runs the drive 6502, two VIA 6522s, the standard DOS
2.6 ROM serial handlers, ATN/CLK/DATA open-collector signaling, ATN acknowledge, and
ROM-level D64 sector reads. The disk-controller VIA mechanics are intentionally
abstracted: ROM READ/SEARCH jobs are satisfied from the mounted D64 image rather than
from a simulated stepper motor, GCR stream, or SYNC signal. This supports standard
KERNAL disk loads and disk autorun. Writes to a writable image are handled at the same
job-dispatch altitude: SAVE, sequential/relative file writes, and BAM/directory updates
persist through the drive's WRITE job, and the DOS command channel (scratch, rename,
validate, initialize, and a FORMT-job-intercepted format) and the `OPEN 15,8,15`
error/status channel work through the real ROM. When the 1541 ROM is absent, SAVE falls
back to the compatibility KERNAL trap. Media-level write fidelity (GCR/SYNC/head/motor),
cross-drive copy, block/memory-execute commands, and unvalidated fast-loader mechanics
remain out of scope.

### Joystick

Port 1 is emulated via CIA #1 Port B bits 0-4. Port 2 via CIA #1 Port A bits 0-4. A
connected gamepad defaults to port 2; Opt+1 and Opt+2 reassign it.

The host keyboard can also act as a joystick. It is disabled by default; assign it to a
port with **Shift+Opt+1** / **Shift+Opt+2**, the Keyboard Joystick control in the
Configure dialog, or `--kbdjoy`. A gamepad and the keyboard may share the same port,
in which case their directions and fire are combined.

When a `.c64state` snapshot is saved, the active keyboard joystick port and layout are
stored with it and restored when the snapshot is loaded.

Two layouts are available:

| Layout   | Directions             | Diagonals            | Fire   |
|----------|------------------------|----------------------|--------|
| `numpad` | Keypad 8 / 2 / 4 / 6   | Keypad 7 / 9 / 1 / 3 | Keypad 0 |
| `wasd`   | W / S / A / D          | (hold two keys)      | Space  |

The numpad keys are not C64 keys, so the `numpad` layout never interferes with typing.
The `wasd` keys are C64 keys, so while the keyboard joystick is assigned to a port those
keys drive the joystick instead of reaching the C64; they type normally when the keyboard
joystick is disabled or when a debugger view has keyboard focus. The `numpad` layout
follows the keypad key codes, so it requires Num Lock to be on. The port and layout are
saved in the `[input]` INI section.

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
- `tiny-regex-c/re.c`, `tiny-regex-c/re.h`
  - Upstream: <https://github.com/kokke/tiny-regex-c>
  - License: The Unlicense (public domain)

## Versions

29 Jun 2026
:   The 77 hours into it version.  Not a release yet.
