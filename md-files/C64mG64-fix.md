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

The current media implementation also repositions the head at a selected
post-sync point on every VIA stepper transition. That is not a physical head
operation: a step changes radial track, not the platter's angular phase.
Removing the two repositioning shortcuts exposes the same first-gap error in
Robocop (`$7B,$1D,$FF,$52` followed by the stable `$8D,$1D,$FF,$52` cadence).
This localises the remaining defect to generic SYNC exit / Port-A byte latch /
SO delivery phase, rather than image parsing or Arkanoid's C64-side code.

The 1541 is also currently advanced once per C64 cycle. This is a 1.5% drive
clock error on PAL. A direct fractional-clock experiment correctly changes
raw-loader behaviour but also exposes an IEC cross-clock scheduling defect,
so the final clock correction must keep IEC transitions sampled in the C64
domain rather than execute multiple drive bus transitions in one C64 cycle.

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

## Investigation record — 2026-07-12

### Current status

**Blocked; not fixed.**  Both PAL Arkanoid variants fail to reach a verified
game state in c64m.  The original image reaches `$9400`, shows the blue
bitmap-mode screen reported by the user, and then remains in `$5F63–$5F6C`
with the drive idle.  The alternate image reaches the same broad failure
class, sometimes without a BRK.  Neither outcome is acceptance.

The temporary title-specific drive-PC hook at `$030D` was removed from
`c1541.c`; `test_c64_robocop_g64` still passed with the generic SO placement
change.  A separate, pre-existing C64-side `$0A00–$0BFF` BA-stall workaround
for Robocop remains in `c64.c`; it must be replaced by a generic model before
this work can be called complete.

### Reproduction path

The machine probe follows the real user path:

1. PAL C64 configuration, true 1541 and G64 media enabled.
2. Mount either Arkanoid G64 on device 8.
3. Put the exact PETSCII `LOAD "*",8,1` plus Return into the keyboard buffer.
4. Run through the real KERNAL, drive ROM, G64 and IEC paths.

Probe target: `probe_c64_arkanoid_g64`

- No argument: `arkanoid[imagine_1988](pal).g64`
- `alt`: `arkanoid[imagine_1988](pal)(alt).g64`

The target is also registered as `c64_arkanoid_g64` and
`c64_arkanoid_alt_g64`, but both are intentionally failing regressions until
the actual game-state assertion can be defined from a valid oracle capture.

### Confirmed Arkanoid milestones in c64m

For the non-alt PAL image with the current worktree:

| Milestone | Approx. C64 cycle | Observation |
|---|---:|---|
| A1 | 5.19 M | Bootstrap invokes the secondary `"F"` load. |
| A2 | 8.33 M | C64 executes `$4000`; prefix is `78 A2 FF 9A A9 37 85 01`. |
| A3 | 81.60 M | C64 executes `$9400`; prefix is `A9 35 85 01 80 AD 4C 5A`. |
| Post-A3 | 82 M+ | C64 settles in `$5F63–$5F6C`; drive becomes idle on half-track 36. |

At the stable post-A3 state, VIC registers show an intentional bitmap-mode
display, not a generic character-mode renderer failure:

```
D011=$1B  D016=$08  D018=$14  VIC bank=$0000
D020=$FE  D021=$06
```

However, the loaded code contains an impossible sequence at `$5F62`:

```
AND #$01
BMI $5F5C
```

`AND #$01` cannot set N, so the following BMI cannot take its backward branch.
This is strong evidence of corrupt payload bytes rather than a merely cosmetic
VIC problem.  A plausible intended branch is `BNE`, but no byte patch was
attempted: that would conceal the transfer defect.

### Diagnostics added

`tests/machine/probe_c64_arkanoid_g64.c` currently records:

- A1/A2/A3 C64 and drive state;
- A2/A3 RAM dumps under `/private/tmp` for local differential work;
- C64 unsupported-opcode counts before A3 and after A3;
- first `$5Fxx` state, selected write histories, and VIC display registers;
- the exact disk-loading command path.

The probe originally treated `$5Fxx` itself as a terminal corruption signal.
That heuristic is too strong because the region contains plausible IRQ/CIA
code.  The useful terminal evidence is instead the permanently impossible
branch sequence, stable C64 PC range, idle drive, and invalid display/game
state.

### Generic changes investigated

