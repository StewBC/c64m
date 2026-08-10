# Control port: operational handoff

**Using c64m from outside this tree?** Start with **`using-c64m.md`** (recipes,
gotchas, co-op). This file is the implementer / full wire contract.

This is the working guide for an agent or external script controlling c64m. The
implementation is split between:

- `src/control/control_protocol.c` - line parser and response formatter.
- `src/control/control_server.c` - localhost TCP server and framing.
- `src/main.c` - main-loop dispatch, cached state, deferred responses.
- `src/runtime/runtime_client.{c,h}` - runtime command API.
- `tests/control/test_control_protocol.c` - parser and response examples.

## Start the server

The server is disabled unless a positive port is supplied:

```sh
./build/c64m --control-port 6510
```

The server binds to `127.0.0.1` only. It accepts one client at a time. The SDL
window and audio device are created in this mode.

For automation, use headless mode. It requires a control port:

```sh
./build/c64m --headless --control-port 6511
```

Headless mode still initializes SDL timer/thread services, starts the runtime and
main loop, and publishes frames. It does not create a window, renderer, frontend,
controller support, or host audio device. `quit-client` closes the socket; it does
not terminate the emulator process, so an automation harness must terminate the
process after its final command.

**Headless latency floor:** the main loop waits with a short timeout and is woken
when a control request is queued (SDL user event). While a deferred response is
outstanding it polls without the 1 ms idle sleep so completion tracks the runtime
rather than a fixed tick. Idle CPU stays bounded (wait, not a busy spin).

Run from the repository root so default ROM lookup finds `roms/`.

## Protocol version

Wire identity is advertised by `hello` / `version` as `protocol=C64M/N`.

**Versioning policy (this work series):** there is no dual-path compatibility
layer for older clients. When wire behavior or control concurrency semantics
change in a way that scripts must learn, bump `N` in the same change as the
code and this document. **Current: C64M/6** (the per-line VIC ring:
`vic-ring-info`, `vic-ring-record`, `vic-ring-clear`, and `vic-ring-find`, on
top of the C64M/5 frame ring, C64M/4 guarded breakpoints, C64M/3 HST1
flight-recorder queries, and C64M/2 bulk-memory/token behavior).

## Wire format

Every request begins with an ASCII line terminated by `\n`:

```text
<decimal-request-id> <command> [arguments]\n
```

IDs are client-chosen decimal integers and are echoed in responses. Hex addresses
accept `$C000` and `0xC000`; most decimal numbers accept normal C integer syntax.
Paths are not quoted: commands that take a path treat the final path portion as
the remainder of the line, so spaces are allowed.

Normal text responses are one line:

```text
<id> ok [text]\n
<id> error <code> <message>\n
```

Data responses have a header, exactly `byte_count` raw bytes, and one final `\n`:

```text
<id> data <type> <byte_count> [metadata]\n
<raw payload bytes>
\n
```

Do not use `readline()` for a data payload. Read the header line, parse the byte
count, then call `recv()` until that many bytes have arrived, then consume exactly
one newline. A payload may contain arbitrary zero bytes and newlines.

### Identity and concurrency model

Internal runtime correlation uses `request_token` (not the wire id). See
`runtime-control.md` § Message contracts. Client-visible rules:

| Rule | Behavior |
|------|----------|
| Wire request id | Echoed on the matching response; client-chosen |
| Duplicate outstanding id | Reject with `bad-id` while a prior request with the same id is still outstanding for this connection |
| Connection epoch | Bumped on accept; disconnect cancels all outstanding deferred work for that session; next client never receives the previous session's responses or payloads |
| Deferred capacity | Multi-entry table (16). **Token-matched multi-outstanding** for `get-cpu` and `get-memory`. Other deferred commands (waits, breakpoints, assemble, …) are still exclusive (second → `busy deferred-response-active`). Wait commands: at most one outstanding wait → `busy wait already active`. Table full → `busy deferred-table-full`. |
| Socket in-flight | Pipelined requests allowed up to deferred high-water mark (16); responses may complete out of request order — correlate by id. Duplicate outstanding wire ids → `bad-id`. |
| Wait concurrency | At most one outstanding wait command; second → `busy` |
| UI vs control | Main-thread UI telemetry must not complete a control deferred wait |

The server queues requests from the socket thread and dispatches them on the SDL
main loop. Multiple token-matched `get-cpu` / `get-memory` deferreds may be
outstanding (up to the table capacity). Exclusive deferred commands still
serialize:

```text
<id> error busy deferred-response-active
<id> error busy wait already active
<id> error busy deferred-table-full
```

Wire pipelining is supported: the socket may read further requests while responses
are still outstanding (high-water mark 16). Responses may complete out of request
order — correlate by wire id.

The standard deferred timeout is 2000 ms. Assembly and `history-find` use
10000 ms. Wait commands
accept 1..600000 ms and default to 2000 ms.

Every accepted request must produce exactly one response with its own wire id;
a command that returns no response (or one with the wrong id) desyncs the client
and leaves the request permanently outstanding. As a backstop, when the client
disconnects the single-client connection handler waits at most 3000 ms (> the
2000 ms deferred timeout, so genuine completions still flush) for outstanding
responses to drain, then abandons the connection so the next client is served —
one lost or misrouted response cannot wedge the control port.

### Delivery classes (control-facing summary)

- **Immediate** responses (parser errors, `get-state` from cache, `get-frame`
  when cache warm, `hello`, paused hot-cache `get-cpu`/`get-vic`/`get-cia`, etc.)
  do not need a runtime round-trip.
- **Deferred RPC** waits for a token-matched runtime completion (or timeout /
  cancel / `busy`). Completions are reliable: queue saturation yields `busy` or
  error, not a silent hang until timeout only.
- **Waits** use sticky latches for completion events (`load-state-complete`,
  `step-complete`, …) and execution-state events (`paused`, `running`,
  `breakpoints`, cleared by the next execution-control command), and non-sticky
  matching for continuous events (`frame`).
  Only one wait may be outstanding.

Bulk memory and multi-outstanding RPC use a result pool keyed by internal token;
payloads are not stuffed into fat event-queue unions (see memory section below).

## Minimal Python client

This is a complete small client for text and binary responses. It does not terminate
the emulator process in headless mode.

```python
import socket


class C64M:
    def __init__(self, host="127.0.0.1", port=6511):
        self.sock = socket.create_connection((host, port))
        self.file = self.sock.makefile("rb")
        self.next_id = 1

    def command(self, command):
        request_id = self.next_id
        self.next_id += 1
        self.sock.sendall(f"{request_id} {command}\n".encode("utf-8"))
        header = self.file.readline()
        if not header:
            raise EOFError("c64m closed the connection")
        fields = header.rstrip(b"\r\n").split(b" ", 3)
        if len(fields) < 2 or int(fields[0]) != request_id:
            raise RuntimeError(f"unexpected response: {header!r}")
        kind = fields[1].decode("ascii")
        if kind == "ok":
            return {"kind": "ok", "text": fields[2].decode("utf-8")
                    if len(fields) > 2 else ""}
        if kind == "error":
            return {"kind": "error", "code": fields[2].decode("ascii")
                    if len(fields) > 2 else "",
                    "message": fields[3].decode("utf-8")
                    if len(fields) > 3 else ""}
        if kind != "data" or len(fields) < 4:
            raise RuntimeError(f"bad response header: {header!r}")
        data_type = fields[2].decode("ascii")
        size_and_meta = fields[3].split(b" ", 1)
        size = int(size_and_meta[0])
        metadata = (size_and_meta[1].decode("ascii")
                    if len(size_and_meta) > 1 else "")
        chunks = []
        remaining = size
        while remaining:
            chunk = self.file.read(remaining)
            if not chunk:
                raise EOFError("truncated c64m data response")
            chunks.append(chunk)
            remaining -= len(chunk)
        payload = b"".join(chunks)
        if self.file.read(1) != b"\n":
            raise EOFError("truncated c64m data response")
        return {"kind": "data", "type": data_type,
                "metadata": metadata, "payload": payload}

    def close(self):
        self.file.close()
        self.sock.close()


c64m = C64M()
print(c64m.command("hello"))
print(c64m.command("reset"))
print(c64m.command("wait-paused 2000"))
print(c64m.command("get-cpu"))
memory = c64m.command("get-memory $0400 1024 map")
assert memory["type"] == "memory"
print(memory["metadata"], len(memory["payload"]))
c64m.close()
```

Clients may pipeline requests (send N without waiting) up to the outstanding
high-water mark. Correlate by request id and consume every response; completion
order may differ from send order. Sequential one-at-a-time clients remain valid.

## Introspection and execution

```text
N hello
N version
N capabilities
N ping
N quit-client
N get-state
N reset
N run
N pause
N step-cycle
N step-instruction
N step-over
N step-out
N step-frame
N run-to-raster <line 0..65535> [cycle-in-line]
N run-cycles <positive-count>
N run-instructions <positive-count>
N run-to <address>
N set-turbo <mode 1|2|3>
```

Current fixed responses:

```text
hello        -> ok name=c64m protocol=C64M/6
version      -> ok protocol=C64M/6 app=0.1.0
capabilities -> ok connection introspection execution state step turbo frame memory debug-memory call-stack input disk file snapshot breakpoints wait assemble symbols drive-cpu vic cia run-to-raster history power-drive frame-ring vic-ring
ping         -> ok
```

`get-state` is an immediate cached response such as:

```text
N ok state=paused has_cpu=1 frame=123 cycle=456 stop=breakpoint turbo=1 raster=48 vic_cycle=12
```

`raster=` and `vic_cycle=` are present when the main loop has received a machine
hardware snapshot (any prior `get-cpu` / `get-vic` / pause / step). They are the
VIC-II beam position, not the host frame counter.

`step-frame` advances until the next completed VIC-II frame is published, then
pauses with `run-complete` (or a breakpoint/BRK). It is the reliable way to capture
consecutive frames without anchoring on target-program exec breakpoints.
`run-cycles` also republishes completed frames when a frame boundary is crossed.

`run-to-raster <line> [cycle]` advances at least one Φ2 cycle, then continues until
the VIC-II beam is on `line` (and optionally exact `cycle` within the line). It
pauses with `run-complete` and a machine snapshot. **Breakpoints and BRK still
win** — if an exec/watchpoint or BRK fires first, that stop reason is used instead.
Valid line ranges depend on video standard (PAL 0..311, NTSC 0..262); out-of-range
targets time out with a runtime error. Use `get-vic` after completion to confirm
`raster=` / `cycle=`.

The former `set-cpu-history` and `get-cpu-history` commands do not exist in
C64M/3 or later. Use the flight-recorder commands below.

## CPU flight recorder

```text
N history-info
N history-record <on|off>
N history-clear
N history-find [key=value ...]
N history-next <cursor> [limit=1..256]
N history-read <id> [epoch=N] [before=0..256] [after=0..256]
N history-close <cursor>
```

`history-info`, recording control, clear, and close may be issued while running.
Find, next, and read require a paused machine and otherwise return
`busy machine-running`. Searches are newest-first by default. Find keys are:

```text
epoch timeline cycle from direction pc address access value opcodes limit
```

Ranges are inclusive and non-wrapping. `from` accepts `oldest`, `newest`, or a
retained record ID. `direction` is `backward` or `forward`. Access categories
are `execute`, `opcode`, `operand`, `data-read`, `data-write`, `dummy-read`,
`rmw-dummy-write`, `stack-read`, `stack-write`, and `vector-read`; aliases are
`fetch`, `read`, `write`, and `data`. Opcode patterns contain 1..32
comma-separated bytes with `?` nibble wildcards, for example `A9,??,8D`.

Find/next/read return counted `data history` responses with metadata:

```text
epoch=N count=N cursor=N more=0|1 oldest=N newest=N
```

The payload is little-endian HST1: a 24-byte header, followed by records with a
48-byte header and 8-byte materialized bus-access entries. Exact field offsets,
stable marker/reset reason IDs, error strings, and lifecycle semantics are in
`cpu-flight-recorder.md`. `tools/c64_control_client.py` validates and decodes
HST1 through `history_info()`, `history_find()`, `history_next()`, and
`history_read()`.

There is one runtime cursor. Any execution, reset, recording control, state
load, or direct mutation makes it stale. `history-close` is idempotent. Result
payload ownership is token-keyed and released on claim, timeout, disconnect,
cancellation, queue failure, or shutdown.

Run/step/load/input commands return `ok accepted=1` when the runtime command was
queued, not when the operation has completed. Follow with `wait-event`,
`get-state`, or a specific query.

`set-turbo` changes the active turbo mode without modifying the configured Opt+T
list. Modes are:

| Mode | Name   | Meaning |
|------|--------|---------|
| 1    | normal | Real-time pace, live pixels |
| 2    | max    | Free-run, live pixels (full correctness) |
| 3    | warp   | Free-run, paint off (debug frames only) |

At modes 1 and 2 the response is:

```text
N ok accepted=1 turbo=2
```

At mode 3 (warp) it includes a warning:

```text
N ok accepted=1 turbo=3 warning=warp-disables-live-framebuffer;get-frame-is-debug-only-until-turbo-is-1-or-2
```

In warp, VIC-II timing still advances, but the live per-cycle pixel renderer is
disabled and `get-frame` returns a geometric debug snapshot. Lowering turbo to
1 or 2 restores live rendering for subsequent frames.

## Frame ring (rolling framebuffer black box)

```text
N frame-ring-info
N frame-ring-record <on|off>
N frame-ring-clear
N get-frame-at <frame=<n>|cycle=<n>> [format=argb8888|indexed8]
```

The flight recorder answers "what executed before it went wrong"; it cannot
answer "what did the screen actually **show** three frames ago". A human pausing
a second after seeing a glitch is ~50 PAL frames too late, and those pixels are
gone. The ring keeps the last N completed frames so the bad frame can still be
retrieved afterwards.

Entries carry both `frame` and `cycle`, so a frame found here yields the
timestamp to query the flight recorder for the same moment.

```text
N ok capacity=827 count=827 dropped=89 recording=1 bytes=134198944
    oldest_frame=90 newest_frame=916 oldest_cycle=1538838 newest_cycle=15659138
```

`dropped` counts frames pushed out of the window; a non-zero value means the
glitch may already have rolled off, and the budget should be raised. Capacity
comes from `[debug] frame_ring_memory_mb` (default 128 MiB, about 827 PAL frames
/ 16.5 seconds at 50 fps; `0` disables the ring, and `capacity=0` is also what a
failed allocation reports).

`get-frame-at` names its target rather than taking a bare number, because a
number alone could be either a frame index or a machine cycle and guessing wrong
returns a plausible but wrong frame. The lookup resolves to the **nearest frame
at or before** the target; a target past the newest clamps to the newest, and a
target that predates the retained window is `error not-found`, never a
substituted neighbour. Responses echo `target=` and `target_kind=` alongside the
frame's own `frame=`/`cycle=`:

```text
N data frame 136760 width=520 height=263 stride=520 format=indexed8
    frame=916 cycle=15659138 target=916 target_kind=frame
```

Payloads are byte-identical to `get-frame` in the same format (both go through
one conversion path), so ring frames work as an oracle-compare source.

These commands answer **immediately** and work while the machine is running:
the ring carries its own mutex, so no runtime round-trip is needed and a scrub
does not contend for the deferred slot. While running, the window keeps moving
under you — pause first if you need a stable view.

**Warp (turbo 3) does not record.** The live pixel renderer is off, so there are
no real pixels; the ring stalls rather than storing geometric debug snapshots
that would look like frames but are not. Recording resumes at turbo 1 or 2.
Loading a machine state clears the ring: those frames belong to a discarded
timeline whose cycle counter has restarted.

Cost is one native indexed frame copy per completed frame. The original ARGB
ring push measured **+0.22%** of turbo-2 free-run throughput and nothing at
turbo 1; Stage 3's matched whole-core measurements remained neutral after
reducing the copy to one quarter of its former size. The default budget is
resident memory, so lower `frame_ring_memory_mb` if that matters more than
window length.

## VIC ring (per-line derived state)

```text
N vic-ring-info
N vic-ring-record <on|off>
N vic-ring-clear
N vic-ring-find [frame=<n>] [raster=<line>|<first>-<last>] [limit=1..2048]
```

The frame ring shows *that* a frame is wrong; this shows *why*. It retains the
VIC-II's end-of-line derived state — the things register writes cannot tell you:
which sprites actually had fetched data on a line, whether it was a bad line,
the sequencer counters, and above all **the sprite X (including the `$D010` MSB)
actually latched for painting that line**. A sprite that flashes at the left edge
for one frame is exactly a latched X that disagrees with the register the game
believes it wrote, and neither the framebuffer ring nor the CPU flight recorder
can show that.

Records carry `cycle`, the same axis as the frame ring and the recorder, so one
moment can be examined from all three.

```text
N ok capacity=161319 count=116846 dropped=0 recording=1 bytes=16777176
    oldest_frame=0 newest_frame=444 oldest_raster=0 newest_raster=73
    oldest_cycle=64 newest_cycle=7594989
```

`vic-ring-find` returns a counted `data vic-ring` payload of newline-separated
`key=value` text records, oldest-first, one per raster line — the flight recorder
uses binary because it holds millions of records; this holds hundreds, so
readable text beats a decoder:

```text
frame=408 raster=60 cycle=6978724 badline=0 allow_bl=0 display=0 vborder=1
  mborder=1 d011=00 d016=00 d018=15 vc=0000 vcbase=0000 rc=0 vmli=0 border=6
  bg=E,0,0,0 irq=01/00 spr_en=01 spr_vis=01 spr_act=01 spr_pri=00 spr_mc=00
  spr_xe=00 spr_yeff=01 spr_x=0050,0000,... spr_y=32,00,... spr_ptr=00,00,...
  spr_col=00,00,... spr_mcnt=1E,00,... spr_mcbase=1E,00,...
```

(one record per line in the payload; wrapped here for reading). The per-sprite
lists are always 8 entries, sprite 0 first. `spr_en`/`spr_vis`/`spr_act` are
bitmasks: enabled as latched for the line, actually had data (painted), and
sequencer still active. `spr_mcnt`/`spr_mcbase` are the sprite data counters.

All keys are optional. Omitting `frame=` matches the raster window in **every**
retained frame, which is how a per-line effect is spotted across frames:

```text
vic-ring-find frame=408                 whole frame, every line
vic-ring-find frame=408 raster=100-109  one window
vic-ring-find raster=60 limit=20        line 60 across the last 20 frames
```

Like the frame ring these answer immediately and work while running (the ring
owns a mutex); the window moves under a running machine, so pause for a stable
view. Loading a machine state clears the ring.

**Cost.** This is a much hotter path than the frame ring: one record per raster
line, ~15.6k/s at turbo 1 and ~280k/s at turbo 2. Measured **2.64%** of turbo-2
free-run throughput, and nothing measurable at turbo 1. Note that
`vic-ring-record off` stops *storing* but does **not** remove that cost — the
record is still built each line. To get the throughput back, disable the ring
with `[debug] vic_ring_memory_mb=0`, which leaves one NULL test per line and
measures identical to a build without the feature. Default budget is 16 MiB
(~161k lines, about 500 PAL frames / 10 s at 50 fps).

## State, memory, and frames

```text
N get-cpu
N get-vic
N get-cia <1|2>
N get-frame [format=argb8888|indexed8]
N get-memory <address> <length 1..65536> <map|ram|rom|drive8|drive9>
N set-memory <address> <length 1..1024> <map|ram>\n<raw length bytes>\n
N get-debug-memory [write-history=0|1]
N get-call-stack
N get-drive-cpu <8|9>
```

`get-cpu` returns text:

```text
N ok pc=E37B a=00 x=00 y=00 sp=F9 p=24 cycles=12345
```

**Hot cache:** when the main thread holds a **paused** machine snapshot and no
mutating command has been accepted since that snapshot, `get-cpu`, `get-vic`, and
`get-cia` are answered immediately from the main-thread cache (no runtime
round-trip). Mutating commands (`run`, `step-*`, `set-memory`, loads, input, etc.)
mark the cache stale. A subsequent pause/step/machine snapshot re-seals the
barrier. While running, or after a mutation without a barrier, these commands
still go through the deferred runtime path (token-matched for `get-cpu`).

`get-vic` and `get-cia` use the same cache when fresh; otherwise they request a
fresh machine hardware snapshot (deferred).
`get-vic` exposes internal VIC-II state that cannot be recovered from the
registers alone, including the raster compare latch:

```text
N ok standard=PAL raster=48 cycle=12 compare=48 d011=1B d016=08 mem=14
    irq_st=01 irq_en=01 irq=1 display=1 badline=0 ba=0 aec=1 rdy=1
    vc=0000 vcbase=0000 rc=0 frame=123 border=14 bg0=6
```

`get-cia 1` / `get-cia 2` includes the ICR mask (not readable on real hardware):

```text
N ok cia=1 pra=FF prb=FF ddra=00 ddrb=00 ta=FFFF/FFFF cra=01 tb=FFFF/FFFF crb=08
    icr_flags=00 icr_mask=81 irq=0 tod=00:00:00.00 alarm=00:00:00.00
```

`get-memory` returns `data memory` with metadata `addr=... length=... mode=...`.
The payload is exactly the requested bytes. **Length may be 1..65536** provided
`address + length <= 65536` (32-bit arithmetic; wrap is rejected with
`bad-args`). A full 16-bit space dump is `get-memory $0000 65536 <mode>` in
**one** request (no 1K chunking). Write-history is **not** included on this path
(use `get-debug-memory` when history is required).

Internally the result is a token-keyed RPC pool entry (not a fat event-queue
union). Concurrent bulk reads are limited by the pool capacity (16); a full
pool returns `busy`. Modes are CPU-visible map (`0`), raw RAM (`1`), raw ROM
(`2`), drive 8 map (`3`), and drive 9 map (`4`). Drive maps contain holes; the
machine-side debug API marks invalid bytes, but the control payload contains
the returned byte values only.

`set-memory` is the poke counterpart of `get-memory`, using the same
paste-style framing as `paste-text-data` / `paste-events-data`:

```text
N set-memory $0400 4 ram\n
<4 raw bytes>
\n
```

Only **writable** modes are accepted: `map` and `ram`. `rom`, `drive8`, and
`drive9` are rejected at parse time with `bad-args`. The command auto-pauses
(like `assemble`) and the runtime force-pauses if still running so the poke
always applies. Completion is deferred until the write finishes:

```text
N ok addr=0400 length=4 mode=1
```

`set-memory` length remains 1..1024. Address+length for set-memory uses the
write path's 16-bit modular addressing for the poke itself; parse still
rejects oversize length.

**One-shot pokes vs active demos.** Free-running code often re-stages registers
every frame (e.g. `$D015` in the border). A single `set-memory` is overwritten
before the next effect draw. For isolation against a live demo use a CPU stub,
breakpoint+poke each frame, or a VIC line-log build — there is no register-freeze
command.

`get-frame` returns `data frame` with metadata:

```text
width=504 height=312 stride=2080 format=argb8888 frame=... cycle=...
```

Default format is `argb8888`: row-major 32-bit ARGB8888, `height * stride` bytes.
The buffer is a full VIC-II raster line in VIC-X order, so framebuffer x = VIC X:
PAL is 504x312 and NTSC 520x263. **`stride` is always 2080 bytes (520 px), not
`width * 4`** - PAL rows carry 64 bytes / 16 pixels of slack so one buffer
shape serves both standards. Index rows by `stride`, never by `width`. Every
dot of the line is composed, HBLANK included. The frontend crop is not applied
to this payload.
At turbo 3 (warp) this is a geometric debug snapshot rather than the live
framebuffer; use `set-turbo 1` or `set-turbo 2` before inspecting live pixels.

`format=indexed8` returns one byte per pixel (palette index 0..15), with
`stride=width` and payload size `height * width` (PAL 504×312 = 157248 bytes).
This is the machine/runtime's native representation; internal unpainted padding
is mapped to index 0 and never appears on the wire. Indexed frames are the
preferred oracle compare format because c64m and VICE RGB values differ.

**Mid-frame pause:** `get-frame` while paused mid-raster returns the **partial
working buffer**, not a completed frame. For a full frame use `step-frame`, or
`run-to-raster` into the lower border (or a known stable line), then `get-frame`.

`get-debug-memory` **always requests a fresh snapshot** (never serves a stale
cache). It concatenates three 65536-byte arrays in this order: CPU map, raw RAM,
raw ROM. With `write-history=1`, it appends 65536 little-endian `uint64`
write-history values. Metadata includes `generation`, `map`, `ram`, `rom`, and
`write_history` flags. Treat a new `generation` as the freshness marker.

`get-call-stack` returns text beginning `sp=.. count=..` followed by
`frameN=JSR:DEST` entries. `get-drive-cpu` returns the cached drive snapshot fields
`device`, `rom`, `media`, `tracks`, `g64`, `pc`, `ht`, `dens`, `mot`, `wr`, and `sync`.

## Input and file commands

```text
N key-down <key-name>
N key-up <key-name>
N restore
N joystick <1|2> <mask 0..255>
N paste-text <text>
N paste-events <paste syntax>
N paste-text-data <byte-count>\n<raw bytes>\n
N paste-events-data <byte-count>\n<raw paste syntax>\n
N load-prg <path>
N load-bin <path> <address> <use-file-address> <reset-first> <is-basic>
N save-bin <path> <start> <end> <write-file-address> <is-basic>
N load-state <path>
N save-state <path>
N mount-d64 <8|9> <path>
N unmount-disk <8|9>
N power-drive <8|9> [on|off]
N get-disk-status <8|9>
```

`power-drive 8` / `power-drive 8 on` soft-powers that unit (no media required).
`power-drive 8 off` ejects any mounted media then powers the unit off. Mount also
powers on. `get-disk-status` includes `powered=0|1`. `get-drive-cpu` includes
`powered=`.

Boolean tokens are `0`, `1`, `false`, or `true`. `load-bin` and `save-bin` path
arguments may contain spaces because the last four tokens are parsed from the end.
`load-state` and `save-state` take a path as the rest of the line (spaces allowed)
and operate on machine `.c64state` snapshots via the runtime. They return
`ok accepted=1` when queued; wait for `load-state-complete` or `save-state-complete`
(or use `wait-event`) for completion. Failed loads leave the live machine unchanged.
The control protocol currently exposes `is_basic`, but not the frontend's Basic
Text flag; use the runtime/frontend path for Basic Text.

The joystick mask uses the C64 constants in `src/machine/c64.h`:

```text
bit 0 up, bit 1 down, bit 2 left, bit 3 right, bit 4 fire
```

`load-prg`, `load-bin`, `save-bin`, `load-state`, `save-state`, and disk commands
are accepted asynchronously; use `wait-event` or later state/status queries to
observe completion.

## Breakpoints

```text
N break-exec <address>
N break-clear <id>
N break-enable <id> <0|1>
N break-list
N get-breakpoints                 # alias for break-list
N break-clear-all
N rearm-oneshots
N break-create <access> <address> [enabled=0|1] [end=<address>] [actions=<list>] [counter=<n>] [reset=<n>] [mapping=map|rom|ram] [when=<condition>]
N break-update <id> <access> <address> [enabled=0|1] [end=<address>] [actions=<list>] [counter=<n>] [reset=<n>] [mapping=map|rom|ram] [when=<condition>]
```

`<access>` is one of:

| Token | Meaning |
|-------|---------|
| `exec` / `execute` | Break on instruction fetch at PC |
| `read` / `load` | Break on memory read |
| `write` / `store` | Break on memory write |
| `read-write` / `load-store` | Break on either access |

Address ranges use `end=<addr>` (inclusive). Example store watchpoint on the
raster register:

```text
N break-create write $D012 actions=break
```

`actions` is a comma-separated subset of `break,fast,slow,tron,troff,type,swap`,
or the exclusive token `none` for a **count-only** breakpoint: hits accumulate
while the machine free-runs (no pause). Use `break-list` / `hits=` to read the
count after a run.

```text
N break-create exec $EA31 actions=none
N run
N wait-frame 50 10000
N pause
N break-list
```

### Guarded breakpoints (`when=`)

`when=` adds a **bounded AND-list of up to 4 comparison terms**, evaluated only
*after* the address/access/mapping test already matched. That keeps the CPU hot
path untouched: a guard on `$D021` does work on the handful of accesses that hit
`$D021`, never on the general bus stream. Measured cost versus the same
watchpoint unguarded is under 1% for one term (see § Guarded-breakpoint cost).

This is not an expression language — no OR, no grouping, no precedence. If a
case needs OR, arm two breakpoints.

```text
when=<term>[,<term>...]      term = <lhs><op><imm>
```

| LHS | Meaning |
|-----|---------|
| `a` `x` `y` `sp` `p` | CPU registers |
| `n` `v` `b` `d` `i` `z` `c` | individual P flag bits, as 0 or 1 |
| `value` | the byte carried by the matching access |
| `mem($addr)` | one CPU-map byte read at match time |
| `raster` | VIC-II raster line |
| `vic_cycle` | VIC-II cycle within the line |

| Op | Meaning |
|----|---------|
| `==` `!=` `<` `>` `<=` `>=` | integer compare |
| `&` | mask set: `(lhs & imm) != 0` |
| `!&` | mask clear: `(lhs & imm) == 0` |

Immediates accept `$hex`, `0x`, and decimal, and must fit 16 bits. **No
whitespace is allowed inside the condition** (the definition is whitespace
tokenized). Terms may be separated by `;` as well as `,` — the `.ini` breakpoint
value is itself a comma-separated list, so persisted conditions use `;`.

```text
break-create write $D021 when=i==1
break-create write $D010 when=value!&1,mem($D000)>$F0
break-create write $00C3 when=raster>=250
```

`value` has no meaning on an instruction fetch, so combining it with `exec`
access is rejected. Rejections name the actual problem rather than only
`bad-args`:

```text
error bad-args invalid breakpoint definition: unknown condition term
error bad-args invalid breakpoint definition: unknown condition operator
error bad-args invalid breakpoint definition: condition needs a 16-bit immediate
error bad-args invalid breakpoint definition: too many condition terms (max 4)
error bad-args invalid breakpoint definition: mem() needs a 16-bit address
error bad-args invalid breakpoint definition: `value` has no meaning on an exec breakpoint
```

The guard is evaluated **before** the hit counter, so `hits=` and `counter=`
only advance on guarded matches — a count-only guarded breakpoint
(`actions=none`) counts exactly the accesses that satisfy the condition.

Guarded definitions round-trip through the debug `.ini`.

Richer frontend breakpoint parameters (Type text, Swap param, Tron path) are
persisted by the UI but are not all expressible through this control syntax.
Breakpoint data responses are newline-separated text records with metadata
`count=N`:

```text
id=1 enabled=1 start=C000 end=C000 has_end=0 access=1 mapping=0 actions=1 use_counter=0 hits=0 initial=0 reset=1 counter=0 cond=0 when=
```

`cond=` is the number of guard terms (0 for an unguarded breakpoint) and `when=`
echoes the condition in parse syntax, so a client can read back what it armed.
Immediates are echoed in hex, so `when=value==$06` reads back as `value==$6`.

`access` is a bit mask: **1=exec, 2=read, 4=write**. Do not reuse VICE checkpoint
op-mask numbers — VICE uses `load=1, store=2, exec=4` (the reverse assignment).
Mutating breakpoint commands wait for the corresponding breakpoint snapshot and
are therefore subject to the one-deferred-response rule. If the runtime rejects a
definition (invalid access bits, table full, …), the client receives
`error runtime <message>` instead of hanging until the deferred timeout.

## Waits and events

```text
N wait-paused [timeout-ms]
N wait-running [timeout-ms]
N wait-frame <positive-delta> [timeout-ms]
N wait-event <event-name> [timeout-ms]
```

