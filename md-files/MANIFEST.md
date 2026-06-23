# c64m handoff split manifest

Generated from the monolithic `STATUS.md` handoff and the existing `AGENTS.md`
and `MASTER.md` project documents.

## Replacement top-level files

- `AGENTS.md` - updated reading order, phase workflow, definition of done, and status update rules for the split `docs/status/` layout.
- `MASTER.md` - updated with a documentation layout section; architecture content preserved.
- `STATUS.md` - short current handoff and routing document.

## New component handoff files

- `docs/status/README.md`
- `docs/status/VICII.md`
- `docs/status/CIA.md`
- `docs/status/SID.md`
- `docs/status/AUDIO.md`
- `docs/status/CPU_MACHINE.md`
- `docs/status/FRONTEND_DEBUGGER.md`
- `docs/status/DISK_IO.md`
- `docs/status/TESTING.md`
- `docs/status/DEFERRED.md`
- `docs/status/OPTIMIZATIONS.md`

## Historical reference

- `docs/status/ORIGINAL_STATUS.md` - verbatim copy of the prior monolithic status handoff.

## Install suggestion

Copy the contents of this folder into the repository root, replacing the old
`AGENTS.md`, `MASTER.md`, and `STATUS.md`, and adding the `docs/status/` tree.
