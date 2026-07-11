# Priority cases for Option-2 Phase 4

These are the first acceptance cases a coding agent should green on c64m after
re-timing the CPU-visible interrupt line. All were run under local `x64sc`
(see `results/`).

## Tier 1 — interrupt delay and ICR races

| Case | Source | CIA model(s) | Why it matters |
|------|--------|--------------|----------------|
| `cia1tb123.prg` / `cia2tb123.prg` | Lorenz-2.15 | old + new | Canonical timer-B write 1–3 cycles race |
| `icr01.prg` / `icr01new.prg` | Lorenz-2.15 | old / new | ICR read during set |
| `imr.prg` | Lorenz-2.15 | old | Interrupt mask register behaviour |
| `flipos.prg` | Lorenz-2.15 | old + new | Oneshot/flip edge cases |
| `oneshot.prg` | Lorenz-2.15 | old + new | One-shot timer + IRQ |
| `CIA/irqdelay/*` | VICE CIA suite | old/new variants | Detects 1-cycle IRQ delay / CIA type |
| `CIA/dd0dtest/dd0dtest.prg` | VICE CIA suite | old + new | CIA #2 `$DD0D` path / NMI-related timing |
| `CIA/reload0/reload0a.prg`, `reload0b.prg` | VICE CIA suite | default | Reload-at-zero races |

## Tier 2 — broader CIA (after Tier 1 green)

- `CIA/ciavarious/cia*.prg` (and `*new` variants with matching `-ciamodel`)
- `CIA/cia-timer/*`
- `CIA/CIA-AcountsB/*` (Timer A counts B)
- `CIA/shiftregister/*` (serial + ICR interactions; long)
- Lorenz remaining CIA PRGs (`cia1ta`, `cia1tb`, `cia1tab`, PB6/PB7, …)

## Tier 3 — cycle-stamped logs (not pass/fail alone)

Only when “PASS/FAIL” is insufficient:

1. Instrument VICE: log ICR latch set, IRQ pin assert/deassert, ICR read-clear + cycle counter.
2. Instrument c64m at the same logical points.
3. Diff logs for Tier 1 races.

Do **not** sample via external ICR reads.

## VICE baseline (this machine)

See latest:

```text
md-files/corpus/cia-timing/results/x64sc-priority-*.tsv
```

Expected: all Tier 1 rows `PASS` with exit `0` under VICE 3.10 `x64sc`.
