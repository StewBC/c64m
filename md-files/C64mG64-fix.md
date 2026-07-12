# G64 Root-Cause and Compatibility Plan

## Goal

Make both of these reference images load to their documented game-entry
checkpoints without title-specific emulator behavior:

- `assets/disks/arkanoid[imagine_1988](pal).g64` on a PAL C64
- `assets/disks/robocop[data_east_1987](ntsc)(alt)(!).g64` on an NTSC C64

VICE with true-drive emulation is the behavioral oracle. c64m must reproduce
the observable state at the checkpoints below; it must not select media
behavior from a C64 or 1541 program counter, disk filename, or title name.

## What we know

Arkanoid's initial `LOAD "*",8,1` succeeds. Its bootstrap subsequently loads
`"F"` through the real KERNAL/1541 path and enters a protection/fast-loader
stage at `$4000`. The stage uses undocumented, self-modifying 6510 code and
custom 1541 media accesses. c64m later reaches a zero-filled handoff at
`$9400` and pauses on its first `BRK` at `$9404`.

Robocop's current pass depends on an address-triggered adjustment in
`c1541.c` for drive PC `$030D`. Arkanoid also has drive code at that address.
The adjustment therefore proves that byte phase matters, but it is not a
valid general media model and must not become the final fix.

## Rules

1. Find the first divergence from VICE before changing emulation behavior.
2. Compare semantic milestones, not wall-clock time or raw global cycle count.
3. Keep diagnostics test-only or explicitly opt-in; no live-machine pointers
   may cross the runtime/frontend boundary.
4. No title names, disk filenames, C64 PCs, or drive PCs may choose final
   media behavior.
5. Keep both titles as regression tests. A fix for one may not regress the
   other.

## Phase 1 - Deterministic checkpoints and VICE oracle data

### 1A. c64m checkpoints

Add machine-level regression runners for both images. They load the same ROM
set and use the real 1541/G64 media path. Each runner must report a compact,
machine-readable checkpoint record.

Arkanoid checkpoints:

| ID | Event | Required capture |
|---|---|---|
| A0 | initial `LOAD "*",8,1` returns | C64 PC/registers, `$0100` bootstrap prefix, drive PC/state |
| A1 | bootstrap invokes `LOAD "F",8` | filename/LFS state, C64 PC/registers, drive state |
| A2 | C64 first executes `$4000` | registers, `$4000` prefix, `$01`, drive media/VIA state |
| A3 | first entry into `$9400` | registers, `$9400` prefix, write origins for `$9400..$941F`, drive state |

Robocop checkpoints:

| ID | Event | Required capture |
|---|---|---|
| R0 | `LOAD "FAST",8,1` returns | existing `$7000` handoff state |
| R1 | `LOAD "LOAD1",8,1` returns | existing VICE-checked `$8000` prefix |
| R2 | drive performs the stage-3 gap measurement | drive VIA/media state and gap-table digest |
| R3 | game-entry handoff | C64 PC/registers and game-memory signature |

The runner must stop at the event, not at an arbitrary cycle. It must emit the
checkpoint even when c64m fails, so that it can become a differential probe.

### 1B. VICE oracle captures

Use the installed VICE version with true-drive emulation, the matching PAL or
NTSC model, and the same disk image. Capture the equivalent checkpoint fields
through VICE's monitor. Store only small normalized text records and selected
memory windows under `tests/oracles/g64/`; do not commit VICE snapshots or
large continuous traces.

For each record include:

- emulator/ROM/version metadata;
- image hash and C64 standard;
- checkpoint identifier;
- C64 PC, A/X/Y/SP/P, `$01`, and relevant memory bytes;
- 1541 PC/registers, `$1C00` VIA registers, media position, and selected RAM;
- an event-relative count where useful, never a wall-clock timestamp.

The initial acceptance criterion is reproducibility of records, not equality.
After both capture paths are stable, promote selected VICE fields into test
assertions.

## Phase 2 - First-divergence trace

Add an opt-in ring-buffer trace to the machine test harness. It starts at a
checkpoint and retains only the final bounded window before the next
checkpoint/failure.

Record, per relevant event:

- C64 instruction PC/opcode/registers and writes to watched ranges;
- 1541 instruction PC/opcode/registers;
- VIA2 Port A/B, DDRA/DDRB, PCR, IFR/IER;
- head half-track, density, bit position, SYNC state, byte latch, BYTE READY,
  SO edge;
- IEC ATN/CLK/DATA changes.

Trace storage belongs to the test/machine side. Runtime may expose copied
snapshots for manual diagnosis, but tracing must not change emulation timing.

## Phase 3 - Isolate the subsystem

Compare c64m and VICE at the first mismatching checkpoint and apply these
experiments in order:

1. Confirm standard DOS/KERNAL file bytes and return state.
2. Compare drive byte sequence, SYNC exit, byte-latch lifetime, and SO/BVC
   cadence after the first custom-drive entry.
3. Compare density, motor/stepper, and G64 bit position evolution.
4. Compare IEC transitions if the custom loader communicates across IEC.
5. Compare the C64's undocumented-opcode instruction/register trace only if
   the input byte stream is already equal.

The first mismatch decides the owning subsystem. Do not change later layers
until that mismatch is understood.

## Phase 4 - General correction

Replace PC-triggered media realignment with a state-derived model:

- preserve actual flux position across SYNC and Port-A reads;
- derive BYTE READY/SO from completed bitcells and the byte latch;
- honor VIA direction/PCR behavior without synthetic per-loader skips;
- model density and stepper changes from VIA outputs;
- preserve correct IEC sampling order.

If the first divergence is instead C64 CPU behavior, add focused tests for the
exact documented or undocumented opcode/bus sequence seen in the oracle trace.

## Phase 5 - Acceptance

- Arkanoid reaches a VICE-matching post-loader/game-entry checkpoint.
- Robocop retains its current load-to-game result without an address-triggered
  alignment exception.
- Both machine regression tests run in the normal suite.
- Existing 1541 media, GCR, IEC, CPU, and runtime suites pass.
- `docs/status/IEC1541.md`, `docs/status/TESTING.md`, and
  `md-files/c64m1541media.md` describe the verified matrix and any remaining
  limitation precisely.
