# C64MFEAT_PADDLE_6 — Paddle / 1351-mouse potentiometer input

## Status of this document

Implementation guide. Agent-ready for the SID-side POT read model; host-input
mapping needs a small design decision. Feature #6 of the "next features" list.

**Milestone scope:** Deferred (`docs/status/SID.md`, `docs/status/DEFERRED.md`:
"Paddle/potentiometer behavior is not connected; `$D419/$D41A` return 0xFF").
Niche — enables paddle games (Arkanoid-style, Pong) and, with more work, the 1351
proportional mouse. Lower priority than #1–#5.

## Required reading before starting

1. `AGENTS.md`.
2. `STATUS.md`.
3. `docs/status/SID.md` — POTX/POTY (`$D419/$D41A`) currently hardwired to 0xFF.
4. `docs/status/CIA.md` — the paddle fire buttons and paddle *select* line come
   through the control ports (CIA #1); the SID only reads the pot value.
5. This document.

## Goal

Provide analog potentiometer values on `$D419` (POTX) and `$D41A` (POTY) so
paddle-based software reads meaningful positions, and route the paddle fire
buttons through the existing joystick/control-port model. Optionally extend to the
1351 mouse (proportional mode) as a follow-on.

## Non-goals

- No 1351 mouse in v1 (proportional mouse encodes movement as pot deltas and is a
  larger, separate design — see Open Questions).
- No 1350 "joystick-mode" mouse.
- No analog SID-timer emulation of the POT charge cycle beyond what is needed to
  return a stable value.

## Current state (verified against source)

- SID POT reads are stubbed:
  `case 0x19u: return 0xFFu;  /* POTX: not connected */` and
  `case 0x1Au: return 0xFFu;  /* POTY: not connected */`
  in the SID register read switch (`src/machine/sid.c:193-194`).
- The SID struct (`src/machine/sid.h`) has no pot-value fields.
- Joystick/control-port input already flows machine-side via
  `c64_set_joystick()` (`src/machine/c64.c:1210`) into CIA #1 port pull-downs
  (`src/machine/c64.c:919-920`); paddle fire buttons map onto the same control-port
  bits (left/right button = the two joystick fire-ish lines), so the fire path can
  reuse existing plumbing.
- Host controller input is read in `src/main.c` (`sdl_c64_controller_read_inputs`,
  `:1551`); there is currently no analog-axis → pot mapping.

## Hardware model (what to emulate)

- Each SID exposes two pot inputs (POTX/POTY), read as an 8-bit value 0–255
  proportional to the RC charge time of the paddle potentiometer.
- Two paddle pairs exist, one per control port, but **both ports' pots are read by
  the single SID's POTX/POTY** — the CIA #1 port selection bits (PRA of CIA #1,
  the same register that scans the keyboard/joystick) choose *which* control
  port's pots are currently connected to the SID. Software toggles the select
  bits and reads `$D419/$D41A` for each port in turn.
- For emulator purposes, v1 can model this as: the SID returns the pot value for
  the port currently selected by CIA #1 PRA bits 6–7 (verify exact select bits
  against a reference; commonly bit 7/6 of `$DC00`).

## Implementation phases

### Phase 1 — Pot value storage + wired reads
- Add pot fields to the SID (or, cleaner, to the machine and pass the selected
  values into the SID read): e.g. `uint8_t pot_x[2], pot_y[2];` indexed by control
  port. Recommended: store the four values on the **machine** (`c64_t`) and have
  the SID read call receive the currently-selected `pot_x/pot_y` (avoids the SID
  needing to know about CIA port selection, keeping SID a pure device).
- Replace the `0xFF` returns at `src/machine/sid.c:193-194` with the selected
  value. If the SID must stay ignorant of selection, add a `sid_set_pot(sid*,
  uint8_t x, uint8_t y)` setter that the machine updates whenever CIA #1 PRA
  select bits change, and return those in the read.

### Phase 2 — Machine API + CIA select wiring
- Add `c64_set_paddle(c64_t *m, unsigned port /*1..2*/, uint8_t x, uint8_t y)` and
  `c64_set_paddle_button(...)` (or fold buttons into `c64_set_joystick` using the
  fire/second-fire lines). Update the SID-visible pot value when either the
  paddle value or the CIA #1 PRA select bits change.
- Confirm the CIA #1 PRA select-bit semantics and update the SID-visible pot pair
  on writes to `$DC00`.

### Phase 3 — Runtime command + client API
- `RUNTIME_COMMAND_SET_PADDLE` in `src/runtime/runtime_command.h`, dispatched in
  `src/runtime/runtime_thread.c` (mirror `RUNTIME_COMMAND_SET_JOYSTICK:47` /
  the dispatch at `:2810`). Client wrapper
  `runtime_client_set_paddle(client, port, x, y, buttons)` in
  `src/runtime/runtime_client.{c,h}`.

### Phase 4 — Host input source
- Map a host input to pot X/Y. Options (pick one for v1):
  - **Mouse absolute position** within the emulated screen rect → X/Y 0–255
    (most intuitive for paddle games). Read SDL mouse motion in the `src/main.c`
    event loop and call `runtime_client_set_paddle`.
  - **Game-controller analog stick** axes → X/Y (reuse
    `SDL_GameControllerGetAxis` already called at `src/main.c:1560`).
  - Recommended v1: mouse position for pot values, left/right mouse button for
    the two paddle fire buttons, gated by an enable hotkey/INI so it does not
    interfere with normal use.

## Tests / smoke checks

- **SID read unit test** (extend `tests/machine/test_sid.c`): set pot values,
  read `$D419/$D41A`, assert the selected-port value is returned; assert the
  default (no paddle connected / feature off) still returns `0xFF` so existing
  behavior is preserved.
- **Select-bit test:** set different pots on port 1 vs 2, toggle CIA #1 PRA
  select bits, assert the SID read follows the selection.
- **Smoke (manual):** `timeout 12 ./build/c64m --prg <paddle game>`; move the
  mouse and confirm the paddle tracks.

## Docs to update on completion

- `STATUS.md` — paddle input capability.
- `docs/status/SID.md` — POTX/POTY now return live values; update the "Paddle
  reads ... return 0xFF" invariant.
- `docs/status/CIA.md` — note pot-select bit handling.
- `docs/status/DEFERRED.md` — remove/replace the paddle line; add "1351 mouse
  proportional mode" and "analog POT RC-timing" as still deferred.
- `docs/status/TESTING.md` — new tests + smoke.

## Open questions / decisions for the author

1. **Where pot state lives.** Recommended: on the machine (`c64_t`), pushed into
   the SID via a setter, so the SID stays a pure device unaware of CIA selection.
2. **Exact CIA #1 select bits.** Verify which `$DC00` bits select port 1 vs port 2
   pots against a hardware reference before wiring.
3. **Host mapping.** Recommended: mouse position → pot value, mouse buttons →
   fire, behind an enable toggle. Confirm this does not clash with any future GUI
   mouse use inside the SDL window.
4. **1351 mouse.** Decide whether proportional-mouse support is in scope later;
   it reuses the pot path but encodes *movement deltas* into pot values with a
   specific wraparound protocol — treat as a separate follow-on guide.
