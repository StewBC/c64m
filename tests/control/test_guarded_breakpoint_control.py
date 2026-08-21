#!/usr/bin/env python3
"""End-to-end test for guarded (condition) breakpoints over the control port.

A breakpoint's address/access/mapping test says *where*; the `when=` guard says
*under what circumstances*. This test proves the guard actually gates both the
hit counter and the stop, using a deterministic program planted in the stub
KERNAL so the machine resets straight into it:

    E000  LDA #$00
    E002  STA $C000     ; write 0
    E005  LDA #$06
    E007  STA $C000     ; write 6
    E00A  JMP $E000

Every loop performs exactly two writes to $C000, one carrying $00 and one
carrying $06, so a guard on the written byte has an exact expected effect:

  * unguarded            -> counts every write
  * when=value==$06      -> counts (and stops on) half of them
  * when=value==$42      -> never counts, never stops

$C000 is RAM in every bank configuration, so the test does not depend on I/O
or banking behavior.
"""
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


LOOP = bytes([
    0xA9, 0x00,             # LDA #$00
    0x8D, 0x00, 0xC0,       # STA $C000
    0xA9, 0x06,             # LDA #$06
    0x8D, 0x00, 0xC0,       # STA $C000
    0x4C, 0x00, 0xE0,       # JMP $E000
])


def reserve_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def write_roms(directory):
    """system.bin is BASIC ($A000, offset 0) + KERNAL ($E000, offset 8192)."""
    system = bytearray([0xEA] * 16384)
    system[8192:8192 + len(LOOP)] = LOOP      # program at $E000
    system[8192 + 0x1FFC] = 0x00              # reset vector -> $E000
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


def require_error(result, context):
    if result[0] != "error":
        raise AssertionError(f"{context}: expected an error, got {result}")
    return result[1]


def require_accepted(result, context):
    """Breakpoint mutations answer with the resulting snapshot, not `ok`."""
    if result[0] == "error":
        raise AssertionError(f"{context}: {result}")
    return result


def clear_breakpoints(client):
    require_accepted(client.cmd("break-clear-all"), "break-clear-all")


def create_breakpoint(client, definition):
    return require_accepted(
        client.cmd(f"break-create {definition}"),
        f"break-create {definition}")


def break_list(client):
    """Return break-list records as a list of key->value dicts."""
    result = client.cmd("break-list")
    if result[0] != "data":
        raise AssertionError(f"break-list: {result}")
    records = []
    for line in result[2].decode("latin1").splitlines():
        if not line.strip():
            continue
        fields = {}
        for token in line.split():
            key, _, value = token.partition("=")
            fields[key] = value
        records.append(fields)
    return records


def count_hits(client, definition, cycles=30000):
    """Arm one count-only breakpoint, run a fixed number of cycles, read hits."""
    clear_breakpoints(client)
    create_breakpoint(client, f"{definition} actions=none")
    require_ok(client.cmd(f"run-cycles {cycles}"), "run-cycles")
    require_ok(client.cmd("wait-paused 10000"), "wait-paused")
    records = break_list(client)
    assert len(records) == 1, f"expected one breakpoint, got {records}"
    return int(records[0]["hits"])


def test_guard_gates_hit_counting(client):
    """hits= must advance only when the guard also holds."""
    unguarded = count_hits(client, "write $C000")
    assert unguarded > 0, "unguarded watchpoint never fired"

    guarded_06 = count_hits(client, "write $C000 when=value==$06")
    assert guarded_06 > 0, "guard on the written byte never fired"

    guarded_42 = count_hits(client, "write $C000 when=value==$42")
    assert guarded_42 == 0, (
        f"guard that can never hold counted {guarded_42} hits")

    # The loop writes $00 and $06 once each per iteration, so the guarded count
    # is half the unguarded count (allow one iteration of slop at the edges).
    assert abs(unguarded - 2 * guarded_06) <= 2, (
        f"expected unguarded ({unguarded}) to be twice guarded "
        f"({guarded_06})")


