# CIA timing work — handoff

**Date:** 2026-07-11  
**Branch:** `main` (local; see recent commits below)  
**Status:** Option-2 CPU wiring + corpus harness done; timer model partially Lorenz-aligned; **priority corpus 11/31 PASS** on c64m.

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
5. **START / force-load:** still apply on write (immediate). Full “all CRA bits after tick” was tried and **regressed** oneshot/icr01new — do not reintroduce without a plan.

### VICE baselines (green oracle)
| Suite | Result |
|--------|--------|
| priority | 31/31 PASS |
| lorenz-cia | 40/40 PASS |
| cia-core | 66/66 PASS |

### c64m priority matrix (latest)
**11 PASS / 18 FAIL / 2 OTHER** — `results/c64m-priority-latest.tsv`

**Typically PASS:**
- `oneshot` (old+new labels; no chip switch yet)
- `icr01new`
- Several **new-CIA** `irqdelay*` and **CIA2** irqdelay variants

**Typically FAIL:**
| Cluster | Cases | Likely need |
|---------|--------|-------------|
| CRB write races | `cia1tb123`, `cia2tb123` | Sub-instruction write phase vs timer (Lorenz TB123 table) |
| FLIPOS / IMR PRGs | `flipos`, `imr` | Tighter write-vs-uf / IMR enable timing (partial model only) |
| Old CIA irqdelay | most `*old*` / some CIA1 | Explicit 6526 vs 8521 delay policy |
| Other | `reload0*`, `dd0dtest`, `icr01` (old) | Reload-0 races; ICR read-during-set old variant |

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

1. **Do not** blindly re-add full CRA/CRB “clock then apply” global delay — it broke oneshot.  
2. **Instrument one failure** (prefer `flipos` first block or `cia1tb123` first case): log per Phi2 `counter`, CRA, `oneshot_effective`, ICR flags, IR pin — compare to instrumented VICE or datasheet table.  
3. Drive **unit tests from Lorenz tables** (TB123 CRB write outcomes; FLIPOS set/clear at t-2/t-1/t) before chasing full PRGs.  
4. Only then consider **chip model** (old vs new interrupt delay) if old irqdelay remains the blocker after new-path is solid.  
5. Later: cycle-stamped dual logs; ctest gate on corpus subset.

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
