#!/usr/bin/env python3
"""Minimal c64m control-port client (C64M/6 line protocol).

Debug/introspection helper for driving a headless c64m over its localhost
control port. Written for the lft-nine VIC-II investigation
(md-files/lft-nine.md); reusable for any control-port scripting.

Launch the emulator, e.g.:
    ./build/c64m --headless --control-port 17652 --pal -a --turbo=2 \
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
  * Rendering: turbo modes are 1=normal, 2=max (free-run, live pixels), 3=warp
    (free-run, paint off). At turbo=3 get-frame returns the geometric DEBUG
    snapshot (closed border, border-region sprites MASKED). Use turbo 1 or 2 for
    real frames. Register/memory reads are unaffected by turbo.
  * Addresses parse base-0: prefix hex with '$' (mem() does this for you).
  * get-memory length must be DECIMAL (mem() handles it); max 65536 bytes/call
    with address+length <= 65536 (full dump: mem(0, 65536)).
  * break-create only supports 'exec' breakpoints via the control port
    (control_parse_breakpoint_definition in src/main.c hard-requires "exec");
    read/write watchpoints would need a protocol extension.

Wire format:
  request : "<id> <command> [args]\n"
  ok      : "<id> ok [text]\n"
  error   : "<id> error <code> <message>\n"
  data    : "<id> data <type> <byte_count> [metadata]\n" + <bytes> + "\n"
"""
import socket, struct, time

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
        if self.buf[n:n + 1] != b"\n":
            raise ValueError("binary response missing trailing newline")
        self.buf = self.buf[n+1:]  # drop trailing newline
        return data

    def _parse_response_header(self, line, expect_id=None):
        parts = line.split(" ")
        rid = int(parts[0])
        if expect_id is not None:
            assert rid == expect_id, f"id mismatch: {line!r}"
        kind = parts[1]
        if kind == "ok":
            return rid, ("ok", " ".join(parts[2:]))
        if kind == "error":
            return rid, ("error", " ".join(parts[2:]))
        if kind == "data":
            byte_count = int(parts[3])
            meta = " ".join(parts[4:])
            payload = self._readbytes(byte_count)
            return rid, ("data", meta, payload)
        raise ValueError(f"unknown response: {line!r}")

    def cmd(self, text):
        self.id += 1
        rid = self.id
        self.s.sendall(f"{rid} {text}\n".encode("latin1"))
        line = self._readline()
        _, result = self._parse_response_header(line, expect_id=rid)
        return result

    def pipeline(self, commands):
        """Send many requests without waiting, then collect responses by id.

        Phase 2b: server may complete out of order; returns list of results in
        the same order as `commands`.
        """
        ids = []
        for text in commands:
            self.id += 1
            rid = self.id
            ids.append(rid)
            self.s.sendall(f"{rid} {text}\n".encode("latin1"))
        by_id = {}
        pending = set(ids)
        while pending:
            line = self._readline()
            rid, result = self._parse_response_header(line)
            if rid not in pending:
                raise RuntimeError(f"unexpected response id {rid}: {line!r}")
            by_id[rid] = result
            pending.remove(rid)
        return [by_id[i] for i in ids]

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

    @staticmethod
    def _metadata(text):
        result = {}
        if not text:
            return result
        for token in text.split():
            if "=" not in token:
                raise ValueError(f"malformed response metadata: {token!r}")
            key, value = token.split("=", 1)
            result[key] = value
        return result

    @staticmethod
    def decode_hst1(payload):
        """Decode and validate one HST1 version-1 history payload."""
        if len(payload) < 24 or payload[:4] != b"HST1":
            raise ValueError("invalid HST1 magic/header")
        version, flags = struct.unpack_from("<HH", payload, 4)
        epoch, count, reserved = struct.unpack_from("<QII", payload, 8)
        if version != 1:
            raise ValueError(f"unsupported HST1 version {version}")
        if flags != 0 or reserved != 0:
            raise ValueError("nonzero HST1 reserved header field")

        records = []
        offset = 24
        for _ in range(count):
            if offset + 48 > len(payload):
                raise ValueError("truncated HST1 record header")
            record_size = struct.unpack_from("<H", payload, offset)[0]
            kind = payload[offset + 2]
            record_flags = payload[offset + 3]
            access_count = payload[offset + 35]
            expected_size = 48 + access_count * 8
            if kind > 3:
                raise ValueError(f"invalid HST1 record kind {kind}")
            if record_size != expected_size or offset + record_size > len(payload):
                raise ValueError("invalid HST1 record size/access count")
            if struct.unpack_from("<H", payload, offset + 38)[0] != 0:
                raise ValueError("nonzero HST1 record reserved field")

            record = {
                "kind": kind,
                "partial": bool(record_flags & 0x01),
                "access_truncated": bool(record_flags & 0x02),
                "anchor_match": bool(record_flags & 0x04),
                "timing_truncated": bool(record_flags & 0x08),
                "timeline": struct.unpack_from("<I", payload, offset + 4)[0],
                "id": struct.unpack_from("<Q", payload, offset + 8)[0],
                "machine_cycle": struct.unpack_from("<Q", payload, offset + 16)[0],
                "pc": struct.unpack_from("<H", payload, offset + 24)[0],
                "a": payload[offset + 26],
                "x": payload[offset + 27],
                "y": payload[offset + 28],
                "sp": payload[offset + 29],
                "p": payload[offset + 30],
                "opcode": payload[offset + 31],
                "operands": bytes(payload[offset + 32:offset + 34]),
                "instruction_length": payload[offset + 34],
                "marker_kind": struct.unpack_from("<H", payload, offset + 36)[0],
                "marker_arg0": struct.unpack_from("<I", payload, offset + 40)[0],
                "marker_arg1": struct.unpack_from("<I", payload, offset + 44)[0],
                "accesses": [],
            }
            access_offset = offset + 48
            for _access in range(access_count):
                address, cycle_offset = struct.unpack_from(
                    "<HH", payload, access_offset)
                value = payload[access_offset + 4]
                access_kind = payload[access_offset + 5]
                access_reserved = struct.unpack_from(
                    "<H", payload, access_offset + 6)[0]
                if access_kind > 8 or access_reserved != 0:
                    raise ValueError("invalid HST1 access entry")
                record["accesses"].append({
                    "address": address,
                    "cycle_offset": cycle_offset,
                    "value": value,
                    "kind": access_kind,
                })
                access_offset += 8
            records.append(record)
            offset += record_size
        if offset != len(payload):
            raise ValueError("trailing bytes in HST1 payload")
        return {"epoch": epoch, "records": records}

    def history_info(self):
        result = self.cmd("history-info")
        if result[0] != "ok":
            raise RuntimeError(f"history-info -> {result}")
        metadata = self._metadata(result[1])
        for key, value in list(metadata.items()):
            if key != "reason":
                metadata[key] = int(value, 0)
        return metadata

    def _history_result(self, command):
        result = self.cmd(command)
        if result[0] != "data":
            raise RuntimeError(f"{command!r} -> {result}")
        metadata = self._metadata(result[1])
        for key in ("epoch", "count", "cursor", "more", "oldest", "newest"):
            if key not in metadata:
                raise ValueError(f"missing history metadata {key}")
            metadata[key] = int(metadata[key], 0)
        decoded = self.decode_hst1(result[2])
        if decoded["epoch"] != metadata["epoch"]:
            raise ValueError("HST1 epoch disagrees with response metadata")
        if len(decoded["records"]) != metadata["count"]:
            raise ValueError("HST1 count disagrees with response metadata")
        metadata["records"] = decoded["records"]
        return metadata

    def history_find(self, **options):
        tokens = []
        for key, value in options.items():
            wire_key = key[:-1] if key.endswith("_") else key
            if wire_key == "direction" and value not in ("forward", "backward"):
                raise ValueError("direction must be forward or backward")
            if isinstance(value, (list, tuple)):
                value = ",".join(str(item) for item in value)
            tokens.append(f"{wire_key}={value}")
        command = "history-find"
        if tokens:
            command += " " + " ".join(tokens)
        return self._history_result(command)

    def history_next(self, cursor, limit=64):
        return self._history_result(
            f"history-next {int(cursor)} limit={int(limit)}")

    def history_read(self, record_id, epoch=None, before=32, after=8):
        options = []
        if epoch is not None:
            options.append(f"epoch={int(epoch)}")
        options.extend((f"before={int(before)}", f"after={int(after)}"))
        return self._history_result(
            f"history-read {int(record_id)} " + " ".join(options))

    def history_record(self, enabled):
        return self.ok(f"history-record {'on' if enabled else 'off'}")

    def history_clear(self):
        return self.ok("history-clear")

    def history_close(self, cursor):
        return self.ok(f"history-close {int(cursor)}")

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass
