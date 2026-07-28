#!/usr/bin/env python3
"""Cooperative live-debug watcher for c64m (the "coder at your shoulder" loop).

You play a windowed c64m; when you see a glitch you hit the frontend pause key.
This daemon is blocked on `wait-paused`, wakes on that transition, freezes-in-
place, and writes a full evidence pack (CPU, VIC, sprite registers, your named
game regions, and the recent write history from the flight recorder) to
`build/debug/snap-NNN.txt`. The machine stays frozen while you describe what you
saw in chat and the coder looks around. The coder steers the daemon through a
file inbox: arm a watchpoint to your description, dump more state, or resume.

Nothing here needs a c64m change; it is pure control-port orchestration on top
of `tools/c64_control_client.Ctl`. See `agents/control-port.md`.

LAUNCH c64m (windowed, real-time so you play at speed and see real pixels):

    ./build/c64m --control-port 6510

Then run this daemon (typically in the background so its log streams):

    tools/coop_watch.py --port 6510

WORKFLOW
    1. You play. Daemon prints "watching ...".
    2. You see a glitch -> hit the c64m pause key.
    3. Daemon prints "SNAP NNN stop=pause ..." and writes snap-NNN.txt.
       Machine is now frozen; daemon polls the inbox.
    4. You describe the glitch in chat. The coder reads snap-NNN.txt + your
       words, then drives the daemon via the inbox file.
    5. Coder writes `resume` (or arms a watchpoint first) -> you play on.

    When a bug is intermittent, the coder arms a watchpoint matching your
    description (e.g. `arm write $00C3` for a bogus score write), resumes, and
    you reproduce: the machine auto-freezes at the exact instruction and the
    next SNAP is the smoking gun (stop=breakpoint) rather than the aftermath.

INBOX PROTOCOL (append one command per line to build/debug/coop_inbox)
    resume                         run and go back to watching
    arm  <access> <addr> [end=A]   break-create ... actions=break  (access: exec|read|write|read-write)
    count <access> <addr> [end=A]  break-create ... actions=none   (count-only, no pause)
    clear                          break-clear-all
    dump <addr> <len> [mode]       extra get-memory into the current snap file
    hist <addr> [access] [limit]   extra history-find/read into the current snap file
    note <text...>                 append a line (e.g. your description) into the snap file
    quit                           clean shutdown

Commands are consumed only while the machine is paused (i.e. after a freeze),
which is exactly when arming/poking is valid. Anything you append while you are
still playing is picked up at the next freeze.
"""
import argparse
import os
import sys
import time
import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from c64_control_client import Ctl  # noqa: E402


# --------------------------------------------------------------------------
# CONFIG -- the coder fills in `regions` and `trace_writes` from the game's
# symbol table. Everything else is generic and rarely needs editing.
# --------------------------------------------------------------------------
CONFIG = {
    "port": 6510,
    "out_dir": "build/debug",
    "wait_ms": 600000,          # per wait-paused; loop re-arms on timeout (max 600000)
    # Named game-state regions dumped on every freeze: name -> (addr, length).
    "regions": {
        # "score":        (0x00C3, 2),
        # "pickup_flags": (0x00D0, 4),
        # "mux_table":    (0x0340, 16),
    },
    # Addresses whose recent WRITE history is decoded on every freeze:
    # each entry is (label, addr) or (label, addr, end) for a range.
    "trace_writes": [
        # ("score",      0x00C3),
        ("sprite-regs", 0xD000, 0xD02E),
    ],
    "trace_limit": 48,          # history records per traced address
}


