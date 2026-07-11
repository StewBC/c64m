# Corpus run summary

Machine: macOS arm64 (`stefan-mini`), VICE `x64sc` from  
`/Applications/vice-arm64-gtk3-3.10/bin/x64sc`.

Method: autostart PRG + `-debugcart` + `-limitcycles` (no mid-race ICR polling).

## Baselines checked in

| Suite | File | Result |
|-------|------|--------|
| **priority** (Tier 1) | `results/x64sc-priority-latest.tsv` | **31/31 PASS** |
| **lorenz-cia** | `results/x64sc-lorenz-cia-latest.tsv` | **40/40 PASS** |
| **cia-core** | `results/x64sc-cia-core-latest.tsv` | **66/66 PASS** |
| **c64m priority** | `results/c64m-priority-latest.tsv` | **11 PASS / 18 FAIL / 2 OTHER** |

Priority includes: Lorenz `cia1tb123`/`cia2tb123`, `icr01`/`icr01new`, `imr`,
`flipos`, `oneshot` (old+new where applicable), full `CIA/irqdelay/*` list,
`dd0dtest` old+new, `reload0a/b`.

Lorenz-cia uses **official** cycle limits from VICE `c64-testlist.in` (e.g.
`cia1ta` = 190M cycles — 20M was a false TIMEOUT).

### c64m priority notes

- Harness: `build/run_c64m_cia_corpus` + `$D7FF` debugcart, BASIC keyboard-buffer RUN.
- No mid-race ICR polling.
- c64m does not yet select old vs new CIA models; the `cia_model` column is documentary.
- After Lorenz timer pipeline (2-cycle start delay, underflow-on-1 with skip-after-reload):
  **oneshot** (old+new) and **icr01new** PASS; several new-CIA irqdelay + CIA2 cases PASS.
- Still FAIL: `cia1tb123`/`cia2tb123` CRB write races, `icr01` (old), `imr`, `flipos`,
  old irqdelay matrix, `reload0`, `dd0dtest`.

## How to reproduce

```bash
./tools/cia-timing-corpus/fetch.sh   # once
./tools/cia-timing-corpus/run_x64sc.sh priority
./tools/cia-timing-corpus/run_x64sc.sh lorenz-cia
./tools/cia-timing-corpus/run_x64sc.sh cia-core

# c64m (build run_c64m_cia_corpus first)
cmake --build build --target run_c64m_cia_corpus
./tools/cia-timing-corpus/run_c64m.sh priority
```

## For the Option-2 Phase 4 agent

1. Start from `AGENT_PHASE4.md`.
2. Treat priority PASS as the minimum VICE oracle.
3. Consult `HARDWARE.md` when VICE and silicon may disagree.
4. Do not implement re-timing until you can show matching c64m results (or
   deliberate, documented one-cycle expectation updates that still pass the
   corpus intent).

## Not done yet

- Full `CIA/` tree including TOD + long `cia-sdr-icr` matrix (`run_x64sc.sh cia`)
- Cycle-stamped internal event logs (VICE + c64m instrumentation)
- c64m automated runs of the same PRGs
