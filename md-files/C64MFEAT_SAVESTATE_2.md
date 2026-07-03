# C64MFEAT_SAVESTATE_2 — Save / load machine state (snapshots)

## Status of this document

Implementation guide. Mostly agent-ready; contains a required scoping pass
(enumerate all live machine state) that the author must complete before coding.
Feature #2 of the "next features" list.

**Milestone scope:** Out of the current PAL/NTSC fidelity milestone as a stated
criterion, but it does not violate any scope limit and is the single most-expected
emulator feature that is entirely absent. Treat it as the top post-milestone
feature. Do not let it block milestone completion claims.

## Required reading before starting

1. `AGENTS.md` — especially the Thread Ownership and Snapshot rules (critical).
2. `STATUS.md`.
3. `docs/status/CPU_MACHINE.md` — machine ownership, reset/boot, banking.
4. `docs/status/VICII.md`, `docs/status/CIA.md`, `docs/status/SID.md` —
   per-subsystem live state that must be serialized.
5. `docs/status/DISK_IO.md`, `c64mcrt.md` — mounted-disk/cartridge state.
6. This document.

## Goal

Serialize the entire live machine to a file and restore it later
("quicksave/quickload" and named slots), so users can resume games and sessions.
Distinct from the existing debugger "snapshots," which are read-only display
copies, **not** a full resumable machine image.

## Non-goals

- No cross-version snapshot compatibility guarantee (embed a version; refuse
  mismatches). No migration tooling.
- No compression in v1.
- No snapshotting of host peripherals (audio device, window). Only the machine.
- Not required to capture a running 1541 CPU/VIA state in v1 unless
  `emulate_1541` is on; see Open Questions.

## Critical architecture constraints (from AGENTS.md)

- **The live machine exists only on the runtime thread.** Serialization and
  deserialization of live state **must run on the runtime thread**, driven by new
  runtime commands. The frontend must never read or write live machine memory.
- Frontend requests save/load via `runtime_client_*` → `RUNTIME_COMMAND_*`; the
  runtime thread performs the file I/O against the live `c64_t`, or hands a copied
  buffer back to the frontend for the frontend to write. Prefer **runtime-thread
  file I/O** to avoid copying the whole machine across the thread boundary; the
  runtime already does file I/O for ROM/disk loads
  (`runtime_load_rom` at `src/runtime/runtime_thread.c:794`).
- Machine → runtime/frontend/platform includes remain forbidden. The serializer
  belongs in `machine/` (it needs the `c64_t` layout) and is *invoked* by the
  runtime thread.

## Current state (verified against source)

- The machine struct is `c64_t` (`src/machine/c64.h`). It owns CPU
  (`machine->cpu.cpu`), bus/RAM (`machine->bus.ram`), banking, VIC, CIA #1/#2,
  SID (`machine->sid`), drive slots (`machine->drives[]`,
  `machine->drive8/drive9`), cartridge state, `joystick1/joystick2`,
  `video_standard`, master cycle counter, and the CPU bus-trace scratch.
- Reset path exists (`c64.c`, `c64_set_joystick` neighbours reset joystick to 0 at
  `src/machine/c64.c:1065-1066`), which shows the fields that must be
  reinitialized/overwritten on load.
- ROM contents are loaded from external files
  (`runtime_load_configured_roms`, `src/runtime/runtime_thread.c:815`). ROMs are
  **not** part of RAM; decide whether to serialize ROM images or require the same
  ROM set at load time (see Open Questions).
- Runtime command/event model: commands enumerated in
  `src/runtime/runtime_command.h` (e.g. `RUNTIME_COMMAND_SET_JOYSTICK:47`),
  dispatched in `src/runtime/runtime_thread.c` (see the `switch` around
  `:2810`). Client wrappers in `src/runtime/runtime_client.{c,h}`.

## Mandatory first step: state inventory

Before writing any serializer, produce a complete inventory of everything inside
`c64_t` that constitutes resumable state, classified as:

- **Serialize** (mutable live state): all RAM (`bus.ram`, incl. RAM under I/O and
  color RAM), CPU registers + internal cycle state, banking latch
  (`$0000/$0001` processor port), full VIC-II register file + internal raster/BA/
  sprite sequencer state, both CIAs' full register + latch/counter/ICR/TOD state,
  SID register file + all decoded/mutable oscillator/envelope/filter state,
  master cycle counter, `joystick1/2`, `video_standard`, cartridge attach + shadow
  RAM + EXROM/GAME lines, per-drive mount metadata (which image, which queue
  index, read pointers).
- **Reconstruct-or-reference** (large/external): ROM images, mounted D64 image
  bytes. Prefer storing a *reference* (path + hash) and re-reading at load; embed
  bytes only if you want fully self-contained snapshots (Open Question).
- **Do not serialize** (host/derived/scratch): SDL, renderer, audio device,
  debugger view copies, the CPU bus-trace scratch buffers (`pending_cpu_trace`,
  `last_cpu_trace`) — these are regenerated.

Cross-reference each subsystem's `docs/status/*.md` "Important invariants"
section while building the inventory; those list the internal state that must
survive a round trip (e.g. VIC "models the vertical border as state",
SID "23-bit LFSR", CIA "timer latch/live counters").