def hexdump(addr, data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {addr + i:04X}  {hexs:<47}  {text}")
    return "\n".join(lines)


def decode_sprites(regs):
    """regs is the 47 bytes at $D000..$D02E. Return a readable sprite table."""
    if len(regs) < 0x2F:
        return "  (sprite register block short)"
    msb = regs[0x10]
    enable = regs[0x15]
    yexp = regs[0x17]
    xexp = regs[0x1D]
    pri = regs[0x1B]
    mc = regs[0x1C]
    out = ["  spr  en  x    y    xexp yexp pri  mc  color",
           "  ---  --  ---  ---  ---- ---- ---  --  -----"]
    for s in range(8):
        x = regs[s * 2] | (0x100 if (msb >> s) & 1 else 0)
        y = regs[s * 2 + 1]
        col = regs[0x27 + s] & 0x0F
        out.append(
            f"  {s}    {'Y' if (enable >> s) & 1 else '.'}   "
            f"{x:<4} {y:<4} {'Y' if (xexp >> s) & 1 else '.'}    "
            f"{'Y' if (yexp >> s) & 1 else '.'}    "
            f"{'bg' if (pri >> s) & 1 else 'fg'}  "
            f"{'Y' if (mc >> s) & 1 else '.'}   ${col:X}")
    out.append(f"  raw $D015 enable={enable:02X} $D010 msb={msb:02X} "
               f"$D01B pri={pri:02X} $D01C mc={mc:02X}")
    return "\n".join(out)


class CoopWatch:
    def __init__(self, cfg):
        self.cfg = cfg
        self.out_dir = cfg["out_dir"]
        self.inbox = os.path.join(self.out_dir, "coop_inbox")
        os.makedirs(self.out_dir, exist_ok=True)
        # Socket must outlast a full server-side wait, plus margin.
        sock_timeout = cfg["wait_ms"] / 1000.0 + 30.0
        self.c = Ctl(port=cfg["port"], timeout=sock_timeout)
        self.snap_no = self._next_snap_no()
        self.cur_snap = None    # path of the snap file for the current freeze

    # -- setup --------------------------------------------------------------
    def setup(self):
        self.c.cmd("set-turbo 1")           # real-time, live ARGB (never warp)
        self.c.cmd("history-record on")     # default-on; explicit for the log
        info = self._safe(lambda: self.c.history_info())
        self._log(f"connected port={self.cfg['port']} turbo=1 history=on "
                  f"recorder={info}")

    def _next_snap_no(self):
        n = 0
        for name in os.listdir(self.out_dir) if os.path.isdir(self.out_dir) else []:
            if name.startswith("snap-") and name.endswith(".txt"):
                try:
                    n = max(n, int(name[5:-4]))
                except ValueError:
                    pass
        return n + 1

    def _log(self, msg):
        print(f"[coop] {msg}", flush=True)

    def _safe(self, fn, default=None):
        try:
            return fn()
        except Exception as exc:  # keep the daemon alive through transient errors
            return f"<error: {exc}>" if default is None else default

    # -- main loop ----------------------------------------------------------
    def watch_loop(self):
        self._log("watching -- hit the c64m pause key when you see a glitch "
                  "(Ctrl-C to stop)")
        while True:
            r = self.c.cmd(f"wait-paused {self.cfg['wait_ms']}")
            if r[0] == "error":
                # timeout just means nothing stopped in this window: re-arm.
                if "timeout" in (r[1] or ""):
                    continue
                self._log(f"wait-paused error: {r[1]} -- retrying in 1s")
                time.sleep(1.0)
                continue
            meta = Ctl._metadata(r[1])
            self.dump_pack(meta)
            self.paused_command_loop()

    # -- evidence pack ------------------------------------------------------
    def dump_pack(self, wait_meta):
        path = os.path.join(self.out_dir, f"snap-{self.snap_no:03d}.txt")
        self.cur_snap = path
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        stop = wait_meta.get("stop", "?")
        lines = [
            f"=== SNAP {self.snap_no:03d}  {stamp}",
            f"stop={stop}  " + " ".join(f"{k}={v}" for k, v in wait_meta.items()
                                        if k != "stop"),
            "",
            "--- CPU",
            "  " + self._ok("get-cpu"),
            "",
            "--- VIC",
            "  " + self._ok("get-vic"),
            "",
            "--- CIA",
            "  1: " + self._ok("get-cia 1"),
            "  2: " + self._ok("get-cia 2"),
            "",
            "--- SPRITES ($D000..$D02E)",
        ]
        regs = self._safe(lambda: self.c.mem(0xD000, 0x2F, "map"), default=b"")
        lines.append(decode_sprites(regs))
        lines.append("")

        if self.cfg["regions"]:
            lines.append("--- GAME REGIONS")
            for name, (addr, length) in self.cfg["regions"].items():
                data = self._safe(lambda a=addr, n=length: self.c.mem(a, n, "map"),
                                  default=b"")
                lines.append(f"  {name} (${addr:04X}, {length}):")
                lines.append(hexdump(addr, data) if data else "    <read failed>")
            lines.append("")

        lines.append("--- RECENT WRITES (flight recorder)")
        for entry in self.cfg["trace_writes"]:
            lines.append(self._trace_writes(*entry))
        lines.append("")

        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        self._log(f"SNAP {self.snap_no:03d} stop={stop} "
                  f"frame={wait_meta.get('frame', '?')} -> {path}")
        self.snap_no += 1

    def _ok(self, command):
        r = self.c.cmd(command)
        return r[1] if r[0] == "ok" else f"<{r[0]}: {r[1] if len(r) > 1 else ''}>"

    def _trace_writes(self, label, addr, end=None, limit=None):
        limit = limit or self.cfg["trace_limit"]
        opts = dict(address=addr, access="write", direction="backward",
                    from_="newest", limit=limit)
        if end is not None:
            opts["end"] = end
        try:
            res = self.c.history_find(**opts)
        except Exception as exc:
            return f"  {label}: history-find failed: {exc}"
        span = f"${addr:04X}" + (f"..${end:04X}" if end is not None else "")
        out = [f"  {label} writes to {span}  (newest first, "
               f"{len(res['records'])} of budget):"]
        lo, hi = addr, (end if end is not None else addr)
        for rec in res["records"]:
            hits = [a for a in rec["accesses"]
                    if lo <= a["address"] <= hi and a["kind"] in (4,)]  # data-write
            if not hits:
                # find matched on a write; surface whatever writes it holds
                hits = [a for a in rec["accesses"] if lo <= a["address"] <= hi]
            wr = ", ".join(f"${a['address']:04X}=${a['value']:02X}" for a in hits)
            out.append(f"    id={rec['id']} pc=${rec['pc']:04X} "
                       f"cyc={rec['machine_cycle']} op=${rec['opcode']:02X}  {wr}")
        if not res["records"]:
            out.append("    (no writes in the recorder window)")
        return "\n".join(out)

    # -- paused: consume coder commands until resume ------------------------
    def paused_command_loop(self):
        self._log(f"paused -- append commands to {self.inbox} "
                  f"(arm/count/clear/dump/hist/note/resume/quit)")
        while True:
            for cmd in self._drain_inbox():
                action = self._handle(cmd)
                if action == "resume":
                    self.c.cmd("run")
                    self._log("resume -> running")
                    return
                if action == "quit":
                    self._log("quit")
                    self.c.close()
                    sys.exit(0)
            # If the user resumed from the c64m window, rejoin the watch loop.
            st = Ctl._metadata(self._safe(lambda: self._ok("get-state"), default=""))
            if st.get("state") == "running":
                self._log("machine resumed from the frontend -> watching")
                return
            time.sleep(0.3)

    def _drain_inbox(self):
        if not os.path.exists(self.inbox):
            return []
        tmp = self.inbox + ".proc"
        try:
            os.rename(self.inbox, tmp)          # atomic; new appends land in a fresh file
        except OSError:
            return []
        with open(tmp) as f:
            lines = [ln.strip() for ln in f if ln.strip()]
        os.remove(tmp)
        return lines

    def _handle(self, line):
        self._log(f"inbox: {line}")
        parts = line.split()
        cmd = parts[0].lower()
        try:
            if cmd == "resume":
                return "resume"
            if cmd == "quit":
                return "quit"
            if cmd in ("arm", "count"):
                action = "break" if cmd == "arm" else "none"
                access, addr = parts[1], parts[2]
                extra = " ".join(parts[3:])          # e.g. end=$D02E
                r = self.c.cmd(f"break-create {access} {addr} actions={action}"
                               + (f" {extra}" if extra else ""))
                self._log(f"{cmd} {access} {addr} {extra} -> {r[1] if len(r) > 1 else r[0]}")
            elif cmd == "clear":
                self._log("clear -> " + self._ok("break-clear-all"))
            elif cmd == "dump":
                addr = int(parts[1].lstrip("$").replace("0x", ""), 16)
                length = int(parts[2])
                mode = parts[3] if len(parts) > 3 else "map"
                data = self.c.mem(addr, length, mode)
                self._append_snap(f"--- dump ${addr:04X} {length} {mode}\n"
                                  + hexdump(addr, data))
            elif cmd == "hist":
                addr = int(parts[1].lstrip("$").replace("0x", ""), 16)
                access = parts[2] if len(parts) > 2 else "write"
                limit = int(parts[3]) if len(parts) > 3 else self.cfg["trace_limit"]
                res = self.c.history_find(address=addr, access=access,
                                          direction="backward", from_="newest",
                                          limit=limit)
                body = [f"    id={r['id']} pc=${r['pc']:04X} cyc={r['machine_cycle']} "
                        + ", ".join(f"${a['address']:04X}=${a['value']:02X}"
                                    for a in r["accesses"] if a["address"] == addr)
                        for r in res["records"]]
                self._append_snap(f"--- hist ${addr:04X} {access} (newest first)\n"
                                  + "\n".join(body))
            elif cmd == "note":
                self._append_snap("NOTE: " + " ".join(parts[1:]))
            else:
                self._log(f"unknown inbox command: {line!r}")
        except Exception as exc:
            self._log(f"inbox command failed ({line!r}): {exc}")
        return None

    def _append_snap(self, text):
        target = self.cur_snap or os.path.join(self.out_dir, "coop_extra.txt")
        with open(target, "a") as f:
            f.write("\n" + text + "\n")
        self._log(f"appended to {target}")


def main():
    ap = argparse.ArgumentParser(description="c64m cooperative live-debug watcher")
    ap.add_argument("--port", type=int, default=CONFIG["port"])
    ap.add_argument("--out-dir", default=CONFIG["out_dir"])
    ap.add_argument("--wait-ms", type=int, default=CONFIG["wait_ms"])
    args = ap.parse_args()
    cfg = dict(CONFIG, port=args.port, out_dir=args.out_dir, wait_ms=args.wait_ms)

    watcher = CoopWatch(cfg)
    watcher.setup()
    try:
        watcher.watch_loop()
    except KeyboardInterrupt:
        watcher._log("stopped")
    finally:
        watcher.c.close()


if __name__ == "__main__":
    main()
