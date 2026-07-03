<!--
Implementation guide for c64m cartridge support.
Read order for implementers:
1. AGENTS.md
2. MASTER.md
3. STATUS.md
4. docs/status/README.md
5. docs/status/CPU_MACHINE.md
6. docs/status/DISK_IO.md
7. docs/status/FRONTEND_DEBUGGER.md
8. docs/status/DEFERRED.md
9. This document
-->
# c64mcrt.md
# CRT Cartridge Loading Implementation Plan

## Purpose

Add bounded `.crt` cartridge loading to c64m through drag/drop and the
Misc/Machine load UI without breaking the existing PRG, BASIC, T64, and D64
loader behavior.

Cartridges are not host-file memory injections. They are machine-visible
hardware attached to the expansion port. The implementation must therefore add
a real cartridge slot model to the machine memory map before the frontend load
paths can use it.

## Current Context

The current project supports:

```text
- PRG host loading through runtime memory injection.
- BASIC host loading through runtime memory injection plus BASIC pointer fixes.
- T64 convenience loading by extracting the first loadable entry as PRG-style bytes.
- D64 read-only mounting for devices 8 and 9.
```

The current machine bus has no cartridge state:

```text
- $8000-$9FFF falls through to RAM.
- $A000-$BFFF is BASIC ROM when normal C64 banking exposes BASIC.
- $D000-$DFFF is VIC/SID/color/CIA I/O or character ROM according to CPU port bits.
- $E000-$FFFF is KERNAL ROM when normal C64 banking exposes KERNAL.
- $DE00/$DF00 cartridge I/O areas are not routed to cartridge hardware.
```

`AGENTS.md` currently lists cartridge support as out of scope for the active
milestone. If this work is accepted into the milestone, update the scope notes
in `AGENTS.md`, `STATUS.md`, and the relevant component status files as part of
the final implementation phase.

## Goal

Implement practical `.crt` loading in incremental phases:

```text
1. Parse CRT files safely in tools code.
2. Support generic 8K and 16K cartridges in the machine memory map.
3. Expose runtime/frontend load paths for drag/drop and Misc/Machine loading.
4. Add debugger/status visibility and persistence only after basic loading works.
5. Add selected bank-switching mapper types later, one mapper per phase.
```

The first accepted user-visible target is ordinary generic cartridges that
autostart after reset.

## Implementation Status

Implemented:

```text
- Phase A tools-level CRT parser and generated parser tests.
- Phase B generic 8K/16K cartridge slot and CPU/debug Map bus mapping.
- Phase C runtime LOAD_CRT command and runtime_client_load_crt().
- Phase D drag/drop and Machine Load .crt routing.
- Phase F startup --crt command-line loading.
```

Still deferred:

```text
- Phase E copied cartridge status/detach UI.
- Phase F INI cartridge persistence.
- Phase G bank-switching mapper support.
```

## Resolved Implementation Decisions

These decisions answer expected implementation questions for the first coding
pass. Do not re-open them unless tests or a specific cartridge fixture proves
they are wrong.

```text
EXROM/GAME model:
    Implement a real first-class EXROM/GAME cartridge mode model in the machine
    bus, not a one-off "ROML/ROMH visible" shortcut. Phase B may support only
    the generic 8K and 16K modes, but the bus state should be expressed in terms
    of attached cartridge lines and resulting memory mode so Ultimax and later
    mappers can extend the same path.

RAM under cartridge ROM:
    Writes to $8000-$9FFF and $A000-$BFFF must write to RAM underneath while
    reads return mapped cartridge ROM. This matches the normal C64 shadow-RAM
    behavior and keeps self-modifying code/data assumptions sane after a
    cartridge is detached.

Snapshots:
    "Snapshot" in this document means the existing runtime/frontend copied
    display snapshots, not a persistent save-state system. No save-state
    serialization exists in the current codebase. If a real save-state feature
    is added later, it must serialize cartridge bytes, EXROM/GAME line state,
    selected bank, mapper state, and any cartridge RAM/flash state.

Machine lifecycle:
    Current runtime startup initializes one c64_t, and configuration changes
    call c64_set_config() on the existing machine followed by reset when needed.
    Cartridge attach state must survive normal c64_reset() and config-apply
    resets such as PAL/NTSC changes. c64_init() may clear cartridge state, but
    ordinary reset must not.

Test fixtures:
    Automated tests should use generated CRT byte fixtures only. Do not commit
    copyrighted cartridge images. Real local CRT files are optional manual
    smoke fixtures and are not required to implement Phases A-F.
    A local manual smoke fixture is currently available at:
    assets/crt/International Soccer (1983)(Commodore).crt
    The path intentionally contains spaces and parentheses; every UI, runtime,
    control, CLI, and smoke-test path must preserve it exactly instead of using
    whitespace-splitting.

Autostart verification:
    Automated proof is sufficient when tests show that reset with an attached
    cartridge sees the cartridge signature/vectors through CPU Map reads and
    begins execution through the cartridge reset/autostart path. Visual SDL
    confirmation is a manual smoke check, not a required automated test.

Extension routing:
    .crt is a new route in src/main.c. Keep .d64, .bas, .t64, .prg, and
    fallback behavior unchanged. At the time this guide was written, .t64 uses
    the PRG-style host load path.

Naming:
    Prefer RUNTIME_COMMAND_LOAD_CRT and runtime_client_load_crt unless local
    conventions have changed by the time implementation starts. Match existing
    D64/PRG command style over inventing a different naming scheme.
```

## Non-goals For The First Working Slice

Do not implement in the first cartridge slice:

```text
- every CRT cartridge type;
- EasyFlash, Action Replay, Final Cartridge, Ocean, Magic Desk, or other
  bank-switching mappers;
- freezer buttons;
- cartridge RAM, flash writes, or EEPROM persistence;
- full expansion-port electrical behavior;
- open-bus analog accuracy;
- UI for editing cartridge internals;
- mounted tape/Datasette behavior;
- changes to D64 drive semantics.
```

Bank-switching support should be added only after generic cartridges are tested
and documented.

## Architecture Rules

Respect the existing dependency direction:

```text
frontend -> runtime_client + platform + tools + util
runtime  -> machine + util
machine  -> util
tools    -> util
platform -> util + SDL2
```

Required ownership:

```text
- CRT file parsing belongs in tools.
- Live cartridge attachment and memory mapping belong in machine.
- File loading and command sequencing belong in runtime.
- Menus, dialogs, and drag/drop dispatch belong in frontend/main.
- SDL file dialogs remain platform/frontend concerns, not machine concerns.
```

No live machine pointer may cross to the frontend. Frontend cartridge state must
be copied through runtime snapshots/events.

## CRT Format Notes

The common `.crt` container has:

```text
- a file header beginning with "C64 CARTRIDGE";
- a big-endian header length;
- a big-endian cartridge hardware type;
- EXROM and GAME line defaults;
- a display name field;
- one or more "CHIP" packets;
- each CHIP packet contains packet length, chip type, bank number, load address,
  ROM image size, and ROM bytes.
```

Implement big-endian helpers explicitly and test them. Do not use ad hoc casts
against unaligned byte buffers.

For phase 1 and 2, accept only enough CRT structure to support generic 8K and
16K cartridges. Preserve parsed metadata for future mapper phases even if the
current machine code rejects unsupported hardware types.

The local manual fixture:

```text
assets/crt/International Soccer (1983)(Commodore).crt
```

has been observed to contain:

```text
header magic: C64 CARTRIDGE
display name: INTERNATIONAL SOCCER
hardware type: 0
CHIP load address: $8000
CHIP ROM size: $4000
```

That makes it a useful generic 16K smoke fixture after Phases B-D are in place.
Do not make unit tests depend on this file unless the project explicitly accepts
the asset/licensing implications. Generated CRT byte fixtures remain the default
for automated tests.

## Cartridge Memory Model Primer

The useful first modes are:

```text
8K cartridge:
    ROML visible at $8000-$9FFF.

16K cartridge:
    ROML visible at $8000-$9FFF.
    ROMH visible at $A000-$BFFF.

Ultimax cartridge, later:
    ROML may be visible at $8000-$9FFF.
    ROMH visible at $E000-$FFFF.
    Much of normal RAM/ROM visibility differs from standard C64 mode.
```

Generic cartridge support must model the `/EXROM` and `/GAME` line effects
well enough that the C64 reset/KERNAL autostart path sees the cartridge
signature and vectors at `$8000`.

The first implementation should compute cartridge memory mode from the attached
line state instead of hard-coding address checks at each read site. That keeps
the generic 8K/16K slice small while leaving a clean place for Ultimax and
mapper-controlled line changes.

## Phase A: Tools-Level CRT Parser

### Goal

Create a reusable CRT parser under `src/tools/crt/` with tests under
`tests/tools/`.

### Scope

Implement parser-only functionality:

```text
- validate the CRT file header;
- parse header length, version if present, hardware type, EXROM, GAME, and name;
- parse all CHIP packets;
- validate packet lengths and ROM sizes against the input buffer;
- expose chip type, bank, load address, size, and owned/copied ROM bytes or
  immutable slices with well-defined lifetime;
- return explicit result codes for malformed or unsupported files.
```

Suggested files:

```text
src/tools/crt/crt.h
src/tools/crt/crt.c
src/tools/crt/CMakeLists.txt
tests/tools/test_crt.c
```

### Non-goals

Do not attach to the machine, reset the C64, or implement memory mapping in this
phase.

### Tests

Use generated byte fixtures for exact coverage:

```text
- valid generic 8K CRT with one CHIP at $8000;
- valid generic 16K CRT with one 16K CHIP at $8000 or two 8K CHIP packets at
  $8000 and $A000, depending on fixtures chosen;
- bad magic;
- truncated file header;
- truncated CHIP header;
- CHIP packet length smaller than header;
- CHIP packet length exceeding file size;
- unsupported hardware type still parses but is classified unsupported for
  machine attach;
- unsupported load address is reported cleanly by validation helpers.
```

### Acceptance

The phase is complete when CRT parsing has deterministic tests and no dependency
on machine, runtime, frontend, SDL, or Nuklear.

## Phase B: Machine Cartridge Slot For Generic 8K/16K

### Goal

Add machine-owned cartridge state and generic cartridge memory mapping.

### Scope

Add a cartridge slot owned by `machine/`, likely in or near `c64_t` and
`c64_bus_t`. Keep the first data model simple:

```text
- mounted flag;
- cartridge hardware type;
- EXROM and GAME line state;
- derived cartridge memory mode;
- ROML bytes for $8000-$9FFF;
- ROMH bytes for $A000-$BFFF;
- optional display name for snapshots/debugging;
- unsupported/empty state after reset or detach.
```

Add machine API functions such as:

```c
bool c64_attach_generic_cartridge(
    c64_t *machine,
    const uint8_t *roml,
    size_t roml_size,
    const uint8_t *romh,
    size_t romh_size,
    bool exrom,
    bool game,
    const char *name,
    char *error,
    size_t error_size);

void c64_detach_cartridge(c64_t *machine);
```

Exact names are up to the implementation agent after inspecting local style.

### Bus Mapping Requirements

Update CPU bus reads and debugger Map reads:

```text
- $8000-$9FFF returns ROML when a generic 8K/16K cartridge maps ROML there.
- $A000-$BFFF returns ROMH for generic 16K cartridge mode when cartridge mapping
  selects ROMH there.
- normal RAM underneath is preserved.
- writes to ROML/ROMH address ranges do not mutate cartridge ROM.
- writes to ROML/ROMH address ranges do update RAM underneath.
- cartridge mode is derived from EXROM/GAME line state, even if only generic
  8K/16K modes are supported in this phase.
```

Keep raw RAM debug reads unchanged. Consider adding cartridge-aware ROM/debug
source behavior only in a later UI phase; Map mode must be correct immediately.

### Reset Behavior

Cartridge attachment should be followed by a C64 reset from runtime. The reset
must not erase the attached cartridge. Normal machine reset may clear RAM and
device state, but the cartridge slot should remain attached unless the user
explicitly detaches or a configuration change recreates the machine.

Current config apply uses `c64_set_config()` on the existing machine and resets
when requested. Treat those config-apply resets the same as ordinary resets:
the cartridge remains attached. If a future implementation genuinely recreates
`c64_t`, it must explicitly re-attach or intentionally detach the cartridge and
document that behavior.

### Tests

Add machine/bus tests:

```text
- no cartridge preserves existing banking behavior;
- 8K cartridge read at $8000 returns ROML;
- 8K cartridge does not expose ROMH at $A000;
- 16K cartridge read at $8000 returns ROML and $A000 returns ROMH;
- RAM underneath remains visible through raw RAM debug reads;
- debugger Map mode sees cartridge ROM;
- writes to cartridge ROM range do not corrupt cartridge bytes;
- writes to cartridge ROM range update raw RAM underneath;
- reset keeps the cartridge attached and mapped;
- config-apply reset keeps the cartridge attached and mapped;
- detach restores normal RAM/ROM banking.
```

### Acceptance