Useful event names are `running`, `paused`, `reset-complete`, `step-complete`,
`run-complete`, `frame`, `breakpoints`, `disk-status`, `debug-memory`,
`assemble-complete`, `assemble-error`, `save-state-complete`, and
`load-state-complete`. A successful wait returns metadata such as
`state=paused frame=... stop=...`, `frame=... delta=...`, or
`event=load-state-complete seq=3`.

**Sticky completion events.** `load-state-complete`, `save-state-complete`,
`reset-complete`, `assemble-complete`, `assemble-error`, `step-complete`, and
`run-complete` are latched until a matching `wait-event` consumes them. That
fixes the race where `load-state` finishes before `wait-event load-state-complete`
is registered. Continuous events such as `frame` are not sticky: they only match
while a wait is active. A successful sticky wait includes `seq=<n>`.

**Sticky execution-state events.** `paused`, `running`, and `breakpoints` are
also latched, so `run` then `wait-event paused` (or `breakpoints`) catches the
stop even when it happens before the wait registers — e.g. a breakpoint or BRK
hit only a few cycles into the run. To avoid returning a *previous* stop, any
execution-control command (`reset`, `run`, `pause`, the `step-*`, `run-*`, and
`run-to*` commands) clears these latches when dispatched, so the wait targets the
stop that command produces. Previously a fast hit could time out the wait while
`get-cpu` already showed the PC parked on the breakpoint; polling was the
workaround, and is no longer needed.

`wait-paused` completes only after a machine-state snapshot so `stop=` reflects
the real stop reason (breakpoint, step, pause-command, …), not a stale `none`
from the preceding PAUSED pulse.

Timeout returns `error timeout deferred response timed out`.

A second wait while one is already deferred returns `error busy` (single-waiter
policy). Sticky latches remain one-consumer.

### Oracle / automation traps

- Prefer **`--headless`** for control-port latency; a windowed present is still
  ~16 ms class by design.
- Free-run `wait-frame 1` then `get-frame` can **alias frames** if one main-loop
  turn exceeds a frame period. Prefer **`step-frame`** for consecutive frames,
  or read the frame ring (§ Frame ring), which sees every completed frame even
  when the UI drops one.
- `run` → `wait-frame` → `pause` can still **overshoot by a frame** (pause is
  accepted after the wait). Not fixed by bulk memory or pipelining.
- Breakpoints accept a bounded guard (`when=`, see § Guarded breakpoints), not a
  general VICE-style `CONDITION_SET` expression: up to 4 ANDed terms, no OR.
- c64m vs VICE workflow: match VIC models and load flags in **`vice-oracle.md`**.
  Comparison friction is often VICE-side; that note is the mitigation.

## Assembler and symbols

```text
N assemble [address=<hex>] [run-address=<hex>] [auto-run=0|1] [basic-run=0|1] [reset=0|1] <source-path>
N find-symbol <exact-name>
```

Defaults are address `$8000`, run address equal to address, `auto-run=0`,
`basic-run=0`, and `reset=1`. The control server pauses before assembly.

**Run modes** (`auto-run` and `basic-run` are mutually exclusive; passing both
is `bad-args`):
- `auto-run=1` — after assembling, poke PC to `run-address` and resume (jumps
  straight into ML; skips the BASIC/KERNAL boot ceremony).
- `basic-run=1` — after assembling, stage the image as a LOAD would (fix the
  BASIC pointers TXTTAB/VARTAB/ARYTAB/STREND from the emitted origin and end)
  and `RUN` it through the BASIC editor via paste — no PC poke. This is the
  right mode for a BASIC-stub program (`10 SYS ....`). It needs the machine at a
  READY prompt: with `reset=1` (the default) that is guaranteed; the reset is
  skipped only when the machine is already at a fresh, undisturbed READY (e.g.
  right after launch), so a just-launched emulator does not pay for a redundant
  reset.

A successful response is `N ok address=$0801`, where the reported address is the
**actual emitted origin** (lowest emitted byte), not the requested `address=`
default that the source overrides. Assembly errors are
`N error assemble-error <diagnostic>`. Successful assembly publishes the symbol
snapshot used by `find-symbol`. `find-symbol` returns `not-ready` before any
symbol snapshot and `not-found` for an absent exact name.

## Failure handling and source-level constraints

Parser errors are returned immediately with `bad-id`, `bad-request`, `unknown-command`,
or `bad-args`. Runtime rejection uses `runtime command rejected`. A full request
queue returns `busy request queue full`. Malformed binary payload framing closes the
client after returning `bad-payload` where possible.

The socket thread owns network I/O only (may pipeline; never touches the machine).
Do not poll runtime events from a Python client's perspective as if they were
unsolicited events: responses are tied to requests, and event waits are the
supported synchronization mechanism. Do not add commands by editing only this
document; update `control_protocol`, main-loop dispatch, and the protocol tests
together.
