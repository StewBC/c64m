# Hardware tiebreaker notes

Source: [dmolinagarcia/c64ciaTests](https://github.com/dmolinagarcia/c64ciaTests)
(VICE CIA testprogs repackaged for real 6526 / 74HCT6526 runs).

Local clone (after `fetch.sh`):  
`external/cia-timing-corpus/c64ciaTests/README.md`

The README records per-test results on real hardware (OLD/NEW CIA columns where
filled). **This is ground truth when VICE and silicon disagree.**

## Interpretation caveats

- Many IRQ-delay rows (`14irq*`) are **blank** in the README (not yet filled in
  by the hardware author). Blank ≠ fail.
- Some tests are marked **KO on both OLD and NEW** (e.g. `06reload0a/b`). That
  often means the test expectation or setup does not match that particular
  hardware session, not that “real CIA is wrong.” Treat carefully; re-check
  before using as a fail gate.
- `14irq` is marked **KO** on hardware while our VICE `irqdelay.prg` baseline
  **PASS**es — record the tension; do not silently prefer VICE.

## Extracted highlights (from upstream README, partial)

| Hardware name | Maps roughly to | HW note (from README) |
|---------------|-----------------|------------------------|
| `01cmpnew` / `01cmpold` | CIA-AcountsB compare | OK for matching CIA type |
| `02newcias` / `02oldcias` | cia-timer | OK for matching CIA type |
| `06reload0a` / `06reload0b` | `CIA/reload0` | **KO / KO** on recorded hardware |
| `08cia01`–`08cia05` etc. | `CIA/ciavarious` | mixed OK/KO |
| `09dd0dtest` | `CIA/dd0dtest` | (blank in table) |
| `14irq` | `CIA/irqdelay/irqdelay.prg` | **KO** recorded |
| `14irq-*` family | other irqdelay variants | mostly blank |

Full table: keep reading upstream `c64ciaTests/README.md` rather than
re-copying every row here. When a Phase-4 agent needs a tiebreaker, open that
file and search for the numbered prefix (`14`, `08`, `06`, …).

## Policy for Phase-4 agents

1. Prefer tests that are **PASS on VICE** and **OK or blank on hardware** as
   primary gates.
2. If VICE PASS but hardware KO, **do not** force c64m to match VICE without
   investigation (source of test, CIA variant, power-up state).
3. If both VICE and hardware PASS/OK, a c64m FAIL is a real bug to fix.
4. Document every intentional deviation in `docs/status/CIA.md`.
