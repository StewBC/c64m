<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MFDATA.md
# Host File Load and Save UI

## Purpose

This document defines host file load/save behavior for the Machine tab. It is separate from the D64 disk path.

The D64 disk phases are read-only disk-image work. This file covers direct host-file operations that read or write emulator RAM using user-selected host files.

## Relationship to disk work

The Machine tab target layout is:

```text
Disks
[8] [-] {Name}
[9] [-] {Name}

Load [basic/prg] [bin]
Save [basic] [bin]
```

The `Disks` controls are covered by `C64MDISKPHASE_F.md` and optionally `C64MDISKPHASE_G.md`.

This document owns:

```text
Load [basic/prg] [bin]
Save [basic] [bin]
```

Host load/save must not be confused with saving to a mounted D64 image. These operations use host files directly.

## Architecture rules

Frontend:

- Owns the UI and host OS file dialogs.
- Sends copied runtime commands.
- Never reads or writes live machine memory directly.

Runtime:

- Receives copied commands.
- Performs file I/O on the runtime thread or via existing safe project patterns.
- Mutates live machine memory only on the runtime thread.
- Publishes copied success/error events.

Machine:

- Owns live RAM and CPU state.

Do not introduce frontend-to-machine access.

## File picker policy

Use a broad/trusting host OS file selector for loads. Users may have files with unusual names or extensions. Do not force the user to rename files just to select them.

For saves, the file dialog may suggest useful extensions, but it must allow user-selected names.

## Load [basic/prg]

This is the existing PRG loader button/behavior, moved or preserved in the Machine tab layout.

Requirements:

- Preserve current PRG loader behavior.
- Keep the existing reset-before-load and run-state contract unless the current code has changed and STATUS.md says otherwise.
- Preserve collection PRG behavior that waits until BASIC warm-start/READY entry before injecting PRG bytes, if that behavior is still current.
- Continue supporting files with any extension if the bytes are PRG-like.

The label may be `basic/prg` to communicate that this is the current PRG/BASIC loader path.

## Load [bin]

Open a dialog:

```text
Name <edit box> [Browse]
File address [X]
Address <edit box>
[Okay]
```

Behavior:

- `Browse` opens the broad/trusting host OS file selector.
- The selected path or basename goes into the `Name` edit box.
- `File address` defaults checked.
- When `File address` is checked:
  - The Address edit box is disabled.
  - The first two bytes of the file are read as little-endian load address.
  - The remaining file bytes are loaded starting at that address, like a PRG payload.
  - Files shorter than two bytes fail safely.
- When `File address` is unchecked:
  - The Address edit box is enabled.
  - Address must be exactly four hexadecimal digits in the form `XXXX`.
  - The full file contents are loaded starting at that address.
- The loaded range must fit in `$0000-$FFFF` without wrap.
- On failure, do not modify unrelated memory.

Open question for the implementation agent to resolve from current UI conventions: whether `Name` stores a full path, a display basename plus hidden full path, or an editable path string. The command sent to runtime must contain enough information to open the host file.

## Save [basic]

Save the current BASIC program to a host file with a two-byte PRG header.

Requirements:

- Determine the current BASIC program start and end from the emulator's BASIC/KERNAL state or the project's existing BASIC/PRG loader conventions.
- Default start is expected to be `$0801` on a standard C64 unless current machine state says otherwise.
- Write the two-byte little-endian start address first.
- Then write the BASIC program bytes from start through the current BASIC program end range as defined by the implementation.
- Do not save unrelated RAM beyond the current BASIC program.
- Use a save dialog that allows user-selected names/extensions.

The implementation agent must inspect current BASIC pointer handling before choosing exact variables.

## Save [bin]

Open a dialog:

```text
Name <edit box> [Browse]
File address [X]
Start Address <edit box>
End Address <edit box>
[Okay]
```

Behavior:

- `Browse` opens a host save dialog or path selector consistent with platform conventions.
- `File address` defaults checked.
- Start Address and End Address are mandatory.
- Both addresses must be exactly four hexadecimal digits in the form `XXXX`.
- Validation is `start <= end`.
- Bytes saved are RAM from start through end inclusive.
- If `File address` is checked, write the two-byte little-endian start address before the RAM bytes.
- If `File address` is unchecked, write only the RAM bytes.
- The command must read live RAM only on the runtime thread and write a host file safely.

## Error handling

Required safe failures:

- User cancels dialog: no state change, no error dialog required.
- Missing/invalid path.
- Host file open/read/write failure.
- Load file shorter than two bytes when file-address mode is checked.
- Invalid hex address.
- Load range overflow.
- Save start greater than end.
- Save range outside C64 RAM.
- Allocation failure.

Use a copied runtime event or existing frontend error mechanism to report actionable errors.

## Tests

Required tests:

- Existing Load [basic/prg] behavior still works.
- Load [bin] with file-address checked loads bytes after the first two header bytes to the embedded address.
- Load [bin] with file-address unchecked loads the full file to the entered address.
- Load [bin] rejects files shorter than two bytes when file-address mode is checked.
- Load [bin] rejects invalid hex address text.
- Load [bin] rejects address overflow/wrap.
- Save [basic] writes a two-byte start header and current BASIC program bytes.
- Save [bin] with file-address checked writes two-byte start header plus inclusive RAM range.
- Save [bin] with file-address unchecked writes only the inclusive RAM range.
- Save [bin] allows one-byte saves where `start == end`.
- Save [bin] rejects `start > end`.
- Frontend dialogs send copied runtime commands only.
- Existing disk D64 behavior still works.
- Existing boot, keyboard, debugger, VIC bank, CIA, breakpoint, assembler, PRG loader, and disk tests continue to pass.

## Acceptance criteria

- Machine-tab host file load/save is usable without interfering with D64 mounting.
- File selectors trust user-selected filenames and extensions.
- Binary load/save address rules are implemented exactly.
- BASIC save writes a valid PRG-style header.
- All live RAM access respects runtime/machine thread ownership.
- Existing tests continue to pass.

## STATUS.md update

At the end of this work, update `md-files/STATUS.md` with:

- Host file data UI complete.
- Load [basic/prg] status.
- Load [bin] address-mode behavior.
- Save [basic] behavior and BASIC pointer policy.
- Save [bin] inclusive range behavior.
- Tests added.
- Remaining gaps, if any.
