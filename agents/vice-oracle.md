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
turbo mode 1 or 2 (`--turbo=1` or `2`); mode 3 (warp) disables live pixel output.

## When this note applies

Any agent task that:

- loads a game from `assets/prg/` into VICE,
- compares c64m vs VICE for soft-scroll, raster IRQ, or boot behavior,
- or documents a VICE command line for these titles,

should follow this file. If VICE "doesn't start the game," check
`-autostartprgmode 1` and `-autoload` first.

## Binary monitor: the fast path for c64m-vs-VICE comparison

The **text** monitor cannot load a snapshot (`load_snapshot` has no token in the
parser and is silently ignored) and is fragile about reconnects. For any scripted
comparison use the **binary** monitor instead. It is a different socket and a
different protocol, and it is far better suited to this job.

```sh
./src/x64sc -directory /Applications/vice-arm64-gtk3-3.10/VICE.app/Contents/Resources/share/vice \
  -pal -VICIImodel 6569 \
  -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502
```

Note `-VICIImodel 6569` (see the model-matching trap below). Reference
implementation: `src/monitor/monitor_binary.c`.

**Wire format.** Request: `02 <api=02> <u32 body-len> <u32 request-id> <u8 cmd>
<body>`. Response: `02 <api> <u32 body-len> <u8 type> <u8 error> <u32 request-id>
<body>`. Responses can arrive out of order and unsolicited (checkpoint hits,
`STOPPED` 0x62, `RESUMED` 0x63), so match on request id and queue the rest.

Commands that matter here:

| Cmd | Name | Use |
|-----|------|-----|
| 0x42 | UNDUMP | Load a `.vsf`. Body: `<u8 len><path><NUL>`. This is the only way to load a snapshot from a script. |
| 0x84 | DISPLAY_GET | Raw framebuffer, indexed-8. Body: `<u8 use_vic><u8 format=0>`. See below — this is the single biggest win. |
| 0x12 | CHECKPOINT_SET | Body: `<u16 start><u16 end><u8 stop><u8 enabled><u8 op><u8 temporary>`. `op`: load=1, store=2, exec=4. Ranges and store/load watchpoints both work. |
| 0x31 | REGISTERS_GET | Includes the **`LIN` and `CYC` pseudo-registers** — raster line and cycle within the line. Get names via REGISTERS_AVAILABLE (0x83). |
| 0x01 | MEM_GET | Body: `<u8 side_effects><u16 start><u16 end><u8 memspace><u16 bank>`. Reads 8KB+ per call. |
| 0x82 | BANKS_AVAILABLE | Bank ids; `ram`=1, `rom`=2, `io`=3. Use `ram` to read under I/O and ROM without side effects. |
| 0xaa | EXIT | Resume emulation. |

**Sending any binary command while VICE is running breaks it into the monitor**
(`monitor_check_binary` → `monitor_startup_trap`, called from the vsync hook), and
**closing the socket resumes emulation**. So a script that connects, works, and
exits leaves VICE free-running from wherever it stopped. Always `UNDUMP` at the
start of every script run to get a deterministic starting state; do not assume the
state left by the previous run. Unlike the text monitor, reconnecting the binary
monitor between runs did not wedge VICE.

### DISPLAY_GET is directly comparable to c64m's `get-frame`

`DISPLAY_GET` returns the uncropped debug buffer, which for PAL is **504x312
indexed-8** — the same geometry as c64m's `get-frame` payload, and only 157KB
against c64m's 649KB of ARGB. It also reports `x_off`/`y_off` for the display
window: PAL gives `x_off=136`, `y_off=51`, `inner 320x200`.

Since buffer column 136 is VIC X 24 (`pal-border.md`), the mapping is:

```text
vice_buffer_x = (VIC_X + 112) % 504        vice_buffer_y = raster
c64m framebuffer x = VIC X                 c64m row       = raster
```