## Implementation phases

### Phase 1 — Versioned serializer in `machine/`
- New files `src/machine/c64_snapshot.{c,h}`:
  ```c
  #define C64_SNAPSHOT_MAGIC  0x63363453u  /* 'c64S' */
  #define C64_SNAPSHOT_VERSION 1u
  /* Serialize live state into a caller-owned buffer; returns bytes written or 0. */
  size_t c64_snapshot_save(const c64_t *m, uint8_t *out, size_t out_cap);
  size_t c64_snapshot_size(const c64_t *m);
  /* Restore into an existing, ROM-loaded machine. Returns false on version/format
     mismatch or size error, leaving the machine unchanged on failure. */
  bool   c64_snapshot_load(c64_t *m, const uint8_t *in, size_t in_len);
  ```
- Use an explicit **chunked, tagged** format (FourCC + length per subsystem:
  `CPU_`, `RAM_`, `VIC_`, `CIA1`, `CIA2`, `SID_`, `CART`, `DRV8`, ...). This makes
  version evolution and partial diagnostics tractable and avoids relying on raw
  `struct` layout (do not `memcpy` whole structs containing pointers).
- **Never serialize pointers.** For anything referenced by pointer (drive image
  bytes, ROM sets), store an index/path/offset, not the address.
- Endianness: write little-endian explicitly, or document host-only snapshots.
  Given the project targets macOS/Windows x86-64/ARM64 (little-endian), a
  documented host-endianness format is acceptable for v1; prefer explicit LE if
  cheap.

### Phase 2 — Runtime commands + client API
- Add `RUNTIME_COMMAND_SAVE_STATE` / `RUNTIME_COMMAND_LOAD_STATE` to
  `src/runtime/runtime_command.h` with a path payload (mirror how disk/ROM paths
  are carried).
- Add `runtime_client_save_state(client, path)` /
  `runtime_client_load_state(client, path)` to `runtime_client.{c,h}`.
- Dispatch in `src/runtime/runtime_thread.c`: on the runtime thread, **pause the
  live machine at an instruction boundary** (loads mid-instruction are unsafe),
  call `c64_snapshot_save/load`, do the file I/O, then resume the prior run state.
  Emit a result event so the UI can report success/failure (reuse the existing
  event mechanism used for load errors).

### Phase 3 — Frontend/CLI hooks
- Hotkeys in `src/main.c` event loop (choose non-C64 keys; e.g. `F5`=quicksave,
  `F7`=quickload; verify no collision with step/help keys). Default slot path next
  to the INI (reuse `app_options_path_relative_to_ini`,
  `src/app_options.c:308`).
- Optional: a "State" section in the frontend UI (see
  `docs/status/FRONTEND_DEBUGGER.md` for the intent/config pattern) with named
  slots. Optional CLI: `--load-state <file>` applied after ROM load + reset.
- Drag/drop: extend `handle_drop_file` (`src/main.c:2937`) to route a snapshot
  extension (e.g. `.c64state`) to load-state.

## Tests / smoke checks

- **Round-trip unit test** `tests/machine/test_c64_snapshot.c`: build a machine,
  run N cycles, `c64_snapshot_save` → mutate → `c64_snapshot_load` → assert full
  equality of the inventoried fields (RAM, CPU regs, VIC/CIA/SID state, cycle
  counter). This is the core correctness gate.
- **Version-reject test:** corrupt magic/version and assert `load` returns false
  and leaves the machine unchanged.
- **Determinism test:** two machines fed the same input, one snapshotted +
  restored mid-run, must produce identical subsequent frames/samples.
- **Smoke (manual):** `timeout 10 ./build/c64m`, run a program, quicksave,
  perturb, quickload, confirm exact resume.

## Docs to update on completion

- `STATUS.md` — new baseline capability line.
- `docs/status/CPU_MACHINE.md` — snapshot format + runtime-thread ownership.
- `docs/status/FRONTEND_DEBUGGER.md` — hotkeys / UI / CLI.
- `docs/status/TESTING.md` — new tests + smoke.
- `docs/status/DEFERRED.md` — record anything intentionally excluded (e.g. 1541
  sub-state, compression, cross-version compat).

## Open questions / decisions for the author

1. **ROM/disk bytes: embed vs. reference.** Recommended v1: store ROM set
   identity (paths + hashes) and mounted-image paths + queue index by reference;
   refuse load if the referenced ROMs/images are unavailable or hash-mismatched.
   Embedding makes snapshots portable but large. Decide before Phase 1.
2. **1541 emulation state.** When `emulate_1541=1`, the drive has its own CPU/VIA
   state (`docs/status/IEC1541.md`). v1 may snapshot only the C64 side and
   document that a load resets the drive; or fully capture it. Recommended: defer
   full-drive capture, document it.
3. **Format stability.** Confirm whether snapshots must survive across builds
   (then the chunked/tagged format + strict versioning is mandatory) or are
   throwaway within a session (looser). Recommended: strict versioning from day 1.
4. **Instruction-boundary guarantee.** Confirm the runtime already exposes a
   clean "paused at instruction boundary" state to hook into (it does for
   stepping / BRK auto-pause, `docs/status/CPU_MACHINE.md`); reuse that rather
   than inventing a new pause point.
