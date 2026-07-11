# CIA Interrupt-Timing Reference Corpus — Plan

**Purpose:** Build a trustworthy, non-perturbing cycle/race-level reference corpus for CIA (6526) ICR/IRQ timing, so that a future "Phase 4: pin/race-level interrupt timing" effort in c64m has an actual acceptance gate instead of guessing against datasheet prose.

**Status:** In scope. Scaffolding and VICE priority baseline live under
`md-files/corpus/cia-timing/` and `tools/cia-timing-corpus/`. Not a green light
to re-time c64m until the agent brief acceptance criteria are met.

---

## 1. Guiding constraint: don't build a self-corrupting corpus

Reading CIA ICR (`$DC0D` / `$DD0D`) clears its pending interrupt flags — on real hardware and in VICE alike. Any corpus-generation method that *polls* ICR/IRQ state mid-test via a remote/debug interface is not observing the race, it's participating in it and altering the outcome. This rules out naive "single-step + read register" harnesses as a source of ground truth for the race itself. They're fine for driving tests and reading final/settled results, not for sampling live state during the race window.

---

## 2. Primary corpus: VICE's existing CIA test suite

Don't hand-roll test programs. VICE already has a dedicated, maintained set:

- **Location (SVN/canonical):** `testprogs/CIA/` in the VICE source tree — browsable at `https://sourceforge.net/p/vice-emu/code/HEAD/tree/testprogs/CIA/`
- **Mirror (Git, easier to script against):** `https://github.com/libsidplayfp/VICE-testprogs` — same layout, `./CIA` directory alongside `./CPU`, `./VICII`, `./interrupts`, etc.
- **Notable individual tests to prioritize** (from the Lorenz-2.15 set and CIA-specific dirs): `icr01.prg`, `imr.prg`, `flipos.prg`, `oneshot.prg`, and the classic `CIA1TB123.prg` / `CIA2TB123.prg` (timer B, 1–3 cycles after writing CRB — a canonical race case).
- **Runner:** VICE's `testbench` script (documented at `https://vice-emu.pokefinder.org/wiki/Testbench`) already knows how to run these headlessly across VICE's emulator targets and report pass/fail/exitcode. Use this as the automation layer rather than writing a bespoke one.

**Why this over freehand generation:** these tests encode known-correct expected behavior, including specific documented races (e.g. VICE bug #2052 — CIA IRQ line should only become visible to the CPU when the *first* IRQ source activates, not on subsequent ones while already active). That bug shipped in VICE for years before being caught, which is a good reminder that even VICE's own model isn't an oracle — see §4.

---

## 3. Hardware-validated cross-check

- **Repo:** `https://github.com/dmolinagarcia/c64ciaTests` — the same VICE CIA testprogs, repackaged onto a single floppy image and actually run on real 6526/74HCT6526 hardware, with results recorded per test (old/new CIA, CIA1/CIA2).
- **Use:** when VICE's behavior and your intuition disagree, this is the tiebreaker — not VICE itself. Treat VICE as "very good reference," treat this repo's documented real-hardware results as ground truth where the two conflict.

---

## 4. Automation layer: what the sockets are for (and aren't)

Both VICE and c64m expose remote control sockets. Correct division of labor:

- **Use them for:** launching a test `.prg`, letting it run to completion or to a defined breakpoint/exit condition, and reading the *final* settled state (exit code, memory-mapped result byte, register dump after the race window has closed). This is exactly what VICE's binary monitor protocol and the `testbench` runner already do.
- **Don't use them for:** sampling ICR/IRQ mid-race by repeatedly reading `$DCxx`/`$DDxx` while the test executes — see §1.
- **Reference implementation worth reviewing (not necessarily adopting):** `https://github.com/barryw/vice-mcp` — an MCP server wrapping VICE's remote protocol, notable because it already added *execution tracing and interrupt logging hooks into the VICE CPU core itself* rather than relying on external polling. That's the right shape of solution for anything needing true cycle-by-cycle visibility.

---

## 5. If cycle-level trace logs are actually needed

For race cases where "did the test pass/fail" isn't enough and you need to know exactly which cycle an event happened on:

1. Don't poll from outside. Instead, patch VICE's own C source (GPL, so this is legitimate) to emit a log line at the relevant internal signal transitions (ICR latch set, IRQ pin assert, ICR read-clear) with the current cycle counter attached.
2. Build a debug target of VICE with this instrumentation, run the specific `testprogs/CIA/` case, capture the log.
3. Run the equivalent scenario on c64m via its own TCP control server (per `C64MCONTROL.md`), instrumented the same way at the same logical points.
4. Diff the two cycle-stamped event logs directly. This avoids the observer-effect problem entirely, since the logging is internal to each emulator's own execution rather than an external agent perturbing it.

---

## 6. Deliverable / acceptance gate

Once §2–§5 exist, "full cycle-exact re-timing" (Option 2 from the Phase 4 decision) has a real gate: a set of VICE `testprogs/CIA/` cases, cross-checked against `c64ciaTests` real-hardware results where available, with cycle-stamped logs from both VICE (instrumented) and c64m (instrumented) for the specific known race cases (`CIA1TB123`/`CIA2TB123`, ICR read-during-set, IRQ-line-visible-only-on-first-source).

**Progress (2026-07-11):** §2–§4 scaffolding is in place under `md-files/corpus/cia-timing/` and `tools/cia-timing-corpus/`. VICE `x64sc` baselines: priority 31/31, lorenz-cia 40/40, cia-core 66/66 PASS. Hardware notes summarized in `HARDWARE.md`. §5 cycle-stamped dual logs and c64m re-timing are still open; use `AGENT_PHASE4.md` for the coding step.