So `numpy.roll(vice_frame, -112, axis=1)` puts VICE in c64m's VIC-X order and the
two can be diffed directly. This avoids every trap in the `screenshot` route —
no PNG/BMP format flag, no viewport crop mismatch, no palette RGB differences,
no CRT-shader host capture. Prefer it for all pixel work.

### Read registers from the auto-emitted stop sequence, NOT a separate query

This one silently corrupted an entire investigation. When a checkpoint hits, VICE
**auto-emits an async sequence**, all with request-id `0xFFFFFFFF`:
`RESUMED (0x63)` → `CHECKPOINT_INFO (0x11)` → `REGISTER_INFO (0x31)` →
`STOPPED (0x62)`. The `0x31` in that sequence is the authoritative register state
**at the stop**.

The trap: after `wait_stopped()`, issuing a *separate* `REGISTERS_GET (0x31)`
races the async stream. `EXIT (0xaa)` has already resumed VICE, so your
`REGISTERS_GET` may be answered by the auto-emitted `0x31` of the *next* stop, or
you consume a stale one — and on roughly every other iteration you get a **phantom
read**: consistently `LIN=0`, a fixed ghost stack (`89 3f 4d` in the DEM demo),
and a PC parked in whatever was running at resume. It looks like a real, regular
signal (it fabricated a clean "handler runs twice per frame, once at line 0"), so
it will not announce itself as noise.

Correct client shape:
- `cont()` = **send `EXIT` fire-and-forget**; do not wait for a matching-id reply
  (EXIT emits no id-matched response before running).
- `wait_stopped()` = read the async stream to `STOPPED (0x62)`, and along the way
  **capture the `REGISTER_INFO (0x31)`** (and `CHECKPOINT_INFO`) it carries.
- `registers()` = return that captured `0x31`, never a fresh `REGISTERS_GET` while
  running. Fetch register *metadata* (`REGISTERS_AVAILABLE 0x83`) once **while
  stopped**, before the first `cont` — doing it mid-run deadlocks.

**Always validate a checkpoint finding** before building on it: single-step once
and confirm the PC is really inside the expected routine, or cross-check against a
phase-independent invariant. A cross-emulator **buffer hash match** is the gold
standard — hashing the demo's `$5800` pattern buffer per frame in both emulators
showed a byte-identical sequence offset by a constant 12 frames, which is what
finally proved the "divergence" was just snapshot phase, not an emulator bug.

### Deterministic frame stepping

There is no "step one frame" command. The reliable pattern is an **exec checkpoint
on an address the target executes once per frame**, then `EXIT` and wait for
`STOPPED`, filtering on `LIN` from REGISTERS_GET:

```text
checkpoint_set(handler_addr, op=exec)
loop: EXIT; wait STOPPED; if registers()["LIN"] == 0: capture DISPLAY_GET
```

Emulation only advances when you say so, so no frames are dropped no matter how
slow the transfer is. Anchoring at `LIN == 0` means the buffer holds the
just-completed frame.

### Traps specific to the binary monitor

**The PC reported at a *store* checkpoint stop is not the storing instruction.**
On a `$D000-$D02F` store watchpoint, stops reported PCs such as `$9FB5` — an
address whose instruction is `ROL $5878,X`, not a VIC store at all. `LIN`, `CYC`
and the register file were correct at those stops. Identify the target register by
setting a checkpoint on a **single address** rather than a range, and read the
stored value from `A` (valid for `STA`, meaningless for `INC`/`DEC`).

**A read-modify-write instruction writes twice.** `INC $D012` and the classic
`DEC $D019` acknowledge idiom perform the 6502 dummy write of the *original* value
followed by the new one. c64m's `C64M_VICLOG` shows both writes; a VICE store
checkpoint shows one stop. That is a difference in the *instrumentation*, not in
the emulation — do not report it as a divergence.

