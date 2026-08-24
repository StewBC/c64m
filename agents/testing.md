# Testing and verification

## Baseline

From the repository root:

```sh
ctest --test-dir build --output-on-failure
```

77 tests are registered in the root `CMakeLists.txt`. Do not treat an older
count in a comment or changelog as current.

### Asset SKIP, not fail

Copyrighted media under `assets/` is gitignored. Tests that need a fixture
return `C64M_TEST_SKIP` (77) when it is missing; CMake marks those tests
`SKIP_RETURN_CODE 77`. Helper: `tests/test_asset.h`.

The ten asset-gated tests:

```text
c64_snapshot_1541_midload  c64_disk_load  c64_real_1541_load
c64_robocop_g64  c64_arkanoid_g64  c64_arkanoid_alt_g64
d64  t64  runtime_disk  runtime_real_1541_autorun
```

`d64` and `t64` still run in-memory parser cases and skip only fixture-backed
parts. A run that reports those ten as Skipped is a missing-asset environment,
not a regression.

## High-value groups

- Machine/CPU/bus: `c64_bus`, `c64_cpu_validation`, `c64_cpu_observer`,
  `c64_boot_progression`, `c64_vicii`, `c64_cia`, `c64_keyboard`, `c64_snapshot`
- SID/audio: `sid`, `audio_buffer`, `runtime_scheduler`
- Disk/IEC: `d64`, `g64`, `c1541`, `c1541_gcr`, `c1541_media`, `c64_disk_load`,
  `c64_real_1541_load`, `c64_robocop_g64`, `c64_arkanoid_g64`,
  `c64_arkanoid_alt_g64`, `runtime_disk`, `runtime_real_1541_autorun`
- Runtime/control: `runtime_*`, `control_protocol`,
  `history_control_integration`, `run_to_raster_control_integration`,
  `guarded_breakpoint_control_integration`, `frame_ring_control_integration`,
  `vic_ring_control_integration`, `inspector_control_integration`
- Frontend: `frontend_input`, `frontend_joystick`, `help_view`, `window_title`,
  `crt_renderer`, `disasm_pc_lock`
- Tools/util: assembler tests, `disasm_6502`, `symbol_table`, `t64`, `crt`,
  `basic_v2`, `paste_parser`

Localhost control integration tests need loopback. Sandboxes that block
127.0.0.1 will fail them.

`test_assembler_opcode_matrix` is built but is **not** an `add_test` gate. After
opcode-table edits:

```text
cmake --build build --target test_assembler_opcode_matrix
./build/test_assembler_opcode_matrix
```

## PAL geometry pins (`c64_vicii`)

These are in-process and deterministic. Each was confirmed to fail with its fix
reverted:

- `test_live_d011_mode_write_resolves_at_vice_edge` — same-cycle `$D011`
  ECM/BMM: dots 0..3 keep the old mode, 4..7 take the new one.
- `test_live_full_line_paints_pre_wrap_band` — VIC X 496..503 is composed
  content, not begin-frame fill.
- `test_live_viewport_left_border_is_32_dots` — X 496..503 plus X 0..23 are
  border and X 24 is display (32/320/32).

Edge of Disgrace checker remains a **manual** VICE compare. Match models
(`-VICIImodel 6569`) first. See `vice-oracle.md`.

## Focused workflows

- Binary smoke: `./build/c64m --help`
- Automation: `./build/c64m --headless --control-port PORT`
- SID: `tools/capture_sid_audio.py` and `tools/compare_sid_audio.py`
- CIA races: `tools/cia-timing-corpus/` (optional fetch into
  `external/cia-timing-corpus/`; not a ctest gate)
- VIC traces: build with `C64M_VIC_TRACE` and use `C64M_VICLOG`, `C64M_BALOG`,
  `C64M_SPRDMA`, `C64M_LINELOG` / `C64M_LINELOG_FULL`
- EoD capture: `build/eod_regression_capture` with `roms/system.rom`,
  `roms/character.rom`, `roms/1541.rom`, and the Edge of Disgrace D64s under
  `assets/disks/`. Fine-checker bar: no mono column at x=24, `ones@24` about
  50%, seam 23/24 = 0. Top/bottom black bars solid at x=0. Turbo 3 captures
  are debug geometry only.
- `assets/prg/` vs VICE: `vice-oracle.md` (`-autostartprgmode 1`, `-autoload`,
  `-VICIImodel 6569`)

## Performance

Turbo **2** (max: free-run, live pixels) is the throughput bar for full
correctness. Turbo 3 may drop paint; do not turn warp paint-off into a general
free-run shortcut.

Measure serially (contention and thermal noise dominate):

```text
./tools/bench_core_mhz.sh 20000000
./build/profile_c64_hotloop 20000000
./build/profile_runtime_hotloop 3 config-off
./build/profile_history_query
```

`profile_c64_hotloop` flags (any order after cycle count): `no-video`, `1541`,
`1541-one`, `media`. Pure-core cannot measure runtime/recorder/rings. Unpowered
1541s are not stepped; a powered drive with ROM can dominate free-run.

Absolute MHz is host-specific. Re-measure on the same class of machine after
performance work. Kill with `ctest` plus the demos that exercise the changed
path. Do not land VIC/CPU changes on bench-only evidence.
