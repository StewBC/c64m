# C64MFEAT_KBDJOY_1 — Keyboard-mapped joystick emulation

## Status of this document

**IMPLEMENTED (2026-07-03).** See `docs/status/FRONTEND_DEBUGGER.md` for the
shipped behavior and `STATUS.md` for the handoff summary. Resolved decisions:

- Both layouts shipped, config-selectable: `numpad` (default, conflict-free) and
  `wasd` (steals W/A/S/D/Space while assigned). Fire = `KP_0` (numpad) / `Space`
  (wasd).
- Integrated with the existing controller port model rather than a parallel
  toggle: `Alt+Shift+1`/`Alt+Shift+2` assign/toggle the keyboard on port 1/2;
  `Alt+1`/`Alt+2` still map real controllers. (The guide's original `F11`
  suggestion was dropped — F9–F12 are already bound.)
- Core + persistence: `[input]` INI section + `--kbdjoy` / `--kbdjoy-layout`.
- New module `src/frontend/frontend_joystick_input.{c,h}` (bit values duplicated
  from `c64.h` to avoid a frontend→machine dependency). Combined with controller
  input at the `sdl_c64_controller_send_ports` choke point in `src/main.c`; no
  runtime/machine changes. Tests: `tests/frontend/test_frontend_joystick.c` and
  `[input]` coverage in `tests/test_app_options.c`.

The original guide text below is retained for context.

---

Implementation guide. Agent-ready. Feature #1 of the "next features" list.

**Milestone scope:** In scope. `AGENTS.md` lists "keyboard and joystick input
are usable through the real C64 input paths" as a milestone goal. Joystick input
today works only through real SDL game controllers; host-keyboard joystick is the
missing half of that goal.

## Required reading before starting

1. `AGENTS.md` — workflow, build/test rules, architecture/thread rules.
2. `STATUS.md` — current baseline.
3. `docs/status/CIA.md` — CIA #1 owns the joystick ports.
4. `docs/status/FRONTEND_DEBUGGER.md` — UI/input routing conventions.
5. This document.

## Goal

Let a user drive C64 joystick ports 1 and 2 from the host keyboard, so games are
playable without a physical USB gamepad. This is the single highest
value-to-effort feature: all machine-side and runtime-side plumbing already
exists; only host-side key→joystick mapping and a routing/mode policy are new.

## Non-goals

- No paddle/analog emulation (see `C64MFEAT_PADDLE_6.md`).
- No remapping UI beyond what is specified here (INI-configurable is enough).
- No change to the real SDL game-controller path.

## Current state (verified against source)

The full downstream path is already present and correct:

- Joystick bitmask enum: `src/machine/c64.h:66` —
  `C64_JOYSTICK_UP=0x01, DOWN=0x02, LEFT=0x04, RIGHT=0x08, FIRE=0x10`.
- Runtime client entry point (thread-safe, frontend-callable):
  `runtime_client_set_joystick(runtime_client *client, unsigned port, uint8_t inputs)`
  at `src/runtime/runtime_client.c:160`. It enqueues
  `RUNTIME_COMMAND_SET_JOYSTICK` (masks inputs to `0x1f`).
- Runtime thread applies it: `src/runtime/runtime_thread.c:2810` calls
  `c64_set_joystick(&machine, port, inputs)`.
- Machine applies it: `c64_set_joystick()` at `src/machine/c64.c:1210`
  (`port==1 -> joystick1`, else `joystick2`), consumed into the CIA port
  pull-downs at `src/machine/c64.c:919-920`.
- The SDL game-controller path already produces the exact `inputs` byte we need:
  `sdl_c64_controller_read_inputs()` at `src/main.c:1551` and
  `sdl_c64_controller_send_ports()` at `src/main.c:1543` call
  `runtime_client_set_joystick(client, 1u|2u, ...)`.
- The paste/"Type" breakpoint action already injects joystick via
  `PASTE_EV_JOYSTICK` (`src/runtime/runtime_thread.c:3336`) — proof the port and
  fire semantics are wired end to end.

**What is missing:** the host keyboard maps only to the C64 keyboard matrix.
`src/frontend/frontend_input.{c,h}` has **no** joystick concept
(`frontend_input_action_type` is only `NONE`/`KEY`/`RESTORE`, header at
`src/frontend/frontend_input.h:13`). Keyboard events flow through
`handle_keyboard_input()` (`src/main.c:2088`) →
`frontend_input_map_keyboard_event()` → `dispatch_input_actions()`, none of which
knows about joysticks. The main event loop dispatches key events at
`src/main.c:3026-3033`.

## Key design tension: keyboard keys are also C64 keys

Arrow keys, numpad, and most letters are legitimate C64 keyboard keys. A key that
becomes a joystick direction must be *removed* from the C64 keyboard path while
joystick mode is active, or software will see phantom keypresses. There must be an
explicit mode/routing policy.

### Recommended default (implement this; note alternatives in code comments)

- **Dedicated cluster, always joystick when active mode is ON.** Use the numeric
  keypad as an always-joystick cluster when keyboard-joystick is enabled:
  - `KP_8`=up, `KP_2`=down, `KP_4`=left, `KP_6`=right, diagonals
    `KP_7/9/1/3`, fire = `KP_0` or `Right Ctrl`.
  - These numpad scancodes are not commonly needed as C64 keyboard keys during
    gameplay, so they can be *unconditionally* consumed by the joystick when the
    feature is enabled, without a live toggle.
- **Active port selection + enable toggle** via a host hotkey that is not a C64
  key. Recommended: `F11` cycles `off → port 2 → port 1 → off` (port 2 first
  because most single-player games use port 2). Show current state in the window
  title (reuse `update_window_title` / `platform_window_set_title()` in
  `src/main.c`, already added for run-state; see `docs/status/CPU_MACHINE.md`).
- When keyboard-joystick is **off**, all keys route to the C64 keyboard exactly as
  today (zero behavior change).

Alternatives to mention but not implement now: (a) a full "swap" mode that steals
the arrow keys and reroutes the whole keyboard; (b) WASD+space layout. Keep the
numpad default because it avoids the routing-conflict problem entirely.

## Implementation phases

### Phase 1 — Frontend input model
- Extend `frontend_input_action_type` (`src/frontend/frontend_input.h:13`) with
  `FRONTEND_INPUT_ACTION_JOYSTICK`, or add a parallel small module
  `frontend_joystick_input.{c,h}` that maps `SDL_Scancode` → `(port_delta,
  c64_joystick_bit)`. Prefer a **separate module** to keep the existing keyboard
  mapper untouched and the dependency surface minimal.
- New module API sketch:
  ```c
  typedef struct kbd_joystick_state {
      unsigned active_port;         /* 0 = off, 1 or 2 = live */
      uint8_t  inputs;              /* current accumulated C64_JOYSTICK_* mask */
      /* scancode->bit table, loaded from config or defaults */
  } kbd_joystick_state;

  void kbd_joystick_reset(kbd_joystick_state *s);
  bool kbd_joystick_is_joystick_scancode(const kbd_joystick_state *s,
                                         SDL_Scancode sc);          /* consume? */
  /* returns true if the event changed joystick state (caller should send) */
  bool kbd_joystick_handle_key(kbd_joystick_state *s,
                               const SDL_KeyboardEvent *ev);
  void kbd_joystick_cycle_port(kbd_joystick_state *s);              /* F11 */
  ```
- Dependency check: this module lives under `src/frontend/` and may include
  `src/machine/c64.h` only for the `C64_JOYSTICK_*` enum (frontend → machine is
  **forbidden** by `AGENTS.md`). To stay legal, **duplicate the five bit values
  as local constants** or route them through an allowed header
  (`runtime_client.h` / a `util` header). Confirm the enum's current include
  path before choosing; do not add a `frontend -> machine` include.