**Check the stack frame before concluding anything about an interrupt source.**
`$D019` bit 7 tells you whether the *VIC* is asserting: bit 7 set (e.g. `$F1`) means
the VIC raised it, bit 7 clear (`$70`) means it did not. That is necessary but
**not sufficient** — it does not establish that an interrupt happened at all, and
reading it that way produced a confident wrong answer ("the missing interrupt is
CIA-sourced") in the Deus Ex Machina spirals session. The decisive test is the
pushed status byte: on a 6502, bit 5 of a status byte pushed by an IRQ/BRK is
**always 1**. So at the handler's first instruction, read the three bytes at
`SP+1`:

- top byte has bit 5 set → genuine interrupt frame; bytes 2–3 are the interrupted
  PC, and `$D019` bit 7 then tells you VIC vs CIA;
- top byte has bit 5 clear → **not an interrupt** — you are looking at a `JSR`
  return address, i.e. the handler was called from the main program.

In that session the entry with `$D019=$70` had top-of-stack `89 3F 4D`; `$89` has
bit 5 clear, and `$3F87: JSR $9C03` pushes exactly `$3F89`. It was a plain
subroutine call, and both CIAs had `icr_mask=$00` (no CIA interrupt enabled at
all). Confirm the CIA masks — c64m's `get-cia 1|2` reports `icr_mask` directly,
and in a `.vsf` it is byte 13 of the `CIA1`/`CIA2` module payload (`c_cia[CIA_ICR]`;
byte 20 is the peeked flags, bytes 16–17/18–19 the timer latches).

**CIA1 Timer A makes a good shared clock.** It counts phi2 continuously and is
unaffected by BA stalls, so sampling `$DC04/$DC05` at the same anchor in both
emulators gives comparable cycle deltas without needing a common cycle counter.

### Reading a `.vsf` without loading it

The module chain is walkable directly: after the file header, each module is
`<16-byte name><u8 major><u8 minor><u32 size>` and `size` includes the header, so
`offset += size` reaches the next one. Useful to confirm what a snapshot contains
(`C64MEM`, `VIC-IISC`, `CIA1`, `CIA2`, `DRIVE8`, …) and which machine class it was
taken on, before spending time on a load that will fail.

## Building VICE from Source

The VICE source tree is located at:

    /Users/swessels/Develop/svm/vice-emu-code/vice

It is already configured (how is shown here for completeness only)
```# Generate the configure files
./autogen.sh

# Configure the build system layout
./configure --disable-pdf-docs
```

It can be built with:

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

    src/x64sc -directory data [optional command line switches]

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

**Disconnecting the monitor RESUMES emulation — it does not freeze VICE.** If you
connect, inspect, and disconnect, VICE keeps running and a demo races past the
scene you wanted. There is no "stay frozen on disconnect". Combined with the
reconnect-wedge trap above, this means you must hold **one** connection for the
entire life of the VICE process. The robust pattern is a small **daemon** that
opens the single monitor connection once and forwards commands to short-lived
clients over a second local port. Kill/restart the daemon only while VICE is
parked on a marker that spins forever with no side effects (e.g. EoD's `$020C`
swap loop with no disk attached), where a reconnect is safe; never restart it
mid-scene.

**Async-break needs a first-byte wait, not an idle wait.** Sending a bare newline
over the text monitor breaks a *running* VICE back into the monitor. But your read
must block for the first byte up to a hard cap (the breakpoint/stop banner), NOT
return as soon as the socket goes quiet: while VICE runs the monitor emits
nothing, so an idle-timeout read returns immediately with `g` still running, and
your next command then breaks in at a random PC. Wait for output; then drain the
trailing bytes on a short idle.

**A single monitor playback file is often the safest deterministic driver.**
`-moncommands <file>` can own the complete run: set the initial breakpoint,
`g`, attach or swap media after the stop, replace breakpoints, collect state,
take the screenshot, and finish with `quit`. This avoids a connection race and
keeps one monitor session alive for the whole process. A representative shape is:

```text
break <first-marker>
g
attach "<disk-path>" 8
delete <checkpoint-id>
break <scene-marker>
g
io d000
screenshot "/private/tmp/vice-frame.png" 2
quit
```

**Monitor numbers default to hexadecimal.** This includes checkpoint crossing
counts: `ignore 1 78` ignores 0x78 = 120 hits, while `ignore 1 120` ignores
0x120 = 288 hits. Convert intended decimal counts explicitly instead of assuming
the monitor parses them as decimal.

**Checkpoint IDs can be reused after deletion.** After `delete 1`, the next
`break` may also be checkpoint 1; it is not necessarily checkpoint 2. Read the
creation/stop output or checkpoint list before issuing `ignore`, `enable`, or
`delete` against a presumed ID.

**Monitor output needs the monitor log, not the general emulator log.** Use
`-monlog -monlogname /private/tmp/vice-monitor.log` with `-moncommands` when the
playback contains `io`, memory dumps, or register queries. `-logfile` captures
the general VICE log but not those monitor command results. Keep both logs and
all generated captures outside the repository.

**`io d000` is richer than a `$D000-$D02E` memory dump.** The VIC-II monitor dump
includes raster cycle/line, video mode, scroll values, RC, idle state,
VC/VCBASE/VMLI, video/charset or bitmap bases, and sprite DMA/display state.
Capture it at the first divergent raster point; plain register bytes cannot
answer whether the graphics sequencer or a sprite is producing a pixel.

**`screenshot "<file>" 2` writes PNG (format 2); the default is BMP.** A bare
`screenshot "<file>"` looked like a silent no-op (BMP bytes in a `.png` name / not
where expected). Also: screenshotting immediately at a raster-0 / IRQ-entry
breakpoint captures the *previous* frame's buffer — advance a few frames (re-`g`
with the breakpoint armed) before grabbing the scene.

