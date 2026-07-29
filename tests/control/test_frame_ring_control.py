#!/usr/bin/env python3
"""End-to-end test for the frame ring over the control port.

The ring exists so a one-frame glitch survives a human pause seconds later.
Proving that needs frames that are actually distinguishable from one another,
so the stub KERNAL runs a program that bumps the border colour exactly once per
frame:

    E000  LDA #$FF
    E002  CMP $D012      ; wait for raster $FF
    E005  BNE $E002
    E007  INC $D020      ; bump the border once per frame
    E00A  CMP $D012
    E00D  BEQ $E00A      ; wait until the raster leaves $FF
    E00F  JMP $E000

Frame N therefore has border colour (N + k) mod 16, so a lookup that returns the
wrong frame - or a ring that stores the same frame repeatedly - is visible in
the pixels rather than only in the metadata.
"""
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


LOOP = bytes([
    0xA9, 0xFF,             # LDA #$FF
    0xCD, 0x12, 0xD0,       # CMP $D012
    0xD0, 0xFB,             # BNE -5
    0xEE, 0x20, 0xD0,       # INC $D020
    0xCD, 0x12, 0xD0,       # CMP $D012
    0xF0, 0xFB,             # BEQ -5
    0x4C, 0x00, 0xE0,       # JMP $E000
])

# A row inside the top border, well above the display window in both standards.
BORDER_ROW = 10
BORDER_COL = 260

