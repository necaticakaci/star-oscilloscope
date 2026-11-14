#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads a CSV export of the vendor software (Multi VirAnalyzer, File -> Save; docs/measurements.md §13).

    python3 tools/vendor_csv.py capture.csv [--sine HZ | --step]

The file starts with `sample,<rate>,length,<n>` and `Vol(V),Vol(V)`, then one row of volts per sample for CH1 and CH2.
Prints, per channel: the levels, the smallest step between them (the lsb, which names the gain stage in
docs/protocol.md §8), mean and AC RMS.

With --sine, fits a sine near HZ to each channel over the whole capture. Gaps in the data show as residuals much
larger than one lsb in the chunks after the gap; a contiguous capture stays within about one lsb everywhere.
Pure Python: meant for captures up to some 10^4 samples with --sine.

With --step, on a square wave: the mean level after a rising edge as a share of the step, in the table that
`star-loopback --step` prints (docs/measurements.md §13), so that tools/step_fit.py reads both:

    python3 tools/vendor_csv.py square.csv --step | python3 tools/step_fit.py
"""
import argparse
import math


def read(path):
    with open(path, encoding="latin-1") as f:
        head = [p.strip() for p in f.readline().split(",")]
        f.readline()
        channels = [[], []]
        for line in f:
            parts = line.split(",")
            if len(parts) < 2:
                continue
            channels[0].append(float(parts[0]))
            channels[1].append(float(parts[1]))
    return float(head[1]), int(head[3]), channels


def solve3(a, b):
    """Solves the 3x3 system a x = b by Cramer's rule."""
    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    d = det(a)
    out = []
    for i in range(3):
        m = [row[:] for row in a]
        for j in range(3):
            m[j][i] = b[j]
        out.append(det(m) / d)
    return out


def fit_sine(v, rate, f):
    """Least squares a cos + b sin + c at frequency f; returns (residuals, amplitude)."""
    n = len(v)
    cos = [math.cos(2 * math.pi * f * k / rate) for k in range(n)]
    sin = [math.sin(2 * math.pi * f * k / rate) for k in range(n)]
    a = [[sum(c * c for c in cos), sum(c * s for c, s in zip(cos, sin)), sum(cos)],
         [0.0, sum(s * s for s in sin), sum(sin)],
         [0.0, 0.0, float(n)]]
    a[1][0], a[2][0], a[2][1] = a[0][1], a[0][2], a[1][2]
    b = [sum(x * c for x, c in zip(v, cos)), sum(x * s for x, s in zip(v, sin)), sum(v)]
    p, q, m = solve3(a, b)
    res = [x - (p * c + q * s + m) for x, c, s in zip(v, cos, sin)]
    return res, math.hypot(p, q)


STEP_TIMES_US = (0.25, 0.5, 1, 2, 5, 10, 20, 30, 40, 60, 80, 100, 150, 200, 300, 400)


def step_response(v, rate):
    """Mean level after each rising edge as a share of the step, the base and the slow part, as star-loopback --step.

    Returns (high part in s, levels at STEP_TIMES_US with None past the high part, slow part, its time constant in s),
    or None without a square wave."""
    ordered = sorted(v)
    lo, hi = ordered[len(v) // 10], ordered[len(v) * 9 // 10]
    if hi - lo <= 0:
        return None
    mid, low_mark, high_mark = (lo + hi) / 2, lo + 0.25 * (hi - lo), lo + 0.75 * (hi - lo)
    # Crossings of the middle, each counted only after the signal has been near the other level: noise at the
    # middle does not make extra edges.
    rising, falling = [], []
    armed_rise = armed_fall = False
    for i in range(1, len(v)):
        if v[i] < low_mark:
            armed_rise = True
        elif v[i] > high_mark:
            armed_fall = True
        if armed_rise and v[i - 1] < mid <= v[i]:
            rising.append(i - 1 + (mid - v[i - 1]) / (v[i] - v[i - 1]))
            armed_rise = False
        elif armed_fall and v[i - 1] >= mid > v[i]:
            falling.append(i - 1 + (v[i - 1] - mid) / (v[i - 1] - v[i]))
            armed_fall = False
    pairs = []
    for r in rising:
        later = [f for f in falling if f > r]
        if later:
            pairs.append(later[0] - r)
    if not pairs:
        return None
    high_part = sum(pairs) / len(pairs) / rate
    size = int(high_part * rate * 0.98)

    # The base: the last fifth of every low part, which has settled.
    lows = []
    for f in falling:
        later = [r for r in rising if r > f]
        if later:
            start = int(f + 0.8 * (later[0] - f))
            lows.extend(v[start:int(later[0])])
    base = sum(lows) / len(lows)

    mean = [0.0] * size
    edges = 0
    for r in rising:
        at = int(r)
        if at + size >= len(v):
            continue
        for k in range(size):
            mean[k] += v[at + k]
        edges += 1
    mean = [m / edges for m in mean]
    tail = mean[size - size // 20:]
    top = sum(tail) / len(tail)
    step = top - base
    level = [(m - base) / step for m in mean]

    table = []
    for t in STEP_TIMES_US:
        i = int(t * 1e-6 * rate)
        table.append(level[i] if i < size else None)

    # A straight line through ln(1 - level) from 1 µs on, until what is left nears the noise.
    sx = sy = sxx = sxy = 0.0
    points = 0
    i = int(1e-6 * rate)
    while i < size and 1 - level[i] >= 0.012:
        t = i / rate
        y = math.log(1 - level[i])
        sx += t
        sy += y
        sxx += t * t
        sxy += t * y
        points += 1
        i += 1
    if points < 2:
        return high_part, table, float("nan"), float("nan")
    slope = (points * sxy - sx * sy) / (points * sxx - sx * sx)
    part = math.exp((sy - slope * sx) / points)
    return high_part, table, part, -1 / slope


def print_step(channels, rate):
    results = [step_response(v, rate) for v in channels]
    if any(r is None for r in results):
        print("no square wave on " + ", ".join(f"CH{i + 1}" for i, r in enumerate(results) if r is None))
        return
    print(f"high part {results[0][0] * 1e6:.1f} µs; level after a rising edge as a share of the step")
    print("  t µs  " + " ".join(f"{t:6.2f}" for t in STEP_TIMES_US))
    for i, (high_part, table, part, tau) in enumerate(results):
        print(f"  CH{i + 1}   " + " ".join(f"{x:6.3f}" if x is not None else f"{'-':>6}" for x in table))
        print(f"        slow part {part * 100:.1f} % of the step, time constant {tau * 1e6:.1f} µs")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("csv")
    ap.add_argument("--sine", type=float, help="fit a sine near this frequency (Hz) to check for gaps")
    ap.add_argument("--chunk", type=int, default=64, help="samples per chunk for the residual table")
    ap.add_argument("--step", action="store_true", help="step response of a square wave on both channels")
    args = ap.parse_args()

    rate, length, channels = read(args.csv)
    print(f"rate {rate:.0f} Sa/s, length {length} samples ({len(channels[0])} rows), {len(channels[0]) / rate * 1e3:.3f} ms")
    if args.step:
        print_step(channels, rate)
        return
    for i, v in enumerate(channels):
        n = len(v)
        levels = sorted(set(v))
        steps = sorted({round(b - a, 6) for a, b in zip(levels, levels[1:])})
        mean = sum(v) / n
        ac = math.sqrt(sum((x - mean) ** 2 for x in v) / n)
        lsb = steps[0] if steps else float("nan")
        print(f"CH{i + 1}: {len(levels)} levels, lsb {lsb * 1e3:.3f} mV, min {min(v):.4f} max {max(v):.4f} V, "
              f"mean {mean:.4f} V, AC RMS {ac:.4f} V")
        if args.sine:
            # Scan the frequency in 10 ppm steps around the given value; the DDS and the sample clock differ by ~100 ppm.
            best = None
            for d in range(-300, 301, 10):
                f = args.sine * (1 + d * 1e-6)
                res, amp = fit_sine(v, rate, f)
                rms = math.sqrt(sum(r * r for r in res) / n)
                if best is None or rms < best[0]:
                    best = (rms, f, res, amp)
            rms, f, res, amp = best
            chunks = [max(abs(r) for r in res[k:k + args.chunk]) / lsb for k in range(0, n, args.chunk)]
            print(f"  sine {f:.1f} Hz, {2 * amp:.4f} Vpp, residual RMS {rms * 1e3:.1f} mV ({rms / lsb:.2f} lsb)")
            print(f"  max |residual| per {args.chunk}-sample chunk, in lsb: " + " ".join(f"{c:.1f}" for c in chunks))


if __name__ == "__main__":
    main()
