<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_C.md
# KERNAL LOAD Trap for PRG Files

## Goal

Trap the C64 KERNAL LOAD path for mounted device 8 and support both:

```text
LOAD "NAME",8
LOAD "NAME",8,1
```

This phase makes mounted D64 PRG files load into emulator memory through machine-owned behavior without emulating a 1541 drive or IEC protocol.

## Scope

Implement a KERNAL-load trap in the machine layer. The trap must inspect the current ROM/KERNAL state, decide whether the load is a supported D64 device-8 PRG load, perform the load, and return to the ROM caller with a KERNAL-style success or failure result.

Support:

- Device 8 only.
- PRG files only.
- Exact filename matching sufficient for ordinary C64 filenames.
- `LOAD "NAME",8` using BASIC-load behavior.
- `LOAD "NAME",8,1` using the embedded PRG load address.
- Failure without memory corruption for no disk, missing file, unsupported type, malformed file, and oversized target range.

## Non-goals

Do not implement:

- `LOAD "$",8` directory loading. That is Phase D.
- Wildcards beyond any exact-name helper already available. Wildcards are Phase E unless trivial and well tested.
- Device 9 unless specifically deferred into a later optional phase.
- SAVE, scratch, rename, validate, initialize, or error channel.
- Disk mutation.
- IEC timing or 1541 emulation.
- Automatic RUN after load.

## Trap point

Trap KERNAL LOAD, not BASIC parsing.

The implementation agent must inspect the actual ROM integration and current CPU stepping hooks before choosing the exact trap mechanism. Do not assume a single hardcoded address without verifying the loaded KERNAL image.

The trap must read the KERNAL load context from emulated machine state, including:

- Device number.
- Secondary address.
- Filename pointer and filename length.
- Requested load behavior.
- Load target state and return registers/status expected by the ROM.

If the device is not 8, decline the trap and let the ROM continue normally.

If device 8 is requested but no supported mounted disk/file exists, return a KERNAL-style failure rather than falling through into an unimplemented IEC path, unless current ROM behavior gives a better failure result. Document the chosen failure path.

## LOAD semantics

### `LOAD "NAME",8,1`

Use the embedded PRG address:

- Extract PRG bytes from the mounted D64.
- First two bytes are little-endian load address.
- Write bytes after the address to C64 memory starting at that address.
- Set the KERNAL return state/final address as needed by common callers.
- Do not automatically run.

### `LOAD "NAME",8`

Use normal BASIC-load behavior:

- Extract PRG bytes from the mounted D64.
- Do not blindly use the embedded PRG address if current KERNAL/BASIC semantics require loading at the current BASIC start.
- Inspect the existing KERNAL/BASIC variables and current PRG loader code before deciding whether to use KERNAL target variables or direct BASIC memory/pointer updates.
- Update BASIC program end pointers enough that `RUN` works for ordinary BASIC PRG files.
- Preserve expected user behavior from BASIC `LOAD` as far as practical.

Add tests for both forms. This phase is not complete until both forms work.

## Filename handling

Use the parser's raw PETSCII directory filename bytes internally. Convert the KERNAL filename buffer to the comparable representation carefully.

Required in this phase:

- Strip or ignore surrounding quotes as represented by KERNAL state.
- Treat ordinary C64 filenames as PETSCII bytes.
- Exact match for normal names such as `MENU1`, `LAKESPT.BIN`, and `ODELL LAKE`.
- Case-insensitive host convenience only if it does not break C64 semantics.

Wildcard matching may remain Phase E.

## Memory safety

Before writing any loaded bytes:

- Verify PRG length is at least two bytes.
- Verify target start/end range fits C64 address space.
- Verify the write cannot wrap past `$FFFF`.
- On failure, leave unrelated memory unchanged.

Use existing machine-owned memory write paths where practical, not frontend/debugger write paths.

## Failure cases

Required safe failures:

- No disk mounted for device 8.
- Filename not found.
- Entry is not PRG.
- PRG shorter than two bytes.
- Track/sector extraction failure.
- Target memory range overflow.
- Unsupported KERNAL LOAD mode.

Failure should be visible to BASIC as a normal load failure where practical. Do not crash the emulator.

## Tests

Required tests:

- Mounted `ODELLLAK.D64`: `LOAD "MENU1",8,1` writes bytes to the embedded PRG address.
- Mounted `ODELLLAK.D64`: `LOAD "MENU1",8` loads using BASIC-load semantics and updates BASIC state enough for `RUN` or a targeted BASIC pointer assertion.
- Missing file returns failure and does not corrupt a sentinel memory range.
- SEQ file such as `LAKESTR.TXT` is rejected for PRG load.
- Device other than 8 is not hijacked.
- No mounted disk fails safely.
- Existing host PRG loader still works.
- Existing boot, keyboard, debugger, VIC bank, CIA, breakpoint, and Phase A/B disk tests continue to pass.

If automated BASIC command injection is available, include one end-to-end smoke test that types a `LOAD` command into BASIC. If not, use direct KERNAL-state setup and document the limitation.

## Acceptance criteria

- Mounted D64 PRG files can be loaded through KERNAL LOAD on device 8.
- Both `LOAD "NAME",8` and `LOAD "NAME",8,1` work.
- The trap declines or fails safely for unsupported devices/modes.
- BASIC-load pointer behavior is tested.
- No directory loading is claimed yet.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- Disk Phase C complete.
- Supported commands: `LOAD "NAME",8` and `LOAD "NAME",8,1` for PRG files on mounted device 8.
- Exact BASIC pointer/end-address behavior implemented.
- Tests added.
- Remaining gaps: directory load, wildcard matching if deferred, device 9 if deferred, D64 writes, error channel, 1541/IEC/fast loaders.
