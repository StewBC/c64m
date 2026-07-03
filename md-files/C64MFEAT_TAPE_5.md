# C64MFEAT_TAPE_5 — Datasette / `.TAP` tape support

## Status of this document

Implementation guide. Scoped with a clear approach; the full-signal path needs a
design decision (trap-load vs. real datasette emulation) before it is fully
agent-ready. Feature #5 of the "next features" list.

**Milestone scope:** Out of the current milestone (the milestone lists PRG, D64,
and generic CRT loaders; tape is not mentioned). Land as a compatibility
follow-on. Lower priority than #1–#4.

## Required reading before starting

1. `AGENTS.md`.
2. `STATUS.md`.
3. `docs/status/DISK_IO.md` — existing host-file loader conventions.
4. `docs/status/CIA.md` — CIA #1 handles the datasette lines (motor, read/write,
   sense) on the real hardware; relevant only for the real-datasette approach.
5. `docs/status/CPU_MACHINE.md` — KERNAL trap mechanism and startup load flags.
6. This document.

## Goal

Broaden loader compatibility to tape-only titles by supporting tape images. Two
common container types exist; pick based on the chosen approach:

- **`.T64`** — a *file archive* (not a real tape signal). Already effectively
  supported: `t64_extract_first_prg()` (`src/tools/t64/t64.h:23`) is wired into
  the loader at `src/runtime/runtime_thread.c:2007` and loads the first PRG like a
  normal program.
- **`.TAP`** — a *pulse-level tape dump* (real datasette timing). Not supported.
  This is the actual gap.

## Non-goals

- No tape *writing* / recording in v1.
- No support for exotic turbo-tape formats beyond what the chosen approach yields
  for free.
- No `.T64` changes beyond what already works (optionally add directory selection
  for multi-file `.T64`, see Open Questions).

## Current state (verified against source)

- `.T64` path: `runtime_path_has_extension(path, "t64")`
  (`src/runtime/runtime_thread.c:2007`) → `t64_extract_first_prg()` → loaded as a
  PRG. Only the *first* PRG is used; multi-file `.T64`s expose one file today.
- `.TAP`: no references anywhere (`grep -ri tap src` finds only `t64`/unrelated).
- KERNAL LOAD trap exists (`c64_try_kernal_load_trap`, `src/machine/c64.c:554`)
  and is the model for a "fast" tape load.
- CIA #1 models the ports used by the datasette but there is no datasette device,
  motor control, or read-line pulse source today (`docs/status/CIA.md`).

## Two implementation strategies

### Strategy A — Trap-based fast tape load (recommended v1, small)
Do **not** emulate datasette timing. Instead, parse the `.TAP` pulse stream
offline into file(s) and inject them like the existing PRG/`.T64` path.

- Problem: a raw `.TAP` is a stream of pulse-width bytes, not files. To get a PRG
  out of it you must **decode the Commodore ROM tape format** (leader tone, sync,
  countdown, data bytes with parity, the tape header block giving name + load
  address + end address, then the data block). This decoder turns pulses → header
  + payload bytes.
- Once decoded, reuse the LOAD trap / direct-inject path: place bytes at the
  header's load address and set the KERNAL end pointers, exactly like the D64 LOAD
  trap does via `c64_kernal_load_return(...)`.
- Pros: no timing model, no CIA changes, works for standard-format tapes (the
  majority of BASIC/PRG tapes). Cons: fails for turbo/custom loaders that use
  nonstandard pulse encodings (those need Strategy B).

### Strategy B — Real datasette emulation (larger; defer)
Model the datasette as a device that converts `.TAP` pulse widths into transitions
on the CIA #1 tape read line, gated by KERNAL motor control, and let the real
KERNAL/loader read it cycle-accurately. This handles turbo loaders but requires:
new datasette state on the machine, motor/sense line wiring in CIA #1, a
pulse→cycle scheduler tied to the master clock, and PLAY/STOP transport control.
Substantial; document as a later phase.

## Implementation phases (Strategy A)

### Phase 1 — `.TAP` container + pulse decoder tool
- New `src/tools/tap/tap.{c,h}` (mirrors `t64` structure and CMake target):
  ```c
  typedef struct tap_prg { uint16_t load_addr; uint8_t *data; size_t len; char name[17]; } tap_prg;
  /* Parse .TAP v0/v1 header (signature "C64-TAPE-RAW", version, pulse data). */
  int tap_open(const uint8_t *bytes, size_t len, tap_image *out);
  /* Decode standard ROM tape format from the pulse stream into the first program. */
  int tap_decode_first_prg(const tap_image *img, tap_prg *out);
  void tap_prg_free(tap_prg *out);
  ```
- Implement `.TAP` v0/v1 parsing (12-byte signature, version, 4-byte length,
  then pulse bytes; a 0 byte in v1 introduces a 3-byte long pulse). Convert pulse
  widths → bit stream using standard C64 short/medium/long pulse thresholds, then
  decode leader/sync/bytes/parity → header block and data block.

### Phase 2 — Loader integration
- Add `.tap` handling next to the `.t64` branch
  (`src/runtime/runtime_thread.c:2007`): decode, then inject as PRG using the
  same mechanism `.t64`/PRG loads already use.
- Add `--tap <file>` CLI flag (mirror `--prg`/`--basic` in `src/app_options.c` /
  `src/main.c`) and drag/drop routing in `handle_drop_file`
  (`src/main.c:2937`), plus optional `--autorun` support (buffer-inject `RUN\r`
  like the existing PRG autorun, `docs/status/CPU_MACHINE.md`).

## Tests / smoke checks

- **Tool unit tests** `tests/tools/test_tap.c`: parse a known-good small `.TAP`
  (standard format), assert decoded name/load address/length and that the decoded
  payload byte-matches the equivalent PRG. Include a malformed/short-file case
  that fails cleanly.
- **Loader smoke (manual):** `timeout 12 ./build/c64m --tap standard_game.tap
  --autorun` and confirm it reaches the program. Note which turbo-loader tapes
  fail (expected — Strategy A limitation).

## Docs to update on completion

- `STATUS.md` — `.TAP` (standard format) load capability.
- `docs/status/DISK_IO.md` (or a new tape note) — `.TAP` decoding, `--tap`, limits.
- `docs/status/DEFERRED.md` — add "real datasette / turbo-tape timing (Strategy B)"
  as explicitly deferred; note `.TAP` write is deferred.
- `docs/status/TESTING.md` — new tests + smoke.

## Open questions / decisions for the author

1. **Strategy choice.** Recommended v1: Strategy A (trap/decode inject). Confirm
   the maintainer accepts that turbo-tape titles will not load until Strategy B.
2. **`.T64` multi-file.** Optional adjacent win: expose all directory entries of a
   `.T64` (currently only first PRG via `t64_extract_first_prg`). Decide whether to
   add a selection UI/`,N` suffix now or defer.
3. **Standard-format coverage.** Confirm which pulse thresholds / tape formats to
   target (Commodore ROM loader is the baseline). Turbo formats are out for v1.
