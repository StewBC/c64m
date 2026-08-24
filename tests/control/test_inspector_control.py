#!/usr/bin/env python3
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


def reserve_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def write_roms(directory):
    system = bytearray([0xEA] * 16384)
    system[8192 + 0x1FFC] = 0x00
    system[8192 + 0x1FFD] = 0xE0
    (directory / "system.bin").write_bytes(system)
    (directory / "character.bin").write_bytes(bytes(4096))


def start_emulator(executable, directory):
    port = reserve_port()
    process = subprocess.Popen(
        [
            executable,
            "--headless",
            f"--control-port={port}",
            "--noini",
            "--nosaveini",
            "--inspector",
            "--inspector-memory=16",
        ],
        cwd=directory,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stderr = process.stderr.read()
            raise RuntimeError(f"c64m exited early: {stderr}")
        try:
            probe = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            probe.close()
            return process, port
        except OSError:
            time.sleep(0.02)
    process.terminate()
    raise RuntimeError("control port did not open")


def stop_emulator(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)


def require_ok(result, context):
    if result[0] != "ok":
        raise AssertionError(f"{context}: {result}")
    return result[1]


def run_available(executable, directory, Ctl):
    process, port = start_emulator(executable, directory)
    client = None
    try:
        client = Ctl(port=port, timeout=5.0)
        hello = require_ok(client.cmd("hello"), "hello")
        assert "protocol=C64M/8" in hello
        caps = require_ok(client.cmd("capabilities"), "capabilities")
        assert "inspector" in caps.split()
        require_ok(client.cmd("pause"), "initial pause")
        require_ok(client.cmd("wait-paused 3000"), "wait initial pause")
        require_ok(client.cmd("run"), "run")
        time.sleep(0.12)
        require_ok(client.cmd("pause"), "pause after run")
        require_ok(client.cmd("wait-paused 3000"), "wait pause")

        state = require_ok(client.cmd("get-state"), "get-state live")
        assert "mode=live" in state
        assert "focus_cycle=" in state
        assert "start=" in state

        require_ok(client.cmd("enter-inspector"), "enter-inspector")
        deadline = time.monotonic() + 3.0
        inspect = None
        while time.monotonic() < deadline:
            inspect = require_ok(client.cmd("get-state"), "get-state inspect")
            if "mode=inspector" in inspect:
                break
            time.sleep(0.05)
        assert inspect is not None and "mode=inspector" in inspect, inspect
        assert "focus_cycle=" in inspect

        poke = client.cmd("key-down a")
        assert poke[0] == "error"
        assert "read-only-inspector" in poke[1]

        leave = require_ok(client.cmd("leave-inspector"), "leave-inspector")
        assert "accepted=1" in leave
        deadline = time.monotonic() + 3.0
        live = None
        while time.monotonic() < deadline:
            live = require_ok(client.cmd("get-state"), "get-state after leave")
            if "mode=live" in live:
                break
            time.sleep(0.05)
        assert live is not None and "mode=live" in live, live
    finally:
        if client is not None:
            try:
                client.close()
            except Exception:
                pass
        stop_emulator(process)


def main():
    if len(sys.argv) < 2:
        print("usage: test_inspector_control.py <c64m> [repo]", file=sys.stderr)
        return 2
    executable = sys.argv[1]
    repo = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else ".")
    sys.path.insert(0, str(repo / "tools"))
    from c64_control_client import Ctl

    with tempfile.TemporaryDirectory() as tmp:
        directory = pathlib.Path(tmp)
        write_roms(directory)
        run_available(executable, directory, Ctl)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
