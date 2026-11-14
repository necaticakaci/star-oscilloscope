#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Per-stage gain and zero from a dc_logger.py run against DMM readings (docs/measurements.md).

Steady plateaus are found on the widest stage of CH1. Pass one DMM reading (volts) per plateau,
in order; '-' skips a plateau. --probe gives the probe attenuation of CH1 and CH2.

    python3 dc_gain.py LOG.jsonl                       # list the plateaus
    python3 dc_gain.py LOG.jsonl --dmm=-,0.0176,0.0888,0.2045 [--probe 1,10]
    python3 dc_gain.py LOG.jsonl --dmm=... --json DEVICE_ID.json   # calibration file for the app

--json writes the fitted zero and gain as a Star Oscilloscope calibration file (Acquire > Calibration >
Import). It needs a log whose first line names the device and its stage codes (dc_logger.py writes it).
"""
import argparse
import datetime
import json
import os

import numpy as np


def load(path):
    meta, recs = None, []
    for line in open(path):
        r = json.loads(line)
        if "meta" in r:
            meta = r["meta"]
        else:
            recs.append(r)
    return meta, recs


def plateaus(recs, threshold_mv, min_records):
    ref = np.array([r["stages"][-1][0][0] for r in recs])
    d = np.abs(np.diff(ref))
    stable = np.zeros(len(ref), bool)
    stable[1:-1] = (d[:-1] < threshold_mv) & (d[1:] < threshold_mv)
    runs, start = [], None
    for i, s in enumerate(stable):
        if s and start is None:
            start = i
        if (not s or i == len(stable) - 1) and start is not None:
            end = i if s else i - 1
            if end - start + 1 >= min_records:
                runs.append((start + 1, end))  # the first record may straddle the step
            start = None
    return runs


def mean(recs, run, stage, ch):
    seg = recs[run[0]:run[1]]
    return np.mean([r["stages"][stage][ch][0] for r in seg]), any(r["stages"][stage][ch][1] for r in seg)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--dmm", help="DMM readings in volts, one per plateau; '-' skips")
    ap.add_argument("--probe", default="1,1", help="probe attenuation of CH1,CH2")
    ap.add_argument("--threshold", type=float, default=4.0, help="max mV step between steady records")
    ap.add_argument("--min-records", type=int, default=3)
    ap.add_argument("--json", help="write a calibration file for the app")
    a = ap.parse_args()

    meta, recs = load(a.log)
    runs = plateaus(recs, a.threshold, a.min_records)
    for i, run in enumerate(runs):
        m, _ = mean(recs, run, -1, 0)
        print(f"plateau {i}: t {recs[run[0]]['t']:6.1f}-{recs[run[1]]['t']:6.1f} s  "
              f"n={run[1] - run[0]:2d}  CH1 widest stage {m:9.1f} mV")
    if not a.dmm:
        return

    dmm = a.dmm.split(",")
    if len(dmm) != len(runs):
        raise SystemExit(f"{len(runs)} plateaus but {len(dmm)} DMM readings")
    probe = [float(x) for x in a.probe.split(",")]
    points = [(float(v) * 1000, run) for v, run in zip(dmm, runs) if v != "-"]
    n_stages = len(recs[0]["stages"])
    fits = [[None, None] for _ in range(n_stages)]  # (gain, zero_mv) per stage and channel
    for ch in (0, 1):
        print(f"\nCH{ch + 1} (probe x{probe[ch]:g}): reading = gain x true + zero")
        print("  stage span mV          n    gain     zero mV   max |resid| mV")
        for k in range(n_stages):
            v, m = [], []
            for true_mv, run in points:
                value, clipped = mean(recs, run, k, ch)
                if not clipped:
                    v.append(true_mv / probe[ch])
                    m.append(value)
            label = f"{meta['stages_span_mv'][k][ch][0]:7.0f}..{meta['stages_span_mv'][k][ch][1]:6.0f}" if meta else f"#{k}"
            if len(v) < 2:
                print(f"  {label:18s} {len(v):2d}   (needs two unclipped plateaus)")
                continue
            gain, zero = np.polyfit(v, m, 1)
            resid = np.array(m) - (gain * np.array(v) + zero)
            print(f"  {label:18s} {len(v):2d}  {gain:7.4f}  {zero:9.2f}   {np.abs(resid).max():7.2f}")
            fits[k][ch] = (float(gain), float(zero))

    if a.json:
        measured = datetime.datetime.fromtimestamp(os.path.getmtime(a.log)).replace(microsecond=0)
        write_calibration(a.json, meta, fits, measured.isoformat())


def write_calibration(path, meta, fits, measured):
    """Calibration file in the app's format (app/src/calibrationstore.cpp). Unfitted stages keep factory values."""
    if not meta or "stages" not in meta:
        raise SystemExit("--json needs a log written by the current dc_logger.py (device and stage codes)")
    stages = []
    for k, codes in enumerate(meta["stages"]):
        entry = {"gain_code": codes["gain_code"], "level_code": codes["level_code"],
                 "factory_span_mv_ch1": [round(x, 1) for x in meta["stages_span_mv"][k][0]]}
        for ch in (0, 1):
            gain, zero = fits[k][ch] or (1.0, 0.0)
            entry[f"ch{ch + 1}"] = {"zero_mv": round(zero, 3), "gain": round(gain, 5)}
        stages.append(entry)
    doc = {"format": "star-oscilloscope-calibration", "version": 1, "model": meta["model"],
           "device_id": meta["device_id"], "zero_time": measured, "gain_time": measured, "stages": stages}
    with open(path, "w") as f:
        json.dump(doc, f, indent=4)
    print(f"\nwrote {path}")


if __name__ == "__main__":
    main()
