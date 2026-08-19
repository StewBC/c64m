# Testing and verification

## Baseline

When verification is explicitly authorized, run from the repository root:

```sh
ctest --test-dir build --output-on-failure
```

The current baseline is 69/69 passing, including the longer real 1541 ROM/IEC,
G64, Arkanoid, Robocop, mid-transfer 1541 snapshot paths, CPU flight recorder,
guarded breakpoints, frame/VIC rings, and localhost control integration tests.
Do not rebuild or rerun the suite merely to validate these handoffs when another
agent is actively working in the tree; use the owner-provided baseline unless
explicitly asked to verify it.

Native framebuffer coverage is split across layers:

- `c64_frame` pins the central 16-colour Pepto palette, the internal `0xff`
  unpainted sentinel, indexed-to-ARGB expansion, PAL transparent padding, and
  frontend X rotation.
- `c64_vicii`, `c64_boot_progression`, `runtime_frame`, and
  `runtime_frame_ring` assert native `indexed8` metadata and pixel values.
- `frame_ring_control_integration` checks all 16 indices, every visible
  indexed/ARGB pixel pair, PAL's deeper ~827-slot ring, and large nonblocking
  response delivery.
- Framebuffer Stage 3 was additionally gated against a frozen binary: complete
  and partial PAL/NTSC wire payloads were byte-identical in both formats, as was
  Edge of Disgrace checker frame 7271.

### Tests that need gitignored media SKIP, not fail

Copyrighted disk/tape/CRT media under `assets/` is gitignored, so it is absent on
clean checkouts, git worktrees, and CI. Tests that require such a file check for
it up front and return `C64M_TEST_SKIP` (77) when it is missing; those tests are
marked `SKIP_RETURN_CODE 77` in `CMakeLists.txt`, so CTest reports them as
**Skipped** rather than **Failed**. See the helper `tests/test_asset.h`. The
asset-gated tests are `c64_snapshot_1541_midload`, `c64_disk_load`,
`c64_real_1541_load`, `c64_robocop_g64`, `c64_arkanoid_g64`,
`c64_arkanoid_alt_g64`, `d64`, `t64`, `runtime_disk`, and
`runtime_real_1541_autorun`. `d64` and `t64` still run their in-memory parser
subtests and only skip the fixture-backed ones. On a machine with `assets/`
present the full suite runs; a run showing these ten as Skipped is a missing-asset
environment, not a regression.

## High-value test groups

- Machine/CPU/bus: `c64_bus`, `c64_cpu_validation`, `c64_boot_progression`,
  `c64_vicii`, `c64_cia`, `c64_keyboard`, `c64_snapshot`.
- SID/audio: `sid`, `audio_buffer`, `runtime_scheduler`.
- Disk/IEC/media: `d64`, `c64_disk_load`, `c1541`, `c1541_gcr`, `c1541_media`,
  `g64`, `c64_real_1541_load`, `c64_robocop_g64`, `c64_arkanoid_g64`,
  `runtime_disk`, `runtime_real_1541_autorun`.
- Runtime/UI/platform: `runtime_*`, `frontend_input`, `frontend_joystick`,
  `help_view`, `platform_fs`, `app_options`, `control_protocol`.
- Tools/util: assembler tests, `disasm_6502`, `symbol_table`, `t64`, `crt`,
  `basic_v2`, and `paste_parser`.

CPU flight-recorder coverage is split deliberately across layers:

- `c64_cpu_observer`: begin/access/complete semantics, interrupt and deferred
  paths, host traps, and cycle-step versus instruction-step trace equality.
- `runtime_history`: arena retention, eviction, lifecycle, filters, opcode
  patterns, paging, context, and cursor semantics.
- `runtime_history_wire`: bounded little-endian HST1 encoding.
- `runtime_flight_recorder`: runtime ownership, reset/mutation ordering,
  recording controls, epochs, timelines, partial records, and allocation
  failure.
- `history_control_integration`: real localhost C64M/3 framing and cursor
  invalidation against a headless emulator. Environments that sandbox localhost
  must grant loopback access for this test.

## PAL line geometry and mid-line register timing

