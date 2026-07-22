# Testing and verification

## Baseline

When verification is explicitly authorized, run from the repository root:

```sh
ctest --test-dir build --output-on-failure
```

The repository owner reports the current agent baseline as 51/51 passing, including
the longer real 1541 ROM/IEC, G64, Arkanoid, and Robocop paths. Do not rebuild or
rerun the suite merely to validate these handoffs when another agent is actively
working in the tree; use the owner-provided baseline unless explicitly asked to
verify it.

## High-value test groups

- Machine/CPU/bus: `c64_bus`, `c64_cpu_validation`, `c64_boot_progression`,
  `c64_vicii`, `c64_cia`, `c64_keyboard`, `c64_snapshot`.
- SID/audio: `sid`, `audio_buffer`, `runtime_scheduler`.
- Disk/IEC/media: `d64`, `c64_disk_load`, `c1541`, `c1541_gcr`, `c1541_media`,
  `g64`, `c64_real_1541_load`, `c64_robocop_g64`, `c64_arkanoid_g64`,
  `runtime_disk`, `runtime_real_1541_autorun`.
- Runtime/UI/platform: `runtime_*`, `frontend_input`, `frontend_joystick`,
  `platform_fs`, `app_options`, `control_protocol`.
- Tools/util: assembler tests, `disasm_6502`, `symbol_table`, `t64`, `crt`,
  `basic_v2`, and `paste_parser`.

## Focused workflows

- Use `--help` for a non-blocking binary smoke test.
- Use `--headless --control-port PORT` for automated runtime/control-port checks.
- Use `tools/capture_sid_audio.py` and `tools/compare_sid_audio.py` for audio
  fidelity changes.
- Use the CIA corpus in `md-files/corpus/cia-timing/` for race-level CIA work;
  it is evidence, not a full ctest gate.
- Use the VIC trace build and `C64M_VICLOG`, `C64M_BALOG`, `C64M_SPRDMA` for
  `lft-nine` or sprite/raster investigations.
- For Edge of Disgrace visual regression checks, use `build/eod_regression_capture`
  with `roms/system.rom`, `roms/character.rom`, `roms/1541.rom`,
  `agents/demo/eod/EdgeOfDisgrace_0.d64`, and
  `agents/demo/eod/EdgeOfDisgrace_1a.d64`. It swaps at `$020C`, reaches the checker
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
