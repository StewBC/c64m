# VICE as oracle: loading `assets/prg` one-load collection PRGs

When comparing c64m to VICE, treat VICE as the timing/display oracle. This note
is specifically about **how to start the commercial/game PRGs under
`assets/prg/`** in VICE. Using the wrong load flags is a common false failure:
the game never boots, or boots differently from c64m's `-p` path.

## What these PRGs are

Files under `assets/prg/` (e.g. `Fort Apocalypse.prg`,
`Arkanoid - Revenge of Doh (J1).prg`) are **one-load collection images**, not
small BASIC/sys stubs:

- They are full **64K-style** dumps that replace large parts of memory.
- They **override the IRQ vector** as part of injection.
- As soon as the machine runs after load, the **IRQ fires** and that path is what
  **sets up and starts the game**. There is no separate "RUN" or disk bootstrap
  step for these files.

c64m's `-p` / `load-prg` path is built for that inject-and-run model. VICE must
be told to load the same way.

## Correct VICE launch

Use **`-autostartprgmode 1`** and **`-autoload`** with the PRG path (quote paths
that contain spaces). Example (absolute path; adjust to your tree):

```sh
/Applications/vice-arm64-gtk3-3.10/bin/x64sc \
  -autostartprgmode 1 \
  -autoload "/Users/swessels/Develop/github/personal/c64m/assets/prg/Fort Apocalypse.prg" \
  -remotemonitor -remotemonitoraddress ip4://127.0.0.1:6518
```

Another example (NTSC title that matches c64m `--video NTSC`):

```sh
/Applications/vice-arm64-gtk3-3.10/bin/x64sc \
  -ntsc \
  -autostartprgmode 1 \
  -autoload "/Users/swessels/Develop/github/personal/c64m/assets/prg/Arkanoid - Revenge of Doh (J1).prg" \
  -remotemonitor -remotemonitoraddress ip4://127.0.0.1:6518
```

### Flags that matter

| Flag | Why |
|------|-----|
| `-autostartprgmode 1` | Load as a full memory image / inject style, not a normal small PRG autostart that expects BASIC/`SYS`. Required for these collection dumps. |
| `-autoload "<path.prg>"` | Path to the PRG under `assets/prg/`. Prefer absolute paths; always quote if the name has spaces. |
| `-remotemonitor` / `-remotemonitoraddress ip4://127.0.0.1:PORT` | Optional but useful for oracle tracing (breakpoints, memory, compare with c64m control port). |
| `-ntsc` / `-pal` (or model flags) | Match the title and c64m (`--video NTSC` / `--video PAL`). J1 Arkanoid Revenge of Doh is NTSC. |

Local VICE binary used on this machine:

```text
/Applications/vice-arm64-gtk3-3.10/bin/x64sc
```

VICE source may also be available on the host for reading VIC-II/CIA behavior;
the binary above is the runtime oracle.

## What not to do

- Do **not** assume a plain `-autostart file.prg` without `-autostartprgmode 1`
  matches c64m's inject path for these files.
- Do **not** treat these PRGs like disk-based titles under `assets/disks/` (those
  use G64/D64 + 1541; different workflow — see `disk-iec1541.md` / `testing.md`).
- Do **not** wait for a READY prompt and type RUN; the IRQ after injection is
  the entry.

## c64m counterpart

Rough equivalent for the same class of file:

```sh
./build/c64m --video NTSC -a -p "assets/prg/Arkanoid - Revenge of Doh (J1).prg"
```

Headless / control-port automation: see `control-port.md`. For live frames use
`--turbo` at most 7 (higher turbo can disable live pixel output).

## When this note applies

Any agent task that:

- loads a game from `assets/prg/` into VICE,
- compares c64m vs VICE for soft-scroll, raster IRQ, or boot behavior,
- or documents a VICE command line for these titles,

should follow this file. If VICE "doesn't start the game," check
`-autostartprgmode 1` and `-autoload` first.

## Building VICE from Source

The VICE source tree is located at:

    /Users/swessels/Develop/svm/vice-emu-code/vice

It is already configured and can be built with:

    cd /Users/swessels/Develop/svm/vice-emu-code/vice
    make -j"$(sysctl -n hw.ncpu)"

The build currently ends with the following documentation-generation error:

    generating html documentation...
    Bash version 4 or higher is required
    On macOS, install bash via macports or homebrew and try again.
    make[2]: *** [vice_foot.html] Error 1
    make[1]: *** [all-recursive] Error 1
    make: *** [all-recursive] Error 1

This error occurs after the emulator binaries have been built successfully. It
is caused by the HTML documentation step and does not indicate that the x64sc
build failed.

After the build, the executable is located at:

    src/x64sc

Run it from the VICE source directory with:

    ./src/x64sc [optional command line switches]

Use this locally built version of VICE when additional instrumentation,
logging, tracing, or internal state inspection is required. Source changes can
be made directly in the VICE tree and tested by rebuilding src/x64sc.

The locally built binary does not find its ROMs on its own. Pass the installed
data directory:

```sh
./src/x64sc -directory /Applications/vice-arm64-gtk3-3.10/VICE.app/Contents/Resources/share/vice ...
```

## Comparing c64m against VICE: traps that produce false findings

Each of these produced a confident, wrong conclusion during the Edge of Disgrace
investigation. None is an emulator bug.

**VICE's default device set is not c64m's.** VICE enables **only unit 8** unless
`-drive9type` is given (`iecbus_cpu_write_conf1`, "only the first disk unit is
enabled"). c64m loads a 1541 ROM into both drive objects. Before concluding that
a bus difference is a c64m bug, confirm both sides have the same devices on the
bus — an idle second drive still answers ATN by pulling DATA. See
`disk-iec1541.md`.

**The monitor STOPWATCH is not `maincpu_clk`.** STOPWATCH counts from emulator
start and straight through the reset that `-autostart` performs; `maincpu_clk`
restarts at 0 on that reset. Comparing STOPWATCH against a c64m cycle count
invents a ~3x discrepancy out of nothing. Instrument `maincpu_clk` if you need a
comparable clock.

**`-warp` runs ahead of your breakpoint.** Emulation starts at launch, so by the
time a script connects and arms a breakpoint, VICE may already be tens of
millions of cycles in. A gap between two events measured this way is an artifact
of when you armed the breakpoint, not demo behaviour.

**Attach is silent, and the log is buffered.** `attach "<file>" <dev>` prints
nothing on the monitor socket; the `Unit 8 drive 0: D64 disk image attached:`
line only appears with `-verbose`, and VICE's stdout is block-buffered, so
`SIGTERM` discards it — a working attach looks like a no-op. Use `stdbuf -o0 -e0`
and shut down with the monitor's `quit`. Attach is also ignored while autostart
is still running: check the PC first.

**Never reconnect the remote monitor.** Connecting breaks into the monitor and
freezes emulation; disconnecting and reconnecting wedges VICE into an endless
`vice_network_send: Broken pipe`. Hold one connection for the session and re-enter
via breakpoints. Never probe the port with `nc -z`.

**A breakpoint on a poll loop is not a progress detector.** A loop the target
executes continuously re-hits within cycles whether or not the event happened.
Pick a marker that is only on the post-event path (for EoD: `$020C` is only ever
executed at the swap prompt, and `$028A` only at depack completion).