### Phase 2 — Wire into the event loop
- In `src/main.c`, instantiate a `kbd_joystick_state` next to the existing
  `frontend_input_mapper` and `sdl_c64_controller_state`.
- At the top of key handling (`src/main.c:3026` for KEYDOWN,
  `:3030` for KEYUP), before `handle_keyboard_input(...)`:
  1. If the feature is on and `kbd_joystick_is_joystick_scancode()` is true,
     call `kbd_joystick_handle_key()`; if state changed, call
     `runtime_client_set_joystick(client, s.active_port, s.inputs)`; then
     **skip** the C64 keyboard path (do not fall through to
     `handle_keyboard_input`).
  2. Handle the `F11` enable/port-cycle hotkey (KEYDOWN, `repeat==0`) before the
     quit/help hotkey chain; call `kbd_joystick_cycle_port()` and update the
     window title. On disable, send `set_joystick(prev_port, 0)` to release.
- On focus-loss / port-change / quit, send `set_joystick(port, 0)` to avoid a
  stuck direction. There is an SDL `SDL_WINDOWEVENT_FOCUS_LOST` already reachable
  in the loop; clear joystick inputs there too.

### Phase 3 — Config persistence (optional but recommended)
- Add an `[input]` INI section handled in `src/app_options.c` (follow the disk/
  video section pattern; `config_get`/`config_set` helpers exist around
  `src/app_options.c:596-620`). Suggested keys:
  - `keyboard_joystick_default_port = 0|1|2`
  - `keyboard_joystick_layout = numpad|wasd` (only `numpad` implemented now)
- Plumb into `app_options` struct + `--kbdjoy <port>` CLI flag in
  `src/app_options.c` / `src/main.c` argument parsing (mirror `--video`).
- Persist on quit through the existing INI-save path (see the `[disk]`
  persistence mechanism referenced in `docs/status/DISK_IO.md`).

## Tests / smoke checks

- **Unit (host-side, no SDL window):** add `tests/frontend/test_kbd_joystick.c`
  (mirror an existing `tests/frontend/*` CMake target). Feed synthetic
  `SDL_KeyboardEvent`s and assert the accumulated `inputs` mask and
  `is_joystick_scancode` gating. Multiple directions should OR together; opposite
  directions are allowed to coexist (hardware permits it — do not filter
  UP+DOWN).
- **Smoke (manual, time-limited):** `timeout 8 ./build/c64m --prg <a joystick
  game>`; press `F11` to select port 2, verify numpad drives the game and that
  numpad keys do **not** appear as BASIC characters when joystick mode is on, and
  **do** type normally when off.
- Regression: existing keyboard tests and the real-controller path must be
  unaffected (feature defaults to off).

## Docs to update on completion (per AGENTS.md Phase Workflow)

- `STATUS.md` — one line under recent handoff notes.
- `docs/status/CIA.md` — note keyboard is now an additional joystick source.
- `docs/status/FRONTEND_DEBUGGER.md` — document the `F11` hotkey, numpad layout,
  and `[input]` INI keys.
- `docs/status/TESTING.md` — add the new test + smoke procedure.
- `docs/status/DEFERRED.md` — nothing to remove (keyboard-joystick was not
  previously listed as deferred; add a note only if you defer the WASD layout).

## Open questions / decisions for the author

1. **Hotkey choice.** `F11` is the recommendation; confirm it is not already
   bound (grep `SDLK_F11` / help/step keys in `src/main.c`). Fall back to a
   non-C64 key if it collides.
2. **Module vs. enum extension.** Recommended: separate `frontend_joystick_input`
   module to avoid touching the keyboard mapper. If the reviewer prefers a single
   input path, extend `frontend_input_action_type` instead and emit joystick
   actions alongside key actions.
3. **Bit-value duplication vs. shared header.** Resolve the `frontend -> machine`
   dependency rule before including `c64.h`. Duplicating five constants is the
   pragmatic legal choice; a shared `util` header is cleaner if one already
   carries input types.
