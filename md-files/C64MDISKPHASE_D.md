<!--
Generated implementation guide for c64m.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. C64MDISK.md or C64MFDATA.md as applicable
5. This phase document
-->
# C64MDISKPHASE_D.md
# LOAD "$",8 Directory Synthesis

## Goal

Support BASIC directory loading from a mounted D64:

```text
LOAD "$",8
LIST
```

The implementation should synthesize a BASIC program representing the disk directory and load it into the normal BASIC program area so that `LIST` displays a plausible Commodore directory.

## Scope

Implement:

- Recognition of the KERNAL filename `$` for device 8.
- Directory BASIC program synthesis from the mounted D64 directory entries.
- Loading the synthesized program as a BASIC program.
- BASIC end-pointer updates so `LIST` works immediately.
- Stable, deterministic formatting.

## Non-goals

Do not implement:

- Full byte-perfect 1541 directory formatting if it complicates the first pass.
- `$:pattern` directory filters unless naturally supported by Phase E later.
- Wildcards for PRG loading unless already implemented.
- Device 9 unless explicitly included in a later phase.
- Disk mutation, SAVE, or error channel.
- 1541/IEC behavior.

## Directory program format

The output must be a valid tokenized BASIC program in C64 memory. It does not need to be cosmetically perfect, but it must be valid enough that BASIC `LIST` works.

Recommended first-pass display content:

```text
0 "DISK NAME" ID 2A
10 "FILE1" PRG
20 "FILE2" PRG
...
BLOCKS FREE.
```

A more authentic directory can be added later. Prioritize valid BASIC line links, stable output, and usefulness.

## Data source

Use the mounted machine-owned D64 state and parser metadata. Preserve raw PETSCII names internally and convert to display bytes appropriate for the synthesized BASIC program.

Directory entries should include:

- File block count.
- Filename.
- File type string such as PRG or SEQ.

`blank.d64` should display a valid empty/blank formatted directory with the appropriate blocks-free line.

## Loading behavior

Treat directory load as a BASIC program load:

- Write synthesized program into the normal BASIC program area.
- Update BASIC end pointers consistently.
- Return KERNAL success/final-address state as needed.
- Do not automatically `LIST`; the user controls screen output through BASIC.

## Error handling

Required safe failures:

- No disk mounted.
- Mounted disk state invalid.
- Directory synthesis allocation failure.
- Synthesized program too large for BASIC memory.

Do not corrupt unrelated memory on failure.

## Tests

Required tests:

- Mounted `blank.d64`: `LOAD "$",8` succeeds and produces a listable empty directory.
- Mounted `ODELLLAK.D64`: `LOAD "$",8` succeeds and synthesized content includes ordinary names such as `MENU1`, `LAKESPT.BIN`, and `LAKESTR.TXT` with plausible file types.
- BASIC line links in the generated directory are valid and terminate correctly.
- BASIC end pointers are updated consistently.
- Directory load does not auto-run.
- Missing mount fails safely.
- Existing Phase C PRG loads still work.
- Existing boot, keyboard, debugger, VIC bank, CIA, breakpoint, and disk parser/mount tests continue to pass.

If the current test harness can decode BASIC memory, assert decoded text. Otherwise, assert line-link structure and bytes in memory.

## Acceptance criteria

- `LOAD "$",8` loads a valid BASIC directory program from mounted device 8.
- `LIST` works after directory load in manual or automated smoke testing.
- Both blank and real fixture disks are covered.
- Existing PRG load behavior remains intact.
- Existing tests continue to pass.

## STATUS.md update

At the end of the phase, update `md-files/STATUS.md` with:

- Disk Phase D complete.
- Supported command: `LOAD "$",8` directory load.
- Directory formatting limitations.
- Tests added.
- Remaining gaps: wildcard/pattern behavior if deferred, device 9 if deferred, D64 writes, error channel, 1541/IEC/fast loaders.
