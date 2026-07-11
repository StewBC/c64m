# PAL and NTSC current-model timing baseline

## Status

This is an evaluation baseline captured from c64m's current implementation on
2026-07-10. It is deliberately not an assertion of full real-hardware accuracy.
It exists to expose behavior changes while the remaining CPU microcycle and
VIC-II fetch-schedule work proceeds.

The executable fixtures are in:

- `tests/machine/test_c64_cpu_validation.c`
- `tests/machine/test_c64_vicii.c`

## Shared fixture contract

CPU fixtures use a synthetic KERNAL program containing `LDA $1234`, with RAM
at `$1234` set to `$5A`. A fixture positions VIC timing, steps one Phi2 cycle
at a time, and records the master cycle, CPU cycle count, raster line/cycle,
BA predicate, and pending CPU state.

In the current scheduler, a CPU read that is pending when BA is already low is
held: master/VIC time advances but CPU time does not. A permitted write advances
normally. An assertion made by the VIC-II while it advances a cycle takes effect
for following CPU cycles; this ordering is part of the current baseline.

## PAL baseline

| Fixture | Current signature | Executable coverage |
|---|---|---|
| Bad line | c-access marker cycles 15 through 54; BA assertion is processed at cycle 12. The CPU cycle at 12 proceeds, followed by 43 held read attempts; the next eligible cycle resumes the pending read. | `test_timing_fixture_records_real_badline_stall`, `test_vicii_bus_schedule_reports_c_and_sprite_accesses` |
| Sprite 0 | BA assertion is processed at cycle 54. The current six-cycle window produces five following held read attempts, then resume. Sprite fetch marker is cycle 57. | `test_timing_fixture_records_pal_sprite_ba_stall`, `test_vicii_bus_schedule_reports_c_and_sprite_accesses` |
| Sprite 3 cross-line | BA assertion is processed at cycle 60 of line N-1 for the line-N fetch. The held read spans the raster-line rollover and resumes after the window. Sprite fetch marker is cycle 0 of line N. | `test_timing_fixture_records_cross_line_sprite_ba_stall` |
| CPU write timing | `STA $D020` emits opcode fetch, two operand reads, then the write at instruction start plus 3 Phi2 cycles. VIC observes the register write at that bus-event cycle. | `test_sta_d020_applies_at_event_cycle` |

PAL uses 63 cycles per line and 312 lines per frame.

## NTSC baseline

| Fixture | Current signature | Executable coverage |
|---|---|---|
| Bad line | The c-access marker and current bad-line BA sequence use the same documented cycle numbers as the PAL model: c-access marker cycles 15 through 54 and assertion processed at cycle 12. | `test_vicii_bus_schedule_reports_c_and_sprite_accesses` |
| Sprite 0 | BA assertion is processed at cycle 56. The current six-cycle window produces five following held read attempts, then resume. Sprite fetch marker is cycle 59. | `test_timing_fixture_records_ntsc_sprite_ba_stall` |
| Sprite fetch schedule | Sprite fetch marker cycles are 59, 61, 63, 0, 2, 4, 6, and 8 for sprites 0 through 7. | `vicii_ntsc_sprite_fetch_cycle`, `test_vicii_bus_schedule_reports_c_and_sprite_accesses` |

NTSC uses 65 cycles per line and 263 lines per frame.

## Interpretation rules

- Do not use these figures as a substitute for a cited hardware timing source.
- If a later CPU or VIC-II change intentionally changes a fixture, update its
  executable assertion and this document together, explaining whether the new
  value is a current-model regression baseline or a hardware-validated target.
- A new fixture should name PAL or NTSC, the ROM/program bytes, the starting
  raster line/cycle, expected CPU result, and the relevant bus/BA signature.