Three `c64_vicii` tests pin the work that produced the 32/320/32 PAL viewport.
They are in-process and deterministic - no snapshot, no PRG, no control port -
and each was confirmed to fail with its fix reverted:

- `test_live_d011_mode_write_resolves_at_vice_edge` - a same-cycle Phi2 `$D011`
  ECM/BMM store must land at VICE's mid-cycle edges: dots 0..3 of the previous
  span keep the old mode, dots 4..7 take the new one, and the span composed
  during the store cycle is stale throughout. Reverting the resolution leaves
  the old foreground where black is required.
- `test_live_full_line_paints_pre_wrap_band` - VIC X 496..503 must be composed
  content, not begin-frame fill. It changes `$D020` mid-frame so fill and paint
  are different colours; restoring a 384-column paint window makes it fail.
- `test_live_viewport_left_border_is_32_dots` - X 496..503 plus X 0..23 are all
  border and X 24 is display, which is what makes the frontend viewport
  32/320/32 instead of lopsided. A paint-anchor change cannot silently move the
  viewport without tripping this.

The Edge of Disgrace checker scene stays a **manual** oracle comparison, not a
test: it needs a current-version `.c64state` (v11+; paint buffers are not stored)
and `assets/` is gitignored. Before any c64m-vs-VICE pixel compare, match VIC-II
models (`-VICIImodel 6569` — see `vice-oracle.md`).

## Focused workflows

- Use `--help` for a non-blocking binary smoke test.
- Use `--headless --control-port PORT` for automated runtime/control-port checks.
- Use the recorder profilers serially to avoid thermal/contention noise:
  `profile_c64_hotloop 20000000` with observer modes, then
  `profile_runtime_hotloop 3 config-off` / `full`, and
  `profile_history_query` for a full 256 MiB query store.
- Use `tools/capture_sid_audio.py` and `tools/compare_sid_audio.py` for audio
  fidelity changes.
- Use the CIA corpus in `md-files/corpus/cia-timing/` for race-level CIA work;
  it is evidence, not a full ctest gate.
- Use the VIC trace build and `C64M_VICLOG`, `C64M_BALOG`, `C64M_SPRDMA` for
  `lft-nine` or sprite/raster investigations.
- For Edge of Disgrace visual regression checks, use `build/eod_regression_capture`
  with `roms/system.rom`, `roms/character.rom`, `roms/1541.rom`,
  `assets/disks/EdgeOfDisgrace_0.d64`, and
  `assets/disks/EdgeOfDisgrace_1a.d64`. It swaps at `$020C`, reaches the checker
  marker at `$A3BD`, then captures live turbo-2 (max) frames. Optional scene values
  are `checker`, `plasma`, or `+RACE_FRAMES`; optional sample count and interval
  write separated frames. Treat turbo-3 (warp) captures as debug geometry only.
  Optional
  `EOD_DUMP=<path>` writes a VIC/sprite/matrix dump after the first sample.
  Fine-checker bar: no mono column at x=24 (solid B0C pad), `ones@24 ≈ 50%`,
  seam 23/24 = 0; moving double-pixel lattice is intentional. Top/bottom black
  bars must be solid at x=0 (no 1px previous-`$D020` stub). See `eod-handoff.md`
  for scene landmarks, the XSCROLL pipe fix, and HBLANK color_latency drain.
- **VICE vs c64m on `assets/prg/` games:** follow `vice-oracle.md`. Collection
  PRGs need VICE `-autostartprgmode 1` and `-autoload "<path.prg>"` (IRQ after
  inject starts the game). Do not use a plain small-PRG autostart for those.

## Known gaps

There is no local exhaustive Harte undocumented-opcode corpus. Perfect analog/chip
revision behavior, full cycle-perfect video/audio, broad fast-loader compatibility,
and several UI dialogs remain outside automated coverage or milestone scope.

## Documentation reconciliation

Some older `md-files` documents are implementation plans or historical failed
attempts. Current status documents also contain occasional stale deferred bullets.
When they disagree, inspect the source and tests first; record the reconciled result
in `agents/`, and leave `md-files/` unchanged for this task.
