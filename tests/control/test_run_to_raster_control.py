#!/usr/bin/env python3
"""Regression test for the run-to-raster control-port wedge/desync.

`run-to-raster` was accepted by the runtime but omitted from the response-
formatting tail switch in dispatch_control_request(), so it posted a zeroed
response with wire id 0. That desynced single-response clients ("unsolicited
0 ok" / id mismatch) and, because id 0 never matched the handler's outstanding
set, left `outstanding` permanently non-zero. When such a client disconnected,
the sole-client connection handler looped forever waiting to drain, wedging the
control port for every future client.

This test asserts:
  1. run-to-raster returns an ok response whose wire id matches the request
     (Ctl.cmd asserts the id; a zeroed id-0 response raises "id mismatch"),
     and whose body reports accepted=1.
  2. After a client disconnects with a run-to-raster still in flight, a fresh
     client is still served promptly (the port is not wedged).
"""
import pathlib
import socket
import subprocess
import sys
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


def test_matching_id(port, Ctl):
    """run-to-raster must answer with the request's own wire id and accepted=1."""
    client = Ctl(port=port, timeout=5.0)
    try:
        assert "protocol=C64M/7" in require_ok(client.cmd("hello"), "hello")
        # Ctl.cmd asserts the response id equals the request id; the id-0
        # regression tripped "id mismatch" here.
        body = require_ok(client.cmd("run-to-raster 100"), "run-to-raster")
        assert "accepted=1" in body, f"expected accepted=1, got {body!r}"
    finally:
        client.close()


def test_no_wedge_after_inflight_disconnect(executable, directory, Ctl, port):
    """A client that disconnects mid run-to-raster must not wedge the port."""
    # Send run-to-raster raw and drop the connection without reading a reply,
    # leaving a request in flight from the server's point of view.
    raw = socket.create_connection(("127.0.0.1", port), timeout=5.0)
    raw.sendall(b"1 hello\n")
    raw.recv(256)
    raw.sendall(b"2 run-to-raster 200\n")
    raw.close()

    # A fresh client must still be accepted and served promptly. Before the fix
    # the handler for the dropped connection looped forever and this timed out.
    deadline = time.monotonic() + 8.0
    last_error = None
    while time.monotonic() < deadline:
        try:
            client = Ctl(port=port, timeout=4.0)
            try:
                assert "protocol=C64M/7" in require_ok(
                    client.cmd("hello"), "post-disconnect hello")
                require_ok(client.cmd("run-to-raster 150"),
                           "post-disconnect run-to-raster")
                return
            finally:
                client.close()
        except (OSError, AssertionError) as exc:
            last_error = exc
            time.sleep(0.1)
    raise AssertionError(
        f"control port wedged after in-flight disconnect: {last_error!r}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: test_run_to_raster_control.py C64M_EXE REPO_ROOT")
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    repo_root = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(repo_root / "tools"))
    from c64_control_client import Ctl

    import tempfile
    with tempfile.TemporaryDirectory(prefix="c64m-rtr-control-") as temp:
        directory = pathlib.Path(temp)
        write_roms(directory)
        process, port = start_emulator(executable, directory)
        try:
            test_matching_id(port, Ctl)
            test_no_wedge_after_inflight_disconnect(
                executable, directory, Ctl, port)
        finally:
            stop_emulator(process)
    print("test_run_to_raster_control: ok")


if __name__ == "__main__":
    main()
