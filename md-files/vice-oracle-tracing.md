# Using VICE as a cycle-level oracle for c64m

This records how the Arkanoid/Robocop G64 timing bug (see `C64mG64-fix.md`) was
pinned down by running a local VICE build headless and diffing its behaviour
against c64m. It is the fastest known way to get a trustworthy, same-rate
reference trace, and the technique generalises to any IEC / drive / CIA / VIC
timing question. Keep it for the next time a loader "almost" works.

## The local VICE build

Source + build tree (compiled by the repo owner):

```
/Users/swessels/Develop/svm/vice-emu-code/vice
```

Run the SC emulator from that directory as:

```
src/x64sc -directory data <args>
```

### Rebuilding after a source patch

```sh
cd /Users/swessels/Develop/svm/vice-emu-code/vice
export LDFLAGS="-L$(brew --prefix glew)/lib $LDFLAGS"
export CPPFLAGS="-I$(brew --prefix glew)/include $CPPFLAGS"
export PKG_CONFIG_PATH="$(brew --prefix glew)/lib/pkgconfig:$PKG_CONFIG_PATH"
rm -f src/x64sc                       # delete first: prove it actually relinked
make -j$(sysctl -n hw.ncpu)
ls -la src/x64sc                      # confirm a fresh timestamp
```

**The build "fails" at the very end and that is fine.** The last step generates
HTML docs and dies with `Bash version 4 or higher is required` (macOS ships bash
3.2; the doc generator is a zsh/bash-4 script). That step runs *after* `src/x64sc`
is already linked, so the binary is current. Always `rm -f src/x64sc` before
building and check the timestamp so you know the relink happened rather than
trusting the exit code. The glew env vars above were needed for `configure`/link
to find glew on this machine.

## Running headless with a monitor script

x64sc is a GTK3 build but runs fine headless in `-console` mode. Typical capture:

```sh
src/x64sc -directory data -console -sounddev dummy -warp \
    [-ntsc] -autostart "<abs path to .g64>" \
    -moncommands <monfile>
```

- `-console -sounddev dummy -warp` — no window interaction, no audio, fast.
- `-ntsc` — needed for NTSC titles (e.g. Robocop); omit for PAL (Arkanoid).
- `-autostart <img>` — loads and RUNs the first program (the real load path).
- `-moncommands <file>` — plays monitor commands at startup; this is how you set
  breakpoints and dump state without a UI.

### Gotchas

- **`quit` in the monitor exits the *monitor*, not the process** under
  `-console -warp`; the emulator keeps running. Always wrap the launch in
  `timeout 90 ...` and accept exit code 124 — a `save` performed before `quit`
  has already flushed to disk, so the capture is valid even though the process
  was killed.
- The checkpoint number for `command <N> "..."` is the number VICE assigns to the
  preceding `break` (usually `1` for the first breakpoint). Getting it wrong makes
  the command silently never fire.
- `m <range>` memory dumps go to the monitor console, which is unreliable to
  capture headless. Prefer `save` (binary/PRG to a file) for memory, and a source
  patch (below) for per-access streams.
- Multi-line `command` bodies were unreliable; put everything on one line joined
  with `;`.

### Capturing settled RAM (a clean oracle)

Break at a semantic checkpoint and save the whole RAM as a PRG:

```
break exec 9400
command 1 "save \"/tmp/vice_ram.prg\" 0 0801 fff0; quit"
```

`save "<file>" 0 <start> <end>` writes a 2-byte load address then the bytes
(`0` = C64 memory config). Strip the 2-byte header to index by address. Capture
at a *settled post-load* checkpoint (e.g. the loader's handoff address) — settled
RAM is trustworthy; mid-transfer captures suffer the sync caveat noted in
`C64mG64-fix.md`.

VICE's power-on RAM pattern is `FF FF FF FF 00 00 00 00 ...` repeating, so
unwritten regions and RAM-under-I/O (`$D000-$DFFF`) differ harmlessly from c64m
(which zeroes RAM). Score comparisons over the loaded code region only, or on a
specific corruption signature.

## Per-access stream trace (the decisive tool)

To find the exact cycle a fast loader diverges, log every relevant bus access in
*both* emulators and compare the value sequences. For the IEC dual-bit loaders we
logged every CIA2 `$DD00` read. VICE patch, in
`src/c64/c64cia2.c`, at the end of `read_ciapa()` just before `return value;`
(that file already includes `drive.h`, `iecbus.h`, `maincpu.h`):

```c
{
    static FILE *dd00log = NULL; static int init = 0;
    if (!init) { const char *p = getenv("VICE_DD00LOG");
                 if (p) dd00log = fopen(p, "wb"); init = 1; }
    if (dd00log) {
        unsigned int cpc = maincpu_get_pc();                       /* C64 PC   */
        unsigned int dpc = diskunit_context[0]->cpu->cpu_regs.pc;  /* 1541 PC  */
        unsigned char rec[5] = { value,
            cpc & 0xff, (cpc >> 8) & 0xff, dpc & 0xff, (dpc >> 8) & 0xff };
        fwrite(rec, 1, 5, dd00log);
    }
}
```

Handy VICE accessors: global `iecbus` (`iecbus.cpu_port` = value the C64 reads,
`iecbus.drv_data[8]` = drive IEC output, `iecbus.cpu_bus` = C64 IEC output);
`maincpu_get_pc()`; `diskunit_context[0]->cpu->cpu_regs.pc` (macro
`MOS6510_REGS_GET_PC`). The C64->drive catch-up happens in
`iecbus_cpu_read_conf1()` (`drive_catch_up_hook`) and the write path in
`store_ciapa()` (`iecbus_callback_write(..., maincpu_clk + !write_offset)`) —
that `write_offset` is the one-cycle propagation delay c64m had to reproduce.

Matching c64m patch: in `c64_cpu_read()` at `address == 0xDD00u`, log
`value`, `machine->cpu.cpu.pc`, `machine->drive8.cpu.cpu.pc` behind
`getenv("C64M_DD00LOG")`. Run each side to the same checkpoint:

```sh
C64M_DD00LOG=/tmp/c64m.bin ./build/probe_c64_arkanoid_g64
VICE_DD00LOG=/tmp/vice.bin timeout 90 src/x64sc -directory data -console \
    -sounddev dummy -warp -autostart <img> -moncommands <break-9400-then-quit>
```

### Comparing

- **The C64 runs identically and cycle-exact in both**, so the value sequences
  match until a real divergence. Align by the read index.
- Wait-loop reads (`LDA $DD00 / BPL`) vary in count when timing differs, which
  desyncs a raw index compare. Filter to the loader's *fixed* per-byte sampler
  reads (identify them from the C64 PC histogram) and compare those — the count
  per byte is constant, so byte N aligns.
- **c64m's logged C64 PC is post-increment** (points past the `LDA $DD00`), so it
  reads e.g. `$01A3` where VICE (opcode PC) reads `$01A0`; map accordingly.
- The 1541 PC logged alongside tells you *why* a sample diverged: same drive PC +
  different output = an output-mapping bug; the drive PC running ahead/behind =
  a rate or catch-up-latency bug (which is what this bug turned out to be).

### macOS shell note

The default shell is zsh, which does **not** word-split unquoted variables. A
loop like `for RW in "0 2"; do set -- $RW; ...` leaves `$1="0 2"`. Use explicit
arguments or `${=VAR}` when scripting parameter sweeps.

## Cleanup

The VICE trace patch is debug-only and gated behind an env var, but remove it
from `src/c64/c64cia2.c` when done so the VICE tree stays clean (it was reverted
after this investigation). The c64m-side logging is not committed.