def test_guard_gates_the_stop(client):
    """A guard that holds must stop the machine; one that cannot must not."""
    clear_breakpoints(client)
    create_breakpoint(client, "write $C000 when=value==$06")
    require_ok(client.cmd("run"), "run")
    body = require_ok(client.cmd("wait-paused 10000"), "wait-paused (guarded)")
    assert "stop=breakpoint" in body, f"expected a breakpoint stop, got {body}"
    # The write that matched carried $06, which LDA #$06 had just loaded.
    cpu = require_ok(client.cmd("get-cpu"), "get-cpu")
    assert "a=06" in cpu, f"expected a=06 at the guarded stop, got {cpu}"

    # A guard that can never hold must leave the machine running.
    clear_breakpoints(client)
    create_breakpoint(client, "write $C000 when=value==$42")
    require_ok(client.cmd("run"), "run")
    result = client.cmd("wait-paused 1500")
    assert result[0] == "error" and "timeout" in result[1], (
        f"impossible guard stopped the machine: {result}")
    require_ok(client.cmd("pause"), "pause")
    require_ok(client.cmd("wait-paused 10000"), "wait-paused (after pause)")


def test_break_list_echoes_the_guard(client):
    clear_breakpoints(client)
    create_breakpoint(client, "write $C000 when=value!&1,mem($C000)>$F0")
    records = break_list(client)
    assert len(records) == 1, f"expected one breakpoint, got {records}"
    assert records[0]["cond"] == "2", f"expected cond=2, got {records[0]}"
    when = records[0]["when"]
    assert "value!&$1" in when and "mem($C000)>$F0" in when, (
        f"break-list did not echo the guard: {when!r}")

    # An unguarded breakpoint reports no terms.
    clear_breakpoints(client)
    create_breakpoint(client, "write $C000")
    records = break_list(client)
    assert records[0]["cond"] == "0", f"expected cond=0, got {records[0]}"
    assert records[0]["when"] == "", f"expected empty when=, got {records[0]}"


def test_invalid_guards_are_rejected(client):
    """Each rejection must name the actual problem, not just `bad-args`."""
    clear_breakpoints(client)
    for definition, expected in (
        ("write $C000 when=bogus==1", "unknown condition term"),
        ("write $C000 when=a=1", "unknown condition operator"),
        ("write $C000 when=", "empty condition"),
        ("write $C000 when=a==70000", "16-bit immediate"),
        ("write $C000 when=a==1,x==2,y==3,i==1,c==1", "too many condition terms"),
        ("write $C000 when=mem()>1", "mem() needs a 16-bit address"),
        ("exec $E000 when=value==1", "no meaning on an exec breakpoint"),
    ):
        body = require_error(client.cmd(f"break-create {definition}"),
                             f"break-create {definition}")
        assert expected in body, (
            f"{definition!r}: expected a diagnostic mentioning {expected!r}, "
            f"got {body!r}")
    assert break_list(client) == [], "a rejected definition created a breakpoint"


def test_unguarded_behavior_is_unchanged(client):
    """An unguarded breakpoint must behave exactly as it did before guards."""
    clear_breakpoints(client)
    create_breakpoint(client, "write $C000")
    require_ok(client.cmd("run"), "run")
    body = require_ok(client.cmd("wait-paused 10000"), "wait-paused")
    assert "stop=breakpoint" in body, f"expected a breakpoint stop, got {body}"


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: test_guarded_breakpoint_control.py C64M_EXE REPO_ROOT")
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    repo_root = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(repo_root / "tools"))
    from c64_control_client import Ctl

    with tempfile.TemporaryDirectory(prefix="c64m-guarded-bp-") as temp:
        directory = pathlib.Path(temp)
        write_roms(directory)
        process, port = start_emulator(executable, directory)
        try:
            client = Ctl(port=port, timeout=15.0)
            try:
                assert "protocol=C64M/7" in require_ok(
                    client.cmd("hello"), "hello")
                test_invalid_guards_are_rejected(client)
                test_break_list_echoes_the_guard(client)
                test_guard_gates_hit_counting(client)
                test_guard_gates_the_stop(client)
                test_unguarded_behavior_is_unchanged(client)
            finally:
                client.close()
        finally:
            stop_emulator(process)
    print("test_guarded_breakpoint_control: ok")


if __name__ == "__main__":
    main()