Existing CPU, bus, D64, PRG, BASIC, and T64 tests continue to pass. Generic
cartridge mapping is covered without needing frontend or runtime code.

## Phase C: Runtime CRT Load Command

### Goal

Load a `.crt` file on the runtime thread, attach it to the machine, reset, and
start or pause according to existing loader conventions.

### Scope

Add a runtime command and client helper:

```text
RUNTIME_COMMAND_LOAD_CRT
runtime_client_load_crt(...)
```

The runtime command should:

```text
1. Read the host file.
2. Parse it with the tools CRT parser.
3. Validate that the cartridge type is supported by the current machine phase.
4. Attach cartridge ROM to the live machine.
5. Reset the C64 with the cartridge still attached.
6. Publish machine state and either run or pause consistently with other load
   commands.
```

Recommended initial behavior: reset and run. Cartridge software normally uses
the reset/autostart path, unlike PRG injection.

### Error Handling

Publish clear runtime errors for:

```text
- file open/read failure;
- malformed CRT;
- unsupported cartridge hardware type;
- unsupported CHIP layout;
- unsupported load address;
- unsupported ROM size;
- allocation failure;
- attach failure.
```

Do not partially attach a cartridge after an error. If replacing an existing
cartridge fails, keep the previous cartridge attached unless tests choose and
document a safer all-or-nothing replacement behavior.

### Tests

Add runtime-level tests where practical:

```text
- LOAD_CRT command attaches a generated 8K CRT;
- LOAD_CRT resets with cartridge still visible;
- reset/autostart tests prove the CPU-visible map exposes cartridge signature
  and vectors at the addresses the KERNAL reset path checks;
- malformed CRT publishes an error and does not crash;
- unsupported mapper publishes an error;
- existing PRG/BASIC/D64 load commands still work.
```

### Acceptance

A generated generic CRT can be loaded without frontend code and the reset vector
path can see cartridge bytes through CPU Map reads.

## Phase D: Drag/Drop And Misc/Machine Load UI

### Goal

Expose `.crt` loading to the user through the requested entry points.

### Scope

Update drag/drop dispatch in `src/main.c`:

```text
- .d64 -> mount disk on device 8, unchanged;
- .bas -> BASIC load, unchanged;
- .crt -> runtime_client_load_crt;
- .prg/.t64/other -> existing PRG-style behavior, unchanged unless the UI
  already classifies the extension more specifically.
```

Update the Machine/Misc load path:

```text
- either add `.crt` auto-detection to the existing Load dialog execution path;
- or add a Cartridge Load button/row in the Machine tab.
```

Prefer auto-detection in the existing file picker for the first UI slice unless
the existing frontend layout clearly favors a separate Cartridge section.

### Dialog/User Feedback

On unsupported cartridge type or parse failure, surface the existing runtime
error event path. Do not add a new modal system unless the current error path is
insufficient.

### Tests / Smoke Checks

Manual smoke:

```text
- drag a generic 8K CRT onto the window; it resets and autostarts;
- load the same CRT through Misc/Machine/Load; it resets and autostarts;
- drag `assets/crt/International Soccer (1983)(Commodore).crt`; it resets and
  autostarts as a generic 16K cartridge;
- load `assets/crt/International Soccer (1983)(Commodore).crt` through
  Misc/Machine/Load; spaces and parentheses in the path are preserved;
- drag D64, BAS, PRG, and T64 files; existing behavior is unchanged;
- load malformed CRT; user sees an error and emulator remains usable.
```

Real cartridge images are useful here but optional for automated coverage. Use
generated test CRTs for automated tests, and use the International Soccer file
as the current manual smoke fixture when it is present locally.

### Acceptance

The user can load supported `.crt` files through both requested paths without
regressing existing file-type behavior.

## Phase E: Cartridge Snapshot, Status, And Detach

### Goal

Make cartridge state inspectable and controllable without adding mapper
complexity.

### Scope

Add copied runtime/frontend state:

```text
- cartridge mounted flag;
- display name;
- hardware type;
- EXROM/GAME line values;
- ROML/ROMH presence and size;
- last cartridge load result if useful.
```

This is only UI/debugger display state. It is not a persistent emulator
save-state format.

Add a detach/eject command if the UI needs one:

```text
RUNTIME_COMMAND_UNLOAD_CRT
runtime_client_unload_crt(...)
```

Detach should reset or not reset according to a documented UI decision. The
safest user-facing behavior is "detach and reset" because the C64 memory map
changes underneath running software.

### Debugger Considerations

