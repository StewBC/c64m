# Phase 5 timing acceptance corpus

## Corpus identity

- Corpus: `c64m-phase5-timing-v1`
- Captured: 2026-07-11
- Target: current PAL and NTSC machine models, with cycle-level AEC/RDY
- Authority: executable project tests are the source of truth for pass/fail

This is a small, project-owned acceptance corpus. The trace fixtures are
constructed in the test sources; no external ROM or trace corpus is vendored.
The `dkarcade2016.prg` entry points at the project-owned sample already in
`samples/` and records a reproducible manual/control-port outcome rather than
pretending that a checked-in image hash is a hardware oracle.

## Reproduction

From the repository root:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -j1
```

Focused timing checks:

```sh
ctest --test-dir build --output-on-failure -R 'c64_(vicii|cpu_validation)$'
```

The ordinary-software boot gate is:

```sh
ctest --test-dir build --output-on-failure -R 'c64_boot_progression|runtime_romboot'
```

## Versioned fixtures and expected outcomes

| ID | Executable fixture | Expected outcome |
| --- | --- | --- |
| `CPU-VIC-PAL-BADLINE` | `test_cpu_vic_pal_ntsc_interaction_traces`, PAL bad line | `LDA $1234` keeps its opcode/operand cycles, holds the data read through the bad-line VIC ownership, and completes at the first eligible Phi2; raster and BA transitions match the checked-in PAL baseline. |
| `CPU-VIC-PAL-SPRITE0` | same test, PAL sprite 0 | The CPU read is delayed by the scheduled sprite-data interval and resumes after BA/RDY release. |
| `CPU-VIC-NTSC-SPRITE0` | same test, NTSC sprite 0 | The NTSC late sprite-data interval is used; the result and bus-event order remain identical while absolute completion cycles differ from PAL. |
| `CPU-VIC-PAL-SPRITE3-XLINE` | same test, PAL sprite 3 cross-line | A sprite-data window crossing the raster boundary does not lose or duplicate the pending CPU read. |
| `AEC-RDY-PAL` | `test_aec_rdy_pin_transitions_follow_schedule` | RDY leads the VIC-owned interval; AEC is low only for the actual Phi2 VIC-owned slot; both return high at the documented release cycle. |
| `AEC-WRITE-ARB` | `test_aec_blocks_pending_write_during_vic_phi2` | A pending CPU write is blocked during AEC-low VIC ownership, but a write may complete during a BA/RDY-only interval while AEC remains high. |
| `CPU-FAMILIES` | migrated-family tests in `test_c64_cpu_validation` | Legal opcode/addressing families and practical SLO/RLA/SRE/RRA/DCP/ISC/LAX/SAX representatives have the same architectural result and event shape under normal and PAL bad-line execution. |
| `VIC-SCHEDULE` | `test_vicii_bus_schedule_reports_c_and_sprite_accesses` and VIC schedule tests | PAL/NTSC c-access, g/idle, sprite-pointer, and sprite-data markers are stable, named, and feed the single CPU-facing BA predicate. |
| `VIC-RASTER` | `test_c64_vicii` raster, border, sprite, and expose harness tests | Raster progression, live raster writes, sprite visibility/collision, opened borders, idle graphics, and bad-line latching remain stable. |
| `ORDINARY-BOOT` | `c64_boot_progression`, `runtime_romboot` | Reset/ROM execution reaches the expected boot milestones without a pre-reset BRK or lost reset sequencing. |
| `RASTER-DKARCADE-EXPOSE` | `samples/dkarcade2016.prg` via the control-port recipe in `docs/status/VICII_EXPOSE_REVEAL.md` | The settled NTSC open-border picture has the documented static geometry and the expose animation is compared using frame dimensions, lit-row metric, plateau hashes, `$D017`, and `$D011` observations. This remains a manual compatibility check because no VICE binary or hardware capture is part of the repository. |

The exact cycle signatures for the first four CPU/VIC fixtures are maintained
in [PAL_NTSC_CURRENT.md](PAL_NTSC_CURRENT.md). That file intentionally labels
them current-model regression baselines, not real-chip golden traces.

## Compatibility gains attributable to this timing work

The acceptance corpus identifies concrete behavior changes:

1. A contended CPU read is now held at its pending bus operation while VIC-II
   time advances, instead of being treated as an instruction-level delay.
2. A pending write no longer crosses an AEC-low VIC-owned Phi2 cycle; the
   distinct BA/RDY-only write case remains permitted.
3. PAL and NTSC sprite-data windows, including cross-line windows, now produce
   different scheduled contention where the standards require it.
4. Instruction stepping and cycle stepping use the same CPU/VIC arbitration
   contract for the migrated legal and practical-undocumented families.
5. Timed raster/VIC writes and the existing open-border raster sample retain
   their bus-cycle ordering instead of relying on completion-time replay.

These are compatibility properties demonstrated by the project fixtures; they
are not a claim that every commercial title or demo is now cycle-perfect.

## External comparison and limits

The documented `dkarcade2016.prg` investigation records comparison against
VICE and a real NTSC C64 for the static open-border image and the reveal
behavior. It is retained as a reproducible project sample, but the comparison
is not rerun automatically by this test suite. The local PAL/NTSC timing
documentation remains the hardware-timing reference for the model, with chip
revision and standard called out wherever a choice is made.

The following remain explicitly outside this acceptance corpus and are tracked
in [DEFERRED.md](../docs/status/DEFERRED.md): analog/half-cycle AEC/RDY
waveforms, last-byte/open-bus behavior, light pen, CIA sub-Phi2 races, unstable
undocumented opcodes, and broad FLI/demo-scene compatibility.

## Phase 5 decision

Phase 5 is complete for this bounded cycle-level scope. The next expansion
should be a separately approved hardware-authoritative validation effort or a
separately scoped feature (open bus, light pen, CIA sub-cycle, or broader
demo-scene support), not an implicit claim of full VICE replacement fidelity.
