# Agent brief: Option-2 Phase 4 (CPU-visible CIA IRQ timing)

Read first:

1. `md-files/AGENTS.md`
2. `md-files/docs/status/CIA.md`
3. `md-files/C64MFULL_CIA.md` (Phase 4 design record)
4. `md-files/C64mCIATimingCorpusPlan.md`
5. This directory’s `README.md`, `PRIORITY.md`, `HARDWARE.md`
6. Latest `results/x64sc-priority-*.tsv`

## Current c64m state (do not ignore)

Option-2 Phase 4 CPU wiring is **in tree**:

- `cia_interrupt_line` = delayed internal pin model
- `cia_irq_pending` = immediate latched ICR (flags & mask)
- `c64_cpu_irq_pending` / `c64_cpu_nmi_pending` sample **`cia_interrupt_line`**
- Unit tests cover pin delay and CPU entry after the delay

**Remaining work** for full corpus parity: c64m PRG runner against priority
cases, cycle-stamped dual logs for ambiguous races, variant policy if tests
require it.

## Required work order

1. **Confirm corpus**  
   `./tools/cia-timing-corpus/run_x64sc.sh priority` still all PASS (or explain).

2. **Do not mid-race poll ICR** in any harness.

3. **Re-wire carefully**  
   - Point `c64_cpu_irq_pending` / CIA #2 NMI edge path at
     `cia_interrupt_line` (or equivalent delayed sample).  
   - Preserve VIC IRQ OR and RESTORE NMI independence.  
   - Keep debugger peeks side-effect free.

4. **Update tests deliberately**  
   Existing unit tests that assumed same-cycle IRQ may need one-cycle shifts.
   Document each expectation change. Add integration coverage for Tier 1 cases
   (at least via machine-level unit tests that mirror Lorenz scenarios if full
   PRG runner is not ready).

5. **CIA variant policy**  
   Document default (old 6526 vs new 8521) in `docs/status/CIA.md` / STATUS.
   Only expose a switch if a test proves the need.

6. **Acceptance**  
   - Priority corpus cases that can run on c64m: match VICE exit where hardware
     does not contradict.  
   - All existing c64m unit/smoke tests green after intentional updates.  
   - `docs/status/CIA.md`, `DEFERRED.md`, `TESTING.md` updated.

## Out of scope for the re-timing PR

- Building the entire VICE testbench into CI
- Cycle-log instrumentation of VICE (separate follow-up)
- Tape/RS-232 peripheral wiring of FLAG/SP/PC
- Broad ciavarious/TOD/SDR matrix until Tier 1 is solid

## Pass/fail semantics to implement or reuse

| Result | Meaning |
|--------|---------|
| debugcart `$00` / exit 0 | PASS |
| debugcart `$FF` / exit 255 | FAIL |
| hit cycle limit / exit 1 | TIMEOUT |

If c64m has no debugcart yet, equivalent: test writes a known memory result
byte + border color; control-port reads **after** completion only.

## Success criterion

A reviewer can re-run VICE priority TSV and c64m’s corresponding suite and see
matching PASS/FAIL on Tier 1 without ICR-polling cheats.
