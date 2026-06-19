<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_G.md
# Optional Device 9 Support

## Goal

Add device 9 support for the existing read-only mounted D64 path, if still desired after device 8 is complete.

This phase should be skipped or kept as a small final phase if device 9 is not useful. Earlier phases should avoid hard-coding choices that make this phase unnecessarily hard.

## Scope

Implement device 9 only for behavior already supported on device 8:

- Mount/unmount D64 as device 9.
- Copied status for device 9.
- `LOAD "$",9`.
- `LOAD "NAME",9`.
- `LOAD "NAME",9,1`.
- Filename/wildcard/error behavior matching device 8.

## Non-goals

Do not implement:

- Different drive ROM/device behavior.
- Device numbers beyond 8 and 9.
- Simultaneous IEC protocol behavior.
- Disk writes or SAVE to D64.
- 1541/IEC/fast-loader support.

## Design guidance

If earlier phases used a drive-slot abstraction, this phase should mostly enable and test the second slot.

Do not duplicate device-8 code paths manually. Shared helpers should be used where they already exist, but do not add a speculative generalized drive subsystem beyond what devices 8 and 9 need.

If the KERNAL trap currently checks `device == 8`, change it to accept `device == 8 || device == 9` only where mounted drive state exists.

If device 9 is not mounted, fail safely for device 9 without disturbing device 8.

## UI

Enable the existing Machine-tab row:

```text
[9] [-] {Name}
```

`[9]` opens the same broad/trusting host OS file selector as `[8]` and mounts the selected D64 as device 9. `[-]` unmounts device 9.

Device 8 and device 9 labels must update independently from copied status.

## Tests

Required tests:

- Mount `ODELLLAK.D64` as device 9.
- `LOAD "$",9` loads a directory.
- `LOAD "MENU1",9,1` loads from device 9.
- Device 8 and 9 can hold different mounted images at the same time.
- Unmounting device 9 does not unmount device 8.
- Missing device 9 disk fails safely.
- Device 8 behavior remains unchanged.
- UI status rows update independently.
- Existing tests continue to pass.

## Acceptance criteria

- Device 9 works for the same read-only D64 load behaviors as device 8.
- Device 8 behavior is not regressed.
- Status and UI are independent per device.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- Disk Phase G complete, or explicitly skipped if not implemented.
- Supported device numbers.
- Tests added.
- Remaining gaps: D64 writes, SAVE to disk, 1541/IEC/fast loaders, any unsupported device numbers.
