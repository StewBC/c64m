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
7. **START clear takes effect one Phi2 late (`stop_pending`).** A CPU CR write that clears START on a running timer still counts on the write cycle; the stop lands the next Phi2. This is a **targeted** delay on the START bit only — the full “all CR bits after tick” was tried before and **regressed** oneshot/icr01new; do **not** generalise. Greened cia1tb123 blocks 13-16 (`01 10`, `01 00 cycle 1`).

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
| CRB write races (tail) | `cia1tb123`, `cia2tb123` | Blocks 1-16 pass; blocks 17-18 (`01 00 cycle 2/3`) freeze the counter one count high — the STOP write **lands one Phi2 early** for that instruction pattern (stx as the first opcode). Block 16 (same CIA model) is correct, so this is CPU deferred-write **phase** alignment, not the CIA stop count (a stop-delay of 2 breaks block 16). |
| FLIPOS / IMR PRGs | `flipos`, `imr` | `flipos` **fails at block 1** ("set oneshot at underflow-1", expects 255): c64m returns 252, which is block 2's ("set at t") answer. Traced cause: the counter underflow lands ~1 Phi2 **before** the `stx $dc0e` oneshot write (the double-write `sta`/`stx` and the underflow are misaligned by one cycle), so "t-1" behaves like "t+1". This is underflow-cycle alignment vs the fixed instruction sequence — entangled with the `underflow-on-1` model and start-from-stopped timing that the passing tb123/oneshot cases depend on. Not an isolated oneshot-pipeline tweak (swapping the `oneshot_for_uf` sample vs `update_oneshot_pipe` order does **not** move it, and is unjustified without a test). Needs a cycle-stamped VICE diff of this exact block before touching the count model. |
| Old CIA irqdelay | most `*old*` | Explicit 6526 vs 8521 delay policy |
| Other | `reload0*`, `dd0dtest`, `icr01` (old) | Reload-0 races; ICR read-during-set old variant |

**Instrumentation used:** a standalone corpus runner that snapshots the CPU per
Phi2 (`c64_copy_cpu_snapshot`) and prints, when a Lorenz `.block` first writes
`$FF` to the debugcart, the failing block index and the accumulator it computed —
plus an optional per-Phi2 trace of the timer counter / CR / delay fields for a
`TRACE_BLOCK`. This is how blocks 4 / 9 / 12 / 13 were localised. Recreate under
`tools/` if the tail work needs it again.

**"OTHER" (exit 10) = genuine FAIL:** `irqdelay2` / `irqdelay2-new` write their *own* failure code (`ldy #10; sty $d7ff`) on a detection mismatch — 10 is not an ambiguous value, it means fail. The test measures the exact IRQ delay for five consecutive timer values (20..24 cycles) and compares against reference tables (`old 0b 0b 0c 0c 0d` / `new 0a 0b 0b 0c 0c`). c64m matches neither fully, so both variants fail. Greening needs a per-timer-value IRQ-delay match — a finer-grained version of the `irqdelay` cluster, tied to the underflow→IRQ pipeline.

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

1. **cia1tb123 blocks 17-18 (CPU write-phase, not CIA).** Blocks 1-16 pass. In
   17/18 (`stx $dc0f` as the *first* opcode, plain STOP) the counter freezes one
   count high: the STOP write lands one Phi2 earlier than for block 16 (where a
   `nop` precedes the `stx`). The CIA-side `stop_pending` delay is already correct
   (block 16 freezes exactly right, and a 2-cycle stop delay breaks block 16), so
   the residual is the c6510 deferred-write micro-model landing the CR write one
   Phi2 early for the no-preceding-instruction pattern. Investigate
   `c64_apply_pending_cpu_events_at_elapsed` / the write event `cycle_offset` for
   `stx abs` vs `nop; stx abs`. Confirm against instrumented VICE before changing
   CPU write timing (it is shared with every I/O write).  
2. **Do not** re-add a global “clock then apply all CR bits” delay (regressed
   oneshot/icr01new).  
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
