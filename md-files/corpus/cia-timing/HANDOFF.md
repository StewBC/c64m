# CIA timing work — handoff

**Date:** 2026-07-11  
**Branch:** `main` (local; see recent commits below)  
**Status:** Option-2 CPU wiring + corpus harness done; timer model Lorenz-aligned including 6526 register-write and force-load timing; **priority corpus 13/31 PASS** on c64m.

Read with: `README.md`, `AGENT_PHASE4.md`, `PRIORITY.md`, `SUMMARY.md`, `docs/status/CIA.md`.

---

## Goal

Bit-exact enough 6526 interrupt/timer race behavior that VICE `testprogs` CIA cases (and real-hardware tiebreakers) pass on c64m **without** mid-race ICR polling.

---

## What landed (do not redo)

### Infrastructure
| Piece | Location |
|--------|----------|
| VICE oracle runner | `tools/cia-timing-corpus/run_x64sc.sh` |
| c64m PRG runner | `tools/cia-timing-corpus/run_c64m.c` → `build/run_c64m_cia_corpus` |
| Suite driver | `tools/cia-timing-corpus/run_c64m.sh priority` |
| Fetch testprogs | `tools/cia-timing-corpus/fetch.sh` → `external/cia-timing-corpus/` (gitignored clones) |
| Results | `md-files/corpus/cia-timing/results/*-latest.tsv` |
| `$D7FF` debugcart | `c64_bus` + `c64_set_debugcart_enabled` / `c64_debugcart_*` |

**Hard rule:** never poll `$DC0D`/`$DD0D` mid-race from a remote harness. Settled debugcart exit only (`$00` pass, `$FF` fail).

### CPU path (Option-2 Phase 4)
- `c64_cpu_irq_pending` / `c64_cpu_nmi_pending` sample **`cia_interrupt_line`** (delayed pin), not immediate `cia_irq_pending`.
- RESTORE NMI and VIC IRQ OR unchanged.
- Unit tests in `tests/machine/test_c64_cia.c` cover delay to CPU entry.

### Timer / ICR model (current)
Implemented in `src/machine/cia.c` / `cia.h` (Lorenz software model–oriented):

1. **Start delay:** 2 Phi2 clocks after rising START before counting.  
2. **Underflow:** tick when `counter <= 1` → set ICR source, reload latch, **skip next count** (phi2 pattern `2-1-2-2-…`, no sticky visible `0`).  
3. **IR flip-flop:** set when `flags & mask`; **only ICR read clears**; clearing IMR does not clear IR.  
4. **Oneshot effective bit:** delayed (set delay 1, clear delay 2); CRA oneshot **write applied after** this cycle’s timer tick so same-cycle set does not affect that underflow (FLIPOS intent).  
5. **Timer register writes:** a LOW-byte write updates the **latch only**; a stopped counter is loaded only by a HIGH-byte write (or force-load / underflow). This is the 6526 rule (previously both bytes reloaded → cia1tb123 block 4 read the latch instead of the running counter).  
6. **Force-load:** deferred via `load_delay` — reload lands on the **second Phi2** after the CR write, and suppresses counting on that Phi2 **and** the following one (`load_hold`). Kept separate from the underflow `skip_tick` so a cascade / CNT-gated timer clears the suppression on schedule instead of eating its next real count. This greened cia1tb123 blocks 4-12 (`00 10`, `00 11`, `01 11` write phases).  
7. **START clear still applies immediately.** Full “all CRA bits after tick” was tried and **regressed** oneshot/icr01new — do not reintroduce without a plan. The remaining cia1tb123 blocks (13-18, `01 10` / `01 00`) need a **targeted** delayed effect: a CR write that clears START must let the timer count for the write cycle, with the stop taking effect the next Phi2 (do **not** generalise to all CR bits).

### VICE baselines (green oracle)
| Suite | Result |
|--------|--------|
| priority | 31/31 PASS |
| lorenz-cia | 40/40 PASS |
| cia-core | 66/66 PASS |

### c64m priority matrix (latest)
**13 PASS / 16 FAIL / 2 OTHER** — `results/c64m-priority-latest.tsv`

**Typically PASS:**
- `oneshot` (old+new labels; no chip switch yet)
- `icr01new`
- Several **new-CIA** `irqdelay*` and **CIA2** irqdelay variants
- **New:** `irqdelay-cia1` and `irqdelay-cia1-oneshot` (default model), greened by
  the force-load timing fix.

