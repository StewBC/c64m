#!/usr/bin/env python3
"""Headless control-port latency probe for M1 / M2 / M3 / batch.

Usage:
  python3 tools/measure_control_latency.py --bin ./build/c64m --port 18765 --label tip
"""
from __future__ import annotations

import argparse
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from c64_control_client import Ctl  # noqa: E402


def pct(sorted_vals, p):
    if not sorted_vals:
        return float("nan")
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(sorted_vals) - 1)
    if f == c:
        return sorted_vals[f]
    return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)


def rtt_ms(fn, n, warmup=5):
    for _ in range(warmup):
        fn()
    samples = []
    for _ in range(n):
        t0 = time.perf_counter()
        fn()
        samples.append((time.perf_counter() - t0) * 1000.0)
    samples.sort()
    return {
        "n": n,
        "mean": statistics.fmean(samples),
        "p50": pct(samples, 50),
        "p99": pct(samples, 99),
        "min": samples[0],
        "max": samples[-1],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="./build/c64m")
    ap.add_argument("--port", type=int, default=18765)
    ap.add_argument("--label", default="measure")
    ap.add_argument("--get-cpu-n", type=int, default=100)
    args = ap.parse_args()

    bin_path = Path(args.bin)
    if not bin_path.is_file():
        print(f"binary not found: {bin_path}", file=sys.stderr)
        return 1

    proc = subprocess.Popen(
        [str(bin_path), "--headless", f"--control-port={args.port}", "--pal"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        cwd=str(Path(__file__).resolve().parents[1]),
    )
    try:
        c = None
        last_err = None
        for _ in range(50):
            try:
                c = Ctl(port=args.port, timeout=10.0)
                break
            except OSError as e:
                last_err = e
                time.sleep(0.05)
        if c is None:
            print(f"connect failed: {last_err}", file=sys.stderr)
            return 1

        c.ok("pause")
        time.sleep(0.05)
        try:
            c.cmd("get-vic")
        except Exception:
            pass
        time.sleep(0.05)

        hello = c.cmd("hello")
        print(f"LABEL={args.label}")
        print(f"HELLO={hello}")

        m1 = rtt_ms(lambda: c.cmd("get-cpu"), args.get_cpu_n)
        print(
            f"M1 get-cpu n={m1['n']} mean_ms={m1['mean']:.3f} "
            f"p50_ms={m1['p50']:.3f} p99_ms={m1['p99']:.3f} "
            f"min_ms={m1['min']:.3f} max_ms={m1['max']:.3f}"
        )

        try:
            c.cmd("get-frame format=indexed8")
        except Exception:
            pass
        time.sleep(0.02)
        try:
            m2 = rtt_ms(lambda: c.cmd("get-frame format=indexed8"), 30, warmup=2)
            print(
                f"M2 get-frame n={m2['n']} mean_ms={m2['mean']:.3f} "
                f"p50_ms={m2['p50']:.3f} p99_ms={m2['p99']:.3f}"
            )
        except Exception as e:
            print(f"M2 get-frame FAILED {e}")

        # M3: try bulk 65536, else 64x1024
        try:
            t0 = time.perf_counter()
            r = c.cmd("get-memory $0000 65536 ram")
            if r[0] != "data" or len(r[2]) != 65536:
                raise RuntimeError(f"bulk not accepted: {r[0] if isinstance(r, tuple) else r}")
            bulk_ms = (time.perf_counter() - t0) * 1000.0
            print(f"M3 64K memory mode=bulk wall_ms={bulk_ms:.3f} bytes={len(r[2])}")
        except Exception:
            t0 = time.perf_counter()
            total = 0
            for off in range(0, 65536, 1024):
                r = c.cmd(f"get-memory ${off:04X} 1024 ram")
                if r[0] != "data":
                    raise RuntimeError(f"chunk failed at {off}: {r}")
                total += len(r[2])
            bulk_ms = (time.perf_counter() - t0) * 1000.0
            print(
                f"M3 64K memory mode=chunked64x1024 wall_ms={bulk_ms:.3f} bytes={total}"
            )

        try:
            cmds = ["get-cpu"] * 16
            t0 = time.perf_counter()
            if hasattr(c, "pipeline"):
                res = c.pipeline(cmds)
            else:
                res = [c.cmd(x) for x in cmds]
            batch_ms = (time.perf_counter() - t0) * 1000.0
            ok = sum(1 for r in res if r[0] == "ok")
            print(
                f"M1_batch N=16 wall_ms={batch_ms:.3f} ok={ok} "
                f"serial_equiv_mean_ms={m1['mean'] * 16:.3f} "
                f"speedup_vs_serial_mean={(m1['mean'] * 16) / batch_ms if batch_ms > 0 else float('nan'):.2f}x"
            )
        except Exception as e:
            print(f"M1_batch FAILED {e}")

        c.close()
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
