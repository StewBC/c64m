# Frontend, debugger, input, and configuration handoff

## Source of truth

UI code is in `src/frontend`, with integration in `src/main.c`; runtime-facing
interfaces are in `src/runtime/runtime_client.h`; platform services are in
`src/platform`. Tests cover input, joystick, options, filesystem helpers, and
protocol; much of the Nuklear UI remains manual smoke coverage.

The main integration loop is in `src/main.c`: SDL events become frontend intents,
intents become `runtime_client` commands, and `poll_runtime_events()` updates the
copied debugger state. A new UI action normally needs an intent type/field, a
runtime-client call, main-loop dispatch, and an event/snapshot update if the UI
must show completion.

## Current UI

The Nuklear frontend consumes copied runtime snapshots and provides Machine,
disassembly, memory, CPU/register, hardware, assembler, help, configuration,
disk/program/state, breakpoint, symbol lookup, and file-browser views. It never
reads live machine state directly. Memory views distinguish CPU map, raw RAM, ROM,
and 1541 map sources; debugger edits go through runtime commands.

Host file selection uses the in-app cross-platform browser backed by
`platform_fs`; it does not shell out to macOS scripts. Remembered browse directories
are stored in `[browse]`; the old `[state] quicksave_folder` is migrated and stripped
on save. State hotkeys are Opt+Shift+`>`/`<`.

## Input

SDL keyboard/controller input is translated to C64 keys/joystick through the existing
runtime choke points. Optional keyboard joystick layouts are `numpad` and `wasd`;
WASD keys are consumed only while assigned and C64 keyboard focus is active. Runtime
assignment shortcuts are Alt+Shift+1/2, with Alt+Shift+0 disabling; real controllers
remain Alt+1/2. SDL text input is enabled only while an edit field has focus.

The input path is `SDL event -> frontend_input/frontend_joystick_input ->
runtime_client_keyboard_key` or `runtime_client_set_joystick -> c64_set_key`/
`c64_set_joystick`. Do not write directly to CIA or keyboard state from frontend
code. Dialogs are modal: outside clicks must not focus or activate base views.

## Loading and configuration

Machine dialogs support D64 queue management, writable toggle, PRG/BASIC/BASIC Text
load/save, T64 host extraction, CRT attach, state save/load, ROM endpoint selection,
single combined-vs-split C64 ROM selection, video standard, audio, 1541 emulation,
and media mode. The Emulator configuration tab also owns frontend-only CRT
presentation: optional 4:3 pixel-aspect correction, scanlines with adjustable
strength, and screen curvature with adjustable amount. Scanline/curvature output
uses a second processed texture so the original SDL texture remains the fallback;
the same fitted texture selection is used by debugger and display-only views.
CRT controls preview their editable copy live; cancelling or closing Configure
restores the original presentation values. ROM changes apply by reboot/reload;
1541 emulation applies live.

Basic Text is stock BASIC V2 only. `util/basic_v2` tokenizes/detokenizes ASCII,
updates BASIC pointers, and preserves non-printable PETSCII in named/hex escapes.

## Help and assembler

`manual/manual.md` is compiled into help content by `tools/gen_help.py`; keep that
file ASCII/PETSCII-safe. The shared assembler supports the in-emulator path and the
`c64masm` CLI, scopes/segments, macros, conditionals, expression `*` as the current
instruction address, named output targets, and `C64MASM` predefined detection.

## Limits

The file browser has no Windows drive-letter enumeration UI. Undocumented opcodes
display as `.BYTE` in disassembly. Breakpoint Type timing during cycle stepping,
per-device Swap, and richer Tron trace management remain limited. Treat UI behavior
not covered by automated tests as manual-smoke territory.