PALETTE_ARGB = [
    0xFF000000, 0xFFFFFFFF, 0xFF813338, 0xFF75CEC8,
    0xFF8E3C97, 0xFF56AC4D, 0xFF2E2C9B, 0xFFEDF171,
    0xFF8E5029, 0xFF553800, 0xFFC46C71, 0xFF4A4A4A,
    0xFF7B7B7B, 0xFFA9FF9F, 0xFF706DEB, 0xFFB2B2B2,
]
PALETTE_INDEX = {argb: index for index, argb in enumerate(PALETTE_ARGB)}


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
        [
            executable,
            "--headless",
            f"--control-port={port}",
            "--noini",
            "--nosaveini",
            "--pal",
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


def metadata(text):
    return dict(token.split("=", 1) for token in text.split())


def ring_info(client):
    return metadata(require_ok(client.cmd("frame-ring-info"), "frame-ring-info"))


def get_frame_at(client, target, kind="frame", fmt="indexed8"):
    result = client.cmd(f"get-frame-at {kind}={target} format={fmt}")
    if result[0] != "data":
        raise AssertionError(f"get-frame-at {kind}={target}: {result}")
    return metadata(result[1]), result[2]


def border_of(meta, pixels):
    width = int(meta["width"])
    return pixels[BORDER_ROW * width + BORDER_COL]


def run_a_while(client, seconds=0.6):
    require_ok(client.cmd("set-turbo 2"), "set-turbo 2")
    require_ok(client.cmd("run"), "run")
    time.sleep(seconds)
    require_ok(client.cmd("pause"), "pause")
    require_ok(client.cmd("wait-paused 10000"), "wait-paused")


def test_ring_records_frames(client):
    run_a_while(client)
    info = ring_info(client)
    assert int(info["capacity"]) > 0, f"ring is disabled: {info}"
    assert int(info["count"]) > 0, f"ring recorded nothing: {info}"
    assert info["recording"] == "1", info
    assert int(info["newest_frame"]) >= int(info["oldest_frame"]), info
    assert int(info["newest_cycle"]) >= int(info["oldest_cycle"]), info
    # Native indexed frames make the same 128 MiB budget roughly four times
    # deeper than the former ~206-slot ARGB ring.
    assert int(info["capacity"]) >= 800, info


def test_frames_are_distinct_and_correctly_indexed(client):
    """Sixteen retained frames must cover the palette in border-step order."""
    info = ring_info(client)
    newest = int(info["newest_frame"])

    previous = None
    seen = set()
    for number in range(newest - 15, newest + 1):
        meta, pixels = get_frame_at(client, number)
        assert int(meta["frame"]) == number, (
            f"asked for frame {number}, got {meta}")
        assert meta["target_kind"] == "frame", meta
        assert int(meta["target"]) == number, meta
        colour = border_of(meta, pixels)
        seen.add(colour)
        if previous is not None:
            assert (previous + 1) % 16 == colour, (
                f"frame {number} border {colour:X} does not follow "
                f"{previous:X}; the ring returned the wrong frame's pixels")
        previous = colour
    assert seen == set(range(16)), f"indexed conversion missed colours: {seen}"


def test_lookup_by_cycle(client):
    info = ring_info(client)
    newest = int(info["newest_frame"])

    by_frame_meta, by_frame_pixels = get_frame_at(client, newest)
    cycle = int(by_frame_meta["cycle"])

    # The exact cycle resolves to the same frame...
    meta, pixels = get_frame_at(client, cycle, kind="cycle")
    assert int(meta["frame"]) == newest, meta
    assert meta["target_kind"] == "cycle", meta
    assert pixels == by_frame_pixels, "same frame returned different pixels"

    # ...and so does a cycle just after it, since the lookup resolves to the
    # frame that was on screen at that moment.
    meta, _ = get_frame_at(client, cycle + 100, kind="cycle")
    assert int(meta["frame"]) == newest, meta


def test_formats_match_get_frame(client):
    """A ring frame must convert exactly like a live frame."""
    info = ring_info(client)
    newest = int(info["newest_frame"])

    argb_meta, argb = get_frame_at(client, newest, fmt="argb8888")
    idx_meta, idx = get_frame_at(client, newest, fmt="indexed8")

    assert argb_meta["format"] == "argb8888", argb_meta
    assert idx_meta["format"] == "indexed8", idx_meta
    width = int(idx_meta["width"])
    height = int(idx_meta["height"])
    assert int(idx_meta["stride"]) == width, idx_meta
    assert len(idx) == width * height, (len(idx), width, height)
    assert int(argb_meta["stride"]) == 2080, argb_meta
    assert len(argb) == height * 2080, (len(argb), height)

    argb_stride = int(argb_meta["stride"])
    for y in range(height):
        for x in range(width):
            argb_offset = y * argb_stride + x * 4
            argb_pixel = int.from_bytes(
                argb[argb_offset:argb_offset + 4], sys.byteorder)
            expected = PALETTE_INDEX.get(argb_pixel, 0)
            actual = idx[y * width + x]
            assert actual == expected, (
                f"pixel ({x},{y}) ARGB={argb_pixel:08X}: "
                f"indexed={actual:X}, expected={expected:X}")


def test_out_of_window_and_bad_args(client):
    info = ring_info(client)
    oldest = int(info["oldest_frame"])

    # A frame that has rolled out is reported missing, not substituted.
    body = require_error(client.cmd(f"get-frame-at frame={oldest - 1}"),
                         "get-frame-at below the window")
    assert "precedes the retained window" in body, body

    # A target past the newest clamps to the newest rather than failing.
    meta, _ = get_frame_at(client, 2 ** 62)
    assert int(meta["frame"]) == int(info["newest_frame"]), meta

    for bad in ("get-frame-at", "get-frame-at 123", "get-frame-at frame=",
                "get-frame-at bogus=1"):
        require_error(client.cmd(bad), bad)


def test_record_toggle_and_clear(client):
    require_ok(client.cmd("frame-ring-record off"), "frame-ring-record off")
    assert ring_info(client)["recording"] == "0"

    before = int(ring_info(client)["count"])
    run_a_while(client, 0.3)
    after = ring_info(client)
    assert int(after["count"]) == before, (
        f"ring kept recording while off: {before} -> {after}")

    require_ok(client.cmd("frame-ring-record on"), "frame-ring-record on")
    run_a_while(client, 0.3)
    assert int(ring_info(client)["count"]) > 0

    require_ok(client.cmd("frame-ring-clear"), "frame-ring-clear")
    cleared = ring_info(client)
    assert int(cleared["count"]) == 0, cleared
    assert int(cleared["dropped"]) == 0, cleared
    assert int(cleared["capacity"]) > 0, "clear must not disable the ring"
    require_error(client.cmd("get-frame-at frame=1"), "get-frame-at when empty")


def test_warp_does_not_record(client):
    """Warp turns the live renderer off, so there are no real pixels to store."""
    require_ok(client.cmd("frame-ring-clear"), "frame-ring-clear")
    require_ok(client.cmd("set-turbo 3"), "set-turbo 3")
    require_ok(client.cmd("run"), "run")
    time.sleep(0.4)
    require_ok(client.cmd("pause"), "pause")
    require_ok(client.cmd("wait-paused 10000"), "wait-paused")
    info = ring_info(client)
    assert int(info["count"]) == 0, (
        f"warp recorded geometric debug frames into the ring: {info}")

    # Dropping back to a live mode resumes recording.
    run_a_while(client, 0.3)
    assert int(ring_info(client)["count"]) > 0, "ring did not resume after warp"


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_frame_ring_control.py C64M_EXE REPO_ROOT")
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    repo_root = pathlib.Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(repo_root / "tools"))
    from c64_control_client import Ctl

    with tempfile.TemporaryDirectory(prefix="c64m-frame-ring-") as temp:
        directory = pathlib.Path(temp)
        write_roms(directory)
        process, port = start_emulator(executable, directory)
        try:
            client = Ctl(port=port, timeout=20.0)
            try:
                assert "frame-ring" in require_ok(
                    client.cmd("capabilities"), "capabilities")
                test_ring_records_frames(client)
                test_frames_are_distinct_and_correctly_indexed(client)
                test_lookup_by_cycle(client)
                test_formats_match_get_frame(client)
                test_out_of_window_and_bad_args(client)
                test_record_toggle_and_clear(client)
                test_warp_does_not_record(client)
            finally:
                client.close()
        finally:
            stop_emulator(process)
    print("test_frame_ring_control: ok")


if __name__ == "__main__":
    main()
