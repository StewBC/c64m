# C64MFEAT_NTSCSID_3 — NTSC SID rate tables

## Status of this document

Implementation guide. Agent-ready. Feature #3 of the "next features" list.

**Milestone scope:** In scope. The milestone goal is "both a PAL and NTSC C64 to
acceptable fidelity" with "SID audio present and recognizable." SID ADSR/envelope
and filter timing are currently hardcoded to the PAL CPU clock, so NTSC playback
runs envelopes and filter cutoffs at the wrong rate. Closing this is a genuine
NTSC-correctness fix, not new scope.

## Required reading before starting

1. `AGENTS.md`.
2. `STATUS.md`.
3. `docs/status/SID.md` — current SID model, ADSR tables, filter, fidelity phases.
4. `docs/status/AUDIO.md` — how SID samples are produced/scheduled.
5. `C64MSID.md` / `C64MAUDIO.md` — high-level SID/audio planning docs.
6. This document.

## Goal

Select SID envelope (attack/decay/release) rate tables and filter cutoff
coefficients appropriate to the active video standard's CPU clock, so NTSC
machines produce correctly-timed SID audio. Keep PAL output bit-identical to
today (no fidelity regression on the measured PAL baseline).

## Non-goals

- No new SID fidelity phase / no re-tuning of the PAL baseline (SID Phase 10,
  score 1.2838; see `docs/status/SID.md`). PAL numbers must not move.
- No 8580 support, no analog combined-waveform work (both deferred).
- No paddle work (see `C64MFEAT_PADDLE_6.md`).

## Background: the two clocks

- PAL C64 CPU clock: **985248 Hz**.
- NTSC C64 CPU clock: **1022727 Hz**.

(Both are documented in `src/machine/c64.h:86`.) The SID is clocked from the CPU
clock (Ø2). Envelope step rates and filter cutoff mapping are functions of that
clock. Today everything in `src/machine/sid.c` is derived at 985248 Hz.

## Current state (verified against source)

All PAL-locked constants live in `src/machine/sid.c`:

- `static const uint32_t s_attack_cycles[16]` (`src/machine/sid.c:9`) —
  header comment: "Attack time at PAL 985248 Hz: cycles per envelope +1 step"
  (`src/machine/sid.c:6`).
- `static const uint32_t s_decay_cycles[16]` (`src/machine/sid.c:18`) —
  "Decay/release at PAL 985248 Hz" (`src/machine/sid.c:15`).
- High-frequency rolloff coefficient derived from `.../985248` (`sid.c:29`).
- Filter cutoff coefficient `f = 2*sin(pi*fc/985248)` (`sid.c:33`).
- Envelope integration uses these tables directly:
  `v->env_counter += 1.0 / (double)s_attack_cycles[attack_rate]`
  (`src/machine/sid.c:321`) and the decay/release equivalents at `:334` and
  `:352`, scaled by `sid_exp_period()` (`sid.c:303`).
- SID is advanced one CPU cycle at a time from the machine:
  `sid_advance_cycles(&machine->sid, 1)` (`src/machine/c64.c:107`), and sampled at
  `sid_sample()` (`src/runtime/runtime_thread.c:284,300`).
- `sid_init()` (`src/machine/sid.c:89`) / `sid_reset()` (`:99`) currently take no
  clock/standard argument. The SID struct (`src/machine/sid.h`) has no stored
  clock field.
- Machine knows the standard: `c64_video_standard`
  (`src/machine/c64.h:61`, `NTSC=0`, `PAL=1`), field `video_standard`
  (`src/machine/c64.h:81`). VIC-II already selects PAL/NTSC tables from this
  (see `docs/status/VICII.md`: "per-standard PAL 6569 / NTSC 6567R8 tables"),
  which is the exact pattern to mirror.

## The physics: how the tables scale

The 16 attack/decay values are **cycle counts per envelope step**. The real SID's
step *periods* are fixed in absolute time (they come from a divider off the SID
clock). When the SID clock changes from PAL to NTSC, the number of CPU cycles per
step changes proportionally:

```
cycles_ntsc = round(cycles_pal * (1022727 / 985248))   ≈ cycles_pal * 1.03804
```

So the correct NTSC tables are the PAL tables scaled by the clock ratio.
Similarly, the filter cutoff coefficient denominator and the HF-rolloff
denominator must use the active clock. **Verify against a reference** (VICE
`resid`/`resid-fp` rate tables, or the standard SID attack/decay millisecond
tables) rather than only trusting the scalar — the recommended approach is:

1. Recompute each of the 16 attack and 16 decay/release values from first
   principles at 1022727 Hz using the same derivation the PAL comment describes,
   and
2. Confirm PAL values reproduce exactly with the existing derivation, so the two
   tables share one generator.

## Implementation phases

### Phase 1 — Parameterize SID by clock/standard
- Add a clock (or standard) to SID init:
  ```c
  void sid_init(sid *s, uint32_t cpu_clock_hz);   /* or c64_video_standard */
  ```
  Store `cpu_clock_hz` in the `sid` struct (`src/machine/sid.h`). Update
  `sid_reset()` to preserve/reapply it. Update the single caller in
  `src/machine/c64.c` (SID attach/init path near `c64_bus_attach_sid`) to pass
  `machine->video_standard`'s clock.
- Keep the API change minimal; `docs/status/SID.md` notes prior phases avoided
  struct/API churn, so document this as a deliberate, contained change.

### Phase 2 — Dual rate tables + clock-scaled coefficients
- Provide `s_attack_cycles_pal/_ntsc` and `s_decay_cycles_pal/_ntsc` (or a single
  base table plus a compile-time/runtime scale), selected by the stored clock.
- Replace the literal `985248` in the filter cutoff (`sid.c:33`) and HF-rolloff
  (`sid.c:29`) derivations with the stored clock. If those coefficients are
  precomputed once at init, recompute them in `sid_init` from the clock; if
  computed per-sample, read the stored clock.
- **PAL invariant:** when clock == 985248, every derived value must equal the
  current constants to the bit. Add a static assertion or a test that checks this
  so the PAL fidelity baseline cannot silently drift.

### Phase 3 — Verify sample production path
- Confirm nothing else in the audio path assumes a fixed SID clock
  (`runtime_update_sid_sample_output`, `src/runtime/runtime_thread.c:87`; host
  sample-rate resampling). The host output sample rate is independent; only the
  SID-internal cycle math changes. Note in `docs/status/AUDIO.md` if the
  cycles-per-host-sample ratio also depends on the CPU clock (it should already,
  via the master-cycle scheduler).

## Tests / smoke checks

- Extend `tests/machine/test_sid.c` (currently 60 tests per `docs/status/SID.md`):
  - **PAL-unchanged test:** init at PAL, assert the 16+16 tables and derived
    coefficients equal the pre-change constants (locks the baseline).
  - **NTSC-ratio test:** init at NTSC, assert each table entry ≈ PAL × 1.03804
    within rounding, and filter/rolloff denominators use 1022727.
  - **Envelope-duration test:** at a fixed rate index, measure cycles to reach
    full attack at PAL vs NTSC and assert the NTSC absolute time (cycles/clock)
    matches PAL absolute time within tolerance (same milliseconds, more cycles).
- **Audio regression:** re-run `tools/capture_sid_audio.py` /
  `tools/compare_sid_audio.py` in PAL and confirm the score is unchanged
  (`docs/status/SID.md` warns: do not claim fidelity change unless metrics
  support it). No NTSC reference capture is required to land this, but note its
  absence.

## Docs to update on completion

- `STATUS.md` — remove the "SID PAL-only ADSR" caveat from the baseline notes.
- `docs/status/SID.md` — update "Known limitations": NTSC rate tables now
  implemented; update "Important invariants" with the clock parameter.
- `docs/status/DEFERRED.md` — remove "NTSC SID rate tables are deferred"
  (currently under the SID section).
- `docs/status/TESTING.md` — new SID tests.

## Open questions / decisions for the author

1. **Table source of truth.** Recommended: derive both tables from one generator
   using the documented PAL derivation, parameterized by clock, so PAL is provably
   unchanged and NTSC is consistent. Alternative: hardcode a VICE-derived NTSC
   table. Pick the generator approach unless a reference table is required for
   exactness.
2. **Runtime standard switching.** Video standard can change via config-apply
   reset (`docs/status/CPU_MACHINE.md`). Ensure `sid_reset`/attach re-selects the
   correct clock on that path; add a test if standard-switch reinit is not already
   covered.
3. **Rounding policy.** Decide `round` vs `floor` for scaled cycle counts and
   document it; keep it consistent across all 32 entries.