**Raw screenshots still need alignment before comparison.** VICE's screenshot
viewport can have a different vertical crop from c64m's full raw PAL/NTSC frame,
and their configured palettes need not use identical RGB values. Align using
visible raster boundaries and compare geometry or scene-color classes. Do not
declare equality or divergence from a whole-image hash, exact RGB values, or a
scaled host screenshot with CRT scanlines.

**Match the VIC-II model, and pass it explicitly.** VICE's stock default is an
**8565**; c64m models the **6569**, and they differ by ~8 dots in the border
region. Always launch with `-VICIImodel 6569` rather than relying on `vicerc`
(`VICIIModel=0` is 6569 — see `src/vicii.h`). A `.vsf` only loads when the model
matches, so a snapshot that "loads" but leaves a blank or reset machine is a model
mismatch, not a corrupt file. Full story in `pal-border.md`.

**Pairing recipe on the c64m side.** Use `--turbo=1` or `2` (mode 3 disables live
pixels), reload the snapshot at the start of *every* capture run so both sides
start from a fixed state, and anchor frame capture on an **exec breakpoint**
rather than `wait-frame` — see `agents/remote-improve.md` for why, and for the
control-port rough edges that will otherwise cost you a wrong measurement
(`get-debug-memory` in particular returns a stale cached snapshot).

**The cycle-exact VIC-II core is `src/viciisc/`.** x64sc uses it, not the older
`src/vicii/`. Border/timing ground truth lives in
`src/viciisc/vicii-cycle.c` (`check_hborder`, `check_vborder_*`) and the per-cycle
flag table in `src/viciisc/vicii-chip-model.c` (`cycle_tab_pal`/`cycle_tab_ntsc`).
Main-border flip-flop: right set at cycle 57 (CSEL=1) / 56 (CSEL=0), left clear at
17 (CSEL=1) / 18 (CSEL=0), same cycles PAL and NTSC, sampled *before* the CPU
store.
