#!/usr/bin/env python3
"""Minimal c64m control-port client (C64M/1 line protocol).

Debug/introspection helper for driving a headless c64m over its localhost
control port. Written for the lft-nine VIC-II investigation
(md-files/lft-nine.md); reusable for any control-port scripting.

Launch the emulator, e.g.:
    ./build/c64m --headless --control-port 17652 --pal -a --turbo=7 \
        -p samples/lft-nine.prg

Then:
    from c64_control_client import Ctl
    c = Ctl(port=17652)
    c.cmd("pause")
    print(c.cmd("get-cpu"))
    data = c.mem(0x8D00, 0x40)          # bytes at $8D00..$8D3F (mode "ram")
    c.cmd("break-create exec $9A05")    # exec breakpoints only (see gotchas)
    c.cmd("run"); c.cmd("wait-paused 5000")

GOTCHAS (learned the hard way, see lft-nine.md):
  * Rendering: at --turbo>=8 the live renderer is OFF and get-frame returns the
    geometric DEBUG snapshot (closed border, border-region sprites MASKED). Use
    --turbo<=7 for real frames. Register/memory reads are unaffected by turbo.
  * Addresses parse base-0: prefix hex with '$' (mem() does this for you).
  * get-memory length must be DECIMAL (mem() handles it); max 1024 bytes/call.
  * break-create only supports 'exec' breakpoints via the control port
    (control_parse_breakpoint_definition in src/main.c hard-requires "exec");
    read/write watchpoints would need a protocol extension.

Wire format:
  request : "<id> <command> [args]\n"
  ok      : "<id> ok [text]\n"
  error   : "<id> error <code> <message>\n"
  data    : "<id> data <type> <byte_count> [metadata]\n" + <bytes> + "\n"
"""
import socket, time

class Ctl:
    def __init__(self, host="127.0.0.1", port=17652, timeout=30.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(timeout)
        self.buf = b""
        self.id = 0

    def _readline(self):
        while b"\n" not in self.buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise EOFError("connection closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("latin1")

    def _readbytes(self, n):
        while len(self.buf) < n + 1:  # +1 for trailing newline
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError("connection closed")
            self.buf += chunk
        data = self.buf[:n]
        self.buf = self.buf[n+1:]  # drop trailing newline
        return data

    def cmd(self, text):
        self.id += 1
        rid = self.id
        self.s.sendall(f"{rid} {text}\n".encode("latin1"))
        line = self._readline()
        parts = line.split(" ")
        assert int(parts[0]) == rid, f"id mismatch: {line!r} for {text!r}"
        kind = parts[1]
        if kind == "ok":
            return ("ok", " ".join(parts[2:]))
        if kind == "error":
            return ("error", " ".join(parts[2:]))
        if kind == "data":
            byte_count = int(parts[3])
            meta = " ".join(parts[4:])
            payload = self._readbytes(byte_count)
            return ("data", meta, payload)
        raise ValueError(f"unknown response: {line!r}")

    def ok(self, text):
        r = self.cmd(text)
        if r[0] != "ok":
            raise RuntimeError(f"{text!r} -> {r}")
        return r[1]

    def mem(self, addr, length, mode="map"):
        # address: parse_u16 base-0, '$' forces hex. length: parse_u64 needs
        # a leading digit, so send decimal.
        r = self.cmd(f"get-memory ${addr:04X} {length:d} {mode}")
        if r[0] != "data":
            raise RuntimeError(f"get-memory -> {r}")
        return r[2]

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass
