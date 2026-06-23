#!/usr/bin/env python3
"""Run c64m long enough to capture a timed SID WAV, then stop it."""

import argparse
import os
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description="Capture c64m SID output with a timer")
    parser.add_argument("--emulator", default="./build/c64m")
    parser.add_argument("--prg", default="./samples/el_cartero.prg")
    parser.add_argument("--out", default="./build/sid-el-cartero.wav")
    parser.add_argument("--warmup", type=float, default=9.5)
    parser.add_argument("--duration", type=float, default=4.0)
    parser.add_argument("--guard", type=float, default=1.0)
    parser.add_argument("--no-autorun", action="store_true")
    parser.add_argument("extra_args", nargs="*", help="extra arguments passed to c64m before capture options")
    args = parser.parse_args()

    command = [args.emulator]
    if not args.no_autorun:
        command.append("-a")
    command.extend(["-p", args.prg])
    command.extend(args.extra_args)
    command.extend([
        "--audio-record",
        args.out,
        "--audio-record-start",
        str(args.warmup),
        "--audio-record-duration",
        str(args.duration),
    ])

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    try:
        os.remove(args.out)
    except FileNotFoundError:
        pass

    process = subprocess.Popen(command)
    kill_after = args.warmup + args.duration + args.guard
    deadline = time.monotonic() + kill_after

    try:
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)
    except KeyboardInterrupt:
        if process.poll() is None:
            process.terminate()
        raise

    if not os.path.exists(args.out):
        print(f"capture failed: {args.out} was not produced", file=sys.stderr)
        return 1
    if os.path.getsize(args.out) <= 44:
        print(f"capture failed: {args.out} contains no audio samples", file=sys.stderr)
        return 1

    print(f"captured {args.out} ({os.path.getsize(args.out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
