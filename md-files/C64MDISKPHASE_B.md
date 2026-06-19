<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_B.md
# Runtime Mount Commands and Machine-Owned Drive 8 State

## Goal

Add safe mount/unmount plumbing for a read-only D64 image as device 8. The frontend selects a host file, the runtime thread parses it, and the live machine owns the mounted drive state used by later KERNAL-load phases.

This phase does not yet load from BASIC. It only proves that a disk can be mounted, replaced, unmounted, and reported to the frontend through copied status.

## Design decision

Use this ownership model:

```text
frontend file selection
    -> copied runtime command with host path
    -> runtime thread reads and parses D64 using tools-level parser
    -> runtime installs owned parsed disk state into machine drive slot 8
    -> runtime publishes copied disk-status snapshot for frontend display
```

The live mounted state belongs to the machine because the KERNAL trap will be machine behavior. Runtime commands mutate that state only on the runtime thread.

Do not let frontend read or mutate live machine disk state.

## Scope

Implement:

- Runtime command to mount a D64 path as device 8.
- Runtime command to unmount device 8.
- Runtime-side file loading and parser invocation.
- Machine-owned drive slot 8 state.
- Machine API for installing/replacing/clearing the parsed drive image on the runtime thread.
- Copied runtime/frontend disk-status snapshot.
- Tests for mount, replace, unmount, failed mount, and copied status.

## Non-goals

Do not implement:

- KERNAL LOAD trap.
- `LOAD "$",8`.
- `LOAD "NAME",8` or `LOAD "NAME",8,1`.
- Device 9, unless the data structure shape makes it trivial without extra behavior.
- Disk mutation or host save.
- UI polish beyond any minimal test/status path needed by existing code structure.

## Machine state

Add a machine-owned drive state that can represent at least:

```text
not mounted
mounted standard D64 image
last mount error/status for copied reporting if useful
```

The drive state should own all memory it needs. It must not point into frontend-owned buffers, runtime command buffers, or temporary parser allocations that will be freed.

Prefer drive-slot data structures that do not make later device 9 support awkward. For example, a small array keyed by device number or normalized slot can be used, even if only device 8 is active in this phase.

Do not expose live drive pointers outside machine/runtime-thread code.

## Runtime command behavior

Mount command:

- Copy the path in the frontend/runtime command boundary.
- On the runtime thread, read the file bytes.
- Parse using the Phase A tools-level parser.
- On success, install the parsed disk into machine device 8.
- On failure, preserve or clear the previous mounted disk deliberately. Prefer preserving the previous successful mount unless the UI command explicitly means replace-with-this-path. Document the choice in STATUS.md.
- Publish a copied status event/snapshot.

Unmount command:

- Clear machine drive slot 8.
- Free owned disk state.
- Publish copied status.

Replace behavior:

- Mounting a new valid disk frees/replaces the previous disk exactly once.
- Repeated mount/unmount cycles must not leak memory.

## Copied status

Frontend status should be copied and small. Suggested fields:

```text
device number
mounted yes/no
host display name or basename
disk title as ASCII/debug string if available
image kind, currently D64
last mount result
```

The status must not include live machine pointers or parser-owned internal pointers.

## File selection policy

When UI is later added, the file picker should trust the user and allow broad/all-file selection. This phase may only prepare runtime plumbing, but do not design APIs that assume the path extension must be `.d64`.

The parser still validates actual image content/size.

## Tests

Add tests at the runtime/machine boundary if the current harness supports it. Otherwise, add the closest available unit tests and leave UI rendering to Phase F.

Required tests:

- Mount `assets/disks/blank.d64` as device 8.
- Mount `assets/disks/ODELLLAK.D64` as device 8 and expose copied status with a plausible disk title/display name.
- Replace a mounted disk with another mounted disk.
- Unmount clears the copied status.
- Failed mount of missing or malformed file does not crash.
- Frontend-visible status is copied data only.
- Existing PRG loader behavior still works.
- Existing boot, keyboard, debugger, VIC bank, CIA, and breakpoint tests continue to pass.

## Acceptance criteria

- Device 8 can be mounted from a host file path on the runtime thread.
- Device 8 can be unmounted.
- Replacing a disk is deterministic and memory-safe.
- Machine owns the live drive state.
- Frontend receives copied status only.
- No KERNAL LOAD behavior is claimed yet.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- Device 8 read-only D64 mount/unmount state exists.
- State ownership: tools parser, machine-owned live drive slot, runtime-thread-only mutation, frontend copied status.
- Supported image: standard 35-track D64.
- What tests were added.
- Remaining non-goals: KERNAL trap, directory load, PRG load from D64, device 9 if deferred, D64 writes, 1541/IEC/fast loaders.
