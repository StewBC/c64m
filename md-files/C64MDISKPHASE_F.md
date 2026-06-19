<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_F.md
# Machine-Tab Disk UI, Status, and Validation

## Goal

Add the visible Machine-tab disk controls for mounting and unmounting D64 images, and validate the full device-8 read-only disk path with real fixtures.

This phase should make the feature usable from the frontend while preserving runtime/thread ownership rules.

## Scope

Implement the Machine-tab disk section:

```text
Disks
[8] [-] {Name}
[9] [-] {Name}
```

For this phase:

- `[8]` opens a host OS file selector.
- The selector should trust the user and allow broad/all-file selection rather than forcing a `.d64` extension filter.
- Choosing a file sends a copied runtime mount command for device 8.
- `[-]` unmounts device 8.
- `{Name}` is a changeable/display label showing copied mounted status, such as the host basename or disk title.
- `[9]` and its unmount control may be disabled, hidden, or present-but-not-implemented unless device 9 was already implemented trivially. The UI should not imply working device 9 unless it actually works.

Also add validation coverage across parser, mount state, KERNAL PRG load, directory load, wildcard/error behavior, and UI status.

## Non-goals

Do not implement host file load/save buttons here. Those belong in `C64MFDATA.md`.

Do not implement:

- D64 writes.
- SAVE to mounted disk.
- Device 9 unless explicitly included or already trivial.
- 1541/IEC/fast-loader support.
- File picker extension restrictions that block unusual but valid user-selected files.

## UI layout guidance

The broader Machine-tab target layout is:

```text
Disks
[8] [-] {Name}
[9] [-] {Name}

Load [basic/prg] [bin]
Save [basic] [bin]
```

This phase owns only the Disks portion unless the implementation agent and maintainer explicitly decide to combine with `C64MFDATA.md`. Prefer keeping host file load/save separate.

The existing Machine tab may need a slightly different layout. Keep it compact and consistent with current Nuklear style. Do not introduce frontend-to-machine access.

## Runtime/frontend rules

Frontend:

- Owns UI interactions and host file picker.
- Sends copied runtime commands.
- Displays copied disk status.
- Never reads live machine disk state.

Runtime:

- Receives commands.
- Performs mount/unmount on the runtime thread.
- Publishes copied status.

Machine:

- Owns live drive state.
- Serves KERNAL trap behavior.

## Status display

The label should prefer a human-usable name:

1. Mounted disk display name or disk title if available and safely converted.
2. Host basename.
3. `No disk` or equivalent for unmounted state.
4. Error text/status for failed mount if useful.

Keep strings copied and bounded.

## Validation fixtures

Use:

```text
assets/disks/ODELLLAK.D64
assets/disks/blank.d64
```

`ODELLLAK.D64` should validate realistic directory contents and PRG loads. `blank.d64` should validate empty formatted disk behavior.

Generated fixtures remain useful for malformed-chain tests.

## Tests

Required tests:

- Frontend mount intent for `[8]` sends a runtime mount command with copied path.
- Frontend unmount intent sends runtime unmount command.
- Copied mounted status updates the Machine-tab label.
- Broad file selector policy is documented or represented in platform picker options where testable.
- Mounting `ODELLLAK.D64` and using BASIC `LOAD "$",8` produces a listable directory.
- Mounting `ODELLLAK.D64` and loading `MENU1` works for both `LOAD "MENU1",8` and `LOAD "MENU1",8,1`.
- Mounting `blank.d64` produces a valid empty directory and fails safely for missing PRGs.
- Unmounting then loading from device 8 fails safely.
- Existing host PRG loader still works.
- Existing boot, keyboard, debugger, VIC bank, CIA, breakpoint, assembler, and runtime tests continue to pass.

Manual validation should include:

```text
LOAD "$",8
LIST
LOAD "MENU1",8
RUN
LOAD "MENU1",8,1
```

Document any command that cannot be manually validated because of fixture behavior.

## Acceptance criteria

- Device 8 can be mounted and unmounted from the Machine tab.
- The UI displays copied disk status only.
- The file picker does not force users to rename unusual files just to select them.
- Full read-only device-8 D64 path works from UI to BASIC-visible load behavior.
- Device 9 is not falsely advertised as functional if deferred.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- Disk Phase F complete.
- Machine-tab disk UI behavior.
- Fixture/manual validation results.
- Whether device 9 is deferred or implemented.
- Remaining gaps: host load/save if not done, D64 writes, SAVE to disk, error channel, 1541/IEC/fast loaders.
