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
