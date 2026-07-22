# Control port: operational handoff

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

Run from the repository root so default ROM lookup finds `roms/`.

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

The server queues requests from the socket thread and dispatches them on the SDL
main loop. Only one deferred response can be active at a time. A second deferred
request receives:

```text
<id> error busy deferred-response-active
```

The standard deferred timeout is 2000 ms. Assembly uses 10000 ms. Wait commands
accept 1..600000 ms and default to 2000 ms.

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

The code assumes one outstanding request at a time. If a client pipelines
requests, it must correlate IDs and still consume responses in wire order.

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
N run-cycles <positive-count>
N run-instructions <positive-count>
N run-to <address>
N set-turbo <mode 1|2|3>
```

Current fixed responses:

```text
hello        -> ok name=c64m protocol=C64M/1
version      -> ok protocol=C64M/1 app=0.1.0
capabilities -> ok connection introspection execution state step turbo frame memory debug-memory call-stack input disk file snapshot breakpoints wait assemble symbols drive-cpu
ping         -> ok
```

`get-state` is an immediate cached response such as:

```text
N ok state=paused has_cpu=1 frame=123 cycle=456 stop=breakpoint turbo=1
```

Run/step/load/input commands return `ok accepted=1` when the runtime command was
queued, not when the operation has completed. Follow with `wait-event`,
`get-state`, or a specific query.

`set-turbo` changes the active turbo mode without modifying the configured Opt+T
list. Modes are:

| Mode | Name   | Meaning |
|------|--------|---------|
| 1    | normal | Real-time pace, live ARGB |
| 2    | max    | Free-run, live ARGB (full correctness) |
| 3    | warp   | Free-run, paint off (debug frames only) |

At modes 1 and 2 the response is:

```text
N ok accepted=1 turbo=2
```

At mode 3 (warp) it includes a warning:

```text
N ok accepted=1 turbo=3 warning=warp-disables-live-ARGB-framebuffer;get-frame-is-debug-only-until-turbo-is-1-or-2
```

In warp, VIC-II timing still advances, but the live per-cycle ARGB renderer is
disabled and `get-frame` returns a geometric debug snapshot. Lowering turbo to
1 or 2 restores live rendering for subsequent frames.

## State, memory, and frames

```text
N get-cpu
N get-frame [format=argb8888]
N get-memory <address> <length 1..1024> <map|ram|rom|drive8|drive9>
N get-debug-memory [write-history=0|1]
N get-call-stack
N get-drive-cpu <8|9>
```

`get-cpu` returns text:

```text
N ok pc=E37B a=00 x=00 y=00 sp=F9 p=24 cycles=12345
```

`get-memory` returns `data memory` with metadata `addr=... length=... mode=...`.
The payload is exactly the requested bytes. Modes are CPU-visible map (`0`), raw
RAM (`1`), raw ROM (`2`), drive 8 map (`3`), and drive 9 map (`4`). Drive maps
contain holes; the machine-side debug API marks invalid bytes, but the control
payload contains the returned byte values only.

`get-frame` returns `data frame` with metadata:

```text
width=384 height=312 stride=1536 format=argb8888 frame=... cycle=...
```

The payload is row-major 32-bit ARGB8888 bytes, `height * stride` bytes. PAL is
normally 384x312; NTSC is 384x263. The frontend crop is not applied to this payload.
At turbo 3 (warp) this is a geometric debug snapshot rather than the live ARGB
framebuffer; use `set-turbo 1` or `set-turbo 2` before inspecting live pixels.

`get-debug-memory` concatenates three 65536-byte arrays in this order: CPU map,
raw RAM, raw ROM. With `write-history=1`, it appends 65536 little-endian `uint64`
write-history values. Metadata includes `generation`, `map`, `ram`, `rom`, and
`write_history` flags.

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
N get-disk-status <8|9>
```

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
N break-create exec <address> [enabled=0|1] [end=<address>] [actions=<list>] [counter=<n>] [reset=<n>]
N break-update <id> exec <address> [enabled=0|1] [end=<address>] [actions=<list>] [counter=<n>] [reset=<n>]
```

`actions` is a comma-separated subset of `break,fast,slow,tron,troff,type,swap`.
The control parser currently accepts the core definition fields above; richer
frontend breakpoint parameters are persisted by the UI but are not all expressible
through this control syntax. Breakpoint data responses are newline-separated text
records with metadata `count=N`:

```text
id=1 enabled=1 start=C000 end=C000 has_end=0 access=1 mapping=0 actions=1 use_counter=0 hits=0 initial=0 reset=1 counter=0
```

Mutating breakpoint commands wait for the corresponding breakpoint snapshot and are
therefore subject to the one-deferred-response rule.

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
`state=paused frame=... stop=...`, `frame=... delta=...`, or `event=frame`.
Timeout returns `error timeout deferred response timed out`.

## Assembler and symbols

```text
N assemble [address=<hex>] [run-address=<hex>] [auto-run=0|1] [reset=0|1] <source-path>
N find-symbol <exact-name>
```

Defaults are address `$8000`, run address equal to address, `auto-run=0`, and
`reset=1`. The control server pauses before assembly. A successful response is
`N ok address=$C000`; assembly errors are `N error assemble-error <diagnostic>`.
Successful assembly publishes the symbol snapshot used by `find-symbol`.
`find-symbol` returns `not-ready` before any symbol snapshot and `not-found` for
an absent exact name.

## Failure handling and source-level constraints

Parser errors are returned immediately with `bad-id`, `bad-request`, `unknown-command`,
or `bad-args`. Runtime rejection uses `runtime command rejected`. A full request
queue returns `busy request queue full`. Malformed binary payload framing closes the
client after returning `bad-payload` where possible.

The socket thread owns blocking network I/O only. Do not poll runtime events from a
Python client's perspective as if they were unsolicited events: responses are tied
to requests, and event waits are the supported synchronization mechanism. Do not
add commands by editing only this document; update `control_protocol`, main-loop
dispatch, and the protocol tests together.
