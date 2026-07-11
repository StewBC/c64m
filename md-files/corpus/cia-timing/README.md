# CIA interrupt-timing reference corpus

**Purpose:** Give a coding agent a non-perturbing, externally grounded acceptance
gate for **full Phase 4** CIA work (CPU-observable 6526 interrupt delay + ICR
races), without guessing from datasheet prose alone.

**Status:** In scope. Prerequisite for Option-2 Phase 4 re-timing (wiring
`cia_interrupt_line` into the CPU IRQ/NMI path with bit-exact expectations).
Does **not** by itself change c64m CIA timing.

**Plan source:** [`md-files/C64mCIATimingCorpusPlan.md`](../../C64mCIATimingCorpusPlan.md)

---

## 1. Hard rule: do not corrupt the race

Reading `$DC0D` / `$DD0D` **clears** pending ICR flags. Any harness that polls
ICR or IRQ mid-test via a remote monitor is participating in the race, not
observing it.

| Allowed | Forbidden |
|--------|-----------|
| Autostart PRG → wait for settled result (debugcart exit, border, result byte) | Mid-race remote `m dc0d` / ICR peek |
| Internal cycle-stamped logs of latch / pin / read-clear (instrumented emu) | Single-step + poll ICR as ground truth for the race window |
| Final register dump after the race window has closed | Treating VICE alone as absolute truth when hardware disagrees |

---

## 2. Layout

| Path | Role |
|------|------|
| `tools/cia-timing-corpus/fetch.sh` | Clone VICE-testprogs + c64ciaTests into `external/cia-timing-corpus/` |
| `tools/cia-timing-corpus/run_x64sc.sh` | Run suites under local `x64sc` (debugcart exit codes) |
| `tools/cia-timing-corpus/run_c64m.c` / `run_c64m.sh` | Same convention on c64m (`build/run_c64m_cia_corpus`) |
| `external/cia-timing-corpus/VICE-testprogs/` | Upstream PRGs (gitignored clone) |
| `external/cia-timing-corpus/c64ciaTests/` | Same tests + real-hardware notes (gitignored clone) |
| `md-files/corpus/cia-timing/results/` | Logged VICE + c64m baselines (checked in) |
| `md-files/corpus/cia-timing/HARDWARE.md` | Hardware tiebreaker summary |
| `md-files/corpus/cia-timing/PRIORITY.md` | Phase-4 priority cases and why |
| `md-files/corpus/cia-timing/AGENT_PHASE4.md` | What a Phase-4 coding agent should do with this corpus |
| `md-files/corpus/cia-timing/HANDOFF.md` | **Current session handoff** — status, matrix, next steps |

Default VICE binary: `/Applications/vice-arm64-gtk3-3.10/bin/x64sc`  
Override: `X64SC=/path/to/x64sc`.

---

## 3. How tests report results (VICE testbench convention)

Test programs write an exit code to the **debugcart** register (`$D7FF` on C64):

| Write | Meaning | `x64sc` process exit |
|-------|---------|----------------------|
| `$00` | PASS | `0` |
| `$FF` | FAIL | `255` |
| (none before limit) | TIMEOUT | `1` (with `-limitcycles`) |

Typical visual: green border = pass, red = fail, then debugcart write.

Runner flags (aligned with VICE `testbench/x64sc-hooks.sh`):

```text
-default -console -warp -debugcart -jamaction 1 -VICIIfilter 0
-limitcycles <N>
-ciamodel 0   # old 6526
-ciamodel 1   # new 8521 / 6526A-class
```

---

## 4. Authority order

1. **Real hardware** results from `c64ciaTests` (when documented for that case).
2. **VICE `x64sc`** pass/fail on the same PRG + CIA model (very good reference; not infallible).
3. **Datasheet / literature** only when both are silent.

If hardware and VICE disagree, prefer hardware and record the disagreement.

---

## 5. Commands

```bash
# one-time
./tools/cia-timing-corpus/fetch.sh

# priority race / IRQ-delay set (fast path for Phase 4)
./tools/cia-timing-corpus/run_x64sc.sh priority

# Lorenz CIA subset
./tools/cia-timing-corpus/run_x64sc.sh lorenz-cia

# full CIA/ tree from c64-testlist.in (long; includes TOD/SDR matrix)
./tools/cia-timing-corpus/run_x64sc.sh cia

# c64m (requires: cmake --build build --target run_c64m_cia_corpus)
./tools/cia-timing-corpus/run_c64m.sh priority
```

Results land in `md-files/corpus/cia-timing/results/{x64sc,c64m}-<suite>-*.tsv`.

TSV columns:

```text
status  exit  cia_model  path  prg  timeout  notes  x64sc
```

---

## 6. What is still *not* in this corpus package

- Cycle-stamped event logs (ICR latch set / IRQ pin assert / ICR read-clear +
  cycle index). Those need **instrumented** VICE and c64m cores (§5 of the plan).
- Automatic c64m runs of the same PRGs (future: control-port or debugcart-equivalent).
- Changing c64m CPU-visible IRQ timing (that is Option-2 Phase 4 implementation).

---

## 7. Definition of “corpus ready for Option-2 Phase 4”

Minimum gate:

1. Priority suite green on local `x64sc` (or failures explained + hardware-aligned).
2. Hardware notes consulted for known KO/OK cases (`HARDWARE.md`).
3. Agent brief (`AGENT_PHASE4.md`) followed: re-time only against these cases;
   update unit/integration expectations deliberately; keep FLAG/serial/PC seams.

Full gate (later):

4. Cycle-stamped logs for `cia1tb123` / `cia2tb123`, ICR read-during-set, and
   first-source-only IRQ visibility, from instrumented VICE and c64m, diffs clean.
