#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Two time constants through a step response from `star-loopback --step` (docs/measurements.md §13).

    build/linux/app/star-loopback cal.json --step --range 0.18 | python3 tools/step_fit.py

Fits 1 - level = a1 exp(-t/tau1) + a2 exp(-t/tau2) to the level table of each channel, on a slow square wave with a
probe that has a trimmer: tau1 is the input's part, tau2 the trimmer's. A negative a2 is a trace that overshoots.
"""
import math
import sys


def solve(times, levels, tau1, tau2):
    """Least squares for a1, a2 with both time constants given."""
    s11 = s12 = s22 = b1 = b2 = 0.0
    for t, y in zip(times, levels):
        e1, e2 = math.exp(-t / tau1), math.exp(-t / tau2)
        s11 += e1 * e1
        s12 += e1 * e2
        s22 += e2 * e2
        b1 += e1 * (1 - y)
        b2 += e2 * (1 - y)
    det = s11 * s22 - s12 * s12
    a1 = (b1 * s22 - b2 * s12) / det
    a2 = (b2 * s11 - b1 * s12) / det
    err = sum((1 - y - a1 * math.exp(-t / tau1) - a2 * math.exp(-t / tau2)) ** 2 for t, y in zip(times, levels))
    return err, a1, a2


def fit(times, levels):
    best = None
    tau1 = 3.0
    while tau1 < 40:
        tau2 = 50.0
        while tau2 < 400:
            err, a1, a2 = solve(times, levels, tau1, tau2)
            if best is None or err < best[0]:
                best = (err, a1, tau1, a2, tau2)
            tau2 += 2
        tau1 += 0.25
    return best


def main():
    times = None
    for line in sys.stdin:
        words = line.split()
        if line.lstrip().startswith("t µs"):
            times = [float(w) for w in words[2:]]
        elif times and words and words[0] in ("CH1", "CH2") and len(words) == len(times) + 1:
            pairs = [(t, float(w)) for t, w in zip(times, words[1:]) if w != "-"]
            err, a1, tau1, a2, tau2 = fit([p[0] for p in pairs], [p[1] for p in pairs])
            print(f"{words[0]}: {a1 * 100:.1f} % with {tau1:.1f} µs, {a2 * 100:+.1f} % with {tau2:.0f} µs; "
                  f"rms error {math.sqrt(err / len(pairs)) * 100:.2f} %")


if __name__ == "__main__":
    main()
