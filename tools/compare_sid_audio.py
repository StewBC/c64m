#!/usr/bin/env python3
"""Compare a candidate SID recording against a reference audio file.

This tool intentionally depends on ffmpeg for decoding/resampling and otherwise
uses only the Python standard library. It is a regression aid, not a bit-perfect
audio oracle.
"""

import argparse
import json
import math
import struct
import subprocess
import sys


DEFAULT_SAMPLE_RATE = 44100
DEFAULT_BLOCK_SAMPLES = 441
DEFAULT_MAX_LAG_BLOCKS = 400
SPECTRAL_BANDS = [
    (20, 100),
    (100, 250),
    (250, 500),
    (500, 1000),
    (1000, 2000),
    (2000, 4000),
    (4000, 8000),
    (8000, 16000),
    (16000, 22050),
]


def decode_mono_f32(path, sample_rate):
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        path,
        "-ac",
        "1",
        "-ar",
        str(sample_rate),
        "-f",
        "f32le",
        "-",
    ]
    try:
        proc = subprocess.run(cmd, check=True, stdout=subprocess.PIPE)
    except FileNotFoundError:
        raise SystemExit("ffmpeg not found; install ffmpeg to decode audio inputs")
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"ffmpeg failed for {path}: exit {exc.returncode}")

    data = proc.stdout
    count = len(data) // 4
    if count == 0:
        raise SystemExit(f"{path}: decoded no samples")
    return list(struct.unpack(f"<{count}f", data[: count * 4]))


def basic_stats(samples, sample_rate):
    count = len(samples)
    mean = sum(samples) / count
    rms = math.sqrt(sum(v * v for v in samples) / count)
    peak = max(abs(v) for v in samples)
    return {
        "samples": count,
        "duration_seconds": count / sample_rate,
        "sample_rate": sample_rate,
        "mean_dc": mean,
        "rms": rms,
        "peak": peak,
        "crest_factor": peak / rms if rms > 0 else 0.0,
    }


def rms_envelope(samples, block_samples):
    env = []
    for offset in range(0, len(samples) - block_samples + 1, block_samples):
        block = samples[offset : offset + block_samples]
        env.append(math.sqrt(sum(v * v for v in block) / block_samples))
    return env


def normalized_correlation(a, b):
    if len(a) == 0 or len(b) == 0 or len(a) != len(b):
        return 0.0
    am = sum(a) / len(a)
    bm = sum(b) / len(b)
    aa = [v - am for v in a]
    bb = [v - bm for v in b]
    denom = math.sqrt(sum(v * v for v in aa) * sum(v * v for v in bb))
    if denom == 0.0:
        return 0.0
    return sum(x * y for x, y in zip(aa, bb)) / denom


def best_envelope_alignment(reference, candidate, block_samples, max_lag_blocks):
    ref_env = rms_envelope(reference, block_samples)
    cand_env = rms_envelope(candidate, block_samples)
    best = {"lag_blocks": 0, "correlation": -1.0, "blocks": 0}

    for lag in range(-max_lag_blocks, max_lag_blocks + 1):
        if lag < 0:
            ref_slice = ref_env[-lag:]
            cand_slice = cand_env[: len(ref_slice)]
        else:
            ref_slice = ref_env[: max(0, len(cand_env) - lag)]
            cand_slice = cand_env[lag : lag + len(ref_slice)]
        blocks = min(len(ref_slice), len(cand_slice))
        if blocks < 8:
            continue
        corr = normalized_correlation(ref_slice[:blocks], cand_slice[:blocks])
        if corr > best["correlation"]:
            best = {"lag_blocks": lag, "correlation": corr, "blocks": blocks}

    return best


def aligned_overlap(reference, candidate, lag_samples):
    if lag_samples < 0:
        ref = reference[-lag_samples:]
        cand = candidate[: len(ref)]
    else:
        ref = reference[: max(0, len(candidate) - lag_samples)]
        cand = candidate[lag_samples : lag_samples + len(ref)]
    count = min(len(ref), len(cand))
    return ref[:count], cand[:count]


def best_gain(reference, candidate):
    denom = sum(v * v for v in candidate)
    if denom == 0.0:
        return 0.0
    return sum(r * c for r, c in zip(reference, candidate)) / denom


def fft_magnitudes(samples):
    n = 1
    while n * 2 <= len(samples):
        n *= 2
    if n < 16:
        return [], 0

    work = []
    for i, value in enumerate(samples[:n]):
        window = 0.5 - 0.5 * math.cos((2.0 * math.pi * i) / (n - 1))
        work.append(complex(value * window, 0.0))

    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            work[i], work[j] = work[j], work[i]

    step = 2
    while step <= n:
        theta = -2.0 * math.pi / step
        w_step = complex(math.cos(theta), math.sin(theta))
        half = step // 2
        for start in range(0, n, step):
            w = 1.0 + 0.0j
            for k in range(half):
                even = work[start + k]
                odd = w * work[start + k + half]
                work[start + k] = even + odd
                work[start + k + half] = even - odd
                w *= w_step
        step *= 2

    return [abs(v) for v in work[: n // 2]], n


def spectral_metrics(samples, sample_rate):
    max_samples = min(len(samples), 65536)
    mags, n = fft_magnitudes(samples[:max_samples])
    if not mags:
        return {"centroid_hz": 0.0, "bands": []}

    powers = [m * m for m in mags]
    total = sum(powers[1:]) or 1.0
    centroid = sum((i * sample_rate / n) * powers[i] for i in range(1, len(powers))) / total
    bands = []
    for lo, hi in SPECTRAL_BANDS:
        start = max(1, int(lo * n / sample_rate))
        end = min(len(powers), int(hi * n / sample_rate))
        power = sum(powers[start:end])
        bands.append({
            "low_hz": lo,
            "high_hz": hi,
            "relative_db": 10.0 * math.log10((power / total) + 1e-20),
        })
    return {"centroid_hz": centroid, "bands": bands}


def spectral_band_error(reference_bands, candidate_bands):
    pairs = zip(reference_bands, candidate_bands)
    diffs = [abs(a["relative_db"] - b["relative_db"]) for a, b in pairs]
    return sum(diffs) / len(diffs) if diffs else 0.0


def scalar_score(ref_stats, cand_stats, alignment, band_error):
    ref_rms = ref_stats["rms"]
    cand_rms = cand_stats["rms"]
    if ref_rms > 0 and cand_rms > 0:
        rms_error = abs(math.log(cand_rms / ref_rms))
    else:
        rms_error = 10.0
    dc_error = abs(cand_stats["mean_dc"])
    corr_error = 1.0 - max(-1.0, min(1.0, alignment["correlation"]))
    return corr_error * 4.0 + rms_error * 1.5 + dc_error * 20.0 + band_error * 0.15


def main():
    parser = argparse.ArgumentParser(description="Compare SID audio against a reference")
    parser.add_argument("--reference", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--out")
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)
    parser.add_argument("--max-score", type=float)
    parser.add_argument("--baseline-metrics", help="prior metrics JSON used with --max-score-regression")
    parser.add_argument("--max-score-regression", type=float, help="fail if score exceeds baseline score by more than this")
    args = parser.parse_args()

    if args.max_score_regression is not None and not args.baseline_metrics:
        parser.error("--max-score-regression requires --baseline-metrics")

    reference = decode_mono_f32(args.reference, args.sample_rate)
    candidate = decode_mono_f32(args.candidate, args.sample_rate)

    alignment = best_envelope_alignment(
        reference,
        candidate,
        DEFAULT_BLOCK_SAMPLES,
        DEFAULT_MAX_LAG_BLOCKS,
    )
    lag_samples = alignment["lag_blocks"] * DEFAULT_BLOCK_SAMPLES
    ref_aligned, cand_aligned = aligned_overlap(reference, candidate, lag_samples)

    ref_stats = basic_stats(reference, args.sample_rate)
    cand_stats = basic_stats(candidate, args.sample_rate)
    ref_aligned_stats = basic_stats(ref_aligned, args.sample_rate) if ref_aligned else {}
    cand_aligned_stats = basic_stats(cand_aligned, args.sample_rate) if cand_aligned else {}
    gain = best_gain(ref_aligned, cand_aligned) if ref_aligned else 0.0
    ref_spectrum = spectral_metrics(ref_aligned or reference, args.sample_rate)
    cand_spectrum = spectral_metrics(cand_aligned or candidate, args.sample_rate)
    band_error = spectral_band_error(ref_spectrum["bands"], cand_spectrum["bands"])
    score = scalar_score(ref_stats, cand_stats, alignment, band_error)

    result = {
        "reference": args.reference,
        "candidate": args.candidate,
        "reference_stats": ref_stats,
        "candidate_stats": cand_stats,
        "aligned_reference_stats": ref_aligned_stats,
        "aligned_candidate_stats": cand_aligned_stats,
        "alignment": {
            "lag_blocks": alignment["lag_blocks"],
            "lag_seconds": lag_samples / args.sample_rate,
            "correlation": alignment["correlation"],
            "blocks": alignment["blocks"],
        },
        "best_gain_candidate_to_reference": gain,
        "reference_spectrum": ref_spectrum,
        "candidate_spectrum": cand_spectrum,
        "spectral_band_mean_abs_db_error": band_error,
        "score": score,
        "score_note": "Lower is better; weighted envelope, RMS, DC, and spectral-band error.",
    }

    baseline_score = None
    if args.baseline_metrics:
        with open(args.baseline_metrics, "r", encoding="utf-8") as handle:
            baseline = json.load(handle)
        baseline_score = float(baseline["score"])
        result["baseline_metrics"] = args.baseline_metrics
        result["baseline_score"] = baseline_score
        result["score_delta_from_baseline"] = score - baseline_score

    text = json.dumps(result, indent=2, sort_keys=True)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text)
            handle.write("\n")
    print(text)

    if args.max_score is not None and score > args.max_score:
        return 1
    if args.max_score_regression is not None and baseline_score is not None:
        if score > baseline_score + args.max_score_regression:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