The active worktree contains these implementation experiments:

1. **No drive-PC media hook.**  The `$030D` alignment/skip special case was
   removed from `c1541.c`.
2. **SO delivery after drive Phi2.**  BYTE READY is presented after the drive
   CPU cycle, avoiding an early BVC sample.  This moved Arkanoid from an early
   `$9404` BRK to the later `$9400` handoff, and retained the Robocop test.
3. **VICE-inspired G64 flux decoder.**  G64 bits are treated as flux
   transitions.  The decoder models the 16-reference-tick drive clock,
   UE7/UF4 divider, 40-tick filter, deterministic pseudo-flux recovery,
   G64 head-position scaling on step, PCR-gated BYTE READY, SO delay, and the
   14-reference-tick Port-A bus-read lead.
4. **Cycle-stepped stable undocumented NOPs.**  `$1C`, `$44`, `$80`, and
   `$FC` execute after Arkanoid's `$9400` handoff.  They now run through the
   C64 microcycle path rather than the bulk fallback.  The post-A3 deferred
   opcode list is empty after this change.

None of these changes alone reaches a game state.  They are retained as
investigation work, not claimed fixes.

### Rejected experiments

All of the following were tested and reverted because they either did not
move Arkanoid past the stable failure or regressed Robocop/earlier Arkanoid
stages:

- make PAL drive reference time follow an independent 16 MHz wall clock;
- rotate the G64 stream immediately on motor-on instead of the existing
  spin-up model;
- deliver SO only at BVC/BVS/PHP/CLV polling instructions;
- change drive-to-C64 IEC output visibility from the current two-stage path
  to one-stage or immediate;
- delay C64-to-drive IEC visibility by a cycle;
- freeze the 1541 for all C64 VIC BA stalls;
- replace the Robocop address-range BA workaround with broad or
  CIA2-address-latch-based generic BA conditions.

The BA experiments are especially important: broad conditions corrupted both
titles, so an address substitution is not a valid generalization.

### VICE work and caveat

VICE source was inspected locally, especially:

- `src/drive/rotation.c`
- `src/drive/iecieee/via2d.c`
- `src/drive/drivecpu.c`
- `src/c64/c64cia2.c`
- `src/c64/c64iec.c`

Confirmed VICE details incorporated into investigation:

- G64 is a flux-transition stream;
- GCR rotation uses a 16-reference-tick drive clock and UE7/UF4 divider;
- flux filtering is 40 reference ticks;
- SO delay is phase-dependent and at least 10 reference ticks;
- Port-A reads request a 14-reference-tick bus delay;
- VICE catches the drive up at CIA2 IEC accesses;
- CIA2 PA5/PA4/PA3 map to IEC DATA/CLK/ATN through inverted/open-collector
  logic.  c64m's basic line polarity matches this mapping.

The remote VICE monitor did successfully demonstrate a later valid game run
and showed the known `$9400` prefix.  Attempts to save RAM at a breakpoint
were **not reliably synchronized**: monitor output confirming that a
breakpoint was installed was initially mistaken for proof that it had fired.
Those RAM dumps are invalid as an oracle and must not be used for assertions.

### Exact unresolved problem

The remaining fault is a **dual-bit IEC payload corruption during/after the
final Arkanoid transfer**.  The C64 receiver at `$0190` waits on CIA2 PA7 and
samples two bits per CIA2 read using `ASL/PHP/ROL`.  The final loaded state
contains bit errors that turn control-flow bytes into impossible code.  The
failure is therefore narrower than “G64 parsing is broken,” but no current
trace identifies the first incorrect IEC bit or its corresponding drive VIA
transition.

### Required next step to resume

Obtain a trustworthy, event-synchronized oracle trace or snapshot at the
same transfer boundary.  The most productive trace has one record per C64
CIA2 PA read / 1541 VIA1 PB transition and includes:

- C64 PC/opcode/microcycle and CIA2 PA/DDRA;
- resolved IEC DATA/CLK/ATN state;
- drive PC/opcode, VIA1 ORB/DDRB/input pins;
- the precise point at which each two-bit receiver sample is taken;
- destination address and resulting C64 byte.

Capture it from VICE at the `$0190` receiver loop and c64m with the same
event numbering.  The first differing line value or receiver bit owns the
fix.  Do not resume with byte patches, title/PC timing branches, or another
global delay sweep.