**Typically FAIL:**
| Cluster | Cases | Likely need |
|---------|--------|-------------|
| CRB write races (tail) | `cia1tb123`, `cia2tb123` | Blocks 1-12 pass; blocks 13-18 (`01 10` / `01 00`) need the delayed START-clear effect on counting (targeted, not all-CR-bits) |
| FLIPOS / IMR PRGs | `flipos`, `imr` | Tighter write-vs-uf / IMR enable timing (partial model only) |
| Old CIA irqdelay | most `*old*` | Explicit 6526 vs 8521 delay policy |
| Other | `reload0*`, `dd0dtest`, `icr01` (old) | Reload-0 races; ICR read-during-set old variant |

**Instrumentation used:** a standalone corpus runner that snapshots the CPU per
Phi2 (`c64_copy_cpu_snapshot`) and prints, when a Lorenz `.block` first writes
`$FF` to the debugcart, the failing block index and the accumulator it computed —
plus an optional per-Phi2 trace of the timer counter / CR / delay fields for a
`TRACE_BLOCK`. This is how blocks 4 / 9 / 12 / 13 were localised. Recreate under
`tools/` if the tail work needs it again.

**OTHER (exit 10):** `irqdelay2` / `irqdelay2-new` — non-`$00`/`$FF` debugcart value; investigate before treating as pass/fail.

c64m does **not** implement `-ciamodel`; matrix `cia_model` column is documentary only.

---

## Reproduce

```bash
# once
./tools/cia-timing-corpus/fetch.sh

# VICE oracle
./tools/cia-timing-corpus/run_x64sc.sh priority

# c64m
cmake --build build --target run_c64m_cia_corpus
./tools/cia-timing-corpus/run_c64m.sh priority

# unit + full suite
cmake --build build --target test_c64_cia && ./build/test_c64_cia
cd build && ctest --output-on-failure
```

Default VICE binary path in scripts:  
`/Applications/vice-arm64-gtk3-3.10/bin/x64sc` (override with `X64SC=`).

---

## What to do next (recommended order)

1. **Finish cia1tb123 (blocks 13-18).** These are `01 10` (force-load while running,
   which also clears START) and `01 00` (plain STOP while running). Block 13 was
   traced: c64m's counter is one count **behind** the sampled value because the CR
   write clears START on the write cycle, stopping the count too early. Real HW
   counts for the write cycle and stops the next Phi2. Model this as a **targeted**
   one-cycle delay on the START-bit's effect on counting only — **not** a global
   “apply all CR bits after tick” (that regressed oneshot/icr01new before). Verify
   `oneshot`/`icr01new`/`flipos` after each attempt.  
2. **Do not** blindly re-add full CRA/CRB “clock then apply” global delay.  
3. Then `flipos` (fails old+new) and `imr`: FLIPOS set/clear at t-2/t-1/t vs the
   oneshot-effective pipeline; drive from Lorenz tables as unit tests first.  
4. Only then consider **chip model** (old vs new interrupt delay) for the old
   irqdelay cluster once the model-independent failures are gone.  
5. Later: cycle-stamped dual logs; ctest gate on corpus subset.

**Note:** `c64_boot_progression` (ctest #16) fails at HEAD *independently* of this
arc (pre-existing; `irq vector entered` e100 vs e001). Not caused by the timer work.

---

## Key commits (this arc)

```text
016cfce  Wire CIA IRQ/NMI through delayed pin + VICE corpus package
22e36a7  c64m debugcart + PRG corpus runner
e424137  Lorenz start-delay + underflow-on-1 pipeline
6716408  IR flip-flop + delayed oneshot effective bit
1114f4d  Oneshot CRA write applied after timer tick
```

---

## Files to touch next

- `src/machine/cia.c` / `cia.h` — timer/CRB race phases  
- `tests/machine/test_c64_cia.c` — table-driven race unit tests  
- `tools/cia-timing-corpus/*` — only if harness/debugcart needs extension  
- Update `SUMMARY.md` + this file when matrix moves  

---

## Explicit non-goals for the next session unless asked

- Full TOD / long `cia-sdr-icr` matrix  
- Wiring FLAG/SP/PC to tape/RS-232  
- IEC1541 media expansion (drive path is maintenance-only)  
