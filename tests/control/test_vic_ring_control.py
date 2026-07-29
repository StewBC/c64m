#!/usr/bin/env python3
"""End-to-end test for the per-line VIC ring.

The frame ring says which frame is wrong; this ring says why. Its whole reason
for existing is state that CPU writes cannot reveal - above all the sprite X
(including the $D010 MSB) that was actually latched for painting a line, which
need not match what $D000..$D010 hold afterwards. That disagreement is the
classic "sprite appears at the left edge for one frame" bug.

The stub KERNAL enables sprite 0 at X low byte $50 and then toggles the $D010
MSB once per frame, so the latched X alternates $0150 / $0050 across frames
while the live register pair settles on one value. A ring that read the live
registers instead of the per-line latch would report the same X every frame and
fail here.

    E000  LDA #$01 / STA $D015     enable sprite 0
    E005  LDA #$50 / STA $D000     X low byte
    E00A  LDA #$32 / STA $D001     Y = 50
    E00F  LDA #$FF / CMP $D012 / BNE   wait for raster $FF
    E016  LDA $D010 / EOR #$01 / STA $D010   toggle the X MSB
    E01E  LDA #$FF / CMP $D012 / BEQ   wait until the raster leaves $FF
    E025  JMP $E00F
"""
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


LOOP = bytes([
    0xA9, 0x01, 0x8D, 0x15, 0xD0,          # LDA #$01 ; STA $D015
    0xA9, 0x50, 0x8D, 0x00, 0xD0,          # LDA #$50 ; STA $D000
    0xA9, 0x32, 0x8D, 0x01, 0xD0,          # LDA #$32 ; STA $D001
    0xA9, 0xFF,                            # E00F LDA #$FF
    0xCD, 0x12, 0xD0,                      # CMP $D012
    0xD0, 0xFB,                            # BNE -5
    0xAD, 0x10, 0xD0,                      # LDA $D010
    0x49, 0x01,                            # EOR #$01
    0x8D, 0x10, 0xD0,                      # STA $D010
    0xA9, 0xFF,                            # LDA #$FF
    0xCD, 0x12, 0xD0,                      # CMP $D012
    0xF0, 0xFB,                            # BEQ -5
    0x4C, 0x0F, 0xE0,                      # JMP $E00F
])


def reserve_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def write_roms(directory):
    system = bytearray([0xEA] * 16384)
    system[8192:8192 + len(LOOP)] = LOOP
    system[8192 + 0x1FFC] = 0x00
    system[8192 + 0x1FFD] = 0xE0
    (directory / "system.bin").write_bytes(system)
    (directory / "character.bin").write_bytes(bytes(4096))


def start_emulator(executable, directory):
    port = reserve_port()
    process = subprocess.Popen(
        [executable, "--headless", f"--control-port={port}",
         "--noini", "--nosaveini"],
        cwd=directory, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"c64m exited early: {process.stderr.read()}")
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.1).close()
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


def metadata(text):
    return dict(token.split("=", 1) for token in text.split())


def ring_info(client):
    return metadata(require_ok(client.cmd("vic-ring-info"), "vic-ring-info"))


def find(client, query=""):
    result = client.cmd(f"vic-ring-find {query}".strip())
    if result[0] != "data":
        raise AssertionError(f"vic-ring-find {query}: {result}")
    records = []
    for line in result[2].decode("latin1").splitlines():
        if line.strip():
            records.append(metadata(line))
    return records


def run_a_while(client, seconds=0.5):
    require_ok(client.cmd("set-turbo 2"), "set-turbo 2")
    require_ok(client.cmd("run"), "run")
    time.sleep(seconds)
    require_ok(client.cmd("pause"), "pause")
    require_ok(client.cmd("wait-paused 10000"), "wait-paused")


def test_ring_records_lines(client):
    run_a_while(client)
    info = ring_info(client)
    assert int(info["capacity"]) > 0, f"vic ring is disabled: {info}"
    assert int(info["count"]) > 0, f"vic ring recorded nothing: {info}"
    assert info["recording"] == "1", info
    assert int(info["newest_frame"]) > int(info["oldest_frame"]), info


def test_latched_sprite_x_differs_from_the_live_register(client):
    """The point of the ring: what was painted, not what the register says now."""
    info = ring_info(client)
    newest = int(info["newest_frame"])

    latched = []
    for frame in (newest - 3, newest - 2, newest - 1):
        records = find(client, f"frame={frame} raster=60-60")
        assert len(records) == 1, f"frame {frame} raster 60: {records}"
        record = records[0]
        assert int(record["frame"]) == frame, record
        assert int(record["raster"]) == 60, record
        # Sprite 0 is enabled and actually has data on this line.
        assert int(record["spr_en"], 16) & 1, record
        assert int(record["spr_vis"], 16) & 1, record
        latched.append(int(record["spr_x"].split(",")[0], 16))

    # The MSB toggles once per frame, so consecutive frames must disagree and
    # each X must be one of the two possible latched values.
    assert set(latched) == {0x0050, 0x0150}, (
        f"latched sprite X did not alternate with the $D010 toggle: "
        f"{[hex(x) for x in latched]}")
    assert latched[0] != latched[1] and latched[1] != latched[2], (
        f"expected alternation, got {[hex(x) for x in latched]}")

    # The live registers report only one of those, so a ring that sampled the
    # registers instead of the per-line latch could not have produced both.
    low = client.mem(0xD000, 1, "map")[0]
    msb = client.mem(0xD010, 1, "map")[0]
    live_x = low | ((msb & 1) << 8)
    assert live_x in (0x0050, 0x0150), hex(live_x)
    assert any(x != live_x for x in latched), (
        f"every latched X matched the live register {hex(live_x)}; the ring is "
        f"reporting shadow state rather than what was painted")


