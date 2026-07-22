# lft-nine debugging handoff

This note is general guidance for investigating timing, alignment, and colour
artefacts in the LFT demos (including `lft-nine.prg`).

## c64m run

Build the current tree, then launch the GUI emulator with a control socket. Keep
the normal window enabled: do **not** add `--noini` or `--headless`.

For example:

```sh
./build/c64m --turbo=2 -a -P -p agents/demo/lft/lft-nine.prg \
  --control-port=17652
```

Start a monotonic stopwatch immediately before/at process launch. If a demo's
reproduction instructions specify a wall-clock stop (the lft-nine reproduction
used 32 seconds), that timing applies to c64m in turbo mode 2 only. Send a
debugger pause through the control port; do not quit the emulator. A small
timing margin is normal because the pause request is serviced by the main loop.
If the demo has already completed, the run was not stopped at the intended time
and the capture is not useful.

Once paused, identify a stable marker for the scene under investigation. This may
be a raster address, a code address, or a loop entered once per frame. For one
LFT scene, `$9B00` was a useful raster marker; it is only an example, not a
universal breakpoint. Resume and stop repeatedly at that marker to capture
consecutive frames. This is more repeatable than repeatedly pausing by
wall-clock time and lets the pattern move through its phases. Capture enough
frames to cover at least one complete visual cycle.

The control protocol is documented in `agents/control-port.md`. Keep one client
connection per session where possible. Do not use mode 3/warp for visual timing
work: warp disables live pixel output and changes the relationship between host
time and emulated progress.

## VICE comparison

Use the installed VICE binary, or build the instrumented source tree described in
`agents/vice-oracle.md`:

```sh
/Applications/vice-arm64-gtk3-3.10/bin/x64sc -model c64 \
  -autostart agents/demo/lft/lft-nine.prg \
  -moncommands /private/tmp/lft-vice.mon
```

For this demo, run VICE at **normal speed** (no `-warp`), because c64m's turbo
wall-clock timeline and VICE's warp timeline do not correspond. Do not compare
the two emulators by elapsed seconds. Instead, use a monitor breakpoint or
another deterministic raster/PC marker to locate the same visual scene, then
capture a consecutive frame strip. Keep one remote-monitor connection for the
whole VICE process; connecting enters the monitor, disconnecting resumes the
emulator, and reconnecting can wedge the session. A monitor command file is often
the safest way to set breakpoints, run, screenshot, and quit.

VICE's `draw_colors8()` implementation in `src/viciisc/vicii-draw-cycle.c` is a
useful oracle: it resolves buffered colour tokens after the CPU Phi2 store and
models the 6569's one-pixel latency. If behaviour differs, instrument that path
and the corresponding c64m cycle functions rather than relying on wall-clock
alignment.

## Strip capture and analysis

For each stop, save the full framebuffer with a frame number and emulator frame
counter. Keep a JSON record containing the counter, raster/PC state, and output
path. A contact sheet makes it much easier to see a transient ghost than a single
screenshot. Also make a nearest-neighbour crop of the top border around the
rightmost digit; inspect several adjacent frames, since the bad colour can move
or only appear for one phase of the animation.

Keep captures in a temporary directory with a consistent naming scheme that
includes emulator, sequence number, and frame/cycle counter. Retain the metadata
alongside the images. A contact sheet and small nearest-neighbour crops are often
more useful than a single screenshot. Temporary paths and filenames are only
diagnostic conveniences; do not make later agents depend on a particular `/tmp`
artifact surviving.

## Source investigation

When a mismatch is found, compare the emulators at the same deterministic
marker—not at the same wall-clock time. Trace the complete path from CPU writes
through VIC-II cycle setup, pixel composition, colour-token/pipeline handling,
and frame publication. VICE source is particularly useful for determining whether
a register write is resolved immediately, after Phi2, or through a delayed token
ring. Instrument both implementations at cycle/raster granularity before making
a timing change.

Add a focused test for the smallest reproducible sequence, including overlapping
sprites or layers that share an RGB value; colour identity can matter even when
the rendered colour is identical. Then run the complete test suite and retain a
before/after strip for visual verification.
