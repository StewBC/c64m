# Frontend, debugger, input, and help

## Source of truth

`src/frontend/`, integration in `src/main.c`, runtime-facing APIs in
`src/runtime/runtime_client.h`, platform in `src/platform/`. Automated
coverage: `frontend_input`, `frontend_joystick`, `help_view`, `forensics_view`,
`window_title`, `crt_renderer`, `disasm_pc_lock`. Most Nuklear UI is manual smoke.

SDL events become frontend intents, intents become `runtime_client` commands,
and `poll_runtime_events()` updates copied debugger state. A new UI action
needs an intent, a runtime-client call, main-loop dispatch, and an event
update if the UI must show completion. The frontend never reads live `c64_t`.

## Layout

Display | registers/disasm | memory | Misc. Memory sources: CPU map, raw RAM,
ROM, drive 8, drive 9. Debugger edits go through runtime commands.

Misc tabs (two rows of three): Machine, Debugger, Breakpoints / Hardware,
Assembler, Inspector. Inspector is the only Inspect entry.

Window title: `c64m - VIDEO - TURBO - STATE`. Modes render as `Normal`,
`Max`, `Warp`. Inspecting replaces the state with `Inspect`.

Host file selection uses `platform_fs`. Remembered browse directories live
in `[browse]`. State hotkeys: Opt+Shift+`>` / `<`.

## Inspector tab

Record off: Record checkbox only. Record on: Inspect plus window summary
(oldest / live / duration). In Inspect: Leave Inspector, `[-]` slider `[+]`,
cycle lines. Thumb-down is film preview or pink; release lands. Window
headers use dark cobalt while Inspecting; do not tint the window background.

Product rules and control honesty: `runtime-control.md`. Do not walk HST1
to place the slider. Inspector Record does **not** arm or stop HST1.

## Forensics

Full-window HST1 FIND surface (`forensics_view.*`), not a Misc tab and not a
Help-style CRT overlay. **Forensics...** on the Inspector tab and **Opt+R**
open it (pauses on enter). **Opt+R** / **Close** return to the entry surface
(CRT restores prior run state if it was running; debugger stays paused).
**F9** from Forensics always opens the debugger paused. **Esc** does not
leave. Mutually exclusive with Help.

Query line → structured `HISTORY_*` intents → `main.c` claim/decode →
transcript (`session_id = 0`; `history_close` on exit). Find options use
shared `runtime_history_parse_find_options` / public key tables. The query
line is **verb-first** (`find` / `next` / `read` / `info`); bare `key=value`
is not FIND (status = verb help, same string as Tab). Control-port
`history-find` still accepts bare keys. Tab is a grammar walker:
unique-complete or ASCII slot help; caret at end unique-expands every token
(explicit `edit.cursor` after rewrite). Transcript scroll is kept in
`transcript_scroll_y` and restored on re-open (Help `section_scroll_y`
pattern); Clear view resets it and does not call `history-clear`.

**Land before** / **Land exact** on a selected hit: quantized checkpoint
`<=` N vs `runtime_inspector_land_to_cycle`. Live + can enter → **Inspect &
Land** confirm then ENTER+land; soft-fail without checkpoints. Successful
land (any Inspect focus update used for the land status strip) leaves
Forensics like F9 (debugger paused; abandon CRT resume latch) and selects
Misc → Inspector. Cancel / soft-fail / incomplete land stay in Forensics.
Click selects a logical entry/block; **Copy** uses the full unwrapped text.
Double-click `id=` / `cyc=` / `pc=$...` copies that token. UI strings are
ASCII-only. User docs: `manual/manual.md` (**Forensics**).

## Input

`SDL event -> frontend_input` / `frontend_joystick_input ->`
`runtime_client_keyboard_key` or `runtime_client_set_joystick`. Do not write
CIA or keyboard state from frontend code. Dialogs are modal: outside clicks
must not focus or activate base views.

Keyboard joystick layouts: `numpad` and `wasd`. WASD is consumed only while
assigned and C64 keyboard focus is active. Assignment: Alt+Shift+1/2, Alt+Shift+0
disables; real controllers remain Alt+1/2. SDL text input is enabled only
while an edit field has focus.

## Loading and configuration

Machine dialogs: D64 queues, writable toggle, PRG/BASIC/BASIC Text, T64
extract, CRT attach, state save/load, ROM endpoints, video standard, audio,
1541 emulation, media mode. Emulator tab also owns CRT presentation (4:3
pixel-aspect option, scanlines, curvature) on a second processed texture.
Controls preview live; Cancel restores. ROM changes apply by reboot/reload;
1541 emulation applies live.

Basic Text is stock BASIC V2 only (`util/basic_v2`).

## Help

`manual/manual.md` is compiled by `tools/gen_help.py`. Before editing it,
read `manual/HELP_MARKDOWN.md` (ASCII, no links, help-renderer subset).

Help search highlights hits: inverse band on the jumped-to span, underline
on other visible occurrences. Matching is per drawn line inside
`help_draw_inline_at`. The band must be a real `nk_fill_rect` (`nk_convert`
drops `nk_draw_text` background colour). `help_estimate_span_y` only
estimates scroll; the render pass measures the row that holds the hit and
corrects. `help_view_search()` is what `test_help_view` drives.

## Assembler surface

In-emulator assembly exposes `dest="map"`, ignores `file=`, predefines
`AM65=0` and `C64=1`. `[assembler] auto_adjust_segments=yes` enables bounded
overlap-layout retries. Library details: `tools.md`.

## Limits

File browser has no Windows drive-letter enumeration UI. Undocumented
opcodes display as `.BYTE`. Breakpoint Type during cycle stepping,
per-device Swap, and richer Tron management remain limited.
