# C64PHASE14.md

# Phase 14 - INI Configuration System and Configure Dialog

## Goal

Implement a tabbed INI configuration dialog similar in spirit to a2m.

The dialog edits a temporary copy of configuration state.

No changes are applied until the user presses OK.

Cancel discards all edits.

Some changes apply immediately after OK.
Some changes require a machine reboot after OK.

This phase also introduces INI file selection, INI save policy, and machine configuration management.

---

## Architecture Requirements

Follow AGENTS.md and MASTER.md.

Frontend owns:

- Configure dialog
- Dialog editing state
- Validation UI
- File picker UI

Runtime owns:

- Live machine
- Applying machine-affecting configuration
- Machine reboot/reset requests

Frontend must never directly modify live machine state.

Changes flow through runtime_client.

---

## Dialog Layout

Two tabs initially:

```text
+---------------------------+
| Machine | Emulator        |
+---------------------------+
|                           |
| tab content area          |
|                           |
+---------------------------+
| INI File: [path      ][..]|
|                           |
| [ ] Save INI on Quit      |
|                           |
| [ OK ] [ Cancel ]         |
+---------------------------+
```

The tab body corresponds to the blue area in the reference image.

The bottom area is global and visible regardless of selected tab.

---

## Configuration Model

Maintain two copies:

```c
config_state original;
config_state edited;
```

On dialog open:

```c
original = current_config;
edited   = original;
```

All UI edits modify only:

```c
edited
```

Nothing is applied until OK.

Cancel simply closes the dialog.

---

## Machine Tab

### Video Standard

Controls:

```text
(o) NTSC
(o) PAL
```

Configuration field:

```text
video_standard = ntsc|pal
```

Behavior:

Changing PAL/NTSC does not immediately affect the machine.

On OK:

```text
if changed:
    needs_reboot = true
```

---

## Emulator Tab

### Scroll Wheel Speed

Integer value.

Controls how many lines are moved by:

- Memory view
- Disassembly view

Apply immediately after OK.

No reboot required.

---

### Turbo Speeds

CSV list.

Example:

```text
2,4,8,16
```

Behavior:

First entry becomes active turbo speed.

Example:

```text
2,4,8
```

installs:

```text
current turbo = 2
available speeds = 2,4,8
```

Changing list does not preserve previous selection.

Apply immediately after OK.

No reboot required.

---

### Symbol Files

CSV list.

Stored as paths relative to the INI file location whenever possible.

Example:

```text
symbols/kernel.sym,symbols/basic.sym
```

UI:

```text
Symbol Files:
[ csv edit field             ]
[ Add... ]
```

Add button:

- opens picker
- appends selected path
- never removes paths

Removal:

- edit CSV manually

On OK:

```text
flush all symbols
load all listed files
force debugger view redraw
```

No reboot required.

---

### Auto-save INI on Quit

Persistent option.

INI key:

```ini
Save=yes
```

Absence means:

```ini
Save=no
```

UI text:

```text
[ ] Auto-save INI on Quit
```

Behavior:

Checked:

```ini
Save=yes
```

Unchecked:

```text
Save key removed
```

This is persistent.

---

## Global Controls

### INI File

Editable path field plus picker.

Example:

```text
INI File:
[ ./c64m.ini          ] [ ... ]
```

The picker selects a filename.

The edit field allows direct typing.

---

## One-Shot Save Flag

UI text:

```text
[ ] Save INI on Quit
```

This is NOT stored in the INI.

This is runtime-only.

Default:

```text
off
```

Meaning:

```text
Save configuration once on next exit.
```

---

## --nosaveini Handling

Command line:

```text
-!
--nosaveini
```

Has highest precedence.

Meaning:

```text
Never save any INI file.
```

When active:

Disable:

```text
Auto-save INI on Quit
Save INI on Quit
```

Saving is impossible.

---

## Save Decision Logic

Definitions:

```text
persistent_save = Save=yes
one_shot_save   = Save INI on Quit
nosaveini       = -!
```

Decision:

```text
save_on_exit =
    !nosaveini &&
    (persistent_save || one_shot_save)
```

---

## INI Filename Change Detection

Track:

```c
original_ini_path
edited_ini_path
```

When the user commits a new path:

- field loses focus
- Enter pressed
- picker returns

compare:

```c
edited_ini_path != original_ini_path
```

If different and saving is allowed:

```text
automatically check:
    Save INI on Quit
```

Reason:

User is explicitly selecting a destination INI.

---

## Existing INI File Workflow

When the newly selected filename already exists:

Show dialog:

```text
Parse selected INI file now?

[ Yes ] [ No ] [ Cancel ]
```

### Yes

```text
Load selected INI
Parse selected INI
Repopulate edited configuration
Keep new filename
```

### No

```text
Keep new filename
Keep current edited settings
```

Use case:

Clone current settings over another INI.

### Cancel

```text
Restore previous filename
Restore previous selection state
```

Act as if filename change never occurred.

---

## Apply Model

Nothing applies while editing.

Only OK commits changes.

Pseudo:

```c
if(OK)
{
    diff(original, edited);
    apply_changes();
}
```

---

## Change Classification

### Requires Reboot

```text
PAL / NTSC
```

Result:

```c
result.needs_reboot = true;
```

### Immediate Apply

```text
Scroll Wheel Speed
Turbo Speeds
Symbol Files
```

Apply after OK.

No reboot.

---

## Result Structure

Suggested:

```c
typedef struct config_apply_result
{
    bool accepted;
    bool needs_reboot;
    bool save_ini_on_quit;
} config_apply_result;
```

Dialog returns:

```c
accepted
```

and higher layers decide:

```text
reboot?
save?
reload symbols?
refresh views?
```

---

## Machine View Rename

Current tab:

```text
Programs
```

Rename to:

```text
Machine
```

Purpose:

Machine management.

Future contents:

```text
Mounted disks
Loaded cartridge
Loaded PRG
Machine configuration
```

Add button:

```text
Configure...
```

This opens the INI configuration dialog.

---

## Acceptance Criteria

- Machine and Emulator tabs exist.
- Global INI controls exist.
- Dialog edits temporary state only.
- Cancel discards all changes.
- OK computes diffs and commits.
- PAL/NTSC changes request reboot.
- Scroll wheel speed applies immediately after OK.
- Turbo list applies immediately after OK.
- Symbol files reload on OK.
- Auto-save INI on Quit maps to Save=yes.
- Save INI on Quit is runtime-only.
- --nosaveini disables saving controls.
- Changing INI filename auto-checks Save INI on Quit.
- Existing INI selection prompts Yes/No/Cancel.
- Programs tab renamed to Machine.
