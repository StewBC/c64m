# VICE as oracle

When comparing c64m to VICE, treat VICE as the timing/display oracle and
hardware as the tiebreak. Match video standard and **VIC-II model** first.
VICE's default chip is an 8565; c64m models the 6569. They differ by about 8
dots in the border. Always pass `-VICIImodel 6569`. A `.vsf` only loads when
the model matches; a snapshot that "loads" into a blank/reset machine is a
model mismatch. Tell: single colour-15 dots at transitions (8565 grey-dot).

The cycle-exact VICE core for `x64sc` is `src/viciisc/`, not `src/vicii/`.
Ground truth for borders: `vicii-cycle.c` (`check_hborder`,
`check_vborder_*`) and `vicii-chip-model.c` (`cycle_tab_pal` /
`cycle_tab_ntsc`).

## Collection PRGs under `assets/prg/`

Those files are one-load dumps: they replace large parts of memory and
override the IRQ vector. After inject, the IRQ is what starts the game.
c64m's `-p` / `load-prg` path is built for that. VICE must be told the same
way:

```sh
x64sc \
  -pal -VICIImodel 6569 \
  -autostartprgmode 1 \
  -autoload "/absolute/path/to/game.prg"
```

Use `-ntsc` when the title and c64m `--video NTSC` are NTSC. Quote paths
that contain spaces.

Do **not** use plain `-autostart` without `-autostartprgmode 1` for these
files. Do not wait for READY and type RUN. Disk titles under `assets/disks/`
are a different workflow (`disk-iec1541.md`).

c64m counterpart:

```sh
./build/c64m --video PAL -a -p "assets/prg/some-game.prg"
```

For live frames use turbo 1 or 2. Turbo 3 disables live pixels.

## Binary monitor (scripted compares)

The text monitor cannot load a snapshot (`load_snapshot` has no token) and
is fragile about reconnects. Use the binary monitor:

```sh
x64sc -pal -VICIImodel 6569 \
  -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502
```

Request: `02 <api=02> <u32 body-len> <u32 request-id> <u8 cmd> <body>`.
Response: `02 <api> <u32 body-len> <u8 type> <u8 error> <u32 request-id>
<body>`. Responses can arrive out of order and unsolicited (`STOPPED` 0x62,
`RESUMED` 0x63); match on request id.

| Cmd | Name | Use |
|-----|------|-----|
| 0x42 | UNDUMP | Load a `.vsf`. Only scripted snapshot load. |
| 0x84 | DISPLAY_GET | Indexed-8 framebuffer. Body: `<u8 use_vic><u8 format=0>` |
| 0x12 | CHECKPOINT_SET | `<u16 start><u16 end><u8 stop><u8 enabled><u8 op><u8 temporary>`. op: load=1, store=2, exec=4 |
| 0x31 | REGISTERS_GET | Includes `LIN` and `CYC` |
| 0x01 | MEM_GET | `<u8 side_effects><u16 start><u16 end><u8 memspace><u16 bank>` |
| 0x82 | BANKS_AVAILABLE | `ram`=1, `rom`=2, `io`=3 |
| 0xaa | EXIT | Resume |

Any binary command while VICE is running breaks into the monitor. Closing
the socket **resumes** emulation. `UNDUMP` at the start of every script run.
Unlike the text monitor, reconnecting the binary monitor did not wedge VICE.

### DISPLAY_GET vs c64m `get-frame`

PAL DISPLAY_GET is **504x312 indexed-8**, same geometry as
`get-frame format=indexed8`. PAL `x_off=136` is VIC X 24:

```text
vice_buffer_x = (VIC_X + 112) % 504
c64m framebuffer x = VIC_X
```

`numpy.roll(vice_frame, -112, axis=1)` puts VICE in VIC-X order. Prefer this
over `screenshot` (crop, palette RGB, CRT shader).

### Registers at a stop

When a checkpoint hits, VICE auto-emits (request-id `0xFFFFFFFF`):
`RESUMED` -> `CHECKPOINT_INFO` -> `REGISTER_INFO` -> `STOPPED`. The `0x31`
in that sequence is the register state **at the stop**.

Issuing a separate `REGISTERS_GET` after `wait_stopped()` races that stream
and fabricates phantom reads (`LIN=0`, ghost stack). Capture the auto-emitted
`0x31`. `cont()` sends `EXIT` fire-and-forget. Fetch `REGISTERS_AVAILABLE`
once while stopped, before the first `cont`.

Validate a checkpoint finding: single-step, or hash a guest buffer in both
emulators. A constant frame-offset between otherwise identical hashes is
snapshot phase, not an emulator bug.

There is no "step one frame". Exec-checkpoint an address the target hits
once per frame, `EXIT`, wait `STOPPED`, filter on `LIN`.

### Binary-monitor traps

- The PC at a **store** checkpoint is not the storing instruction. Identify
  the register with a single-address checkpoint; `A` is valid for `STA`.
- RMW writes twice (`INC $D012`, `DEC $D019`). VICE may show one stop; c64m
  VICLOG shows both. That is instrumentation, not a divergence.
- `$D019` bit 7 says whether VIC is asserting, not whether an interrupt
  happened. At the handler, read three bytes at `SP+1`: bit 5 of the top
  byte is always 1 on a 6502 IRQ/BRK push. Clear means you are looking at a
  `JSR` frame. Confirm CIA masks (`get-cia` reports `icr_mask`; in a `.vsf`
  it is byte 13 of the CIA module).
- CIA1 Timer A (`$DC04/$DC05`) is a good shared clock: counts Phi2, ignores
  BA stalls.

A `.vsf` module chain is walkable without loading:
`<16-byte name><u8 major><u8 minor><u32 size>` with `size` including the
header.

## Traps that look like emulator bugs

- **Drive 9.** VICE enables only unit 8 unless `-drive9type` is given. c64m
  can have a powered idle device 9 that still ATN-acks DATA. Match devices
  (`disk-iec1541.md`).
- **STOPWATCH is not `maincpu_clk`.** STOPWATCH counts from emulator start
  through `-autostart` reset; `maincpu_clk` restarts at 0. Comparing
  STOPWATCH to a c64m cycle count invents a ~3x gap.
- **`-warp` runs ahead of your breakpoint.** Arm first, or the gap is when
  you connected.
- **Never reconnect the text remote monitor.** Disconnect/reconnect wedges
  VICE (`vice_network_send: Broken pipe`). Hold one connection. Never
  `nc -z` the port. Disconnect **resumes** emulation.
- **A poll-loop breakpoint is not a progress detector.** Pick a marker that
  exists only on the post-event path.
- **Monitor numbers default to hex.** `ignore 1 78` is 0x78 hits.
- **Checkpoint IDs reuse after `delete`.** Read the list.
- **`screenshot` default is BMP.** Format 2 is PNG. A grab at raster 0 /
  IRQ entry is the previous frame.
- **Do not hash whole RGB screenshots.** Align geometry; compare indices or
  scene-color classes.

On the c64m side: turbo 1 or 2, reload snapshot at the start of every
capture run, prefer `step-frame` or `run-to-raster` over free-run
`wait-frame` (`control-port.md`). `get-debug-memory` always rebuilds.
