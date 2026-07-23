# PAL horizontal border centering — RESOLVED

**Original investigation:** 2026-07-23 (commit `9f1ea9e`, backed out by `2d3521f`)
**Resolved:** 2026-07-23, commits `c80f487` → `cb4e28a` → Phase 4 frontend window
**Outcome:** PAL now presents VICE's **32/320/32** viewport. `VICII_PAL_FRAME_X_OFFSET`
and the whole "left pad" problem are gone.

This file previously argued that the 32/32 geometry could not be reached because
every way of filling the extra left 8 columns produced artefacts. That conclusion
was **wrong**, and the reasoning behind it is worth keeping because of *how* it
was wrong.

---

## What was actually broken

The geometry was right all along. VICE's PAL viewport really does start at
**VIC X 496**, eight dots before the line wraps to 0 — confirmed from
`vicii-draw.c` (`DBUF_OFFSET = 17*8 - leftborderwidth`, a monotone per-line
`dbuf`) and then measured directly: with `assets/prg/vic-calibration.prg` the
display window boundary lands at `dbuf` index 136 → VIC X 24, and the border
boundary at index 456 → VIC X 344. So `dbuf i0 ↔ X392`, exactly as the arithmetic
predicted.

What made 9f1ea9e look broken was a **separate, pre-existing bug**: a same-cycle
`$D011` ECM/BMM store did not reach the painted span until the following cycle.
On the EoD checker the bottom black frame line comes from `$D011=$60` (ECM+BMM →
invalid mode → black) stored at **cycle 11** — precisely the cycle that owns
X 496..503. The old 384-column crop began at X 0, immediately *after* that band,
so the defect was invisible until 9f1ea9e widened the window onto it.

Every "pad model" in the original log was therefore an attempt to paper over a
bug living somewhere else entirely. None of them could have worked.

Fixed in `78453ac`: `vicii_finish_cycle` now resolves same-cycle `$D011` changes
using VICE's 6569 edge model (`vmode11_pipe |=` at pixel 4, `&=` at pixel 6),
repairing both in-flight spans. See `agents/vicii.md`.

---

## Verification

Dot-for-dot against VICE 6569, EoD checker snapshot, raster 245:

| | X408..491 | X492..503 |
|--|-----------|-----------|
| VICE | 2 (84) | **0 (12)** |
| c64m | 2 (84) | **0 (12)** |

Both of this document's original kill criteria now pass through the real viewport:

- **Solid black frame lines.** Rows 245/246 are a single 384-wide run of colour 0
  — no 8-pixel foreign-colour bleed.
- **No first-column period error.** `left8 == mid8` on 0/193 checker rows at the
  fine phase. It does hit 193/193 at a coarse phase, which is the *legitimate*
  periodicity this document allowed for ("occasional equality by phase is OK") —
  a genuine duplication bug would show equality on every frame regardless of
  phase, and does not.

Boot-screen viewport measured at exactly `32 / 320 / 32`, with viewport column 0
= VIC X 496 and column 32 = VIC X 24. NTSC unchanged at `16 / 320 / 16`.

---

## Traps that cost real time here

**Match the VIC-II model before comparing anything.** VICE's default is an
**8565**; c64m models the **6569**. Their output differs by ~8 dots in exactly
this region, which silently invalidated two rounds of analysis and produced two
confident, wrong conclusions ("stores are 8 dots late", "stores are 12 dots
late"). The tell is single colour-15 dots at colour transitions — the 8565
grey-dot artefact. A `.vsf` only loads when the model matches, so a snapshot that
appears to load but leaves a blank/reset machine is a model mismatch.

**The text monitor has no snapshot command.** `load_snapshot` is silently ignored
— there is no snapshot token in the parser. Use the **binary monitor**, command
`0x42 UNDUMP`; `src/monitor/monitor_binary.c` is the reference.

**`assets/prg/vic-calibration.prg` jitters.** Its raster-poll loop starts the
colour staircase at a phase that varies run to run (injection timing), so it
cannot support dot-exact *store-timing* claims without a raster stabiliser. It is
still sound for what it was built for: the display/border boundaries it pins are
phase-independent, and it is what caught the model mismatch. Deterministic
absolute claims come from the snapshot fixtures.

**Frames publish only while free-running.** `run-cycles` steps the CPU but never
refreshes `get-frame`; and grabbing immediately after a snapshot load returns the
frame *stored in the snapshot*, not a live-rendered one. See
`lib_eod.py` in the session scratchpad for the drain-then-free-run recipe.

---

## Known remaining deviation

c64m assigns **X 392..407** to the end of a frame row where VICE assigns it to
the start of the next — a 16-dot, 2-cycle row-boundary offset. The 32/32 window
is X 496..503 + X 0..375, so this band is outside every viewport and appears only
in whole-line comparisons. Documented, not fixed.

---

## One-line summary

**The 32/32 geometry was always correct; what broke 9f1ea9e was a same-cycle
`$D011` mode-store landing a cycle late, hidden for years because the old crop
started exactly one dot past the affected band.**
