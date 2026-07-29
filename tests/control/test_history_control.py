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


def start_emulator(executable, directory, history_memory):
    port = reserve_port()
    process = subprocess.Popen(
        [
            executable,
            "--headless",
            f"--control-port={port}",
            "--noini",
            "--nosaveini",
            f"--history-memory={history_memory}",
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
    process, port = start_emulator(executable, directory, 16)
    client = None
    try:
        client = Ctl(port=port, timeout=5.0)
        hello = require_ok(client.cmd("hello"), "hello")
        assert "protocol=C64M/6" in hello
        assert "history" in require_ok(
            client.cmd("capabilities"), "capabilities").split()

        info = client.history_info()
        assert info["available"] == 1
        assert info["recording"] == 1
        initial_epoch = info["epoch"]
        require_ok(client.cmd("pause"), "initial pause")
        require_ok(client.cmd("wait-paused 3000"), "wait initial pause")
        boot = client.history_find(direction="forward", limit=8)
        reset_markers = [
            record for record in boot["records"]
            if record["kind"] == 3 and record["marker_kind"] == 4
        ]
        assert reset_markers
        assert reset_markers[0]["marker_arg0"] == 1
        for _ in range(6):
            require_ok(client.cmd("step-instruction"), "step-instruction")

        page = client.history_find(
            pc="$E000-$E020", direction="backward", limit=4)
        assert page["records"]
        assert all(record["anchor_match"] for record in page["records"])
        anchor = page["records"][0]["id"]
        context = client.history_read(
            anchor, epoch=page["epoch"], before=1, after=1)
        assert any(
            record["id"] == anchor and record["anchor_match"]
            for record in context["records"])
        assert all(
            record["id"] == anchor or not record["anchor_match"]
            for record in context["records"])

        cursor_page = client.history_find(limit=1)
        assert cursor_page["cursor"] != 0
        require_ok(client.cmd("step-instruction"), "cursor invalidating step")
        stale = client.cmd(
            f"history-next {cursor_page['cursor']} limit=1")
        assert stale[0] == "error"
        assert "stale history-cursor-stale" in stale[1]

        program = directory / "history-write.asm"
        program.write_text(
            ".org $C000\n"
            "LDA #$55\n"
            "STA $D015\n"
            "BRK\n",
            encoding="ascii",
        )
        before_assemble = client.history_info()["newest"]
        require_ok(
            client.cmd(
                f"assemble address=$C000 run-address=$C000 "
                f"auto-run=1 reset=0 {program}"),
            "assemble forensic write")
        require_ok(client.cmd("run-cycles 30"), "run forensic write")
        writes = client.history_find(
            address="$D015",
            access="write",
            value="55",
            direction="backward",
            limit=4,
        )
        assert writes["records"]
        assert writes["records"][0]["pc"] == 0xC002
        assert any(
            access["address"] == 0xD015
            and access["value"] == 0x55
            and access["kind"] == 1
            for access in writes["records"][0]["accesses"])
        assembly_commit = client.history_find(
            from_=before_assemble + 1, direction="forward", limit=1)
        assert assembly_commit["records"][0]["kind"] == 3
        assert assembly_commit["records"][0]["marker_kind"] == 8

        require_ok(
            client.cmd(
                f"assemble address=$C000 run-address=$C000 "
                f"auto-run=0 reset=1 {program}"),
            "reset-first assembly")
        require_ok(client.cmd("pause"), "pause after reset-first assembly")
        require_ok(
            client.cmd("wait-paused 3000"),
            "wait pause after reset-first assembly")
        reset_first_status = client.history_info()
        timeline = reset_first_status["timeline"]
        first_in_timeline = client.history_find(
            timeline=timeline, direction="forward", limit=1)
        reset_marker = first_in_timeline["records"][0]
        assert reset_marker["kind"] == 3
        assert reset_marker["marker_kind"] == 4
        assert reset_marker["marker_arg0"] == 6
        basic_entry = client.history_find(
            timeline=timeline,
            pc="$E38B",
            direction="forward",
            limit=1,
        )
        assembly_context = client.history_read(
            basic_entry["records"][0]["id"],
            epoch=basic_entry["epoch"],
            before=1,
            after=0,
        )
        assembly_marker = assembly_context["records"][0]
        assert assembly_marker["kind"] == 3
        assert assembly_marker["marker_kind"] == 8
        assert reset_marker["id"] < assembly_marker["id"]

        require_ok(client.cmd("restore"), "restore")
        require_ok(client.cmd("run-cycles 30"), "run NMI cycles")
        vectors = client.history_find(
            access="vector-read", direction="backward", limit=8)
        nmi = next(
            record for record in vectors["records"]
            if record["kind"] == 2)
        assert {
            access["address"] for access in nmi["accesses"]
            if access["kind"] == 8
        } == {0xFFFA, 0xFFFB}
        assert sum(
            1 for access in nmi["accesses"]
            if access["kind"] == 7) == 3

        snapshot = directory / "history-control.c64state"
        require_ok(client.cmd(f"save-state {snapshot}"), "save-state")
        after_save = client.history_info()
        assert after_save["epoch"] == initial_epoch
        require_ok(client.cmd(f"load-state {snapshot}"), "load-state")
        after_load = client.history_info()
        assert after_load["epoch"] == initial_epoch + 1
        assert after_load["timeline"] == 1
        loaded = client.history_find(direction="forward", limit=1)
        assert loaded["records"][0]["kind"] == 3
        assert loaded["records"][0]["marker_kind"] == 5
    finally:
        if client is not None:
            client.close()
        stop_emulator(process)


def run_unavailable(executable, directory, Ctl):
    process, port = start_emulator(executable, directory, 0)
    client = None
    try:
        client = Ctl(port=port, timeout=5.0)
        info = client.history_info()
        assert info["available"] == 0
        assert info["reason"] == "disabled-by-config"
        result = client.cmd("history-find limit=1")
        assert result[0] == "error"
        assert "unavailable history-recorder-unavailable" in result[1]
    finally:
        if client is not None:
            client.close()
        stop_emulator(process)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_history_control.py C64M_EXE REPO_ROOT")
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    repo_root = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(repo_root / "tools"))
    from c64_control_client import Ctl

    with tempfile.TemporaryDirectory(prefix="c64m-history-control-") as temp:
        directory = pathlib.Path(temp)
        write_roms(directory)
        run_available(executable, directory, Ctl)
        run_unavailable(executable, directory, Ctl)
    print("test_history_control: ok")


if __name__ == "__main__":
    main()