def test_frame_and_raster_filters(client):
    info = ring_info(client)
    frame = int(info["newest_frame"]) - 1

    whole = find(client, f"frame={frame} limit=2048")
    # A full frame is every raster line of the standard, in order, exactly once.
    assert len(whole) in (263, 312), f"unexpected line count {len(whole)}"
    rasters = [int(r["raster"]) for r in whole]
    assert rasters == sorted(rasters), "records are not in raster order"
    assert len(set(rasters)) == len(rasters), "duplicate raster lines"
    assert rasters[0] == 0 and rasters[-1] == len(whole) - 1, rasters[:3]
    assert all(int(r["frame"]) == frame for r in whole)

    window = find(client, f"frame={frame} raster=100-109")
    assert [int(r["raster"]) for r in window] == list(range(100, 110)), window

    single = find(client, f"frame={frame} raster=42")
    assert len(single) == 1 and int(single[0]["raster"]) == 42, single

    # Without a frame filter the same raster matches in every retained frame.
    across = find(client, "raster=50-50 limit=5")
    assert len(across) == 5, len(across)
    frames = [int(r["frame"]) for r in across]
    assert frames == sorted(frames) and len(set(frames)) == 5, frames

    limited = find(client, f"frame={frame} limit=7")
    assert len(limited) == 7, len(limited)


def test_records_carry_cross_reference_and_state(client):
    info = ring_info(client)
    frame = int(info["newest_frame"]) - 1
    records = find(client, f"frame={frame} raster=0-4")

    previous_cycle = None
    for record in records:
        # machine_cycle is the shared axis with the frame ring and recorder.
        cycle = int(record["cycle"])
        if previous_cycle is not None:
            assert cycle > previous_cycle, "cycles must advance per line"
        previous_cycle = cycle
        for key in ("badline", "display", "vborder", "d011", "d016", "d018",
                    "vc", "vcbase", "rc", "border", "irq",
                    "spr_ptr", "spr_col", "spr_mcnt", "spr_mcbase"):
            assert key in record, f"missing {key} in {record}"
        assert len(record["spr_x"].split(",")) == 8, record["spr_x"]
        assert len(record["spr_y"].split(",")) == 8, record["spr_y"]


def test_bad_args(client):
    for bad in ("vic-ring-find bogus=1", "vic-ring-find frame=",
                "vic-ring-find raster=zz", "vic-ring-find limit=0",
                "vic-ring-find limit=99999", "vic-ring-record maybe"):
        require_error(client.cmd(bad), bad)


def test_record_toggle_and_clear(client):
    require_ok(client.cmd("vic-ring-record off"), "vic-ring-record off")
    assert ring_info(client)["recording"] == "0"
    before = int(ring_info(client)["count"])
    run_a_while(client, 0.3)
    assert int(ring_info(client)["count"]) == before, "kept recording while off"

    require_ok(client.cmd("vic-ring-record on"), "vic-ring-record on")
    run_a_while(client, 0.3)
    assert int(ring_info(client)["count"]) > before

    require_ok(client.cmd("vic-ring-clear"), "vic-ring-clear")
    cleared = ring_info(client)
    assert int(cleared["count"]) == 0, cleared
    assert int(cleared["dropped"]) == 0, cleared
    assert int(cleared["capacity"]) > 0, "clear must not disable the ring"
    assert find(client, "limit=10") == [], "cleared ring still returns records"


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_vic_ring_control.py C64M_EXE REPO_ROOT")
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    repo_root = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(repo_root / "tools"))
    from c64_control_client import Ctl

    with tempfile.TemporaryDirectory(prefix="c64m-vic-ring-") as temp:
        directory = pathlib.Path(temp)
        write_roms(directory)
        process, port = start_emulator(executable, directory)
        try:
            client = Ctl(port=port, timeout=20.0)
            try:
                assert "vic-ring" in require_ok(
                    client.cmd("capabilities"), "capabilities")
                test_ring_records_lines(client)
                test_latched_sprite_x_differs_from_the_live_register(client)
                test_frame_and_raster_filters(client)
                test_records_carry_cross_reference_and_state(client)
                test_bad_args(client)
                test_record_toggle_and_clear(client)
            finally:
                client.close()
        finally:
            stop_emulator(process)
    print("test_vic_ring_control: ok")


if __name__ == "__main__":
    main()