The Hardware view can show cartridge status under Memory/Banks or a separate
Cartridge section. Avoid exposing live pointers. Map memory source should
already show cartridge-visible bytes from Phase B.

### Acceptance

The user can see whether a cartridge is attached, what kind it is, and can
return to a normal no-cartridge C64 state.

## Phase F: CLI And INI Persistence

### Goal

Optionally support startup cartridge loading and persistence.

### Scope

Add only after UI/runtime loading is stable:

```text
--crt <file>
```

Potential INI section:

```ini
[cartridge]
path=...
```

Persist paths using the same relative-path policy as disk queues. Do not persist
unsupported mapper state until the mapper can be restored correctly.

### Interaction Rules

Document precedence with existing startup options:

```text
- --crt should be mutually exclusive with --prg and --basic unless a concrete
  use case says otherwise.
- --disk may coexist with --crt.
- --autorun is unnecessary for CRT and should be ignored or rejected with a
  clear message when only --crt is supplied.
```

### Acceptance

Launching with `--crt game.crt` attaches the cartridge before reset and reaches
the cartridge autostart path.

Also smoke a path containing spaces and parentheses:

```text
--crt "assets/crt/International Soccer (1983)(Commodore).crt"
```

The stored INI path should round-trip using the existing relative-path policy
without losing or escaping literal spaces and parentheses.

## Phase G: Selected Mapper Support

### Goal

Add bank-switching cartridge types incrementally after generic cartridges work.

### Process

Each mapper gets its own mini-phase document or section before implementation.
For each mapper, specify:

```text
- CRT hardware type number;
- supported CHIP packet layout;
- initial EXROM/GAME state;
- ROML/ROMH mapping rules;
- bank-select register address or address range;
- read/write side effects;
- reset behavior;
- whether cartridge RAM exists;
- whether writes must be persisted;
- tests and known software fixtures.
```

Likely candidates, in a practical order:

```text
1. Ultimax-style generic mapping, if generic CRT fixtures require it.
2. Ocean type, common for many games.
3. Magic Desk, simple bank switching.
4. EasyFlash only if flash/write behavior is intentionally scoped.
5. Freezer/action cartridges only when freeze buttons and I/O behavior are
   explicitly accepted.
```

Do not implement a mapper from vague names alone. Require a fixture and a
written mapping spec.

## Phase H: Documentation And Status Updates

### Goal

Make the new support discoverable and keep handoff docs accurate.

### Required Updates

When implementation lands, update:

```text
md-files/STATUS.md
md-files/docs/status/CPU_MACHINE.md
md-files/docs/status/DISK_IO.md
md-files/docs/status/FRONTEND_DEBUGGER.md
md-files/docs/status/DEFERRED.md
manual/manual.md, if user-facing load instructions live there
```

If cartridge support becomes part of the active milestone, update
`md-files/AGENTS.md` scope text. If it remains a follow-on feature, keep the
scope text but note the implemented follow-on status in the appropriate handoff
files.

If a future real save-state feature exists by the time this phase is reached,
add cartridge serialization requirements to that feature's own status/plan
document. Do not invent a save-state format as part of generic CRT loading.

## Overall Acceptance Criteria

The complete generic cartridge feature is accepted when:

```text
- tools-level CRT parser tests pass;
- machine generic 8K/16K mapping tests pass;
- runtime LOAD_CRT tests pass or equivalent smoke coverage exists;
- drag/drop .crt loading works;
- Misc/Machine load path accepts .crt;
- unsupported CRT types fail with a clear error;
- existing PRG, BAS, T64, D64, disk queue, and reset behavior is unchanged;
- status docs describe supported and unsupported cartridge behavior;
- no forbidden dependency direction is introduced.
```

## Implementation Notes For Future Agents

Start narrow. A correct generic cartridge path is more valuable than a broad
mapper table with weak behavior.

Before editing code, inspect these areas:

```text
src/tools/d64/
src/tools/t64/
src/machine/c64.h
src/machine/c64.c
src/machine/c64_bus.h
src/machine/c64_bus.c
src/runtime/runtime_command.h
src/runtime/runtime_client.h
src/runtime/runtime_client.c
src/runtime/runtime_thread.c
src/main.c
src/frontend/frontend.c
tests/tools/
tests/machine/test_c64_bus.c
tests/runtime/
```

Keep tests generated and small where possible. Real cartridge images may be
useful for manual smoke checks, but committed tests should not depend on
copyrighted game ROMs.
